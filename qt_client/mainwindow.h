#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QVideoWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QTabWidget>

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
    // API 통신 관련 슬롯
    void onLoginClicked();      // 로그인 버튼
    void onRefreshClicked();    // 목록 갱신 버튼
    void onDeleteClicked();     // 파일 삭제 버튼
    void onFileDoubleClicked(QListWidgetItem *item); // 파일 재생

    // 녹화 영상 재생 제어
    void onPlayPauseClicked();  // 재생/일시정지 토글

    // 탭 변경 이벤트 (라이브 영상 제어용)
    void onLiveTabChanged(int index);

    // 네트워크 응답 처리
    void onLoginReply(QNetworkReply *reply);
    void onListReply(QNetworkReply *reply);
    void onDeleteReply(QNetworkReply *reply); // 삭제 응답 처리

private:
    Ui::MainWindow *ui;

    // -----------------------------------------------------------
    // UI 구조 객체 (탭으로 분리)
    // -----------------------------------------------------------
    QTabWidget *mainTabWidget;
    QWidget *recordTab; // [Tab 1] 녹화 목록 및 재생
    QWidget *liveTab;   // [Tab 2] 실시간 라이브 뷰

    // -----------------------------------------------------------
    // [Tab 1] 녹화 기능 객체
    // -----------------------------------------------------------
    QMediaPlayer *player;
    QAudioOutput *audioOutput;
    QVideoWidget *videoWidget;
    QSlider *seekSlider;
    QPushButton *btnPlayPause; // 재생/일시정지 버튼

    // 네트워크 객체
    QNetworkAccessManager *networkManager;
    QString serverUrl; // 백엔드 주소 저장

    // UI 객체 (코드로 생성)
    QLineEdit *idInput;
    QLineEdit *pwInput;
    QListWidget *fileListWidget;
    QPushButton *btnLogin;
    QPushButton *btnRefresh;
    QPushButton *btnDelete;

    // -----------------------------------------------------------
    // [Tab 2] 라이브 기능 객체 (4채널)
    // -----------------------------------------------------------
    // 4개의 플레이어와 비디오 화면을 리스트로 관리
    QList<QMediaPlayer*> livePlayers;
    QList<QAudioOutput*> liveAudios;
    QList<QVideoWidget*> liveVideoWidgets;
};

#endif // MAINWINDOW_H
