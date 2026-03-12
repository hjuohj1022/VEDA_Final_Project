#include "Backend.h"
#include "internal/sunapi/BackendSunapiExportParseService.h"

#include <QJsonObject>

QString Backend::sunapiExportExtractKvValue(const QString &text, const QStringList &keys) const
{
    return BackendSunapiExportParseService::sunapiExportExtractKvValue(text, keys);
}

QString Backend::sunapiExportExtractJsonString(const QJsonObject &obj, const QStringList &keys) const
{
    return BackendSunapiExportParseService::sunapiExportExtractJsonString(obj, keys);
}

int Backend::sunapiExportParseHmsToSec(const QString &hms) const
{
    return BackendSunapiExportParseService::sunapiExportParseHmsToSec(hms);
}

bool Backend::sunapiExportParseCreateReply(const QByteArray &body,
                                           QString *jobId,
                                           QString *downloadUrl,
                                           QString *reason) const
{
    return BackendSunapiExportParseService::sunapiExportParseCreateReply(body, jobId, downloadUrl, reason);
}

void Backend::sunapiExportParsePollReply(const QByteArray &body,
                                         int *progress,
                                         bool *done,
                                         bool *failed,
                                         QString *downloadUrl,
                                         QString *reason) const
{
    BackendSunapiExportParseService::sunapiExportParsePollReply(body, progress, done, failed, downloadUrl, reason);
}

