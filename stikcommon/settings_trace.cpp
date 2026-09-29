// settings_trace：anystik 侧定义在 src/main.cpp（mac 落盘诊断探针，非 mac 空转）。
// qlstik 侧不需要该探针，这里给同义空实现，满足 myi18n.cpp 的链接需求。
// L2 阶段与 anystik/src/main.cpp 的定义合并为一份共享实现。
#include "settings_trace.h"

void trace_settings(const char* site)
{
    (void)site;
}
