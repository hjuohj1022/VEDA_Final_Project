#include "ui/QrCodeImageProvider.h"

#include <QByteArray>
#include <QUrl>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "qrcodegen.hpp"

namespace {

constexpr int kQuietZoneModules = 4;
constexpr int kMinimumImageSize = 220;

// QR 코드 렌더링 함수
void renderQrCode(const qrcodegen::QrCode &qr, int imageSize, QImage *image)
{
    if (!image) {
        return;
    }

    const int moduleCount = qr.getSize();
    const int totalModules = moduleCount + kQuietZoneModules * 2;
    const int scale = std::max(1, imageSize / totalModules);
    const int qrPixelSize = totalModules * scale;
    const int offset = (imageSize - qrPixelSize) / 2;

    image->fill(Qt::white);

    for (int y = 0; y < moduleCount; ++y) {
        for (int x = 0; x < moduleCount; ++x) {
            if (!qr.getModule(x, y)) {
                continue;
            }

            const int pixelLeft = offset + (x + kQuietZoneModules) * scale;
            const int pixelTop = offset + (y + kQuietZoneModules) * scale;
            for (int dy = 0; dy < scale; ++dy) {
                QRgb *scanLine = reinterpret_cast<QRgb *>(image->scanLine(pixelTop + dy));
                for (int dx = 0; dx < scale; ++dx) {
                    scanLine[pixelLeft + dx] = qRgb(0, 0, 0);
                }
            }
        }
    }
}

} // namespace

QrCodeImageProvider::QrCodeImageProvider()
    // Q 빠른 이미지 제공자 초기화 함수
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

// 이미지 요청 함수
QImage QrCodeImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const QString text = QUrl::fromPercentEncoding(id.toUtf8());
    const int requestedWidth = requestedSize.width() > 0 ? requestedSize.width() : 0;
    const int requestedHeight = requestedSize.height() > 0 ? requestedSize.height() : 0;
    const int imageSize = std::max(kMinimumImageSize, std::max(requestedWidth, requestedHeight));

    QImage image(imageSize, imageSize, QImage::Format_RGB32);
    image.fill(Qt::white);

    if (!text.isEmpty()) {
        try {
            const QByteArray payload = text.toUtf8();
            const std::vector<std::uint8_t> data(payload.begin(), payload.end());
            const qrcodegen::QrCode qr = qrcodegen::QrCode::encodeBinary(
                data,
                qrcodegen::QrCode::Ecc::LOW
            );
            renderQrCode(qr, imageSize, &image);
        } catch (...) {
            image.fill(Qt::white);
        }
    }

    if (size) {
        *size = image.size();
    }
    return image;
}
