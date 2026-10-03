# Vendor Libraries

## uneif (QQ .eif 解包 CFB 实现)

- **Name**: statementreply/uneif
- **Upstream**: https://github.com/statementreply/uneif
- **License**: MIT（`uneif/LICENSE`）
- **Files**:
  - `uneif/cfbf.h`（CFB 头结构 + `EifFile` 类声明，134 行）
  - `uneif/cfbf.cpp`（DIFAT/FAT/MiniFAT/目录/MiniStream 解析 + `unpack` 按目录树落盘，193 行）
- **限界**: 仅 v3 OLE（`SectSize=512` 硬编码、`checkHeader` 拒 `log2SectorSize!=9`）；
  QQ 表情包 .eif 均为 v3，实用无碍。不解 Face.dat（组名/排序），按 CFB 树序导出。
- **C++23 适配**: 上游为 C++17 代码，入库已打补丁（`cfbf.cpp`）：
  `#include <cstring>`（memcmp）+ `u8string` 拼接改为
  `std::string(u8string().begin(), u8string().end())`（C++20 起 `u8string()` 返回
  `basic_string<char8_t>`，`const char* + u8string` 不再编译）。`g++ -std=c++23 -Wall` 通过。
- **用途**: 应用内 .eif 解析的 CFB 底座候选之一（与 `include/compoundfilereader.h` 并行保留，
  实施时以真实样本验证钦点一个为主）

## microsoft/compoundfilereader（CFB 单头读器）

- **Name**: microsoft/compoundfilereader
- **Upstream**: https://github.com/microsoft/compoundfilereader
- **License**: MIT
- **Files**:
  - `include/compoundfilereader.h`（470 行，buffer 原地读，`CFB::CompoundFileReader`）
- **能力**: 支持 v3(512B) + v4(4096B) sector；`EnumFiles`/`ReadFile`/`GetRootEntry`；
  异常 `WrongFormat`/`FileCorrupted`；仅 little-endian；需整体读入内存。
- **用途**: 应用内 .eif 解析的 CFB 底座候选之一（按需读取每个流，便于 Face.dat 驱动命名/排序）

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

## pugixml（DAV 207 解析的 XML 后端）

- **Name**: zeux/pugixml
- **Version**: 1.15（`PUGIXML_VERSION 1150`，已核对 `pugixml.hpp:17`）
- **Upstream**: https://pugixml.org/ （release 包 `pugixml-1.15.tar.gz`）
- **License**: MIT（`pugixml/LICENSE.md`，可商用，仅需保留声明）
- **Files**:
  - `pugixml/pugixml.hpp`（1585 行）
  - `pugixml/pugixml.cpp`（13553 行）
  - `pugixml/pugiconfig.hpp`（80 行）
  - `pugixml/LICENSE.md`
- **引入原因**: Qt 自带 XML API（QDom/QString）在 Qt3 与 Qt5/6 之间语义不一致，
  实测踩到的坑都是**静默产生错误数据**而非报错：①`QString::utf8()` 与
  `latin1()` 共用同一静态转换缓冲区，同一表达式内先后调用互相覆盖；
  ②`QString(const char*)` 在 Qt3 按 Latin-1 解释源文件 UTF-8 字节，"笔记"
  （6 字节）会变成 6 个 U+00xx 字符；③`QDom::elementsByTagName()` 只按本地名
  匹配、忽略命名空间，第三方命名空间同名元素会被误判。pugixml 不经 Qt，
  接口内统一用 `std::string`，Qt3/Qt5/6 行为一致。
- **实测**: 在本项目 Qt3 编译 flags（`-DQT3_BUILD -fPIC -O1`）下零错误通过，
  18s，`.o` 375KB；`load_buffer` 的 `encoding_auto` 使 UTF-8/UTF-16 自动识别，
  中文 href 无损。
- **C++ 标准**: pugixml **本体**支持 C++98 起（已实测 `-std=c++98 -fsyntax-only` 零错误；
  上游称在 VC++6.0~2026 / GCC 3.4~16 上测试，覆盖率 >99%）。不依赖异常
  （可 `PUGIXML_NO_EXCEPTIONS`）、不依赖 RTTI。
  **但 `stikcommon/dav207pugi.cpp` 用了 lambda，实测需 C++11**（`-std=c++98` 报
  "uses local type" 错）。因 anystik 走 CMake 且 `CMAKE_CXX_STANDARD 23`，
  C++11 无压力；真正的收益是**不依赖 Qt API**，与 C++ 标准版本无关。
- **已知限制**: 1.15 **不提供 `namespace_uri()`**（已 grep 确认，全头无 namespace 成员），
  故 `stikcommon/dav207pugi.cpp` 改为「剥前缀后比对本地名 + DAV 前缀白名单」判定命名空间。
- **配套（均在本目录之外，属自研代码，故放 stikcommon 不放 vendor）**:
  `stikcommon/dav207iface.h` 为统一解析契约（`Resource` 结构 + `parseMultiStatus`），
  `stikcommon/dav207pugi.cpp` 为唯一 pugixml 后端实现。
  原先的 `DAV207_USE_NEON` 后端开关宏**已删除**（neon 已否决，见下方条目），不再有第二后端。
- **自测**: 对 `/tmp/opencode/probe.xml`（11 个 response 的真实 207 样本）`fails=0`，
  覆盖：%20 解码、`+` 保留字面量、非法 `%ZZ`/截断 `%2` 整条拒绝（对齐
  `ne_path_unescape` 返回 NULL）、剥 query/fragment/authority、%2F、
  非 DAV 命名空间 collection 不判目录、嵌套 collection 不判目录、
  零 propstat 与全非 2xx 段剔除、中文 href 码点无损。

## neon 0.37.1 —— 已评估并放弃（仅保留行为基准，未入库任何代码）

- **Upstream**: http://www.webdav.org/neon/ ；GitHub 镜像 notroj/neon
- **取用版本**: tag **0.37.1**（commit `170c36704bfc`）。已逐文件 diff 确认
  `ne_207.c` / `ne_207.h` / `ne_xml.h` / `ne_uri.c` / `ne_string.c` /
  `ne_alloc.c` / `ne_utils.c` 在 0.37.1 与 master 分支**逐字节一致**。
- **License**: LGPL v2.1
- **放弃原因（两条约束与 neon 架构互斥，已实测确认）**:
  1. **neon 自己没有 XML 解析器。** `ne_xml.c` 只是 SAX 包装层
     （首行注释即 "Wrapper interface to XML parser"），第 41/56 行分别
     `#if defined(HAVE_EXPAT)` / `#elif defined(HAVE_LIBXML)`，第 66-67 行是
     `#else` + **`# error need an XML parser`**。即：**不给 expat 或 libxml2
     就编译不过**，无兜底实现。
  2. 因此要满足「neon 不依赖外部 XML 库」，唯一出路是自己实现
     `ne_xml.h` 的解析层；而该层要正确处理实体引用、CDATA、DOCTYPE、
     命名空间作用域、良构性检查与增量喂入 —— 这已属于自研 XML 解析器。
  实测：不定义任何 HAVE_* 时 `ne_xml.c:67: #error need an XML parser`；
  定义 `-DHAVE_EXPAT` 后可继续，但仍需 autoconf 生成的 `NE_FMT_SIZE_T`
  等宏（`ne_xml.c:598/607/638`），且构成外部库依赖。
- **另一处结构性障碍**: `ne_207.c` 并非独立单元。实测其 include 为
  `ne_xmlreq.h` / `ne_basic.h` / `ne_internal.h`，且第 300 行起是 HTTP 胶水
  （`ne_accept_207` / `ne_simple_request` / `ne_xml_dispatchif_request` /
  `ne_fill_server_uri`），依赖 `ne_request` / `ne_session`，与本项目已有的
  curl 传输层冲突。状态机本体只有 1-298 行。
  附带：`ne_buffer` 并无独立 .c，实现在 `ne_string.c:121-251`。
- **结论**: 保留 `dav207iface.h` 顶部契约 A–D 作为**行为基准**（下方已核实事实），
  实现只用 pugixml 一个后端。
- **已核实的源码事实**（tag 0.37.1 逐行核对）:
  - `ne_207.c:192-222` `in_response` 由 **href** 置位，非 propstat
  - `ne_207.c:255-265` `if (!p->in_response) break;` → 零 propstat 的 response
    **照样回调** end_response。「零 propstat 不回调」的说法是错的，过滤在 ne_props.c
  - `ne_uri.c:487` `ne_path_unescape()`: 非法 `%XX` → **整条 free 返回 NULL**
    （非原样保留）；纯字节 `strtol(buf,16)` 不校验 UTF-8；非 `%` 字符原样复制，
    故 URI path 中 `+` 是字面量、非空格
  - `ne_207.c:130-142` cdata 累积上限 2048 字节
  - `ne_xml.c:351-356` handler 栈语义：从**父元素的 handler** 起沿 `next`
    向下遍历，返回 `>0` 接受（值即该元素 state）、`0`=DECLINE（剪枝）、
    `<0` 中止解析；`<100` 为 `NE_XML_STATE_TOP` 保留

## doctest（单元测试框架）

- **Name**: doctest/doctest
- **Upstream**: https://github.com/doctest/doctest
- **License**: MIT（`doctest/LICENSE.txt`）
- **版本**: **2.4.11**（`DOCTEST_VERSION_MAJOR/MINOR/PATCH = 2/4/11` 已 grep 核实）
- **Files**:
  - `doctest/doctest.h`（单头，321644 字节，v2.4.11 发布版原文，未改一字）
- **为何是这个版本**：`qldox/run_tests`（2026-09 前遗留二进制，源码已丢）的调试信息里
  编译单元为 `test_main.cpp` + `test_md5.cpp` + `test_emojiutil.cpp` +
  `test_translate_util.cpp` + `test_compat34_time.cpp`，二进制内嵌版本串 `2.4.11`；
  同串 `2.2.5` 经核实是 `GLIBC_2.2.5` 符号版本，可排除。**按同一版本 vendor 以保持一致。**
- **能力**: header-only；`TEST_CASE`/`SUBCASE`/`CHECK_EQ`/`REQUIRE_EQ`；Console 与 JUnit
  XML reporter；命令行过滤（长短双别名，如 `--test-case=`/`-tc=`/`--dt-test-case=`，
  短名定义见 `doctest.h:6614-6630`）
- **已验证与本仓 Qt3 的兼容**：`qldox/run_tests` 链接 `libqt-mt.so.3`（即曾对着 Qt 3.5
  真实编译运行），且本次已用 `-DQT3_BUILD -lqt-mt` 实测编译运行通过 —— Qt 3.5 的
  `signals`/`slots`/`emit` 宏与 doctest 无冲突
- **用途**: `stikcommon/` 与 `qlstik/` 两处单元测试套件的框架。include 路径为
  `anystik/vendor` 根（与 `pugixml` 同一手法），写作 `#include "doctest/doctest.h"`

## uc_apng_loader + stb_image（APNG 解码，批次 5 Qt3 侧实际使用）

- **Name**: uctakeoff/uc_apng_loader（主用） + nothings/stb（仅取 `stb_image.h`）
- **Upstream**: https://github.com/uctakeoff/uc_apng_loader
- **License**: uc_apng_loader 为 MIT（`uc_apng_loader/LICENSE`）；`stb_image.h` 为 public domain
  （v2.30，头部自述 `stb_image - v2.30 - public domain image loader`）
- **Files** (SHA256):
  - `uc_apng_loader/uc_apng_loader.h`（17027 字节，头文件第 4-5 行自述 MIT）
    `126257eeb00cab85b0c685ac20fc8c572309d351c637da90a4c058e1b6278c6b`
  - `uc_apng_loader/stb_image.h`（283010 字节，v2.30）
    `594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3`
  - `uc_apng_loader/LICENSE`（1057 字节）
    `afb11426e09da40a1ae4f8fa17ddcc6b6a52d14df04c29bc5bcd06eb8730624d`
- **能力**: 解析 `acTL`/`fcTL`/`fdAT`；`create_file_loader()`/`create_memory_loader(buf,len)`
  （返回**对象本身**，不是指针）；`width()`/`height()`/`num_frames()`/`num_plays()`；
  `has_frame()`/`next_frame()` → `frame{index, image}`。`frame.image` 恒为**全画布尺寸**
  的已合成结果，合成代码在头文件内：`blend_frame()`（L329，按 `blend_op_t::SOURCE`/OVER 混合）
  与 `next_frame()` 的 `dispose_op_t::NONE`/`BACKGROUND`/`PREVIOUS` 三分支（L413-427）；
  L485-487 实现了规范细节「首帧 `dispose_op==PREVIOUS` 视为 `BACKGROUND`」
- **已实测**（`~/ztprobe/img/apngtest.cpp` / `apngscale.cpp`，PIL 生成 3 帧 40x40 APNG，
  duration 120/80/200ms，loop=0）：`canvas=40x40 frames=3 plays=0` 与 PIL `n_frames=3` 一致；
  每帧输出全画布 40x40；首帧左上 `ff0000ff`、后两帧 `00000000`，与帧内容相符；
  缩放到 20x20 后用 PIL 复核三张 PNG 尺寸/颜色均正确。`-std=c++11` 与 `-std=c++23`
  （项目 `CMAKE_CXX_STANDARD 23`）均编译通过
- **⚠ 上游缺陷：不能喂静态 PNG**。`uc_apng_loader.h:241-248` 的 `image_t(std::vector<uint8_t>&)`
  先调 `stbi_load_from_memory(..., STBI_rgb_alpha)`，再 `UC_APNG_ASSERT(d == BPP)`；
  但 stb 在指定 `req_comp` 强制转换时**不改写** `d`，故 RGB 静态 PNG（`d==3`）必然断言失败，
  抛 `uc::apng::exception: image_t : d == BPP failed.`。**本项目只把它用于 APNG 分支**，
  静态 PNG 仍走 Qt 自己的 `QImageReader`，两者不混用
- **⚠ 不可定义 `UC_APNG_LOADER_NO_EXCEPTION`**：实测该宏下 APNG 静默解析失败
  （`canvas=0x0 frames=0 plays=0`、`decoded=0`，不抛异常也不报警），错误被完全吞掉。
  保持默认异常模式，调用侧用 try/catch 转成错误返回
- **落地三个坑（均实测）**:
  1. 头文件**自身不 include `<cstdint>`**，调用方须先 `#include <cstdint>`，
     否则 `uint8_t`/`uint16_t`/`uint32_t` 全部报错
  2. 需在某 TU 里 `#define STB_IMAGE_IMPLEMENTATION` + `#include "stb_image.h"`，
     否则链接期报 `undefined reference to stbi_image_free`
  3. `create_memory_loader` 返回对象，写 `loader->` 编译失败，须 `loader.`
- **成熟度风险（选型时已知并接受）**: 仅 **11★ / 3 forks**，created 2017-04-08，
  pushed **2024-07-24**，无 OSS-Fuzz 等安全审计、无公开发行版。选它是因为功能完整
  （已含合成，不必自写 150-200 行）且 header-only 体量小（17KB+283KB）
- **为何不选 Firefox `media/libpng/apng.patch`**: 该 patch 只提供帧流 API
  （`png_get_acTL`/`png_read_frame_head`/`png_write_frame_head`/`png_write_frame_tail`），
  `pnginfo.h` 新增字段全是元数据（`num_frames`/`next_frame_dispose_op`/`next_frame_blend_op` 等）、
  **不含已合成画布**，合成仍要自写；且 patch 是针对 Mozilla 内 **libpng 1.6.59** 打的，
  本机系统是 1.6.50，不能直接 apply。上游 `pnggroup/libpng` PR #706 中维护者明确要求
  「disabling APNG handling by default」，长期游离于上游之外。详见 `qlstik/移植计划.md` §6.3k
- **用途**: 批次 5 `stickerstore` 在 Qt3 侧的 APNG 多帧解码（Qt3 无 APNG 动图支持）；
  GIF 走 libnsgif、WebP 走系统 libwebp，见 `qlstik/移植计划.md` §6.3k K.1

## libnsgif（GIF 动图解码，批次 5 Qt3 侧实际使用）

- **Name**: NetSurf libnsgif
- **Upstream**: 官方 git 在 `source.netsurf-browser.org/libnsgif.git`（**不在 GitHub**）。
  入库取自镜像 `netsurf-plan9/libnsgif` 的 `master` 分支 commit `e97bc7b86f`
- **License**: MIT（`libnsgif/COPYING`；© 2004 Richard Wilson / © 2008 Sean Fox /
  © 2013-2021 Michael Drake）
- **为何这样取**：官方源是 git 而非 tarball 快照，GitHub 镜像 `netsurf-plan9/libnsgif`
  仅 9★ 且 push 停在 2024-03-29，**未跟进官方 master**。镜像仓库的文件路径与官方不同
  （镜像用 `src/gif.c`、`include/nsgif.h`，而非常见的 `libnsgif.c`），故按镜像实际布局入库。
  文件名以镜像为准，源码是上游 NetSurf 原件。GitHub 上 `NetSurf/libnsgif`、
  `giflib/giflib` 均返回 404
- **Files** (SHA256):
  - `libnsgif/gif.c`（50874 字节，**注意不叫 `libnsgif.c`**）
    `e9d5fe0e63b10d9be603e1ad2f1fede0a5acf0909bc17c8c7911b776f0a0a1af`
  - `libnsgif/lzw.c`（16711 字节）
    `55ee58985717ad622c42ac853d241470ed48fbf9d78820ac497a29c83a5bf794`
  - `libnsgif/lzw.h`（4878 字节）
    `6191b797a275e9c250aad591853bc2825593bb182b5002b7ae1cd9e50329a6f2`
  - `libnsgif/nsgif.h`（15318 字节）
    `586ed21a3e12d6b0cd423af262fb6beee4f0e45b848672040505cb5d395d71fa`
  - `libnsgif/COPYING`（1133 字节）
    `1469b759cf18e43c6e1b4ff892307d3962cbbb337ac497620d6690a219fad10c`
- **零外部依赖（已核对全部 include）**：`gif.c`/`lzw.c` 只 include
  `<assert.h> <stdint.h> <stdlib.h> <string.h> <stdbool.h>` + 本目录 `lzw.h`/`nsgif.h`；
  `nsgif.h` 只 include `<stdint.h> <stdbool.h> <inttypes.h>`。C99 即可编译
- **API 范式（与官方 README 一致，两步式）**:
  `nsgif_create(&vt, fmt, &gif)` → `nsgif_data_scan(gif, size, data)`（可多次喂增量数据）
  → `nsgif_data_complete(gif)` → `nsgif_get_info(gif)` 取 `const nsgif_info_t*`
  （`width/height/frame_count/loop_max/background/global_palette`）
  → 循环 `nsgif_frame_prepare(gif,&area,&delay,&frame)` + `nsgif_frame_decode(gif,frame,bitmap)`
- **⚠ 三个 API 陷阱（实测踩过，务必先读）**:
  1. **`nsgif_frame_decode` 的 bitmap 参数是二级指针** `nsgif_bitmap_t **bitmap`
     （`gif.c:1947`），库会**自己创建并回填** bitmap。传单层指针会让库写进调用方的
     栈上临时变量，随后读另一个 buffer 就得到 `905c1aef` 之类垃圾值——极易误判成
     库有 bug。正确写法：`nsgif_bitmap_t* bmp=NULL; nsgif_frame_decode(gif,frame,&bmp);`
     之后用 `bmp`（库持有，勿 free）
  2. **`delay` 是 GIF 原始单位（1/100 秒），不是毫秒**。`gif.c:774`
     `frame->info.delay = data[3] | (data[4] << 8)` 直接取 GCE 两字节小端。
     实测对照：`t3.gif` 文件 GCE 字节为 `0c`/`08`/`14`，libnsgif 报 12/8/20，
     而 stb_image 报 120/80/200（它内部已 ×10）。`stickerstore` 现有 delay 语义是
     **毫秒**，接 libnsgif 必须 **×10**
  3. **`nsgif_frame_prepare()` 不会自己停**：`loop_max == 0`（永久循环）时永远返回
     `NSGIF_OK` 并从第 0 帧重来，实测 3 帧 GIF 无限吐帧。须在拿到
     `NSGIF_ERR_ANIMATION_END`（仅有限循环结束时出现）**或自行按 `loop_max` 计数退出**
- **与其他解码器的实测对照**（同一批文件，40x40）：
  `t3.gif`（无 disposal）与 `d2.gif`（`disposal=2`）两者的**像素与 stb_image v2.30、
  PIL 完全一致**，含 disposal=2 的透明处理（帧1 `(0,0)=000000ff` 透明、`(15,15)=00ff00ff` 绿）。
  唯一差异就是上面第 2 条的 delay 单位
- **stb_image 也能解 GIF 动画**（`stb_image.h:433` 有 `stbi_load_gif_from_memory`，
  返回紧凑排列的 `frames×w×h×4` RGBA + `int** delays` 毫秒数组，两个 buffer 都要
  `stbi_image_free`，链接需 `-lm`）。本项目仍选 libnsgif 而非 stb_image 解 GIF，
  原因见下条 stb 维护者立场
- **已实测**（`/tmp/vtest/nsgiftest.c`，`-std=c99 -O2 -Wall`，PIL 生成 3 帧 40x40 GIF，
  duration 120/80/200ms、loop=0）：`canvas=40x40 frames=3 loop=0` 与 PIL `n_frames=3` 一致；
  三帧 `delay` 分别回读为 **12/8/20ms**（GIF 时间单位是 10ms，120ms→12 正确）；
  第 3 帧 `redraw` 区域为 **32x32**，正确反映了该帧的局部尺寸（前两帧为全画布 40x40）
- **成熟度**: GitHub star 数不具参考性（官方源不在 GitHub）。真实采用证据：
  **libvips**（11697★）、**OBS Studio**（76826★）、**GEGL**、**NetSurf 浏览器** 均内置。
  维护者 Michael Drake 是 NetSurf 核心开发者
- **用途**: 批次 5 `stickerstore` 在 Qt3 侧的 GIF 多帧解码（Qt 3.5.0 只有 `libqmng.so`
  + `libqjpeg.so`，实测 gif 解码失败）。GIF **编码**侧沿用现有 `vendor/tangora_gif.h`
  （纯编码器，`GifBegin`/`GifWriteFrame`/`GifEnd`）。见 `qlstik/移植计划.md` §6.3k

## stb_image（含 GIF 动画，评估参考）

- **Name**: nothings/stb
- **File**: `uc_apng_loader/stb_image.h` 已入库（v2.30，public domain）
- **API（GIF 动画）**: `stbi_load_gif_from_memory(buf, len, int **delays, int *x, int *y,
  int *z, int *comp, int req_comp)`，其中 `*z` 是帧数，`**delays` 返回每帧时长
  数组（**毫秒**）。返回值是紧凑排列的 `frames × w × h × 4` RGBA，两个 buffer
  均须用 `stbi_image_free()` 释放，链接需 `-lm`
- **实测对照**（与 libnsgif/PIL）：像素数据完全一致（包括 `disposal=2` 的透明处理）。
  **唯一差异**：delay 单位 —— libnsgif 返回 GIF 原始 10ms 单位（GCE 两字节小端÷10），
  stb_image 返回**毫秒**
- **stb 维护者立场（重要）**：`nothings/stb` issue #1568（作者本人）明确表示
  _"The plan is to remove all support for animated gifs from stb_image and fork them
  into a separate library (which I personally will not be maintaining), because this
  has just been a source of headaches and it's really outside the original intent of
  stb_image."_ 此外 issue #1688（2024-09）还报告 disposal method 2/3 的处理有 PR
  未合并，说明动画 GIF 路径并非 stb 的长期维护重心
- **项目选型结论**：GIF 动图解码选用 **libnsgif**（NetSurf 官方、MIT、被 libvips/OBS/GEGL
  广泛采用，API 明确、维护活跃于 NetSurf 生态）。stb_image 的 GIF 动画实现**不选作主路线**，
  仅在此作为对照验证之用（已实测可用）
