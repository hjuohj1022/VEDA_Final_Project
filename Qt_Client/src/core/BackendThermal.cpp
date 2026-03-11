#include "Backend.h"

#include <QBuffer>
#include <QByteArray>
#include <QDateTime>
#include <QDebug>
#include <QImage>
#include <QList>
#include <QMap>
#include <QtEndian>

#include <algorithm>
#include <vector>

namespace {
constexpr int kThermalWidth = 160;
constexpr int kThermalHeight = 120;
constexpr int kThermalFrameBytes = kThermalWidth * kThermalHeight * 2;
constexpr int kThermalHeaderBytes = 10;
constexpr int kThermalHeaderWithRangeBytes = 8;
constexpr int kThermalHeaderLegacyBytes = 4;
constexpr int kThermalChunkLimit = 100;
constexpr qint64 kThermalFrameTimeoutMs = 1500;

struct ThermalChunkHeader {
    int frameId = -1;
    quint16 idx = 0;
    quint16 total = 0;
    quint16 minVal = 0;
    quint16 maxVal = 0;
    int headerBytes = 0;
    bool hasFrameId = false;
};

static bool isReasonableChunkHeader(const ThermalChunkHeader &header) {
    return header.total > 0 && header.total <= kThermalChunkLimit && header.idx < header.total;
}

static bool parseThermalChunkHeader(const QByteArray &message, ThermalChunkHeader *header) {
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

static QRgb jetColor(unsigned char value) {
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

static QRgb ironColor(unsigned char value) {
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

    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return qRgb(r, g, b);
}

static int percentileValue(std::vector<int> values, int p) {
    if (values.empty()) return 0;
    if (p <= 0) return *std::min_element(values.begin(), values.end());
    if (p >= 100) return *std::max_element(values.begin(), values.end());

    const size_t idx = static_cast<size_t>((static_cast<long long>(values.size() - 1) * p) / 100);
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(idx), values.end());
    return values[idx];
}
}

void Backend::startThermalStream() {
    if (m_thermalStreaming) {
        return;
    }
    m_thermalStreaming = true;
    m_thermalCurrentFrameId = -1;
    m_thermalTotalChunksExpected = 0;
    m_thermalFrameStartedMs = 0;
    m_thermalHeaderMin = 0;
    m_thermalHeaderMax = 0;
    m_thermalFrameChunks.clear();
    m_thermalLastFrameId = -1;
    m_thermalLastDisplayMs = 0;
    m_thermalDisplayFps = 0.0;
    m_thermalInfoText = "Thermal stream active";
    qInfo() << "[THERMAL] stream started";
    emit thermalStreamingChanged();
    emit thermalInfoTextChanged();
}

void Backend::stopThermalStream() {
    if (!m_thermalStreaming) {
        return;
    }
    m_thermalStreaming = false;
    m_thermalCurrentFrameId = -1;
    m_thermalTotalChunksExpected = 0;
    m_thermalFrameStartedMs = 0;
    m_thermalHeaderMin = 0;
    m_thermalHeaderMax = 0;
    m_thermalFrameChunks.clear();
    m_thermalInfoText = "Thermal stream stopped";
    qInfo() << "[THERMAL] stream stopped";
    emit thermalStreamingChanged();
    emit thermalInfoTextChanged();
}
void Backend::setThermalPalette(const QString &palette) {
    QString normalized = palette.trimmed();
    if (normalized.compare("gray", Qt::CaseInsensitive) == 0) {
        normalized = "Gray";
    } else if (normalized.compare("iron", Qt::CaseInsensitive) == 0) {
        normalized = "Iron";
    } else {
        normalized = "Jet";
    }

    if (m_thermalPalette == normalized) {
        return;
    }
    m_thermalPalette = normalized;
    emit thermalPaletteChanged();
}

void Backend::setThermalAutoRange(bool enabled) {
    if (m_thermalAutoRange == enabled) {
        return;
    }
    m_thermalAutoRange = enabled;
    emit thermalAutoRangeChanged();
}

void Backend::setThermalAutoRangeWindowPercent(int percent) {
    int normalized = percent;
    if (normalized < 50) normalized = 50;
    if (normalized > 100) normalized = 100;
    if (m_thermalAutoRangeWindowPercent == normalized) {
        return;
    }
    m_thermalAutoRangeWindowPercent = normalized;
    emit thermalAutoRangeWindowPercentChanged();
}

void Backend::setThermalManualRange(int minValue, int maxValue) {
    int nextMin = std::max(0, minValue);
    int nextMax = std::max(1, maxValue);
    if (nextMin >= nextMax) {
        nextMax = nextMin + 1;
    }
    if (m_thermalManualMin == nextMin && m_thermalManualMax == nextMax) {
        return;
    }
    m_thermalManualMin = nextMin;
    m_thermalManualMax = nextMax;
    emit thermalManualRangeChanged();
}

void Backend::handleThermalChunkMessage(const QByteArray &message) {
    const bool debugThermal = (m_env.value("THERMAL_DEBUG", "0").trimmed() == "1");
    if (!m_thermalStreaming) {
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
    if (m_thermalCurrentFrameId >= 0 && m_thermalFrameStartedMs > 0
        && (nowMs - m_thermalFrameStartedMs) > kThermalFrameTimeoutMs) {
        qWarning() << "[THERMAL] frame timeout id=" << m_thermalCurrentFrameId;
        m_thermalCurrentFrameId = -1;
        m_thermalTotalChunksExpected = 0;
        m_thermalFrameStartedMs = 0;
        m_thermalHeaderMin = 0;
        m_thermalHeaderMax = 0;
        m_thermalFrameChunks.clear();
    }

    bool shouldStartNewFrame = false;
    if (header.hasFrameId) {
        shouldStartNewFrame = (m_thermalCurrentFrameId < 0 || m_thermalCurrentFrameId != header.frameId);
    } else {
        shouldStartNewFrame = (m_thermalTotalChunksExpected != static_cast<int>(header.total))
                           || (header.idx == 0 && !m_thermalFrameChunks.isEmpty());
    }

    if (shouldStartNewFrame) {
        if (m_thermalCurrentFrameId >= 0
            && !m_thermalFrameChunks.isEmpty()
            && m_thermalFrameChunks.size() != m_thermalTotalChunksExpected) {
            qWarning() << "[THERMAL] drop incomplete frame id=" << m_thermalCurrentFrameId
                       << "chunks=" << m_thermalFrameChunks.size()
                       << "/" << m_thermalTotalChunksExpected;
        }

        m_thermalCurrentFrameId = -1;
        m_thermalTotalChunksExpected = 0;
        m_thermalFrameStartedMs = 0;
        m_thermalHeaderMin = 0;
        m_thermalHeaderMax = 0;
        m_thermalFrameChunks.clear();
        m_thermalCurrentFrameId = header.hasFrameId ? header.frameId : 0;
        m_thermalTotalChunksExpected = static_cast<int>(header.total);
        m_thermalFrameStartedMs = nowMs;
    }

    if (m_thermalCurrentFrameId < 0) {
        m_thermalCurrentFrameId = header.hasFrameId ? header.frameId : 0;
    }
    if (m_thermalTotalChunksExpected <= 0) {
        m_thermalTotalChunksExpected = static_cast<int>(header.total);
        m_thermalFrameStartedMs = nowMs;
    }

    const QByteArray payload = message.mid(header.headerBytes);
    if (debugThermal && header.idx == 0) {
        qInfo() << "[THERMAL] frame begin id=" << m_thermalCurrentFrameId
                << "total=" << header.total
                << "payload=" << payload.size()
                << "headerMinMax=" << header.minVal << header.maxVal;
    }

    m_thermalHeaderMin = header.minVal;
    m_thermalHeaderMax = header.maxVal;
    m_thermalFrameChunks[static_cast<int>(header.idx)] = payload;

    if (m_thermalFrameChunks.size() == m_thermalTotalChunksExpected) {
        const int frameId = m_thermalCurrentFrameId;
        if (debugThermal) {
            qInfo() << "[THERMAL] frame chunks complete id=" << frameId
                    << "chunks=" << m_thermalTotalChunksExpected;
        }
        processThermalFrame(m_thermalFrameChunks,
                            m_thermalTotalChunksExpected,
                            m_thermalHeaderMin,
                            m_thermalHeaderMax,
                            frameId);
        m_thermalCurrentFrameId = -1;
        m_thermalTotalChunksExpected = 0;
        m_thermalFrameStartedMs = 0;
        m_thermalHeaderMin = 0;
        m_thermalHeaderMax = 0;
        m_thermalFrameChunks.clear();
    }
}

void Backend::processThermalFrame(const QMap<int, QByteArray> &chunks, int totalChunks, quint16 minVal, quint16 maxVal, int frameId) {
    const bool debugThermal = (m_env.value("THERMAL_DEBUG", "0").trimmed() == "1");
    QByteArray full;
    full.reserve(kThermalFrameBytes);
    for (int i = 0; i < totalChunks; ++i) {
        if (!chunks.contains(i)) {
            if (debugThermal) {
                qWarning() << "[THERMAL] missing chunk:" << i << "/" << totalChunks;
            }
            return;
        }
        full.append(chunks.value(i));
    }
    if (full.size() < kThermalFrameBytes) {
        if (debugThermal) {
            qWarning() << "[THERMAL] frame too small:" << full.size() << "expected:" << kThermalFrameBytes;
        }
        return;
    }

    std::vector<int> raw;
    raw.reserve(kThermalWidth * kThermalHeight);
    std::vector<int> valid;
    valid.reserve(kThermalWidth * kThermalHeight);
    const uchar *buf = reinterpret_cast<const uchar *>(full.constData());
    for (int i = 0; i < kThermalWidth * kThermalHeight; ++i) {
        const int v = static_cast<int>(qFromBigEndian<quint16>(buf + i * 2));
        raw.push_back(v);
        if (v > 1000 && v < 30000) {
            valid.push_back(v);
        }
    }

    int frameMin = static_cast<int>(minVal);
    int frameMax = static_cast<int>(maxVal);
    if (m_thermalAutoRange) {
        if (!valid.empty()) {
            const int window = std::max(50, std::min(100, m_thermalAutoRangeWindowPercent));
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
        frameMin = m_thermalManualMin;
        frameMax = m_thermalManualMax;
    }
    if (frameMax <= frameMin) {
        frameMax = frameMin + 1;
    }

    QImage image(kThermalWidth, kThermalHeight, QImage::Format_RGB32);
    for (int y = 0; y < kThermalHeight; ++y) {
        for (int x = 0; x < kThermalWidth; ++x) {
            const int src = raw[static_cast<size_t>(y * kThermalWidth + x)];
            int normalized = ((src - frameMin) * 255) / (frameMax - frameMin);
            if (normalized < 0) normalized = 0;
            if (normalized > 255) normalized = 255;
            if (m_thermalPalette == "Gray") {
                image.setPixel(x, y, qRgb(normalized, normalized, normalized));
            } else if (m_thermalPalette == "Iron") {
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
    if (m_thermalLastDisplayMs > 0) {
        const double deltaSec = static_cast<double>(nowMs - m_thermalLastDisplayMs) / 1000.0;
        if (deltaSec > 0.0) {
            const double instant = 1.0 / deltaSec;
            m_thermalDisplayFps = (m_thermalDisplayFps == 0.0)
                                ? instant
                                : ((m_thermalDisplayFps * 0.8) + (instant * 0.2));
        }
    }
    m_thermalLastDisplayMs = nowMs;
    m_thermalLastFrameId = frameId;

    const QString rangeMode = m_thermalAutoRange
                                  ? QString("Auto(%1%)").arg(m_thermalAutoRangeWindowPercent)
                                  : "Manual";
    const QString info = QString("Frame: %1 | FPS: %2 | Palette: %3 | %4 Range: %5 ~ %6")
                             .arg(frameId)
                             .arg(m_thermalDisplayFps, 0, 'f', 2)
                             .arg(m_thermalPalette)
                             .arg(rangeMode)
                             .arg(frameMin)
                             .arg(frameMax);
    if (m_thermalFrameDataUrl != nextDataUrl) {
        m_thermalFrameDataUrl = nextDataUrl;
        emit thermalFrameDataUrlChanged();
    }
    if (m_thermalInfoText != info) {
        m_thermalInfoText = info;
        emit thermalInfoTextChanged();
    }
    if (debugThermal) {
        qInfo() << "[THERMAL] frame rendered id=" << frameId
                << "bytes=" << full.size()
                << "range=" << frameMin << frameMax
                << "fps=" << m_thermalDisplayFps
                << "palette=" << m_thermalPalette;
    }
}
