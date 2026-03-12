#include "internal/sunapi/BackendSunapiTimelineService.h"

#include "Backend.h"
#include "internal/core/Backend_p.h"

#include <QDate>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

void BackendSunapiTimelineService::loadPlaybackTimeline(Backend *backend,
                                                        BackendPrivate *state,
                                                        int channelIndex,
                                                        const QString &dateText)
{
    if (channelIndex < 0) {
        emit backend->playbackTimelineFailed("timeline failed: invalid channel index");
        return;
    }

    const QString date = dateText.trimmed();
    const QRegularExpression dateRe("^\\d{4}-\\d{2}-\\d{2}$");
    if (!dateRe.match(date).hasMatch()) {
        emit backend->playbackTimelineFailed("timeline failed: invalid date format (YYYY-MM-DD)");
        return;
    }

    if (state->m_authToken.trimmed().isEmpty()) {
        emit backend->playbackTimelineFailed("timeline failed: login required");
        return;
    }

    QNetworkRequest request = backend->makeApiJsonRequest("/api/sunapi/timeline", {
        {"channel", QString::number(channelIndex)},
        {"date", date}
    });
    backend->applyAuthIfNeeded(request);
    qInfo() << "[SUNAPI] request:" << "Playback timeline" << "url=" << request.url().toString();

    QNetworkReply *reply = state->m_manager->get(request);
    backend->attachIgnoreSslErrors(reply, "SUNAPI_TIMELINE");
    QObject::connect(reply, &QNetworkReply::finished, backend, [backend, reply, channelIndex, date]() {
        QVariantList segments;
        const QByteArray body = reply->readAll();

        if (reply->error() != QNetworkReply::NoError) {
            const QString err = QString("timeline failed: HTTP error %1 (%2)")
                                    .arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt())
                                    .arg(reply->errorString());
            qWarning() << "[SUNAPI]" << err << "url=" << reply->request().url() << "body=" << QString::fromUtf8(body.left(160));
            emit backend->playbackTimelineFailed(err);
            emit backend->playbackTimelineLoaded(channelIndex, date, segments);
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
            if (parts.size() != 3) {
                return -1;
            }
            bool okH = false;
            bool okM = false;
            bool okS = false;
            const int h = parts[0].toInt(&okH);
            const int m = parts[1].toInt(&okM);
            const int s = parts[2].toInt(&okS);
            if (!okH || !okM || !okS) {
                return -1;
            }
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
        emit backend->playbackTimelineLoaded(channelIndex, date, segments);
        reply->deleteLater();
    });
}

void BackendSunapiTimelineService::loadPlaybackMonthRecordedDays(Backend *backend,
                                                                 BackendPrivate *state,
                                                                 int channelIndex,
                                                                 int year,
                                                                 int month)
{
    if (channelIndex < 0) {
        emit backend->playbackMonthRecordedDaysFailed("month days failed: invalid channel index");
        return;
    }
    if (year < 2000 || year > 2100 || month < 1 || month > 12) {
        emit backend->playbackMonthRecordedDaysFailed("month days failed: invalid year/month");
        return;
    }

    if (state->m_authToken.trimmed().isEmpty()) {
        emit backend->playbackMonthRecordedDaysFailed("month days failed: login required");
        return;
    }

    const QDate firstDate(year, month, 1);
    if (!firstDate.isValid()) {
        emit backend->playbackMonthRecordedDaysFailed("month days failed: invalid date");
        return;
    }
    const QString yearMonth = firstDate.toString("yyyy-MM");

    QNetworkRequest request = backend->makeApiJsonRequest("/api/sunapi/month-days", {
        {"channel", QString::number(channelIndex)},
        {"year", QString::number(year)},
        {"month", QString::number(month)}
    });
    backend->applyAuthIfNeeded(request);
    qInfo() << "[SUNAPI] request:" << "Playback month days" << "url=" << request.url().toString();

    QNetworkReply *reply = state->m_manager->get(request);
    backend->attachIgnoreSslErrors(reply, "SUNAPI_MONTH_DAYS");
    QObject::connect(reply, &QNetworkReply::finished, backend, [backend, reply, channelIndex, yearMonth]() {
        QVariantList dayList;
        const QByteArray body = reply->readAll();

        if (reply->error() != QNetworkReply::NoError) {
            const QString err = QString("month days failed: HTTP error %1 (%2)")
                                    .arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt())
                                    .arg(reply->errorString());
            qWarning() << "[SUNAPI]" << err << "url=" << reply->request().url() << "body=" << QString::fromUtf8(body.left(160));
            emit backend->playbackMonthRecordedDaysFailed(err);
            emit backend->playbackMonthRecordedDaysLoaded(channelIndex, yearMonth, dayList);
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
        emit backend->playbackMonthRecordedDaysLoaded(channelIndex, yearMonth, dayList);
        reply->deleteLater();
    });
}

