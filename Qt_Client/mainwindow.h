#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMediaPlayer>
#include <QVideoWidget>
#include <QAudioOutput>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QListWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QSlider>
#include <QStackedWidget>
#include <QTimer>
#include <QMap>
#include <QProgressDialog>
#include <vlc/vlc.h>

#include "SidebarWidget.h"
#include "HeaderWidget.h"
#include "VideoContainerWidget.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    // Page Navigation
    void onPageChanged(int index);

    // Tab 1 (recordings) logic
    void onLoginClicked();
    void onRefreshClicked();
    void onLinkActivated(const QString &link); // Not used currently but standard
    void onFileDoubleClicked(QListWidgetItem *item);
    void onPlayPauseClicked();
    void onDeleteClicked();

    // Network replies
    void onLoginReply(QNetworkReply *reply);
    void onListReply(QNetworkReply *reply);
    void onDeleteReply(QNetworkReply *reply);
    void onStorageReply(QNetworkReply *reply);
    void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);

    // Tab 2 (live) logic
    void updateLiveFps();
    void updateServerStorage();
    void toggleTheme();

private:
    Ui::MainWindow *ui;
    
    // Core Layout Widgets
    SidebarWidget *sidebar;
    HeaderWidget *header;
    QStackedWidget *mainStackedWidget;

    // Data
    QMap<QString, QString> env;
    void loadEnv();
    QString serverUrl;

    // VLC
    libvlc_instance_t *vlcInstance;
    // We now use VideoContainerWidget to hold the player, but we still need to track the players
    QList<libvlc_media_player_t*> liveVlcPlayers;
    QList<VideoContainerWidget*> videoContainers; // 4 containers
    QTimer *fpsTimer;

    // Widgets (Live Page)
    QWidget *livePage;

    // Widgets (Recordings Page - repurposed from Tab 1)
    QWidget *recordPage;
    QLineEdit *idInput;
    QLineEdit *pwInput;
    QPushButton *btnLogin;
    QPushButton *btnRefresh;
    QPushButton *btnDelete;
    QListWidget *fileListWidget;
    QVideoWidget *videoWidget;
    QSlider *seekSlider;
    QPushButton *btnPlayPause;
    QLabel *timeLabel;
    
    QString formatTime(qint64 ms);

    QMediaPlayer *player;
    QAudioOutput *audioOutput;
    QNetworkAccessManager *networkManager;
    QNetworkReply *currentDownload = nullptr;
    QString currentTempFile;
    QProgressDialog *progressDialog = nullptr;
    
    void updateWindowStyle();
};

#endif // MAINWINDOW_H
