# Vendor Libraries

## libobfuscate

- Name: libobfuscate (adamyaxley/Obfuscate)
- Version: upstream (C++14 compile-time string obfuscation, header-only)
- Upstream: https://github.com/adamyaxley/Obfuscate
- License: Unlicense
- Files:
  - `include/libobfuscate/obfuscate.h` (271 lines, XOR compile-time encryption via `AY_OBFUSCATE`)
  - 来源：vcpkg `libobfuscate` port（x64-linux-dynamic triplet 已装同版本）
- 用途：`src/davobfus.cpp` 用 `AY_OBFUSCATE` 编译期混淆 dav 鉴权 key（占位 `AUTHKEY_PLACEHOLDER`）
- 注意：单字节 XOR 为缓解非绝对安全；运行时解码后内存仍可被逆向提取。

## ADVobfuscator（已 vendor，Release 构建实际使用）

- **Name**: ADVobfuscator (andrivet/ADVobfuscator)，**v2.1.2**
- **Upstream**: https://github.com/andrivet/ADVobfuscator
- **License**: BSD-3-Clause-Clear（与 libobfuscate 的 Unlicense 不同）
- **结构**: header-only、**多文件**——`include/advobfuscator/` 下 9 个头（含相互 include）：
  `aes.h`, `aes_string.h`, `bytes.h`, `call.h`, `format.h`, `fsm.h`, `obf.h`, `random.h`, `string.h`
- **加密**: 编译期加密 + FSM 混淆运行期解码调用（`call.h`/`fsm.h` 的 `ObfuscatedMethodCall`、
  `ObfuscatedCall`）；`string.h` 的多算法链（XOR/CAESAR/ROTATE/SUBSTITUTE）+ `aes_string.h` 的
  AES-128-CTR，比 libobfuscate 的单字节 XOR 强度高
- **C++ 标准**: **必须 C++20**（`consteval`、class-NTTP UDL `template<ObfuscatedString str> operator""_obf`、
  constexpr 析构）。**项目已 `CMAKE_CXX_STANDARD 23`（CMakeLists.txt:4），满足**
- **Files** (SHA256):
  - `include/advobfuscator/aes.h` (`6c1efe45a4b1b0e9162e8ffc2c24c73259d9e5b67f360799e5575db5c59b9155`)
  - `include/advobfuscator/aes_string.h` (`3d495c17dcfaaae1a958cfb4e201b0d8ae8da8c79d8471c5759e39362b323d47`)
  - `include/advobfuscator/bytes.h` (`6c4eca412b422689f834cd01ed39cfcdabe9bb34511cd9f0f50a0057e80fbbf5`)
  - `include/advobfuscator/call.h` (`5e11f8d7639475d0284ab05e7263cb6d770799bd78ac3a098fefe31e78ce69c7`)
  - `include/advobfuscator/format.h` (`46174781ae38b370f75d6b549721fb0249bacafd9c0469914318b94e00217451`)
  - `include/advobfuscator/fsm.h` (`0be1d216ba50693b26d7a7ff1e015bd290283c884a36e925a7f9ddeb37759492`)
  - `include/advobfuscator/obf.h` (`07b99a8a2859576e4fecb8756ae43f3f97dc79ec2a0e1182d3a7474133769118`)
  - `include/advobfuscator/random.h` (`a50caffb578daa7070ebb9bffc28adc03ef543ef984d6275da0d191531275ac6`)
  - `include/advobfuscator/string.h` (`ec7838c939663fd70bea72a548ef8ede445fca8c4ddc9382e67a66aa3e838ec6`)
  - 来源：GitHub tag v2.1.2 `include/advobfuscator/`（经 gh-proxy.org 逐文件下载）
- **用途**: dav 鉴权 key 混淆方案，**双轨实现**——构建期按信号自动选择：
  - Debug/等同调试型（`_DEBUG` 或 `!NDEBUG` 或 `QT_DEBUG` 任一命中）→ libobfuscate `AY_OBFUSCATE`
  - Release（三信号均不命中，含 Android `RelWithDebInfo`）→ ADVobfuscator `""_obf`
  - 见 `src/davobfus.cpp` 与 `src/davobfus.cpp.tmpl`
- **Debug 构建限制**: upstream README 明确「Obfuscation works only for Release builds」，
  Debug 下明文照常进二进制。本项目 x64 构建用 `-DCMAKE_BUILD_TYPE=Debug`（+`-O1`）
  → 走 libobfuscate 分支；Android 构建 `RelWithDebInfo`（含 `-DNDEBUG -DQT_NO_DEBUG`）
  → 走 ADVobfuscator 分支
- **必须用 `_obf` UDL，勿用显式构造**: 实测 `andrivet::advobfuscator::ObfuscatedString("...")`
  会把字面量当运行时参数留在 `.rodata` 中**泄露明文**（-O1/-O2/-O3 均复现）；
  `"..."_obf` 把 ObfuscatedString 整体作为 class-NTTP 模板实参，编译期加密后仅存编码字节
  （g++/clang++ -std=c++23 -O0~-O3 全组合验证 `strings` 无明文）。需 `using namespace andrivet::advobfuscator;`
  使 UDL 可见。`DAVOBFUS_KEY""_obf`（宏展开后邻接字面量接 UDL）可用，key 保持单源宏定义
- **决策记录**: x64 Debug 不生效 → 采用**双轨**：Debug 用 libobfuscate（全构建生效），
  Release/RelWithDebInfo 用 ADVobfuscator（混淆更强）。检测条件取三信号 OR，宁保守不露明文。
  勿把 UDL 对象赋给 `const char*`（语句结束析构置零导致悬垂指针），
  须经隐式转换 `const char*` 后 `QString::fromUtf8(key)`

## cJSON

- Version: 1.7.19
- Upstream: https://github.com/DaveGamble/cJSON
- Files:
  - `cJSON.c` (SHA256: `607e756460fa0de37d20a7a9181f2de29c97bfb7ce5a0e6c2f548243836cd852`)
  - `cJSON.h` (SHA256: `25b0145150d500498e4d209cec69c18c42cf818bffcc54690be3b895a2a16dee`)

## SQLite

- Version: 3.53.3
- Android arm64 prebuilt .so:
  - Download: https://github.com/simolus3/sqlite3.dart/releases/download/sqlite3-3.4.0/libsqlite3.arm64.android.so
  - Built by: NDK r29 (14206865), Android API 24+
  - File: `lib/arm64-v8a/libsqlite3.so` (SHA256: `e99515af1d7119fb61843ae5e597344e7f258563de3a7e5a3869f627aab2887b`)
- Header (sqlite3.h):
  - Download: https://raw.githubusercontent.com/rhuijben/sqlite-amalgamation/master/sqlite3.h
  - File: `sqlite3.h` (SHA256: `4ff81af4849acabc76fc8349abb926814395072617ca18e08800abf734ab7612`)

## curl + OpenSSL

- curl: 8.19.0
- OpenSSL: 3.6.x
- Source: https://github.com/XDcobra/libcurl-ios-android-prebuilt-and-buildscripts/releases/tag/v8.19.0-1
- Android arm64 prebuilt .so:
  - zip 内来源目录：`libcurl-openssl/jniLibs/arm64-v8a/`（openssl 后端变体；
    包内可能并存其它 SSL 后端变体，取库须限定此目录）
  - `lib/arm64-v8a/libcurl.so` (SHA256: `5ceb34ff92d9f6cd6b28901cc220bc2917a53e2614e8c9f6764af18c89063b88`)
  - `lib/arm64-v8a/libssl.so` (SHA256: `96d844acd9b264face6529b3502269577c1c14843cef4b55031deb114db8f0a7`)
  - `lib/arm64-v8a/libcrypto.so` (SHA256: `953e2c534771b09e022bf4ef3d7d3ff4c18ba10241fb430036d1147399a28a90`)
- Headers:
  - `include/curl/` (curl 头文件)
  - 注：v8.19.0-1 包内不含 OpenSSL 头，本地 `include/openssl/` 为另行放置；
    qsktox 无直接 `#include <openssl/>`，CI 缺此头不影响编译链接
- 此目录不入库：CI 于构建时经 `scripts/ci_prebuild_qsktox_vendor.sh` 自动下载归位并校验 SHA256

## UnifiedPush android-connector

- Version: 3.3.3
- Source: Maven Central (`org.unifiedpush.android:connector:3.3.3`)
- Upstream: https://github.com/UnifiedPush/android-connector
- License: Apache 2.0
- Min Android: 4.1 (API 16)，qsktox minSdkVersion 23 完全兼容
- 依赖：`com.google.crypto.tink:tink-android:1.20.0`（需 resolutionStrategy 解决冲突）
- 集成方式：Gradle 依赖（非预编译 .so）
- Java 层：`PushServiceImpl extends PushService`（`android/src/java/io/fedlet/mobutil/PushServiceImpl.java`）
- C++ 层：`pushhandler.h/cpp`，JNI 桥接
- AndroidManifest.xml：声明 `PushServiceImpl`（action: `org.unifiedpush.android.connector.PUSH_EVENT`）
- 注册流程：`UnifiedPush.tryUseCurrentOrDefaultDistributor()` → callback → `UnifiedPush.register(context, INSTANCE_DEFAULT, ...)`
- Go 服务端需实现 Web Push 发送（`webpush-go` 库），但当前不在 qsktox 范围内
- 推送测试（ntfy.sh）：
  - 简单推送：
    ```bash
    curl -d "Hello from qsktox" https://ntfy.sh/mytopic
    ```
  - 带标题和优先级：
    ```bash
    curl -H "Title: qsktox Push" -H "Priority: high" -d "New message" https://ntfy.sh/mytopic
    ```
  - 带 action broadcast（触发 BroadcastReceiver）：
    ```bash
    curl -H "Title: New Message" \
         -H "Actions: broadcast, Open qsktox, intent=io.fedlet.qsktox.PUSH_RECEIVED, extras.cmd=open" \
         -d "You have a new message" \
         https://ntfy.sh/mytopic
    ```
   - JSON 格式：
    ```bash
    curl -H "Content-Type: application/json" \
         -d '{"topic":"mytopic","message":"New message","title":"qsktox","priority":4}' \
         https://ntfy.sh
    ```

## UnifiedPush 字符串说明

### 三个关键字符串

| 字符串 | 代码位置 | 含义 | 示例 |
|--------|----------|------|------|
| **connection token** | `UnifiedPush.register(context, token)` 第二参数 | App 标识符，distributor 用它匹配回调到对应 app | UUIDv4（如 `a1b2c3d4-e5f6-...`） |
| **instance** | `PushServiceImpl.onNewEndpoint(endpoint, instance)` 的 instance 参数 | 同 connection token，回调中原样返回 | 同上 |
| **topic** | ntfy 服务端生成，包含在 endpoint URL 路径中 | ntfy 上的频道名，推送方 POST 到此 topic | `upAbCdEfGh1234` |

### 单注册模式（per-device UUID）

每个设备注册一次，使用随机生成的 UUIDv4 作为 token：

| instance | topic | 用途 | 谁推送 |
|----------|-------|------|--------|
| UUIDv4 | `upXyZaBcDeF5678`（随机） | per-device 定向推送 | Go 服务端知道此 endpoint |

**安全设计**：
- Token 由 `QUuid::createUuid()` 生成，符合 UnifiedPush spec 推荐的 UUIDv4 格式
- 每设备独立 topic，endpoint URL 不可猜测（capability URL 模型）
- 开源代码中不暴露任何硬编码 token，避免 DDoS/消息伪造风险

### 调用链路

```
启动 → 读取/生成 UUID → 保存到 QSettings("pushDeviceToken")
      ↓
register(context, UUID)               → 分配 topic → 回调 onNewEndpoint(endpoint, UUID)
      ↓
onNewEndpoint: 保存 pushDeviceEndpoint = endpoint
      ↓
推送方 POST 到 endpoint URL:
  curl -d "targeted" https://ntfy.sh/upXyZaBcDeF5678?up=1
```

### QSettings 存储

| key | 值 | 说明 |
|-----|-----|------|
| `pushDeviceToken` | UUID 如 `a1b2c3d4-e5f6-...` | per-device 注册 token，重启复用 |
| `pushDeviceEndpoint` | `https://ntfy.sh/upXyZaBcDeF5678?up=1` | per-device 的 endpoint URL |

### ntfy 服务端对 UnifiedPush topic 的硬编码约束

```go
// ntfy/server/server.go
unifiedPushTopicPrefix = "up"   // 必须以 "up" 开头
unifiedPushTopicLength = 14     // 总长度必须 14 字符（含 "up"）
```

- topic 格式：`up` + 12字符随机串（如 `upAbCdEfGh1234`）
- **不可自定义** topic 名称（如 `io.fedlet.pushto.user9` 不满足约束）
- 此限制来自 ntfy 公共服务器，自建 ntfy 也可能继承

### 总结

| 概念 | 谁控制 | 可自定义？ |
|------|--------|-----------|
| connection token / instance | App 端 | **是**，per-device UUIDv4（随机生成） |
| topic | ntfy 服务端 | **否**，随机生成 |
| endpoint URL | ntfy 服务端 | **否**，格式为 `{server}/{topic}?up=1` |
