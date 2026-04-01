#include "ui/CaptureHelper.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QDebug>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QSharedPointer>
#include <QTimer>
#include <QVariant>
#include <QtMath>

namespace {

constexpr qreal kMinimumCaptureScale = 1.0;

int dotsPerMeterFromDpi(int dpi)
{
    return qRound(static_cast<qreal>(dpi) / 0.0254);
}

QString normalizedPath(const QString &path)
{
    return QDir::toNativeSeparators(path);
}

QQuickItem *quickItemFromVariant(const QVariant &value)
{
    if (!value.isValid())
        return nullptr;

    QObject *object = value.value<QObject *>();
    if (object)
        return qobject_cast<QQuickItem *>(object);

    return value.value<QQuickItem *>();
}

QQuickItem *visualRootFromItem(QQuickItem *item)
{
    if (!item)
        return nullptr;

    if (QQuickItem *parentItem = item->parentItem())
        return parentItem;

    return item;
}

QQuickItem *resolveCaptureItem(QObject *target)
{
    if (!target)
        return nullptr;

    if (auto *item = qobject_cast<QQuickItem *>(target))
        return item;

    if (auto *window = qobject_cast<QQuickWindow *>(target))
        return window->contentItem();

    const QQuickItem *const targetItem = quickItemFromVariant(target->property("popupItem"));
    if (targetItem)
        return const_cast<QQuickItem *>(targetItem);

    const QQuickItem *const backgroundItem = quickItemFromVariant(target->property("background"));
    if (backgroundItem)
        return visualRootFromItem(const_cast<QQuickItem *>(backgroundItem));

    const QQuickItem *const contentItem = quickItemFromVariant(target->property("contentItem"));
    if (contentItem)
        return visualRootFromItem(const_cast<QQuickItem *>(contentItem));

    return nullptr;
}

} // namespace

CaptureHelper::CaptureHelper(QObject *parent)
    : QObject(parent)
{
}

QString CaptureHelper::defaultCaptureDirectory() const
{
    const QString basePath = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("captures"));
    QDir().mkpath(basePath);
    return normalizedPath(basePath);
}

QString CaptureHelper::createTimestampedDirectory(const QString &prefix) const
{
    const QString baseDir = defaultCaptureDirectory();
    const QString folderName = QStringLiteral("%1_%2")
                                   .arg(prefix.trimmed().isEmpty() ? QStringLiteral("AEGIS_Captures") : prefix.trimmed(),
                                        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    const QString outDir = QDir(baseDir).filePath(folderName);
    if (!QDir().mkpath(outDir))
        return QString();
    return normalizedPath(outDir);
}

void CaptureHelper::captureObject(QObject *target, const QString &filePath, qreal scale, int dpi)
{
    if (!target) {
        qWarning() << "[CAPTURE][CPP] target is null";
        emitFailure(filePath, QStringLiteral("Capture target is null."));
        return;
    }
    if (filePath.trimmed().isEmpty()) {
        qWarning() << "[CAPTURE][CPP] output path is empty";
        emitFailure(filePath, QStringLiteral("Capture output path is empty."));
        return;
    }

    qInfo() << "[CAPTURE][CPP] request target=" << target->metaObject()->className()
            << "path=" << normalizedPath(filePath)
            << "scale=" << scale
            << "dpi=" << dpi;

    if (QQuickItem *item = resolveCaptureItem(target)) {
        qInfo() << "[CAPTURE][CPP] resolved item=" << item->metaObject()->className()
                << "size=" << item->size();
        captureItem(item, filePath, scale, dpi);
        return;
    }

    qWarning() << "[CAPTURE][CPP] unsupported target type" << target->metaObject()->className();
    emitFailure(filePath,
                QStringLiteral("Unsupported capture target type or missing popupItem/contentItem: %1")
                    .arg(QString::fromLatin1(target->metaObject()->className())));
}

void CaptureHelper::captureItem(QQuickItem *item, const QString &filePath, qreal scale, int dpi)
{
    if (!item) {
        qWarning() << "[CAPTURE][CPP] resolved item is null";
        emitFailure(filePath, QStringLiteral("Capture item is null."));
        return;
    }
    if (!item->window()) {
        qWarning() << "[CAPTURE][CPP] item has no window" << item->metaObject()->className();
        emitFailure(filePath, QStringLiteral("Capture item is not attached to a window."));
        return;
    }

    const QSizeF logicalSize = item->size();
    if (logicalSize.width() <= 0.0 || logicalSize.height() <= 0.0) {
        qWarning() << "[CAPTURE][CPP] invalid item size" << logicalSize;
        emitFailure(filePath, QStringLiteral("Capture item has an invalid size."));
        return;
    }

    QString pathError;
    if (!ensureOutputDirectory(filePath, &pathError)) {
        emitFailure(filePath, pathError);
        return;
    }

    const qreal safeScale = qMax(kMinimumCaptureScale, scale);
    const QSize targetSize(qMax(1, qCeil(logicalSize.width() * safeScale)),
                           qMax(1, qCeil(logicalSize.height() * safeScale)));
    qInfo() << "[CAPTURE][CPP] grabToImage targetSize=" << targetSize;
    const QSharedPointer<QQuickItemGrabResult> result = item->grabToImage(targetSize);
    if (result.isNull()) {
        qWarning() << "[CAPTURE][CPP] grabToImage request failed";
        emitFailure(filePath, QStringLiteral("Failed to start the grab request."));
        return;
    }

    const auto completed = QSharedPointer<bool>::create(false);

    QObject::connect(result.data(), &QQuickItemGrabResult::ready, this,
                     [this, result, filePath, dpi, completed]() {
        if (*completed)
            return;
        *completed = true;
        QImage image = result->image();
        if (image.isNull()) {
            qWarning() << "[CAPTURE][CPP] grab result image is null";
            emitFailure(filePath, QStringLiteral("The grab result is empty."));
            return;
        }

        const int safeDpi = qMax(1, dpi);
        const int dotsPerMeter = dotsPerMeterFromDpi(safeDpi);
        image.setDotsPerMeterX(dotsPerMeter);
        image.setDotsPerMeterY(dotsPerMeter);

        if (!image.save(filePath, "PNG")) {
            qWarning() << "[CAPTURE][CPP] failed to save image" << normalizedPath(filePath);
            emitFailure(filePath,
                        QStringLiteral("Failed to save PNG: %1").arg(normalizedPath(filePath)));
            return;
        }

        qInfo() << "[CAPTURE][CPP] saved" << normalizedPath(filePath)
                << "pixels=" << image.size()
                << "dpi=" << safeDpi;
        emit captureFinished(normalizedPath(filePath), true, QString());
    });

    QTimer::singleShot(2500, this, [this, filePath, completed]() {
        if (*completed)
            return;
        *completed = true;
        qWarning() << "[CAPTURE][CPP] grabToImage timed out waiting for ready signal";
        emitFailure(filePath, QStringLiteral("grabToImage timed out before the item was rendered."));
    });
}

void CaptureHelper::emitFailure(const QString &filePath, const QString &error)
{
    qWarning() << "[CAPTURE][CPP] failure path=" << normalizedPath(filePath)
               << "error=" << error;
    emit captureFinished(normalizedPath(filePath), false, error);
}

bool CaptureHelper::ensureOutputDirectory(const QString &filePath, QString *error) const
{
    const QFileInfo fileInfo(filePath);
    const QString dirPath = fileInfo.absolutePath();
    if (dirPath.isEmpty()) {
        if (error)
            *error = QStringLiteral("Capture output directory is invalid.");
        return false;
    }

    if (QDir().mkpath(dirPath))
        return true;

    if (error)
        *error = QStringLiteral("Failed to create output directory: %1").arg(normalizedPath(dirPath));
    return false;
}
