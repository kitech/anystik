#ifndef PASTEUPLOADER_H
#define PASTEUPLOADER_H

#include <string>
#include <vector>

class QObject;

// 上传内容类别（决定候选服务能力过滤）
enum PasteKind {
    kPasteKindText  = 0,
    kPasteKindImage = 1,
    kPasteKindVideo = 2,
    kPasteKindFile  = 3,
};

enum PasteHostId {
    kPasteHostDpaste = 0,
    kPasteHostCatbox,
    kPasteHost0x0,
    kPasteHostTransferSh,
    kPasteHostMhimg,
    kPasteHostScdnIo,
    kPasteHostTmpfileLink,
    kPasteHostTempfileOrg,
    kPasteHostStorageTo,
    kPasteHostLitterbox,
    kPasteHostPastebinCom,
};

// expire 预设位（对应属性栏 1h/1d/1w/1m/1y/never）
enum PasteExpireBit {
    kPasteExp1h    = 1 << 0,
    kPasteExp1d    = 1 << 1,
    kPasteExp1w    = 1 << 2,
    kPasteExp1m    = 1 << 3,
    kPasteExp1y    = 1 << 4,
    kPasteExpNever = 1 << 5,
};

// 单个服务的静态契约（表内顺序 = provider="all" 的固定尝试序）
struct PasteHost {
    PasteHostId id;
    const char* provider;
    bool text;
    bool image;
    bool video;
    bool file;
    unsigned int expireMask;
    bool unsupported;   // true=保留属性选项但选中即报错（pastebin.com 需官方 api_dev_key）
    bool disabled;      // true=保留表中但自动模式(all/any)跳过；显式选中即报错（当前暂不可用）
    bool needsProxy;    // true=该服务需经用户配置的代理访问（境外站点）
};

const std::vector<PasteHost>& pasteHosts();
bool pasteKindSupported(const PasteHost& host, PasteKind kind);
unsigned int pasteExpireMask(const std::string& expire);
bool pasteExpireSupported(const PasteHost& host, const std::string& expire);

// 按文件名（含扩展名）判定 MIME；宿主据此选 image/video/file 上传类别。
std::string pasteMimeOf(const std::string& fileName);

struct PasteRequest {
    std::string text;        // kPasteKindText 用（UTF-8 原样上传）
    std::string fileData;    // 图片/视频/文件用（二进制）
    std::string fileName;    // 含扩展名，服务据此判类型
    PasteKind kind = kPasteKindText;
    std::string provider;    // any / all / 具体 provider（空串视为 any）
    std::string expire;      // 1h/1d/1w/1m/1y/never（空串视为 never）
    long long localId = 0;   // 回传定位：宿主侧 ChatElement
    int chatId = 0;
    std::string chatType;
};

// 启动一次上传会话：按候选序逐个降级，首个成功即止；
// 全败（含能力/有效期不匹配、pastebin.com）发 PasteResultEvent(success=false)。
// target 接收 PasteResultEvent（须为 QObject，通常是 MainWindow）。
void pasteUploadStart(const PasteRequest& req, QObject* target);

#endif