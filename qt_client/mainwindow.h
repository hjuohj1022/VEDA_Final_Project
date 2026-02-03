#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QVideoWidget>

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
    void on_btnPlay_clicked(); // 버튼 클릭 이벤트

private:
    Ui::MainWindow *ui;

    // 영상 재생 객체 포인터 선언
    QMediaPlayer *player;
    QAudioOutput *audioOutput;
    QVideoWidget *videoWidget;
};

#endif // MAINWINDOW_H
