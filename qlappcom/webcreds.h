#ifndef WEBCREDS_H
#define WEBCREDS_H

#include <string>
#include <vector>

// qltox 凭据加密存储（rclone config encryption 风格，Qt-free，禁异常）。
//
// 容器：~/.config/qltox/webcreds.enc（JSON，0600）
//   {"magic":"QLW1","version":1,"fields":{ "<name>": 明文 | 信封 }}
//   信封：{"m":"token"|"pass","s":salt_b64,"i":iter,"iv":b64,"ct":b64,"mac":b64}
//   明文项 = JSON 字符串；加密项 = 信封对象。逐字段独立，可任意混用。
// 令牌：~/.config/qltox/webcreds.key（0600，hex 64 字符的 256-bit 随机密钥）。
//
// 环境变量（运行时与 CLI 通用）：
//   QTOX_WEB_CRED_FILE            覆盖侧车路径（默认 ~/.config/qltox/webcreds.enc）
//   QTOX_WEB_CRED_TOKEN           覆盖令牌路径（默认 ~/.config/qltox/webcreds.key）
//   QTOX_WEB_CRED_PASS            口令（优先于 PASS_COMMAND）
//   QTOX_WEB_CRED_PASS_COMMAND    取口令的外部命令（stdout 首行）
//   QTOX_WEB_CRED_ITERS           PBKDF2 迭代数（加密时；解密用信封存储值，测试可调低）
//
// 语义不变式：
//   - 侧车命中必优先于 fallback；显式删除字段才回退常量。
//   - 对称解不开（口令/令牌缺失或 MAC 校验失败）= Broken，绝不静默回退 fallback。
//   - 所有错误经返回值 + err 字符串传递。

// 单字段运行时状态（过滤/报错/UI/日志的唯一同源枚举）
enum WebCredStatus {
    kWebCredNone = 0,   // 未配置：侧车无字段且无 fallback
    kWebCredConstant,   // 常量兜底：侧车无字段，使用调用方 fallback
    kWebCredPlain,      // 侧车明文
    kWebCredTokenEnc,   // 侧车·机器令牌加密
    kWebCredPassEnc,    // 侧车·口令加密
    kWebCredBroken,     // 信封存在但解不开（密钥缺失/错误或篡改）→ 视为不可用
};

struct WebCredResult {
    bool ok = false;            // 有可用明文（Plain/TokenEnc/PassEnc 解密成功，或 Constant 且 fallback 非空）
    std::string value;          // 明文值（仅 ok 时有效）
    WebCredStatus status = kWebCredNone;
};

// 字段写入模式
enum WebCredMode {
    kWebCredPlainMode = 0,      // 明文
    kWebCredTokenMode,          // 机器令牌加密（零交互解密）
    kWebCredPassMode,           // 口令加密（口令缺失则 ok=false，不回落常量）
};

struct WebCredFieldIn {
    std::string name;
    std::string value;
    WebCredMode mode;
};

// 路径覆盖（程序内覆盖；优先级：setOverride > env > 默认）
void webCredsSetFileOverride(const std::string& sidecarPath,
                             const std::string& tokenPath);

// 重载：丢弃进程内缓存并重读 env/pass 来源（测试用；换口令/改文件后调用）
void webCredsReload();

// 运行时查询（惰性加载，进程内缓存）
WebCredResult webCredGet(const std::string& field, const std::string& fallback);
// 指定口令来源（UI 会话口令回显用；passOverride 空 = 用环境来源）
WebCredResult webCredGet(const std::string& field, const std::string& fallback,
                         const std::string& passOverride);
WebCredStatus webCredStatus(const std::string& field);

// 状态中文名（UI/日志统一用）
const char* webCredStatusName(WebCredStatus s);

// 令牌文件是否存在（check 展示用）
bool webCredsTokenFileExists();

// ── CLI 管理接口 ──
// 全量重建容器（create）。passphrase 用于 pass 模式字段；含 token 字段时会
// 视情况生成令牌文件。err 非空表示失败。
bool webCredsCreate(const std::string& passphrase,
                    const std::vector<WebCredFieldIn>& fields,
                    std::string& err);

// 单字段增改（set）。pass 模式需要 passphrase。
bool webCredsSetField(const std::string& name, const std::string& value,
                      WebCredMode mode, const std::string& passphrase,
                      std::string& err);

// 删除字段（删除后才允许回退到 fallback/常量）。
bool webCredsRemoveField(const std::string& name, std::string& err);

struct WebCredListEntry {
    std::string name;
    WebCredStatus status;
    std::string value;   // 遮蔽时为空
    bool ok = false;
};

// 列出全部字段（show/check）。reveal=true 时回显明文；pass 字段需 passphrase。
bool webCredsList(bool reveal, const std::string& passphrase,
                  std::vector<WebCredListEntry>& out, std::string& err);

// 删除侧车与令牌文件。
bool webCredsWipe(std::string& err);

// 已知字段名（check 展示顺序；可含侧车主键之外的字段）
// 6 个 web 凭据 + 19 个非 web API key（noweb_*，仅管理存储，调用方暂未读取）
const char* const* webCredsKnownFields(size_t& count);

#endif // WEBCREDS_H