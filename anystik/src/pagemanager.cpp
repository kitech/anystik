#include "pagemanager.h"
#include <QskStackBox.h>
#include <QSettings>
#include <QDebug>

PageManager::PageManager(QskStackBox* stackBox, QObject* parent)
    : QObject(parent), m_stackBox(stackBox)
{
}

PageManager::~PageManager()
{
    // 不需要手动清理 — Qt 父子关系会处理
}

void PageManager::registerPage(const QString& id, PageFactory factory,
                                const PageRegistration& reg)
{
    if (m_factories.contains(id)) {
        qWarning() << "[PageManager] duplicate register:" << id;
        return;
    }
    m_factories[id] = factory;
    m_registrations[id] = reg;
}

// ── 内部: 创建并初始化新页面 ──
Page* PageManager::createPage(const QString& id, const QVariantMap& args,
                               const QVariantMap& savedState)
{
    auto fit = m_factories.find(id);
    if (fit == m_factories.end()) {
        qWarning() << "[PageManager] unknown page:" << id;
        return nullptr;
    }

    Page* page = fit.value()();
    page->setPageManager(this);
    page->setPageId(id);
    page->setState(PageState::None);

    m_stackBox->addItem(page);
    m_pages[id] = page;

    // 连接 finishRequested → back()
    connect(page, &Page::finishRequested, this, [this, id]() {
        if (!m_busy && m_pages.contains(id) && m_pages[id] == currentPage()) {
            back();
        }
    });

    page->onCreate(args, savedState);
    page->setState(PageState::Created);

    qDebug() << "[PageManager] created:" << id;
    return page;
}

// ── 内部: 销毁页面 ──
void PageManager::destroyPage(Page* page)
{
    if (!page || page->state() == PageState::Destroyed) return;

    const QString id = page->pageId();
    m_cacheLRU.removeAll(id);

    page->onDestroy();
    page->setState(PageState::Destroyed);
    page->setPageManager(nullptr);

    m_pages.remove(id);
    m_stackBox->removeItem(page);
    page->deleteLater();

    qDebug() << "[PageManager] destroyed:" << id;
}

// ── 内部: 激活页面（使其成为当前项）──
void PageManager::activatePage(Page* page)
{
    if (!page) return;

    // 取消待处理的延迟销毁（快速导航时旧页还在 stackbox）
    if (m_destroyTimer.isActive()) {
        m_destroyTimer.stop();
        if (m_pendingDestroy) {
            destroyPage(m_pendingDestroy);
            m_pendingDestroy = nullptr;
        }
    }

    // 取消延迟移除（页面被重新激活，不应再从 stackbox 移除）
    if (m_pendingRemove) {
        m_pendingRemove = nullptr;
    }

    switch (page->state()) {
    case PageState::Created:
        // 新创建: onStart → onResume
        page->onStart();
        page->setState(PageState::Started);
        break;

    case PageState::Stopped:
        // 从缓存: onRestart → onStart → onResume
        page->setFinishing(false);
        page->onRestart();
        page->onStart();
        page->setState(PageState::Started);
        break;

    case PageState::Paused:
        // 从部分覆盖恢复: 只 onResume
        break;

    default:
        qWarning() << "[PageManager] activatePage invalid state:" << (int)page->state()
                    << "for" << page->pageId();
        return;
    }

    setStackBoxCurrent(page);
    page->onResume();
    page->setState(PageState::Resumed);

    qDebug() << "[PageManager] activated:" << page->pageId();
}

// ── 内部: 停用当前页面（onPause → onStop）──
void PageManager::deactivateCurrentPage()
{
    Page* page = currentPage();
    if (!page) return;

    if (page->state() == PageState::Resumed) {
        page->onPause();
        page->setState(PageState::Paused);
    }
    if (page->state() == PageState::Paused ||
        page->state() == PageState::Started) {
        page->onStop();
        page->setState(PageState::Stopped);
    }

    qDebug() << "[PageManager] deactivated:" << page->pageId();
}

// ── 内部: 将页面加入 LRU 缓存 ──
void PageManager::addToCache(Page* page)
{
    const QString id = page->pageId();
    m_cacheLRU.removeAll(id);
    m_cacheLRU.push_front(id);

    // 超量时驱逐最久未用的
    while (m_cacheLRU.size() > m_cacheMaxSize) {
        evictLRU();
    }
}

// ── 内部: 驱逐 LRU 缓存中最久未用的页面 ──
void PageManager::evictLRU()
{
    if (m_cacheLRU.isEmpty()) return;

    QString id = m_cacheLRU.back();
    m_cacheLRU.pop_back();

    Page* page = m_pages.value(id);
    if (!page) return;

    qDebug() << "[PageManager] LRU evict:" << id;

    // 在销毁前保存状态（进程死亡后可能需要恢复）
    QVariantMap state;
    page->onSaveInstanceState(state);
    QSettings().setValue("page_" + id + "_state", state);

    // 标记 finishing = true（即将被销毁）
    page->setFinishing(true);
    destroyPage(page);
}

// ── 内部: 设置 QskStackBox 当前项 ──
void PageManager::setStackBoxCurrent(Page* page)
{
    if (!page || !m_stackBox) return;
    if (m_stackBox->currentItem() != page) {
        m_stackBox->setCurrentItem(page);
    }
}

// ── 查询 ──
Page* PageManager::currentPage() const
{
    if (m_history.isEmpty()) return nullptr;
    QString id = m_history.last();
    return m_pages.value(id);
}

QString PageManager::currentPageId() const
{
    Page* p = currentPage();
    return p ? p->pageId() : QString();
}

Page* PageManager::findPage(const QString& id) const
{
    return m_pages.value(id);
}

// ── 导航: open ──
void PageManager::open(const QString& id, const QVariantMap& args,
                        LaunchMode mode)
{
    if (m_busy) {
        qWarning() << "[PageManager] busy, ignoring open:" << id;
        return;
    }
    m_busy = true;

    // 检查 LaunchMode
    if (mode == LaunchMode::SingleTop && !m_history.isEmpty()) {
        if (m_history.last() == id) {
            // 已在栈顶: onNewIntent
            Page* top = currentPage();
            if (top) {
                top->onNewIntent(args);
            }
            m_busy = false;
            return;
        }
    }

    if (mode == LaunchMode::SingleInstance) {
        // 如果已存在，bringToFront
        int existingIdx = m_history.indexOf(id);
        if (existingIdx >= 0) {
            // 将目标页之上的页面全部回退
            // 先停用当前页
            deactivateCurrentPage();

            // 回退历史到目标页
            while (m_history.size() > existingIdx + 1) {
                QString popId = m_history.last();
                Page* popPage = m_pages.value(popId);
                CachePolicy popPolicy = m_registrations.value(popId).policy;

                if (popPage) {
                    popPage->setFinishing(popPolicy == CachePolicy::Transient);
                    if (popPolicy == CachePolicy::Transient) {
                        destroyPage(popPage);
                    } else {
                        if (popPolicy == CachePolicy::LRU) {
                            addToCache(popPage);
                        }
                        QTimer::singleShot(250, this, [this, popPage]() {
                            if (m_stackBox->indexOf(popPage) >= 0) {
                                m_stackBox->removeItem(popPage);
                            }
                        });
                    }
                }
                m_history.pop_back();
            }

            // 激活目标页
            Page* target = m_pages.value(id);
            if (target) {
                target->onNewIntent(args);
                activatePage(target);
            }
            m_busy = false;
            return;
        }
    }

    // ── Standard 或 SingleInstance 未命中 ──
    // 1. 停用当前页
    deactivateCurrentPage();

    // 2. 创建或恢复目标页
    Page* target = findPage(id);
    if (!target) {
        // 全新创建
        QVariantMap savedState;
        if (!m_history.isEmpty()) {
            // 仅在恢复时使用 QSettings
            savedState = QSettings().value("page_" + id + "_state").toMap();
        }
        target = createPage(id, args, savedState);
    } else {
        // 在缓存中（cacheable/permanent）
        target->onNewIntent(args);
        // 如果页面在 m_pages 中但不在 stackbox 中（之前 back 时被移除了），重新添加
        if (m_stackBox->indexOf(target) < 0) {
            m_stackBox->addItem(target);
        }
    }

    if (!target) {
        qWarning() << "[PageManager] open: failed to create" << id;
        m_busy = false;
        return;
    }

    // 3. 激活目标页
    activatePage(target);

    // 4. 记录历史（栈顶去重，避免重复推入导致返回栈膨胀）
    if (m_history.isEmpty() || m_history.last() != id) {
        m_history.push_back(id);
    }

    m_busy = false;
}

// ── 导航: openForResult ──
void PageManager::openForResult(const QString& id, const QVariantMap& args,
                                 PageResultCallback callback,
                                 LaunchMode mode)
{
    if (m_history.isEmpty()) {
        qWarning() << "[PageManager] openForResult: no caller";
        return;
    }
    QString callerId = m_history.last();
    m_pendingResults[id] = {callerId, callback};
    open(id, args, mode);
}

// ── 导航: back ──
void PageManager::back()
{
    if (m_busy) {
        qWarning() << "[PageManager] busy, ignoring back";
        return;
    }

    if (m_history.size() <= 1) {
        qDebug() << "[PageManager] back: at root, ignored";
        return;
    }

    m_busy = true;

    QString currentId = m_history.last();
    QString targetId = m_history[m_history.size() - 2];
    CachePolicy policy = m_registrations.value(currentId).policy;

    Page* current = m_pages.value(currentId);

    // 1. 停用当前页（延迟从 stackbox 移除，保留页面实例）
    if (current) {
        current->setFinishing(policy == CachePolicy::Transient);
        deactivateCurrentPage();
    }

    // 2. 查找或创建目标页
    Page* target = m_pages.value(targetId);
    if (!target) {
        QVariantMap savedState = QSettings().value("page_" + targetId + "_state").toMap();
        target = createPage(targetId, {}, savedState);
    } else {
        // 重新添加到 stackbox（如果之前 back 时被移除了）
        if (m_stackBox->indexOf(target) < 0) {
            m_stackBox->addItem(target);
        }
        auto rit = m_pendingResults.find(targetId);
        if (rit != m_pendingResults.end()) {
            if (current) {
                rit.value().callback(current->resultCode(), current->resultData());
            } else {
                rit.value().callback(-1, {});
            }
            m_pendingResults.erase(rit);
        }
    }

    // 3. 激活目标页（此时两个页面都在 stackbox，索引正确，动画可触发）
    if (target) {
        activatePage(target);
    }

    // 4. 延迟销毁/移除当前页（等动画完成后 removeItem，避免 animator item 引用失效）
    if (current) {
        if (policy == CachePolicy::Transient) {
            m_pendingDestroy = current;
            m_destroyTimer.singleShot(250, this, [this]() {
                if (m_pendingDestroy) {
                    destroyPage(m_pendingDestroy);
                    m_pendingDestroy = nullptr;
                }
            });
        } else {
            // LRU/Permanent: 延迟从 stackbox 移除（等动画完成），但保留页面实例
            if (policy == CachePolicy::LRU) {
                addToCache(current);
            }
            m_pendingRemove = current;
            QTimer::singleShot(250, this, [this]() {
                if (m_pendingRemove) {
                    if (m_stackBox->indexOf(m_pendingRemove) >= 0) {
                        m_stackBox->removeItem(m_pendingRemove);
                    }
                    m_pendingRemove = nullptr;
                }
            });
        }
    }

    // 5. 从历史中移除
    m_history.pop_back();

    m_busy = false;
}

// ── 导航: replace ──
void PageManager::replace(const QString& id, const QVariantMap& args)
{
    if (m_busy) {
        qWarning() << "[PageManager] busy, ignoring replace:" << id;
        return;
    }
    m_busy = true;

    QString currentId = m_history.isEmpty() ? QString() : m_history.last();
    Page* current = m_pages.value(currentId);

    // 1. 停用当前页，延迟销毁（等动画完成后 removeItem）
    if (current) {
        current->setFinishing(true);
        deactivateCurrentPage();
        m_pendingDestroy = current;
        m_destroyTimer.singleShot(250, this, [this]() {
            if (m_pendingDestroy) {
                destroyPage(m_pendingDestroy);
                m_pendingDestroy = nullptr;
            }
        });
    }

    // 2. 替换历史最后一项（或为空时新增）
    if (!m_history.isEmpty()) {
        m_history.last() = id;
    } else {
        m_history.push_back(id);
    }

    // 3. 创建或激活目标页
    Page* target = m_pages.value(id);
    if (!target) {
        QVariantMap savedState = QSettings().value("page_" + id + "_state").toMap();
        target = createPage(id, args, savedState);
    } else {
        target->onNewIntent(args);
    }

    if (target) {
        activatePage(target);
    }

    m_busy = false;
}

// ── 状态保存（进程死亡保护）──
void PageManager::saveAllStates()
{
    qDebug() << "[PageManager] saveAllStates";

    for (auto it = m_pages.constBegin(); it != m_pages.constEnd(); ++it) {
        Page* page = it.value();
        if (page->state() == PageState::None ||
            page->state() == PageState::Destroyed) continue;

        QVariantMap state;
        page->onSaveInstanceState(state);
        if (!state.isEmpty()) {
            QSettings().setValue("page_" + it.key() + "_state", state);
        }
    }

    // 保存栈结构
    QSettings().setValue("pageManager_history", m_history);
    QSettings().setValue("pageManager_currentId", currentPageId());
    QSettings().sync();

    qDebug() << "[PageManager] saved" << m_pages.size() << "pages, history:" << m_history;
}

// ── 状态恢复（进程死亡后重建）──
void PageManager::restoreAllStates()
{
    QSettings settings;
    QStringList history = settings.value("pageManager_history").toStringList();
    if (history.isEmpty()) return;

    qDebug() << "[PageManager] restoreAllStates, history:" << history;

    m_history = history;

    // 重建所有历史中的页面
    for (int i = 0; i < m_history.size(); ++i) {
        const QString& id = m_history[i];
        if (m_pages.contains(id)) continue;

        QVariantMap savedState = settings.value("page_" + id + "_state").toMap();

        auto fit = m_factories.find(id);
        if (fit == m_factories.end()) {
            qWarning() << "[PageManager] restore: unknown page" << id;
            continue;
        }

        Page* page = fit.value()();
        page->setPageManager(this);
        page->setPageId(id);
        m_stackBox->addItem(page);
        m_pages[id] = page;

        // 连接 finishRequested
        connect(page, &Page::finishRequested, this, [this, id]() {
            if (!m_busy && m_pages.contains(id) && m_pages[id] == currentPage()) {
                back();
            }
        });

        page->onCreate({}, savedState);
        page->setState(PageState::Created);

        // 如果这是当前页，激活它
        if (i == m_history.size() - 1) {
            activatePage(page);
        }
    }

    qDebug() << "[PageManager] restored" << m_pages.size() << "pages";
}
