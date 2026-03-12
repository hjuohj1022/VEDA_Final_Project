#ifndef BACKEND_SUNAPI_EXPORT_PARSE_SERVICE_H
#define BACKEND_SUNAPI_EXPORT_PARSE_SERVICE_H

#include <QByteArray>
#include <QString>
#include <QStringList>

class QJsonObject;

class BackendSunapiExportParseService
{
public:
    static QString sunapiExportExtractKvValue(const QString &text, const QStringList &keys);
    static QString sunapiExportExtractJsonString(const QJsonObject &obj, const QStringList &keys);
    static int sunapiExportParseHmsToSec(const QString &hms);
    static bool sunapiExportParseCreateReply(const QByteArray &body,
                                             QString *jobId,
                                             QString *downloadUrl,
                                             QString *reason);
    static void sunapiExportParsePollReply(const QByteArray &body,
                                           int *progress,
                                           bool *done,
                                           bool *failed,
                                           QString *downloadUrl,
                                           QString *reason);
};

#endif // BACKEND_SUNAPI_EXPORT_PARSE_SERVICE_H
