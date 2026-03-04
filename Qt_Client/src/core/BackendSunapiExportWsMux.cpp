#include "Backend.h"

#include <QDateTime>
#include <QFile>
#include <QProcess>
#include <QTimer>

namespace {
void removeFileWithRetry(QObject *ctx, const QString &path, int retries = 120, int intervalMs = 250) {
    if (!ctx || path.isEmpty()) {
        return;
    }
    if (QFile::remove(path)) {
        return;
    }
    if (retries <= 0) {
        return;
    }
    QTimer::singleShot(intervalMs, ctx, [ctx, path, retries, intervalMs]() {
        // 파일 핸들이 늦게 닫히는 경우를 대비한 지연 재시도
        removeFileWithRetry(ctx, path, retries - 1, intervalMs);
    });
}
}

QByteArray Backend::buildPlaybackExportRtspRequest(int &nextCseq,
                                                   const QString &authHeader,
                                                   const QString &session,
                                                   const QByteArray &method,
                                                   const QByteArray &uri,
                                                   bool withSession) const {
    QByteArray req;
    req += method + " " + uri + " RTSP/1.0\r\n";
    req += "CSeq: " + QByteArray::number(nextCseq++) + "\r\n";
    if (!authHeader.isEmpty()) {
        req += authHeader.toUtf8() + "\r\n";
    }
    req += "User-Agent: UWC[undefined]\r\n";
    if (withSession && !session.isEmpty()) {
        req += "Session: " + session.toUtf8() + "\r\n";
    }
    return req;
}

void Backend::playbackExportWriteAnnexBNal(QFile &outFile, qint64 &writtenBytes, const QByteArray &nal) {
    if (nal.isEmpty()) {
        return;
    }
    static const QByteArray startCode("\x00\x00\x00\x01", 4);
    // raw H264 저장 형식은 Annex-B start code 기반으로 기록
    outFile.write(startCode);
    outFile.write(nal);
    writtenBytes += (startCode.size() + nal.size());
}

void Backend::playbackExportProcessRtpH264(const QByteArray &rtp,
                                           QFile &outFile,
                                           qint64 &writtenBytes,
                                           QByteArray &fuBuffer,
                                           int &fuNalType) {
    if (rtp.size() < 12) {
        return;
    }
    const quint8 vpxcc = static_cast<quint8>(rtp[0]);
    const int csrcCount = vpxcc & 0x0F;
    int pos = 12 + (csrcCount * 4);
    if (rtp.size() < pos) {
        return;
    }
    const bool hasExtension = (vpxcc & 0x10) != 0;
    if (hasExtension) {
        if (rtp.size() < pos + 4) return;
        const quint16 extLenWords = (static_cast<quint8>(rtp[pos + 2]) << 8) | static_cast<quint8>(rtp[pos + 3]);
        pos += 4 + (extLenWords * 4);
        if (rtp.size() < pos) return;
    }
    QByteArray payload = rtp.mid(pos);
    if (payload.isEmpty()) {
        return;
    }

    const quint8 nal0 = static_cast<quint8>(payload[0]);
    const int nalType = nal0 & 0x1F;

    if (nalType >= 1 && nalType <= 23) {
        // Single NAL unit
        playbackExportWriteAnnexBNal(outFile, writtenBytes, payload);
        return;
    }
    if (nalType == 24) {
        // STAP-A: 여러 NAL 묶음 프레임 분해
        int off = 1;
        while (off + 2 <= payload.size()) {
            const int nsz = (static_cast<quint8>(payload[off]) << 8) | static_cast<quint8>(payload[off + 1]);
            off += 2;
            if (nsz <= 0 || off + nsz > payload.size()) break;
            playbackExportWriteAnnexBNal(outFile, writtenBytes, payload.mid(off, nsz));
            off += nsz;
        }
        return;
    }
    if (nalType == 28 && payload.size() >= 2) {
        // FU-A: 분할 NAL 조립
        const quint8 fuIndicator = static_cast<quint8>(payload[0]);
        const quint8 fuHeader = static_cast<quint8>(payload[1]);
        const bool start = (fuHeader & 0x80) != 0;
        const bool end = (fuHeader & 0x40) != 0;
        const quint8 ntype = (fuHeader & 0x1F);
        const quint8 reconstructedNal = (fuIndicator & 0xE0) | ntype;
        const QByteArray frag = payload.mid(2);

        if (start) {
            fuBuffer.clear();
            fuNalType = ntype;
            fuBuffer.append(static_cast<char>(reconstructedNal));
            fuBuffer.append(frag);
        } else if (!fuBuffer.isEmpty()) {
            fuBuffer.append(frag);
        }

        if (end && !fuBuffer.isEmpty()) {
            playbackExportWriteAnnexBNal(outFile, writtenBytes, fuBuffer);
            fuBuffer.clear();
            fuNalType = 0;
        }
    }
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
                                               qint64 &lastProgressMs) {
    interleavedBuf.append(bytes);
    while (interleavedBuf.size() >= 4) {
        if (static_cast<unsigned char>(interleavedBuf[0]) != 0x24) {
            // interleaved marker('$') 기준으로 재동기화
            interleavedBuf.remove(0, 1);
            continue;
        }
        const int payloadLen = (static_cast<unsigned char>(interleavedBuf[2]) << 8)
                | static_cast<unsigned char>(interleavedBuf[3]);
        if (interleavedBuf.size() < (4 + payloadLen)) {
            return false;
        }
        const int channel = static_cast<unsigned char>(interleavedBuf[1]);
        const QByteArray payload = interleavedBuf.mid(4, payloadLen);
        interleavedBuf.remove(0, 4 + payloadLen);

        if (channel != h264RtpChannel) {
            // 비디오 채널 외 데이터는 건너뜀
            continue;
        }
        if (payload.size() < 12) {
            continue;
        }

        gotRtp = true;
        lastRtpMs = QDateTime::currentMSecsSinceEpoch();
        const quint32 ts = (static_cast<quint32>(static_cast<unsigned char>(payload[4])) << 24)
                | (static_cast<quint32>(static_cast<unsigned char>(payload[5])) << 16)
                | (static_cast<quint32>(static_cast<unsigned char>(payload[6])) << 8)
                | static_cast<quint32>(static_cast<unsigned char>(payload[7]));
        if (firstTs == 0) {
            firstTs = ts;
        }
        lastTs = ts;

        playbackExportProcessRtpH264(payload, outFile, writtenBytes, fuBuffer, fuNalType);

        const quint32 deltaTs = lastTs - firstTs;
        const int progress = qMax(10, qMin(98, 10 + static_cast<int>((deltaTs * 88LL) / qMax<qint64>(1, targetTsDelta))));
        lastProgress = progress;
        lastProgressMs = lastRtpMs;
        emit playbackExportProgress(progress, QString("내보내기 수집 중 %1%").arg(progress));
        if (deltaTs >= static_cast<quint32>(targetTsDelta) && writtenBytes > 0) {
            return true;
        }
    }
    return false;
}

void Backend::cancelPlaybackExport() {
    if (!m_playbackExportInProgress) {
        return;
    }

    m_playbackExportCancelRequested = true;
    m_playbackExportInProgress = false;

    if (m_playbackExportWs) {
        if (m_playbackExportWs->state() == QAbstractSocket::ConnectedState
            || m_playbackExportWs->state() == QAbstractSocket::ConnectingState) {
            m_playbackExportWs->close();
        }
        m_playbackExportWs->deleteLater();
        m_playbackExportWs = nullptr;
    }

    if (m_playbackExportFfmpegProc) {
        m_playbackExportFfmpegProc->disconnect(this);
        if (m_playbackExportFfmpegProc->state() != QProcess::NotRunning) {
            m_playbackExportFfmpegProc->kill();
            m_playbackExportFfmpegProc->waitForFinished(300);
        }
        m_playbackExportFfmpegProc->deleteLater();
        m_playbackExportFfmpegProc = nullptr;
    }

    if (m_playbackExportDownloadReply) {
        m_playbackExportDownloadReply->disconnect(this);
        if (m_playbackExportDownloadReply->isRunning()) {
            m_playbackExportDownloadReply->abort();
        }
        m_playbackExportDownloadReply->deleteLater();
        m_playbackExportDownloadReply = nullptr;
    }

    const QString outPath = m_playbackExportOutPath;
    const QString finalPath = m_playbackExportFinalPath;
    // 취소 시 생성 중/완성 파일 모두 삭제 시도
    if (!outPath.isEmpty()) {
        removeFileWithRetry(this, outPath);
    }
    if (!finalPath.isEmpty() && finalPath != outPath) {
        removeFileWithRetry(this, finalPath);
    }

    m_playbackExportOutPath.clear();
    m_playbackExportFinalPath.clear();

    emit playbackExportFailed("내보내기 취소됨");
}


