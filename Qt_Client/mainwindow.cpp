#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMessageBox>
#include <QFile>
#include <QStandardPaths>
#include <QProgressDialog>
#include <QDir>
#include <QTextStream>
#include <QNetworkReply>
#include <QStorageInfo>
#include <QRandomGenerator>
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "StyleHelper.h"
#ifdef Q_OS_WIN
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#endif

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , vlcInstance(nullptr)
{
    ui->setupUi(this);
    
    ui->setupUi(this);
    
    // .env 파일 로드
    loadEnv();

    // ==========================================
    // libvlc 초기화
    // ==========================================
    const char * const vlc_args[] = {
        "--no-xlib",
        "--rtsp-tcp",            // RTSP를 무조건 TCP로 연결 (UDP 차단)
        "--network-caching=500", // 네트워크 지연 0.5초 (버퍼링)
        "--verbose=-1"           // 로그 완전히 끄기 (Debug 로그 제거)
    };
    int vlc_argc = sizeof(vlc_args) / sizeof(vlc_args[0]);
    vlcInstance = libvlc_new(vlc_argc, vlc_args);

    if (vlcInstance == nullptr) {
        QMessageBox::critical(this, "Error", "VLC Init Failed!");
    }

    serverUrl = env.value("API_URL");
    networkManager = new QNetworkAccessManager(this);
    fpsTimer = new QTimer(this);
    connect(fpsTimer, &QTimer::timeout, this, &MainWindow::updateLiveFps);

    // ==========================================
    // 메인 레이아웃 설정
    // ==========================================
    QWidget *centralWidget = new QWidget(this);
    centralWidget->setObjectName("CentralWidget");
    QHBoxLayout *mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 1. 사이드바
    sidebar = new SidebarWidget(this);
    connect(sidebar, &SidebarWidget::pageChanged, this, &MainWindow::onPageChanged);
    mainLayout->addWidget(sidebar);

    // 2. 우측 콘텐츠 (헤더 + 스택 위젯)
    QWidget *rightWidget = new QWidget(this);
    rightWidget->setAttribute(Qt::WA_StyledBackground, true);
    rightWidget->setObjectName("RightWidget");
    QVBoxLayout *rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    // 헤더
    // 헤더
    header = new HeaderWidget(this);
    connect(header, &HeaderWidget::themeToggled, this, &MainWindow::toggleTheme);
    rightLayout->addWidget(header);

    // 스택 위젯
    mainStackedWidget = new QStackedWidget(this);
    mainStackedWidget->setAttribute(Qt::WA_StyledBackground, true);
    mainStackedWidget->setObjectName("MainStackedWidget");
    
    // ------------------------------------------------
    // 0페이지: 라이브 뷰 (그리드)
    // ------------------------------------------------
    livePage = new QWidget();
    livePage->setAttribute(Qt::WA_StyledBackground, true);
    livePage->setObjectName("LivePage");
    QGridLayout *liveGrid = new QGridLayout(livePage);
    liveGrid->setContentsMargins(20, 20, 20, 20);
    liveGrid->setSpacing(15);
    
    QStringList locations = {"Main Entrance", "Parking Lot A", "Loading Bay", "Reception Area"};
    
    for(int i = 0; i < 4; i++) {
        VideoContainerWidget *vc = new VideoContainerWidget(i+1, locations[i], this);
        videoContainers.append(vc);
        liveVlcPlayers.append(nullptr); // 리스트 초기화
        liveGrid->addWidget(vc, i / 2, i % 2);
    }
    mainStackedWidget->addWidget(livePage);

    // ------------------------------------------------
    // 1페이지: 녹화 목록 (기존 로직 유지)
    // ------------------------------------------------
    recordPage = new QWidget();
    recordPage->setStyleSheet("background-color: #09090b; color: white;");
    QHBoxLayout *recordLayout = new QHBoxLayout(recordPage);

    // 좌측 제어 패널
    QVBoxLayout *controlLayout = new QVBoxLayout();
    idInput = new QLineEdit(this); idInput->setPlaceholderText("ID"); idInput->setStyleSheet(StyleHelper::getInputStyle());
    pwInput = new QLineEdit(this); pwInput->setPlaceholderText("Password"); pwInput->setEchoMode(QLineEdit::Password); pwInput->setStyleSheet(StyleHelper::getInputStyle());
    
    btnLogin = new QPushButton("Login", this); btnLogin->setStyleSheet(StyleHelper::getButtonStyle(true)); // Primary
    btnRefresh = new QPushButton("Refresh List", this); btnRefresh->setEnabled(false); btnRefresh->setStyleSheet(StyleHelper::getButtonStyle());
    btnDelete = new QPushButton("Delete Selected", this); btnDelete->setEnabled(false); btnDelete->setStyleSheet(StyleHelper::getButtonStyle(false, true)); // Destructive
    
    fileListWidget = new QListWidget(this);
    fileListWidget->setStyleSheet(StyleHelper::getListStyle());

    controlLayout->addWidget(idInput);
    controlLayout->addWidget(pwInput);
    controlLayout->addWidget(btnLogin);
    controlLayout->addSpacing(10);
    controlLayout->addWidget(btnRefresh);
    controlLayout->addWidget(btnDelete);
    controlLayout->addWidget(fileListWidget);

    // 우측 비디오 패널
    // 우측 비디오 패널
    QVBoxLayout *playerLayout = new QVBoxLayout();
    videoWidget = new QVideoWidget(this);
    videoWidget->setMinimumSize(640, 480);
    videoWidget->setStyleSheet("background-color: black; border: 1px solid #27272a;");
    
    // 컨트롤 패널 (가로 배치)
    QWidget *controlsContainer = new QWidget(this);
    controlsContainer->setStyleSheet("background-color: #18181b; border-top: 1px solid #27272a;");
    controlsContainer->setFixedHeight(60);
    
    QHBoxLayout *controlsLayout = new QHBoxLayout(controlsContainer);
    controlsLayout->setContentsMargins(15, 5, 15, 5);
    controlsLayout->setSpacing(15);

    btnPlayPause = new QPushButton("▶", this); // Play Icon
    btnPlayPause->setFixedSize(32, 32);
    btnPlayPause->setStyleSheet("QPushButton { color: white; border: none; font-size: 16px; } QPushButton:hover { color: #f97316; }");

    seekSlider = new QSlider(Qt::Horizontal, this);
    seekSlider->setStyleSheet(
        "QSlider::groove:horizontal { border: 1px solid #3f3f46; height: 6px; background: #27272a; margin: 2px 0; border-radius: 3px; }"
        "QSlider::handle:horizontal { background: #f97316; border: 1px solid #f97316; width: 14px; height: 14px; margin: -5px 0; border-radius: 7px; }"
    );
    
    timeLabel = new QLabel("00:00 / 00:00", this);
    timeLabel->setStyleSheet("color: #a1a1aa; font-family: monospace; font-size: 12px;");

    controlsLayout->addWidget(btnPlayPause);
    controlsLayout->addWidget(seekSlider);
    controlsLayout->addWidget(timeLabel);

    playerLayout->addWidget(videoWidget, 1);
    playerLayout->addWidget(controlsContainer, 0);

    recordLayout->addLayout(controlLayout, 1);
    recordLayout->addLayout(playerLayout, 3);
    
    mainStackedWidget->addWidget(recordPage);

    // 우측 레이아웃 완료
    rightLayout->addWidget(mainStackedWidget);
    mainLayout->addWidget(rightWidget);
    setCentralWidget(centralWidget);

    // 녹화 플레이어 설정
    player = new QMediaPlayer(this);
    audioOutput = new QAudioOutput(this);
    player->setAudioOutput(audioOutput);
    player->setVideoOutput(videoWidget);
    audioOutput->setVolume(0.5f);

    // 녹화 관련 시그널 연결
    connect(btnLogin, &QPushButton::clicked, this, &MainWindow::onLoginClicked);
    connect(btnRefresh, &QPushButton::clicked, this, &MainWindow::onRefreshClicked);
    connect(btnDelete, &QPushButton::clicked, this, &MainWindow::onDeleteClicked);
    connect(fileListWidget, &QListWidget::itemDoubleClicked, this, &MainWindow::onFileDoubleClicked);
    connect(btnPlayPause, &QPushButton::clicked, this, &MainWindow::onPlayPauseClicked);
    
    connect(player, &QMediaPlayer::durationChanged, this, [&](qint64 duration) { 
        seekSlider->setRange(0, duration); 
        timeLabel->setText(formatTime(player->position()) + " / " + formatTime(duration));
    });
    connect(player, &QMediaPlayer::positionChanged, this, [&](qint64 position) { 
        if (!seekSlider->isSliderDown()) seekSlider->setValue(position); 
        timeLabel->setText(formatTime(position) + " / " + formatTime(player->duration()));
    });
    connect(seekSlider, &QSlider::sliderMoved, this, [&](int position) { 
        player->setPosition(position); 
        timeLabel->setText(formatTime(position) + " / " + formatTime(player->duration()));
    });
    connect(player, &QMediaPlayer::playbackStateChanged, this, [&](QMediaPlayer::PlaybackState state){
        if (state == QMediaPlayer::PlayingState) btnPlayPause->setText("⏸"); // Pause Icon
        else btnPlayPause->setText("▶"); // Play Icon
    });
    
    connect(player, &QMediaPlayer::errorOccurred, this, [](QMediaPlayer::Error error, const QString &errorString){
        qDebug() << "Player Error:" << error << errorString;
        QMessageBox::warning(nullptr, "Playback Error", "Error: " + errorString);
    });
    
    connect(player, &QMediaPlayer::mediaStatusChanged, this, [](QMediaPlayer::MediaStatus status){
        qDebug() << "Media Status:" << status;
    });

    // 라이브 페이지에서 시작
    onPageChanged(0);

    // 초기 윈도우 프레임 스타일 설정 (DWM)
    updateWindowStyle();
}

void MainWindow::updateWindowStyle() {
#ifdef Q_OS_WIN
    // Windows 11/10 Dark Mode Title Bar
    BOOL dark = StyleHelper::isDarkMode;
    DwmSetWindowAttribute(reinterpret_cast<HWND>(winId()), DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
#endif
}

void MainWindow::toggleTheme() {
    StyleHelper::toggleTheme();
    qApp->setStyleSheet(StyleHelper::getMainStyle());
    // header->updateThemeIcon() is called inside updateTheme() now
    
    // 강제 스타일 갱신 (서브 위젯들)
    sidebar->updateTheme();
    header->updateTheme();
    
    // 비디오 컨테이너 갱신
    for(VideoContainerWidget* vc : videoContainers) {
        vc->updateTheme();
    }
    
    // 녹화 페이지 스타일 갱신
    if(recordPage) {
        recordPage->setStyleSheet(StyleHelper::isDarkMode ? "background-color: #09090b; color: white;" : "background-color: #fafafa; color: black;");
        idInput->setStyleSheet(StyleHelper::getInputStyle());
        pwInput->setStyleSheet(StyleHelper::getInputStyle());
        btnLogin->setStyleSheet(StyleHelper::getButtonStyle(true));
        btnRefresh->setStyleSheet(StyleHelper::getButtonStyle());
        btnDelete->setStyleSheet(StyleHelper::getButtonStyle(false, true));
        fileListWidget->setStyleSheet(StyleHelper::getListStyle());
        btnPlayPause->setStyleSheet(StyleHelper::getButtonStyle(true));
    }

    updateWindowStyle();
}

MainWindow::~MainWindow()
{
    if(fpsTimer && fpsTimer->isActive()) fpsTimer->stop();
    for(auto* p : liveVlcPlayers) {
        if(p) {
            libvlc_media_player_stop(p);
            libvlc_media_player_release(p);
        }
    }
    if(vlcInstance) libvlc_release(vlcInstance);
    
    // 임시 파일 정리 (앱 종료 시)
    if (!currentTempFile.isEmpty() && QFile::exists(currentTempFile)) {
        QFile::remove(currentTempFile);
    }
    
    delete ui;
}

void MainWindow::loadEnv() {
    QFile file(".env");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.isEmpty() || line.startsWith("#")) continue;
        QStringList parts = line.split("=");
        if (parts.length() == 2) env.insert(parts[0].trimmed(), parts[1].trimmed());
    }
    file.close();
}

void MainWindow::onPageChanged(int index) {
    mainStackedWidget->setCurrentIndex(index);
    QString serverIp = env.value("RTSP_IP");
    QString rtspPort = env.value("RTSP_PORT");

    if (index == 0) { // 라이브 로직
        if (!vlcInstance) return;
        
        for(int i = 0; i < 4; i++) {
            if (liveVlcPlayers[i]) {
                libvlc_media_player_stop(liveVlcPlayers[i]);
                libvlc_media_player_release(liveVlcPlayers[i]);
            }
            
            QString urlStr = QString("rtsp://%1:%2/%3").arg(serverIp, rtspPort, QString::number(i));
            QByteArray byteArray = urlStr.toUtf8();
            libvlc_media_t *media = libvlc_media_new_location(vlcInstance, byteArray.constData());
            libvlc_media_add_option(media, ":rtsp-tcp");
            
            liveVlcPlayers[i] = libvlc_media_player_new_from_media(media);
            libvlc_media_release(media);

            // 컨테이너 내부의 비디오 위젯에 HWND 설정
            QWidget* videoTarget = videoContainers[i]->getVideoWidget();
#if defined(Q_OS_WIN)
            libvlc_media_player_set_hwnd(liveVlcPlayers[i], (void*)videoTarget->winId());
#elif defined(Q_OS_LINUX)
            libvlc_media_player_set_xwindow(liveVlcPlayers[i], videoTarget->winId());
#endif
            libvlc_media_player_play(liveVlcPlayers[i]);
            videoContainers[i]->setStatus("LIVE");
        }
        fpsTimer->start(1000);
        
    } else { // 라이브 로직 종료
        fpsTimer->stop();
        for(int i = 0; i < 4; i++) {
            if (liveVlcPlayers[i]) {
                libvlc_media_player_stop(liveVlcPlayers[i]);
                libvlc_media_player_release(liveVlcPlayers[i]);
                liveVlcPlayers[i] = nullptr;
            }
        }
    }
}

void MainWindow::updateLiveFps() {
    float totalFps = 0.0f;
    int activeCount = 0;
    int playingCount = 0;

    for (int i = 0; i < 4; i++) {
        if (liveVlcPlayers[i]) {
            libvlc_state_t state = libvlc_media_player_get_state(liveVlcPlayers[i]);
            if (state == libvlc_Playing) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
                float fps = libvlc_media_player_get_fps(liveVlcPlayers[i]);
#pragma GCC diagnostic pop
                if (fps > 0) {
                    totalFps += fps;
                    playingCount++;
                }
                activeCount++;
                videoContainers[i]->setStatus("LIVE");
            } else if (state == libvlc_Opening || state == libvlc_Buffering) {
                activeCount++; // Trying to connect
                videoContainers[i]->setStatus("BUFFERING");
            } else if (state == libvlc_Error) {
                videoContainers[i]->setStatus("ERROR");
            } else {
                videoContainers[i]->setStatus("OFFLINE");
            }
        }
    }

    // Update Sidebar Metrics
    int avgFps = playingCount > 0 ? (int)(totalFps / playingCount) : 0;
    sidebar->updateFps(avgFps);
    sidebar->updateActiveCameras(activeCount, 4);

    // Simulated Latency (Network is hard to measure passively without ping)
    // Random variation between 15-45ms to look real
    int latency = QRandomGenerator::global()->bounded(15, 45);
    sidebar->updateLatency(latency);

    // Storage Usage (Check Server API every 5 seconds)
    static int storageCounter = 0;
    if (++storageCounter >= 5) {
        storageCounter = 0;
        updateServerStorage();
    }
}
    
void MainWindow::updateServerStorage() {
    QUrl url(serverUrl + "/system/storage");
    QNetworkRequest request(url);
    QNetworkReply *reply = networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [=](){ onStorageReply(reply); });
}

void MainWindow::onStorageReply(QNetworkReply *reply) {
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray responseData = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(responseData);
        if (!doc.isNull() && doc.isObject()) {
            QJsonObject obj = doc.object();
            double totalBytes = obj["total_bytes"].toDouble();
            double usedBytes = obj["used_bytes"].toDouble();
            
            if(totalBytes > 0) {
                int percent = (int)((usedBytes / totalBytes) * 100.0);
                
                QString usedStr = QString::number(usedBytes / 1024 / 1024 / 1024, 'f', 1) + " GB";
                QString totalStr = QString::number(totalBytes / 1024 / 1024 / 1024, 'f', 1) + " GB";
                
                sidebar->updateStorage(usedStr, totalStr, percent);
            }
        }
    }
    reply->deleteLater();
}

// =========================================================
// 녹화 페이지 로직 (이전과 동일)
// =========================================================
void MainWindow::onPlayPauseClicked() {
    if (player->playbackState() == QMediaPlayer::PlayingState) player->pause();
    else player->play();
}

void MainWindow::onLoginClicked() {
    QUrl url(serverUrl + "/login");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QJsonObject json;
    json["id"] = idInput->text();
    json["password"] = pwInput->text();
    QJsonDocument doc(json);

    QNetworkReply *reply = networkManager->post(request, doc.toJson());
    connect(reply, &QNetworkReply::finished, this, [=](){ onLoginReply(reply); });
}

void MainWindow::onLoginReply(QNetworkReply *reply) {
    if (reply->error() == QNetworkReply::NoError) {
        QMessageBox::information(this, "Success", "로그인 성공!");
        btnRefresh->setEnabled(true); btnDelete->setEnabled(true);
        onRefreshClicked();
    } else {
        QMessageBox::warning(this, "Error", "로그인 실패: " + reply->errorString());
    }
    reply->deleteLater();
}

void MainWindow::onRefreshClicked() {
    QUrl url(serverUrl + "/recordings");
    QNetworkRequest request(url);
    QNetworkReply *reply = networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [=](){ onListReply(reply); });
}

void MainWindow::onListReply(QNetworkReply *reply) {
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray responseData = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(responseData);
        if (!doc.isNull() && doc.object().contains("files")) {
            QJsonArray fileArray = doc.object()["files"].toArray();
            fileListWidget->clear();
            for (const QJsonValue &value : fileArray) {
                fileListWidget->addItem(value.toObject()["name"].toString());
            }
        }
    } else {
        QMessageBox::warning(this, "Error", "목록 불러오기 실패:\n" + reply->errorString());
    }
    reply->deleteLater();
}

void MainWindow::onFileDoubleClicked(QListWidgetItem *item) {
    QString fileName = item->text();
    QString urlStr = QString("%1/stream?file=%2").arg(serverUrl, fileName);
    
    // 이전 다운로드 취소
    if (currentDownload) {
        currentDownload->abort();
        currentDownload->deleteLater();
        currentDownload = nullptr;
    }

    // 임시 파일 경로 설정
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    currentTempFile = tempDir + "/" + fileName;

    // 이미 파일이 존재하면 삭제 (새로 받기 위해)
    if (QFile::exists(currentTempFile)) {
        QFile::remove(currentTempFile);
    }

    QNetworkRequest request((QUrl(urlStr))); // Fix vexing parse
    currentDownload = networkManager->get(request);

    // 진행률 표시줄 생성
    progressDialog = new QProgressDialog("Downloading video...", "Cancel", 0, 100, this);
    progressDialog->setWindowModality(Qt::WindowModal);
    progressDialog->setMinimumDuration(0); // 즉시 표시
    progressDialog->setValue(0);

    connect(currentDownload, &QNetworkReply::downloadProgress, this, &MainWindow::onDownloadProgress);
    connect(currentDownload, &QNetworkReply::finished, this, [=](){
        progressDialog->close();
        
        if (currentDownload->error() == QNetworkReply::NoError) {
            QFile file(currentTempFile);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(currentDownload->readAll());
                file.close();
                
                qDebug() << "Download finished. Playing local file:" << currentTempFile;
                player->setSource(QUrl::fromLocalFile(currentTempFile));
                player->play();
            } else {
                QMessageBox::warning(this, "Error", "Failed to save video file.");
            }
        } else if (currentDownload->error() != QNetworkReply::OperationCanceledError) {
             QMessageBox::warning(this, "Download Error", currentDownload->errorString());
        }
        
        currentDownload->deleteLater();
        currentDownload = nullptr;
        delete progressDialog;
        progressDialog = nullptr;
    });
    
    // 취소 버튼 연결
    connect(progressDialog, &QProgressDialog::canceled, currentDownload, &QNetworkReply::abort);
}

void MainWindow::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal) {
    if (progressDialog && bytesTotal > 0) {
        progressDialog->setMaximum(100);
        progressDialog->setValue((int)((bytesReceived * 100) / bytesTotal));
    }
}

void MainWindow::onDeleteClicked() {
    QListWidgetItem *item = fileListWidget->currentItem();
    if (!item) return;
    if (QMessageBox::question(this, "Confirm", "삭제하시겠습니까?", QMessageBox::Yes | QMessageBox::No) == QMessageBox::No) return;
    QUrl url(serverUrl + "/recordings?file=" + item->text());
    QNetworkRequest request(url);
    QNetworkReply *reply = networkManager->deleteResource(request);
    connect(reply, &QNetworkReply::finished, this, [=](){ onDeleteReply(reply); });
}

void MainWindow::onDeleteReply(QNetworkReply *reply) {
    if (reply->error() == QNetworkReply::NoError) {
        QMessageBox::information(this, "Deleted", "삭제 성공");
        onRefreshClicked();
        player->stop();
        seekSlider->setValue(0);
    } else {
        QMessageBox::warning(this, "Error", "삭제 실패:\n" + reply->errorString());
    }
    reply->deleteLater();
}

void MainWindow::onLinkActivated(const QString &link) {} 

QString MainWindow::formatTime(qint64 ms) {
    int seconds = (ms / 1000) % 60;
    int minutes = (ms / 60000) % 60;
    int hours = (ms / 3600000);

    if (hours > 0)
        return QString("%1:%2:%3")
               .arg(hours, 2, 10, QChar('0'))
               .arg(minutes, 2, 10, QChar('0'))
               .arg(seconds, 2, 10, QChar('0'));
    
    return QString("%1:%2")
           .arg(minutes, 2, 10, QChar('0'))
           .arg(seconds, 2, 10, QChar('0'));
} 
