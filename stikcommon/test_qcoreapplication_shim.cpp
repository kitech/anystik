// stikcommon/test_qcoreapplication_shim.cpp —— QCoreApplication 垫片契约（Qt3 单端）
//
// 被测对象：qcoreapplication_shim.h —— Qt 3.5 没有 QCoreApplication 这个类
//   （Qt4 才引入），也没有静态的 installTranslator/removeTranslator/translate。
//   Qt3 下这些动作挂在全局 qApp 上，垫片用同名类 + 静态包装补齐 Qt4 调用形态。
//
// 为什么必须测 translate()（qcoreapplication_shim.h:23-29 的立身之本）：
//   Qt3 的 QApplication::translate() 默认 encoding = DefaultCodec，会按应用
//   locale 猜编码（无 GUI locale 时退 **Latin-1**）。于是源码里的 UTF-8 中文
//   字面量会被逐字节当 Latin-1 展开，"站点加载失败：%1" 变成乱码，**且不报错**
//   ——只是用户看到坏文案。垫片靠显式传 QApplication::UnicodeUTF8 对齐 Qt4+，
//   无 qApp 时用 QString::fromUtf8 兜底，两者同语义。
//   本用例用**码点**验证，不看终端显示（AGENTS.md：非 ASCII 一律 fromUtf8 +
//   码点数字验证）。
//
// ⚠ qApp 存在与否（见 test_qstandardpaths_shim.cpp 文件头的说明）不影响本文件：
//   translate() 两条分支（有 qApp 走 UnicodeUTF8、无 qApp 走 fromUtf8）都应给出
//   正确的 UTF-8 解码结果，故断言对执行顺序不敏感。

#include <qstring.h>

#include "doctest/doctest.h"

#include "qcoreapplication_shim.h"

// "站点加载失败：%1"（源码直接写 CJK 原文，不用 \x 转义；Qt3 的 translate()
//   收 const char* 并按 UTF-8 解码，故字面量即 UTF-8 源码字节）
//   站=U+7AD9 点=U+70B9 加=U+52A0 载=U+8F7D 失=U+5931 败=U+8D25 ：=U+FF1A
static const char* kZhFail = "站点加载失败：%1";

// ⚠⚠ 核心回归点：translate() 必须按 UTF-8 解码，绝不能退成 Latin-1。
//   Latin-1 会把 23 个字节变成 23 个 QChar（每个 < 0x100），而正确结果是
//   9 个 QChar（6 汉字 + 全角冒号 + '%' + '1'）。长度差异一眼可辨。
TEST_CASE("QCoreApplication::translate: 中文源串按 UTF-8 解码（非 Latin-1）")
{
    const QString got = QCoreApplication::translate("sitelistclient", kZhFail);

    CHECK_EQ(got.length(), 9);                 // Latin-1 会得到 23

    // 逐码点核对，不依赖终端显示
    CHECK(got.at(0) == QChar(0x7AD9));         // 站
    CHECK(got.at(1) == QChar(0x70B9));         // 点
    CHECK(got.at(2) == QChar(0x52A0));         // 加
    CHECK(got.at(3) == QChar(0x8F7D));         // 载
    CHECK(got.at(4) == QChar(0x5931));         // 失
    CHECK(got.at(5) == QChar(0x8D25));         // 败
    CHECK(got.at(6) == QChar(0xFF1A));         // 全角冒号
    CHECK(got.at(7) == QChar('%'));
    CHECK(got.at(8) == QChar('1'));
}

// ⚠ 无翻译器时应原样返回源串（Qt 语义）；ASCII 通路也必须完好。
TEST_CASE("QCoreApplication::translate: ASCII 源串原样返回")
{
    CHECK_EQ(QCoreApplication::translate("ctx", "Hello %1"),
             QString::fromLatin1("Hello %1"));
}

// ⚠ disambiguation 参数存在时必须能传且不影响「无翻译器即原样」的结果。
TEST_CASE("QCoreApplication::translate: 带 disambiguation 仍原样返回源串")
{
    CHECK_EQ(QCoreApplication::translate("ctx", "Open", "verb"),
             QString::fromLatin1("Open"));
}

// ⚠ 混合 ASCII 与多字节：必须整串按 UTF-8 解，而不是只在开头对。
//   在中文后再接 ASCII，可抓「只解了前缀」这类截断错误。
TEST_CASE("QCoreApplication::translate: 中英混排整串 UTF-8 正确")
{
    // "失败: ok" → 失(U+5931) 败(U+8D25) :(0x3A) 空格 o k
    const QString got = QCoreApplication::translate(
        "ctx", "失败: ok");
    CHECK_EQ(got.length(), 6);
    CHECK(got.at(0) == QChar(0x5931));
    CHECK(got.at(1) == QChar(0x8D25));
    CHECK(got.at(2) == QChar(':'));
    CHECK(got.at(3) == QChar(' '));
    CHECK(got.at(4) == QChar('o'));
    CHECK(got.at(5) == QChar('k'));
}

// ⚠ Qt3 无「组织名」概念，organizationName() 必须恒空 —— qlstik 不设 org，
//   AppLocalDataLocation 因此少一层目录。把它钉住，防止有人「顺手实现」成
//   返回应用名或别的值而与 Qt6 路径分叉。
TEST_CASE("QCoreApplication::organizationName: 恒为空串（Qt3 无此概念）")
{
    CHECK(QCoreApplication::organizationName().isEmpty());   // 不用 CHECK_EQ(..., QString())：null QString 打印会 SIGSEGV
}
