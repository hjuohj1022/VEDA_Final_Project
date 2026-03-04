#include "Backend.h"

#include <QDate>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

void Backend::loadPlaybackTimeline(int channelIndex, const QString &dateText) {
    // 입력값 기본 검증
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
        if (jsonBytes.startsWith("\xEF\xBB\xBF")) {
            jsonBytes = jsonBytes.mid(3);
        }

        QJsonParseError parseErr{};
        QJsonDocument doc = QJsonDocument::fromJson(jsonBytes, &parseErr);

        if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
            // 장비가 JSON 앞뒤에 쓰레기 바이트를 섞어 보내는 경우 보정
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
            // JSON 표준 응답 파싱
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
            // 구형 펌웨어 Key=Value 텍스트 응답 파싱
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

