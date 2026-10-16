#ifndef LINKPREVIEW_H
#define LINKPREVIEW_H

#include "compatcore34.h"
#include <string>
#include <map>
#include <set>
#include <ctime>

// 链接预览数据（OpenGraph / Twitter Card，回退 <title>/<meta name=description>）。
// 纯文本卡片：站点 + 标题 + 描述，不含图片。
struct LinkPreviewData {
    bool ok;
    QString url;          // 规范化后的页面 URL（缓存键）
    QString title;
    QString description;
    QString siteName;
    LinkPreviewData() : ok(false) {}
};

// 检测并转码为 UTF-8：BOM > HTTP Content-Type charset > meta prescan > uchardet > UTF-8。
// 主编码解码失败时（声明 UTF-8 实为其它编码 / charset 非法未知），依次用 meta 声明、uchardet 兜底。
// httpContentType 为原始 Content-Type 头（可为空）。
QByteArray linkPreviewDecodeToUtf8(const QByteArray& raw, const QString& httpContentType);

// 解析 HTML 头部元数据；不执行 JS。输入须为 UTF-8（内部先转码）。
// 桌面平台用 gumbo 建 DOM；Android 无 gumbo 时用内置浅扫描器（跳过 script/style）。
// 取得任一可用字段即返回 true。
bool parseLinkPreviewHtml(const QByteArray& body, const QString& httpContentType,
                          const QString& urlKey, LinkPreviewData& out);

// 去 fragment（#...）；其余原样保留。
QString linkPreviewNormalizeUrl(const QString& url);

// UI 线程内存缓存（重启失效，无锁，仅主线程访问）。
class LinkPreviewCache {
public:
    static LinkPreviewCache& inst();
    // 命中则拷贝到 out 返回 true；失败负缓存未过期同样返回 true（out.ok=false）。
    bool get(const QString& urlKey, LinkPreviewData& out);
    void put(const QString& urlKey, const LinkPreviewData& data);
    bool isInflight(const QString& urlKey) const;
    void markInflight(const QString& urlKey);
    void clearInflight(const QString& urlKey);
    void clearAll();
private:
    LinkPreviewCache() {}
    struct Entry { LinkPreviewData data; std::time_t stamp; };
    std::map<std::string, Entry> m_map;
    std::set<std::string> m_inflight;
    void evictIfNeeded();
    static const size_t kCapacity = 200;
    static const int kNegativeTtlSec = 300;
};

#endif // LINKPREVIEW_H
