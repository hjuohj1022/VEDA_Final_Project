#include "Backend.h"

#include <QDebug>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QDate>
#include <QDir>
#include <QSet>
#include <QUrl>
#include <QUrlQuery>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QTimer>
#include <QTcpSocket>
#include <QRandomGenerator>
#include <QWebSocket>
#include <QHostAddress>
#include <QProcess>
#include <QStandardPaths>
#include <algorithm>
#include <memory>

namespace {
void removeFileWithRetry(QObject *ctx, const QString &path, int retries = 120, int intervalMs = 250) {
    if (!ctx || path.isEmpty()) {
        return;
    }
    if (QFile::remove(path)) {
        return;
    }
    if (retries <= 0) {
        return;
    }
    QTimer::singleShot(intervalMs, ctx, [ctx, path, retries, intervalMs]() {
        removeFileWithRetry(ctx, path, retries - 1, intervalMs);
    });
}

QString extractKvValue(const QString &text, const QStringList &keys) {
    for (const QString &k : keys) {
        QRegularExpression re(QString("(^|\\s|\\r|\\n)%1\\s*=\\s*([^\\r\\n]+)")
                                  .arg(QRegularExpression::escape(k)),
                              QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch m = re.match(text);
        if (m.hasMatch()) {
            return m.captured(2).trimmed();
        }
    }
    return QString();
}

QString extractJsonString(const QJsonObject &obj, const QStringList &keys) {
    for (const QString &k : keys) {
        if (obj.contains(k)) {
            const QJsonValue v = obj.value(k);
            if (v.isString()) return v.toString().trimmed();
            if (v.isDouble()) return QString::number(static_cast<qint64>(v.toDouble()));
            if (v.isBool()) return v.toBool() ? "true" : "false";
        }
    }
    return QString();
}

int parseHmsToSec(const QString &hms) {
    const QRegularExpression re("^(\\d{2}):(\\d{2}):(\\d{2})$");
    const auto m = re.match(hms.trimmed());
    if (!m.hasMatch()) return -1;
    const int h = m.captured(1).toInt();
    const int mm = m.captured(2).toInt();
    const int s = m.captured(3).toInt();
    if (h < 0 || h > 23 || mm < 0 || mm > 59 || s < 0 || s > 59) return -1;
    return (h * 3600) + (mm * 60) + s;
}

QString resolveFfmpegBinary(const QMap<QString, QString> &env) {
    const QString envBin = env.value("FFMPEG_BIN").trimmed();
    if (!envBin.isEmpty() && QFileInfo::exists(envBin)) {
        return envBin;
    }

    const QString fromPathExe = QStandardPaths::findExecutable("ffmpeg.exe");
    if (!fromPathExe.isEmpty()) return fromPathExe;

    const QString fromPath = QStandardPaths::findExecutable("ffmpeg");
    if (!fromPath.isEmpty()) return fromPath;

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + "/ffmpeg.exe",
        appDir + "/tools/ffmpeg.exe",
        appDir + "/tools/ffmpeg/bin/ffmpeg.exe",
        QDir::currentPath() + "/ffmpeg.exe",
        QDir::currentPath() + "/tools/ffmpeg.exe",
        QDir::currentPath() + "/tools/ffmpeg/bin/ffmpeg.exe"
    };
    for (const QString &c : candidates) {
        if (QFileInfo::exists(c)) return c;
    }

    // 마지막 fallback: 시스템 PATH에서 찾도록 이름 반환
    return "ffmpeg";
}
} // namespace

// SUNAPI GET 요청 생성 및 공통 응답 처리
bool Backend::sendSunapiCommand(const QString &cgiName,
                                const QMap<QString, QString> &params,
                                int cameraIndex,
                                const QString &actionLabel,
                                bool includeChannelParam) {
    if (cameraIndex < 0) {
        emit cameraControlMessage(QString("%1 failed: invalid camera index").arg(actionLabel), true);
        return false;
    }

    const QString host = m_env.value("SUNAPI_IP").trimmed();
    if (host.isEmpty()) {
        emit cameraControlMessage(QString("%1 failed: SUNAPI_IP is empty").arg(actionLabel), true);
        return false;
    }

    const QString schemeRaw = m_env.value("SUNAPI_SCHEME", "http").trimmed().toLower();
    const QString scheme = (schemeRaw == "https") ? QString("https") : QString("http");
    const int defaultPort = (scheme == "https") ? 443 : 80;
    const int port = m_env.value("SUNAPI_PORT", QString::number(defaultPort)).toInt();

    QUrl url;
    url.setScheme(scheme);
    url.setHost(host);
    if (port > 0) {
        url.setPort(port);
    }
    url.setPath(QString("/stw-cgi/%1").arg(cgiName));

    QUrlQuery query;
    // 호환성 확보용 정규 파라미터 순서 유지: msubmenu -> action -> Channel -> 기타
    if (params.contains("msubmenu")) {
        query.addQueryItem("msubmenu", params.value("msubmenu"));
    }
    if (params.contains("action")) {
        query.addQueryItem("action", params.value("action"));
    }

    if (params.contains("Channel")) {
        query.addQueryItem("Channel", params.value("Channel"));
    } else if (params.contains("channel")) {
        query.addQueryItem("channel", params.value("channel"));
    } else if (includeChannelParam) {
        query.addQueryItem("Channel", QString::number(cameraIndex));
    }

    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        if (it.key() == "msubmenu" || it.key() == "action" || it.key() == "Channel" || it.key() == "channel") {
            continue;
        }
        query.addQueryItem(it.key(), it.value());
    }
    url.setQuery(query);

    QNetworkRequest request(url);
    applySslIfNeeded(request);
    qInfo() << "[SUNAPI] request:" << actionLabel << "url=" << url.toString();

    QNetworkReply *reply = m_manager->get(request);
    attachIgnoreSslErrors(reply, "SUNAPI");
    connect(reply, &QNetworkReply::finished, this, [this, reply, actionLabel]() {
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString body = QString::fromUtf8(reply->readAll()).trimmed();
        const QString bodyLower = body.toLower();

        if (reply->error() == QNetworkReply::NoError) {
            bool sunapiBodyError = false;
            QString sunapiErrMsg;

            if (!body.isEmpty()) {
                const QRegularExpression errPattern("^\\s*error\\s*=\\s*([^\\r\\n]+)",
                                                    QRegularExpression::CaseInsensitiveOption
                                                    | QRegularExpression::MultilineOption);
                const QRegularExpressionMatch errMatch = errPattern.match(body);
                if (errMatch.hasMatch()) {
                    const QString errValue = errMatch.captured(1).trimmed();
                    const QString errValueLower = errValue.toLower();
                    if (errValueLower != "0"
                        && errValueLower != "ok"
                        && errValueLower != "none"
                        && errValueLower != "success") {
                        sunapiBodyError = true;
                        sunapiErrMsg = QString("Error=%1").arg(errValue);
                    }
                }

                if (!sunapiBodyError
                    && (bodyLower.contains("fail")
                        || bodyLower.contains("unsupported")
                        || bodyLower.contains("not support")
                        || bodyLower.contains("invalid")
                        || bodyLower.startsWith("ng"))) {
                    sunapiBodyError = true;
                    sunapiErrMsg = body.left(160);
                }
            }

            if (sunapiBodyError) {
                const QString err = QString("%1 failed: device response error (%2)")
                                        .arg(actionLabel, sunapiErrMsg);
                qWarning() << "[SUNAPI]" << err << "url=" << reply->request().url() << "body=" << body.left(200);
                emit cameraControlMessage(err, true);
            } else {
                emit cameraControlMessage(QString("%1 success").arg(actionLabel), false);
            }
        } else {
            const QString err = QString("%1 failed (HTTP %2): %3")
                                    .arg(actionLabel)
                                    .arg(statusCode)
                                    .arg(reply->errorString());
            qWarning() << "[SUNAPI]" << err << "url=" << reply->request().url() << "body=" << body.left(160);
            emit cameraControlMessage(err, true);
        }

        reply->deleteLater();
    });

    return true;
}

// 줌 인 명령 전송
bool Backend::sunapiZoomIn(int cameraIndex) {
    return sendSunapiCommand(
        "image.cgi",
        {{"msubmenu", "focus"}, {"action", "control"}, {"ZoomContinuous", "In"}},
        cameraIndex,
        "Zoom In");
}

// 줌 아웃 명령 전송
bool Backend::sunapiZoomOut(int cameraIndex) {
    return sendSunapiCommand(
        "image.cgi",
        {{"msubmenu", "focus"}, {"action", "control"}, {"ZoomContinuous", "Out"}},
        cameraIndex,
        "Zoom Out");
}

// 줌 동작 정지
bool Backend::sunapiZoomStop(int cameraIndex) {
    return sendSunapiCommand(
        "image.cgi",
        {{"msubmenu", "focus"}, {"action", "control"}, {"ZoomContinuous", "Stop"}},
        cameraIndex,
        "Zoom Stop");
}

// 포커스 근거리 방향 이동
bool Backend::sunapiFocusNear(int cameraIndex) {
    return sendSunapiCommand(
        "image.cgi",
        {{"msubmenu", "focus"}, {"action", "control"}, {"FocusContinuous", "Near"}},
        cameraIndex,
        "Focus Near");
}

// 포커스 원거리 방향 이동
bool Backend::sunapiFocusFar(int cameraIndex) {
    return sendSunapiCommand(
        "image.cgi",
        {{"msubmenu", "focus"}, {"action", "control"}, {"FocusContinuous", "Far"}},
        cameraIndex,
        "Focus Far");
}

// 포커스 동작 정지
bool Backend::sunapiFocusStop(int cameraIndex) {
    return sendSunapiCommand(
        "image.cgi",
        {{"msubmenu", "focus"}, {"action", "control"}, {"FocusContinuous", "Stop"}},
        cameraIndex,
        "Focus Stop");
}

// 오토포커스 명령 전송
bool Backend::sunapiSimpleAutoFocus(int cameraIndex) {
    return sendSunapiCommand(
        "image.cgi",
        {{"msubmenu", "focus"}, {"action", "control"}, {"Mode", "SimpleFocus"}},
        cameraIndex,
        "Auto Focus");
}

// 카메라별 지원 PTZ 액션 조회
void Backend::sunapiLoadSupportedPtzActions(int cameraIndex) {
    if (cameraIndex < 0) {
        emit cameraControlMessage("PTZ capability query failed: invalid camera index", true);
        return;
    }

    const QString host = m_env.value("SUNAPI_IP").trimmed();
    if (host.isEmpty()) {
        emit cameraControlMessage("PTZ capability query failed: SUNAPI_IP is empty", true);
        return;
    }

    const QString schemeRaw = m_env.value("SUNAPI_SCHEME", "http").trimmed().toLower();
    const QString scheme = (schemeRaw == "https") ? QString("https") : QString("http");
    const int defaultPort = (scheme == "https") ? 443 : 80;
    const int port = m_env.value("SUNAPI_PORT", QString::number(defaultPort)).toInt();

    QUrl url;
    url.setScheme(scheme);
    url.setHost(host);
    if (port > 0) {
        url.setPort(port);
    }
    url.setPath("/stw-cgi/ptzcontrol.cgi");

    QUrlQuery query;
    query.addQueryItem("msubmenu", "supportedptzactions");
    query.addQueryItem("action", "view");
    query.addQueryItem("Channel", QString::number(cameraIndex));
    url.setQuery(query);

    QNetworkRequest request(url);
    applySslIfNeeded(request);
    qInfo() << "[SUNAPI] request:" << "Supported PTZ actions" << "url=" << url.toString();

    QNetworkReply *reply = m_manager->get(request);
    attachIgnoreSslErrors(reply, "SUNAPI_CAPS");
    connect(reply, &QNetworkReply::finished, this, [this, reply, cameraIndex]() {
        QVariantMap actions;
        actions.insert("zoom", true);
        actions.insert("focus", true);

        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString body = QString::fromUtf8(reply->readAll()).trimmed();
        const QString lower = body.toLower();

        if (reply->error() == QNetworkReply::NoError) {
            const bool zoomUnsupported =
                (lower.contains("zoom=\"false\"")
                 || lower.contains("zoom=false")
                 || lower.contains("zoom: false")
                 || lower.contains("zoom unsupported"));
            const bool focusUnsupported =
                (lower.contains("focus=\"false\"")
                 || lower.contains("focus=false")
                 || lower.contains("focus: false")
                 || lower.contains("focus unsupported"));

            if (zoomUnsupported) actions.insert("zoom", false);
            if (focusUnsupported) actions.insert("focus", false);

            emit sunapiSupportedPtzActionsLoaded(cameraIndex, actions);
            emit cameraControlMessage("PTZ capability query complete", false);
        } else {
            const QString err = QString("PTZ capability query failed (HTTP %1): %2")
                                    .arg(statusCode)
                                    .arg(reply->errorString());
            qWarning() << "[SUNAPI]" << err << "url=" << reply->request().url() << "body=" << body.left(160);
            emit cameraControlMessage(err, true);
            emit sunapiSupportedPtzActionsLoaded(cameraIndex, actions);
        }

        reply->deleteLater();
    });
}

void Backend::loadPlaybackTimeline(int channelIndex, const QString &dateText) {
    if (channelIndex < 0) {
        emit playbackTimelineFailed("timeline failed: invalid channel index");
        return;
    }

    const QString date = dateText.trimmed();
    const QRegularExpression dateRe("^\\d{4}-\\d{2}-\\d{2}$");
    if (!dateRe.match(date).hasMatch()) {
        emit playbackTimelineFailed("timeline failed: invalid date format (YYYY-MM-DD)");
        return;
    }

    const QString host = m_env.value("SUNAPI_IP").trimmed();
    if (host.isEmpty()) {
        emit playbackTimelineFailed("timeline failed: SUNAPI_IP is empty");
        return;
    }

    const QString schemeRaw = m_env.value("SUNAPI_SCHEME", "http").trimmed().toLower();
    const QString scheme = (schemeRaw == "https") ? QString("https") : QString("http");
    const int defaultPort = (scheme == "https") ? 443 : 80;
    const int port = m_env.value("SUNAPI_PORT", QString::number(defaultPort)).toInt();

    QUrl url;
    url.setScheme(scheme);
    url.setHost(host);
    if (port > 0) {
        url.setPort(port);
    }
    url.setPath("/stw-cgi/recording.cgi");

    QUrlQuery query;
    query.addQueryItem("msubmenu", "timeline");
    query.addQueryItem("action", "view");
    query.addQueryItem("FromDate", date + " 00:00:00");
    query.addQueryItem("ToDate", date + " 23:59:59");
    query.addQueryItem("ChannelIDList", QString::number(channelIndex));
    query.addQueryItem("Type", "All");
    query.addQueryItem("OverlappedID", "0");
    url.setQuery(query);

    QNetworkRequest request(url);
    applySslIfNeeded(request);
    qInfo() << "[SUNAPI] request:" << "Playback timeline" << "url=" << url.toString();

    QNetworkReply *reply = m_manager->get(request);
    attachIgnoreSslErrors(reply, "SUNAPI_TIMELINE");
    connect(reply, &QNetworkReply::finished, this, [this, reply, channelIndex, date]() {
        QVariantList segments;
        const QByteArray body = reply->readAll();

        if (reply->error() != QNetworkReply::NoError) {
            const QString err = QString("timeline failed: HTTP error %1 (%2)")
                                    .arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt())
                                    .arg(reply->errorString());
            qWarning() << "[SUNAPI]" << err << "url=" << reply->request().url() << "body=" << QString::fromUtf8(body.left(160));
            emit playbackTimelineFailed(err);
            emit playbackTimelineLoaded(channelIndex, date, segments);
            reply->deleteLater();
            return;
        }

        QByteArray jsonBytes = body;
        // UTF-8 BOM 제거
        if (jsonBytes.startsWith("\xEF\xBB\xBF")) {
            jsonBytes = jsonBytes.mid(3);
        }

        QJsonParseError parseErr{};
        QJsonDocument doc = QJsonDocument::fromJson(jsonBytes, &parseErr);

        // 일부 장비/프록시 응답에서 JSON 앞뒤로 잡텍스트가 섞이는 경우 fallback
        if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
            const int l = jsonBytes.indexOf('{');
            const int r = jsonBytes.lastIndexOf('}');
            if (l >= 0 && r > l) {
                const QByteArray sliced = jsonBytes.mid(l, r - l + 1);
                QJsonParseError parseErr2{};
                const QJsonDocument doc2 = QJsonDocument::fromJson(sliced, &parseErr2);
                if (parseErr2.error == QJsonParseError::NoError && doc2.isObject()) {
                    doc = doc2;
                    parseErr.error = QJsonParseError::NoError;
                }
            }
        }

        auto timeToSec = [](const QString &dt) -> int {
            // 기대 시간 형식: "... HH:mm:ss"
            const int pos = dt.lastIndexOf(' ');
            const QString timePart = (pos >= 0) ? dt.mid(pos + 1) : dt;
            const QStringList parts = timePart.split(":");
            if (parts.size() != 3) return -1;
            bool okH = false, okM = false, okS = false;
            const int h = parts[0].toInt(&okH);
            const int m = parts[1].toInt(&okM);
            const int s = parts[2].toInt(&okS);
            if (!okH || !okM || !okS) return -1;
            return (h * 3600) + (m * 60) + s;
        };

        if (parseErr.error == QJsonParseError::NoError && doc.isObject()) {
            const QJsonArray resultsRoot = doc.object().value("TimeLineSearchResults").toArray();
            for (const QJsonValue &channelItemVal : resultsRoot) {
                const QJsonObject channelItem = channelItemVal.toObject();
                if (channelItem.value("Channel").toInt(-1) != channelIndex) {
                    continue;
                }
                const QJsonArray items = channelItem.value("Results").toArray();
                for (const QJsonValue &itVal : items) {
                    const QJsonObject it = itVal.toObject();
                    const int startSec = timeToSec(it.value("StartTime").toString());
                    const int endSec = timeToSec(it.value("EndTime").toString());
                    if (startSec < 0 || endSec < 0) {
                        continue;
                    }
                    const int s = qMax(0, qMin(86399, startSec));
                    const int e = qMax(0, qMin(86399, endSec));
                    QVariantMap seg;
                    seg.insert("start", qMin(s, e));
                    seg.insert("end", qMax(s, e));
                    seg.insert("type", it.value("Type").toString("Normal"));
                    segments.push_back(seg);
                }
                break;
            }
        } else {
            // JSON이 아닌 key=value 포맷 fallback:
            // TotalCount=...
            // Channel.<ch>.Result.<idx>.StartTime=YYYY-MM-DD HH:MM:SS
            // Channel.<ch>.Result.<idx>.EndTime=YYYY-MM-DD HH:MM:SS
            // Channel.<ch>.Result.<idx>.Type=Normal
            const QString text = QString::fromUtf8(body);
            QMap<int, QVariantMap> byResult;

            const QRegularExpression startRe(
                "Channel\\.(\\d+)\\.Result\\.(\\d+)\\.StartTime="
                "(\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2})");
            const QRegularExpression endRe(
                "Channel\\.(\\d+)\\.Result\\.(\\d+)\\.EndTime="
                "(\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2})");
            const QRegularExpression typeRe(
                "Channel\\.(\\d+)\\.Result\\.(\\d+)\\.Type=([A-Za-z0-9_\\-]+)");

            auto fillRows = [&](const QRegularExpression &re, const QString &key) {
                auto it = re.globalMatch(text);
                while (it.hasNext()) {
                    const QRegularExpressionMatch m = it.next();
                    const int ch = m.captured(1).toInt();
                    if (ch != channelIndex) {
                        continue;
                    }
                    const int resultIdx = m.captured(2).toInt();
                    QVariantMap row = byResult.value(resultIdx);
                    row.insert(key, m.captured(3).trimmed());
                    byResult.insert(resultIdx, row);
                }
            };

            fillRows(startRe, "StartTime");
            fillRows(endRe, "EndTime");
            fillRows(typeRe, "Type");

            for (auto rit = byResult.constBegin(); rit != byResult.constEnd(); ++rit) {
                const QVariantMap row = rit.value();
                const int startSec = timeToSec(row.value("StartTime").toString());
                const int endSec = timeToSec(row.value("EndTime").toString());
                if (startSec < 0 || endSec < 0) {
                    continue;
                }
                const int s = qMax(0, qMin(86399, startSec));
                const int e = qMax(0, qMin(86399, endSec));
                QVariantMap seg;
                seg.insert("start", qMin(s, e));
                seg.insert("end", qMax(s, e));
                seg.insert("type", row.value("Type", "Normal").toString());
                segments.push_back(seg);
            }
        }

        qInfo() << "[SUNAPI][TIMELINE] parsed segments=" << segments.size()
                << "channel=" << channelIndex
                << "date=" << date;
        emit playbackTimelineLoaded(channelIndex, date, segments);
        reply->deleteLater();
    });
}

void Backend::loadPlaybackMonthRecordedDays(int channelIndex, int year, int month) {
    if (channelIndex < 0) {
        emit playbackMonthRecordedDaysFailed("month days failed: invalid channel index");
        return;
    }
    if (year < 2000 || year > 2100 || month < 1 || month > 12) {
        emit playbackMonthRecordedDaysFailed("month days failed: invalid year/month");
        return;
    }

    const QString host = m_env.value("SUNAPI_IP").trimmed();
    if (host.isEmpty()) {
        emit playbackMonthRecordedDaysFailed("month days failed: SUNAPI_IP is empty");
        return;
    }

    const QString schemeRaw = m_env.value("SUNAPI_SCHEME", "http").trimmed().toLower();
    const QString scheme = (schemeRaw == "https") ? QString("https") : QString("http");
    const int defaultPort = (scheme == "https") ? 443 : 80;
    const int port = m_env.value("SUNAPI_PORT", QString::number(defaultPort)).toInt();

    const QDate firstDate(year, month, 1);
    if (!firstDate.isValid()) {
        emit playbackMonthRecordedDaysFailed("month days failed: invalid date");
        return;
    }
    const QDate lastDate = firstDate.addMonths(1).addDays(-1);
    const QString fromDate = firstDate.toString("yyyy-MM-dd") + " 00:00:00";
    const QString toDate = lastDate.toString("yyyy-MM-dd") + " 23:59:59";
    const QString yearMonth = firstDate.toString("yyyy-MM");

    QUrl url;
    url.setScheme(scheme);
    url.setHost(host);
    if (port > 0) {
        url.setPort(port);
    }
    url.setPath("/stw-cgi/recording.cgi");

    QUrlQuery query;
    query.addQueryItem("msubmenu", "timeline");
    query.addQueryItem("action", "view");
    query.addQueryItem("FromDate", fromDate);
    query.addQueryItem("ToDate", toDate);
    query.addQueryItem("ChannelIDList", QString::number(channelIndex));
    query.addQueryItem("Type", "All");
    query.addQueryItem("OverlappedID", "0");
    url.setQuery(query);

    QNetworkRequest request(url);
    applySslIfNeeded(request);
    qInfo() << "[SUNAPI] request:" << "Playback month days" << "url=" << url.toString();

    QNetworkReply *reply = m_manager->get(request);
    attachIgnoreSslErrors(reply, "SUNAPI_MONTH_DAYS");
    connect(reply, &QNetworkReply::finished, this, [this, reply, channelIndex, yearMonth]() {
        QVariantList dayList;
        const QByteArray body = reply->readAll();

        if (reply->error() != QNetworkReply::NoError) {
            const QString err = QString("month days failed: HTTP error %1 (%2)")
                                    .arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt())
                                    .arg(reply->errorString());
            qWarning() << "[SUNAPI]" << err << "url=" << reply->request().url() << "body=" << QString::fromUtf8(body.left(160));
            emit playbackMonthRecordedDaysFailed(err);
            emit playbackMonthRecordedDaysLoaded(channelIndex, yearMonth, dayList);
            reply->deleteLater();
            return;
        }

        QSet<int> daySet;
        QByteArray jsonBytes = body;
        if (jsonBytes.startsWith("\xEF\xBB\xBF")) {
            jsonBytes = jsonBytes.mid(3);
        }

        QJsonParseError parseErr{};
        QJsonDocument doc = QJsonDocument::fromJson(jsonBytes, &parseErr);
        if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
            const int l = jsonBytes.indexOf('{');
            const int r = jsonBytes.lastIndexOf('}');
            if (l >= 0 && r > l) {
                const QByteArray sliced = jsonBytes.mid(l, r - l + 1);
                QJsonParseError parseErr2{};
                const QJsonDocument doc2 = QJsonDocument::fromJson(sliced, &parseErr2);
                if (parseErr2.error == QJsonParseError::NoError && doc2.isObject()) {
                    doc = doc2;
                    parseErr.error = QJsonParseError::NoError;
                }
            }
        }

        auto collectDayFromDateTime = [&](const QString &dateTime) {
            const QString datePart = dateTime.left(10);
            const QDate d = QDate::fromString(datePart, "yyyy-MM-dd");
            if (!d.isValid()) {
                return;
            }
            if (d.toString("yyyy-MM") != yearMonth) {
                return;
            }
            daySet.insert(d.day());
        };

        if (parseErr.error == QJsonParseError::NoError && doc.isObject()) {
            const QJsonArray resultsRoot = doc.object().value("TimeLineSearchResults").toArray();
            for (const QJsonValue &channelItemVal : resultsRoot) {
                const QJsonObject channelItem = channelItemVal.toObject();
                if (channelItem.value("Channel").toInt(-1) != channelIndex) {
                    continue;
                }
                const QJsonArray items = channelItem.value("Results").toArray();
                for (const QJsonValue &itVal : items) {
                    const QJsonObject it = itVal.toObject();
                    collectDayFromDateTime(it.value("StartTime").toString());
                    collectDayFromDateTime(it.value("EndTime").toString());
                }
                break;
            }
        } else {
            const QString text = QString::fromUtf8(body);
            const QRegularExpression startRe(
                "Channel\\.(\\d+)\\.Result\\.\\d+\\.StartTime="
                "(\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}(?::\\d{2})?)");
            const QRegularExpression endRe(
                "Channel\\.(\\d+)\\.Result\\.\\d+\\.EndTime="
                "(\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}(?::\\d{2})?)");

            auto collectByRegex = [&](const QRegularExpression &re) {
                auto it = re.globalMatch(text);
                while (it.hasNext()) {
                    const QRegularExpressionMatch m = it.next();
                    const int ch = m.captured(1).toInt();
                    if (ch != channelIndex) {
                        continue;
                    }
                    collectDayFromDateTime(m.captured(2));
                }
            };
            collectByRegex(startRe);
            collectByRegex(endRe);
        }

        QList<int> sorted = daySet.values();
        std::sort(sorted.begin(), sorted.end());
        for (int d : sorted) {
            dayList.push_back(d);
        }

        qInfo() << "[SUNAPI][MONTH_DAYS] parsed days=" << dayList.size()
                << "channel=" << channelIndex
                << "month=" << yearMonth;
        emit playbackMonthRecordedDaysLoaded(channelIndex, yearMonth, dayList);
        reply->deleteLater();
    });
}

void Backend::requestPlaybackExport(int channelIndex,
                                    const QString &dateText,
                                    const QString &startTimeText,
                                    const QString &endTimeText,
                                    const QString &savePath) {
    if (channelIndex < 0) {
        emit playbackExportFailed("내보내기 실패: invalid channel index");
        return;
    }

    const QRegularExpression dateRe("^\\d{4}-\\d{2}-\\d{2}$");
    if (!dateRe.match(dateText.trimmed()).hasMatch()) {
        emit playbackExportFailed("내보내기 실패: 날짜 형식 오류 (YYYY-MM-DD)");
        return;
    }

    const int startSec = parseHmsToSec(startTimeText);
    const int endSec = parseHmsToSec(endTimeText);
    if (startSec < 0 || endSec < 0) {
        emit playbackExportFailed("내보내기 실패: 시간 형식 오류 (HH:MM:SS)");
        return;
    }
    if (startSec > endSec) {
        emit playbackExportFailed("내보내기 실패: 시작 시간이 종료 시간보다 큼");
        return;
    }

    const QString host = m_env.value("SUNAPI_IP").trimmed();
    if (host.isEmpty()) {
        emit playbackExportFailed("내보내기 실패: SUNAPI_IP 비어 있음");
        return;
    }

    const QString schemeRaw = m_env.value("SUNAPI_SCHEME", "http").trimmed().toLower();
    const QString scheme = (schemeRaw == "https") ? QString("https") : QString("http");
    const int defaultPort = (scheme == "https") ? 443 : 80;
    const int port = m_env.value("SUNAPI_PORT", QString::number(defaultPort)).toInt();

    QString outPath = savePath.trimmed();
    if (outPath.startsWith("file:", Qt::CaseInsensitive)) {
        outPath = QUrl(outPath).toLocalFile();
    }
    if (outPath.isEmpty()) {
        emit playbackExportFailed("내보내기 실패: 저장 경로 비어 있음");
        return;
    }

    const QString format = m_env.value("SUNAPI_EXPORT_TYPE", "AVI").trimmed().toUpper();
    const QString startDt = QString("%1 %2").arg(dateText.trimmed(), startTimeText.trimmed());
    const QString endDt = QString("%1 %2").arg(dateText.trimmed(), endTimeText.trimmed());

    struct Candidate {
        QString cgi;
        QString submenu;
        QString action;
        QString extraQuery;
    };

    QList<Candidate> createCandidates;
    createCandidates.push_back({
        m_env.value("SUNAPI_EXPORT_CREATE_CGI", "recording.cgi").trimmed(),
        m_env.value("SUNAPI_EXPORT_CREATE_SUBMENU", "export").trimmed(),
        m_env.value("SUNAPI_EXPORT_CREATE_ACTION", "create").trimmed(),
        m_env.value("SUNAPI_EXPORT_CREATE_QUERY").trimmed()
    });
    createCandidates.push_back({"recording.cgi", "backup", "create", ""});
    createCandidates.push_back({"recording.cgi", "export", "start", ""});

    auto buildUrl = [scheme, host, port](const QString &cgi,
                                         const QString &submenu,
                                         const QString &action,
                                         const QMap<QString, QString> &params,
                                         const QString &extraQuery = QString()) {
        QUrl url;
        url.setScheme(scheme);
        url.setHost(host);
        if (port > 0) url.setPort(port);
        url.setPath(QString("/stw-cgi/%1").arg(cgi));
        QUrlQuery q;
        q.addQueryItem("msubmenu", submenu);
        q.addQueryItem("action", action);
        for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
            q.addQueryItem(it.key(), it.value());
        }
        const QString eq = extraQuery.trimmed();
        if (!eq.isEmpty()) {
            const QStringList pairs = eq.split('&', Qt::SkipEmptyParts);
            for (const QString &pair : pairs) {
                const int sep = pair.indexOf('=');
                if (sep > 0) q.addQueryItem(pair.left(sep), pair.mid(sep + 1));
                else q.addQueryItem(pair, QString());
            }
        }
        url.setQuery(q);
        return url;
    };

    auto parseCreateReply = [&](const QByteArray &body, QString *jobId, QString *downloadUrl, QString *reason) -> bool {
        const QByteArray trimmed = body.trimmed();
        if (trimmed.isEmpty()) {
            if (reason) *reason = "empty body";
            return false;
        }

        QJsonParseError pe{};
        const QJsonDocument doc = QJsonDocument::fromJson(trimmed, &pe);
        if (pe.error == QJsonParseError::NoError && doc.isObject()) {
            const QJsonObject obj = doc.object();
            const QString err = extractJsonString(obj, {"Error", "error", "Result", "result"});
            if (!err.isEmpty()) {
                const QString el = err.toLower();
                if (el == "fail" || el == "error" || el == "false" || el == "ng") {
                    if (reason) *reason = err;
                    return false;
                }
            }
            if (jobId) *jobId = extractJsonString(obj, {"JobID", "JobId", "ExportID", "id"});
            if (downloadUrl) *downloadUrl = extractJsonString(obj, {"DownloadUrl", "DownloadURL", "Url", "url", "FileUrl"});
            if ((jobId && !jobId->isEmpty()) || (downloadUrl && !downloadUrl->isEmpty())) return true;
        }

        const QString text = QString::fromUtf8(trimmed);
        if (jobId) *jobId = extractKvValue(text, {"JobID", "JobId", "ExportID", "id"});
        if (downloadUrl) *downloadUrl = extractKvValue(text, {"DownloadUrl", "DownloadURL", "Url", "url", "FileUrl"});
        const QString err = extractKvValue(text, {"Error", "Result", "Status"});
        if (!err.isEmpty()) {
            const QString el = err.toLower();
            if (el == "fail" || el == "error" || el == "false" || el == "ng") {
                if (reason) *reason = err;
                return false;
            }
        }
        if ((jobId && !jobId->isEmpty()) || (downloadUrl && !downloadUrl->isEmpty())) return true;

        if (reason) *reason = text.left(120);
        return false;
    };

    auto parsePollReply = [&](const QByteArray &body, int *progress, bool *done, bool *failed, QString *downloadUrl, QString *reason) {
        *progress = -1;
        *done = false;
        *failed = false;
        const QByteArray trimmed = body.trimmed();
        if (trimmed.isEmpty()) {
            if (reason) *reason = "empty body";
            return;
        }

        QString statusText;
        QJsonParseError pe{};
        const QJsonDocument doc = QJsonDocument::fromJson(trimmed, &pe);
        if (pe.error == QJsonParseError::NoError && doc.isObject()) {
            const QJsonObject obj = doc.object();
            statusText = extractJsonString(obj, {"Status", "status", "State", "state", "Result", "result"});
            if (downloadUrl) *downloadUrl = extractJsonString(obj, {"DownloadUrl", "DownloadURL", "Url", "url", "FileUrl"});
            const QString p = extractJsonString(obj, {"Progress", "progress", "Percent", "percent"});
            bool ok = false;
            int n = p.toInt(&ok);
            if (ok) *progress = qMax(0, qMin(100, n));
        } else {
            const QString text = QString::fromUtf8(trimmed);
            statusText = extractKvValue(text, {"Status", "State", "Result"});
            if (downloadUrl) *downloadUrl = extractKvValue(text, {"DownloadUrl", "DownloadURL", "Url", "url", "FileUrl"});
            const QString p = extractKvValue(text, {"Progress", "Percent"});
            bool ok = false;
            int n = p.toInt(&ok);
            if (ok) *progress = qMax(0, qMin(100, n));
            if (statusText.isEmpty()) statusText = text.left(60);
        }

        const QString st = statusText.toLower();
        if (st.contains("complete") || st.contains("done") || st.contains("ready") || st.contains("success")) {
            *done = true;
            return;
        }
        if (st.contains("fail") || st.contains("error") || st.contains("invalid") || st.contains("timeout")) {
            *failed = true;
            if (reason) *reason = statusText;
            return;
        }
    };

    auto startDownload = [this, outPath](const QUrl &downloadUrl) {
        emit playbackExportProgress(95, "내보내기 파일 다운로드 시작");
        QNetworkRequest req(downloadUrl);
        applySslIfNeeded(req);
        QNetworkReply *dl = m_manager->get(req);
        m_playbackExportDownloadReply = dl;
        attachIgnoreSslErrors(dl, "SUNAPI_EXPORT_DOWNLOAD");

        connect(dl, &QNetworkReply::downloadProgress, this, [this](qint64 rec, qint64 total) {
            if (total <= 0) return;
            const int p = qMax(0, qMin(100, static_cast<int>((rec * 100) / total)));
            emit playbackExportProgress(p, QString("다운로드 중 %1%").arg(p));
        });

        connect(dl, &QNetworkReply::finished, this, [this, dl, outPath]() {
            m_playbackExportDownloadReply = nullptr;
            if (m_playbackExportCancelRequested) {
                dl->deleteLater();
                return;
            }
            if (dl->error() != QNetworkReply::NoError) {
                m_playbackExportInProgress = false;
                emit playbackExportFailed(QString("다운로드 실패: %1").arg(dl->errorString()));
                dl->deleteLater();
                return;
            }
            const QByteArray bytes = dl->readAll();
            QFile out(outPath);
            const QFileInfo fi(outPath);
            QDir().mkpath(fi.absolutePath());
            if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                m_playbackExportInProgress = false;
                emit playbackExportFailed(QString("저장 실패: %1").arg(out.errorString()));
                dl->deleteLater();
                return;
            }
            out.write(bytes);
            out.close();
            m_playbackExportInProgress = false;
            m_playbackExportOutPath.clear();
            m_playbackExportFinalPath.clear();
            emit playbackExportFinished(outPath);
            dl->deleteLater();
        });
    };

    m_playbackExportCancelRequested = false;
    m_playbackExportInProgress = true;
    m_playbackExportOutPath = outPath;
    m_playbackExportFinalPath = outPath;
    emit playbackExportStarted("내보내기 요청 시작");

    auto startFfmpegBackup = [this, channelIndex, dateText, startTimeText, endTimeText, outPath](std::function<void()> onFailedFallback) -> bool {
        const QString host = m_env.value("SUNAPI_IP").trimmed();
        const QString user = m_useCustomRtspAuth ? m_rtspUsernameOverride : m_env.value("SUNAPI_USER").trimmed();
        const QString pass = m_useCustomRtspAuth ? m_rtspPasswordOverride : m_env.value("SUNAPI_PASSWORD").trimmed();
        if (host.isEmpty() || user.isEmpty()) {
            return false;
        }

        QString ffOutPath = outPath.trimmed();
        if (ffOutPath.startsWith("file:", Qt::CaseInsensitive)) {
            ffOutPath = QUrl(ffOutPath).toLocalFile();
        }
        if (ffOutPath.isEmpty()) {
            return false;
        }
        if (QFileInfo(ffOutPath).suffix().trimmed().isEmpty()) {
            ffOutPath += ".avi";
        }
        QDir().mkpath(QFileInfo(ffOutPath).absolutePath());
        m_playbackExportOutPath = ffOutPath;
        m_playbackExportFinalPath = ffOutPath;

        const QString startCompact = dateText.trimmed().remove('-') + startTimeText.trimmed().remove(':');
        const QString endCompact = dateText.trimmed().remove('-') + endTimeText.trimmed().remove(':');

        bool rtspPortOk = false;
        const int rtspPort = m_env.value("SUNAPI_RTSP_PORT", "554").trimmed().toInt(&rtspPortOk);
        const int rtspPortFinal = rtspPortOk ? rtspPort : 554;

        const QString authUser = QString::fromUtf8(QUrl::toPercentEncoding(user));
        const QString authPass = QString::fromUtf8(QUrl::toPercentEncoding(pass));
        const QString rtspUrl = QString("rtsp://%1:%2@%3:%4/%5/recording/%6-%7/OverlappedID=0/backup.smp")
                                    .arg(authUser,
                                         authPass,
                                         host,
                                         QString::number(rtspPortFinal),
                                         QString::number(channelIndex),
                                         startCompact,
                                         endCompact);

        const QString ffmpegBin = resolveFfmpegBinary(m_env);
        QProcess *proc = new QProcess(this);
        m_playbackExportFfmpegProc = proc;
        auto ffHandled = std::make_shared<bool>(false);
        proc->setProcessChannelMode(QProcess::MergedChannels);
        QStringList args;
        args << "-y"
             << "-hide_banner"
             << "-loglevel"
             << "error"
             << "-rtsp_transport"
             << "tcp"
             << "-i"
             << rtspUrl
             << "-map"
             << "0"
             << "-c"
             << "copy"
             << "-f"
             << "avi"
             << ffOutPath;

        emit playbackExportProgress(8, "내보내기 ffmpeg 추출 시작");

        connect(proc, &QProcess::errorOccurred, this, [this, proc, onFailedFallback, ffHandled](QProcess::ProcessError) {
            if (*ffHandled) return;
            *ffHandled = true;
            const QString err = proc->errorString();
            qWarning() << "[SUNAPI][EXPORT][FFMPEG] process error:" << err;
            m_playbackExportFfmpegProc = nullptr;
            proc->deleteLater();
            if (m_playbackExportCancelRequested) {
                m_playbackExportInProgress = false;
                return;
            }
            if (onFailedFallback) {
                emit playbackExportProgress(9, "ffmpeg 실패, WS fallback 전환");
                onFailedFallback();
            } else {
                m_playbackExportInProgress = false;
                emit playbackExportFailed(QString("내보내기 실패: ffmpeg 실행 오류 (%1)").arg(err));
            }
        });

        connect(proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                [this, proc, ffOutPath, onFailedFallback, ffHandled](int exitCode, QProcess::ExitStatus exitStatus) {
                    if (*ffHandled) return;
                    *ffHandled = true;
                    const QString logs = QString::fromUtf8(proc->readAll()).trimmed();
                    m_playbackExportFfmpegProc = nullptr;
                    proc->deleteLater();
                    if (m_playbackExportCancelRequested) {
                        m_playbackExportInProgress = false;
                        return;
                    }
                    if (exitStatus == QProcess::NormalExit && exitCode == 0 && QFileInfo::exists(ffOutPath) && QFileInfo(ffOutPath).size() > 0) {
                        m_playbackExportInProgress = false;
                        m_playbackExportOutPath.clear();
                        m_playbackExportFinalPath.clear();
                        emit playbackExportProgress(100, "내보내기 완료");
                        emit playbackExportFinished(ffOutPath);
                        return;
                    }

                    qWarning() << "[SUNAPI][EXPORT][FFMPEG] failed:"
                               << "exitCode=" << exitCode
                               << "status=" << static_cast<int>(exitStatus)
                               << "log=" << logs.left(300);
                    if (onFailedFallback) {
                        emit playbackExportProgress(9, "ffmpeg 실패, WS fallback 전환");
                        onFailedFallback();
                    } else {
                        m_playbackExportInProgress = false;
                        emit playbackExportFailed("내보내기 실패: ffmpeg 추출 실패");
                    }
                });

        proc->start(ffmpegBin, args);
        if (!proc->waitForStarted(1500)) {
            const QString err = proc->errorString();
            m_playbackExportFfmpegProc = nullptr;
            proc->deleteLater();
            qWarning() << "[SUNAPI][EXPORT][FFMPEG] start failed:" << err;
            return false;
        }

        return true;
    };

    const auto lastCreateReason = std::make_shared<QString>();
    const auto tryCreate = std::make_shared<std::function<void(int)>>();
    *tryCreate = [=](int idx) {
        if (m_playbackExportCancelRequested) {
            m_playbackExportInProgress = false;
            return;
        }
        if (idx < 0 || idx >= createCandidates.size()) {
            if (lastCreateReason->contains("Error Code: 608", Qt::CaseInsensitive)) {
                qInfo() << "[SUNAPI][EXPORT] fallback to RTSP-over-WS backup.smp path (Error 608)";
                const bool started = startFfmpegBackup(
                    [this, channelIndex, dateText, startTimeText, endTimeText, outPath]() {
                        if (m_playbackExportCancelRequested) {
                            m_playbackExportInProgress = false;
                            return;
                        }
                        requestPlaybackExportViaWs(channelIndex, dateText, startTimeText, endTimeText, outPath);
                    });
                if (!started) {
                    if (m_playbackExportCancelRequested) {
                        m_playbackExportInProgress = false;
                        return;
                    }
                    requestPlaybackExportViaWs(channelIndex, dateText, startTimeText, endTimeText, outPath);
                }
            } else if (!lastCreateReason->trimmed().isEmpty()) {
                m_playbackExportInProgress = false;
                emit playbackExportFailed(QString("내보내기 실패: create 엔드포인트 호환 실패 (%1)").arg(*lastCreateReason));
            } else {
                m_playbackExportInProgress = false;
                emit playbackExportFailed("내보내기 실패: create 엔드포인트 호환 실패");
            }
            return;
        }

        const Candidate c = createCandidates.at(idx);
        QMap<QString, QString> params;
        params.insert("Channel", QString::number(channelIndex));
        params.insert("ChannelIDList", QString::number(channelIndex));
        params.insert("StartTime", startDt);
        params.insert("EndTime", endDt);
        params.insert("FromDate", startDt);
        params.insert("ToDate", endDt);
        params.insert("Type", format);
        params.insert("FileType", format);

        const QUrl url = buildUrl(c.cgi, c.submenu, c.action, params, c.extraQuery);
        qInfo() << "[SUNAPI][EXPORT] create request url=" << url;
        QNetworkRequest req(url);
        applySslIfNeeded(req);
        QNetworkReply *reply = m_manager->get(req);
        attachIgnoreSslErrors(reply, "SUNAPI_EXPORT_CREATE");

        connect(reply, &QNetworkReply::finished, this, [=]() {
            const QByteArray body = reply->readAll();
            if (reply->error() != QNetworkReply::NoError) {
                *lastCreateReason = reply->errorString();
                reply->deleteLater();
                (*tryCreate)(idx + 1);
                return;
            }

            QString jobId;
            QString downloadUrlText;
            QString reason;
            if (!parseCreateReply(body, &jobId, &downloadUrlText, &reason)) {
                *lastCreateReason = reason;
                qWarning() << "[SUNAPI][EXPORT] create parse failed" << "url=" << url
                           << "reason=" << reason
                           << "body=" << QString::fromUtf8(body.left(180));
                reply->deleteLater();
                (*tryCreate)(idx + 1);
                return;
            }

            reply->deleteLater();

            if (!downloadUrlText.isEmpty()) {
                QUrl dl = QUrl(downloadUrlText);
                if (dl.isRelative()) {
                    dl = url.resolved(dl);
                }
                startDownload(dl);
                return;
            }

            if (jobId.isEmpty()) {
                m_playbackExportInProgress = false;
                emit playbackExportFailed("내보내기 실패: JobID/다운로드 URL 없음");
                return;
            }

            emit playbackExportProgress(5, QString("내보내기 작업 생성 완료 (JobID=%1)").arg(jobId));

            const QString pollCgi = m_env.value("SUNAPI_EXPORT_POLL_CGI", c.cgi).trimmed();
            const QString pollSubmenu = m_env.value("SUNAPI_EXPORT_POLL_SUBMENU", c.submenu).trimmed().isEmpty()
                    ? c.submenu : m_env.value("SUNAPI_EXPORT_POLL_SUBMENU", c.submenu).trimmed();
            const QString pollAction = m_env.value("SUNAPI_EXPORT_POLL_ACTION", "status").trimmed();
            const QString pollExtra = m_env.value("SUNAPI_EXPORT_POLL_QUERY").trimmed();
            const int pollMs = qMax(500, m_env.value("SUNAPI_EXPORT_POLL_INTERVAL_MS", "1500").toInt());
            const int timeoutMs = qMax(5000, m_env.value("SUNAPI_EXPORT_POLL_TIMEOUT_MS", "120000").toInt());
            const qint64 startMs = QDateTime::currentMSecsSinceEpoch();

            const auto pollOnce = std::make_shared<std::function<void()>>();
            *pollOnce = [=]() {
                const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - startMs;
                if (elapsed > timeoutMs) {
                    m_playbackExportInProgress = false;
                    emit playbackExportFailed("내보내기 실패: 상태 조회 시간 초과");
                    return;
                }

                QMap<QString, QString> pollParams;
                pollParams.insert("JobID", jobId);
                pollParams.insert("ExportID", jobId);
                const QUrl pollUrl = buildUrl(pollCgi, pollSubmenu, pollAction, pollParams, pollExtra);
                QNetworkRequest pollReq(pollUrl);
                applySslIfNeeded(pollReq);
                QNetworkReply *pollReply = m_manager->get(pollReq);
                attachIgnoreSslErrors(pollReply, "SUNAPI_EXPORT_POLL");

                connect(pollReply, &QNetworkReply::finished, this, [=]() {
                    const QByteArray pollBody = pollReply->readAll();
                    if (pollReply->error() != QNetworkReply::NoError) {
                        pollReply->deleteLater();
                        QTimer::singleShot(pollMs, this, [=]() { (*pollOnce)(); });
                        return;
                    }

                    int progress = -1;
                    bool done = false;
                    bool failed = false;
                    QString dlUrlText;
                    QString reason2;
                    parsePollReply(pollBody, &progress, &done, &failed, &dlUrlText, &reason2);
                    if (progress >= 0) {
                        emit playbackExportProgress(progress, QString("내보내기 생성 중 %1%").arg(progress));
                    }

                    if (failed) {
                        m_playbackExportInProgress = false;
                        emit playbackExportFailed(QString("내보내기 실패: %1").arg(reason2.isEmpty() ? "장비 오류" : reason2));
                        pollReply->deleteLater();
                        return;
                    }

                    if (done) {
                        if (dlUrlText.isEmpty()) {
                            const QString dlCgi = m_env.value("SUNAPI_EXPORT_DOWNLOAD_CGI", c.cgi).trimmed();
                            const QString dlSubmenu = m_env.value("SUNAPI_EXPORT_DOWNLOAD_SUBMENU", c.submenu).trimmed().isEmpty()
                                    ? c.submenu : m_env.value("SUNAPI_EXPORT_DOWNLOAD_SUBMENU", c.submenu).trimmed();
                            const QString dlAction = m_env.value("SUNAPI_EXPORT_DOWNLOAD_ACTION", "download").trimmed();
                            const QString dlExtra = m_env.value("SUNAPI_EXPORT_DOWNLOAD_QUERY").trimmed();
                            QMap<QString, QString> dlParams;
                            dlParams.insert("JobID", jobId);
                            dlParams.insert("ExportID", jobId);
                            const QUrl dlUrl = buildUrl(dlCgi, dlSubmenu, dlAction, dlParams, dlExtra);
                            pollReply->deleteLater();
                            startDownload(dlUrl);
                            return;
                        }

                        QUrl dlUrl = QUrl(dlUrlText);
                        if (dlUrl.isRelative()) {
                            dlUrl = pollUrl.resolved(dlUrl);
                        }
                        pollReply->deleteLater();
                        startDownload(dlUrl);
                        return;
                    }

                    pollReply->deleteLater();
                    QTimer::singleShot(pollMs, this, [=]() { (*pollOnce)(); });
                });
            };

            QTimer::singleShot(pollMs, this, [=]() { (*pollOnce)(); });
        });
    };

    (*tryCreate)(0);
}

void Backend::cancelPlaybackExport() {
    if (!m_playbackExportInProgress) {
        return;
    }

    m_playbackExportCancelRequested = true;
    m_playbackExportInProgress = false;

    if (m_playbackExportWs) {
        if (m_playbackExportWs->state() == QAbstractSocket::ConnectedState
            || m_playbackExportWs->state() == QAbstractSocket::ConnectingState) {
            m_playbackExportWs->close();
        }
        // WS sender 해제를 강제해 연결/타이머 람다 참조를 끊고 파일 핸들 정리 유도
        m_playbackExportWs->deleteLater();
        m_playbackExportWs = nullptr;
    }
    if (m_playbackExportFfmpegProc) {
        m_playbackExportFfmpegProc->disconnect(this);
        if (m_playbackExportFfmpegProc->state() != QProcess::NotRunning) {
            m_playbackExportFfmpegProc->kill();
            m_playbackExportFfmpegProc->waitForFinished(300);
        }
        m_playbackExportFfmpegProc->deleteLater();
        m_playbackExportFfmpegProc = nullptr;
    }
    if (m_playbackExportDownloadReply) {
        m_playbackExportDownloadReply->disconnect(this);
        if (m_playbackExportDownloadReply->isRunning()) {
            m_playbackExportDownloadReply->abort();
        }
        m_playbackExportDownloadReply->deleteLater();
        m_playbackExportDownloadReply = nullptr;
    }

    const QString outPath = m_playbackExportOutPath;
    const QString finalPath = m_playbackExportFinalPath;
    if (!outPath.isEmpty()) {
        removeFileWithRetry(this, outPath);
    }
    if (!finalPath.isEmpty() && finalPath != outPath) {
        removeFileWithRetry(this, finalPath);
    }
    m_playbackExportOutPath.clear();
    m_playbackExportFinalPath.clear();

    emit playbackExportFailed("내보내기 취소됨");
}

void Backend::requestPlaybackExportViaWs(int channelIndex,
                                         const QString &dateText,
                                         const QString &startTimeText,
                                         const QString &endTimeText,
                                         const QString &savePath) {
    if (m_playbackExportCancelRequested) {
        m_playbackExportInProgress = false;
        return;
    }
    m_playbackExportInProgress = true;

    const QString host = m_env.value("SUNAPI_IP").trimmed();
    const QString user = m_useCustomRtspAuth ? m_rtspUsernameOverride : m_env.value("SUNAPI_USER").trimmed();
    const QString pass = m_useCustomRtspAuth ? m_rtspPasswordOverride : m_env.value("SUNAPI_PASSWORD").trimmed();
    if (host.isEmpty() || user.isEmpty()) {
        m_playbackExportInProgress = false;
        emit playbackExportFailed("내보내기 실패: SUNAPI 접속 정보 누락");
        return;
    }

    const int startSec = parseHmsToSec(startTimeText);
    const int endSec = parseHmsToSec(endTimeText);
    if (startSec < 0 || endSec < 0 || endSec < startSec) {
        m_playbackExportInProgress = false;
        emit playbackExportFailed("내보내기 실패: 시작/종료 시간 형식 오류");
        return;
    }
    const int durationSec = qMax(1, (endSec - startSec) + 1);

    const QDateTime dtStart = QDateTime::fromString(dateText.trimmed() + " " + startTimeText.trimmed(), "yyyy-MM-dd HH:mm:ss");
    const QDateTime dtEnd = QDateTime::fromString(dateText.trimmed() + " " + endTimeText.trimmed(), "yyyy-MM-dd HH:mm:ss");
    if (!dtStart.isValid() || !dtEnd.isValid()) {
        m_playbackExportInProgress = false;
        emit playbackExportFailed("내보내기 실패: 날짜/시간 파싱 오류");
        return;
    }

    const QString tsStart = dtStart.toString("yyyyMMddHHmmss");
    const QString tsEnd = dtEnd.toString("yyyyMMddHHmmss");
    const QString rtspUri = QString("rtsp://%1/%2/recording/%3-%4/OverlappedID=0/backup.smp")
            .arg(host, QString::number(channelIndex), tsStart, tsEnd);

    QString requestedOutPath = savePath.trimmed();
    if (requestedOutPath.startsWith("file:", Qt::CaseInsensitive)) {
        requestedOutPath = QUrl(requestedOutPath).toLocalFile();
    }
    if (requestedOutPath.isEmpty()) {
        m_playbackExportInProgress = false;
        emit playbackExportFailed("내보내기 실패: 저장 경로 비어 있음");
        return;
    }

    QFileInfo requestedFi(requestedOutPath);
    QString requestedExt = requestedFi.suffix().trimmed().toLower();
    if (requestedExt.isEmpty()) {
        requestedExt = "avi";
        requestedOutPath += ".avi";
    }

    const bool wantsAvi = (requestedExt == "avi");
    const QString finalOutPath = requestedOutPath;

    QString outPath = requestedOutPath;
    if (wantsAvi || !outPath.endsWith(".h264", Qt::CaseInsensitive)) {
        outPath = QFileInfo(requestedOutPath).absolutePath() + "/" + QFileInfo(requestedOutPath).completeBaseName() + ".h264";
    }
    QDir().mkpath(QFileInfo(outPath).absolutePath());
    m_playbackExportOutPath = outPath;
    m_playbackExportFinalPath = finalOutPath;

    // 1) RTSP OPTIONS 챌린지 확보
    bool rtspPortOk = false;
    const int rtspPort = m_env.value("SUNAPI_RTSP_PORT", "554").trimmed().toInt(&rtspPortOk);
    const int rtspPortFinal = rtspPortOk ? rtspPort : 554;
    QTcpSocket socket;
    socket.connectToHost(host, static_cast<quint16>(rtspPortFinal));
    if (!socket.waitForConnected(2000)) {
        m_playbackExportInProgress = false;
        emit playbackExportFailed(QString("내보내기 실패: RTSP 연결 실패 (%1)").arg(socket.errorString()));
        return;
    }

    QByteArray optReq;
    optReq += "OPTIONS " + rtspUri.toUtf8() + " RTSP/1.0\r\n";
    optReq += "CSeq: 1\r\n";
    optReq += "User-Agent: UWC[undefined]\r\n";
    optReq += "\r\n";
    socket.write(optReq);
    socket.flush();
    if (!socket.waitForReadyRead(2000)) {
        socket.disconnectFromHost();
        m_playbackExportInProgress = false;
        emit playbackExportFailed("내보내기 실패: RTSP 챌린지 응답 대기 시간 초과");
        return;
    }
    QByteArray challengeResp = socket.readAll();
    while (socket.waitForReadyRead(120)) {
        challengeResp += socket.readAll();
    }
    socket.disconnectFromHost();

    const QString challengeText = QString::fromUtf8(challengeResp);
    const QRegularExpression realmRe("realm\\s*=\\s*\"([^\"]+)\"", QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression nonceRe("nonce\\s*=\\s*\"([^\"]+)\"", QRegularExpression::CaseInsensitiveOption);
    const QString realm = realmRe.match(challengeText).captured(1).trimmed();
    const QString nonce = nonceRe.match(challengeText).captured(1).trimmed();
    if (realm.isEmpty() || nonce.isEmpty()) {
        m_playbackExportInProgress = false;
        emit playbackExportFailed("내보내기 실패: RTSP Digest challenge 파싱 실패");
        return;
    }

    // 2) security.cgi digestauth로 response 생성
    const QString cnonce = QString::number(QRandomGenerator::global()->generate(), 16).left(8).toUpper();
    const QString schemeRaw = m_env.value("SUNAPI_SCHEME", "http").trimmed().toLower();
    const QString scheme = (schemeRaw == "https") ? QString("https") : QString("http");
    const int defaultPort = (scheme == "https") ? 443 : 80;
    const int httpPort = m_env.value("SUNAPI_PORT", QString::number(defaultPort)).toInt();

    QUrl digestUrl;
    digestUrl.setScheme(scheme);
    digestUrl.setHost(host);
    if (httpPort > 0) {
        digestUrl.setPort(httpPort);
    }
    digestUrl.setPath("/stw-cgi/security.cgi");
    QUrlQuery dq;
    dq.addQueryItem("msubmenu", "digestauth");
    dq.addQueryItem("action", "view");
    dq.addQueryItem("Method", "OPTIONS");
    dq.addQueryItem("Realm", realm);
    dq.addQueryItem("Nonce", nonce);
    dq.addQueryItem("Uri", rtspUri);
    dq.addQueryItem("username", user);
    dq.addQueryItem("password", "");
    dq.addQueryItem("Nc", "00000001");
    dq.addQueryItem("Cnonce", cnonce);
    dq.addQueryItem("SunapiSeqId", QString::number(QRandomGenerator::global()->bounded(100000, 999999)));
    digestUrl.setQuery(dq);

    QNetworkRequest digestReq(digestUrl);
    applySslIfNeeded(digestReq);
    digestReq.setRawHeader("Accept", "application/json");
    digestReq.setRawHeader("X-Secure-Session", "Normal");
    QNetworkReply *digestReply = m_manager->get(digestReq);
    attachIgnoreSslErrors(digestReply, "SUNAPI_EXPORT_WS_DIGEST");

    connect(digestReply, &QNetworkReply::finished, this, [=]() {
        if (digestReply->error() != QNetworkReply::NoError) {
            const QString e = digestReply->errorString();
            digestReply->deleteLater();
            m_playbackExportInProgress = false;
            emit playbackExportFailed(QString("내보내기 실패: digestauth 요청 실패 (%1)").arg(e));
            return;
        }

        QString digestResponse;
        const QByteArray body = digestReply->readAll();
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isObject()) {
            digestResponse = doc.object().value("Response").toString().trimmed();
        }
        digestReply->deleteLater();
        if (digestResponse.isEmpty()) {
            m_playbackExportInProgress = false;
            emit playbackExportFailed("내보내기 실패: digest response 누락");
            return;
        }

        emit playbackExportProgress(3, "내보내기 세션 연결 준비");

        struct WsExportState {
            QPointer<QWebSocket> ws;
            QPointer<QTimer> keepAliveTimer;
            QPointer<QTimer> hardTimeoutTimer;
            QFile outFile;
            QString authHeader;
            QString uri;
            QString session;
            int nextCseq = 1;
            int setupDoneCount = 0;
            int setupExpected = 9;
            int h264RtpChannel = 2;
            QByteArray interleavedBuf;
            QByteArray fuBuffer;
            int fuNalType = 0;
            bool playSent = false;
            bool playAck = false;
            bool teardownSent = false;
            bool finished = false;
            bool gotRtp = false;
            quint32 firstTs = 0;
            quint32 lastTs = 0;
            qint64 targetTsDelta = 0;
            qint64 writtenBytes = 0;
            qint64 startMs = 0;
            qint64 lastRtpMs = 0;
            int lastProgress = 0;
            qint64 lastProgressMs = 0;
            QString outPath;
            QString finalOutPath;
            bool needsAviRemux = false;
            QHash<int, QString> setupCseqTrack; // cseq -> track
            QHash<QString, QByteArray> trackInterleaved; // track -> "x-y"
        };

        auto st = std::make_shared<WsExportState>();
        st->uri = rtspUri;
        st->authHeader = QString("Authorization: Digest username=\"%1\", realm=\"%2\", uri=\"%3\", nonce=\"%4\", response=\"%5\"")
                .arg(user, realm, rtspUri, nonce, digestResponse);
        st->targetTsDelta = static_cast<qint64>(durationSec) * 90000LL;
        st->outPath = outPath;
        st->finalOutPath = finalOutPath;
        st->needsAviRemux = wantsAvi;
        st->outFile.setFileName(outPath);
        if (!st->outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            emit playbackExportFailed(QString("내보내기 실패: 파일 열기 실패 (%1)").arg(st->outFile.errorString()));
            return;
        }

        auto sendRtsp = [this, st](const QByteArray &rtspText) {
            if (!st->ws || st->ws->state() != QAbstractSocket::ConnectedState) {
                return;
            }
            st->ws->sendBinaryMessage(rtspText);
        };

        auto buildReq = [st](const QByteArray &method, const QByteArray &uri, bool withSession = false) {
            QByteArray req;
            req += method + " " + uri + " RTSP/1.0\r\n";
            req += "CSeq: " + QByteArray::number(st->nextCseq++) + "\r\n";
            if (!st->authHeader.isEmpty()) {
                req += st->authHeader.toUtf8() + "\r\n";
            }
            req += "User-Agent: UWC[undefined]\r\n";
            if (withSession && !st->session.isEmpty()) {
                req += "Session: " + st->session.toUtf8() + "\r\n";
            }
            return req;
        };

        auto finishWith = [this, st](bool ok, const QString &message) {
            if (st->finished) {
                return;
            }
            st->finished = true;
            m_playbackExportInProgress = false;

            if (st->keepAliveTimer) {
                st->keepAliveTimer->stop();
                st->keepAliveTimer->deleteLater();
                st->keepAliveTimer = nullptr;
            }
            if (st->hardTimeoutTimer) {
                st->hardTimeoutTimer->stop();
                st->hardTimeoutTimer->deleteLater();
                st->hardTimeoutTimer = nullptr;
            }

            if (st->ws && st->ws->state() == QAbstractSocket::ConnectedState
                && !st->teardownSent && !st->session.isEmpty()) {
                QByteArray td;
                td += "TEARDOWN " + st->uri.toUtf8() + " RTSP/1.0\r\n";
                td += "CSeq: " + QByteArray::number(st->nextCseq++) + "\r\n";
                if (!st->authHeader.isEmpty()) {
                    td += st->authHeader.toUtf8() + "\r\n";
                }
                td += "User-Agent: UWC[undefined]\r\n";
                td += "Session: " + st->session.toUtf8() + "\r\n";
                td += "\r\n";
                st->ws->sendBinaryMessage(td);
                st->teardownSent = true;
            }

            if (st->ws) {
                st->ws->close();
                st->ws->deleteLater();
                st->ws = nullptr;
            }
            m_playbackExportWs = nullptr;

            st->outFile.flush();
            st->outFile.close();

            if (ok) {
                if (st->needsAviRemux) {
                    emit playbackExportProgress(99, "AVI 변환 중");
                    const QString ffmpegBin = resolveFfmpegBinary(m_env);
                    QProcess ff;
                    QStringList args;
                    args << "-y"
                         << "-loglevel" << "error"
                         << "-f" << "h264"
                         << "-i" << st->outPath
                         << "-c:v" << "copy"
                         << "-an"
                         << st->finalOutPath;
                    ff.start(ffmpegBin, args);
                    const bool started = ff.waitForStarted(3000);
                    const bool done = started && ff.waitForFinished(120000);
                    if (!started || !done || ff.exitStatus() != QProcess::NormalExit || ff.exitCode() != 0) {
                        const QString stderrText = QString::fromUtf8(ff.readAllStandardError()).trimmed();
                        QFile::remove(st->finalOutPath);
                        emit playbackExportFailed(QString("내보내기 실패: AVI 변환 실패 (%1)")
                                                      .arg(stderrText.isEmpty() ? "ffmpeg 실행 오류/미설치" : stderrText));
                        return;
                    }
                    QFile::remove(st->outPath);
                }
                emit playbackExportProgress(100, "내보내기 완료");
                emit playbackExportFinished(st->needsAviRemux ? st->finalOutPath : st->outPath);
                m_playbackExportOutPath.clear();
                m_playbackExportFinalPath.clear();
            } else {
                QFile::remove(st->outPath);
                if (!st->finalOutPath.isEmpty() && st->finalOutPath != st->outPath) {
                    QFile::remove(st->finalOutPath);
                }
                m_playbackExportOutPath.clear();
                m_playbackExportFinalPath.clear();
                emit playbackExportFailed(message);
            }
        };

        auto writeAnnexBNal = [st](const QByteArray &nal) {
            if (nal.isEmpty()) {
                return;
            }
            static const QByteArray startCode("\x00\x00\x00\x01", 4);
            st->outFile.write(startCode);
            st->outFile.write(nal);
            st->writtenBytes += (startCode.size() + nal.size());
        };

        auto processRtpH264 = [st, writeAnnexBNal](const QByteArray &rtp) {
            if (rtp.size() < 12) {
                return;
            }
            const quint8 vpxcc = static_cast<quint8>(rtp[0]);
            const int csrcCount = vpxcc & 0x0F;
            int pos = 12 + (csrcCount * 4);
            if (rtp.size() < pos) {
                return;
            }
            const bool hasExtension = (vpxcc & 0x10) != 0;
            if (hasExtension) {
                if (rtp.size() < pos + 4) return;
                const quint16 extLenWords = (static_cast<quint8>(rtp[pos + 2]) << 8) | static_cast<quint8>(rtp[pos + 3]);
                pos += 4 + (extLenWords * 4);
                if (rtp.size() < pos) return;
            }
            QByteArray payload = rtp.mid(pos);
            if (payload.isEmpty()) {
                return;
            }

            const quint8 nal0 = static_cast<quint8>(payload[0]);
            const int nalType = nal0 & 0x1F;

            if (nalType >= 1 && nalType <= 23) {
                writeAnnexBNal(payload);
                return;
            }
            if (nalType == 24) { // STAP-A
                int off = 1;
                while (off + 2 <= payload.size()) {
                    const int nsz = (static_cast<quint8>(payload[off]) << 8) | static_cast<quint8>(payload[off + 1]);
                    off += 2;
                    if (nsz <= 0 || off + nsz > payload.size()) break;
                    writeAnnexBNal(payload.mid(off, nsz));
                    off += nsz;
                }
                return;
            }
            if (nalType == 28 && payload.size() >= 2) { // FU-A
                const quint8 fuIndicator = static_cast<quint8>(payload[0]);
                const quint8 fuHeader = static_cast<quint8>(payload[1]);
                const bool start = (fuHeader & 0x80) != 0;
                const bool end = (fuHeader & 0x40) != 0;
                const quint8 ntype = (fuHeader & 0x1F);
                const quint8 reconstructedNal = (fuIndicator & 0xE0) | ntype;
                const QByteArray frag = payload.mid(2);

                if (start) {
                    st->fuBuffer.clear();
                    st->fuNalType = ntype;
                    st->fuBuffer.append(static_cast<char>(reconstructedNal));
                    st->fuBuffer.append(frag);
                } else if (!st->fuBuffer.isEmpty()) {
                    st->fuBuffer.append(frag);
                }

                if (end && !st->fuBuffer.isEmpty()) {
                    writeAnnexBNal(st->fuBuffer);
                    st->fuBuffer.clear();
                    st->fuNalType = 0;
                }
            }
        };

        auto parseRtspResponse = [this, st, buildReq, sendRtsp, finishWith](const QString &text) {
            const QRegularExpression statusRe("^RTSP/1\\.0\\s+(\\d{3})", QRegularExpression::MultilineOption);
            const QRegularExpression cseqRe("CSeq:\\s*(\\d+)", QRegularExpression::CaseInsensitiveOption);
            const QRegularExpression sessRe("Session:\\s*([^;\\r\\n]+)", QRegularExpression::CaseInsensitiveOption);
            const QRegularExpression trRe("Transport:\\s*([^\\r\\n]+)", QRegularExpression::CaseInsensitiveOption);
            const QRegularExpression ilRe("interleaved\\s*=\\s*(\\d+)\\s*-\\s*(\\d+)", QRegularExpression::CaseInsensitiveOption);

            const int status = statusRe.match(text).captured(1).toInt();
            const int cseq = cseqRe.match(text).captured(1).toInt();
            const QString sess = sessRe.match(text).captured(1).trimmed();
            const QString transport = trRe.match(text).captured(1).trimmed();
            if (!sess.isEmpty()) {
                st->session = sess;
            }

            // OPTIONS 단계(초기 CSeq 1~2) 401은 정상 challenge/재시도 흐름으로 간주
            if (status == 401) {
                if (cseq >= 1 && cseq <= 2 && !st->playSent && st->setupDoneCount == 0) {
                    return;
                }
                finishWith(false, QString("내보내기 실패: RTSP 인증 오류 (401, CSeq %1)").arg(cseq));
                return;
            }

            if (status >= 400) {
                finishWith(false, QString("내보내기 실패: RTSP 응답 오류 (%1, CSeq %2)").arg(status).arg(cseq));
                return;
            }

            if (st->setupCseqTrack.contains(cseq)) {
                st->setupDoneCount++;
                const QString track = st->setupCseqTrack.value(cseq);
                const auto m = ilRe.match(transport);
                if (m.hasMatch()) {
                    st->trackInterleaved.insert(track, QString("%1-%2").arg(m.captured(1), m.captured(2)).toUtf8());
                    if (track.compare("H264", Qt::CaseInsensitive) == 0) {
                        st->h264RtpChannel = m.captured(1).toInt();
                    }
                }
                if (st->setupDoneCount >= st->setupExpected && !st->playSent && !st->session.isEmpty()) {
                    QByteArray playReq = buildReq("PLAY", st->uri.toUtf8(), true);
                    playReq += "Require: samsung-replay-timezone\r\n";
                    playReq += "Rate-Control: no\r\n";
                    playReq += "\r\n";
                    sendRtsp(playReq);
                    st->playSent = true;
                }
                return;
            }

            if (st->playSent && !st->playAck && text.contains("RTP-Info:", Qt::CaseInsensitive)) {
                st->playAck = true;
                emit playbackExportProgress(10, "내보내기 데이터 수신 시작");
                return;
            }
        };

        auto processInterleaved = [this, st, processRtpH264, finishWith](const QByteArray &bytes) {
            st->interleavedBuf.append(bytes);
            while (st->interleavedBuf.size() >= 4) {
                if (static_cast<unsigned char>(st->interleavedBuf[0]) != 0x24) {
                    st->interleavedBuf.remove(0, 1);
                    continue;
                }
                const int payloadLen = (static_cast<unsigned char>(st->interleavedBuf[2]) << 8)
                        | static_cast<unsigned char>(st->interleavedBuf[3]);
                if (st->interleavedBuf.size() < (4 + payloadLen)) {
                    return;
                }
                const int channel = static_cast<unsigned char>(st->interleavedBuf[1]);
                const QByteArray payload = st->interleavedBuf.mid(4, payloadLen);
                st->interleavedBuf.remove(0, 4 + payloadLen);

                if (channel != st->h264RtpChannel) {
                    continue;
                }
                if (payload.size() < 12) {
                    continue;
                }

                st->gotRtp = true;
                st->lastRtpMs = QDateTime::currentMSecsSinceEpoch();
                const quint32 ts = (static_cast<quint32>(static_cast<unsigned char>(payload[4])) << 24)
                        | (static_cast<quint32>(static_cast<unsigned char>(payload[5])) << 16)
                        | (static_cast<quint32>(static_cast<unsigned char>(payload[6])) << 8)
                        | static_cast<quint32>(static_cast<unsigned char>(payload[7]));
                if (st->firstTs == 0) {
                    st->firstTs = ts;
                }
                st->lastTs = ts;

                processRtpH264(payload);

                const quint32 deltaTs = st->lastTs - st->firstTs;
                const int progress = qMax(10, qMin(98, 10 + static_cast<int>((deltaTs * 88LL) / qMax<qint64>(1, st->targetTsDelta))));
                st->lastProgress = progress;
                st->lastProgressMs = st->lastRtpMs;
                emit playbackExportProgress(progress, QString("내보내기 수집 중 %1%").arg(progress));
                if (deltaTs >= static_cast<quint32>(st->targetTsDelta) && st->writtenBytes > 0) {
                    finishWith(true, QString());
                    return;
                }
            }
        };

        st->ws = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
        st->keepAliveTimer = new QTimer(st->ws);
        st->keepAliveTimer->setInterval(15000);
        st->hardTimeoutTimer = new QTimer(st->ws);
        st->hardTimeoutTimer->setSingleShot(true);
        st->hardTimeoutTimer->setInterval(qMax(120000, durationSec * 2000));

        connect(st->hardTimeoutTimer, &QTimer::timeout, this, [finishWith, st]() {
            if (!st->gotRtp || st->writtenBytes <= 0) {
                finishWith(false, "내보내기 실패: 데이터 수신 시간 초과");
            } else {
                finishWith(true, QString());
            }
        });

        connect(st->keepAliveTimer, &QTimer::timeout, this, [st, sendRtsp, buildReq, finishWith, durationSec]() {
            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            if (st->gotRtp && st->writtenBytes > 0) {
                const qint64 idleMs = nowMs - st->lastRtpMs;
                const qint64 runMs = nowMs - st->startMs;
                const qint64 expectedMs = qMax<qint64>(30000, static_cast<qint64>(durationSec) * 1000 + 10000);
                if ((st->lastProgress >= 97 && idleMs >= 3000) || (idleMs >= 10000 && runMs >= expectedMs)) {
                    finishWith(true, QString());
                    return;
                }
            }

            if (!st->ws || st->ws->state() != QAbstractSocket::ConnectedState || st->session.isEmpty()) {
                return;
            }
            QByteArray req = buildReq("GET_PARAMETER", st->uri.toUtf8(), true);
            req += "Content-Length: 0\r\n\r\n";
            sendRtsp(req);
        });

        connect(st->ws, &QWebSocket::connected, this, [st, sendRtsp, buildReq]() {
            QByteArray options1;
            options1 += "OPTIONS " + st->uri.toUtf8() + " RTSP/1.0\r\n";
            options1 += "CSeq: " + QByteArray::number(st->nextCseq++) + "\r\n";
            options1 += "User-Agent: UWC[undefined]\r\n";
            options1 += "\r\n";
            sendRtsp(options1);

            QByteArray options2 = buildReq("OPTIONS", st->uri.toUtf8(), false);
            options2 += "\r\n";
            sendRtsp(options2);

            const QStringList tracks = {"JPEG", "H264", "H265", "PCMU", "G726-16", "G726-24", "G726-32", "G726-40", "aac-16"};
            int interleave = 0;
            for (const QString &track : tracks) {
                const int cseq = st->nextCseq;
                st->setupCseqTrack.insert(cseq, track);
                QByteArray setup = buildReq("SETUP", (st->uri + "/trackID=" + track).toUtf8(), false);
                setup += "Transport: RTP/AVP/TCP;unicast;interleaved="
                        + QByteArray::number(interleave)
                        + "-"
                        + QByteArray::number(interleave + 1)
                        + "\r\n\r\n";
                sendRtsp(setup);
                interleave += 2;
            }
        });

        connect(st->ws, &QWebSocket::binaryMessageReceived, this, [parseRtspResponse, processInterleaved](const QByteArray &payload) {
            if (payload.startsWith("RTSP/1.0")) {
                parseRtspResponse(QString::fromUtf8(payload));
                return;
            }
            processInterleaved(payload);
        });

        connect(st->ws, &QWebSocket::errorOccurred, this, [this, finishWith, st](QAbstractSocket::SocketError) {
            if (m_playbackExportCancelRequested) {
                st->finished = true;
                m_playbackExportWs = nullptr;
                if (st->outFile.isOpen()) {
                    st->outFile.flush();
                    st->outFile.close();
                }
                QFile::remove(st->outPath);
                if (!st->finalOutPath.isEmpty() && st->finalOutPath != st->outPath) {
                    QFile::remove(st->finalOutPath);
                }
                m_playbackExportOutPath.clear();
                m_playbackExportFinalPath.clear();
                return;
            }
            if (!st->finished) {
                finishWith(false, QString("내보내기 실패: websocket 오류 (%1)").arg(st->ws ? st->ws->errorString() : QString("unknown")));
            }
        });

        connect(st->ws, &QWebSocket::disconnected, this, [this, finishWith, st]() {
            if (m_playbackExportCancelRequested) {
                st->finished = true;
                m_playbackExportWs = nullptr;
                if (st->outFile.isOpen()) {
                    st->outFile.flush();
                    st->outFile.close();
                }
                QFile::remove(st->outPath);
                if (!st->finalOutPath.isEmpty() && st->finalOutPath != st->outPath) {
                    QFile::remove(st->finalOutPath);
                }
                m_playbackExportOutPath.clear();
                m_playbackExportFinalPath.clear();
                return;
            }
            if (!st->finished) {
                if (st->writtenBytes > 0) {
                    finishWith(true, QString());
                } else {
                    finishWith(false, "내보내기 실패: 연결 종료(수신 데이터 없음)");
                }
            }
        });

        st->startMs = QDateTime::currentMSecsSinceEpoch();
        st->hardTimeoutTimer->start();
        st->keepAliveTimer->start();
        emit playbackExportProgress(5, "내보내기 WebSocket 연결 시작");
        m_playbackExportWs = st->ws;
        st->ws->open(QUrl(QString("ws://%1/StreamingServer").arg(host)));
    });
}
