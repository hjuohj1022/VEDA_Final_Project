#include "internal/stream/BackendPlaybackWsRuntimeService.h"

#include "Backend.h"
#include "internal/core/Backend_p.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QStandardPaths>
#include <QUrl>
#include <QUdpSocket>

// 재생 WebSocket Sdp 소스 확인 함수
QString BackendPlaybackWsRuntimeService::ensurePlaybackWsSdpSource(Backend *backend, BackendPrivate *state)
{
    Q_UNUSED(backend);

    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (tempDir.isEmpty()) {
        tempDir = QDir::currentPath();
    }
    QDir().mkpath(tempDir);

    const QString sdpPath = QDir(tempDir).filePath("team3_playback_ws.sdp");
    QFile sdpFile(sdpPath);
    if (sdpFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QByteArray content;
        const QByteArray pt = QByteArray::number(state->m_playbackRtpPayloadType);
        content += "v=0\n";
        content += "o=- 0 0 IN IP4 127.0.0.1\n";
        content += "s=Team3 Playback WS RTP\n";
        content += "c=IN IP4 127.0.0.1\n";
        content += "t=0 0\n";
        content += "m=video " + QByteArray::number(state->m_playbackRtpVideoPort) + " RTP/AVP " + pt + "\n";
        content += "a=rtpmap:" + pt + " H264/90000\n";
        if (!state->m_playbackSps.isEmpty() && !state->m_playbackPps.isEmpty() && state->m_playbackSps.size() >= 4) {
            const QByteArray profileLevelId = QByteArray::number(static_cast<unsigned char>(state->m_playbackSps[1]), 16).rightJustified(2, '0')
                    + QByteArray::number(static_cast<unsigned char>(state->m_playbackSps[2]), 16).rightJustified(2, '0')
                    + QByteArray::number(static_cast<unsigned char>(state->m_playbackSps[3]), 16).rightJustified(2, '0');
            content += "a=fmtp:" + pt
                    + " packetization-mode=1;profile-level-id="
                    + profileLevelId.toUpper()
                    + ";sprop-parameter-sets="
                    + state->m_playbackSps.toBase64()
                    + ","
                    + state->m_playbackPps.toBase64()
                    + "\n";
        } else {
            content += "a=fmtp:" + pt + " packetization-mode=1\n";
        }
        content += "a=control:streamid=0\n";
        content += "a=recvonly\n";

        sdpFile.write(content);
        sdpFile.close();
        state->m_playbackWsSdpPath = sdpPath;
    }

    const QString useFileProtocol = state->m_env.value("PLAYBACK_WS_SDP_FILE_PROTOCOL", "0").trimmed().toLower();
    if (useFileProtocol == "1" || useFileProtocol == "true" || useFileProtocol == "on") {
        return QUrl::fromLocalFile(state->m_playbackWsSdpPath).toString();
    }
    return QUrl::fromLocalFile(state->m_playbackWsSdpPath).toString();
}

void BackendPlaybackWsRuntimeService::parsePlaybackH264ConfigFromRtp(Backend *backend,
                                                                     BackendPrivate *state,
                                                                     const QByteArray &rtpPacket)
{
    Q_UNUSED(backend);

    if (rtpPacket.size() < 12) {
        return;
    }

    const int csrcCount = static_cast<unsigned char>(rtpPacket[0]) & 0x0F;
    const bool hasExt = (static_cast<unsigned char>(rtpPacket[0]) & 0x10) != 0;
    int payloadOffset = 12 + (csrcCount * 4);
    if (payloadOffset >= rtpPacket.size()) {
        return;
    }

    if (hasExt) {
        if (payloadOffset + 4 > rtpPacket.size()) {
            return;
        }
        const int extWords = (static_cast<unsigned char>(rtpPacket[payloadOffset + 2]) << 8)
                | static_cast<unsigned char>(rtpPacket[payloadOffset + 3]);
        payloadOffset += 4 + extWords * 4;
        if (payloadOffset >= rtpPacket.size()) {
            return;
        }
    }

    const QByteArray payload = rtpPacket.mid(payloadOffset);
    if (payload.isEmpty()) {
        return;
    }

    const int nalType = static_cast<unsigned char>(payload[0]) & 0x1F;
    if (nalType >= 1 && nalType <= 23) {
        if (nalType == 7 && state->m_playbackSps.isEmpty()) {
            state->m_playbackSps = payload;
        } else if (nalType == 8 && state->m_playbackPps.isEmpty()) {
            state->m_playbackPps = payload;
        }
        return;
    }

    if (nalType != 28 || payload.size() < 2) {
        return;
    }

    const unsigned char fuIndicator = static_cast<unsigned char>(payload[0]);
    const unsigned char fuHeader = static_cast<unsigned char>(payload[1]);
    const bool start = (fuHeader & 0x80) != 0;
    const bool end = (fuHeader & 0x40) != 0;
    const int fuNalType = fuHeader & 0x1F;
    const unsigned char reconstructedNalHeader = (fuIndicator & 0xE0) | (fuNalType & 0x1F);

    if (start) {
        state->m_playbackFuNalType = fuNalType;
        state->m_playbackFuBuffer.clear();
        state->m_playbackFuBuffer.append(static_cast<char>(reconstructedNalHeader));
        state->m_playbackFuBuffer.append(payload.mid(2));
    } else if (!state->m_playbackFuBuffer.isEmpty() && state->m_playbackFuNalType == fuNalType) {
        state->m_playbackFuBuffer.append(payload.mid(2));
    } else {
        return;
    }

    if (end) {
        if (state->m_playbackFuNalType == 7 && state->m_playbackSps.isEmpty()) {
            state->m_playbackSps = state->m_playbackFuBuffer;
        } else if (state->m_playbackFuNalType == 8 && state->m_playbackPps.isEmpty()) {
            state->m_playbackPps = state->m_playbackFuBuffer;
        }
        state->m_playbackFuBuffer.clear();
        state->m_playbackFuNalType = 0;
    }
}

void BackendPlaybackWsRuntimeService::forwardPlaybackInterleavedRtp(Backend *backend,
                                                                    BackendPrivate *state,
                                                                    const QByteArray &bytes)
{
    if (!state->m_playbackWsActive || !state->m_playbackRtpOutSocket || bytes.isEmpty()) {
        return;
    }
    if (bytes.startsWith("RTSP/1.0")) {
        return;
    }

    state->m_playbackInterleavedBuffer.append(bytes);
    int offset = 0;
    while (offset + 4 <= state->m_playbackInterleavedBuffer.size()) {
        if (static_cast<unsigned char>(state->m_playbackInterleavedBuffer[offset]) != 0x24) {
            const int next = state->m_playbackInterleavedBuffer.indexOf('$', offset + 1);
            if (next < 0) {
                if (offset > 0) {
                    state->m_playbackInterleavedBuffer.remove(0, offset);
                } else if (state->m_playbackInterleavedBuffer.size() > 65536) {
                    state->m_playbackInterleavedBuffer.clear();
                }
                return;
            }
            offset = next;
            continue;
        }

        const int channel = static_cast<unsigned char>(state->m_playbackInterleavedBuffer[offset + 1]);
        const int payloadLen = (static_cast<unsigned char>(state->m_playbackInterleavedBuffer[offset + 2]) << 8)
                | static_cast<unsigned char>(state->m_playbackInterleavedBuffer[offset + 3]);
        if (payloadLen <= 0) {
            offset += 1;
            continue;
        }

        const int packetEnd = offset + 4 + payloadLen;
        if (packetEnd > state->m_playbackInterleavedBuffer.size()) {
            break;
        }

        const bool isCandidateVideoChannel =
                (channel == state->m_playbackRtpVideoChannel || channel == state->m_playbackRtpVideoAltChannel);
        if (isCandidateVideoChannel) {
            const QByteArray rtpPacket = state->m_playbackInterleavedBuffer.mid(offset + 4, payloadLen);
            if (state->m_playbackWsPaused) {
                offset = packetEnd;
                continue;
            }
            if (rtpPacket.size() < 12) {
                offset = packetEnd;
                continue;
            }

            const int rtpVersion = (static_cast<unsigned char>(rtpPacket[0]) >> 6) & 0x03;
            if (rtpVersion != 2) {
                offset += 1;
                continue;
            }

            if (rtpPacket.size() >= 2) {
                const int secondByte = static_cast<unsigned char>(rtpPacket[1]);
                const bool looksRtcp = (secondByte >= 200 && secondByte <= 206);
                if (looksRtcp) {
                    offset = packetEnd;
                    continue;
                }
                if (channel != state->m_playbackRtpVideoChannel) {
                    state->m_playbackRtpVideoChannel = channel;
                }
            }

            BackendPlaybackWsRuntimeService::parsePlaybackH264ConfigFromRtp(backend, state, rtpPacket);
            if (rtpPacket.size() >= 2) {
                const int pt = static_cast<unsigned char>(rtpPacket[1]) & 0x7F;
                if (pt >= 96 && pt <= 127 && pt != state->m_playbackRtpPayloadType) {
                    state->m_playbackRtpPayloadType = pt;
                }
            }

            state->m_playbackValidRtpCount += 1;
            if (!state->m_playbackWsSdpPublished && state->m_playbackValidRtpCount >= 1) {
                const QString sdpSource = BackendPlaybackWsRuntimeService::ensurePlaybackWsSdpSource(backend, state);
                state->m_playbackWsSdpPublished = true;
                qInfo() << "[PLAYBACK][WS] SDP published after RTP count="
                        << state->m_playbackValidRtpCount
                        << "sps=" << !state->m_playbackSps.isEmpty()
                        << "pps=" << !state->m_playbackPps.isEmpty();
                emit backend->playbackPrepared(sdpSource);
            }

            state->m_playbackRtpOutSocket->writeDatagram(rtpPacket, QHostAddress::LocalHost, state->m_playbackRtpVideoPort);
        }

        offset = packetEnd;
    }

    if (offset > 0) {
        state->m_playbackInterleavedBuffer.remove(0, offset);
    }
    if (state->m_playbackInterleavedBuffer.size() > (2 * 1024 * 1024)) {
        state->m_playbackInterleavedBuffer.clear();
    }
}

