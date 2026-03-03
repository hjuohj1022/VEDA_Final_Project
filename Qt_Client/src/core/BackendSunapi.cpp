#include "Backend.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QDate>
#include <QSet>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>

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
