#include "Backend.h"
#include "internal/sunapi/BackendSunapiExportWsMuxService.h"
#include "internal/core/Backend_p.h"

QByteArray Backend::buildPlaybackExportRtspRequest(int &nextCseq,
                                                   const QString &authHeader,
                                                   const QString &session,
                                                   const QByteArray &method,
                                                   const QByteArray &uri,
                                                   bool withSession) const
{
    return BackendSunapiExportWsMuxService::buildPlaybackExportRtspRequest(const_cast<Backend *>(this),
                                                                            d_ptr.get(),
                                                                            nextCseq,
                                                                            authHeader,
                                                                            session,
                                                                            method,
                                                                            uri,
                                                                            withSession);
}

void Backend::playbackExportWriteAnnexBNal(QFile &outFile, qint64 &writtenBytes, const QByteArray &nal)
{
    BackendSunapiExportWsMuxService::playbackExportWriteAnnexBNal(this, d_ptr.get(), outFile, writtenBytes, nal);
}

void Backend::playbackExportProcessRtpH264(const QByteArray &rtp,
                                           QFile &outFile,
                                           qint64 &writtenBytes,
                                           QByteArray &fuBuffer,
                                           int &fuNalType)
{
    BackendSunapiExportWsMuxService::playbackExportProcessRtpH264(this,
                                                                  d_ptr.get(),
                                                                  rtp,
                                                                  outFile,
                                                                  writtenBytes,
                                                                  fuBuffer,
                                                                  fuNalType);
}

bool Backend::playbackExportConsumeInterleaved(const QByteArray &bytes,
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
                                               qint64 &lastProgressMs)
{
    return BackendSunapiExportWsMuxService::playbackExportConsumeInterleaved(this,
                                                                              d_ptr.get(),
                                                                              bytes,
                                                                              h264RtpChannel,
                                                                              interleavedBuf,
                                                                              outFile,
                                                                              writtenBytes,
                                                                              fuBuffer,
                                                                              fuNalType,
                                                                              gotRtp,
                                                                              lastRtpMs,
                                                                              firstTs,
                                                                              lastTs,
                                                                              targetTsDelta,
                                                                              lastProgress,
                                                                              lastProgressMs);
}

void Backend::cancelPlaybackExport()
{
    BackendSunapiExportWsMuxService::cancelPlaybackExport(this, d_ptr.get());
}

