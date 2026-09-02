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
- 剪贴板：桌面 GIF 走 `image/gif` 原始字节（mac 另补 `com.compuserve.gif`/`public.gif` UTI＋`public.file-url`），其它格式 `setImage`；Android JNI `ShareActivity.copyImageToClipboard`（FileProvider content URI）+ Toast
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
    → Desktop(GIF): image/gif 原始多帧字节 + PNG 位图回退；
        mac 另写 com.compuserve.gif/public.gif UTI + public.file-url 文件引用（file<->url 双通道）
    → Desktop(其它): setImage(QImage(path))（PNG）
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
| 剪贴板复制 | GIF：`image/gif` 原字节（mac 补 GIF UTI＋file-url）；其它 `setImage` | JNI `copyImageToClipboard`（FileProvider URI） + Toast |
| GIF 动图 | 网格=首帧；预览=QMovie 全帧 | 同左 |
| WebP 动图 | Qt6 图像插件 | 缺 libwebp 时降级静态首帧 |

## GIF 粘贴处理：跨平台链路与 Qt/macOS 限制

### 平台矩阵

| 平台 | 粘贴链路 | 动画保留 |
|------|----------|----------|
| Linux | image/gif 原字节 或 text/uri-list 本地文件引用 | ✅ 源提供动图字节即保留 |
| macOS | MIME 循环 → QUtiMimeConverter（gif UTI→image/gif） → 位图兜底 | ⚠️ 浏览器拷贝本就得静态（见下） |
| Android | content:// Uri 原字节读取 | ✅ 剪贴板可读前提下 |

存储（贴纸 id = SHA1(全量原字节) 入库）与预览（网格首帧、预览层 QMovie→QImageReader 全帧）三平台共用，动画无损。

### 三层成因（为何 macOS 常读不到完整 GIF 动画）

1. **源 App 不把动画字节放上剪贴板（主因，应用层不可逆）**
   - NSPasteboard 无标准 GIF 类型（标准类型仅 tiff/png/jpeg/pdf/…；GIF 仅 UTI `com.compuserve.gif`/`public.gif`）
   - 实测/权威来源：Chrome(mac) "Copy Image" 落**静态 PNG**+URL（Clipboard Inspector、Photocopier README「Chrome copies GIFs as flat images」）；Safari 拷成 **RTFD** 或仅 URL（text/uri-list + text/html）；基于 NSImage 写剪贴板一律单帧 TIFF/PNG（[WebKit bug 190101：GIF 动画只保留首帧](https://bugs.webkit.org/show_bug.cgi?id=190101)）
   - Linux 浏览器拷贝图片同为静态 PNG，故「浏览器拷贝动图→静态」非本应用缺陷
2. **Qt 无 GIF UTI↔MIME 映射**：[QUtiMimeConverter](https://doc.qt.io/qt-6/qmacmimedata.html) 预置 UTI 仅 text/html/url/file-url/tiff/pict/vcard，不含 `com.compuserve.gif`/`public.gif`；未注册的 UTI 不保证出现在 `mime->formats()`，即使存在也无转换器把 `mime->data("image/gif")` 喂出字节
3. **API 单帧限制**：`QClipboard::image()` 只返回单帧 QImage；完整动画必须拿到原始字节再用 QImageReader/QMovie 播放

### 实现对照（本仓库）

| 环节 | 位置 |
|------|------|
| 候选 MIME 循环（动画优先 gif/webp/apng/png/…） | `stickerstore.cpp:434-450` |
| text/uri-list 本地文件引用（两段式校验） | `stickerstore.cpp:451-474` |
| macOS 原始 GIF UTI 映射（QUtiMimeConverter，默认） | `macgifconverter.h/.cpp` + `stickerstore.cpp`（ensureMacGifConverter，MIME 循环前） |
| 旧方案 macOS NSPasteboard 直读（同时编译，运行时切换） | `macpasteboard.mm`；环境变量 `ANYS_USE_MM_PASTEBOARD=1` 启用 |
| 原字节验签入库（probeImageValidity/importImageBytes） | `stickerstore.cpp`（:233 附近起） |
| 通用动画预览（QMovie→QImageReader 逐帧→静态） | `stickerpreviewoverlay.cpp` |
| Android 原字节读取 | `ShareActivity.readClipboardImageBytes/readUriBytes` |

### 已知限制

- mac 浏览器/部分应用拷贝 GIF 时，源端常把动画重编码成**单帧**写进 `image/gif` 数据，同时保留 `text/uri-list` 指向原始多帧文件。已实现回退：检测到剪贴板 GIF 单帧（`frames=1`）且 uri 指向本地多帧 GIF 时，改读原始文件（`source=uri-gif-fallback`）。若源端连本地文件引用都不给（纯网页拷贝），剪贴板层不可恢复
- mac 复制出已写 `com.compuserve.gif`/`public.gif` UTI 原始字节并附 `public.file-url` 文件引用（`MacGifUtiConverter::convertFromMime` 写方向启用）；仅读图片通道（TIFF/PNG）的接收方仍落回静态首帧——系统级行为，非本应用可绕
- Android 剪贴板 `text/uri-list`（文本型）未解析，仅日志
- WebP 动图缺 libwebp 插件时降级静态首帧；APNG/AVIF 需对应 Qt 图像插件
- Android API 33+ 非本应用写入的剪贴板有遮蔽策略；读剪贴板为 UI 线程同步 I/O（既有 TODO）

### [StickerPaste] 日志速查

| 日志 | 含义 |
|------|------|
| `mime formats: …` | 剪贴板暴露的 MIME 全集（mac 排查关键：能确认源到底写了什么） |
| `text/uri-list=…` / `uri[file/remote]…` | mac 诊断：源端原始文件引用（`file://` 可回退；`https://`=纯网页拷贝，源端限制不可救） |
| `source=mime type=…` | 命中某 MIME 的原字节 |
| `source=mime type=…(gif) … frames=… backend=…` | GIF 分支元信息行恒含后端；新增 `frames=`（`QImageReader::imageCount`）用于核对剪贴板 GIF 是否已被源端压成单帧 |
| `source=uri-gif-fallback path=… … frames=…` | 剪贴板 GIF 为单帧时回退成功读取 uri 本地多帧原始 GIF（动画恢复点） |
| `source=uri path=…` | text/uri-list 本地文件读取成功（常规空字节路径） |
| `source=macpb type=…` | NSPasteboard 直读抢救成功（mac 专有；现含 `frames=`，单帧时同走 uri-gif-fallback 回退） |
| `source=bitmap …` | 位图兜底 PNG（动画必然丢失处） |
| `import ok fmt=…` | 入库成功 |

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

# 表情包目录（自带地址下载）

## 目标

新增「表情包目录」页（main.cpp `registerPage("bundledpacks")`），从**代码硬编码**的 4 个内置地址下载贴纸包（断点续传 + 大小显示），并管理已下载包（启用/停用、卸载、彻底删除）；版本（commit/ETag/Last-Modified）与 MD5 持久化于 QSettings。改动集中：`bundledpackspage.{h,cpp}` 新增 + `stickerstore.{h,cpp}` 扩展 + `sticker_db.{h,cpp}`（qldox）加 `update_pack_installed` + 两处接线（菜单项/registerPage/CMake）。

## 内置下载源（唯一改源点 = bundledpackspage.cpp 内 kSources）

| 名称 | URL | 说明 |
|---|---|---|
| WhatsApp 官方示例贴纸 (SDK) | `https://codeload.github.com/WhatsApp/stickers/zip/refs/heads/main` | **SDK 仓库**，含 67 webp+100 png 官方示例贴纸；codeload 响应为 chunked 无 Content-Length → 行内显示「大小未知（以下载实计）」；版本解析走 GitHub API `commits/<branch>` sha（匿名限流 60/h） |
| Animals (Telegram) | `https://raw.githubusercontent.com/kanelai/stickerapp/master/Animals.stickerpack` | TG 生态唯一稳定直链整包，206+Range 齐全 → 支持续传 |
| LINE 贴纸 2938 | `https://stickershop.line-scdn.net/stickershop/v1/product/2938/iphone/stickers@2x.zip` | LINE 静态包（静态域名优先静态 CDN） |
| LINE 动态 18060 | `https://stickershop.line-scdn.net/stickershop/v1/product/18060/iphone/stickerpack@2x.zip` | LINE 动态包（readsnippet→packageId 已在客户端侧完成） |

LINE CDN 本沙箱环境不可达，仅真机可验证；失败一律错误提示 + 可重试。

## 元数据模型 v3（无临时元文件）

- 磁盘下载区 `dataDir/packs/.download/` **仅一个文件** `<md5(url)前16>.part`（文件名即 URL 指纹）；无 .meta 临时文件。
- 下载中状态在内存 `DownloadTask{url, partPath, out, reply, offset, total, name, cancelled}`；resume 提示低频落盘 `QSettings["dlProgress/<urlhash>"] = {name, total}`（offset 权威值取 .part 实际大小）。
- 安装成功后持久化：`QSettings["downloadedPacks"]`（QStringList<packId>，A 区成员）+ `QSettings["downloadedPackMeta/<packId>"] = {url, name, total, version, versionRaw, md5, dir, dl_time}`。
- 版本 `version`：codeload 源 → 异步 `GET api.github.com/repos/<o>/<r>/commits/<branch>` 取 sha；raw → ETag/Last-Modified；LINE → Last-Modified；无 → 「未知」。A 区显示 commit 前 7 + md5 前 8。
- MD5：安装时**重读完整 zip 流式计算**（跨续传会话正确，与 `extras/go/md5.c` 无关）。
- 下载完成（probe 版本一致且已装）→ B 区「已装且未变化」标语；同 commit 重下 MD5 不一 → 报告内容变化。

## 续传状态机（StickerStore::downloadPack/startDownload/handleDownloadFinished）

- `.part` 已存在 → `Range: bytes=N-`；206 续写（Content-Range 解析 total）；200 且 offset>0（服务端忽略 Range）→ 删 .part 以 noRange 全量重下；200 且 offset==0 → 全量（CL 取 total）；416 → 复位重下。
- 完成条件：`total==-1` 或 `size >= total`；中断/取消保留 .part 供「继续」。
- 并发门闩：B 区仅允许同时下载一个（`m_busyUrl`），其余行按钮禁用。
- 网络栈复用 `QNetworkAccessManager`；请求带 `User-Agent: anystik/<ver>` + codeload 源加 `Accept: application/vnd.github+json`。

## 安装流程（installDownload）

part → zip rename → `QZipReader`（`<QtCore/private/qzipreader_p.h>`，需链 `Qt6::CorePrivate`）→ 标题 = zip 顶层唯一目录名，否则 URL 派生名 → `QDir(dataDir/packs/<sanitized 标题>).removeRecursively()` 重建 → `extractAll` → `importDirectory(targetDir)`（标题=目录名，findOrCreatePack 同标题复用/新建）→ 扫 `list_packs(-1)` 按标题取 packId → 写 QSettings → 删 zip → `dataChanged()` + `downloadFinished(url, true, 标题)`。

## A 区管理行（BundledPacksPage）

元数据来自 packMeta(packId)；行内按钮：
- **启用/停用**：`setPackInstalled(id, bool)` → qldox `update_pack_installed`（`UPDATE sticker_packs SET installed=?2 WHERE id=?1`）。
- **卸载**：`uninstallPack(id, false)` → `delete_pack`，保留图片文件与 meta（可再导入）。
- **彻底删除**：`uninstallPack(id, true)` → 删分组 + `dataDir/packs/<dir>` 目录 + `downloadedPackMeta/<id>` 记录。
- 两操作均先 `qskDialog->question` 确认。

## 接线

- 菜单：stickerhomepage.cpp showOptionsMenu 在「导入表情包文件夹」「表情包目录」「粘贴添加」（idxBundled=1）/「分组管理」（idxManage=3）→ `pageManager()->open("bundledpacks")`。
- main.cpp：`registerPage("bundledpacks", new BundledPacksPage, {Transient, Standard})`。
- CMakeLists：新增 `src/bundledpackspage.{h,cpp}`；`target_link_libraries` 加 `Qt6::CorePrivate`（qzipreader）。
- qldox：`sticker_packs` 表新方法 `update_pack_installed`（toxhttpd/qltox/sticker_db.{h,cpp}）。

## 构建验证（本沙箱）

```bash
./build-x64.sh     # sed hotfix 去除 -lQt6Qml -lQt6Quick -lQt6OpenGL（防误链系统 Qt）→ Built target anystik
./build-android.sh # BUILD SUCCESSFUL in 1m35s（StickerStore 扩展 + 新页面全量编译）
```

实测原始数据：codeload 仓库 git 对象统计 ≈10.2MB；沙箱限速 ~20KB/s 下 900s 截断 13,163,057B（zip 未压缩 tree 合计 18,809,384B，预计整包 ≈15–18MB）；Animals 1,088,205B（206+Range）。

## 加固修复（A/B 轮）

- **A1 安装不阻塞 GUI**：`installDownload` 拆除为三段式——`runInstall`（GUI 线程启动）→ `runInstallWork`（`QtConcurrent::run` 工作线程：rename/fileMd5/解压/importDirectory/安全扫描/A2 对比）→ `finalizeInstall`（`future.then(this,…)` 回 GUI 线程写 QSettings、发信号、清理 task）。`DownloadTask::installing` 期间 `cancelDownload` 静默忽略；UI 在 `done>=total` 时显示「下载完成，正在安装…」。数据库并发由 `SqliteConnectionSafe` 的 `pthread_rwlock` 序列化（storage.h:135-156），无死锁。
- **A2 内容变化检测**：安装完成后若同源同 `downloadedPackMeta/<packId>` 且新旧 md5 不同，成功文案附「（远端内容已变化，已覆盖安装）」。
- **A3 卸载语义**：卸载（保留文件）从 `downloadedPacks` 移除但保留 meta；「彻底删除」才连 meta+文件一并清；probe 的「已装且未变化」仅对该包仍启用时显示。
- **A4 续传偏移校验**：206 分支解析 `Content-Range` 起始字节，与请求 `offset` 不符 → 删 `.part` 自动重下。
- **B1 zip 安全预扫**：解压前检查 `fileInfoList`，拒绝符号链接、绝对路径、`..` 父目录穿越、盘符冒号条目（源可信，硬拦）。
- **B2 残留下载清理**：`StickerStore::cleanupAbandonedDownloads(knownUrls)` 删除指纹不属于内置源的 `*.part` 并清 `dlProgress` 死条目；页面进入时调用。
- **B3 操作反馈**：卸载/彻底删除成功补 toast。
- 依赖：CMakeLists 追加 `Qt6::Concurrent`；`main.cpp` 增加 `ANYSTIK_SELFTEST=1` 自检入口（offscreen 自动下载安装内置源贴纸 zip 并打印结果退出，用于无头冒烟）。自检实测额外修复一个崩溃：`downloadPack` 原先未调用 `ensureNam()`，直接下载时 `m_nam` 为空指针崩溃（UI 流程先 probe 后下载故未触发）；已在 `startDownload` 开头防御性调用。

## 已知限制 / TODO

- LINE CDN 两条地址沙箱不可达，需真机验证（含断点续传）。
- codeload 精确全长未知（无 CL、chunked），以下载字节实计。
- Telegram 生态无稳定第三方整包静态直链（swim233/oracle.swimgit.top:8070 确认宕机），仅 Animals 覆盖；可自建直链后加 kSources 常量。
- GitHub API commit 解析匿名额 60/h，超限时版本回落「未知」。
- Android 真机验证待做（下载/续传/安装/卸载全链路）。
- **偏差记录（D）**：①「version 一致且已装」仅提示「已装且未变化」，不自动跳过；②下载采用**全局单并发门闩**（一次仅一个下载任务，非行内并发）；③运行时冒烟受沙箱限制：LINE 源与 Android 真机链路未验证，仅桌面 offscreen 自检（`ANYSTIK_SELFTEST=1`）+ 编译冒烟；④codeload SDK 包会将仓库全部源码文件解出到 `dataDir/packs/`（仅图片入库），磁盘占用偏大。
