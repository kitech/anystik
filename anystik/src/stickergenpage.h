#ifndef STICKER_GEN_PAGE_H
#define STICKER_GEN_PAGE_H

#include "page.h"
#include "stickerstore.h"
#include <QDateTime>
#include <QSet>
#include <QStringList>
#include <QPointer>
#include <QElapsedTimer>

class QskTextLabel;
class QskComboBox;
class QskTextField;
class QskPushButton;
class QskSpinBox;
class QskCheckBox;
class QskGraphicLabel;
class QskMenu;
class QTimer;
class QNetworkAccessManager;
class QNetworkReply;
class QImage;
class MyScrollArea;

// 生成表情页：输入提示词 → Pollinations 免费 API 生成表情图 → 预览/
// 入库/拷贝/另存。支持提交历史（QSettings 持久化）与匿名层 15s 限频。
class StickerGenPage : public Page
{
    Q_OBJECT
public:
    StickerGenPage(QQuickItem* parent = nullptr);

    Q_INVOKABLE void retranslateUi() override;

protected:
    void onCreate(const QVariantMap& launchArgs,
                  const QVariantMap& savedState) override;
    void onStop() override;

private:
    void startGenerate();
    void onReplyFinished();
    void updateRateLimitBar();
    void openHistoryMenu(const QPointF& scenePos);
    void showPreviewMenu(const QPointF& scenePos);
    void importToStore();
    void copyResult();
    void saveResultAs();
    void pushHistory(const QString& prompt, quint64 seed);
    void setMetricBar(const QString& text);
    void showToast(const QString& text);

    QskTextLabel* m_title = nullptr;
    QskComboBox* m_engineCombo = nullptr;
    QskTextLabel* m_engineLabel = nullptr;
    QskTextField* m_promptInput = nullptr;
    QskPushButton* m_historyBtn = nullptr;
    QskTextLabel* m_seedLabel = nullptr;
    QskSpinBox* m_seedSpin = nullptr;
    QskCheckBox* m_styleCheck = nullptr;
    QskPushButton* m_saveBtn = nullptr;
    QskPushButton* m_genBtn = nullptr;
    QskTextLabel* m_statusRow = nullptr;
    QskGraphicLabel* m_preview = nullptr;
    QskTextLabel* m_metricBar = nullptr;
    MyScrollArea* m_scroll = nullptr;

    QNetworkAccessManager* m_nam = nullptr;
    QNetworkReply* m_reply = nullptr;
    QByteArray m_lastBytes;
    QImage m_resultImage;
    int m_lastImageWidth = 0;
    int m_lastImageHeight = 0;
    double m_lastElapsedSec = 0.0;  // 最近一次请求用时（秒）

    QStringList m_history;      // 按新→旧，去重（提示词）
    QList<quint64> m_seedHistory; // 与 m_history 平行（同序，新→旧）
    QDateTime m_allowNextAt;    // 限频窗口截止（生成按钮解锁时刻）
    QElapsedTimer m_elapsed;    // 本次请求计时
    QTimer* m_countdownTimer = nullptr;
    QPointer<QskMenu> m_previewMenu; // 预览右键菜单（触屏长按共用）
};

#endif // STICKER_GEN_PAGE_H