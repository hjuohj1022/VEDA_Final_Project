#include "internal/sunapi/BackendSunapiExportParseService.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>

// Sunapi 내보내기 키값 추출 함수
QString BackendSunapiExportParseService::sunapiExportExtractKvValue(const QString &text, const QStringList &keys)
{
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

// Sunapi 내보내기 JSON 문자열 추출 함수
QString BackendSunapiExportParseService::sunapiExportExtractJsonString(const QJsonObject &obj, const QStringList &keys)
{
    for (const QString &k : keys) {
        if (obj.contains(k)) {
            const QJsonValue v = obj.value(k);
            if (v.isString()) {
                return v.toString().trimmed();
            }
            if (v.isDouble()) {
                return QString::number(static_cast<qint64>(v.toDouble()));
            }
            if (v.isBool()) {
                return v.toBool() ? "true" : "false";
            }
        }
    }
    return QString();
}

// Sunapi 내보내기 시분초 초 단위 변환 함수
int BackendSunapiExportParseService::sunapiExportParseHmsToSec(const QString &hms)
{
    const QRegularExpression re("^(\\d{2}):(\\d{2}):(\\d{2})$");
    const QRegularExpressionMatch m = re.match(hms.trimmed());
    if (!m.hasMatch()) {
        return -1;
    }
    const int h = m.captured(1).toInt();
    const int mm = m.captured(2).toInt();
    const int s = m.captured(3).toInt();
    if (h < 0 || h > 23 || mm < 0 || mm > 59 || s < 0 || s > 59) {
        return -1;
    }
    return (h * 3600) + (mm * 60) + s;
}

// Sunapi 내보내기 생성 응답 파싱 함수
bool BackendSunapiExportParseService::sunapiExportParseCreateReply(const QByteArray &body,
                                                                    QString *jobId,
                                                                    QString *downloadUrl,
                                                                    QString *reason)
{
    const QByteArray trimmed = body.trimmed();
    if (trimmed.isEmpty()) {
        if (reason) {
            *reason = "empty body";
        }
        return false;
    }

    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(trimmed, &pe);
    if (pe.error == QJsonParseError::NoError && doc.isObject()) {
        const QJsonObject obj = doc.object();
        const QString err = sunapiExportExtractJsonString(obj, {"Error", "error", "Result", "result"});
        if (!err.isEmpty()) {
            const QString el = err.toLower();
            if (el == "fail" || el == "error" || el == "false" || el == "ng") {
                if (reason) {
                    *reason = err;
                }
                return false;
            }
        }
        if (jobId) {
            *jobId = sunapiExportExtractJsonString(obj, {"JobID", "JobId", "ExportID", "id"});
        }
        if (downloadUrl) {
            *downloadUrl = sunapiExportExtractJsonString(obj, {"DownloadUrl", "DownloadURL", "Url", "url", "FileUrl"});
        }
        if ((jobId && !jobId->isEmpty()) || (downloadUrl && !downloadUrl->isEmpty())) {
            return true;
        }
    }

    const QString text = QString::fromUtf8(trimmed);
    if (jobId) {
        *jobId = sunapiExportExtractKvValue(text, {"JobID", "JobId", "ExportID", "id"});
    }
    if (downloadUrl) {
        *downloadUrl = sunapiExportExtractKvValue(text, {"DownloadUrl", "DownloadURL", "Url", "url", "FileUrl"});
    }
    const QString err = sunapiExportExtractKvValue(text, {"Error", "Result", "Status"});
    if (!err.isEmpty()) {
        const QString el = err.toLower();
        if (el == "fail" || el == "error" || el == "false" || el == "ng") {
            if (reason) {
                *reason = err;
            }
            return false;
        }
    }
    if ((jobId && !jobId->isEmpty()) || (downloadUrl && !downloadUrl->isEmpty())) {
        return true;
    }

    if (reason) {
        *reason = text.left(120);
    }
    return false;
}

// Sunapi 내보내기 진행 응답 파싱 함수
void BackendSunapiExportParseService::sunapiExportParsePollReply(const QByteArray &body,
                                                                  int *progress,
                                                                  bool *done,
                                                                  bool *failed,
                                                                  QString *downloadUrl,
                                                                  QString *reason)
{
    *progress = -1;
    *done = false;
    *failed = false;

    const QByteArray trimmed = body.trimmed();
    if (trimmed.isEmpty()) {
        if (reason) {
            *reason = "empty body";
        }
        return;
    }

    QString statusText;
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(trimmed, &pe);
    if (pe.error == QJsonParseError::NoError && doc.isObject()) {
        const QJsonObject obj = doc.object();
        statusText = sunapiExportExtractJsonString(obj, {"Status", "status", "State", "state", "Result", "result"});
        if (downloadUrl) {
            *downloadUrl = sunapiExportExtractJsonString(obj, {"DownloadUrl", "DownloadURL", "Url", "url", "FileUrl"});
        }
        const QString p = sunapiExportExtractJsonString(obj, {"Progress", "progress", "Percent", "percent"});
        bool ok = false;
        const int n = p.toInt(&ok);
        if (ok) {
            *progress = qMax(0, qMin(100, n));
        }
    } else {
        const QString text = QString::fromUtf8(trimmed);
        statusText = sunapiExportExtractKvValue(text, {"Status", "State", "Result"});
        if (downloadUrl) {
            *downloadUrl = sunapiExportExtractKvValue(text, {"DownloadUrl", "DownloadURL", "Url", "url", "FileUrl"});
        }
        const QString p = sunapiExportExtractKvValue(text, {"Progress", "Percent"});
        bool ok = false;
        const int n = p.toInt(&ok);
        if (ok) {
            *progress = qMax(0, qMin(100, n));
        }
        if (statusText.isEmpty()) {
            statusText = text.left(60);
        }
    }

    const QString st = statusText.toLower();
    if (st.contains("complete") || st.contains("done") || st.contains("ready") || st.contains("success")) {
        *done = true;
        return;
    }
    if (st.contains("fail") || st.contains("error") || st.contains("invalid") || st.contains("timeout")) {
        *failed = true;
        if (reason) {
            *reason = statusText;
        }
        return;
    }
}

