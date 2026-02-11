#ifndef STYLEHELPER_H
#define STYLEHELPER_H

#include <QString>

class StyleHelper {
public:
    static bool isDarkMode;

    static void toggleTheme() {
        isDarkMode = !isDarkMode;
    }

    static QString getSidebarStyle() {
        if (isDarkMode) {
            return "QWidget#SidebarWidget { background-color: #09090b; color: #ffffff; border-right: 1px solid #27272a; }"
                   "QLabel { color: #a1a1aa; font-size: 12px; }"
                   "QLabel#Title { color: #ffffff; font-size: 16px; font-weight: bold; }"
                   "QLabel#ValueLabel { color: #ffffff; font-size: 20px; font-weight: bold; }"
                   "QProgressBar { border: none; background-color: #27272a; border-radius: 2px; text-align: center; }"
                   "QProgressBar::chunk { background-color: #f97316; border-radius: 2px; }";
        } else {
            return "QWidget#SidebarWidget { background-color: #ffffff; color: #18181b; border-right: 1px solid #e4e4e7; }"
                   "QLabel { color: #52525b; font-size: 12px; }"
                   "QLabel#Title { color: #18181b; font-size: 16px; font-weight: bold; }"
                   "QLabel#ValueLabel { color: #ea580c; font-size: 20px; font-weight: bold; }"
                   "QProgressBar { border: none; background-color: #e4e4e7; border-radius: 2px; text-align: center; }"
                   "QProgressBar::chunk { background-color: #f97316; border-radius: 2px; }";
        }
    }

    static QString getHeaderStyle() {
        if (isDarkMode) {
            return "QWidget#HeaderWidget { background-color: #09090b; border-bottom: 1px solid #27272a; }"
                   "QLineEdit { background-color: #18181b; border: 1px solid #27272a; border-radius: 6px; color: #e4e4e7; padding: 4px; }"
                   "QPushButton { background-color: transparent; border: none; border-radius: 6px; padding: 6px; }"
                   "QPushButton:hover { background-color: #27272a; }";
        } else {
            return "QWidget#HeaderWidget { background-color: #ffffff; border-bottom: 1px solid #e4e4e7; }"
                   "QLineEdit { background-color: #f4f4f5; border: 1px solid #e4e4e7; border-radius: 6px; color: #18181b; padding: 4px; }"
                   "QPushButton { background-color: transparent; border: none; border-radius: 6px; padding: 6px; }"
                   "QPushButton:hover { background-color: #f4f4f5; }";
        }
    }

    static QString getMainStyle() {
        if (isDarkMode) {
            return "QMainWindow { background-color: #09090b; }"
                   "QWidget#centralwidget, QWidget#RightWidget, QStackedWidget#MainStackedWidget, QWidget#LivePage { background-color: #09090b; }"
                   "QMessageBox { background-color: #18181b; color: white; }"
                   "QMessageBox QLabel { color: white; }"
                   "QMessageBox QPushButton { background-color: #27272a; color: white; border: 1px solid #3f3f46; border-radius: 4px; padding: 6px 12px; min-width: 60px; }"
                   "QMessageBox QPushButton:hover { background-color: #3f3f46; border-color: #52525b; }"
                   "QMessageBox QPushButton:pressed { background-color: #f97316; border-color: #f97316; color: black; }"
                   "QSlider::groove:horizontal { border: 1px solid #27272a; height: 4px; background: #27272a; margin: 2px 0; border-radius: 2px; }"
                   "QSlider::handle:horizontal { background: #f97316; border: 1px solid #f97316; width: 14px; height: 14px; margin: -6px 0; border-radius: 7px; }"
                   "QSlider::handle:horizontal:hover { background: #fb923c; }"
                   "QScrollBar:vertical { border: none; background: #09090b; width: 10px; margin: 0px; }"
                   "QScrollBar::handle:vertical { background: #27272a; min-height: 20px; border-radius: 5px; }"
                   "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
                   "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }";
        } else {
            return "QMainWindow { background-color: #fafafa; }"
                   "QWidget#centralwidget, QWidget#RightWidget, QStackedWidget#MainStackedWidget, QWidget#LivePage { background-color: #fafafa; }"
                   "QMessageBox { background-color: #ffffff; color: #18181b; }"
                   "QMessageBox QLabel { color: #18181b; }"
                   "QMessageBox QPushButton { background-color: #f4f4f5; color: #18181b; border: 1px solid #e4e4e7; border-radius: 4px; padding: 6px 12px; min-width: 60px; }"
                   "QMessageBox QPushButton:hover { background-color: #e4e4e7; border-color: #d4d4d8; }"
                   "QMessageBox QPushButton:pressed { background-color: #f97316; border-color: #f97316; color: white; }"
                   "QSlider::groove:horizontal { border: 1px solid #e4e4e7; height: 4px; background: #e4e4e7; margin: 2px 0; border-radius: 2px; }"
                   "QSlider::handle:horizontal { background: #f97316; border: 1px solid #f97316; width: 14px; height: 14px; margin: -6px 0; border-radius: 7px; }"
                   "QSlider::handle:horizontal:hover { background: #fb923c; }"
                   "QScrollBar:vertical { border: none; background: #fafafa; width: 10px; margin: 0px; }"
                   "QScrollBar::handle:vertical { background: #e4e4e7; min-height: 20px; border-radius: 5px; }"
                   "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
                   "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }";
        }
    }

    static QString getInputStyle() {
        if (isDarkMode) {
            return "QLineEdit { background-color: #18181b; border: 1px solid #27272a; padding: 10px; color: #e4e4e7; border-radius: 6px; font-size: 12px; }"
                   "QLineEdit:focus { border: 1px solid #f97316; }";
        } else {
            return "QLineEdit { background-color: #ffffff; border: 1px solid #e4e4e7; padding: 10px; color: #18181b; border-radius: 6px; font-size: 12px; }"
                   "QLineEdit:focus { border: 1px solid #f97316; }";
        }
    }

    static QString getButtonStyle(bool primary = false, bool destructive = false) {
        if (destructive) {
            // Destructive is consistent in Red for both
            return "QPushButton { background-color: #7f1d1d; color: #fecaca; border: 1px solid #991b1b; padding: 10px; border-radius: 6px; font-weight: bold; }"
                   "QPushButton:hover { background-color: #991b1b; border-color: #b91c1c; }"
                   "QPushButton:pressed { background-color: #b91c1c; }"; 
        }
        if (primary) {
            // Primary matches the Brand Orange
            return "QPushButton { background-color: #ea580c; color: white; border: 1px solid #c2410c; padding: 10px; border-radius: 6px; font-weight: bold; }"
                   "QPushButton:hover { background-color: #f97316; border-color: #ea580c; }"
                   "QPushButton:pressed { background-color: #c2410c; }";
        }
        if (isDarkMode) {
            return "QPushButton { background-color: #27272a; color: #e4e4e7; border: 1px solid #3f3f46; padding: 10px; border-radius: 6px; font-weight: bold; }"
                   "QPushButton:hover { background-color: #3f3f46; border-color: #52525b; color: white; }"
                   "QPushButton:pressed { background-color: #52525b; }";
        } else {
            return "QPushButton { background-color: #ffffff; color: #18181b; border: 1px solid #e4e4e7; padding: 10px; border-radius: 6px; font-weight: bold; }"
                   "QPushButton:hover { background-color: #f4f4f5; border-color: #d4d4d8; color: #000000; }"
                   "QPushButton:pressed { background-color: #e4e4e7; }";
        }
    }

    static QString getListStyle() {
        if (isDarkMode) {
            return "QListWidget { background-color: #18181b; border: 1px solid #27272a; color: #e4e4e7; border-radius: 6px; outline: none; }"
                   "QListWidget::item { padding: 8px; border-bottom: 1px solid #27272a; }"
                   "QListWidget::item:selected { background-color: #27272a; border-left: 3px solid #f97316; color: white; }"
                   "QListWidget::item:hover { background-color: #27272a; }";
        } else {
            return "QListWidget { background-color: #ffffff; border: 1px solid #e4e4e7; color: #18181b; border-radius: 6px; outline: none; }"
                   "QListWidget::item { padding: 8px; border-bottom: 1px solid #f4f4f5; }"
                   "QListWidget::item:selected { background-color: #f4f4f5; border-left: 3px solid #f97316; color: #000000; }"
                   "QListWidget::item:hover { background-color: #fafafa; }";
        }
    }

    static QString getVideoContainerStyle() {
        if (isDarkMode) {
            return "QFrame { background-color: #18181b; border: 1px solid #27272a; border-radius: 8px; }"
                   "QFrame:hover { border: 1px solid #f97316; }"
                   "QLabel { color: #e4e4e7; background: transparent; }";
        } else {
            return "QFrame { background-color: #ffffff; border: 1px solid #e4e4e7; border-radius: 8px; }"
                   "QFrame:hover { border: 1px solid #f97316; }"
                   "QLabel { color: #18181b; background: transparent; }";
        }
    }
};

#endif // STYLEHELPER_H
