#include "HeaderWidget.h"
#include "StyleHelper.h"
#include <QHBoxLayout>
#include <QIcon>

HeaderWidget::HeaderWidget(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName("HeaderWidget");
    setFixedHeight(50); // height 60 -> 50 for more compact look
    setupUi();
    updateTheme();
}

void HeaderWidget::setupUi() {
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(15, 5, 15, 5); // Reduced margins
    layout->setSpacing(10);

    // 제목 영역
    QLabel *icon = new QLabel("🛡️", this); // 방패 아이콘 (이미지 대신 텍스트)
    icon->setStyleSheet("font-size: 20px; background: transparent; border: none;"); // 테두리 제거
    
    QVBoxLayout *titleLayout = new QVBoxLayout();
    titleLayout->setSpacing(0);
    QLabel *title = new QLabel("Vision VMS", this);
    title->setObjectName("AppTitle");
    QLabel *subtitle = new QLabel("AI Surveillance", this); // Shortened subtitle
    subtitle->setObjectName("AppSubtitle");
    
    titleLayout->addWidget(title);
    titleLayout->addWidget(subtitle);

    layout->addWidget(icon);
    layout->addLayout(titleLayout);

    layout->addStretch();

    // 검색창
    QWidget *searchContainer = new QWidget(this);
    searchContainer->setObjectName("SearchContainer");
    searchContainer->setFixedWidth(250); // Slightly smaller
    
    QHBoxLayout *searchLayout = new QHBoxLayout(searchContainer);
    searchLayout->setContentsMargins(8, 4, 8, 4);
    
    QLabel *searchIcon = new QLabel("🔍", this);
    searchIcon->setObjectName("SearchIcon");
    
    QLineEdit *searchInput = new QLineEdit(this);
    searchInput->setPlaceholderText("Search...");
    // Style set in updateTheme()
    
    searchLayout->addWidget(searchIcon);
    searchLayout->addWidget(searchInput);
    
    layout->addWidget(searchContainer);

    layout->addSpacing(15);

    // 우측 동작 버튼
    // Styles handled in updateTheme()
    
    QPushButton *btnNotif = new QPushButton("🔔", this);
    // Style set in updateTheme()
    
    QPushButton *btnSettings = new QPushButton("⚙️", this);
    // Style set in updateTheme()
    
    btnTheme = new QPushButton("🌙", this);
    // Style set in updateTheme()
    connect(btnTheme, &QPushButton::clicked, this, &HeaderWidget::themeToggled);

    // 사용자 프로필
    QWidget *profileBadge = new QWidget(this);
    profileBadge->setObjectName("ProfileBadge");
    profileBadge->setFixedSize(24, 24);
    QLabel *profileLabel = new QLabel("AD", profileBadge);
    profileLabel->setObjectName("ProfileLabel");
    profileLabel->setAlignment(Qt::AlignCenter);
    QHBoxLayout *pLayout = new QHBoxLayout(profileBadge);
    pLayout->setContentsMargins(0,0,0,0);
    pLayout->addWidget(profileLabel);

    layout->addWidget(btnNotif);
    layout->addWidget(btnSettings);
    layout->addWidget(btnTheme);
    layout->addWidget(profileBadge);
    layout->addWidget(profileBadge);
}

void HeaderWidget::updateTheme() {
    // 1. Update Header Container Style
    setStyleSheet(StyleHelper::getHeaderStyle());

    // 2. Update Theme Button Icon
    if(btnTheme) btnTheme->setText(StyleHelper::isDarkMode ? "🌙" : "☀️");

    // 3. Update Child Widgets (Search, Buttons, Profile)
    // Search Container
    QList<QWidget*> widgets = findChildren<QWidget*>();
    for(QWidget* w : widgets) {
        if(w->objectName() == "SearchContainer") {
             w->setStyleSheet(StyleHelper::isDarkMode ? 
                "background-color: #18181b; border: 1px solid #27272a; border-radius: 6px;" :
                "background-color: #f4f4f5; border: 1px solid #e4e4e7; border-radius: 6px;");
        }
        if(w->objectName() == "ProfileBadge") {
             w->setStyleSheet(StyleHelper::isDarkMode ? 
                "background-color: #27272a; border-radius: 12px; border: 1px solid #3f3f46;" :
                "background-color: #f4f4f5; border-radius: 12px; border: 1px solid #e4e4e7;");
        }
    }
    
    // Labels (Title, Subtitle, Search Icon, Profile Label)
    QList<QLabel*> labels = findChildren<QLabel*>();
    for(QLabel* lbl : labels) {
        if(lbl->objectName() == "AppTitle") {
            lbl->setStyleSheet(StyleHelper::isDarkMode ? 
                "font-size: 14px; font-weight: bold; color: white; background: transparent; border: none;" :
                "font-size: 14px; font-weight: bold; color: #18181b; background: transparent; border: none;");
        }
        if(lbl->objectName() == "AppSubtitle") {
            lbl->setStyleSheet("font-size: 9px; color: #71717a; background: transparent; border: none;");
        }
        if(lbl->objectName() == "SearchIcon") {
            lbl->setStyleSheet("color: #71717a; border: none; background: transparent; font-size: 10px;");
        }
        if(lbl->objectName() == "ProfileLabel") {
            lbl->setStyleSheet(StyleHelper::isDarkMode ? 
                "color: white; font-weight: bold; font-size: 9px; border: none; background: transparent;" :
                "color: #18181b; font-weight: bold; font-size: 9px; border: none; background: transparent;");
        }
    }
    
    // LineEdit
    QList<QLineEdit*> edits = findChildren<QLineEdit*>();
    for(QLineEdit* edit : edits) {
         edit->setStyleSheet(StyleHelper::isDarkMode ? 
             "border: none; background: transparent; color: white; font-size: 11px;" :
             "border: none; background: transparent; color: #18181b; font-size: 11px;");
    }

    // Buttons
    QString btnStyle = StyleHelper::isDarkMode ? 
        "QPushButton { color: #a1a1aa; font-size: 14px; border: none; background: transparent; } QPushButton:hover { color: white; background-color: #27272a; border-radius: 4px; }" :
        "QPushButton { color: #71717a; font-size: 14px; border: none; background: transparent; } QPushButton:hover { color: #18181b; background-color: #f4f4f5; border-radius: 4px; }";
        
    QList<QPushButton*> btns = findChildren<QPushButton*>();
    for(QPushButton* btn : btns) {
        btn->setStyleSheet(btnStyle);
    }
}
