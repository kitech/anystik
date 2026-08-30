# qsktox 实现详解

基于 Qt 6.7 + QSkinny 的跨平台表情包/贴纸管理应用，支持 Android/Linux/macOS/Windows，不使用 QML。

> 最近更新: **移除原聊天列表/消息页面及其存储**（MainPage 保留空壳不注册、聊天型 4 模块删除、聊天域数据启动即 DROP；PushStatusBar/keepScreenOn 迁至 stickerhome；分享改为独立队列）。详见文末「聊天功能移除」章节。

---

## 架构概览

```
main.cpp (入口 + BackButtonFilter + extras线程)
  ├── PageManager (导航栈 + 生命周期管理 + LRU缓存 + 进程死亡保护)
  │     ├── [stickerhome]  — 表情包管理首页 (默认启动页, 唯一根页)
  │     ├── LoginPage        — 服务器选择 (保留注册，无 UI 入口)
  │     ├── SettingsPage     — 设置（主题/字体/动画）
  │     ├── AboutPage        — 系统信息
  │     └── LogPage          — 日志查看
  │
  ├── NetworkMonitor (4平台网络监控 + 桌面通知)
  ├── KeepAlive (Android前台服务保活)
  ├── androidutils (Android Toast)
  ├── shareintentreceiver.cpp (Android分享接收)
  ├── LogModel (内存日志缓冲，单例)
  └── extras: libgoso.so (Go) + libcso.so (C++)
```

---

## 文件清单

### 构建文件

| 文件 | 说明 |
|------|------|
| `CMakeLists.txt` | 顶层 CMake，配置 Qt6 + QSkinny + Go/C++ extras |
| `build-x64.sh` | Linux/macOS x64 构建脚本 |
| `build-android.sh` | Android arm64 构建脚本（含 Go 交叉编译 + APK 打包） |
| `plan.md` | 设计笔记和构建环境参考 |

### 源码文件 (`src/`)

| 文件 | 行数 | 说明 |
|------|------|------|
| `main.cpp` | 448 | 入口：QGuiApplication、皮肤、字体、窗口、页面注册、extras线程、Push分发器选择 |
| `page.h/cpp` | 107+26 | Page 基类，Android 风格生命周期模型 |
| `pagemanager.h/cpp` | 122+505 | 导航栈 + 生命周期管理 + LRU缓存 + 状态保存 |
| `loginpage.h/cpp` | 16+47 | 登录页：3个硬编码服务器按钮 |
| `mainpage.h/cpp` | 37+219 | 主聊天界面：TopBar + ChatArea + InputBar |
| `settingspage.h/cpp` | 53+243 | 设置页：过渡动画/主题/字体/调试背景 |
| `aboutpage.h/cpp` | 16+90 | 关于页：版本/Qt/RHI/架构/设备信息 |
| `logpage.h/cpp` | 37+164 | 日志查看器：过滤 + 搜索 + 实时更新 |
| `logmodel.h/cpp` | 41+29 | 内存日志缓冲（单例，500条上限） |
| `menuoverlay.h/cpp` | 47+69 | 透明遮罩层，点击外部关闭 QskMenu |
| `networkmonitor.h/cpp` | 14+195 | 4平台网络监控 |
| `keepalive.h/cpp` | 14+42 | Android 前台服务保活 |
| `pushhandler.h/cpp` | 60+571 | UnifiedPush 注册、分发器管理、信号路由 |
| `androidutils.h/cpp` | 8+26 | Android Toast 工具函数 |
| `shareintentreceiver.cpp` | 45 | JNI 桥接：ShareActivity → MainPage |
| `stickerstore.h/cpp` | ~90+340 | 表情包数据层单例：Storage 惰性初始化 + sticker_db 封装 + 目录导入 + 剪贴板复制 |
| `stickerlist.h/cpp` | ~120+300 | 表情网格：虚拟滚动 + QskPaintedNode 瓦片，单击复制/双击预览/长按菜单 |
| `stickerhomepage.h/cpp` | ~85+500 | 表情包首页：搜索 + 分组Tab + 网格 + 选项菜单 + 目录选择器/重命名弹窗 |
| `stickerpreviewoverlay.h/cpp` | ~65+280 | 大图/动图(GIF QMovie) 全屏预览遮罩 |

### Android Java 文件 (`android/`)

| 文件 | 说明 |
|------|------|
| `AndroidManifest.xml` | 清单：权限、ShareActivity、KeepAliveService、FileProvider |
| `ShareActivity.java` | 主 Activity，处理 SEND 分享意图 |
| `NetworkMonitor.java` | BroadcastReceiver 监听网络变化 |
| `KeepAliveService.java` | 前台服务保活 |
| `PermissionHelper.java` | 运行时权限请求（通知/媒体） |
| `MobUtil.java` | 工具：状态栏/导航栏高度、屏幕尺寸、Toast |

### Extras 共享库

| 文件 | 说明 |
|------|------|
| `extras/go/goso.go` | Go 共享库：`gosoMainLoop()` 占位无限循环 |
| `extras/go/go.mod` | Go 模块定义 |
| `extras/cpp/cso.h/cpp` | C++ 共享库：`csoMainLoop()` 占位无限循环 |

---

## 核心组件详解

### 1. Page 生命周期模型 (`page.h/cpp`)

模仿 Android Activity/Fragment 生命周期：

```cpp
enum PageState {
    None, Created, Started, Resumed,
    Paused, Stopped, Destroyed
};

enum LaunchMode {
    Standard,       // 每次创建新实例
    SingleTop,      // 栈顶复用，调 onNewIntent
    SingleInstance  // 栈中唯一，bring to front
};

enum CachePolicy {
    Transient,  // 返回时销毁（250ms延迟，配合动画）
    LRU,        // LRU缓存，超限淘汰
    Permanent   // 永不销毁
};
```

生命周期回调：`onCreate` → `onStart` → `onResume` → `onPause` → `onStop` → `onDestroy`

额外支持：`finish()` 发射 `finishRequested`，`setResult()`/`resultCode()`/`resultData()` 用于页面间传结果。

### 2. PageManager (`pagemanager.h/cpp`)

核心导航和生命周期管理器，管理 `QskStackBox`。

**关键数据结构：**
- `m_history: QList<QString>` — 导航历史栈
- `m_pages: QMap<QString, Page*>` — 所有存活页面
- `m_cacheLRU: QStringList` — LRU 缓存队列
- `m_factories/m_registrations` — 页面类型注册表
- `m_pendingResults` — `openForResult` 的回调
- `m_busy` — 重入锁
- `m_destroyTimer/m_pendingDestroy` — 延迟销毁（配合动画）

**关键方法：**

| 方法 | 说明 |
|------|------|
| `registerPage(id, factory, registration)` | 注册页面类型 |
| `open(id, args, mode)` | 压栈导航，处理 SingleTop/SingleInstance |
| `openForResult(id, args, callback, mode)` | 带结果回调的导航 |
| `back()` | 弹栈，延迟销毁（Transient用250ms timer配合动画） |
| `replace(id, args)` | 替换当前页面 |
| `saveAllStates()` / `restoreAllStates()` | QSettings 序列化/反序列化（进程死亡恢复） |

**动画安全设计：**
- `back()`: 先 activate 目标页，再延迟销毁当前页（250ms timer）
- `replace()`: 先销毁当前页，再 activate 新页（无动画过渡）
- `QStackBoxAnimator::itemAt()` 通过 `m_startIndex/m_endIndex` 访问 items，删除会导致引用失效
- `activatePage()` 会取消待销毁的页面（`cancelPendingDestroy()`）

### 3. LoginPage (`loginpage.cpp`)

简单的服务器选择页面，3个硬编码服务器按钮：
- `localhost:8181`
- `192.168.43.157:4004`
- `192.168.49.136:4004`

点击后调用 `pageManager()->replace("stickerhome")` 进入表情包首页（登录页保留注册、无 UI 入口；「聊天模式」菜单项已移除）。

### 4. MainPage (`mainpage.cpp`) — 已弃用空壳

聊天主界面已移除，`mainpage.h/cpp` 保留为**空壳 Page**（仅继承，无 UI、不注册、"main"/"message" 路由与 `registerMainPage` 已删）。相关能力迁移：
- keepScreenOn 开关 → StickerHomePage 选项菜单
- 分享意图处理 → `shareintentreceiver.cpp` 静态队列
- PushStatusBar → StickerHomePage 顶栏下方
- 推送消息 → Android Toast
- 聊天记录（messages/reactions/translations/bookmarks/channels/peers/pending_messages + messages_fts）→ `StickerStore::dropLegacyChatTables()` 于 Storage init 后 DROP

### 5. SettingsPage (`settingspage.cpp`)

5个设置项：

| 设置 | 控件 | 持久化键 |
|------|------|----------|
| Page Transition | QskComboBox (None/Slide Fade) | `transition` |
| Theme | QskComboBox (Fusion) | `skin` |
| Color Scheme | QskSwitchButton | `darkMode` |
| Font Size | QskComboBox + 动态调节 | `fontScale` |
| Debug Background | QskSwitchButton | `debugBackground` |

`FontSizes` 结构体：`body, title, caption, global`（int 字号）

`applyAndroidFonts()`: 设置 QSkinny 字体角色（仅 Android），桌面端不生效。

### 6. AboutPage (`aboutpage.cpp`)

显示信息：
- App Version
- Qt Version
- RHI Backend
- Architecture（ARM64/x86_64 等）
- Device Info（Android 版本/SDK 等）

### 7. LogPage (`logpage.cpp`)

日志查看器：
- 级别过滤：All / Info / Warn / Error
- 搜索：防抖（QTimer）+ 大小写不敏感
- 实时更新：监听 `LogModel::entryAdded/cleared` 信号
- 底部状态栏：条目计数

### 8. LogModel (`logmodel.cpp`)

内存日志缓冲（单例）：
- 最大 500 条，超出淘汰最旧
- 每条包含：Level（Debug/Info/Warn/Error）、时间戳（HH:mm:ss.zzz）、tag、message
- 发射信号：`entryAdded(index)` / `cleared()`

### 9. MenuOverlay (`menuoverlay.cpp`)

解决 QSkinny 的 `CloseOnPressOutside` 对兄弟控件不生效的问题。

透明全屏遮罩层，拦截鼠标/触摸事件，点击菜单外部时关闭菜单。通过 `stackBefore(menu)` 控制 z-order。

底部有 `#include "moc_menuoverlay.cpp"`（预包含 MOC，因为 Q_OBJECT 在 `#ifdef` 外）。

### 10. NetworkMonitor (`networkmonitor.cpp`)

4平台独立实现：

| 平台 | 网络检测 | 通知方式 |
|------|----------|----------|
| Android | JNI → `NetworkMonitor.java` (BroadcastReceiver) | Android Toast (LENGTH_LONG ~3.5s) |
| Linux | `QNetworkInformation::loadDefaultBackend()` + `instance()` | `notify-send -t 7000` |
| Windows | `QNetworkInformation` | PowerShell Toast API |
| macOS | `QNetworkInformation` | `osascript` (AppleScript) |

**重要：** `QNetworkInformation` 是单例，析构函数是 private 的。必须用 `loadDefaultBackend()` + `instance()`，不能 `new`/`delete`。`stop()` 只能 `disconnect` + 置 nullptr。

### 11. KeepAlive (`keepalive.cpp`)

仅 Android 有效：
1. 请求通知权限 (`PermissionHelper.requestNotificationPermission`)
2. 启动前台服务 (`KeepAliveService.startService`)

桌面端为 no-op。

### 12. androidutils (`androidutils.cpp`)

`showAndroidToast(const QString& message)`: 通过 JNI 在 Android 主线程调用 `android.widget.Toast.makeText().show()`。桌面端 no-op。

### 13. shareintentreceiver.cpp

Android 分享接收：Java `ShareActivity` → 静态队列 → 主线程 `StickerStore::importImageBytes()`

不再依赖聊天 UI。`ShareActivity` 的两个 JNI 回调（`onShareIntentReceived` 文本/多文件、`onShareImageReceived` 原始字节）在子线程把 `PendingShare`（Image/Generic 两类）压入带锁静态队列 `s_queue`；主线程由 `drainPendingShareIntents()` 取出 `processOne()`：
- Image（带字节）→ `StickerStore::importImageBytes()` 入库到「粘贴板」+ Toast + `s_navigator()` 跳 `stickerhome`
- Generic（文本/URI 计数）→ 仅 Toast

`registerShareNavigator(std::function<void()>)` 由 `main.cpp` 调用（回调内 `pageManager->open("stickerhome")`）；`main.cpp` 在启动页打开后调用 `drainPendingShareIntents()`（`s_draining` 防重入，队列空置），`QCoreApplication` 与 JNI 线程判定后用 `QMetaObject::invokeMethod`（QueuedConnection）回主线程。

---

## JNI 桥接映射

| C++ 侧 | Java 侧 | 方向 |
|---------|---------|------|
| `showAndroidToast()` | `android.widget.Toast.makeText().show()` | C++ → Java |
| `KeepAlive::start()` | `PermissionHelper` + `KeepAliveService.startService()` | C++ → Java |
| `KeepAlive::stop()` | `KeepAliveService.stopService()` | C++ → Java |
| `NetworkMonitor::start()` | `NetworkMonitor.startMonitoring()` | C++ → Java |
| `NetworkMonitor::stop()` | `NetworkMonitor.stopMonitoring()` | C++ → Java |
| `Java_..._onNetworkChanged()` | `NetworkMonitor.onNetworkChanged()` | Java → C++ |
| `Java_..._onShareIntentReceived()` | `ShareActivity.onShareIntentReceived()` | Java → C++ |

---

## Extras 共享库

### Go 共享库 (`extras/go/goso.go`)

```go
package main

import "C"
import "time"

//export gosoMainLoop
func gosoMainLoop() {
    for {
        time.Sleep(time.Hour)
    }
}
```

编译：`CGO_ENABLED=1 go build -buildmode=c-shared -o libgoso.so`

### C++ 共享库 (`extras/cpp/cso.cpp`)

```cpp
#include "cso.h"
#include <unistd.h>

extern "C" void csoMainLoop() {
    while (1) {
        sleep(3600);
    }
}
```

所有平台均使用共享库（.so），通过 CGO 编译。主程序在 detached 线程中启动两个入口函数。

---

## 平台特定代码

| 文件 | `#ifdef` 守卫 |
|------|---------------|
| `main.cpp` | `Q_OS_ANDROID`（渲染循环、皮肤路径、窗口显示、KeepAlive、NetworkMonitor）；`!Q_OS_ANDROID`（桌面图标、快捷键、窗口大小） |
| `networkmonitor.cpp` | `Q_OS_ANDROID` / `Q_OS_LINUX` / `Q_OS_WINDOWS` / `Q_OS_MACOS`（4套独立实现） |
| `keepalive.cpp` | `Q_OS_ANDROID`（整个实现） |
| `androidutils.cpp` | `Q_OS_ANDROID`（JNI 实现，否则 no-op） |
| `shareintentreceiver.cpp` | `Q_OS_ANDROID`（JNI 回调） |
| `mainpage.cpp` | `Q_OS_ANDROID`（JNI KeepScreenOn） |
| `aboutpage.cpp` | `Q_PROCESSOR_ARM_V8`（架构标签）；`Q_OS_ANDROID`（Android 版本） |
| `CMakeLists.txt` | `ANDROID`（ABI 目标修复、额外链接库、Go 交叉编译环境） |
| `build-x64.sh` | `uname != Darwin`（Linux 专用 sed 修复） |

**注意：** Android NDK 定义了 `Q_OS_LINUX`，所以必须用 `#if defined(Q_OS_ANDROID)` / `#elif defined(Q_OS_LINUX)` 模式。

---

## 构建系统

### CMakeLists.txt

- C++17，Qt6（Core, Quick, Network）
- `CMAKE_AUTOMOC = ON`
- Android ABI 目标修复：为 `Qt6Gui_arm64-v8a` 等创建别名接口库
- Go 共享库：自定义命令 `go build -buildmode=c-shared`，跨编译环境按平台设置
- C++ 共享库：`add_library(cso SHARED ...)`
- 链接：`Qsk::QSkinny Qt6::Quick Qt6::Network goso cso`，Android 额外链接 `log android`
- Go 库需要 `IMPORTED_NO_SONAME TRUE` 防止绝对路径写入 RPATH

### build-x64.sh

1. Go: `CGO_ENABLED=1 go build -buildmode=c-shared -o libgoso.so`
2. CMake: `qt-cmake` 配置（Qt 6.7.3, QSkinny）
3. Hotfix: Linux 上移除 `-lQt6Qml -lQt6Quick -lQt6OpenGL`（macOS 不需要）
4. Build: `make -j$(nproc)`

### build-android.sh

1. Go 交叉编译: `GOOS=android GOARCH=arm64 CGO_ENABLED=1 CC=$NDK/toolchains/llvm/.../aarch64-linux-android24-clang go build -buildmode=c-shared`
2. CMake: `qt-cmake` 配置（Android SDK/NDK）
3. Build: `make -j$(nproc)` (native .so only)
4. Prepare: 创建 `android-build/` 目录，复制 flat JNI libs
5. QSkinny 皮肤: 复制 `qskinny_fusion` 插件
6. `androiddeployqt --aux-mode`: 准备 Qt 依赖
7. Copy: 自定义 Java 源码 + manifest + icon
8. Patch Gradle: 移除 renderscript，添加 Qt 属性，减少堆大小
9. `./gradlew assembleDebug`

---

## 外部依赖

| 依赖 | 版本 | 用途 |
|------|------|------|
| Qt | 6.7.3 | Core, Quick, Network, Qml |
| QSkinny | 8bc872f | UI 框架（替代 QML） |
| Go | 1.22+ | 共享库编译（cgo） |
| Android NDK | r26b | arm64-v8a 交叉编译 |
| Android SDK | API 33-34 | Java 编译、Gradle、APK |
| JDK | 17 | Gradle 运行时 |

---

## 已知设计决策

1. **不使用 QML** — 全部 UI 使用 QSkinny 控件布局
2. **Android 风格生命周期** — Page 模仿 Fragment 生命周期，PageManager 管理状态
3. **延迟销毁** — `back()` 使用 250ms timer 延迟销毁当前页，避免 QStackBoxAnimator 引用失效
4. **单例模式** — `LogModel` 为单例，`QNetworkInformation` 为 Qt 单例（private destructor）
5. **原子指针** — `shareintentreceiver.cpp` 使用 `std::atomic<MainPage*>` 跨线程传递 MainPage 引用
6. **extras 独立** — Go/C++ 共享库与主程序无通信，仅作为独立线程运行
7. **平台通知差异** — Android Toast ~3.5s，Linux notify-send 7s，Windows/macOS 使用系统默认
8. **IMPORTED_NO_SONAME** — Go 共享库防止绝对路径写入 RPATH，Android 通过文件名查找 .so

---

## Push Handler 流程

### 文件
- `pushhandler.h/cpp` — UnifiedPush 注册、分发器管理
- `main.cpp` — 启动时弹出分发器选择对话框（`QskDialog::select`）
- `settingspage.cpp` — Settings 页查询已安装分发器，更新 combo 标签

### 信号路由

| 信号 | 触发源 | 消费方 |
|------|--------|--------|
| `distributorsFound` | `registerDevice()`（启动注册） | `main.cpp` → 弹 dialog 选择 |
| `distributorsUpdated` | `installedDistributors()`（Settings 页查询） | `settingspage.cpp` → 更新 combo 标签 |

### 启动注册流程 (`registerDevice`)

```
PushHandler::start()
  → registerDevice()
    → Android getSavedDistributor()
      → 有 saved → 直接 register(saved)，不弹任何东西
      → 无 saved → getDistributors()
            → 空 → 报错 "未找到 UnifiedPush 分发器"
            → 1个 → selectDistributor() 自动选
            → >1个 → emit distributorsFound → main.cpp 弹 dialog
```

### Settings 页查询流程 (`installedDistributors`)

```
SettingsPage::onCreate()
  → PushHandler::installedDistributors()（异步）
    → Android getDistributors()
      → emit distributorsUpdated → rebuildBackendLabels() 更新 combo 标签
```

### 分发器切换

用户在 Settings 页手动切换分发器：
- `switchDistributor(newDistributor)` → 重新注册
- `selectDistributor(distributor)` → 直接选择（Android native 调用）

---

# 附录：表情包管理首页（增量重构 v2）

## 背景与目标

原始 `qsktox` 是聊天客户端。基于用户决策，将其增补为***跨平台表情包/贴纸管理应用**：

- **定位**：两者都要 —— 本地收藏管理与贴纸包导入/导出
- **系统组件**：保留推送/保活等系统级能力
- **核心功能**：本地文件夹导入、点击复制剪贴板、搜索、GIF/WebP 动图预览、最近使用、编辑/删除/分组重命名
- **决策约束**：***暂时不删除任何代码，纯增量添加新首页***，聊天功能保留（"聊天模式"入口仍在）

### 技术决策

| 项 | 决策 |
|----|------|
| ① 贴纸包格式 | 自研 `.stik`（ZIP 容器 + manifest.json），兼容导入 Telegram `.webp` 集 / WhatsApp `.wastickers` |
| ② compatcore34/limelog/md5 | 保留不移除 |
| ③ WebP 动图 | 支持（Android 侧动态 GIF 用 QMovie、WebP 渲染降级首帧） |

## 结构概览（新增页面树）

```
stickerhome (StickerHomePage, CachePolicy::Permanent, LaunchMode::SingleInstance — 默认启动页)
  ├── TopBar: 标题 + 导入按钮 + 选项菜单(⋯)
  │     ├── 导入表情包文件夹    → showDirPicker() 目录选择器
  │     ├── 分组管理            → showPackManageMenu() (重命名/删除分组)
  │     ├── Keep Screen On     → JNI FLAG_KEEP_SCREEN_ON 开关 (QSettings 持久化)
  │     ├── App Log / Settings / About   → 复用系统页面
  ├── PushStatusBar (推送状态条，桌面自隐藏; Android 常驻)
  ├── QskTextField 搜索框 (防抖 350ms → StickerStore::search)
  ├── QskTabBar 分组 Tab: [全部][最近][pack1][pack2]...
  └── StickerGridWidget (虚拟网格，复用 MyScrollArea)
        ├── 单击   → copyStickerToClipboard + touchSticker (最近使用)
        ├── 双击   → StickerPreviewOverlay (大图/GIF动画预览)
        └── 长按   → QskMenu (复制/预览/删除)
```

### StickerStore 单例（数据层）

`StickerStore` 封装 `::Storage` 惰性初始化 + `StickerDbSyncInterface`：

- `ensureInit()`：首次使用时调用 `Storage::init(AppLocalDataLocation)`（创建 message.db + cache.db + sticker_packs/stickers 表，多线程互斥单次执行）；init 成功后再调 `dropLegacyChatTables()` 清理聊天域 8 表
- stickers 表含 `description`（描述 ≤140 字，`StickerStore::MaxDescriptionLength` 截断上限，参与搜索）与 `deleted`（软删除标记，全部读写 SQL 过滤 `deleted=0`，删除为 `UPDATE deleted=1`；老库由 `init_sticker_db` 内 PRAGMA table_info 幂等 `ALTER` 补列）
- 查询：`packs(installed=1, orderby="created_at DESC", limit=0, offset=0)` / `stickers(packId, orderby="rowid DESC", limit=0, offset=0, deleted=0, emoji=nullptr)` / `recent(limit)` / `search(query)` / `countStickers()`
- 排序/分页：默认**时间倒序**（新添加在上面）；`orderby` 走白名单（packs：created_at/position/title；stickers：rowid/position/last_used × ASC/DESC，未知回落默认序），`limit>0` 才分页、`offset` 随 limit 生效，杜绝 SQL 注入。贴纸无 created_at 用 `rowid DESC`（rowid 严格递增≈最近导入）——贴纸时间序表现为「最近导入在前」，同 id 重导会置顶；包的 created_at 用 COALESCE 保留原值，包序稳定。- `position` 列保留写路径（新增即队尾）供将来手动排序功能复用
- 写操作：`importDirectory()`（子目录=分组，递归扫描 png/jpg/gif/webp/bmp/svg，事务+幂等ID）、`pasteFromClipboard()`（读系统剪贴板位图→PNG 落盘 `dataDir/pastes/`→归入「粘贴板」分组）、`renamePack()`（SQL 直改标题）、`deletePack()`（外键级联删贴纸）、`deleteSticker()`、`touchSticker()`
- 剪贴板：桌面 `QClipboard::setImage`；Android JNI `ShareActivity.copyImageToClipboard`（FileProvider content URI）+ Toast
- `dataChanged()` 信号 → 首页刷新 Tab 与网格

## 数据入口：Storage::init 惰性初始化链

```cpp
StickerStore::ensureInit()
  → Storage::instance().init(dataDir)      // sqlite3_config + mkdir + initCacheFsDirs
  → openDb(message.db) + openDb(cache.db)  // 5 domains, 12 tables
  → init_sticker_db(m_msgDb)               // sticker_packs + stickers (+FTS索引)
  → StickerStore 方法全部走 stickerDb() 直连查询
```

## 核心交互（时序）

### 导入
```
导入按钮 → showDirPicker()          // 无 QtWidgets，纯 QSkinny QskSimpleListBox 目录浏览器
  → 选目录 → StickerStore::importDirectory(dir)
    → scanRecursive 收集图片 → 按目录名建 pack（同名复用）→ begin/commit 事务
  → dataChanged → refreshTabBar + onTabChanged
```

### 粘贴添加
```
首页「粘贴」按钮 / 选项菜单「粘贴添加」 → requestPasteSticker()
  → StickerStore::pasteFromClipboard()
    → Desktop: QClipboard::image() 读位图 → QBuffer 编码 PNG
    → Android: JNI ShareActivity.readClipboardImageBytes()（先判 ClipDescription.hasMimeType("image/*")，再按 item.getUri() 读字节，全程 try/catch）
    → importImageBytes: 按内容探测真实格式 → 落盘 dataDir/pastes/<sha1>.<ext>（幂等覆盖）
    → findOrCreatePack「粘贴板」（同名复用/哈希建包）
    → 事务 add_sticker → dataChanged → 刷新 Tab + 网格
  （Android 不再依赖 QClipboard::image()——Qt 文档确认 Android 仅支持 text/plain|text/html|text/uri-list，image() 恒 null）
```

### 复制
```
单击瓦片 → touchSticker(id)（更新 last_used）
  → copyStickerToClipboard(filePath)
    → Desktop: clipboard->setImage(QImage(path))
    → Android: JNI ShareActivity.copyImageToClipboard（FileProvider content URI(image/*) + Toast）
      → 粘贴方（含本应用 readClipboardImageBytes）经 ContentResolver 按 Uri 读字节；
        外部支持 URI 解析的应用可贴出图，不支持的贴文本（平台语义）
```

### 分享（Android 出向）
```
长按菜单「分享」/（预览栏「删除」为软删入口）
  → StickerStore::shareStickerFile(filePath)
    → Android: ShareActivity.shareLocalImage(ctx, path)
       外部路径(目录导入)先拷入 <files>/shares/ → FileProvider(qtprovider)
       → ACTION_SEND + EXTRA_STREAM + image/<ext> + FLAG_GRANT_READ_URI_PERMISSION
       → createChooser 系统分享面板
    → 桌面: toast「桌面暂不支持分享」
```

### 删除（通用软删除）
```
网格长按「删除」/ 预览页右段「删除」→ confirmDeleteSticker(brief)
  → qskDialog 确认（Yes/No）
  → StickerStore::deleteSticker(id)（DB UPDATE deleted=1，文件保留，可重导恢复）
  → 关预览 → 刷新 Tab 与网格
```

### 搜索
```
文本防抖 350ms → StickerStore::search(keyword)
  → search_stickers("LIKE %keyword%") 匹配 emoji、分组标题或描述，按最近使用排序
```

## 平台差异

| 能力 | Desktop | Android |
|------|---------|---------|
| 目录选择 | QskSimpleListBox 目录浏览器（纯 QSK） | 走 ShareActivity 分享意图导入（TODO: SAF ACTION_OPEN_DOCUMENT_TREE） |
| 剪贴板复制 | `QClipboard::setImage` | JNI `copyImageToClipboard`（FileProvider URI） + Toast |
| GIF 动图 | 网格=首帧；预览=QMovie 全帧 | 同左 |
| WebP 动图 | Qt6 图像插件 | 缺 libwebp 时降级静态首帧 |

## 新增文件对齐 CMakeLists

`qt_add_executable(anystik ...)` 仅追加（不删任何既有条目）：
```cmake
src/stickerstore.h   src/stickerstore.cpp
src/stickerlist.h    src/stickerlist.cpp
src/stickerhomepage.h src/stickerhomepage.cpp
src/stickerpreviewoverlay.h src/stickerpreviewoverlay.cpp
```

`main.cpp` 追加注册：
```cpp
pageManager->registerPage("stickerhome", []() -> Page* {
    return new StickerHomePage();
}, {CachePolicy::Permanent, LaunchMode::SingleInstance});
// 启动页 open("login") → open("stickerhome")
```

## 设计决策

- **聊天移除后的存储**：qldox 零改动（storage.cpp 原样保留聊天三表初始化），聊天数据在 `ensureInit()` 成功后由 `StickerStore::dropLegacyChatTables()` DROP（messages/messages_fts/reactions/translations/bookmarks/channels/peers/pending_messages）；因此 `message_db/channel_db/pending_db.cpp` 仍需留在 CMake 构建集内供链接
- **零侵入**：分组重命名用 `Storage::msgDb().prepare("UPDATE sticker_packs ...")`
- **无 QtWidgets**：目录选择器用 `QskSimpleListBox` + `QDir` 组合，避免引入 QFileDialog
- **虚拟网格**：cols 自适应宽度，行级回收/创建
- **动图瓦片**：网格加载首帧（缩放 2x 保清晰），预览层才用 QMovie，兼顾性能
- **幂等导入**：贴纸 ID = SHA1(文件路径)，重复导入覆盖而非堆积
- **退出交互**：BackButtonFilter 泛化——非根层返栈，根层（stickerhome）单击 Toast 提示、2000ms 内再按退出；push 推送消息 Android 直接 Toast

## 构建验证

```bash
./build-x64.sh     # make -j1 单核避竞态 → Built target anystik
# offscreen 冒烟: Storage init complete / [StickerStore] legacy chat tables dropped / 无崩溃
./build-android.sh # APK 重建; 校验 unzip -lv 全部 .so Defl:N（勿回退 STORED）、体积≈48M
```

## 已知限制 / TODO

- Android SAF 目录选择未实现（目录导入提示走「分享到 anystik」；分享图片已入库到「粘贴板」）
- Android 剪贴板图片以 `content://` uri 存储，经 `ClipboardManager`（ShareActivity.readClipboardImageBytes：`ClipDescription.hasMimeType("image/*")` 判定 + try/catch）读取字节入库「粘贴板」；API 33+ 对非本应用写入的剪贴板有遮蔽策略，读不到时提示「剪贴板中没有图片」
- Android 读剪贴板为 UI 线程同步 I/O（方案A）：本地小图流畅，超大图/云盘 content provider 可能有短暂卡顿。候选方案B（未实施）：Java 后台线程读字节 → `runOnAndroidMainThread` → 新增 native `onClipboardReadComplete(byte[])` 回调 → C++ 完成入库并通知首页刷新（需将 `pasteFromClipboard` 同步 API 改造为异步链路）
- `.stik` 自研格式的导入/导出尚未实现（仅本地文件夹扫描）
- Android WebP 动图若无 libwebp 插件则降级首帧
- 网格内 GIF 不逐帧播放（预览层才动图），后续可按需加瓦片 QMovie
- Android `copyStickerToClipboard` 写 FileProvider content URI（image/*），自拷自贴闭环与外部图片粘贴可用；不支持 URI 解析的第三方应用会贴出 URI/PATH 文本（平台语义，非本应用可解）
