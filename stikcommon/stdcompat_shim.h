#ifndef QLSTIK_STDCOMPAT_SHIM_H
#define QLSTIK_STDCOMPAT_SHIM_H

// 与 Qt 版本无关的 C++ 标准缺口垫片（平铺，无聚合头、无 -include 预包含）。
// 当前只有 std::as_const：C++17 才入标准，qlstik 的 Qt3/Qt4 用 -std=c++14。
// C++17 及以上（含 Qt6 构建）由 #if 自动跳过。

#if defined(__cplusplus) && __cplusplus < 201703L
#include <utility>
namespace std {
template <class T>
const T& as_const(T& t) noexcept { return t; }
}
#endif

#endif // QLSTIK_STDCOMPAT_SHIM_H
