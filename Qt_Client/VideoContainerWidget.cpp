#include "VideoContainerWidget.h"
#include "StyleHelper.h"
#include <QVBoxLayout>
#include <QHBoxLayout>

VideoContainerWidget::VideoContainerWidget(int id, QString location, QWidget *parent) 
    : QFrame(parent), cameraId(id), locationName(location) {
    
    setStyleSheet(StyleHelper::getVideoContainerStyle());
    setupUi();
    updateTheme();
}

void VideoContainerWidget::setupUi() {
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 헤더 오버레이 (프레임 내부의 상단 바 형태로 시뮬레이션)
    QWidget *header = new QWidget(this);
    header->setStyleSheet("background-color: rgba(0, 0, 0, 0.6); border-bottom: 1px solid #27272a; border-top-left-radius: 8px; border-top-right-radius: 8px;");
    header->setFixedHeight(40);
    
    QHBoxLayout *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(10, 0, 10, 0);
    
    QLabel *camIcon = new QLabel("📷", this);
    camIcon->setStyleSheet("background: transparent; border: none; font-size: 14px;");
    
    QVBoxLayout *titleLayout = new QVBoxLayout();
    titleLayout->setSpacing(0);
    titleLayout->setContentsMargins(0, 5, 0, 5);
    
    QLabel *camTitle = new QLabel(QString("Camera %1").arg(cameraId), this);
    camTitle->setStyleSheet("color: white; font-weight: bold; font-size: 12px; background: transparent; border: none;");
    
    QLabel *locTitle = new QLabel(locationName, this);
    locTitle->setStyleSheet("color: #a1a1aa; font-size: 10px; background: transparent; border: none;");
    
    titleLayout->addWidget(camTitle);
    titleLayout->addWidget(locTitle);
    
    statusLabel = new QLabel(" LIVE ", this);
    statusLabel->setStyleSheet("background-color: rgba(249, 115, 22, 0.2); color: #f97316; border: 1px solid #f97316; border-radius: 4px; font-size: 10px; font-weight: bold; padding: 2px;");
    
    headerLayout->addWidget(camIcon);
    headerLayout->addLayout(titleLayout);
    headerLayout->addStretch();
    headerLayout->addWidget(statusLabel);
    
    // 비디오 영역
    videoArea = new QWidget(this);
    videoArea->setStyleSheet("background-color: #000000;"); // VLC가 여기에 렌더링됨
    videoArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // 하단바 (해상도/FPS)
    QWidget *footer = new QWidget(this);
    footer->setStyleSheet("background-color: rgba(0, 0, 0, 0.6); border-top: 1px solid #27272a; border-bottom-left-radius: 8px; border-bottom-right-radius: 8px;");
    footer->setFixedHeight(30);
    
    QHBoxLayout *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(10, 0, 10, 0);
    
    QLabel *recIcon = new QLabel("🔴 REC", this);
    recIcon->setStyleSheet("color: #f97316; font-size: 10px; font-weight: bold; background: transparent; border: none;");
    
    QLabel *resLabel = new QLabel("1920x1080 • 30 FPS", this);
    resLabel->setStyleSheet("color: #71717a; font-size: 10px; background: transparent; border: none;");

    footerLayout->addWidget(recIcon);
    footerLayout->addStretch();
    footerLayout->addWidget(resLabel);

    // 조립
    layout->addWidget(header);
    layout->addWidget(videoArea);
    layout->addWidget(footer);
}

QWidget* VideoContainerWidget::getVideoWidget() const {
    return videoArea;
}

void VideoContainerWidget::setStatus(QString status) {
    statusLabel->setText(status.toUpper());
    if (status.toLower() == "alert") {
         statusLabel->setStyleSheet("background-color: rgba(239, 68, 68, 0.2); color: #ef4444; border: 1px solid #ef4444; border-radius: 4px; font-size: 10px; font-weight: bold; padding: 2px;");
    } else if (status.toLower() == "offline") {
         statusLabel->setStyleSheet("background-color: rgba(113, 113, 122, 0.2); color: #71717a; border: 1px solid #71717a; border-radius: 4px; font-size: 10px; font-weight: bold; padding: 2px;");
    } else {
         statusLabel->setStyleSheet("background-color: rgba(249, 115, 22, 0.2); color: #f97316; border: 1px solid #f97316; border-radius: 4px; font-size: 10px; font-weight: bold; padding: 2px;");
    }
}


void VideoContainerWidget::updateTheme() {
    setStyleSheet(StyleHelper::getVideoContainerStyle());

    // Update Header and Footer backgrounds (they are QWidgets inside QFrame)
    QList<QWidget*> widgets = findChildren<QWidget*>();
    for(QWidget* w : widgets) {
        if(w->maximumHeight() == 40 && w->minimumHeight() == 40) {
             // Header
             w->setStyleSheet(StyleHelper::isDarkMode ? 
                "background-color: rgba(0, 0, 0, 0.6); border-bottom: 1px solid #27272a; border-top-left-radius: 8px; border-top-right-radius: 8px;" :
                "background-color: rgba(255, 255, 255, 0.8); border-bottom: 1px solid #e4e4e7; border-top-left-radius: 8px; border-top-right-radius: 8px;");
        }
        if(w->maximumHeight() == 30 && w->minimumHeight() == 30) {
             // Footer
             w->setStyleSheet(StyleHelper::isDarkMode ? 
                "background-color: rgba(0, 0, 0, 0.6); border-top: 1px solid #27272a; border-bottom-left-radius: 8px; border-bottom-right-radius: 8px;" :
                "background-color: rgba(255, 255, 255, 0.8); border-top: 1px solid #e4e4e7; border-bottom-left-radius: 8px; border-bottom-right-radius: 8px;");
        }
    }

    // Update Labels
    QList<QLabel*> labels = findChildren<QLabel*>();
    for(QLabel* lbl : labels) {
         // Camera Title
         if(lbl->text().startsWith("Camera")) {
              lbl->setStyleSheet(StyleHelper::isDarkMode ? 
                  "color: white; font-weight: bold; font-size: 12px; background: transparent; border: none;" :
                  "color: #18181b; font-weight: bold; font-size: 12px; background: transparent; border: none;");
         }
         // Cam Icon
         if(lbl->text() == "📷") {
              lbl->setStyleSheet("background: transparent; border: none; font-size: 14px;");
         }
         // Location
         if(lbl->text() == locationName) {
              lbl->setStyleSheet(StyleHelper::isDarkMode ? 
                  "color: #a1a1aa; font-size: 10px; background: transparent; border: none;" :
                  "color: #52525b; font-size: 10px; background: transparent; border: none;");
         }
         // Res/FPS
         if(lbl->text().contains("FPS")) {
              lbl->setStyleSheet(StyleHelper::isDarkMode ? 
                  "color: #71717a; font-size: 10px; background: transparent; border: none;" :
                  "color: #a1a1aa; font-size: 10px; background: transparent; border: none;");
         }
    }
}
