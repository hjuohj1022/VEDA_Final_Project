#ifndef QRCODEIMAGEPROVIDER_H
#define QRCODEIMAGEPROVIDER_H

#include <QImage>
#include <QQuickImageProvider>

class QrCodeImageProvider final : public QQuickImageProvider
{
public:
    QrCodeImageProvider();

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};

#endif // QRCODEIMAGEPROVIDER_H
