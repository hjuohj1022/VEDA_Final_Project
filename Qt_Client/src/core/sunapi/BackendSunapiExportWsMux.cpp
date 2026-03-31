#include "Backend.h"
#include "internal/sunapi/BackendSunapiExportWsMuxService.h"
#include "internal/core/Backend_p.h"

// 재생 내보내기 RTSP 요청 생성 함수
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

// 재생 내보내기 Annex-B NAL 기록 함수
void Backend::playbackExportWriteAnnexBNal(QFile &outFile, qint64 &writtenBytes, const QByteArray &nal)
{
    BackendSunapiExportWsMuxService::playbackExportWriteAnnexBNal(this, d_ptr.get(), outFile, writtenBytes, nal);
}

// 재생 내보내기 RTP H264 처리 함수
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

// 재생 내보내기 인터리브 데이터 처리 함수
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

// 재생 내보내기 취소 함수
void Backend::cancelPlaybackExport()
{
    BackendSunapiExportWsMuxService::cancelPlaybackExport(this, d_ptr.get());
}

