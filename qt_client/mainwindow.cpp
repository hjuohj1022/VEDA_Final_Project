#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // 비디오 위젯 생성
    videoWidget = new QVideoWidget(this);

    // 레이아웃 설정
    // 주의: Designer에서 이미 레이아웃을 걸어버렸다면 new QVBoxLayout()에서 경고가 뜰 수 있음
    // 여기서는 Designer에서 레이아웃 없이 버튼만 올려뒀다고 가정

    if (ui->centralwidget->layout() == nullptr) {
        QVBoxLayout *layout = new QVBoxLayout(ui->centralwidget);
        ui->centralwidget->setLayout(layout);
    }

    // 기존 레이아웃 가져오기
    QVBoxLayout *layout = qobject_cast<QVBoxLayout*>(ui->centralwidget->layout());

    if (layout) {
        layout->addWidget(videoWidget); // 비디오를 위에 추가
        layout->addWidget(ui->btnPlay); // 버튼을 아래로 이동 (순서 정리)
    }

    // 미디어 플레이어 초기화
    player = new QMediaPlayer(this);
    audioOutput = new QAudioOutput(this);

    player->setAudioOutput(audioOutput);
    player->setVideoOutput(videoWidget);

    audioOutput->setVolume(50); // 0.0 ~ 1.0 사이가 아니라 0~100 스케일인 경우도 있음 (Qt 버전에 따라 다름, 보통 setVolume은 0.0~1.0 권장이나 Qt6 int 변환됨)
    // Qt 6.x에서는 float (0.0f ~ 1.0f)를 사용, 소리가 너무 작으면 0.5f 등으로 수정하면 됨
    audioOutput->setVolume(0.5f);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::on_btnPlay_clicked()
{
    // 윈도우에서는 http, https 모두 지원
    const QUrl url("http://commondatastorage.googleapis.com/gtv-videos-bucket/sample/BigBuckBunny.mp4");

    player->setSource(url);

    // 디버깅용: 상태가 변할 때 출력
    connect(player, &QMediaPlayer::mediaStatusChanged, [&](QMediaPlayer::MediaStatus status){
        if(status == QMediaPlayer::LoadedMedia) {
            player->play();
        }
    });

    player->play();
}
