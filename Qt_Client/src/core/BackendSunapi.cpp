#include "Backend.h"

#include <QDebug>
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
#include <algorithm>
#include <memory>

namespace {
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
        attachIgnoreSslErrors(dl, "SUNAPI_EXPORT_DOWNLOAD");

        connect(dl, &QNetworkReply::downloadProgress, this, [this](qint64 rec, qint64 total) {
            if (total <= 0) return;
            const int p = qMax(0, qMin(100, static_cast<int>((rec * 100) / total)));
            emit playbackExportProgress(p, QString("다운로드 중 %1%").arg(p));
        });

        connect(dl, &QNetworkReply::finished, this, [this, dl, outPath]() {
            if (dl->error() != QNetworkReply::NoError) {
                emit playbackExportFailed(QString("다운로드 실패: %1").arg(dl->errorString()));
                dl->deleteLater();
                return;
            }
            const QByteArray bytes = dl->readAll();
            QFile out(outPath);
            const QFileInfo fi(outPath);
            QDir().mkpath(fi.absolutePath());
            if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                emit playbackExportFailed(QString("저장 실패: %1").arg(out.errorString()));
                dl->deleteLater();
                return;
            }
            out.write(bytes);
            out.close();
            emit playbackExportFinished(outPath);
            dl->deleteLater();
        });
    };

    emit playbackExportStarted("내보내기 요청 시작");

    const auto lastCreateReason = std::make_shared<QString>();
    const auto tryCreate = std::make_shared<std::function<void(int)>>();
    *tryCreate = [=](int idx) {
        if (idx < 0 || idx >= createCandidates.size()) {
            if (lastCreateReason->contains("Error Code: 608", Qt::CaseInsensitive)) {
                emit playbackExportFailed("내보내기 실패: 카메라가 SUNAPI export 기능을 지원하지 않음(Error 608)");
            } else if (!lastCreateReason->trimmed().isEmpty()) {
                emit playbackExportFailed(QString("내보내기 실패: create 엔드포인트 호환 실패 (%1)").arg(*lastCreateReason));
            } else {
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
