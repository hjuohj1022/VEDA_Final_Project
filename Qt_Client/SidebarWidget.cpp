#include "SidebarWidget.h"
#include "StyleHelper.h"
#include <QPushButton>
#include <QRandomGenerator>
#include <QGridLayout>

SidebarWidget::SidebarWidget(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName("SidebarWidget");
    setStyleSheet(StyleHelper::getSidebarStyle());
    setFixedWidth(250);
    
    // Initialize pointers to nullptr for safety
    fpsBar = nullptr;
    fpsValueLabel = nullptr;
    latencyBar = nullptr;
    latencyValueLabel = nullptr;
    activeCamValue = nullptr;
    activeCamSub = nullptr;
    storageValue = nullptr;
    storageSub = nullptr;

    storageSub = nullptr;
    
    setupUi();
    updateTheme(); // Apply initial theme styles dynamically
    
    updateTimer = new QTimer(this);
    connect(updateTimer, &QTimer::timeout, this, [=]() {
        // "Detected" 지표는 시뮬레이션 유지 (백엔드 AI 없음)
        // 다른 지표들은 MainWindow에서 실제 데이터로 업데이트됨
    });
    updateTimer->start(2000);
}

void SidebarWidget::updateFps(int fps) {
    if(fpsBar) fpsBar->setValue(fps);
    if(fpsValueLabel) fpsValueLabel->setText(QString("%1 FPS").arg(fps));
}

void SidebarWidget::updateLatency(int ms) {
    if(latencyBar) latencyBar->setValue(ms);
    if(latencyValueLabel) latencyValueLabel->setText(QString("%1 ms").arg(ms));
}

void SidebarWidget::updateActiveCameras(int count, int total) {
    if(activeCamValue) activeCamValue->setText(QString::number(count));
    if(activeCamSub) activeCamSub->setText(QString("of %1 total").arg(total));
}

void SidebarWidget::updateStorage(QString used, QString total, int percent) {
    if(storageValue) storageValue->setText(percent > 0 ? QString("%1%").arg(percent) : "N/A");
    if(storageSub) storageSub->setText(QString("%1 / %2").arg(used, total));
}

void SidebarWidget::setupUi() {
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(15, 15, 15, 15);
    layout->setSpacing(15);

    // 제목
    QLabel *title = new QLabel("System Metrics", this);
    title->setObjectName("Title");
    layout->addWidget(title);

    // 지표 1: FPS
    {
        QWidget *widget = new QWidget(this);
        QVBoxLayout *wLayout = new QVBoxLayout(widget);
        wLayout->setContentsMargins(0,0,0,0); wLayout->setSpacing(5);
        QHBoxLayout *header = new QHBoxLayout();
        QLabel *lblTitle = new QLabel("Frame Rate");
        fpsValueLabel = new QLabel("0 FPS");
        fpsValueLabel->setStyleSheet("color: #f97316; font-weight: bold;");
        header->addWidget(lblTitle); header->addStretch(); header->addWidget(fpsValueLabel);
        fpsBar = new QProgressBar();
        fpsBar->setRange(0, 60);
        fpsBar->setValue(0);
        fpsBar->setTextVisible(false);
        fpsBar->setFixedHeight(4);
        wLayout->addLayout(header); wLayout->addWidget(fpsBar);
        layout->addWidget(widget);
    }

    // 지표 2: Latency
    {
        QWidget *widget = new QWidget(this);
        QVBoxLayout *wLayout = new QVBoxLayout(widget);
        wLayout->setContentsMargins(0,0,0,0); wLayout->setSpacing(5);
        QHBoxLayout *header = new QHBoxLayout();
        QLabel *lblTitle = new QLabel("Network Latency");
        latencyValueLabel = new QLabel("0 ms");
        latencyValueLabel->setStyleSheet("color: #f97316; font-weight: bold;");
        header->addWidget(lblTitle); header->addStretch(); header->addWidget(latencyValueLabel);
        latencyBar = new QProgressBar();
        latencyBar->setRange(0, 200);   
        latencyBar->setValue(0);
        latencyBar->setTextVisible(false);
        latencyBar->setFixedHeight(4);
        wLayout->addLayout(header); wLayout->addWidget(latencyBar);
        layout->addWidget(widget);
    }

    layout->addSpacing(5);

    // 개요 제목
    QLabel *overviewTitle = new QLabel("System Overview", this);
    overviewTitle->setObjectName("Title");
    overviewTitle->setStyleSheet("font-size: 14px;");
    layout->addWidget(overviewTitle);

    // 정보 카드 (Grid Layout)
    QGridLayout *grid = new QGridLayout();
    grid->setSpacing(10);
    grid->setContentsMargins(0, 0, 0, 0);

    // Active Cameras
    {
        QWidget *widget = new QWidget(this);
        widget->setObjectName("MetricWidget");
        QVBoxLayout *wLayout = new QVBoxLayout(widget);
        wLayout->setContentsMargins(10, 10, 10, 10); wLayout->setSpacing(5);
        QHBoxLayout *top = new QHBoxLayout();
        QLabel *icon = new QLabel("🎥"); icon->setObjectName("IconLabel");
        QLabel *lbl = new QLabel("Active Cameras"); lbl->setStyleSheet("font-size: 10px; color: #a1a1aa; font-weight: bold;");
        top->addWidget(icon); top->addWidget(lbl); top->addStretch();
        activeCamValue = new QLabel("0");
        activeCamValue->setObjectName("ValueLabel");
        activeCamSub = new QLabel("of 4 total");
        activeCamSub->setStyleSheet("font-size: 9px; color: #52525b; background: transparent; border: none;");
        wLayout->addLayout(top); wLayout->addWidget(activeCamValue); wLayout->addWidget(activeCamSub);
        grid->addWidget(widget, 0, 0);
    }

    // Detected (Simulated)
    grid->addWidget(createInfoCard("Detected", "12", "+3/min", "👥"), 0, 1);

    // Storage
    {
        QWidget *widget = new QWidget(this);
        widget->setObjectName("MetricWidget"); // Tag for styling
        QVBoxLayout *wLayout = new QVBoxLayout(widget);
        wLayout->setContentsMargins(10, 10, 10, 10); wLayout->setSpacing(5);
        QHBoxLayout *top = new QHBoxLayout();
        QLabel *icon = new QLabel("💾"); icon->setObjectName("IconLabel");
        QLabel *lbl = new QLabel("Storage"); lbl->setStyleSheet("font-size: 10px; color: #a1a1aa; font-weight: bold;");
        top->addWidget(icon); top->addWidget(lbl); top->addStretch();
        storageValue = new QLabel("0%");
        storageValue->setObjectName("ValueLabel");
        storageSub = new QLabel("Calculating...");
        storageSub->setStyleSheet("font-size: 9px; color: #52525b; background: transparent; border: none;");
        wLayout->addLayout(top); wLayout->addWidget(storageValue); wLayout->addWidget(storageSub);
        grid->addWidget(widget, 1, 0);
    }
    
    // Network (Simulated)
    grid->addWidget(createInfoCard("Network", "Good", "Connected", "📶"), 1, 1);

    layout->addLayout(grid);
    
    layout->addStretch();
    
    // AI Status Box
    QWidget *aiStatus = new QWidget(this);
    aiStatus->setObjectName("AiStatusBox");
    // Style handled in updateTheme()
    QVBoxLayout *aiLayout = new QVBoxLayout(aiStatus);
    aiLayout->setContentsMargins(10, 10, 10, 10);
    
    QHBoxLayout *aiHeader = new QHBoxLayout();
    QLabel *dot = new QLabel("●", this);
    dot->setStyleSheet("color: #f97316; font-size: 8px;");
    QLabel *aiTitle = new QLabel("AI STATUS", this);
    aiTitle->setStyleSheet("color: #f97316; font-weight: bold; font-size: 10px;");
    aiHeader->addWidget(dot);
    aiHeader->addWidget(aiTitle);
    aiHeader->addStretch();
    
    QLabel *aiDesc = new QLabel("Active on all feeds", this);
    aiDesc->setStyleSheet("color: #71717a; font-size: 10px;");
    
    aiLayout->addLayout(aiHeader);
    aiLayout->addWidget(aiDesc);
    layout->addWidget(aiStatus);
    layout->addSpacing(10);

    // 하단 영역 (네비게이션)
    QPushButton *btnRecordings = new QPushButton("📂  Recordings", this);
    // Style handled in updateTheme()
    connect(btnRecordings, &QPushButton::clicked, this, [=]() {
        emit pageChanged(1); // 녹화 목록으로 전환
    });
    layout->addWidget(btnRecordings);

    QPushButton *btnLive = new QPushButton("🔴  Live View", this);
    // Style handled in updateTheme()
    connect(btnLive, &QPushButton::clicked, this, [=]() {
        emit pageChanged(0); // 라이브 뷰로 전환
    });
    layout->addWidget(btnLive);
}

// Helper methods (kept private)
QWidget* SidebarWidget::createMetricWidget(QString name, int value, QString unit) {
    return nullptr; // Not used
}

QWidget* SidebarWidget::createInfoCard(QString title, QString value, QString subtext, QString iconText) {
    QWidget *widget = new QWidget(this);
    widget->setObjectName("InfoCard");
    QVBoxLayout *layout = new QVBoxLayout(widget);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(5);

    QHBoxLayout *topLayout = new QHBoxLayout();
    QLabel *icon = new QLabel(iconText);
    icon->setObjectName("IconLabel");
    
    QLabel *lblTitle = new QLabel(title);
    lblTitle->setStyleSheet("font-size: 10px; color: #a1a1aa; font-weight: bold;");
    
    topLayout->addWidget(icon);
    topLayout->addWidget(lblTitle);
    topLayout->addStretch();
    
    
    QLabel *lblValue = new QLabel(value);
    lblValue->setObjectName("ValueLabel");
    // Style set in updateTheme()
    
    QLabel *lblSub = new QLabel(subtext);
    lblSub->setStyleSheet("font-size: 9px; color: #52525b; background: transparent; border: none;");

    layout->addLayout(topLayout);
    layout->addWidget(lblValue);
    layout->addWidget(lblSub);

    return widget;
}

void SidebarWidget::updateTheme() {
    setStyleSheet(StyleHelper::getSidebarStyle());
    
    // Find all child widgets that need specific styling updates
    // Iterate over children and update if they have specific object names or types
    // Or simply re-apply styles to known pointers if we kept them.
    // However, since we didn't keep pointers to container widgets, we might need to iterate or just rely on cascading.
    // BUT, the container widgets have hardcoded styles in setupUi().
    // We need to fix setupUi to NOT hardcode, OR we need to find them and update them.
    
    // Better approach: Re-apply styles to specific elements if we can find them.
    // Since we didn't store pointers to the container widgets (Metrics, Info Cards), 
    // we should iterate or use object names. 
    // Let's assume we can just find them by type or we need to refactor setupUi to verify.
    // Actually, let's just force update all QWidgets that look like containers.
    
    QList<QWidget*> widgets = findChildren<QWidget*>();
    for(QWidget* w : widgets) {
        // Check if it's a card container
        if (w->objectName() == "MetricWidget" || w->objectName() == "InfoCard") {
             w->setStyleSheet(StyleHelper::isDarkMode ? 
                "background-color: #18181b; border: 1px solid #27272a; border-radius: 8px;" :
                "background-color: #ffffff; border: 1px solid #e4e4e7; border-radius: 8px;");
        }
        // AI Status Box
        if (w->objectName() == "AiStatusBox") {
             w->setStyleSheet(StyleHelper::isDarkMode ? 
                "background-color: #18181b; border: 1px solid rgba(249, 115, 22, 0.3); border-radius: 6px;" :
                "background-color: #ffffff; border: 1px solid rgba(249, 115, 22, 0.3); border-radius: 6px;");
        }
    }
    
    // Update Labels
    QList<QLabel*> labels = findChildren<QLabel*>();
    for(QLabel* lbl : labels) {
        // Icon backgrounds
        if(lbl->objectName() == "IconLabel") {
            lbl->setStyleSheet(StyleHelper::isDarkMode ? 
                "font-size: 14px; background: #27272a; border-radius: 4px; padding: 2px;" :
                "font-size: 14px; background: #f4f4f5; border-radius: 4px; padding: 2px;");
        }
        // Values
        if(lbl->objectName() == "ValueLabel") {
             lbl->setStyleSheet(StyleHelper::isDarkMode ? 
                "background: transparent; border: none; font-size: 16px; font-weight: bold; color: white;" :
                "background: transparent; border: none; font-size: 16px; font-weight: bold; color: #18181b;");
        }
    }
    
    // Update Navigation Buttons
    QList<QPushButton*> btns = findChildren<QPushButton*>();
    for(QPushButton* btn : btns) {
        btn->setStyleSheet(StyleHelper::isDarkMode ? 
            "QPushButton { text-align: left; padding: 10px; background-color: #18181b; color: white; border: 1px solid #27272a; border-radius: 6px; } QPushButton:hover { border: 1px solid #f97316; }" :
            "QPushButton { text-align: left; padding: 10px; background-color: #ffffff; color: #18181b; border: 1px solid #e4e4e7; border-radius: 6px; } QPushButton:hover { border: 1px solid #f97316; }");
    }
}
