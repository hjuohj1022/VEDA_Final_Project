#ifndef BACKEND_SUNAPI_EXPORT_WS_RTSP_SERVICE_H
#define BACKEND_SUNAPI_EXPORT_WS_RTSP_SERVICE_H

#include <functional>

template <typename Key, typename T>
class QHash;
class QString;
class QByteArray;
class Backend;
struct BackendPrivate;

class BackendSunapiExportWsRtspService
{
public:
    static void playbackExportHandleRtspResponse(Backend *backend,
                                                 BackendPrivate *state,
                                                 const QString &text,
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
                                                 const std::function<void(const QString &)> &failWith);
};

#endif // BACKEND_SUNAPI_EXPORT_WS_RTSP_SERVICE_H
