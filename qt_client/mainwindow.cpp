#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QNetworkReply>
#include "mainwindow.h"
#include "./ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , vlcInstance(nullptr) // 초기화
{
    ui->setupUi(this);

    // .env 파일 로드
    loadEnv();

    // ==========================================
    // libvlc 인스턴스 생성 (TCP 강제 옵션)
    // ==========================================
    const char * const vlc_args[] = {
        "--no-xlib",             // 리눅스용 (윈도우는 무시됨)
        "--rtsp-tcp",            // RTSP를 무조건 TCP로 연결 (UDP 차단)
        "--network-caching=500", // 네트워크 지연 0.5초 (버퍼링)
        "--verbose=-1"           // 로그 완전히 끄기 (Debug 로그 제거)
    };
    int vlc_argc = sizeof(vlc_args) / sizeof(vlc_args[0]);

    vlcInstance = libvlc_new(vlc_argc, vlc_args);

    if (vlcInstance == nullptr) {
        QMessageBox::critical(this, "Error", "VLC 인스턴스 생성 실패! DLL 파일을 확인하세요.");
    }

    // ==========================================
    // 서버 주소 설정
    // ==========================================
    serverUrl = env.value("API_URL"); // 기본값 설정
    qDebug() << "API URL:" << serverUrl; // [디버그] 연결된 서버 주소 확인
    qDebug() << "Target IP:" << env.value("RTSP_IP") << "Target Port:" << env.value("RTSP_PORT");

    // 네트워크 매니저 생성
    networkManager = new QNetworkAccessManager(this);

    // FPS 갱신용 타이머 생성 및 연결
    fpsTimer = new QTimer(this);
    connect(fpsTimer, &QTimer::timeout, this, &MainWindow::updateLiveFps);

    // ==========================================
    // 메인 UI 구성 (탭 위젯 도입)
    // ==========================================
    QWidget *centralWidget = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);

    // 탭 위젯 생성
    mainTabWidget = new QTabWidget(this);
    mainLayout->addWidget(mainTabWidget);
    setCentralWidget(centralWidget);

    // ============================================================
    // [Tab 1] 녹화 목록 및 재생 (기존 코드 유지)
    // ============================================================
    recordTab = new QWidget();
    QHBoxLayout *recordLayout = new QHBoxLayout(recordTab);

    // [왼쪽 패널]
    QVBoxLayout *controlLayout = new QVBoxLayout();
    idInput = new QLineEdit(this); idInput->setPlaceholderText("ID");
    pwInput = new QLineEdit(this); pwInput->setPlaceholderText("Password"); pwInput->setEchoMode(QLineEdit::Password);
    btnLogin = new QPushButton("Login", this);
    btnRefresh = new QPushButton("Refresh List", this); btnRefresh->setEnabled(false);
    btnDelete = new QPushButton("Delete Selected", this); btnDelete->setEnabled(false);
    fileListWidget = new QListWidget(this);

    controlLayout->addWidget(idInput);
    controlLayout->addWidget(pwInput);
    controlLayout->addWidget(btnLogin);
    controlLayout->addSpacing(10);
    controlLayout->addWidget(btnRefresh);
    controlLayout->addWidget(btnDelete);
    controlLayout->addWidget(fileListWidget);

    // [오른쪽 패널]
    QVBoxLayout *rightLayout = new QVBoxLayout();
    videoWidget = new QVideoWidget(this);
    videoWidget->setMinimumSize(640, 480);
    videoWidget->setStyleSheet("background-color: black;");
    seekSlider = new QSlider(Qt::Horizontal, this); seekSlider->setRange(0, 0);
    btnPlayPause = new QPushButton("Play", this);

    rightLayout->addWidget(videoWidget, 1);
    rightLayout->addWidget(seekSlider);
    rightLayout->addWidget(btnPlayPause);

    recordLayout->addLayout(controlLayout, 1);
    recordLayout->addLayout(rightLayout, 3);
    mainTabWidget->addTab(recordTab, "Recordings");

    // [Tab 1]용 플레이어 (QtMultimedia 유지 - HTTP 재생용)
    player = new QMediaPlayer(this);
    audioOutput = new QAudioOutput(this);
    player->setAudioOutput(audioOutput);
    player->setVideoOutput(videoWidget);
    audioOutput->setVolume(0.5f);

    // [Tab 1] 이벤트 연결
    connect(btnLogin, &QPushButton::clicked, this, &MainWindow::onLoginClicked);
    connect(btnRefresh, &QPushButton::clicked, this, &MainWindow::onRefreshClicked);
    connect(btnDelete, &QPushButton::clicked, this, &MainWindow::onDeleteClicked);
    connect(fileListWidget, &QListWidget::itemDoubleClicked, this, &MainWindow::onFileDoubleClicked);
    connect(btnPlayPause, &QPushButton::clicked, this, &MainWindow::onPlayPauseClicked);

    connect(player, &QMediaPlayer::durationChanged, this, [&](qint64 duration) { seekSlider->setRange(0, duration); });
    connect(player, &QMediaPlayer::positionChanged, this, [&](qint64 position) { if (!seekSlider->isSliderDown()) seekSlider->setValue(position); });
    connect(seekSlider, &QSlider::sliderMoved, this, [&](int position) { player->setPosition(position); });
    connect(player, &QMediaPlayer::playbackStateChanged, this, [&](QMediaPlayer::PlaybackState state){
        if (state == QMediaPlayer::PlayingState) btnPlayPause->setText("Pause"); else btnPlayPause->setText("Play");
    });

    // ============================================================
    // [Tab 2] 실시간 라이브 (libvlc 적용)
    // ============================================================
    liveTab = new QWidget();
    QGridLayout *liveGridLayout = new QGridLayout(liveTab);
    liveGridLayout->setSpacing(2);

    // 4개의 화면 위젯 생성
    for(int i = 0; i < 4; i++) {
        QWidget *vid = new QWidget(this);
        // 검은 배경 설정
        QPalette pal = vid->palette();
        pal.setColor(QPalette::Window, Qt::black);
        vid->setAutoFillBackground(true);
        vid->setPalette(pal);
        vid->setStyleSheet("border: 1px solid gray;");

        // 리스트에 위젯 보관
        liveVideoWidgets.append(vid);
        // VLC 플레이어 리스트는 nullptr로 초기화 (탭 들어갈 때 생성)
        liveVlcPlayers.append(nullptr);

        // Grid 배치
        liveGridLayout->addWidget(vid, i / 2, i % 2);
    }

    mainTabWidget->addTab(liveTab, "Live View (4CH)");

    // 탭 변경 시 이벤트 연결 (라이브 영상 재생/정지 제어용)
    connect(mainTabWidget, &QTabWidget::currentChanged, this, &MainWindow::onLiveTabChanged);
}

void MainWindow::loadEnv() {
    QFile file(".env");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "Failed to open .env file. Using defaults.";
        return;
    }

    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.isEmpty() || line.startsWith("#")) continue;

        QStringList parts = line.split("=");
        if (parts.length() == 2) {
            env.insert(parts[0].trimmed(), parts[1].trimmed());
        }
    }
    file.close();
}

MainWindow::~MainWindow()
{
    // 타이머 정지
    if(fpsTimer && fpsTimer->isActive()) fpsTimer->stop();

    // VLC 플레이어 정리
    for(auto* p : liveVlcPlayers) {
        if(p) {
            libvlc_media_player_stop(p);
            libvlc_media_player_release(p);
        }
    }
    // VLC 인스턴스 정리
    if(vlcInstance) {
        libvlc_release(vlcInstance);
    }

    delete ui;
}

// ---------------------------------------------------------
// [Tab 2] 탭 변경 시 라이브 뷰 제어 (VLC 로직)
// ---------------------------------------------------------
void MainWindow::onLiveTabChanged(int index) {
    // index 1: Live View
    if (index == 1) {
        if (!vlcInstance) return;

        qDebug() << "Start Live Streaming via libvlc (TCP Forced)...";
        QString serverIp = env.value("RTSP_IP");
        QString rtspPort = env.value("RTSP_PORT");

        for(int i = 0; i < 4; i++) {
            // 이미 플레이어가 있다면 정리
            if (liveVlcPlayers[i]) {
                libvlc_media_player_stop(liveVlcPlayers[i]);
                libvlc_media_player_release(liveVlcPlayers[i]);
                liveVlcPlayers[i] = nullptr;
            }

            // RTSP 주소 (이제 ?tcp 같은 옵션 필요 없음, libvlc가 알아서 함)
            QString urlStr = QString("rtsp://%1:%2/%3").arg(serverIp, rtspPort, QString::number(i));
            QByteArray byteArray = urlStr.toUtf8();
            const char* url = byteArray.constData();

            // 미디어 생성
            libvlc_media_t *media = libvlc_media_new_location(vlcInstance, url);

            // RTSP TCP 강제 설정
            libvlc_media_add_option(media, ":rtsp-tcp");

            // 플레이어 생성
            liveVlcPlayers[i] = libvlc_media_player_new_from_media(media);
            libvlc_media_release(media); // 미디어는 플레이어에 등록됐으니 해제

            // VLC OSD(Marquee) 설정 - 텍스트 오버레이 활성화
            // ---------------------------------------------------
            // libvlc_Marquee... -> libvlc_marquee_... (Snake Case로 변경)
            libvlc_video_set_marquee_int(liveVlcPlayers[i], libvlc_marquee_Enable, 1);       // 활성화
            libvlc_video_set_marquee_int(liveVlcPlayers[i], libvlc_marquee_Size, 24);        // 폰트 크기
            libvlc_video_set_marquee_int(liveVlcPlayers[i], libvlc_marquee_Position, 6);     // 위치 (6=TopRight)
            libvlc_video_set_marquee_int(liveVlcPlayers[i], libvlc_marquee_Color, 0x00FF00); // 색상 (초록색)
            libvlc_video_set_marquee_int(liveVlcPlayers[i], libvlc_marquee_Timeout, 0);      // 무제한 표시
            libvlc_video_set_marquee_int(liveVlcPlayers[i], libvlc_marquee_Opacity, 255);    // 불투명
            // ---------------------------------------------------

// 윈도우 핸들 연결 (Qt 위젯 <-> VLC)
#if defined(Q_OS_WIN)
            libvlc_media_player_set_hwnd(liveVlcPlayers[i], (void*)liveVideoWidgets[i]->winId());
#elif defined(Q_OS_LINUX)
            libvlc_media_player_set_xwindow(liveVlcPlayers[i], liveVideoWidgets[i]->winId());
#endif

            // 재생
            libvlc_media_player_play(liveVlcPlayers[i]);
            qDebug() << "Playing Camera" << i << "with libvlc";
        }

        //  FPS 갱신 타이머 시작 (1초마다)
        fpsTimer->start(1000);

    } else {
        // 탭 나가면 타이머 정지
        fpsTimer->stop();

        // 녹화 탭으로 돌아오면 라이브 정지 (리소스 절약)
        qDebug() << "Stop Live Streaming...";
        for(int i = 0; i < 4; i++) {
            if (liveVlcPlayers[i]) {
                libvlc_media_player_stop(liveVlcPlayers[i]);
                libvlc_media_player_release(liveVlcPlayers[i]);
                liveVlcPlayers[i] = nullptr;
            }
        }
    }
}

// 실시간 FPS 갱신 함수
void MainWindow::updateLiveFps() {
    for (int i = 0; i < 4; i++) {
        // 플레이어가 존재하고 재생 중일 때만 확인
        if (liveVlcPlayers[i] && libvlc_media_player_is_playing(liveVlcPlayers[i])) {

            // 현재 FPS 가져오기
            float fps = libvlc_media_player_get_fps(liveVlcPlayers[i]);

            // 텍스트 포맷팅 (FPS가 0일 땐 로딩중 표시)
            QString fpsText;
            if (fps > 0.0f) {
                fpsText = QString::asprintf("FPS: %.1f", fps);
            } else {
                fpsText = "FPS: --";
            }

            // Marquee 텍스트 업데이트
            QByteArray ba = fpsText.toUtf8();
            // libvlc_MarqueeText -> libvlc_marquee_Text
            libvlc_video_set_marquee_string(liveVlcPlayers[i], libvlc_marquee_Text, ba.constData());
        }
    }
}

// ---------------------------------------------------------
// [Tab 1] 기존 녹화 탭 기능들 (중요: 시그널 연결 방식 개선)
// ---------------------------------------------------------
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

    // reply 객체에 직접 시그널을 연결하여 중복 호출 방지
    QNetworkReply *reply = networkManager->post(request, doc.toJson());
    connect(reply, &QNetworkReply::finished, this, [=](){
        onLoginReply(reply);
    });
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

    // 버튼을 여러 번 눌러도 시그널이 꼬이지 않도록 reply에 직접 연결
    QNetworkReply *reply = networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [=](){
        onListReply(reply);
    });
}

void MainWindow::onListReply(QNetworkReply *reply) {
    // 디버깅을 위한 로그 출력
    qDebug() << "--- [DEBUG] List Reply ---";
    qDebug() << "URL:" << reply->request().url().toString();

    if (reply->error() == QNetworkReply::NoError) {
        // 데이터를 한 번만 읽어서 변수에 저장 (두 번 읽으면 비어있음)
        QByteArray responseData = reply->readAll();
        qDebug() << "Data:" << responseData;

        QJsonDocument doc = QJsonDocument::fromJson(responseData);

        if (doc.isNull()) {
            qDebug() << "[Error] JSON Parsing Failed!";
            return;
        }

        QJsonObject rootObj = doc.object();
        // 서버 응답 키 확인 ("files")
        if (rootObj.contains("files")) {
            QJsonArray fileArray = rootObj["files"].toArray();
            fileListWidget->clear();

            for (const QJsonValue &value : fileArray) {
                fileListWidget->addItem(value.toObject()["name"].toString());
            }
            qDebug() << "Files Loaded:" << fileArray.count();
        } else {
            qDebug() << "[Error] Key 'files' not found in JSON";
        }
    } else {
        QMessageBox::warning(this, "Error", "목록 불러오기 실패:\n" + reply->errorString());
    }
    reply->deleteLater();
}

void MainWindow::onFileDoubleClicked(QListWidgetItem *item) {
    // 서버의 GET /stream?file=... 형식에 맞춤
    QString streamUrlStr = QString("%1/stream?file=%2").arg(serverUrl, item->text());
    qDebug() << "Playing URL:" << streamUrlStr;
    player->setSource(QUrl(streamUrlStr));
    player->play();
}

void MainWindow::onDeleteClicked() {
    QListWidgetItem *item = fileListWidget->currentItem();
    if (!item) return;
    if (QMessageBox::question(this, "Confirm", "삭제하시겠습니까?", QMessageBox::Yes | QMessageBox::No) == QMessageBox::No) return;

    // 서버의 DELETE /recordings?file=... 형식에 맞춤
    QUrl url(serverUrl + "/recordings?file=" + item->text());
    QNetworkRequest request(url);

    // DELETE 요청 전송 및 시그널 연결
    QNetworkReply *reply = networkManager->deleteResource(request);
    connect(reply, &QNetworkReply::finished, this, [=](){
        onDeleteReply(reply);
    });
}

void MainWindow::onDeleteReply(QNetworkReply *reply) {
    if (reply->error() == QNetworkReply::NoError) {
        QMessageBox::information(this, "Deleted", "삭제 성공");
        onRefreshClicked(); // 목록 새로고침
        player->stop();
        seekSlider->setValue(0);
    } else {
        QMessageBox::warning(this, "Error", "삭제 실패:\n" + reply->errorString());
    }
    reply->deleteLater();
}
