#ifndef NETPROXY_H
#define NETPROXY_H

#include <string>

// 全局代理配置（进程内单例，线程安全）。
//
// 用途：fanyibot/aigptbot/pasteuploader 三模块中「需要代理」的 provider
//   走用户配置的代理，其余直连。
//   HTTP 请求跑在 EventPoller 线程，而设置在 GUI 线程读写，故加锁。
//
// 语义：启用(enabled=true)时以用户设置为准（url 空或非法 = 直连，不回退）；
//   未启用时回退环境变量 https_proxy > HTTPS_PROXY > ALL_PROXY > all_proxy。
//   用户 url 与环境变量格式非法时均记 warn。

// 设置代理（GUI 线程：启动时 / 设置保存后调用）。
// enabled=true 且 url 合法才生效；url 空或非法仅告警，不生效（=直连）。
void netProxySet(bool enabled, const std::string& url);

// 对需要代理的 provider 返回代理 URL；否则返回空串（直连）。
// needsProxy=false 一律返回空串。
// 优先级：启用则用户设置（空/非法即直连，不回退）> 未启用则环境变量 > 直连。
std::string netProxyFor(bool needsProxy);

#endif // NETPROXY_H
