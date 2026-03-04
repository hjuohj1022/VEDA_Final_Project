#include "Backend.h"

#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <QDebug>
#include <QUdpSocket>

QString Backend::ensurePlaybackWsSdpSource() {
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (tempDir.isEmpty()) {
        tempDir = QDir::currentPath();
    }
    QDir().mkpath(tempDir);
    const QString sdpPath = QDir(tempDir).filePath("team3_playback_ws.sdp");
    QFile sdpFile(sdpPath);
    if (sdpFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        // 수신 중인 RTP payload type/SPS/PPS 기준으로 로컬 SDP를 동적으로 생성
        QByteArray content;
        const QByteArray pt = QByteArray::number(m_playbackRtpPayloadType);
        content += "v=0\n";
        content += "o=- 0 0 IN IP4 127.0.0.1\n";
        content += "s=Team3 Playback WS RTP\n";
        content += "c=IN IP4 127.0.0.1\n";
        content += "t=0 0\n";
        content += "m=video " + QByteArray::number(m_playbackRtpVideoPort) + " RTP/AVP " + pt + "\n";
        content += "a=rtpmap:" + pt + " H264/90000\n";
        if (!m_playbackSps.isEmpty() && !m_playbackPps.isEmpty() && m_playbackSps.size() >= 4) {
            const QByteArray profileLevelId = QByteArray::number(static_cast<unsigned char>(m_playbackSps[1]), 16).rightJustified(2, '0')
                    + QByteArray::number(static_cast<unsigned char>(m_playbackSps[2]), 16).rightJustified(2, '0')
                    + QByteArray::number(static_cast<unsigned char>(m_playbackSps[3]), 16).rightJustified(2, '0');
            content += "a=fmtp:" + pt
                    + " packetization-mode=1;profile-level-id="
                    + profileLevelId.toUpper()
                    + ";sprop-parameter-sets="
                    + m_playbackSps.toBase64()
                    + ","
                    + m_playbackPps.toBase64()
                    + "\n";
        } else {
            content += "a=fmtp:" + pt + " packetization-mode=1\n";
        }
        content += "a=control:streamid=0\n";
        content += "a=recvonly\n";
        sdpFile.write(content);
        sdpFile.close();
        m_playbackWsSdpPath = sdpPath;
    }
    const QString useFileProtocol = m_env.value("PLAYBACK_WS_SDP_FILE_PROTOCOL", "0").trimmed().toLower();
    if (useFileProtocol == "1" || useFileProtocol == "true" || useFileProtocol == "on") {
        return QUrl::fromLocalFile(m_playbackWsSdpPath).toString();
    }
    return QUrl::fromLocalFile(m_playbackWsSdpPath).toString();
}

void Backend::parsePlaybackH264ConfigFromRtp(const QByteArray &rtpPacket) {
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
        // 단일 NAL에서 SPS/PPS를 직접 수집
        if (nalType == 7 && m_playbackSps.isEmpty()) {
            m_playbackSps = payload;
        } else if (nalType == 8 && m_playbackPps.isEmpty()) {
            m_playbackPps = payload;
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
        // FU-A 시작 패킷: 원본 NAL 헤더를 복원해 조립 버퍼 시작
        m_playbackFuNalType = fuNalType;
        m_playbackFuBuffer.clear();
        m_playbackFuBuffer.append(static_cast<char>(reconstructedNalHeader));
        m_playbackFuBuffer.append(payload.mid(2));
    } else if (!m_playbackFuBuffer.isEmpty() && m_playbackFuNalType == fuNalType) {
        m_playbackFuBuffer.append(payload.mid(2));
    } else {
        return;
    }

    if (end) {
        // FU-A 종료 시점에 완성된 SPS/PPS 반영
        if (m_playbackFuNalType == 7 && m_playbackSps.isEmpty()) {
            m_playbackSps = m_playbackFuBuffer;
        } else if (m_playbackFuNalType == 8 && m_playbackPps.isEmpty()) {
            m_playbackPps = m_playbackFuBuffer;
        }
        m_playbackFuBuffer.clear();
        m_playbackFuNalType = 0;
    }
}

void Backend::forwardPlaybackInterleavedRtp(const QByteArray &bytes) {
    if (!m_playbackWsActive || !m_playbackRtpOutSocket || bytes.isEmpty()) {
        return;
    }
    if (bytes.startsWith("RTSP/1.0")) {
        return;
    }

    m_playbackInterleavedBuffer.append(bytes);
    int offset = 0;
    while (offset + 4 <= m_playbackInterleavedBuffer.size()) {
        if (static_cast<unsigned char>(m_playbackInterleavedBuffer[offset]) != 0x24) {
            // interleaved($) 프레임 경계 재동기화
            const int next = m_playbackInterleavedBuffer.indexOf('$', offset + 1);
            if (next < 0) {
                if (offset > 0) {
                    m_playbackInterleavedBuffer.remove(0, offset);
                } else if (m_playbackInterleavedBuffer.size() > 65536) {
                    m_playbackInterleavedBuffer.clear();
                }
                return;
            }
            offset = next;
            continue;
        }

        const int channel = static_cast<unsigned char>(m_playbackInterleavedBuffer[offset + 1]);
        const int payloadLen = (static_cast<unsigned char>(m_playbackInterleavedBuffer[offset + 2]) << 8)
                | static_cast<unsigned char>(m_playbackInterleavedBuffer[offset + 3]);
        if (payloadLen <= 0) {
            offset += 1;
            continue;
        }
        const int packetEnd = offset + 4 + payloadLen;
        if (packetEnd > m_playbackInterleavedBuffer.size()) {
            break;
        }

        const bool isCandidateVideoChannel =
                (channel == m_playbackRtpVideoChannel || channel == m_playbackRtpVideoAltChannel);
        if (isCandidateVideoChannel) {
            const QByteArray rtpPacket = m_playbackInterleavedBuffer.mid(offset + 4, payloadLen);
            if (m_playbackWsPaused) {
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
                    // RTCP는 재생 데이터 포워딩 대상에서 제외
                    offset = packetEnd;
                    continue;
                }
                if (channel != m_playbackRtpVideoChannel) {
                    m_playbackRtpVideoChannel = channel;
                }
            }
            parsePlaybackH264ConfigFromRtp(rtpPacket);
            if (rtpPacket.size() >= 2) {
                const int pt = static_cast<unsigned char>(rtpPacket[1]) & 0x7F;
                if (pt >= 96 && pt <= 127 && pt != m_playbackRtpPayloadType) {
                    m_playbackRtpPayloadType = pt;
                }
            }
            m_playbackValidRtpCount += 1;
            if (!m_playbackWsSdpPublished
                && m_playbackValidRtpCount >= 1) {
                // 첫 유효 RTP 수신 후 MediaPlayer source로 사용할 SDP 발행
                const QString sdpSource = ensurePlaybackWsSdpSource();
                m_playbackWsSdpPublished = true;
                qInfo() << "[PLAYBACK][WS] SDP published after RTP count="
                        << m_playbackValidRtpCount
                        << "sps=" << !m_playbackSps.isEmpty()
                        << "pps=" << !m_playbackPps.isEmpty();
                emit playbackPrepared(sdpSource);
            }
            m_playbackRtpOutSocket->writeDatagram(rtpPacket, QHostAddress::LocalHost, m_playbackRtpVideoPort);
        }

        offset = packetEnd;
    }

    if (offset > 0) {
        m_playbackInterleavedBuffer.remove(0, offset);
    }
    // 비정상 누적 버퍼 방지
    if (m_playbackInterleavedBuffer.size() > (2 * 1024 * 1024)) {
        m_playbackInterleavedBuffer.clear();
    }
}
