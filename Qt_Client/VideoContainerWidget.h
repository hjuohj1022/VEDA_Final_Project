#ifndef VIDEOCONTAINERWIDGET_H
#define VIDEOCONTAINERWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QFrame>

class VideoContainerWidget : public QFrame {
    Q_OBJECT
public:
    explicit VideoContainerWidget(int id, QString location, QWidget *parent = nullptr);
    
    // Returns the widget where VLC should render
    QWidget* getVideoWidget() const;
    void setStatus(QString status);
    void updateTheme(); // "Live", "Alert", "Offline"

private:
    void setupUi();
    
    int cameraId;
    QString locationName;
    QWidget *videoArea;
    QLabel *statusLabel;
};

#endif // VIDEOCONTAINERWIDGET_H
