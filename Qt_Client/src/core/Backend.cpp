#include "Backend.h"

#include <QAuthenticator>
#include <QByteArray>
#include <QDebug>
#include <QElapsedTimer>
#include <QNetworkCookieJar>
#include <QPointer>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QHostAddress>
#include <memory>

Backend::Backend(QObject *parent) : QObject(parent)
{
    m_manager = new QNetworkAccessManager(this);
    m_manager->setCookieJar(new QNetworkCookieJar(this));
    setActiveCameras(0);
    loadEnv();
    setupSslConfiguration();
    connect(m_manager, &QNetworkAccessManager::authenticationRequired,
            this,
            [this](QNetworkReply *reply, QAuthenticator *authenticator) {
                const QString user = m_env.value("SUNAPI_USER").trimmed();
                const QString pass = m_env.value("SUNAPI_PASSWORD").trimmed();
                const QString sunapiHost = m_env.value("SUNAPI_IP").trimmed();
                const QString host = reply ? reply->url().host() : QString();
                qInfo() << "[SUNAPI][AUTH] challenge host=" << host
                        << "realm=" << authenticator->realm()
                        << "userConfigured=" << !user.isEmpty();
                if (!user.isEmpty() && !sunapiHost.isEmpty() && host.compare(sunapiHost, Qt::CaseInsensitive) == 0) {
                    authenticator->setUser(user);
                    authenticator->setPassword(pass);
                }
            });
    setupMqtt();
    setupThermalWs();

    const QString envIp = m_env.value("RTSP_IP", "127.0.0.1").trimmed();
    const QString envPort = m_env.value("RTSP_PORT", "8555").trimmed();

    // 앱 시작 시 .env 값 우선 적용
    m_useCustomRtspConfig = false;
    m_rtspIp = envIp;
    m_rtspPort = envPort;

    if (m_rtspIp.isEmpty()) {
        m_rtspIp = envIp;
    }
    if (m_rtspPort.isEmpty()) {
        m_rtspPort = envPort;
    }

    m_sessionTimer = new QTimer(this);
    m_sessionTimer->setInterval(1000);
    connect(m_sessionTimer, &QTimer::timeout, this, &Backend::onSessionTick);

    m_storageTimer = new QTimer(this);
    connect(m_storageTimer, &QTimer::timeout, this, &Backend::checkStorage);
    m_storageTimer->start(5000);
    checkStorage();

    m_playbackWsKeepaliveTimer = new QTimer(this);
    m_playbackWsKeepaliveTimer->setInterval(15000);
    connect(m_playbackWsKeepaliveTimer, &QTimer::timeout, this, [this]() {
        if (!m_playbackWsActive || !m_streamingWs || m_streamingWs->state() != QAbstractSocket::ConnectedState) {
            return;
        }
        if (m_playbackWsUri.isEmpty() || m_playbackWsSession.isEmpty()) {
            return;
        }

        QByteArray req;
        req += "GET_PARAMETER " + m_playbackWsUri.toUtf8() + " RTSP/1.0\r\n";
        req += "CSeq: " + QByteArray::number(m_playbackWsNextCseq++) + "\r\n";
        if (!m_playbackWsAuthHeader.isEmpty()) {
            req += m_playbackWsAuthHeader.toUtf8();
            req += "\r\n";
        }
        req += "User-Agent: UWC[undefined]\r\n";
        req += "Session: " + m_playbackWsSession.toUtf8() + "\r\n";
        req += "Content-Length: 0\r\n";
        req += "\r\n";
        streamingWsSendHex(QString::fromLatin1(req.toHex().toUpper()));
    });

    connect(this, &Backend::streamingWsStateChanged, this, [this](const QString &state) {
        if (state == "disconnected") {
            m_playbackWsKeepaliveTimer->stop();
            m_playbackWsPlaySent = false;
            m_playbackWsPaused = false;
            m_playbackWsSession.clear();
            m_playbackInterleavedBuffer.clear();
            m_playbackValidRtpCount = 0;
        }
    });

    m_playbackRtpOutSocket = new QUdpSocket(this);

    connect(this, &Backend::streamingWsFrame, this, [this](const QString &direction, const QString &hexPayload) {
        if (!m_playbackWsActive || direction != "recv-bin") {
            return;
        }

        QString normalized = hexPayload;
        normalized.remove(QRegularExpression("\\s+"));
        const QByteArray bytes = QByteArray::fromHex(normalized.toLatin1());
        forwardPlaybackInterleavedRtp(bytes);
        if (!bytes.startsWith("RTSP/1.0")) {
            return;
        }

        const QString text = QString::fromLatin1(bytes);
        const QRegularExpression statusRe("^RTSP/1\\.0\\s+(\\d{3})");
        const QRegularExpression cseqRe("CSeq:\\s*(\\d+)", QRegularExpression::CaseInsensitiveOption);
        const QRegularExpression sessionRe("Session:\\s*([^;\\r\\n]+)", QRegularExpression::CaseInsensitiveOption);
        const QRegularExpression transportRe("Transport:\\s*([^\\r\\n]+)", QRegularExpression::CaseInsensitiveOption);
        const QRegularExpression interleavedRe("interleaved\\s*=\\s*(\\d+)\\s*-\\s*(\\d+)", QRegularExpression::CaseInsensitiveOption);

        const int status = statusRe.match(text).captured(1).toInt();
        const int cseq = cseqRe.match(text).captured(1).toInt();
        const QString session = sessionRe.match(text).captured(1).trimmed();
        const QString transport = transportRe.match(text).captured(1).trimmed();
        if (!session.isEmpty()) {
            m_playbackWsSession = session;
        }

        if (status < 400 && cseq > 0 && cseq == m_playbackWsH264SetupCseq && !transport.isEmpty()) {
            const auto m = interleavedRe.match(transport);
            if (m.hasMatch()) {
                m_playbackRtpVideoChannel = m.captured(1).toInt();
                m_playbackRtpVideoAltChannel = m.captured(2).toInt();
                qInfo() << "[PLAYBACK][WS] H264 interleaved mapped:"
                        << m_playbackRtpVideoChannel << "-" << m_playbackRtpVideoAltChannel;
            }
        }

        if (status >= 400) {
            return;
        }

        if (!m_playbackWsPlaySent
            && !m_playbackWsPaused
            && m_playbackWsFinalSetupCseq > 0
            && cseq == m_playbackWsFinalSetupCseq
            && !m_playbackWsSession.isEmpty()
            && m_streamingWs
            && m_streamingWs->state() == QAbstractSocket::ConnectedState) {
            playbackWsPlay();
        }
    });

    QTimer *simTimer = new QTimer(this);
    simTimer->setInterval(5000);
    connect(simTimer, &QTimer::timeout, this, [this]() {
        // 서버 EOF/TLS 경고 노이즈 방지용 RTSPS 포트 평문 TCP 프로브 회피
        const QString rtspScheme = m_env.value("RTSP_SCHEME", "rtsp").trimmed().toLower();
        if (rtspScheme == "rtsps") {
            return;
        }

        static bool probeInFlight = false;
        if (probeInFlight) {
            return;
        }
        probeInFlight = true;

        QTcpSocket *socket = new QTcpSocket(this);
        QElapsedTimer *timer = new QElapsedTimer();
        timer->start();

        QString ip = rtspIp();
        int port = rtspPort().toInt();
        if (port == 0) port = 8555;

        auto finished = std::make_shared<bool>(false);
        auto finishProbe = [socket, timer, finished]() {
            if (*finished) {
                return;
            }
            *finished = true;
            socket->deleteLater();
            delete timer;
            probeInFlight = false;
        };

        connect(socket, &QTcpSocket::connected, this, [this, socket, timer, finishProbe]() {
            const int elapsed = timer->elapsed();
            setLatency(elapsed);
            socket->disconnectFromHost();
            finishProbe();
        });

        connect(socket, &QTcpSocket::errorOccurred, this, [this, finishProbe](QAbstractSocket::SocketError socketError) {
            Q_UNUSED(socketError);
            finishProbe();
        });

        QPointer<QTcpSocket> socketGuard(socket);
        // 연결 지연 장기화 시 프로브 중단
        QTimer::singleShot(1200, this, [socketGuard, finishProbe]() {
            if (socketGuard && socketGuard->state() == QAbstractSocket::ConnectingState) {
                socketGuard->abort();
                finishProbe();
            }
        });

        socket->connectToHost(ip, port);
    });
    simTimer->start();
}

QString Backend::ensurePlaybackWsSdpSource() {
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (tempDir.isEmpty()) {
        tempDir = QDir::currentPath();
    }
    QDir().mkpath(tempDir);
    const QString sdpPath = QDir(tempDir).filePath("team3_playback_ws.sdp");
    QFile sdpFile(sdpPath);
    if (sdpFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
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
                // 역동기화 안전 처리용 무효 후보 판정 및 재스캔 지속
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
    if (m_playbackInterleavedBuffer.size() > (2 * 1024 * 1024)) {
        m_playbackInterleavedBuffer.clear();
    }
}

Backend::~Backend() {}

// 로그인 상태 반환
bool Backend::isLoggedIn() const { return m_isLoggedIn; }
// 백엔드 서버 주소 반환
QString Backend::serverUrl() const { return m_env.value("API_URL", "http://localhost:8080"); }
// 현재 RTSP IP 반환
QString Backend::rtspIp() const { return m_rtspIp; }
// 현재 RTSP 포트 반환
QString Backend::rtspPort() const { return m_rtspPort; }
