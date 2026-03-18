#ifndef BACKEND_SUNAPI_EXPORT_WS_MUX_SERVICE_H
#define BACKEND_SUNAPI_EXPORT_WS_MUX_SERVICE_H

#include <QtGlobal>

class Backend;
struct BackendPrivate;
class QFile;
class QString;
class QByteArray;

class BackendSunapiExportWsMuxService
{
public:
    static QByteArray buildPlaybackExportRtspRequest(Backend *backend,
                                                     BackendPrivate *state,
                                                     int &nextCseq,
                                                     const QString &authHeader,
                                                     const QString &session,
                                                     const QByteArray &method,
                                                     const QByteArray &uri,
                                                     bool withSession);
    static void playbackExportWriteAnnexBNal(Backend *backend,
                                             BackendPrivate *state,
                                             QFile &outFile,
                                             qint64 &writtenBytes,
                                             const QByteArray &nal);
    static void playbackExportProcessRtpH264(Backend *backend,
                                             BackendPrivate *state,
                                             const QByteArray &rtp,
                                             QFile &outFile,
                                             qint64 &writtenBytes,
                                             QByteArray &fuBuffer,
                                             int &fuNalType);
    static bool playbackExportConsumeInterleaved(Backend *backend,
                                                 BackendPrivate *state,
                                                 const QByteArray &bytes,
                                                 int h264RtpChannel,
                                                 QByteArray &interleavedBuf,
                                                 QFile &outFile,
                                                 qint64 &writtenBytes,
                                                 QByteArray &fuBuffer,
                                                 int &fuNalType,
                                                 bool &gotRtp,
                                                 qint64 &lastRtpMs,
                                                 quint32 &firstTs,
                                                 quint32 &lastTs,
                                                 qint64 targetTsDelta,
                                                 int &lastProgress,
                                                 qint64 &lastProgressMs);
    static void cancelPlaybackExport(Backend *backend, BackendPrivate *state);
};

#endif // BACKEND_SUNAPI_EXPORT_WS_MUX_SERVICE_H
