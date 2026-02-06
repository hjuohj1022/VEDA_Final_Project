#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMessageBox>
#include "mainwindow.h"
#include "./ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // ==========================================
    // 서버 주소 설정 (Crow Server IP)
    // ==========================================
    serverUrl = "http://192.168.50.204:8080";

    // 네트워크 매니저 생성
    networkManager = new QNetworkAccessManager(this);

    // ==========================================
    // 메인 UI 구성 (탭 위젯 도입)
    // ==========================================
    QWidget *centralWidget = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);

    // 탭 위젯 생성
    mainTabWidget = new QTabWidget(this);
    mainLayout->addWidget(mainTabWidget);
    setCentralWidget(centralWidget);

    // 탭 변경 시 이벤트 연결 (라이브 영상 재생/정지 제어용)
    connect(mainTabWidget, &QTabWidget::currentChanged, this, &MainWindow::onLiveTabChanged);

    // ============================================================
    // [Tab 1] 녹화 목록 및 재생 (기존 기능)
    // ============================================================
    recordTab = new QWidget();
    QHBoxLayout *recordLayout = new QHBoxLayout(recordTab);

    // -----------------------------------------------------------
    // [왼쪽 패널] 로그인 + 파일 목록 + 삭제 버튼
    // -----------------------------------------------------------
    QVBoxLayout *controlLayout = new QVBoxLayout();

    idInput = new QLineEdit(this);
    idInput->setPlaceholderText("ID");

    pwInput = new QLineEdit(this);
    pwInput->setPlaceholderText("Password");
    pwInput->setEchoMode(QLineEdit::Password);

    btnLogin = new QPushButton("Login", this);

    btnRefresh = new QPushButton("Refresh List", this);
    btnRefresh->setEnabled(false); // 로그인 전엔 비활성화

    // 삭제 버튼 생성
    btnDelete = new QPushButton("Delete Selected", this);
    btnDelete->setEnabled(false); // 로그인 전엔 비활성화

    fileListWidget = new QListWidget(this);

    controlLayout->addWidget(idInput);
    controlLayout->addWidget(pwInput);
    controlLayout->addWidget(btnLogin);
    controlLayout->addSpacing(10);
    controlLayout->addWidget(btnRefresh);
    controlLayout->addWidget(btnDelete);
    controlLayout->addWidget(fileListWidget);

    // -----------------------------------------------------------
    // [오른쪽 패널] 비디오 화면 + 탐색 슬라이더 + 재생 버튼
    // -----------------------------------------------------------
    QVBoxLayout *rightLayout = new QVBoxLayout();

    // 비디오 위젯 생성
    videoWidget = new QVideoWidget(this);
    videoWidget->setMinimumSize(640, 480);
    videoWidget->setStyleSheet("background-color: black;");

    // 탐색 슬라이더 생성
    seekSlider = new QSlider(Qt::Horizontal, this);
    seekSlider->setRange(0, 0); // 아직 영상 로드 전이므로 0

    // 재생/일시정지 버튼 생성
    btnPlayPause = new QPushButton("Play", this);

    // 오른쪽 레이아웃에 순서대로 추가
    rightLayout->addWidget(videoWidget, 1); // 비디오가 공간 대부분 차지(비율 1)
    rightLayout->addWidget(seekSlider);     // 슬라이더 추가
    rightLayout->addWidget(btnPlayPause);   // 일시정지 버튼 추가

    // 녹화 탭 레이아웃 배치 (왼쪽 1 : 오른쪽 3 비율)
    recordLayout->addLayout(controlLayout, 1);
    recordLayout->addLayout(rightLayout, 3);

    // 탭에 추가
    mainTabWidget->addTab(recordTab, "Recordings");

    // 녹화용 미디어 플레이어 설정 (Qt6 방식)
    player = new QMediaPlayer(this);
    audioOutput = new QAudioOutput(this);
    player->setAudioOutput(audioOutput);
    player->setVideoOutput(videoWidget);
    audioOutput->setVolume(0.5f);

    // -----------------------------------------------------------
    // [Tab 1] 이벤트 연결 (Signal & Slot)
    // -----------------------------------------------------------
    connect(btnLogin, &QPushButton::clicked, this, &MainWindow::onLoginClicked);
    connect(btnRefresh, &QPushButton::clicked, this, &MainWindow::onRefreshClicked);
    connect(btnDelete, &QPushButton::clicked, this, &MainWindow::onDeleteClicked);
    connect(fileListWidget, &QListWidget::itemDoubleClicked, this, &MainWindow::onFileDoubleClicked);
    connect(btnPlayPause, &QPushButton::clicked, this, &MainWindow::onPlayPauseClicked); // [추가] 재생 버튼 연결

    // 슬라이더와 플레이어 연동
    connect(player, &QMediaPlayer::durationChanged, this, [&](qint64 duration) {
        seekSlider->setRange(0, duration);
    });

    connect(player, &QMediaPlayer::positionChanged, this, [&](qint64 position) {
        if (!seekSlider->isSliderDown()) {
            seekSlider->setValue(position);
        }
    });

    connect(seekSlider, &QSlider::sliderMoved, this, [&](int position) {
        player->setPosition(position);
    });

    // 플레이어 상태가 변하면 버튼 글씨 변경 (Play <-> Pause)
    connect(player, &QMediaPlayer::playbackStateChanged, this, [&](QMediaPlayer::PlaybackState state){
        if (state == QMediaPlayer::PlayingState)
            btnPlayPause->setText("Pause");
        else
            btnPlayPause->setText("Play");
    });

    // ============================================================
    // [Tab 2] 실시간 라이브 (4채널 Grid 구성)
    // ============================================================
    liveTab = new QWidget();
    QGridLayout *liveGridLayout = new QGridLayout(liveTab);
    liveGridLayout->setSpacing(2); // 영상 간 간격

    // 4개의 플레이어를 생성하여 배치
    for(int i = 0; i < 4; i++) {
        QVideoWidget *vid = new QVideoWidget(this);
        vid->setStyleSheet("border: 1px solid gray; background-color: #222;"); // 테두리 추가

        QMediaPlayer *ply = new QMediaPlayer(this);
        QAudioOutput *aud = new QAudioOutput(this);

        ply->setAudioOutput(aud);
        ply->setVideoOutput(vid);
        aud->setVolume(0.0f); // 라이브 소리는 끔 (4개가 섞이면 시끄러움)

        // 리스트에 보관 (나중에 제어하기 위해)
        liveVideoWidgets.append(vid);
        livePlayers.append(ply);
        liveAudios.append(aud);

        // Grid 배치 (2행 2열) -> (0,0) (0,1) (1,0) (1,1)
        liveGridLayout->addWidget(vid, i / 2, i % 2);
    }

    // 탭에 추가
    mainTabWidget->addTab(liveTab, "Live View (4CH)");
}

MainWindow::~MainWindow()
{
    delete ui;
}

// ---------------------------------------------------------
// 재생 / 일시정지 토글
// ---------------------------------------------------------
void MainWindow::onPlayPauseClicked() {
    if (player->playbackState() == QMediaPlayer::PlayingState) {
        player->pause();
    } else {
        player->play();
    }
}

// ---------------------------------------------------------
// 탭 변경 시 라이브 뷰 제어
// ---------------------------------------------------------
void MainWindow::onLiveTabChanged(int index) {
    // index 0: Recordings, index 1: Live View
    if (index == 1) {
        qDebug() << "Start Live Streaming...";

        // RTSP 주소 연결 (인증 정보: admin / team6cam!)
        // 포맷: rtsp://admin:team6cam!@192.168.50.5/<채널>/H.264/media.smp
        for(int i = 0; i < 4; i++) {
            QString urlStr = QString("rtsp://admin:team6cam!@192.168.50.5/%1/H.264/media.smp").arg(i);
            livePlayers[i]->setSource(QUrl(urlStr));
            livePlayers[i]->play();
        }
    } else {
        // 녹화 탭으로 돌아오면 라이브는 정지 (리소스 절약)
        qDebug() << "Stop Live Streaming...";
        for(auto* livePlayer : livePlayers) {
            livePlayer->stop();
        }
    }
}

// ---------------------------------------------------------
// [기능 1] 로그인 요청 (POST /login)
// ---------------------------------------------------------
void MainWindow::onLoginClicked()
{
    QUrl url(serverUrl + "/login");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    // JSON 데이터 만들기
    QJsonObject json;
    json["id"] = idInput->text();
    json["password"] = pwInput->text();
    QJsonDocument doc(json);
    QByteArray data = doc.toJson();

    // 서버로 전송
    QNetworkReply *reply = networkManager->post(request, data);

    // 응답이 오면 처리
    connect(reply, &QNetworkReply::finished, [this, reply](){
        onLoginReply(reply);
    });
}

void MainWindow::onLoginReply(QNetworkReply *reply)
{
    if (reply->error() == QNetworkReply::NoError) {
        QMessageBox::information(this, "Success", "로그인 성공!");
        btnRefresh->setEnabled(true); // 목록 버튼 활성화
        btnDelete->setEnabled(true);  // 삭제 버튼 활성화
        onRefreshClicked();           // 자동으로 목록 불러오기
    } else {
        QMessageBox::warning(this, "Error", "로그인 실패: " + reply->errorString());
    }
    reply->deleteLater();
}

// ---------------------------------------------------------
// [기능 2] 녹화 목록 조회 (GET /recordings)
// ---------------------------------------------------------
void MainWindow::onRefreshClicked()
{
    QUrl url(serverUrl + "/recordings");
    QNetworkRequest request(url);

    QNetworkReply *reply = networkManager->get(request);

    connect(reply, &QNetworkReply::finished, [this, reply](){
        onListReply(reply);
    });
}

void MainWindow::onListReply(QNetworkReply *reply)
{
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray response = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(response);
        QJsonObject obj = doc.object();

        // "files" 배열 파싱
        QJsonArray fileArray = obj["files"].toArray();

        fileListWidget->clear();
        for (const QJsonValue &value : fileArray) {
            QJsonObject fileObj = value.toObject();
            QString fileName = fileObj["name"].toString();
            fileListWidget->addItem(fileName);
        }
    } else {
        QMessageBox::warning(this, "Error", "목록 불러오기 실패: " + reply->errorString());
    }
    reply->deleteLater();
}

// ---------------------------------------------------------
// [기능 3] 영상 스트리밍 (GET /stream?file=...)
// ---------------------------------------------------------
void MainWindow::onFileDoubleClicked(QListWidgetItem *item)
{
    QString fileName = item->text();

    // 스트리밍 URL 완성 (예: http://.../stream?file=cam1.mp4)
    QString streamUrlStr = QString("%1/stream?file=%2").arg(serverUrl, fileName);
    QUrl streamUrl(streamUrlStr);

    qDebug() << "Streaming URL:" << streamUrl;

    player->setSource(streamUrl);
    player->play();
}

// ---------------------------------------------------------
// [기능 4] 파일 삭제 요청 (DELETE /recordings?file=...)
// ---------------------------------------------------------
void MainWindow::onDeleteClicked()
{
    // 리스트에서 선택된 아이템이 있는지 확인
    QListWidgetItem *item = fileListWidget->currentItem();
    if (!item) {
        QMessageBox::warning(this, "Warning", "삭제할 파일을 선택해주세요.");
        return;
    }

    QString fileName = item->text();

    // 정말 삭제할지 확인
    if (QMessageBox::question(this, "Confirm", fileName + " 파일을 삭제하시겠습니까?",
                              QMessageBox::Yes | QMessageBox::No) == QMessageBox::No) {
        return;
    }

    // URL 생성 (DELETE /recordings?file=파일명)
    QUrl url(serverUrl + "/recordings?file=" + fileName);
    QNetworkRequest request(url);

    // DELETE 요청 전송
    QNetworkReply *reply = networkManager->deleteResource(request);

    connect(reply, &QNetworkReply::finished, [this, reply](){
        onDeleteReply(reply);
    });
}

void MainWindow::onDeleteReply(QNetworkReply *reply)
{
    if (reply->error() == QNetworkReply::NoError) {
        QMessageBox::information(this, "Deleted", "파일이 성공적으로 삭제되었습니다.");
        // 삭제 후 목록 자동 갱신
        onRefreshClicked();

        // (선택사항) 만약 삭제한 파일이 재생 중이었다면 재생 중지
        player->stop();
        seekSlider->setValue(0);

    } else {
        QMessageBox::warning(this, "Error", "삭제 실패: " + reply->errorString());
    }
    reply->deleteLater();
}
