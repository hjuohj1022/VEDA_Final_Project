#include "Backend.h"
#include "internal/sunapi/BackendSunapiExportWsRtspService.h"
#include "internal/core/Backend_p.h"

void Backend::playbackExportHandleRtspResponse(const QString &text,
                                               const QHash<int, QString> &setupCseqTrack,
                                               int &setupDoneCount,
                                               QHash<QString, QByteArray> &trackInterleaved,
                                               int &h264RtpChannel,
                                               int setupExpected,
                                               bool &playSent,
                                               bool &playAck,
                                               QString &session,
                                               int &nextCseq,
                                               const QString &authHeader,
                                               const QString &uri,
                                               const std::function<void(const QByteArray &)> &sendRtsp,
                                               const std::function<void(const QString &)> &failWith)
{
    BackendSunapiExportWsRtspService::playbackExportHandleRtspResponse(this,
                                                                       d_ptr.get(),
                                                                       text,
                                                                       setupCseqTrack,
                                                                       setupDoneCount,
                                                                       trackInterleaved,
                                                                       h264RtpChannel,
                                                                       setupExpected,
                                                                       playSent,
                                                                       playAck,
                                                                       session,
                                                                       nextCseq,
                                                                       authHeader,
                                                                       uri,
                                                                       sendRtsp,
                                                                       failWith);
}

