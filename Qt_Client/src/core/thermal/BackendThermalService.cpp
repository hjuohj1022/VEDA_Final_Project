#include "internal/thermal/BackendThermalService.h"

#include "Backend.h"
#include "internal/core/Backend_p.h"

#include <QAbstractSocket>
#include <QBuffer>
#include <QByteArray>
#include <QDateTime>
#include <QDebug>
#include <QImage>
#include <QList>
#include <QMap>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslError>
#include <QUrl>
#include <QWebSocket>
#include <QtEndian>

#include <algorithm>
#include <vector>

namespace {
constexpr int kThermalWidth = 160;
constexpr int kThermalHeight = 120;
constexpr int kThermalPixelCount = kThermalWidth * kThermalHeight;
constexpr int kThermalFrameBytes8 = kThermalPixelCount;
constexpr int kThermalFrameBytes16 = kThermalPixelCount * 2;
constexpr int kThermalHeaderBytes = 10;
constexpr int kThermalHeaderWithRangeBytes = 8;
constexpr int kThermalHeaderLegacyBytes = 4;
constexpr int kThermalChunkLimit = 100;
constexpr qint64 kThermalFrameTimeoutMs = 1500;

enum class ThermalFrameEncoding {
    Raw16,
    Scaled8
};

static QString normalizedThermalPath(const QString &rawPath, const QString &fallback)
{
    QString path = rawPath.trimmed();
    if (path.isEmpty()) {
        path = fallback;
    }
    if (!path.startsWith('/')) {
        path.prepend('/');
    }
    return path;
}

static bool usesThermalWsTransport(const BackendPrivate *state)
{
    if (!state) {
        return true;
    }

    const QString transport = state->m_env.value("THERMAL_TRANSPORT", "ws").trimmed().toLower();
    return transport != "mqtt";
}

static QString thermalStartPath(const BackendPrivate *state)
{
    return normalizedThermalPath(state ? state->m_env.value("THERMAL_START_PATH") : QString(),
                                 QStringLiteral("/thermal/control/start"));
}

static QString thermalStopPath(const BackendPrivate *state)
{
    return normalizedThermalPath(state ? state->m_env.value("THERMAL_STOP_PATH") : QString(),
                                 QStringLiteral("/thermal/control/stop"));
}

static QString thermalWsPath(const BackendPrivate *state)
{
    return normalizedThermalPath(state ? state->m_env.value("THERMAL_WS_PATH") : QString(),
                                 QStringLiteral("/thermal/stream"));
}

static void resetThermalAssemblyState(BackendPrivate *state)
{
    if (!state) {
        return;
    }

    state->m_thermalCurrentFrameId = -1;
    state->m_thermalTotalChunksExpected = 0;
    state->m_thermalFrameStartedMs = 0;
    state->m_thermalHeaderMin = 0;
    state->m_thermalHeaderMax = 0;
    state->m_thermalFrameChunks.clear();
    state->m_thermalLastFrameId = -1;
    state->m_thermalLastDisplayMs = 0;
    state->m_thermalChunkCount = 0;
    state->m_thermalTotalBytes = 0;
    state->m_thermalDisplayFps = 0.0;
}

static void setThermalInfoText(Backend *backend, BackendPrivate *state, const QString &text)
{
    if (!backend || !state || state->m_thermalInfoText == text) {
        return;
    }
    state->m_thermalInfoText = text;
    emit backend->thermalInfoTextChanged();
}

static QUrl buildThermalWsUrl(Backend *backend, BackendPrivate *state)
{
    Q_UNUSED(state);

    const QUrl apiBase(backend ? backend->serverUrl() : QString());
    if (!apiBase.isValid() || apiBase.host().trimmed().isEmpty()) {
        return {};
    }

    const QString apiScheme = apiBase.scheme().trimmed().toLower();
    const QString wsScheme = (apiScheme == "https") ? QStringLiteral("wss") : QStringLiteral("ws");
    const int defaultPort = (apiScheme == "https") ? 443 : 80;

    QUrl wsUrl;
    wsUrl.setScheme(wsScheme);
    wsUrl.setHost(apiBase.host());
    wsUrl.setPort(apiBase.port(defaultPort));
    wsUrl.setPath(thermalWsPath(state));
    return wsUrl;
}

static void disconnectThermalWs(Backend *backend, BackendPrivate *state, bool expectedStop)
{
    Q_UNUSED(backend);

    if (!state) {
        return;
    }

    state->m_thermalStopExpected = expectedStop;
    if (!state->m_thermalWs) {
        return;
    }

    if (state->m_thermalWs->state() == QAbstractSocket::ConnectedState
        || state->m_thermalWs->state() == QAbstractSocket::ConnectingState) {
        state->m_thermalWs->close();
    }
}

static void ensureThermalWs(Backend *backend, BackendPrivate *state)
{
    if (!backend || !state || state->m_thermalWs) {
        return;
    }

    state->m_thermalWs = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, backend);

    QObject::connect(state->m_thermalWs, &QWebSocket::connected, backend, [backend, state]() {
        if (!state->m_thermalStreaming) {
            disconnectThermalWs(backend, state, true);
            return;
        }

        state->m_thermalChunkCount = 0;
        state->m_thermalTotalBytes = 0;
        const QString scheme = buildThermalWsUrl(backend, state).scheme().toUpper();
        setThermalInfoText(backend, state, QString("Thermal %1 connected").arg(scheme));
        qInfo() << "[THERMAL][WS] connected";
    });

    QObject::connect(state->m_thermalWs, &QWebSocket::disconnected, backend, [backend, state]() {
        if (state->m_thermalStopExpected) {
            state->m_thermalStopExpected = false;
            return;
        }

        if (state->m_thermalStreaming) {
            state->m_thermalStreaming = false;
            emit backend->thermalStreamingChanged();
        }
        setThermalInfoText(backend, state, QStringLiteral("Thermal stream disconnected"));
        qWarning() << "[THERMAL][WS] disconnected unexpectedly";
    });

    QObject::connect(state->m_thermalWs, &QWebSocket::binaryMessageReceived, backend,
                     [backend, state](const QByteArray &payload) {
        if (!state->m_thermalStreaming) {
            return;
        }

        state->m_thermalChunkCount += 1;
        state->m_thermalTotalBytes += payload.size();
        backend->handleThermalChunkMessage(payload);
    });

    QObject::connect(state->m_thermalWs, &QWebSocket::textMessageReceived, backend,
                     [state](const QString &message) {
        qInfo() << "[THERMAL][WS][TEXT]" << message.left(180);
        Q_UNUSED(state);
    });

    QObject::connect(state->m_thermalWs, &QWebSocket::errorOccurred, backend,
                     [backend, state](QAbstractSocket::SocketError) {
        const QString err = state->m_thermalWs
                ? state->m_thermalWs->errorString()
                : QStringLiteral("unknown websocket error");
        setThermalInfoText(backend, state, QString("Thermal WS error: %1").arg(err));
        qWarning() << "[THERMAL][WS] error:" << err;
    });

    QObject::connect(state->m_thermalWs, &QWebSocket::sslErrors, backend,
                     [state](const QList<QSslError> &errors) {
        for (const auto &err : errors) {
            qWarning() << "[THERMAL][WS][SSL]" << err.errorString();
        }
        if (state->m_sslIgnoreErrors && state->m_thermalWs) {
            state->m_thermalWs->ignoreSslErrors();
        }
    });
}

static void openThermalWs(Backend *backend, BackendPrivate *state)
{
    if (!backend || !state) {
        return;
    }

    ensureThermalWs(backend, state);
    if (!state->m_thermalWs) {
        setThermalInfoText(backend, state, QStringLiteral("Thermal WS unavailable"));
        return;
    }

    const QUrl wsUrl = buildThermalWsUrl(backend, state);
    if (!wsUrl.isValid() || wsUrl.host().trimmed().isEmpty()) {
        state->m_thermalStreaming = false;
        emit backend->thermalStreamingChanged();
        setThermalInfoText(backend, state, QStringLiteral("Thermal WS connect failed: invalid API_URL"));
        return;
    }

    if (state->m_thermalWs->state() == QAbstractSocket::ConnectedState
        || state->m_thermalWs->state() == QAbstractSocket::ConnectingState) {
        state->m_thermalStopExpected = true;
        state->m_thermalWs->abort();
    }

    QNetworkRequest request(wsUrl);
    if (!state->m_authToken.trimmed().isEmpty()) {
        request.setRawHeader("Authorization", QByteArray("Bearer ") + state->m_authToken.toUtf8());
    }

    state->m_thermalStopExpected = false;
    if (wsUrl.scheme().compare("wss", Qt::CaseInsensitive) == 0 && state->m_sslConfigReady) {
        state->m_thermalWs->setSslConfiguration(state->m_sslConfig);
    }

    setThermalInfoText(backend, state,
                       QString("Thermal %1 connecting...").arg(wsUrl.scheme().toUpper()));
    qInfo() << "[THERMAL][WS] opening:" << wsUrl;
    state->m_thermalWs->open(request);
}

static void postThermalStopRequest(Backend *backend, BackendPrivate *state)
{
    if (!backend || !state || !state->m_manager) {
        return;
    }

    if (state->m_thermalStopReply && state->m_thermalStopReply->isRunning()) {
        state->m_thermalStopReply->abort();
    }

    QNetworkRequest request = backend->makeApiJsonRequest(thermalStopPath(state));
    backend->applyAuthIfNeeded(request);
    state->m_thermalStopReply = state->m_manager->post(request, QByteArray("{}"));
    QPointer<QNetworkReply> reply = state->m_thermalStopReply;
    backend->attachIgnoreSslErrors(reply, QStringLiteral("THERMAL_STOP"));

    QObject::connect(reply, &QNetworkReply::finished, backend, [backend, state, reply]() {
        if (!reply) {
            return;
        }

        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString body = QString::fromUtf8(reply->readAll()).trimmed();
        const bool ok = (reply->error() == QNetworkReply::NoError) && (statusCode < 400 || statusCode == 0);
        if (!ok) {
            qWarning() << "[THERMAL] stop request failed. status=" << statusCode
                       << "err=" << reply->errorString()
                       << "body=" << body.left(180);
        }
        reply->deleteLater();
        if (state->m_thermalStopReply == reply) {
            state->m_thermalStopReply = nullptr;
        }
        Q_UNUSED(backend);
    });
}

struct ThermalChunkHeader {
    int frameId = -1;
    quint16 idx = 0;
    quint16 total = 0;
    quint16 minVal = 0;
    quint16 maxVal = 0;
    int headerBytes = 0;
    bool hasFrameId = false;
};

static bool isReasonableChunkHeader(const ThermalChunkHeader &header)
{
    return header.total > 0 && header.total <= kThermalChunkLimit && header.idx < header.total;
}

static bool parseThermalChunkHeader(const QByteArray &message, ThermalChunkHeader *header)
{
    if (!header || message.isEmpty()) {
        return false;
    }

    const uchar *p = reinterpret_cast<const uchar *>(message.constData());

    if (message.size() >= kThermalHeaderBytes) {
        ThermalChunkHeader parsed;
        parsed.hasFrameId = true;
        parsed.frameId = static_cast<int>(qFromBigEndian<quint16>(p));
        parsed.idx = qFromBigEndian<quint16>(p + 2);
        parsed.total = qFromBigEndian<quint16>(p + 4);
        parsed.minVal = qFromBigEndian<quint16>(p + 6);
        parsed.maxVal = qFromBigEndian<quint16>(p + 8);
        parsed.headerBytes = kThermalHeaderBytes;
        if (isReasonableChunkHeader(parsed)) {
            *header = parsed;
            return true;
        }
    }

    if (message.size() >= kThermalHeaderWithRangeBytes) {
        ThermalChunkHeader parsed;
        parsed.idx = qFromBigEndian<quint16>(p);
        parsed.total = qFromBigEndian<quint16>(p + 2);
        parsed.minVal = qFromBigEndian<quint16>(p + 4);
        parsed.maxVal = qFromBigEndian<quint16>(p + 6);
        parsed.headerBytes = kThermalHeaderWithRangeBytes;
        if (isReasonableChunkHeader(parsed)) {
            *header = parsed;
            return true;
        }
    }

    if (message.size() >= kThermalHeaderLegacyBytes) {
        ThermalChunkHeader parsed;
        parsed.idx = qFromBigEndian<quint16>(p);
        parsed.total = qFromBigEndian<quint16>(p + 2);
        parsed.headerBytes = kThermalHeaderLegacyBytes;
        if (isReasonableChunkHeader(parsed)) {
            *header = parsed;
            return true;
        }
    }

    return false;
}

static QRgb jetColor(unsigned char value)
{
    const int v = static_cast<int>(value);
    int r = 0;
    int g = 0;
    int b = 0;

    if (v < 64) {
        r = 0;
        g = v * 4;
        b = 255;
    } else if (v < 128) {
        r = 0;
        g = 255;
        b = 255 - (v - 64) * 4;
    } else if (v < 192) {
        r = (v - 128) * 4;
        g = 255;
        b = 0;
    } else {
        r = 255;
        g = 255 - (v - 192) * 4;
        b = 0;
    }
    return qRgb(r, g, b);
}

static QRgb ironColor(unsigned char value)
{
    const int v = static_cast<int>(value);
    int r = 0;
    int g = 0;
    int b = 0;

    if (v < 64) {
        r = v * 2;
        g = 0;
        b = v;
    } else if (v < 128) {
        r = 128 + (v - 64) * 2;
        g = (v - 64);
        b = 64 - (v - 64);
    } else if (v < 192) {
        r = 255;
        g = 64 + (v - 128) * 2;
        b = 0;
    } else {
        r = 255;
        g = 192 + (v - 192);
        b = (v - 192) * 2;
    }

    if (g > 255)
        g = 255;
    if (b > 255)
        b = 255;
    return qRgb(r, g, b);
}

static int percentileValue(std::vector<int> values, int p)
{
    if (values.empty())
        return 0;
    if (p <= 0)
        return *std::min_element(values.begin(), values.end());
    if (p >= 100)
        return *std::max_element(values.begin(), values.end());

    const size_t idx = static_cast<size_t>((static_cast<long long>(values.size() - 1) * p) / 100);
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(idx), values.end());
    return values[idx];
}

static QString thermalEncodingLabel(ThermalFrameEncoding encoding)
{
    return (encoding == ThermalFrameEncoding::Raw16) ? QStringLiteral("16-bit") : QStringLiteral("8-bit");
}
} // namespace

void BackendThermalService::startThermalStream(Backend *backend, BackendPrivate *state)
{
    if (state->m_thermalStreaming) {
        return;
    }

    state->m_thermalStreaming = true;
    resetThermalAssemblyState(state);

    const bool useWs = usesThermalWsTransport(state);
    state->m_thermalInfoText = useWs ? QStringLiteral("Thermal stream starting...") : QStringLiteral("Thermal stream active (MQTT)");
    emit backend->thermalStreamingChanged();
    emit backend->thermalInfoTextChanged();

    if (!useWs) {
        qInfo() << "[THERMAL] stream started via MQTT";
        return;
    }

    if (!state->m_manager) {
        state->m_thermalStreaming = false;
        emit backend->thermalStreamingChanged();
        setThermalInfoText(backend, state, QStringLiteral("Thermal start failed: network manager unavailable"));
        return;
    }

    ensureThermalWs(backend, state);

    if (state->m_thermalStartReply && state->m_thermalStartReply->isRunning()) {
        state->m_thermalStartReply->abort();
    }

    QNetworkRequest request = backend->makeApiJsonRequest(thermalStartPath(state));
    backend->applyAuthIfNeeded(request);
    state->m_thermalStartReply = state->m_manager->post(request, QByteArray("{}"));
    QPointer<QNetworkReply> reply = state->m_thermalStartReply;
    backend->attachIgnoreSslErrors(reply, QStringLiteral("THERMAL_START"));

    QObject::connect(reply, &QNetworkReply::finished, backend, [backend, state, reply]() {
        if (!reply) {
            return;
        }

        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString body = QString::fromUtf8(reply->readAll()).trimmed();
        const QString errorText = reply->errorString();
        const bool ok = (reply->error() == QNetworkReply::NoError) && (statusCode < 400 || statusCode == 0);

        reply->deleteLater();
        if (state->m_thermalStartReply == reply) {
            state->m_thermalStartReply = nullptr;
        }

        if (!state->m_thermalStreaming) {
            return;
        }

        if (!ok) {
            state->m_thermalStreaming = false;
            emit backend->thermalStreamingChanged();
            setThermalInfoText(backend, state,
                               QString("Thermal start failed (HTTP %1): %2")
                                       .arg(statusCode)
                                       .arg(body.isEmpty() ? errorText : body.left(160)));
            qWarning() << "[THERMAL] start request failed. status=" << statusCode
                       << "err=" << errorText
                       << "body=" << body.left(180);
            return;
        }

        openThermalWs(backend, state);
    });

    qInfo() << "[THERMAL] start request sent via API:" << thermalStartPath(state);
}

void BackendThermalService::stopThermalStream(Backend *backend, BackendPrivate *state)
{
    if (!state->m_thermalStreaming) {
        return;
    }

    state->m_thermalStreaming = false;
    resetThermalAssemblyState(state);
    state->m_thermalInfoText = "Thermal stream stopped";
    emit backend->thermalStreamingChanged();
    emit backend->thermalInfoTextChanged();

    if (state->m_thermalStartReply && state->m_thermalStartReply->isRunning()) {
        state->m_thermalStartReply->abort();
    }
    state->m_thermalStartReply = nullptr;

    if (usesThermalWsTransport(state)) {
        disconnectThermalWs(backend, state, true);
        postThermalStopRequest(backend, state);
        qInfo() << "[THERMAL] stream stopped via WS";
        return;
    }

    qInfo() << "[THERMAL] stream stopped via MQTT";
}

void BackendThermalService::setThermalPalette(Backend *backend, BackendPrivate *state, const QString &palette)
{
    QString normalized = palette.trimmed();
    if (normalized.compare("gray", Qt::CaseInsensitive) == 0) {
        normalized = "Gray";
    } else if (normalized.compare("iron", Qt::CaseInsensitive) == 0) {
        normalized = "Iron";
    } else {
        normalized = "Jet";
    }

    if (state->m_thermalPalette == normalized) {
        return;
    }
    state->m_thermalPalette = normalized;
    emit backend->thermalPaletteChanged();
}

void BackendThermalService::setThermalAutoRange(Backend *backend, BackendPrivate *state, bool enabled)
{
    if (state->m_thermalAutoRange == enabled) {
        return;
    }
    state->m_thermalAutoRange = enabled;
    emit backend->thermalAutoRangeChanged();
}

void BackendThermalService::setThermalAutoRangeWindowPercent(Backend *backend, BackendPrivate *state, int percent)
{
    int normalized = percent;
    if (normalized < 50)
        normalized = 50;
    if (normalized > 100)
        normalized = 100;
    if (state->m_thermalAutoRangeWindowPercent == normalized) {
        return;
    }
    state->m_thermalAutoRangeWindowPercent = normalized;
    emit backend->thermalAutoRangeWindowPercentChanged();
}

void BackendThermalService::setThermalManualRange(Backend *backend, BackendPrivate *state, int minValue, int maxValue)
{
    int nextMin = std::max(0, minValue);
    int nextMax = std::max(1, maxValue);
    if (nextMin >= nextMax) {
        nextMax = nextMin + 1;
    }
    if (state->m_thermalManualMin == nextMin && state->m_thermalManualMax == nextMax) {
        return;
    }
    state->m_thermalManualMin = nextMin;
    state->m_thermalManualMax = nextMax;
    emit backend->thermalManualRangeChanged();
}

void BackendThermalService::handleThermalChunkMessage(Backend *backend, BackendPrivate *state, const QByteArray &message)
{
    const bool debugThermal = (state->m_env.value("THERMAL_DEBUG", "0").trimmed() == "1");
    if (!state->m_thermalStreaming) {
        if (debugThermal) {
            static int droppedWhileStopped = 0;
            droppedWhileStopped++;
            if (droppedWhileStopped <= 5 || (droppedWhileStopped % 100) == 0) {
                qInfo() << "[THERMAL] chunk received while stream inactive:"
                        << "count=" << droppedWhileStopped
                        << "bytes=" << message.size();
            }
        }
        return;
    }
    ThermalChunkHeader header;
    if (!parseThermalChunkHeader(message, &header)) {
        if (debugThermal) {
            qWarning() << "[THERMAL] dropped invalid payload bytes=" << message.size();
        }
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (state->m_thermalCurrentFrameId >= 0 && state->m_thermalFrameStartedMs > 0
        && (nowMs - state->m_thermalFrameStartedMs) > kThermalFrameTimeoutMs) {
        qWarning() << "[THERMAL] frame timeout id=" << state->m_thermalCurrentFrameId;
        state->m_thermalCurrentFrameId = -1;
        state->m_thermalTotalChunksExpected = 0;
        state->m_thermalFrameStartedMs = 0;
        state->m_thermalHeaderMin = 0;
        state->m_thermalHeaderMax = 0;
        state->m_thermalFrameChunks.clear();
    }

    bool shouldStartNewFrame = false;
    if (header.hasFrameId) {
        shouldStartNewFrame = (state->m_thermalCurrentFrameId < 0 || state->m_thermalCurrentFrameId != header.frameId);
    } else {
        shouldStartNewFrame = (state->m_thermalTotalChunksExpected != static_cast<int>(header.total))
                           || (header.idx == 0 && !state->m_thermalFrameChunks.isEmpty());
    }

    if (shouldStartNewFrame) {
        if (state->m_thermalCurrentFrameId >= 0
            && !state->m_thermalFrameChunks.isEmpty()
            && state->m_thermalFrameChunks.size() != state->m_thermalTotalChunksExpected) {
            qWarning() << "[THERMAL] drop incomplete frame id=" << state->m_thermalCurrentFrameId
                       << "chunks=" << state->m_thermalFrameChunks.size()
                       << "/" << state->m_thermalTotalChunksExpected;
        }

        state->m_thermalCurrentFrameId = -1;
        state->m_thermalTotalChunksExpected = 0;
        state->m_thermalFrameStartedMs = 0;
        state->m_thermalHeaderMin = 0;
        state->m_thermalHeaderMax = 0;
        state->m_thermalFrameChunks.clear();
        state->m_thermalCurrentFrameId = header.hasFrameId ? header.frameId : 0;
        state->m_thermalTotalChunksExpected = static_cast<int>(header.total);
        state->m_thermalFrameStartedMs = nowMs;
    }

    if (state->m_thermalCurrentFrameId < 0) {
        state->m_thermalCurrentFrameId = header.hasFrameId ? header.frameId : 0;
    }
    if (state->m_thermalTotalChunksExpected <= 0) {
        state->m_thermalTotalChunksExpected = static_cast<int>(header.total);
        state->m_thermalFrameStartedMs = nowMs;
    }

    const QByteArray payload = message.mid(header.headerBytes);
    if (debugThermal && header.idx == 0) {
        qInfo() << "[THERMAL] frame begin id=" << state->m_thermalCurrentFrameId
                << "total=" << header.total
                << "payload=" << payload.size()
                << "headerMinMax=" << header.minVal << header.maxVal;
    }

    state->m_thermalHeaderMin = header.minVal;
    state->m_thermalHeaderMax = header.maxVal;
    state->m_thermalFrameChunks[static_cast<int>(header.idx)] = payload;

    if (state->m_thermalFrameChunks.size() == state->m_thermalTotalChunksExpected) {
        const int frameId = state->m_thermalCurrentFrameId;
        if (debugThermal) {
            qInfo() << "[THERMAL] frame chunks complete id=" << frameId
                    << "chunks=" << state->m_thermalTotalChunksExpected;
        }
        BackendThermalService::processThermalFrame(backend,
                                                   state,
                                                   state->m_thermalFrameChunks,
                                                   state->m_thermalTotalChunksExpected,
                                                   state->m_thermalHeaderMin,
                                                   state->m_thermalHeaderMax,
                                                   frameId);
        state->m_thermalCurrentFrameId = -1;
        state->m_thermalTotalChunksExpected = 0;
        state->m_thermalFrameStartedMs = 0;
        state->m_thermalHeaderMin = 0;
        state->m_thermalHeaderMax = 0;
        state->m_thermalFrameChunks.clear();
    }
}

void BackendThermalService::processThermalFrame(Backend *backend,
                                                BackendPrivate *state,
                                                const QMap<int, QByteArray> &chunks,
                                                int totalChunks,
                                                quint16 minVal,
                                                quint16 maxVal,
                                                int frameId)
{
    const bool debugThermal = (state->m_env.value("THERMAL_DEBUG", "0").trimmed() == "1");
    QByteArray full;
    full.reserve(kThermalFrameBytes16);
    for (int i = 0; i < totalChunks; ++i) {
        if (!chunks.contains(i)) {
            if (debugThermal) {
                qWarning() << "[THERMAL] missing chunk:" << i << "/" << totalChunks;
            }
            return;
        }
        full.append(chunks.value(i));
    }

    ThermalFrameEncoding encoding = ThermalFrameEncoding::Raw16;
    int expectedFrameBytes = 0;
    if (full.size() == kThermalFrameBytes16) {
        encoding = ThermalFrameEncoding::Raw16;
        expectedFrameBytes = kThermalFrameBytes16;
    } else if (full.size() == kThermalFrameBytes8) {
        encoding = ThermalFrameEncoding::Scaled8;
        expectedFrameBytes = kThermalFrameBytes8;
    } else if (full.size() < kThermalFrameBytes8) {
        if (debugThermal) {
            qWarning() << "[THERMAL] frame too small:" << full.size()
                       << "expected one of:" << kThermalFrameBytes8 << "," << kThermalFrameBytes16;
        }
        return;
    } else {
        qWarning() << "[THERMAL] unexpected frame size:" << full.size()
                   << "expected one of:" << kThermalFrameBytes8 << "," << kThermalFrameBytes16;
        return;
    }

    std::vector<int> raw;
    raw.reserve(kThermalPixelCount);
    std::vector<int> valid;
    valid.reserve(kThermalPixelCount);
    const uchar *buf = reinterpret_cast<const uchar *>(full.constData());

    if (encoding == ThermalFrameEncoding::Raw16) {
        for (int i = 0; i < kThermalPixelCount; ++i) {
            const int v = static_cast<int>(qFromBigEndian<quint16>(buf + i * 2));
            raw.push_back(v);
            if (v > 1000 && v < 30000) {
                valid.push_back(v);
            }
        }
    } else {
        const int baseMin = static_cast<int>(minVal);
        const int baseMax = static_cast<int>(maxVal);
        const int range = std::max(1, baseMax - baseMin);
        for (int i = 0; i < kThermalPixelCount; ++i) {
            const int pixel8 = static_cast<int>(buf[i]);
            const int v = (baseMax > baseMin)
                    ? (baseMin + ((pixel8 * range + 127) / 255))
                    : (pixel8 << 8);
            raw.push_back(v);
            if (v > 1000 && v < 30000) {
                valid.push_back(v);
            }
        }
    }

    int frameMin = static_cast<int>(minVal);
    int frameMax = static_cast<int>(maxVal);
    if (state->m_thermalAutoRange) {
        if (!valid.empty()) {
            const int window = std::max(50, std::min(100, state->m_thermalAutoRangeWindowPercent));
            const int trim = (100 - window) / 2;
            const int lowP = trim;
            const int highP = 100 - trim;
            frameMin = percentileValue(valid, lowP);
            frameMax = percentileValue(valid, highP);
            if (frameMax - frameMin < 100) {
                frameMax = frameMin + 100;
            }
        }
    } else {
        frameMin = state->m_thermalManualMin;
        frameMax = state->m_thermalManualMax;
    }
    if (frameMax <= frameMin) {
        frameMax = frameMin + 1;
    }

    QImage image(kThermalWidth, kThermalHeight, QImage::Format_RGB32);
    for (int y = 0; y < kThermalHeight; ++y) {
        for (int x = 0; x < kThermalWidth; ++x) {
            const int src = raw[static_cast<size_t>(y * kThermalWidth + x)];
            int normalized = ((src - frameMin) * 255) / (frameMax - frameMin);
            if (normalized < 0)
                normalized = 0;
            if (normalized > 255)
                normalized = 255;
            if (state->m_thermalPalette == "Gray") {
                image.setPixel(x, y, qRgb(normalized, normalized, normalized));
            } else if (state->m_thermalPalette == "Iron") {
                image.setPixel(x, y, ironColor(static_cast<unsigned char>(normalized)));
            } else {
                image.setPixel(x, y, jetColor(static_cast<unsigned char>(normalized)));
            }
        }
    }

    QImage scaled = image.scaled(800, 600, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    scaled.save(&buffer, "PNG");
    const QString nextDataUrl = QString("data:image/png;base64,%1").arg(QString::fromLatin1(png.toBase64()));

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (state->m_thermalLastDisplayMs > 0) {
        const double deltaSec = static_cast<double>(nowMs - state->m_thermalLastDisplayMs) / 1000.0;
        if (deltaSec > 0.0) {
            const double instant = 1.0 / deltaSec;
            state->m_thermalDisplayFps =
                    (state->m_thermalDisplayFps == 0.0) ? instant : ((state->m_thermalDisplayFps * 0.8) + (instant * 0.2));
        }
    }
    state->m_thermalLastDisplayMs = nowMs;
    state->m_thermalLastFrameId = frameId;

    const QString rangeMode = state->m_thermalAutoRange ? QString("Auto(%1%)").arg(state->m_thermalAutoRangeWindowPercent) : "Manual";
    const QString info = QString("Frame: %1 | FPS: %2 | Format: %3 | Palette: %4 | %5 Range: %6 ~ %7")
                                  .arg(frameId)
                                  .arg(state->m_thermalDisplayFps, 0, 'f', 2)
                                  .arg(thermalEncodingLabel(encoding))
                                  .arg(state->m_thermalPalette)
                                  .arg(rangeMode)
                                  .arg(frameMin)
                                  .arg(frameMax);
    if (state->m_thermalFrameDataUrl != nextDataUrl) {
        state->m_thermalFrameDataUrl = nextDataUrl;
        emit backend->thermalFrameDataUrlChanged();
    }
    if (state->m_thermalInfoText != info) {
        state->m_thermalInfoText = info;
        emit backend->thermalInfoTextChanged();
    }
    if (debugThermal) {
        qInfo() << "[THERMAL] frame rendered id=" << frameId
                << "bytes=" << full.size()
                << "expectedBytes=" << expectedFrameBytes
                << "encoding=" << thermalEncodingLabel(encoding)
                << "range=" << frameMin << frameMax
                << "fps=" << state->m_thermalDisplayFps
                << "palette=" << state->m_thermalPalette;
    }
}

