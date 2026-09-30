// stikcommon/test_main.cpp —— 单元测试套件 A 的入口（Qt3 单端）
//
// 范式来源：qldox 的单元测试（本仓旧范式，源码已丢，此处按其行为复原）：
//   * 全部 test_*.cpp 的 TEST_CASE 链成**单个** run_tests 二进制
//   * 本文件只负责提供 main（doctest 要求实现侧宏恰好出现在一个 TU）
//   * 过滤靠 doctest **内置**的命令行选项，不自写解析层
//
// 用法（过滤由 doctest 内置选项完成，长短别名都有）：
//   ./run_tests                              跑全部
//   ./run_tests -tc="QWebdavLite*"           只跑匹配的用例
//   ./run_tests --test-case="DAV:*"          同上（长名）
//   ./run_tests -sf="test_dav207.cpp"        只跑某文件
//   ./run_tests -r=junit -o=result.xml        出 JUnit XML（CI 用）
//
// 注：早先推测 qldox 的 test_main.cpp 自写了一套 dt-* 环境变量过滤层，
//     已由 doctest.h:6614-6630 否证 —— 那 10 组长短别名是 doctest 自带的
//     命令行选项名（DOCTEST_CONFIG_OPTIONS_PREFIX = "dt-"），二进制里的同名字符串
//     来自 doctest 自身而非项目代码。故此处不重复实现。

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
