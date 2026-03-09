#include "Backend.h"

#include <QBuffer>
#include <QByteArray>
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
constexpr int kThermalHeaderBytes = 8;
constexpr int kThermalHeaderLegacyBytes = 4;
constexpr int kThermalChunkLimit = 256;

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
    m_thermalFrameChunks.clear();
    m_thermalTotalChunksExpected = 0;
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
    m_thermalFrameChunks.clear();
    m_thermalTotalChunksExpected = 0;
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
        return;
    }
    if (message.size() < kThermalHeaderLegacyBytes) {
        if (debugThermal) {
            qWarning() << "[THERMAL] dropped short payload:" << message.size();
        }
        return;
    }

    const uchar *p = reinterpret_cast<const uchar *>(message.constData());
    const quint16 idx = qFromBigEndian<quint16>(p);
    const quint16 total = qFromBigEndian<quint16>(p + 2);
    quint16 minVal = 0;
    quint16 maxVal = 0;
    int headerBytes = kThermalHeaderLegacyBytes;
    if (message.size() >= kThermalHeaderBytes) {
        minVal = qFromBigEndian<quint16>(p + 4);
        maxVal = qFromBigEndian<quint16>(p + 6);
        headerBytes = kThermalHeaderBytes;
    }
    const QByteArray payload = message.mid(headerBytes);

    if (total == 0 || total > kThermalChunkLimit) {
        if (debugThermal) {
            qWarning() << "[THERMAL] invalid total chunks:" << total;
        }
        return;
    }
    if (idx >= total) {
        if (debugThermal) {
            qWarning() << "[THERMAL] invalid chunk index:" << idx << "total:" << total;
        }
        return;
    }

    if (m_thermalTotalChunksExpected != static_cast<int>(total)) {
        m_thermalFrameChunks.clear();
        m_thermalTotalChunksExpected = total;
    }

    if (idx == 0 && !m_thermalFrameChunks.isEmpty()) {
        m_thermalFrameChunks.clear();
    }
    if (debugThermal && idx == 0) {
        qInfo() << "[THERMAL] frame begin total=" << total
                << "payload=" << payload.size()
                << "headerMinMax=" << minVal << maxVal;
    }

    m_thermalHeaderMin = minVal;
    m_thermalHeaderMax = maxVal;
    m_thermalFrameChunks[static_cast<int>(idx)] = payload;

    if (m_thermalFrameChunks.size() == m_thermalTotalChunksExpected) {
        if (debugThermal) {
            qInfo() << "[THERMAL] frame chunks complete:" << m_thermalTotalChunksExpected;
        }
        processThermalFrame(m_thermalFrameChunks, m_thermalTotalChunksExpected, m_thermalHeaderMin, m_thermalHeaderMax);
        m_thermalFrameChunks.clear();
    }
}

void Backend::processThermalFrame(const QMap<int, QByteArray> &chunks, int totalChunks, quint16 minVal, quint16 maxVal) {
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

    const QString rangeMode = m_thermalAutoRange
                                  ? QString("Auto(%1%)").arg(m_thermalAutoRangeWindowPercent)
                                  : "Manual";
    const QString info = QString("Palette: %1 | %2 Range: %3 ~ %4")
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
        qInfo() << "[THERMAL] frame rendered bytes=" << full.size()
                << "range=" << frameMin << frameMax
                << "palette=" << m_thermalPalette;
    }
}
