#include "Backend.h"
#include "internal/sunapi/BackendSunapiExportParseService.h"

#include <QJsonObject>

// Sunapi 내보내기 키값 추출 함수
QString Backend::sunapiExportExtractKvValue(const QString &text, const QStringList &keys) const
{
    return BackendSunapiExportParseService::sunapiExportExtractKvValue(text, keys);
}

// Sunapi 내보내기 JSON 문자열 추출 함수
QString Backend::sunapiExportExtractJsonString(const QJsonObject &obj, const QStringList &keys) const
{
    return BackendSunapiExportParseService::sunapiExportExtractJsonString(obj, keys);
}

// Sunapi 내보내기 시분초 초 단위 변환 함수
int Backend::sunapiExportParseHmsToSec(const QString &hms) const
{
    return BackendSunapiExportParseService::sunapiExportParseHmsToSec(hms);
}

// Sunapi 내보내기 생성 응답 파싱 함수
bool Backend::sunapiExportParseCreateReply(const QByteArray &body,
                                           QString *jobId,
                                           QString *downloadUrl,
                                           QString *reason) const
{
    return BackendSunapiExportParseService::sunapiExportParseCreateReply(body, jobId, downloadUrl, reason);
}

// Sunapi 내보내기 진행 응답 파싱 함수
void Backend::sunapiExportParsePollReply(const QByteArray &body,
                                         int *progress,
                                         bool *done,
                                         bool *failed,
                                         QString *downloadUrl,
                                         QString *reason) const
{
    BackendSunapiExportParseService::sunapiExportParsePollReply(body, progress, done, failed, downloadUrl, reason);
}

