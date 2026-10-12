// webcreds_cli.cpp — qltox 凭据加密存储 CLI（Qt-free，与 webcreds.cpp 同构）
//
// 用法：
//   webcreds [--file PATH] [--token PATH] <子命令>
//   create  --field NAME --value VAL [--mode plain|token|pass] [--pass PW] ...（可多个字段）
//   set     --field NAME --value VAL [--mode plain|token|pass] [--pass PW]
//   rm      --field NAME
//   show    [--reveal] [--pass PW]
//   check
//   wipe    [--yes]
//
// 未给 --pass/口令环境时，pass 模式会交互提示输入口令。

#include "webcreds.h"

#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

void usage(const char* prog) {
    std::cerr
        << "用法: " << prog << " [--file PATH] [--token PATH] <子命令>\n"
        << "  create  --field NAME --value VAL [--mode plain|token|pass] [--pass PW] ...\n"
        << "  set     --field NAME --value VAL [--mode plain|token|pass] [--pass PW]\n"
        << "  rm      --field NAME\n"
        << "  show    [--reveal] [--pass PW]\n"
        << "  check\n"
        << "  wipe    [--yes]\n"
        << "环境变量: QTOX_WEB_CRED_FILE / QTOX_WEB_CRED_TOKEN / QTOX_WEB_CRED_PASS /\n"
        << "          QTOX_WEB_CRED_PASS_COMMAND\n";
}

std::string promptSecret(const char* what) {
    char* p = getpass(what);
    if (p) { return std::string(p); }
    std::string line;
    std::getline(std::cin, line);
    return line;
}

WebCredMode parseMode(const std::string& s, bool& ok) {
    ok = true;
    if (s == "plain") { return kWebCredPlainMode; }
    if (s == "token") { return kWebCredTokenMode; }
    if (s == "pass")  { return kWebCredPassMode; }
    ok = false;
    return kWebCredPlainMode;
}

std::string modeName(WebCredMode m) {
    switch (m) {
    case kWebCredPlainMode: return "plain";
    case kWebCredTokenMode: return "token";
    case kWebCredPassMode:  return "pass";
    }
    return "plain";
}

std::string statusName(WebCredStatus s) {
    return std::string(webCredStatusName(s));
}

int cmdCreate(int argc, char** argv, int start, const std::string& file,
              const std::string& tokenFile) {
    std::vector<WebCredFieldIn> fields;
    std::string passphrase;
    for (int i = start; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--field") {
            if (i + 1 >= argc) { std::cerr << "create: --field 缺少名称\n"; return 1; }
            WebCredFieldIn f;
            f.name = argv[++i];
            f.mode = kWebCredTokenMode;   // 不标 mode 默认 token
            fields.push_back(f);
        } else if (a == "--value") {
            if (fields.empty() || i + 1 >= argc) {
                std::cerr << "create: --value 需在 --field 后且带值\n";
                return 1;
            }
            fields.back().value = argv[++i];
        } else if (a == "--mode") {
            if (fields.empty() || i + 1 >= argc) {
                std::cerr << "create: --mode 需在 --field 后\n";
                return 1;
            }
            bool ok;
            fields.back().mode = parseMode(argv[++i], ok);
            if (!ok) { std::cerr << "create: 非法 --mode\n"; return 1; }
        } else if (a == "--pass") {
            if (i + 1 >= argc) { std::cerr << "create: --pass 缺少口令\n"; return 1; }
            passphrase = argv[++i];
        } else {
            std::cerr << "create: 未知参数 " << a << "\n";
            return 1;
        }
    }
    if (fields.empty()) {
        std::cerr << "create: 至少需要一个 --field\n";
        return 1;
    }
    for (size_t k = 0; k < fields.size(); ++k) {
        if (fields[k].value.empty()) {
            fields[k].value = promptSecret(("字段 " + fields[k].name + " 值: ").c_str());
        }
    }
    bool needPass = false;
    for (size_t k = 0; k < fields.size(); ++k) {
        if (fields[k].mode == kWebCredPassMode) { needPass = true; }
    }
    if (needPass && passphrase.empty()) {
        const char* envp = getenv("QTOX_WEB_CRED_PASS");
        if (envp && envp[0]) { passphrase = envp; }
        else { passphrase = promptSecret("口令（pass 模式）: "); }
        if (passphrase.empty()) {
            std::cerr << "create: pass 模式口令不能为空\n";
            return 1;
        }
    }
    std::string err;
    if (!webCredsCreate(passphrase, fields, err)) {
        std::cerr << "create 失败: " << err << "\n";
        return 1;
    }
    std::cout << "已写入侧车。\n";
    return 0;
}

int cmdSet(int argc, char** argv, int start, const std::string& file,
           const std::string& tokenFile) {
    std::string name;
    std::string value;
    WebCredMode mode = kWebCredTokenMode;
    std::string passphrase;
    for (int i = start; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--field") {
            if (i + 1 >= argc) { std::cerr << "set: --field 缺少名称\n"; return 1; }
            name = argv[++i];
        } else if (a == "--value") {
            if (i + 1 >= argc) { std::cerr << "set: --value 缺少值\n"; return 1; }
            value = argv[++i];
        } else if (a == "--mode") {
            if (i + 1 >= argc) { std::cerr << "set: --mode 缺少值\n"; return 1; }
            bool ok;
            mode = parseMode(argv[++i], ok);
            if (!ok) { std::cerr << "set: 非法 --mode: " << argv[i] << "\n"; return 1; }
        } else if (a == "--pass") {
            if (i + 1 >= argc) { std::cerr << "set: --pass 缺少口令\n"; return 1; }
            passphrase = argv[++i];
        } else {
            std::cerr << "set: 未知参数 " << a << "\n";
            return 1;
        }
    }
    if (name.empty()) { std::cerr << "set: 需要 --field\n"; return 1; }
    if (mode == kWebCredPassMode && passphrase.empty()) {
        const char* envp = getenv("QTOX_WEB_CRED_PASS");
        if (envp && envp[0]) { passphrase = envp; }
        else { passphrase = promptSecret("口令（pass 模式）: "); }
        if (passphrase.empty()) {
            std::cerr << "set: pass 模式口令不能为空\n";
            return 1;
        }
    }
    std::string err;
    if (!webCredsSetField(name, value, mode, passphrase, err)) {
        std::cerr << "set 失败: " << err << "\n";
        return 1;
    }
    std::cout << "已写入字段 " << name << "（" << modeName(mode) << "）。\n";
    return 0;
}

int cmdRm(int argc, char** argv, int start, const std::string& file,
          const std::string& tokenFile) {
    if (start >= argc) { std::cerr << "rm: 需要 --field NAME\n"; return 1; }
    std::string name;
    if (std::string(argv[start]) == "--field" && start + 1 < argc) {
        name = argv[start + 1];
    } else {
        name = argv[start];
    }
    std::string err;
    if (!webCredsRemoveField(name, err)) {
        std::cerr << "rm 失败: " << err << "\n";
        return 1;
    }
    std::cout << "已删除字段 " << name << "（该字段将回退常量兜底）。\n";
    return 0;
}

int cmdShow(int argc, char** argv, int start, const std::string& file,
            const std::string& tokenFile) {
    bool reveal = false;
    std::string passphrase;
    for (int i = start; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--reveal") { reveal = true; }
        else if (a == "--pass") {
            if (i + 1 >= argc) { std::cerr << "show: --pass 缺少口令\n"; return 1; }
            passphrase = argv[++i];
        } else {
            std::cerr << "show: 未知参数 " << a << "\n";
            return 1;
        }
    }
    std::vector<WebCredListEntry> out;
    std::string err;
    if (!webCredsList(reveal, passphrase, out, err)) {
        std::cerr << "show 失败: " << err << "\n";
        return 1;
    }
    if (out.empty()) {
        std::cout << "（无字段）\n";
        return 0;
    }
    std::cout << "field        status       value\n";
    for (size_t i = 0; i < out.size(); ++i) {
        const WebCredListEntry& e = out[i];
        std::string val = e.ok ? e.value : "-";
        std::string shown;
        if (e.status == kWebCredTokenEnc || e.status == kWebCredPassEnc) {
            shown = e.value.empty() ? std::string("*** ENCRYPTED [")
                        + modeName(e.status == kWebCredTokenEnc ? kWebCredTokenMode : kWebCredPassMode)
                        + "] ***"
                    : e.value;
        } else {
            shown = e.value.empty() ? "-" : e.value;
        }
        std::printf("%-12s %-10s %s\n", e.name.c_str(),
                    statusName(e.status).c_str(), shown.c_str());
    }
    std::printf("令牌文件: %s\n", webCredsTokenFileExists() ? "存在" : "缺失");
    return 0;
}

int cmdCheck(int argc, char** argv, int start, const std::string& file,
             const std::string& tokenFile) {
    std::vector<WebCredListEntry> out;
    std::string err;
    if (!webCredsList(false, std::string(), out, err)) {
        std::cerr << "check 失败: " << err << "\n";
        return 1;
    }
    bool allOk = true;
    for (size_t i = 0; i < out.size(); ++i) {
        const WebCredListEntry& e = out[i];
        std::printf("%-12s %-10s %s\n", e.name.c_str(),
                    statusName(e.status).c_str(), e.ok ? "可用" : "不可用");
        if (!e.ok) { allOk = false; }
    }
    std::printf("令牌文件: %s；退出状态: %s\n",
                webCredsTokenFileExists() ? "存在" : "缺失",
                allOk ? "全部可用" : "含不可用项");
    return allOk ? 0 : 1;
}

int cmdWipe(int argc, char** argv, int start, const std::string& file,
            const std::string& tokenFile) {
    bool yes = false;
    for (int i = start; i < argc; ++i) {
        if (std::string(argv[i]) == "--yes") { yes = true; }
    }
    if (!yes) {
        std::cerr << "将删除侧车与令牌文件，确认？[y/N] ";
        std::string line;
        std::getline(std::cin, line);
        if (line != "y" && line != "Y") { return 0; }
    }
    std::string err;
    if (!webCredsWipe(err)) {
        std::cerr << "wipe 失败: " << err << "\n";
        return 1;
    }
    std::cout << "已删除。将全部回退常量兜底。\n";
    return 0;
}

} // namespace

// 处理全局 --file/--token，再分发子命令。
int main(int argc, char** argv) {
    (void)argc;
    std::string file;
    std::string tokenFile;
    int start = 1;
    while (start < argc) {
        const std::string a = argv[start];
        if (a == "--file") {
            if (start + 1 >= argc) { std::cerr << "--file 缺少路径\n"; return 1; }
            file = argv[start + 1];
            start += 2;
        } else if (a == "--token") {
            if (start + 1 >= argc) { std::cerr << "--token 缺少路径\n"; return 1; }
            tokenFile = argv[start + 1];
            start += 2;
        } else {
            break;
        }
    }
    if (!file.empty() || !tokenFile.empty()) {
        webCredsSetFileOverride(file, tokenFile);
    }
    if (start >= argc) {
        usage(argv[0]);
        return 1;
    }
    const std::string cmd = argv[start];
    ++start;
    if (cmd == "create") { return cmdCreate(argc, argv, start, file, tokenFile); }
    if (cmd == "set")    { return cmdSet(argc, argv, start, file, tokenFile); }
    if (cmd == "rm")     { return cmdRm(argc, argv, start, file, tokenFile); }
    if (cmd == "show")   { return cmdShow(argc, argv, start, file, tokenFile); }
    if (cmd == "check")  { return cmdCheck(argc, argv, start, file, tokenFile); }
    if (cmd == "wipe")   { return cmdWipe(argc, argv, start, file, tokenFile); }
    if (cmd == "-h" || cmd == "--help") { usage(argv[0]); return 0; }
    std::cerr << "未知子命令: " << cmd << "\n";
    usage(argv[0]);
    return 1;
}