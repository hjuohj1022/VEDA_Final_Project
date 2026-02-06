#include <QVBoxLayout>
#include <QHBoxLayout>
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

    // UI 화면 구성 (코드로 레이아웃 생성)
    QWidget *centralWidget = new QWidget(this);
    QHBoxLayout *mainLayout = new QHBoxLayout(centralWidget);

    // -----------------------------------------------------------
    // [왼쪽 패널] 로그인 + 파일 목록
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

    fileListWidget = new QListWidget(this);

    controlLayout->addWidget(idInput);
    controlLayout->addWidget(pwInput);
    controlLayout->addWidget(btnLogin);
    controlLayout->addSpacing(10);
    controlLayout->addWidget(btnRefresh);
    controlLayout->addWidget(fileListWidget);

    // -----------------------------------------------------------
    // [오른쪽 패널] 비디오 화면 + 탐색 슬라이더
    // -----------------------------------------------------------
    QVBoxLayout *rightLayout = new QVBoxLayout();

    // 비디오 위젯 생성
    videoWidget = new QVideoWidget(this);
    videoWidget->setMinimumSize(640, 480);
    videoWidget->setStyleSheet("background-color: black;");

    // 탐색 슬라이더 생성
    seekSlider = new QSlider(Qt::Horizontal, this);
    seekSlider->setRange(0, 0); // 아직 영상 로드 전이므로 0

    // 오른쪽 레이아웃에 순서대로 추가 (비디오 위, 슬라이더 아래)
    rightLayout->addWidget(videoWidget, 1); // 비디오가 공간 대부분 차지(비율 1)
    rightLayout->addWidget(seekSlider);     // 슬라이더 추가

    // 레이아웃 배치 (왼쪽 1 : 오른쪽 3 비율)
    mainLayout->addLayout(controlLayout, 1);
    mainLayout->addLayout(rightLayout, 3);

    setCentralWidget(centralWidget);

    // 미디어 플레이어 설정 (Qt6 방식)
    player = new QMediaPlayer(this);
    audioOutput = new QAudioOutput(this);

    player->setAudioOutput(audioOutput);
    player->setVideoOutput(videoWidget);
    audioOutput->setVolume(0.5f);

    // -----------------------------------------------------------
    // 이벤트 연결 (Signal & Slot)
    // -----------------------------------------------------------
    connect(btnLogin, &QPushButton::clicked, this, &MainWindow::onLoginClicked);
    connect(btnRefresh, &QPushButton::clicked, this, &MainWindow::onRefreshClicked);
    connect(fileListWidget, &QListWidget::itemDoubleClicked, this, &MainWindow::onFileDoubleClicked);

    // 슬라이더와 플레이어 연동
    // 영상 길이가 인식되면 슬라이더 범위 설정 (0 ~ 영상길이)
    connect(player, &QMediaPlayer::durationChanged, this, [&](qint64 duration) {
        seekSlider->setRange(0, duration);
    });

    // 영상 재생 중 현재 위치에 맞춰 슬라이더 이동
    connect(player, &QMediaPlayer::positionChanged, this, [&](qint64 position) {
        // 사용자가 슬라이더를 잡고 드래그 중일 땐, 코드가 강제로 옮기지 않도록 함
        if (!seekSlider->isSliderDown()) {
            seekSlider->setValue(position);
        }
    });

    // 사용자가 슬라이더를 움직이면 해당 위치로 영상 점프
    connect(seekSlider, &QSlider::sliderMoved, this, [&](int position) {
        player->setPosition(position);
    });
}

MainWindow::~MainWindow()
{
    delete ui;
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
