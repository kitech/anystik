#ifndef QLSTIK_BUILDINFO_H
#define QLSTIK_BUILDINFO_H

// 构建信息：GIT_COMMIT 在 qlstik.pro 里以裸 token 形式传入
// （Qt3 qmake 把 \" 转义写坏，字符串字面量走不通），故代码侧用宏字符串化还原，
// 用法同 qltox。
#include "compat34.h"

#ifndef GIT_COMMIT
#define QLSTIK_GIT_COMMIT_STR "unknown"
#else
#define QLSTIK_XSTR_(x) #x
#define QLSTIK_XSTR(x) QLSTIK_XSTR_(x)
#define QLSTIK_GIT_COMMIT_STR QLSTIK_XSTR(GIT_COMMIT)
#endif

#endif // QLSTIK_BUILDINFO_H
