#ifndef HEADERWIDGET_H
#define HEADERWIDGET_H

#include <QWidget>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>


class HeaderWidget : public QWidget {
    Q_OBJECT
public:
    explicit HeaderWidget(QWidget *parent = nullptr);
    void updateTheme();

signals:
    void themeToggled();

private:
    void setupUi();
    QPushButton *btnTheme;
};

#endif // HEADERWIDGET_H
// hi