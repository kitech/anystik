#ifndef LOADINGBAR_H
#define LOADINGBAR_H

#include "compat34.h"
#include <qwidget.h>
#include <qlabel.h>
#include <qtimer.h>
#include <vector>

class LoadingBar : public QWidget {
    Q_OBJECT
public:
    LoadingBar(QWidget* parent = 0);
    void showLoading(int id, const QString& msg);
    void hideLoading(int id);
    void clearLoading();

private slots:
    void onTimerTick();

private:
    struct LoadingItem {
        int id;
        QString msg;
    };
    QLabel* m_label;
    QTimer* m_timer;
    std::vector<LoadingItem> m_items;
    int m_dotCount;
    void updateDisplay();
};

#endif
