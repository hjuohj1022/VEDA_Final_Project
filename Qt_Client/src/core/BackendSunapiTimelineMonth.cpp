#include "Backend.h"

#include <QDate>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>

void Backend::loadPlaybackMonthRecordedDays(int channelIndex, int year, int month) {
    // 월 단위 조회 입력값 검증
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
            // 일부 장비 응답의 JSON 래핑 깨짐 보정
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
            // JSON 응답에서 Start/End 날짜의 day 값 수집
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
            // 텍스트(Key=Value) 응답 폴백 파싱
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

