#include "internal/cctv/BackendCctv3dMapService.h"

#include "Backend.h"
#include "internal/core/Backend_p.h"

#include <QAbstractSocket>
#include <QBuffer>
#include <QColor>
#include <QDateTime>
#include <QDebug>
#include <QtEndian>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QNetworkReply>
#include <QPainter>
#include <QRegularExpression>
#include <QSslError>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace {
constexpr int kCctv3dMapMoveStatusPollIntervalMs = 400;
constexpr int kCctv3dMapMoveStatusMaxAttempts = 20;
constexpr int kCctv3dMapAutofocusSettleMs = 5000;
constexpr int kCctv3dMapStartRetryDelayMs = 600;
constexpr int kCctv3dMapStartRetryMaxAttempts = 10;
constexpr int kCctv3dMapStreamRetryDelayMs = 800;
constexpr int kCctv3dMapStreamRetryMaxAttempts = 10;
constexpr int kCctv3dMapStopRetryDelayMs = 600;
constexpr int kCctv3dMapStopRetryMaxAttempts = 0;
constexpr qint64 kCctv3dMapStopMinIntervalMs = 3000;
constexpr qint64 kCctv3dMapRenderIntervalMs = 66;
constexpr int kCctv3dMapMaxWsBufferBytes = 64 * 1024 * 1024;

int defaultPortForScheme(const QString &scheme)
{
    return (scheme.compare("https", Qt::CaseInsensitive) == 0) ? 443 : 80;
}

bool sameApiOrigin(const QUrl &requestUrl, const QUrl &apiBase)
{
    if (!requestUrl.isValid() || !apiBase.isValid()) {
        return false;
    }

    const bool sameScheme = (requestUrl.scheme().compare(apiBase.scheme(), Qt::CaseInsensitive) == 0);
    const bool sameHost = (requestUrl.host().compare(apiBase.host(), Qt::CaseInsensitive) == 0);
    const bool samePort = (requestUrl.port(defaultPortForScheme(requestUrl.scheme()))
                           == apiBase.port(defaultPortForScheme(apiBase.scheme())));
    return sameScheme && sameHost && samePort;
}

void applyCctvAuthIfNeeded(Backend *backend, BackendPrivate *state, QNetworkRequest &request)
{
    if (!backend || !state) {
        return;
    }
    if (state->m_authToken.trimmed().isEmpty()) {
        return;
    }

    const QUrl apiBase(backend->serverUrl());
    if (!sameApiOrigin(request.url(), apiBase)) {
        return;
    }

    request.setRawHeader("Authorization", QByteArray("Bearer ") + state->m_authToken.toUtf8());
}

void applyCctvSslIfNeeded(BackendPrivate *state, QNetworkRequest &request)
{
    if (!state) {
        return;
    }
    if (request.url().scheme().compare("https", Qt::CaseInsensitive) != 0) {
        return;
    }
    if (state->m_sslConfigReady) {
        request.setSslConfiguration(state->m_sslConfig);
    }
}

void attachCctvIgnoreSslErrors(BackendPrivate *state, QNetworkReply *reply, const QString &tag)
{
    if (!reply || !state) {
        return;
    }
    QObject::connect(reply, &QNetworkReply::sslErrors, reply, [reply, tag, state](const QList<QSslError> &errors) {
        for (const auto &err : errors) {
            qWarning() << "[" << tag << "][SSL]" << err.errorString();
        }
        if (state->m_sslIgnoreErrors) {
            reply->ignoreSslErrors();
        }
    });
}

QUrl buildCctvApiUrl(Backend *backend,
                     const QString &path,
                     const QMap<QString, QString> &query = {})
{
    QUrl url(backend ? backend->serverUrl() : QString());
    QString cleanPath = path.trimmed();
    if (!cleanPath.startsWith('/')) {
        cleanPath.prepend('/');
    }
    url.setPath(cleanPath);

    QUrlQuery q(url);
    for (auto it = query.constBegin(); it != query.constEnd(); ++it) {
        q.addQueryItem(it.key(), it.value());
    }
    url.setQuery(q);
    return url;
}

QNetworkRequest makeCctvApiJsonRequest(Backend *backend,
                                       BackendPrivate *state,
                                       const QString &path,
                                       const QMap<QString, QString> &query = {})
{
    QNetworkRequest request(buildCctvApiUrl(backend, path, query));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    applyCctvSslIfNeeded(state, request);
    applyCctvAuthIfNeeded(backend, state, request);
    return request;
}

bool parseCctvControlApiResult(const QString &body, bool httpOk, QString *outApiStatus, QString *outApiMessage)
{
    QString apiStatus;
    QString apiMessage = body.trimmed();

    const QJsonDocument parsed = QJsonDocument::fromJson(body.toUtf8());
    if (parsed.isObject()) {
        const QJsonObject obj = parsed.object();
        if (obj.contains("status") && obj.value("status").isString()) {
            apiStatus = obj.value("status").toString();
        }
        if (obj.contains("message") && obj.value("message").isString()) {
            apiMessage = obj.value("message").toString().trimmed();
        }
    }

    if (outApiStatus) {
        *outApiStatus = apiStatus;
    }
    if (outApiMessage) {
        *outApiMessage = apiMessage;
    }

    const bool statusBad = !apiStatus.isEmpty()
                           && apiStatus.compare("OK", Qt::CaseInsensitive) != 0
                           && apiStatus.compare("ACCEPTED", Qt::CaseInsensitive) != 0;
    const bool apiReportedError =
        apiMessage.startsWith("Error:", Qt::CaseInsensitive) || statusBad;
    return httpOk && !apiReportedError;
}

QByteArray buildPngDataUrl(const QImage &image)
{
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return QByteArray("data:image/png;base64,") + png.toBase64();
}

bool loadImageFromPayload(const QByteArray &payload, QImage *outImage, int *outOffset)
{
    if (!outImage || payload.isEmpty()) {
        return false;
    }

    auto tryLoad = [&](const QByteArray &bytes, int offset) {
        QImage image;
        if (!image.loadFromData(bytes)) {
            return false;
        }
        *outImage = image;
        if (outOffset) {
            *outOffset = offset;
        }
        return true;
    };

    if (tryLoad(payload, 0)) {
        return true;
    }

    static const QList<QByteArray> kSignatures {
        QByteArray::fromHex("89504E470D0A1A0A"), // PNG
        QByteArray::fromHex("FFD8FF"),           // JPEG
        QByteArray("RIFF"),                      // WEBP (RIFF....WEBP)
        QByteArray("BM"),                        // BMP
    };

    int bestOffset = -1;
    for (const QByteArray &signature : kSignatures) {
        const int idx = payload.indexOf(signature);
        if (idx >= 0 && (bestOffset < 0 || idx < bestOffset)) {
            bestOffset = idx;
        }
    }

    if (bestOffset > 0) {
        return tryLoad(payload.mid(bestOffset), bestOffset);
    }

    return false;
}

struct ParsedRgbdFrame {
    quint32 frameIdx = 0;
    int width = 0;
    int height = 0;
    QByteArray depthBytes;
    QByteArray bgrBytes;
};

struct ParsedPcFrame {
    quint32 frameIdx = 0;
    int width = 0;
    int height = 0;
    QByteArray imageBytes;
};

quint32 readLeU32(const char *p)
{
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(p));
}

bool peekRgbdFrameHeader(const QByteArray &buffer,
                         quint32 *outFrameIdx,
                         int *outWidth,
                         int *outHeight,
                         int *outDepthBytes,
                         int *outBgrBytes,
                         int *outPacketBytes)
{
    if (buffer.size() < 20) {
        return false;
    }

    const char *base = buffer.constData();
    const quint32 frameIdx = readLeU32(base + 0);
    const quint32 w = readLeU32(base + 4);
    const quint32 h = readLeU32(base + 8);
    const quint32 depthBytes = readLeU32(base + 12);
    const quint32 bgrBytes = readLeU32(base + 16);
    if (w == 0 || h == 0) {
        return false;
    }

    const qint64 pixelCount = static_cast<qint64>(w) * static_cast<qint64>(h);
    if (pixelCount <= 0 || pixelCount > (4096LL * 4096LL)) {
        return false;
    }
    if (depthBytes != static_cast<quint32>(pixelCount * 4)) {
        return false;
    }
    if (bgrBytes != static_cast<quint32>(pixelCount * 3)) {
        return false;
    }

    const qint64 packetBytes = 20LL + static_cast<qint64>(depthBytes) + static_cast<qint64>(bgrBytes);
    if (packetBytes <= 20 || packetBytes > (64LL * 1024LL * 1024LL)) {
        return false;
    }

    if (outFrameIdx) {
        *outFrameIdx = frameIdx;
    }
    if (outWidth) {
        *outWidth = static_cast<int>(w);
    }
    if (outHeight) {
        *outHeight = static_cast<int>(h);
    }
    if (outDepthBytes) {
        *outDepthBytes = static_cast<int>(depthBytes);
    }
    if (outBgrBytes) {
        *outBgrBytes = static_cast<int>(bgrBytes);
    }
    if (outPacketBytes) {
        *outPacketBytes = static_cast<int>(packetBytes);
    }
    return true;
}

bool peekPcFrameHeader(const QByteArray &buffer,
                       quint32 *outFrameIdx,
                       int *outWidth,
                       int *outHeight,
                       int *outPayloadBytes,
                       int *outPacketBytes)
{
    if (buffer.size() < 16) {
        return false;
    }

    const char *base = buffer.constData();
    const quint32 frameIdx = readLeU32(base + 0);
    const quint32 w = readLeU32(base + 4);
    const quint32 h = readLeU32(base + 8);
    const quint32 payloadBytes = readLeU32(base + 12);
    if (w == 0 || h == 0 || w > 8192 || h > 8192) {
        return false;
    }
    if (payloadBytes == 0 || payloadBytes > (16U * 1024U * 1024U)) {
        return false;
    }

    const qint64 packetBytes = 16LL + static_cast<qint64>(payloadBytes);
    if (packetBytes > (32LL * 1024LL * 1024LL)) {
        return false;
    }

    if (outFrameIdx) {
        *outFrameIdx = frameIdx;
    }
    if (outWidth) {
        *outWidth = static_cast<int>(w);
    }
    if (outHeight) {
        *outHeight = static_cast<int>(h);
    }
    if (outPayloadBytes) {
        *outPayloadBytes = static_cast<int>(payloadBytes);
    }
    if (outPacketBytes) {
        *outPacketBytes = static_cast<int>(packetBytes);
    }
    return true;
}

bool tryConsumeRgbdFrame(QByteArray *buffer, ParsedRgbdFrame *out)
{
    if (!buffer || !out) {
        return false;
    }

    quint32 frameIdx = 0;
    int width = 0;
    int height = 0;
    int depthBytes = 0;
    int bgrBytes = 0;
    int packetBytes = 0;
    if (!peekRgbdFrameHeader(*buffer, &frameIdx, &width, &height, &depthBytes, &bgrBytes, &packetBytes)) {
        return false;
    }
    if (buffer->size() < packetBytes) {
        return false;
    }

    out->frameIdx = frameIdx;
    out->width = width;
    out->height = height;
    out->depthBytes = buffer->mid(20, depthBytes);
    out->bgrBytes = buffer->mid(20 + depthBytes, bgrBytes);
    buffer->remove(0, packetBytes);
    return true;
}

bool tryConsumePcFrame(QByteArray *buffer, ParsedPcFrame *out)
{
    if (!buffer || !out) {
        return false;
    }

    quint32 frameIdx = 0;
    int width = 0;
    int height = 0;
    int payloadBytes = 0;
    int packetBytes = 0;
    if (!peekPcFrameHeader(*buffer, &frameIdx, &width, &height, &payloadBytes, &packetBytes)) {
        return false;
    }
    if (buffer->size() < packetBytes) {
        return false;
    }

    out->frameIdx = frameIdx;
    out->width = width;
    out->height = height;
    out->imageBytes = buffer->mid(16, payloadBytes);
    buffer->remove(0, packetBytes);
    return true;
}

struct CloudPoint {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    uchar r = 0;
    uchar g = 0;
    uchar b = 0;
    int gx = -1;
    int gy = -1;
};

bool renderRgbdPointCloudImage(const QByteArray &depthBytes,
                               const QByteArray &bgrBytes,
                               int width,
                               int height,
                               double rxDeg,
                               double ryDeg,
                               QImage *outImage)
{
    if (!outImage || width <= 0 || height <= 0) {
        return false;
    }

    const qint64 pixelCount = static_cast<qint64>(width) * static_cast<qint64>(height);
    if (depthBytes.size() != pixelCount * 4 || bgrBytes.size() != pixelCount * 3) {
        return false;
    }

    constexpr float kMinDepth = 0.1f;
    constexpr float kMaxDepth = 80.0f;
    constexpr int kStride = 3;
    constexpr int kOutW = 960;
    constexpr int kOutH = 720;
    constexpr float kPad = 14.0f;
    constexpr float kPi = 3.14159265358979323846f;
    constexpr bool kEnableWireframeOverlay = true;
    constexpr float kWireDepthDeltaMax = 0.80f;
    constexpr int kWirePixelSpanMax = 32;

    const float hfov = 109.0f * (kPi / 180.0f);
    const float vfov = 55.0f * (kPi / 180.0f);
    const float fx = (static_cast<float>(width) * 0.5f) / std::tan(hfov * 0.5f);
    const float fy = (static_cast<float>(height) * 0.5f) / std::tan(vfov * 0.5f);
    const float cx = static_cast<float>(width) * 0.5f;
    const float cy = static_cast<float>(height) * 0.5f;

    const float rx = static_cast<float>(rxDeg) * (kPi / 180.0f);
    const float ry = static_cast<float>(ryDeg) * (kPi / 180.0f);
    const float cosRx = std::cos(rx);
    const float sinRx = std::sin(rx);
    const float cosRy = std::cos(ry);
    const float sinRy = std::sin(ry);

    const char *depthRaw = depthBytes.constData();
    const uchar *bgrRaw = reinterpret_cast<const uchar *>(bgrBytes.constData());

    const int sampleW = (width + kStride - 1) / kStride;
    const int sampleH = (height + kStride - 1) / kStride;

    std::vector<CloudPoint> points;
    points.reserve(static_cast<size_t>(sampleW * sampleH));
    std::vector<int> gridToPoint(static_cast<size_t>(sampleW * sampleH), -1);

    float minX = std::numeric_limits<float>::infinity();
    float maxX = -std::numeric_limits<float>::infinity();
    float minY = std::numeric_limits<float>::infinity();
    float maxY = -std::numeric_limits<float>::infinity();

    for (int gy = 0, y = 0; y < height; y += kStride, ++gy) {
        for (int gx = 0, x = 0; x < width; x += kStride, ++gx) {
            const int idx = y * width + x;
            float d = 0.0f;
            std::memcpy(&d, depthRaw + (idx * 4), sizeof(float));
            if (!std::isfinite(d) || d < kMinDepth || d > kMaxDepth) {
                continue;
            }

            const float X = (static_cast<float>(x) - cx) * d / fx;
            const float Y = (static_cast<float>(y) - cy) * d / fy;
            const float Z = d;

            const float x1 = (X * cosRy) + (Z * sinRy);
            const float z1 = (-X * sinRy) + (Z * cosRy);
            const float y1 = (Y * cosRx) - (z1 * sinRx);
            const float z2 = (Y * sinRx) + (z1 * cosRx);
            if (!std::isfinite(x1) || !std::isfinite(y1) || !std::isfinite(z2)) {
                continue;
            }

            const int bgrOffset = idx * 3;
            CloudPoint p;
            p.x = x1;
            p.y = y1;
            p.z = z2;
            p.b = bgrRaw[bgrOffset + 0];
            p.g = bgrRaw[bgrOffset + 1];
            p.r = bgrRaw[bgrOffset + 2];
            p.gx = gx;
            p.gy = gy;
            gridToPoint[static_cast<size_t>(gy * sampleW + gx)] = static_cast<int>(points.size());
            points.push_back(p);

            minX = std::min(minX, p.x);
            maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y);
            maxY = std::max(maxY, p.y);
        }
    }

    if (points.empty()) {
        return false;
    }

    const float spanX = std::max(maxX - minX, 1e-5f);
    const float spanY = std::max(maxY - minY, 1e-5f);
    const float scaleX = (static_cast<float>(kOutW) - 2.0f * kPad) / spanX;
    const float scaleY = (static_cast<float>(kOutH) - 2.0f * kPad) / spanY;
    const float scale = std::min(scaleX, scaleY);
    if (!std::isfinite(scale) || scale <= 0.0f) {
        return false;
    }

    const float offsetX = ((static_cast<float>(kOutW) - spanX * scale) * 0.5f) - (minX * scale);
    const float offsetY = ((static_cast<float>(kOutH) - spanY * scale) * 0.5f) - (minY * scale);

    QImage image(kOutW, kOutH, QImage::Format_RGB888);
    image.fill(Qt::black);
    std::vector<float> zBuffer(static_cast<size_t>(kOutW * kOutH), std::numeric_limits<float>::infinity());
    std::vector<int> pointScreenX(points.size(), -1);
    std::vector<int> pointScreenY(points.size(), -1);

    for (size_t i = 0; i < points.size(); ++i) {
        const CloudPoint &p = points[i];
        const int sx = static_cast<int>(p.x * scale + offsetX);
        const int sy = static_cast<int>(p.y * scale + offsetY);
        if (sx < 0 || sx >= kOutW || sy < 0 || sy >= kOutH) {
            continue;
        }

        pointScreenX[i] = sx;
        pointScreenY[i] = sy;

        const int pixIdx = sy * kOutW + sx;
        if (p.z >= zBuffer[static_cast<size_t>(pixIdx)]) {
            continue;
        }
        zBuffer[static_cast<size_t>(pixIdx)] = p.z;

        image.setPixelColor(sx, sy, QColor(p.r, p.g, p.b));
    }

    if (kEnableWireframeOverlay) {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, false);
        auto tryDrawEdge = [&](int ia, int ib) {
            if (ia < 0 || ib < 0) {
                return;
            }
            const int ax = pointScreenX[static_cast<size_t>(ia)];
            const int ay = pointScreenY[static_cast<size_t>(ia)];
            const int bx = pointScreenX[static_cast<size_t>(ib)];
            const int by = pointScreenY[static_cast<size_t>(ib)];
            if (ax < 0 || ay < 0 || bx < 0 || by < 0) {
                return;
            }

            const CloudPoint &a = points[static_cast<size_t>(ia)];
            const CloudPoint &b = points[static_cast<size_t>(ib)];
            if (std::fabs(a.z - b.z) > kWireDepthDeltaMax) {
                return;
            }
            if (std::abs(ax - bx) > kWirePixelSpanMax || std::abs(ay - by) > kWirePixelSpanMax) {
                return;
            }
            const int r = (static_cast<int>(a.r) + static_cast<int>(b.r)) / 2;
            const int g = (static_cast<int>(a.g) + static_cast<int>(b.g)) / 2;
            const int bch = (static_cast<int>(a.b) + static_cast<int>(b.b)) / 2;
            painter.setPen(QPen(QColor(r, g, bch), 1));
            painter.drawLine(ax, ay, bx, by);
        };

        for (int gy = 0; gy < sampleH; ++gy) {
            for (int gx = 0; gx < sampleW; ++gx) {
                const int i0 = gridToPoint[static_cast<size_t>(gy * sampleW + gx)];
                if (i0 < 0) {
                    continue;
                }
                if (gx + 1 < sampleW) {
                    const int iRight = gridToPoint[static_cast<size_t>(gy * sampleW + (gx + 1))];
                    tryDrawEdge(i0, iRight);
                }
                if (gy + 1 < sampleH) {
                    const int iDown = gridToPoint[static_cast<size_t>((gy + 1) * sampleW + gx)];
                    tryDrawEdge(i0, iDown);
                }
            }
        }
    }

    *outImage = image;
    return true;
}

void renderCctv3dMapFromCachedRgbd(Backend *backend, BackendPrivate *state, bool force)
{
    if (!backend || !state) {
        return;
    }
    if (state->m_cctv3dMapRgbdWidth <= 0 || state->m_cctv3dMapRgbdHeight <= 0) {
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (!force && state->m_cctv3dMapLastRenderMs != 0
        && (nowMs - state->m_cctv3dMapLastRenderMs) < kCctv3dMapRenderIntervalMs) {
        return;
    }

    QImage rendered;
    if (!renderRgbdPointCloudImage(state->m_cctv3dMapRgbdDepthBytes,
                                   state->m_cctv3dMapRgbdBgrBytes,
                                   state->m_cctv3dMapRgbdWidth,
                                   state->m_cctv3dMapRgbdHeight,
                                   state->m_cctv3dMapViewRx,
                                   state->m_cctv3dMapViewRy,
                                   &rendered)) {
        return;
    }

    state->m_cctv3dMapLastRenderMs = nowMs;
    state->m_cctv3dMapFrameDataUrl = QString::fromLatin1(buildPngDataUrl(rendered));
    emit backend->cctv3dMapFrameDataUrlChanged();
}

bool parseZoomMovingState(const QString &body, int channel, bool *known, bool *moving)
{
    if (!known || !moving) {
        return false;
    }
    *known = false;
    *moving = false;

    const QRegularExpression byChannelText(
        QString("Channel\\.%1\\.Zoom\\s*=\\s*(Idle|Moving)").arg(channel),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression anyChannelText(
        QStringLiteral("Zoom\\s*=\\s*(Idle|Moving)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression byChannelJson(
        QStringLiteral("\\\"Channel\\\"\\s*:\\s*%1[^\\}]*\\\"Zoom\\\"\\s*:\\s*\\\"(Idle|Moving)\\\"")
            .arg(channel),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression anyChannelJson(
        QStringLiteral("\\\"Zoom\\\"\\s*:\\s*\\\"(Idle|Moving)\\\""),
        QRegularExpression::CaseInsensitiveOption);

    QRegularExpressionMatch m = byChannelText.match(body);
    if (!m.hasMatch()) {
        m = byChannelJson.match(body);
    }
    if (!m.hasMatch()) {
        m = anyChannelText.match(body);
    }
    if (!m.hasMatch()) {
        m = anyChannelJson.match(body);
    }
    if (!m.hasMatch()) {
        return false;
    }

    const QString st = m.captured(1).trimmed();
    *known = true;
    *moving = (st.compare("Moving", Qt::CaseInsensitive) == 0);
    return true;
}

void ensureCctv3dMapStepTimer(Backend *backend, BackendPrivate *state)
{
    if (state->m_cctv3dMapStepTimer) {
        return;
    }
    state->m_cctv3dMapStepTimer = new QTimer(backend);
    state->m_cctv3dMapStepTimer->setSingleShot(true);
    QObject::connect(state->m_cctv3dMapStepTimer, &QTimer::timeout, backend, [backend, state]() {
        BackendCctv3dMapService::runCctv3dMapSequenceStep(
            backend,
            state,
            state->m_cctv3dMapSequenceToken,
            state->m_cctv3dMapPendingStep);
    });
}

} // namespace

bool BackendCctv3dMapService::startCctv3dMapPrepareSequence(Backend *backend, BackendPrivate *state, int cameraIndex)
{
    if (cameraIndex < 0 || cameraIndex > 3) {
        emit backend->cameraControlMessage("3D Map prepare failed: invalid camera index", true);
        return false;
    }

    state->m_cctv3dMapSequenceToken += 1;
    state->m_cctv3dMapPendingStep = 0;
    if (state->m_cctv3dMapStepTimer) {
        state->m_cctv3dMapStepTimer->stop();
    }
    BackendCctv3dMapService::disconnectCctvStreamWs(backend, state, true);

    state->m_cctv3dMapCameraIndex = cameraIndex;
    state->m_cctv3dMapMoveStatusPollCount = 0;
    state->m_cctv3dMapStartRetryCount = 0;
    state->m_cctv3dMapStreamRetryCount = 0;
    state->m_cctv3dMapStopInFlight = false;
    state->m_cctv3dMapLastStopRequestMs = 0;
    state->m_cctv3dMapPrepareOnly = true;
    state->m_cctv3dMapViewRx = -20.0;
    state->m_cctv3dMapViewRy = 35.0;
    state->m_cctv3dMapWsStreamBuffer.clear();
    ensureCctv3dMapStepTimer(backend, state);

    emit backend->cameraControlMessage("3D Map 준비: zoom out", false);
    if (!backend->sunapiZoomOut(cameraIndex)) {
        emit backend->cameraControlMessage("3D Map 준비 실패: zoom out 실패", true);
        return false;
    }

    state->m_cctv3dMapPendingStep = 1;
    state->m_cctv3dMapStepTimer->start(kCctv3dMapMoveStatusPollIntervalMs);
    emit backend->cameraControlMessage("3D Map 준비: 줌 안정화 대기", false);
    return true;
}

bool BackendCctv3dMapService::startCctv3dMapSequence(Backend *backend, BackendPrivate *state, int cameraIndex)
{
    if (cameraIndex < 0 || cameraIndex > 3) {
        emit backend->cameraControlMessage("3D Map start failed: invalid camera index", true);
        return false;
    }

    state->m_cctv3dMapSequenceToken += 1;
    state->m_cctv3dMapPendingStep = 0;
    if (state->m_cctv3dMapStepTimer) {
        state->m_cctv3dMapStepTimer->stop();
    }
    BackendCctv3dMapService::disconnectCctvStreamWs(backend, state, true);

    state->m_cctv3dMapCameraIndex = cameraIndex;
    state->m_cctv3dMapMoveStatusPollCount = 0;
    state->m_cctv3dMapStartRetryCount = 0;
    state->m_cctv3dMapStreamRetryCount = 0;
    state->m_cctv3dMapStopInFlight = false;
    state->m_cctv3dMapLastStopRequestMs = 0;
    state->m_cctv3dMapPrepareOnly = false;
    state->m_cctv3dMapViewRx = -20.0;
    state->m_cctv3dMapViewRy = 35.0;
    state->m_cctv3dMapFrameCount = 0;
    state->m_cctv3dMapTotalBytes = 0;
    state->m_cctv3dMapLastRenderMs = 0;
    state->m_cctv3dMapRgbdWidth = 0;
    state->m_cctv3dMapRgbdHeight = 0;
    state->m_cctv3dMapRgbdDepthBytes.clear();
    state->m_cctv3dMapRgbdBgrBytes.clear();
    state->m_cctv3dMapWsStreamBuffer.clear();
    if (!state->m_cctv3dMapFrameDataUrl.isEmpty()) {
        state->m_cctv3dMapFrameDataUrl.clear();
        emit backend->cctv3dMapFrameDataUrlChanged();
    }

    emit backend->cameraControlMessage("3D Map: start API requested", false);
    return BackendCctv3dMapService::postCctvControlStart(
        backend,
        state,
        state->m_cctv3dMapSequenceToken);
}

bool BackendCctv3dMapService::pauseCctv3dMapSequence(Backend *backend, BackendPrivate *state)
{
    if (!backend || !state || !state->m_manager) {
        if (backend) {
            emit backend->cameraControlMessage("3D Map pause failed: network manager unavailable", true);
        }
        return false;
    }

    QNetworkRequest request = makeCctvApiJsonRequest(backend, state, "/cctv/control/pause");

    QNetworkReply *reply = state->m_manager->post(request, QByteArray());
    attachCctvIgnoreSslErrors(state, reply, "CCTV_3DMAP_PAUSE");
    QObject::connect(reply, &QNetworkReply::finished, backend, [backend, reply]() {
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString body = QString::fromUtf8(reply->readAll()).trimmed();
        const bool httpOk = (reply->error() == QNetworkReply::NoError) && (statusCode < 400 || statusCode == 0);

        QString apiStatus;
        QString apiMessage;
        const bool semanticOk = parseCctvControlApiResult(body, httpOk, &apiStatus, &apiMessage);
        if (!semanticOk) {
            const QString err = httpOk
                                    ? QString("3D Map pause command failed: %1").arg(apiMessage.left(180))
                                    : QString("3D Map pause failed (HTTP %1): %2")
                                          .arg(statusCode)
                                          .arg(reply->errorString());
            qWarning() << "[CCTV_3DMAP]" << err << "body=" << body.left(180);
            emit backend->cameraControlMessage(err, true);
            reply->deleteLater();
            return;
        }

        qInfo() << "[CCTV_3DMAP] pause requested. status=" << statusCode << "body=" << body.left(180);
        emit backend->cameraControlMessage("3D Map pause requested", false);
        reply->deleteLater();
    });

    return true;
}

bool BackendCctv3dMapService::resumeCctv3dMapSequence(Backend *backend, BackendPrivate *state)
{
    if (!backend || !state || !state->m_manager) {
        if (backend) {
            emit backend->cameraControlMessage("3D Map resume failed: network manager unavailable", true);
        }
        return false;
    }

    QNetworkRequest request = makeCctvApiJsonRequest(backend, state, "/cctv/control/resume");

    QNetworkReply *reply = state->m_manager->post(request, QByteArray());
    attachCctvIgnoreSslErrors(state, reply, "CCTV_3DMAP_RESUME");
    QObject::connect(reply, &QNetworkReply::finished, backend, [backend, reply]() {
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString body = QString::fromUtf8(reply->readAll()).trimmed();
        const bool httpOk = (reply->error() == QNetworkReply::NoError) && (statusCode < 400 || statusCode == 0);

        QString apiStatus;
        QString apiMessage;
        const bool semanticOk = parseCctvControlApiResult(body, httpOk, &apiStatus, &apiMessage);
        if (!semanticOk) {
            const QString err = httpOk
                                    ? QString("3D Map resume command failed: %1").arg(apiMessage.left(180))
                                    : QString("3D Map resume failed (HTTP %1): %2")
                                          .arg(statusCode)
                                          .arg(reply->errorString());
            qWarning() << "[CCTV_3DMAP]" << err << "body=" << body.left(180);
            emit backend->cameraControlMessage(err, true);
            reply->deleteLater();
            return;
        }

        qInfo() << "[CCTV_3DMAP] resume requested. status=" << statusCode << "body=" << body.left(180);
        emit backend->cameraControlMessage("3D Map resume requested", false);
        reply->deleteLater();
    });

    return true;
}

void BackendCctv3dMapService::stopCctv3dMapSequence(Backend *backend, BackendPrivate *state)
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (state->m_cctv3dMapLastStopRequestMs > 0
        && (nowMs - state->m_cctv3dMapLastStopRequestMs) < kCctv3dMapStopMinIntervalMs) {
        emit backend->cameraControlMessage("3D Map stop ignored (cooldown)", false);
        return;
    }
    if (state->m_cctv3dMapStopInFlight) {
        emit backend->cameraControlMessage("3D Map stop already in progress", false);
        return;
    }
    state->m_cctv3dMapStopInFlight = true;
    state->m_cctv3dMapLastStopRequestMs = nowMs;

    state->m_cctv3dMapSequenceToken += 1;
    const int stopToken = state->m_cctv3dMapSequenceToken;
    state->m_cctv3dMapPendingStep = 0;
    state->m_cctv3dMapMoveStatusPollCount = 0;
    state->m_cctv3dMapStartRetryCount = 0;
    state->m_cctv3dMapStreamRetryCount = 0;
    state->m_cctv3dMapCameraIndex = -1;
    state->m_cctv3dMapPrepareOnly = false;
    state->m_cctv3dMapRgbdWidth = 0;
    state->m_cctv3dMapRgbdHeight = 0;
    state->m_cctv3dMapRgbdDepthBytes.clear();
    state->m_cctv3dMapRgbdBgrBytes.clear();
    state->m_cctv3dMapWsStreamBuffer.clear();
    state->m_cctv3dMapLastRenderMs = 0;
    if (state->m_cctv3dMapStepTimer) {
        state->m_cctv3dMapStepTimer->stop();
    }
    BackendCctv3dMapService::disconnectCctvStreamWs(backend, state, true);
    if (!state->m_cctv3dMapFrameDataUrl.isEmpty()) {
        state->m_cctv3dMapFrameDataUrl.clear();
        emit backend->cctv3dMapFrameDataUrlChanged();
    }

    emit backend->cameraControlMessage("3D Map stop requested", false);
    BackendCctv3dMapService::postCctvControlStopWithRetry(backend, state, stopToken, 0);
}

void BackendCctv3dMapService::postCctvControlStopWithRetry(Backend *backend, BackendPrivate *state, int sequenceToken, int attempt)
{
    if (!backend || !state) {
        return;
    }
    if (!state->m_manager) {
        state->m_cctv3dMapStopInFlight = false;
        emit backend->cameraControlMessage("3D Map stop failed: network manager unavailable", true);
        return;
    }
    if (sequenceToken != state->m_cctv3dMapSequenceToken) {
        return;
    }

    QNetworkRequest request = makeCctvApiJsonRequest(backend, state, "/cctv/control/stop");
    QNetworkReply *reply = state->m_manager->post(request, QByteArray());
    attachCctvIgnoreSslErrors(state, reply, "CCTV_3DMAP_STOP");

    QObject::connect(reply, &QNetworkReply::finished, backend, [backend, state, reply, sequenceToken, attempt]() {
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString body = QString::fromUtf8(reply->readAll()).trimmed();
        if (sequenceToken != state->m_cctv3dMapSequenceToken) {
            reply->deleteLater();
            return;
        }

        const bool httpOk = (reply->error() == QNetworkReply::NoError) && (statusCode < 400 || statusCode == 0);
        QString apiStatus;
        QString apiMessage = body;
        const QJsonDocument parsed = QJsonDocument::fromJson(body.toUtf8());
        if (parsed.isObject()) {
            const QJsonObject obj = parsed.object();
            if (obj.contains("status") && obj.value("status").isString()) {
                apiStatus = obj.value("status").toString();
            }
            if (obj.contains("message") && obj.value("message").isString()) {
                apiMessage = obj.value("message").toString().trimmed();
            }
        }

        const bool statusBad = !apiStatus.isEmpty()
                               && apiStatus.compare("OK", Qt::CaseInsensitive) != 0
                               && apiStatus.compare("ACCEPTED", Qt::CaseInsensitive) != 0;
        const bool apiReportedError =
            apiMessage.startsWith("Error:", Qt::CaseInsensitive) || statusBad;
        const bool semanticOk = httpOk && !apiReportedError;

        if (!semanticOk) {
            const bool busy = (statusCode == 409)
                              || apiStatus.compare("BUSY", Qt::CaseInsensitive) == 0
                              || apiMessage.contains("already running", Qt::CaseInsensitive);
            const bool transientHttp = !httpOk
                                       && (statusCode == 0 || statusCode == 429
                                           || statusCode == 502 || statusCode == 503 || statusCode == 504);
            const bool transientNet = !httpOk
                                      && (reply->error() == QNetworkReply::TimeoutError
                                          || reply->error() == QNetworkReply::TemporaryNetworkFailureError
                                          || reply->error() == QNetworkReply::NetworkSessionFailedError
                                          || reply->error() == QNetworkReply::UnknownNetworkError);
            const bool transient = busy
                                   || transientHttp
                                   || transientNet
                                   || apiMessage.contains("Timed out", Qt::CaseInsensitive)
                                   || apiMessage.contains("timeout", Qt::CaseInsensitive);
            if (transient && attempt < kCctv3dMapStopRetryMaxAttempts) {
                emit backend->cameraControlMessage(
                    QString("3D Map stop retry (%1/%2)...")
                        .arg(attempt + 1)
                        .arg(kCctv3dMapStopRetryMaxAttempts),
                    false);
                reply->deleteLater();
                QTimer::singleShot(kCctv3dMapStopRetryDelayMs, backend, [backend, state, sequenceToken, attempt]() {
                    BackendCctv3dMapService::postCctvControlStopWithRetry(backend, state, sequenceToken, attempt + 1);
                });
                return;
            }

            const QString err = httpOk
                                    ? QString("3D Map stop command failed: %1").arg(apiMessage.left(180))
                                    : QString("3D Map stop failed (HTTP %1): %2")
                                          .arg(statusCode)
                                          .arg(reply->errorString());
            qWarning() << "[CCTV_3DMAP]" << err << "body=" << body.left(180);
            emit backend->cameraControlMessage(err, true);
            state->m_cctv3dMapStopInFlight = false;
            reply->deleteLater();
            return;
        }

        qInfo() << "[CCTV_3DMAP] stop requested. status=" << statusCode << "body=" << body.left(180);
        emit backend->cameraControlMessage("3D Map stop accepted", false);
        state->m_cctv3dMapStopInFlight = false;
        reply->deleteLater();
    });
}

void BackendCctv3dMapService::runCctv3dMapSequenceStep(Backend *backend, BackendPrivate *state, int sequenceToken, int step)
{
    if (sequenceToken != state->m_cctv3dMapSequenceToken || state->m_cctv3dMapCameraIndex < 0) {
        return;
    }

    if (step == 1) {
        BackendCctv3dMapService::pollCctv3dMapMoveStatus(backend, state, sequenceToken);
        return;
    }

    if (step == 2) {
        if (!backend->sunapiZoomStop(state->m_cctv3dMapCameraIndex)) {
            qWarning() << "[CCTV_3DMAP] zoom stop failed before autofocus";
        }

        emit backend->cameraControlMessage("3D Map: autofocus", false);
        if (!backend->sunapiSimpleAutoFocus(state->m_cctv3dMapCameraIndex)) {
            emit backend->cameraControlMessage("3D Map start aborted: autofocus failed", true);
            return;
        }

        state->m_cctv3dMapPendingStep = 3;
        if (state->m_cctv3dMapStepTimer) {
            state->m_cctv3dMapStepTimer->start(kCctv3dMapAutofocusSettleMs);
        }
        emit backend->cameraControlMessage("3D Map: wait 5s, then start API", false);
        return;
    }

    if (step == 3) {
        if (state->m_cctv3dMapPrepareOnly) {
            state->m_cctv3dMapPendingStep = 0;
            emit backend->cameraControlMessage("3D Map 준비 완료 (줌-/오토포커스)", false);
            return;
        }
        BackendCctv3dMapService::postCctvControlStart(backend, state, sequenceToken);
    }
}

void BackendCctv3dMapService::pollCctv3dMapMoveStatus(Backend *backend, BackendPrivate *state, int sequenceToken)
{
    if (sequenceToken != state->m_cctv3dMapSequenceToken || state->m_cctv3dMapCameraIndex < 0) {
        return;
    }

    state->m_cctv3dMapMoveStatusPollCount += 1;
    const int attempt = state->m_cctv3dMapMoveStatusPollCount;

    QNetworkRequest request(buildCctvApiUrl(backend, "/sunapi/stw-cgi/ptzcontrol.cgi", {
        {"msubmenu", "movestatus"},
        {"action", "view"},
        {"Channel", QString::number(state->m_cctv3dMapCameraIndex)},
    }));
    applyCctvSslIfNeeded(state, request);
    applyCctvAuthIfNeeded(backend, state, request);

    QNetworkReply *reply = state->m_manager->get(request);
    attachCctvIgnoreSslErrors(state, reply, "CCTV_3DMAP_MOVE_STATUS");
    QObject::connect(reply, &QNetworkReply::finished, backend, [backend, state, reply, sequenceToken, attempt]() {
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString body = QString::fromUtf8(reply->readAll()).trimmed();
        reply->deleteLater();

        if (sequenceToken != state->m_cctv3dMapSequenceToken) {
            return;
        }

        bool zoomKnown = false;
        bool zoomMoving = false;
        bool parseOk = false;

        if (reply->error() == QNetworkReply::NoError && (statusCode < 400 || statusCode == 0)) {
            parseOk = parseZoomMovingState(body, state->m_cctv3dMapCameraIndex, &zoomKnown, &zoomMoving);
        } else {
            qWarning() << "[CCTV_3DMAP] movestatus request failed. status=" << statusCode
                       << "err=" << reply->errorString()
                       << "body=" << body.left(180);
        }

        if (parseOk && zoomKnown && !zoomMoving) {
            emit backend->cameraControlMessage("3D Map: zoom settled (Idle), starting autofocus", false);
            state->m_cctv3dMapPendingStep = 2;
            if (state->m_cctv3dMapStepTimer) {
                state->m_cctv3dMapStepTimer->start(30);
            }
            return;
        }

        if (attempt >= kCctv3dMapMoveStatusMaxAttempts) {
            emit backend->cameraControlMessage("3D Map: zoom status timeout, continue with autofocus", true);
            state->m_cctv3dMapPendingStep = 2;
            if (state->m_cctv3dMapStepTimer) {
                state->m_cctv3dMapStepTimer->start(30);
            }
            return;
        }

        if ((attempt % 4) == 0) {
            emit backend->cameraControlMessage(
                QString("3D Map: waiting zoom settle... (%1/%2)")
                    .arg(attempt)
                    .arg(kCctv3dMapMoveStatusMaxAttempts),
                false);
        }

        state->m_cctv3dMapPendingStep = 1;
        if (state->m_cctv3dMapStepTimer) {
            state->m_cctv3dMapStepTimer->start(kCctv3dMapMoveStatusPollIntervalMs);
        }
    });
}

bool BackendCctv3dMapService::postCctvControlStart(Backend *backend, BackendPrivate *state, int sequenceToken)
{
    if (sequenceToken != state->m_cctv3dMapSequenceToken || state->m_cctv3dMapCameraIndex < 0) {
        return false;
    }

    QNetworkRequest request = makeCctvApiJsonRequest(backend, state, "/cctv/control/start");
    const QJsonObject payload {
        {"channel", state->m_cctv3dMapCameraIndex},
        {"mode", "headless"},
    };

    QNetworkReply *reply = state->m_manager->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    attachCctvIgnoreSslErrors(state, reply, "CCTV_3DMAP_START");
    QObject::connect(reply, &QNetworkReply::finished, backend, [backend, state, reply, sequenceToken]() {
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString body = QString::fromUtf8(reply->readAll()).trimmed();

        if (sequenceToken != state->m_cctv3dMapSequenceToken) {
            reply->deleteLater();
            return;
        }

        const bool ok = (reply->error() == QNetworkReply::NoError) && (statusCode < 400 || statusCode == 0);
        if (!ok) {
            const bool isBusy = (statusCode == 409)
                                && body.contains("\"status\":\"BUSY\"", Qt::CaseInsensitive);
            if (isBusy && state->m_cctv3dMapStartRetryCount < kCctv3dMapStartRetryMaxAttempts) {
                state->m_cctv3dMapStartRetryCount += 1;
                const int retry = state->m_cctv3dMapStartRetryCount;
                emit backend->cameraControlMessage(
                    QString("3D Map start busy (retry %1/%2)...")
                        .arg(retry)
                        .arg(kCctv3dMapStartRetryMaxAttempts),
                    false);
                reply->deleteLater();
                QTimer::singleShot(kCctv3dMapStartRetryDelayMs, backend, [backend, state, sequenceToken]() {
                    BackendCctv3dMapService::postCctvControlStart(backend, state, sequenceToken);
                });
                return;
            }

            const QString err = QString("3D Map start API failed (HTTP %1): %2")
                                    .arg(statusCode)
                                    .arg(reply->errorString());
            qWarning() << "[CCTV_3DMAP]" << err << "body=" << body.left(180);
            emit backend->cameraControlMessage(err, true);
            reply->deleteLater();
            return;
        }

        state->m_cctv3dMapStartRetryCount = 0;
        state->m_cctv3dMapStreamRetryCount = 0;
        qInfo() << "[CCTV_3DMAP] start accepted. status=" << statusCode << "body=" << body.left(180);
        emit backend->cameraControlMessage("3D Map API start accepted", false);
        reply->deleteLater();
        BackendCctv3dMapService::postCctvControlStream(backend, state, sequenceToken);
    });

    return true;
}

bool BackendCctv3dMapService::postCctvControlStream(Backend *backend, BackendPrivate *state, int sequenceToken)
{
    if (sequenceToken != state->m_cctv3dMapSequenceToken) {
        qWarning() << "[CCTV_3DMAP] skip stream request: token mismatch."
                   << "requested=" << sequenceToken
                   << "current=" << state->m_cctv3dMapSequenceToken;
        return false;
    }

    QNetworkRequest request = makeCctvApiJsonRequest(backend, state, "/cctv/control/stream");
    const QJsonObject payload {
        {"stream", "rgbd_stream"},
    };
    const QByteArray bodyData = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    qInfo() << "[CCTV_3DMAP] stream request send."
            << "token=" << sequenceToken
            << "url=" << request.url().toString()
            << "body=" << QString::fromUtf8(bodyData);

    QNetworkReply *reply = state->m_manager->post(request, bodyData);
    attachCctvIgnoreSslErrors(state, reply, "CCTV_3DMAP_STREAM");
    QObject::connect(reply, &QNetworkReply::finished, backend, [backend, state, reply, sequenceToken]() {
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString body = QString::fromUtf8(reply->readAll()).trimmed();
        qInfo() << "[CCTV_3DMAP] stream request finished."
                << "token=" << sequenceToken
                << "status=" << statusCode
                << "netErr=" << static_cast<int>(reply->error())
                << "errStr=" << reply->errorString();

        if (sequenceToken != state->m_cctv3dMapSequenceToken) {
            qWarning() << "[CCTV_3DMAP] stream reply ignored: token mismatch."
                       << "replyToken=" << sequenceToken
                       << "current=" << state->m_cctv3dMapSequenceToken;
            reply->deleteLater();
            return;
        }

        const bool httpOk = (reply->error() == QNetworkReply::NoError) && (statusCode < 400 || statusCode == 0);
        QString apiStatus;
        QString apiMessage = body;
        const QJsonDocument parsed = QJsonDocument::fromJson(body.toUtf8());
        if (parsed.isObject()) {
            const QJsonObject obj = parsed.object();
            if (obj.contains("status") && obj.value("status").isString()) {
                apiStatus = obj.value("status").toString();
            }
            if (obj.contains("message") && obj.value("message").isString()) {
                apiMessage = obj.value("message").toString().trimmed();
            }
        }

        const bool apiReportedError =
            apiMessage.startsWith("Error:", Qt::CaseInsensitive) ||
            (!apiStatus.isEmpty() && apiStatus.compare("OK", Qt::CaseInsensitive) != 0);
        const bool semanticOk = httpOk && !apiReportedError;

        if (!semanticOk) {
            const bool transient =
                apiMessage.contains("Failed to read stream ACK", Qt::CaseInsensitive) ||
                apiMessage.contains("Timed out", Qt::CaseInsensitive) ||
                apiMessage.contains("timeout", Qt::CaseInsensitive);
            if (transient && state->m_cctv3dMapStreamRetryCount < kCctv3dMapStreamRetryMaxAttempts) {
                state->m_cctv3dMapStreamRetryCount += 1;
                const int retry = state->m_cctv3dMapStreamRetryCount;
                emit backend->cameraControlMessage(
                    QString("3D Map stream not ready (retry %1/%2)...")
                        .arg(retry)
                        .arg(kCctv3dMapStreamRetryMaxAttempts),
                    false);
                reply->deleteLater();
                QTimer::singleShot(kCctv3dMapStreamRetryDelayMs, backend, [backend, state, sequenceToken]() {
                    BackendCctv3dMapService::postCctvControlStream(backend, state, sequenceToken);
                });
                return;
            }

            const QString err = httpOk
                                    ? QString("3D Map stream command failed: %1").arg(apiMessage.left(180))
                                    : QString("3D Map stream request failed (HTTP %1): %2")
                                          .arg(statusCode)
                                          .arg(reply->errorString());
            qWarning() << "[CCTV_3DMAP]" << err << "body=" << body.left(180);
            emit backend->cameraControlMessage(err, true);
            reply->deleteLater();
            return;
        }

        state->m_cctv3dMapStreamRetryCount = 0;
        qInfo() << "[CCTV_3DMAP] stream request OK. status=" << statusCode << "body=" << body.left(180);
        emit backend->cameraControlMessage("3D Map stream mode requested (rgbd_stream)", false);
        reply->deleteLater();
        BackendCctv3dMapService::connectCctvStreamWs(backend, state, sequenceToken);
    });

    return true;
}

bool BackendCctv3dMapService::postCctvControlView(Backend *backend, BackendPrivate *state, double rx, double ry)
{
    if (!backend || !state) {
        return false;
    }
    // 드래그 회전은 서버에 보내지 않고 로컬 RGBD 재렌더링만 수행한다.
    state->m_cctv3dMapViewRx = rx;
    state->m_cctv3dMapViewRy = ry;
    renderCctv3dMapFromCachedRgbd(backend, state, true);
    return true;
}

void BackendCctv3dMapService::connectCctvStreamWs(Backend *backend, BackendPrivate *state, int sequenceToken)
{
    if (sequenceToken != state->m_cctv3dMapSequenceToken) {
        return;
    }

    const QUrl apiBase(backend->serverUrl());
    const QString host = apiBase.host().trimmed();
    if (!apiBase.isValid() || host.isEmpty()) {
        emit backend->cameraControlMessage("3D Map WS connect failed: invalid API_URL", true);
        return;
    }

    const QString apiScheme = apiBase.scheme().trimmed().toLower();
    const QString wsScheme = (apiScheme == "https") ? QStringLiteral("wss") : QStringLiteral("ws");
    const int defaultPort = (apiScheme == "https") ? 443 : 80;
    const int httpPort = apiBase.port(defaultPort);

    QUrl wsUrl;
    wsUrl.setScheme(wsScheme);
    wsUrl.setHost(host);
    if (httpPort > 0) {
        wsUrl.setPort(httpPort);
    }
    wsUrl.setPath("/cctv/stream");

    if (!state->m_cctvStreamWs) {
        state->m_cctvStreamWs = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, backend);

        QObject::connect(state->m_cctvStreamWs, &QWebSocket::connected, backend, [backend, state]() {
            if (state->m_cctv3dMapWsActiveToken != state->m_cctv3dMapSequenceToken) {
                BackendCctv3dMapService::disconnectCctvStreamWs(backend, state, true);
                return;
            }
            state->m_cctv3dMapFrameCount = 0;
            state->m_cctv3dMapTotalBytes = 0;
            state->m_cctv3dMapLastRenderMs = 0;
            state->m_cctv3dMapWsStreamBuffer.clear();
            emit backend->cameraControlMessage("3D Map WS connected", false);
            qInfo() << "[CCTV_3DMAP][WS] connected";
        });

        QObject::connect(state->m_cctvStreamWs, &QWebSocket::disconnected, backend, [backend, state]() {
            if (state->m_cctv3dMapStoppingExpected) {
                state->m_cctv3dMapStoppingExpected = false;
                return;
            }
            emit backend->cameraControlMessage("3D Map WS disconnected", true);
            qWarning() << "[CCTV_3DMAP][WS] disconnected unexpectedly";
        });

        QObject::connect(state->m_cctvStreamWs, &QWebSocket::binaryMessageReceived, backend, [backend, state](const QByteArray &payload) {
            if (state->m_cctv3dMapWsActiveToken != state->m_cctv3dMapSequenceToken) {
                return;
            }
            state->m_cctv3dMapTotalBytes += payload.size();
            state->m_cctv3dMapWsStreamBuffer.append(payload);
            if (state->m_cctv3dMapWsStreamBuffer.size() > kCctv3dMapMaxWsBufferBytes) {
                qWarning() << "[CCTV_3DMAP][WS] stream buffer overflow, clearing."
                           << "bufferBytes=" << state->m_cctv3dMapWsStreamBuffer.size();
                state->m_cctv3dMapWsStreamBuffer.clear();
                return;
            }

            int parsedFramesNow = 0;
            while (true) {
                ParsedRgbdFrame rgbd;
                if (tryConsumeRgbdFrame(&state->m_cctv3dMapWsStreamBuffer, &rgbd)) {
                    state->m_cctv3dMapFrameCount += 1;
                    parsedFramesNow += 1;
                    state->m_cctv3dMapRgbdWidth = rgbd.width;
                    state->m_cctv3dMapRgbdHeight = rgbd.height;
                    state->m_cctv3dMapRgbdDepthBytes = rgbd.depthBytes;
                    state->m_cctv3dMapRgbdBgrBytes = rgbd.bgrBytes;
                    renderCctv3dMapFromCachedRgbd(backend, state, false);
                    continue;
                }

                ParsedPcFrame pc;
                if (tryConsumePcFrame(&state->m_cctv3dMapWsStreamBuffer, &pc)) {
                    state->m_cctv3dMapFrameCount += 1;
                    parsedFramesNow += 1;
                    QImage decoded;
                    int offset = 0;
                    if (loadImageFromPayload(pc.imageBytes, &decoded, &offset)) {
                        state->m_cctv3dMapLastRenderMs = QDateTime::currentMSecsSinceEpoch();
                        state->m_cctv3dMapFrameDataUrl = QString::fromLatin1(buildPngDataUrl(decoded));
                        emit backend->cctv3dMapFrameDataUrlChanged();
                    } else if (state->m_cctv3dMapFrameCount <= 5) {
                        const QByteArray head = pc.imageBytes.left(16).toHex();
                        qWarning() << "[CCTV_3DMAP][WS] undecodable pc_stream frame."
                                   << "payloadBytes=" << pc.imageBytes.size()
                                   << "head=" << head;
                    }
                    continue;
                }

                const int eol = state->m_cctv3dMapWsStreamBuffer.indexOf('\n');
                if (eol > 0 && eol <= 128) {
                    const QByteArray line = state->m_cctv3dMapWsStreamBuffer.left(eol + 1).trimmed();
                    if (line.startsWith("OK ") || line.startsWith("ERR ")) {
                        qInfo() << "[CCTV_3DMAP][WS][LINE]" << line;
                        state->m_cctv3dMapWsStreamBuffer.remove(0, eol + 1);
                        continue;
                    }
                }
                break;
            }

            if (state->m_cctv3dMapFrameCount == 1 && parsedFramesNow > 0) {
                emit backend->cameraControlMessage(
                    QString("3D Map WS first frame received (%1 bytes)").arg(payload.size()),
                    false);
            } else if (parsedFramesNow > 0
                       && state->m_cctv3dMapFrameCount > 0
                       && (state->m_cctv3dMapFrameCount % 30) == 0) {
                qInfo() << "[CCTV_3DMAP][WS] frames=" << state->m_cctv3dMapFrameCount
                        << "bytes=" << state->m_cctv3dMapTotalBytes
                        << "buffer=" << state->m_cctv3dMapWsStreamBuffer.size();
            } else if (parsedFramesNow == 0 && state->m_cctv3dMapFrameCount < 5) {
                qInfo() << "[CCTV_3DMAP][WS] waiting full frame..."
                        << "chunkBytes=" << payload.size()
                        << "buffer=" << state->m_cctv3dMapWsStreamBuffer.size();
            }
        });

        QObject::connect(state->m_cctvStreamWs, &QWebSocket::textMessageReceived, backend, [state](const QString &message) {
            if (state->m_cctv3dMapWsActiveToken != state->m_cctv3dMapSequenceToken) {
                return;
            }
            qInfo() << "[CCTV_3DMAP][WS][TEXT]" << message.left(180);
        });

        QObject::connect(state->m_cctvStreamWs, &QWebSocket::errorOccurred, backend, [backend, state](QAbstractSocket::SocketError) {
            const QString err = state->m_cctvStreamWs
                                    ? state->m_cctvStreamWs->errorString()
                                    : QString("unknown websocket error");
            emit backend->cameraControlMessage("3D Map WS error: " + err, true);
            qWarning() << "[CCTV_3DMAP][WS] error:" << err;
        });

        QObject::connect(state->m_cctvStreamWs, &QWebSocket::sslErrors, backend, [state](const QList<QSslError> &errors) {
            for (const auto &err : errors) {
                qWarning() << "[CCTV_3DMAP][WS][SSL]" << err.errorString();
            }
            if (state->m_sslIgnoreErrors && state->m_cctvStreamWs) {
                state->m_cctvStreamWs->ignoreSslErrors();
            }
        });
    } else if (state->m_cctvStreamWs->state() == QAbstractSocket::ConnectedState
               || state->m_cctvStreamWs->state() == QAbstractSocket::ConnectingState) {
        state->m_cctv3dMapStoppingExpected = true;
        state->m_cctvStreamWs->abort();
    }

    state->m_cctv3dMapWsActiveToken = sequenceToken;
    state->m_cctv3dMapStoppingExpected = false;
    state->m_cctv3dMapWsStreamBuffer.clear();
    if (wsScheme == "wss" && state->m_sslConfigReady) {
        state->m_cctvStreamWs->setSslConfiguration(state->m_sslConfig);
    }

    emit backend->cameraControlMessage("3D Map WS connecting...", false);
    state->m_cctvStreamWs->open(wsUrl);
}

void BackendCctv3dMapService::disconnectCctvStreamWs(Backend *backend, BackendPrivate *state, bool expectedStop)
{
    Q_UNUSED(backend);
    state->m_cctv3dMapStoppingExpected = expectedStop;
    state->m_cctv3dMapWsStreamBuffer.clear();
    if (!state->m_cctvStreamWs) {
        return;
    }

    if (state->m_cctvStreamWs->state() == QAbstractSocket::ConnectedState
        || state->m_cctvStreamWs->state() == QAbstractSocket::ConnectingState) {
        state->m_cctvStreamWs->close();
    }
}

