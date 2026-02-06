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
    void onFileDoubleClicked(QListWidgetItem *item); // 파일 재생

    // 네트워크 응답 처리
    void onLoginReply(QNetworkReply *reply);
    void onListReply(QNetworkReply *reply);

private:
    Ui::MainWindow *ui;

    // 미디어 객체
    QMediaPlayer *player;
    QAudioOutput *audioOutput;
    QVideoWidget *videoWidget;

    // 탐색 슬라이더 객체
    QSlider *seekSlider;

    // 네트워크 객체
    QNetworkAccessManager *networkManager;
    QString serverUrl; // 백엔드 주소 저장

    // UI 객체 (코드로 생성)
    QLineEdit *idInput;
    QLineEdit *pwInput;
    QListWidget *fileListWidget;
    QPushButton *btnLogin;
    QPushButton *btnRefresh;
};

#endif // MAINWINDOW_H
