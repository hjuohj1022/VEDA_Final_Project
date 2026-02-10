#ifndef SIDEBARWIDGET_H
#define SIDEBARWIDGET_H

#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QTimer>

class SidebarWidget : public QWidget {
    Q_OBJECT
public:
    explicit SidebarWidget(QWidget *parent = nullptr);

signals:
    void pageChanged(int index); // 0: Live, 1: Recordings

public slots:
    void updateFps(int fps);
    void updateLatency(int ms);
    void updateActiveCameras(int count, int total);
    void updateStorage(QString used, QString total, int percent);
    void updateTheme();

private:
    void setupUi();
    QWidget* createMetricWidget(QString name, int value, QString unit);
    QWidget* createInfoCard(QString title, QString value, QString subtext, QString iconText);
    
    QTimer *updateTimer;

    // UI Elements to update
    QProgressBar *fpsBar;
    QLabel *fpsValueLabel;
    
    QProgressBar *latencyBar;
    QLabel *latencyValueLabel;

    QLabel *activeCamValue;
    QLabel *activeCamSub;
    
    QLabel *storageValue;
    QLabel *storageSub;
};

#endif // SIDEBARWIDGET_H
