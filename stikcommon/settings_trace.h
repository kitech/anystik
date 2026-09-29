#ifndef SETTINGS_TRACE_H
#define SETTINGS_TRACE_H

// mac 设置落盘诊断探针：非 mac 平台为空转。
// 定义在 main.cpp，调用点在各自 cpp，统一走本声明。
void trace_settings(const char* site);

#endif