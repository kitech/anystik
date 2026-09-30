// qlstik/test/test_main.cpp —— 单元测试套件 B 的入口（Qt3 单端）
//
// 与 stikcommon/test_main.cpp 同构：doctest 要求实现侧宏恰好出现在一个 TU，
// 全部 test_*.cpp 链成单个 run_tests。过滤用 doctest 内置命令行选项
// （DOCTEST_CONFIG_OPTIONS_PREFIX = "dt-"，长短别名都有），不自写解析层。
//
//   ./run_tests                          跑全部
//   ./run_tests -tc="Config*"            只跑匹配的用例
//   ./run_tests --test-case="Config:*"   同上（长名）
//   ./run_tests -sf="test_config.cpp"    只跑某文件
//   ./run_tests -r=junit -o=result.xml   出 JUnit XML（CI 用）

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
