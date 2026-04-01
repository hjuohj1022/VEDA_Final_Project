#ifndef CAPTUREHELPER_H
#define CAPTUREHELPER_H

#include <QObject>

class QQuickItem;

class CaptureHelper : public QObject
{
    Q_OBJECT

public:
    explicit CaptureHelper(QObject *parent = nullptr);

    Q_INVOKABLE QString defaultCaptureDirectory() const;
    Q_INVOKABLE QString createTimestampedDirectory(const QString &prefix = QStringLiteral("AEGIS_Captures")) const;
    Q_INVOKABLE void captureObject(QObject *target,
                                   const QString &filePath,
                                   qreal scale = 2.0,
                                   int dpi = 300);

signals:
    void captureFinished(QString path, bool success, QString error);

private:
    void captureItem(QQuickItem *item, const QString &filePath, qreal scale, int dpi);
    void emitFailure(const QString &filePath, const QString &error);
    bool ensureOutputDirectory(const QString &filePath, QString *error) const;
};

#endif // CAPTUREHELPER_H
