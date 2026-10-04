#!/bin/bash
# anystik Linux(x86_64) 打包：只打包，不编译。
#
# 直接用 build-x64/ 里已编好的二进制（anystik + libgoso.so + libcso.so），把
# /opt/qt 的 Qt6 与 /opt/qt/qskinny 的 QSkinny 一起收进自包含 AppDir，再由同一份
# AppDir 产出两种包：
#   dist/anystik-<ver>-linux-x86_64.tar.gz    解压即用（./AppRun 或 ./anystik）
#   dist/anystik-<ver>-linux-x86_64.AppImage  单文件双击即用
# 两者同源同内容，后者只是被压成 squashfs 并前面拼一段 AppImage runtime。
#
# 与 package-osx.sh 同构：只打包不编译、版本取 src/version_config.h、
# 产物旁写 dist/sha256sums.txt。
#
# 用法：./package-linux.sh
# 覆盖：BUILD_DIR= QTDIR= QSKDIR= VERSION= BUILDNUM= STRIP=1 OUT=
#       ICON_SRC= MKSQ_COMP= SMOKE_TIMEOUT= APPIMAGE_SMOKE_TIMEOUT=
#       RUNTIME_URL= RUNTIME_SHA256= RUNTIME_CACHE= ./package-linux.sh
#
# 关于 AppImage runtime（type-2 格式强制的 ELF 前缀，按需下载并缓存）：
#   type-2 AppImage = runtime ELF + squashfs 镜像。那段 runtime 去不掉，appimagetool
#   之类工具只是替你把它找回来；脚本自己从官方 type2-runtime 取 runtime-x86_64，
#   static-pie 静态链接，不依赖 libfuse2。
#   下到 $WORK 校验 sha256，通过才落盘 dist/appimage-runtime-x86_64 复用；此后构建
#   不再联网（首次构建需要一次网络）。pin 住具体字节是因为 continuous 是滚动标签、
#   官方会原地替换资产（核对方式：官方在 Releases API 为每个资产公布 digest）。
#   下载走 https://gh-proxy.com/ 前缀：实测直连 GitHub 会被截断（拿到 540672/944632
#   的残流），代理转发的是同一份字节（sha256 与官方 digest 相符），去掉前缀也能下。
#   【为什么不从本地现成 AppImage 截取】那份是老的 FUSE2 动态 runtime，依赖
#   libfuse.so.2，而 libfuse2 在 Ubuntu 23.04+/Fedora/Debian 13 已被移除，且字节
#   来源无从追溯。
#   代价：没有 appimagetool 就不内嵌图标（文件管理器显示通用图标，不影响运行），
#   也不写 update-information/zsync。runtime 本身 GPL-2.0，可再分发。
set -euo pipefail
# 与 package-osx.sh 同理：变量引用后紧跟中文/全角标点时，bash 会把那些字节吞进
# 变量名。LC_ALL=C 让 bash 走 MB_CUR_MAX=1 的纯字节分支，整类问题根除。
export LC_ALL=C
cd "$(dirname "$0")"

APP_NAME=anystik
BUNDLE_ID="${BUNDLE_ID:-io.fedlet.anystik}"   # 与 Android/macOS 包名一致
BUILD_DIR="${BUILD_DIR:-build-x64}"
QTDIR="${QTDIR:-/opt/qt/6.7.3/gcc_64}"
QSKDIR="${QSKDIR:-/opt/qt/qskinny}"
VERSION="${VERSION:-}"                         # 空则取 src/version_config.h
BUILDNUM="${BUILDNUM:-}"                       # 空则取 APP_VERSION_CODE
STRIP="${STRIP:-0}"
OUT="${OUT:-dist}"
# AppImage runtime：官方 type2-runtime 资产，按需下载校验后缓存到 dist/ 复用
RUNTIME_URL="${RUNTIME_URL:-https://gh-proxy.com/https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64}"
# continuous 是滚动标签，官方会原地替换资产，所以 pin 住具体字节。升级 runtime 必须
# 同时改这里；否则构建会因 sha256 不符而报错，不会静默接受新字节。
RUNTIME_SHA256="${RUNTIME_SHA256:-156f4bdbde9c52d01814600013e0a273f0118dc2de98975f3c8c63427ec79074}"
RUNTIME_CACHE="${RUNTIME_CACHE:-${OUT}/appimage-runtime-x86_64}"
ICON_SRC="${ICON_SRC:-app_icon.png}"          # 仓库里 128x128，256 用 ImageMagick 放大
# 【别改回 xz】官方 continuous runtime 内置的 libsquashfuse 只支持 zlib 与 zstd，
# xz 压缩的 payload 它解不开（实测报 "uses xz compression, this version supports only
# zlib, zstd"，且 FUSE 挂载走同一个库，真实用户也会挂载失败）。zstd 也是 appimagetool
# 的默认压缩器。
MKSQ_COMP="${MKSQ_COMP:-zstd}"                  # mksquashfs 压缩器
# 「进程还活着」不等于「初始化成功」。三道冒烟一律改成必须打出 READY_RE 才算过。
# 上一版默认 5s，而实测冷缓存下首行输出就要 5.8s（AppDir）/ 16.1s（FUSE 挂载的
# AppImage），日志到时限必然是空的，后面那些 grep 等于在空文件里找错误串，永远
# 通过 —— 那道 xcb 门等于没验，却一直被当成「xcb 已验证通过」在用。
# 标志行取自 main.cpp:262、qldox/storage.cpp:280、src/pagemanager.cpp:127，
# offscreen 与 xcb 两种模式下均实测命中。三者都在 QGuiApplication 建好之后打印，
# 所以 xcb 那道门命中它，就等于证明 xcb 平台插件真的加载成功了。
READY_RE='\[anystik\] DPI: |Storage init complete|\[PageManager\] activated:'
SMOKE_TIMEOUT="${SMOKE_TIMEOUT:-30}"           # 起 AppDir 的时限（热缓存实测 0.2s 到标志行）
APPIMAGE_SMOKE_TIMEOUT="${APPIMAGE_SMOKE_TIMEOUT:-60}"   # 起 AppImage 的（热缓存 0.7s，FUSE 冷缓存 16.1s）
VC=src/version_config.h

step() { echo; echo "=== $* ==="; }
die()  { echo "ERROR: $*" >&2; exit 1; }

# 本脚本开着 set -e，任何 `测试 && 命令` 里测试为假都会直接终止脚本，
# 所以下面所有条件分支都写成 if ... fi，不写 && 短接。

case "${1:-}" in
    -h|--help)
        printf '%s\n' \
            '用法：./package-linux.sh' \
            '只打包 build-x64/ 里已编好的二进制，不编译。' \
            '产物：dist/<包名>.tar.gz 与 dist/<包名>.AppImage，外加 dist/sha256sums.txt' \
            '覆盖项全部走环境变量，见脚本头部注释。'
        exit 0
        ;;
    "") ;;
    *) die "未知参数 ${1}（-h 看帮助）" ;;
esac

# ── 0. 前置检查 ─────────────────────────────────────────────────────
[[ "$(uname)" == Linux ]] || die "必须在 Linux 上跑"
case "$(uname -m)" in
    x86_64|amd64) ;;
    *) die "本脚本只做 x86_64 包，当前架构 $(uname -m)" ;;
esac
for t in tar gzip sha256sum patchelf strip readelf od mksquashfs desktop-file-validate timeout env; do
    command -v "$t" >/dev/null || die "缺少工具 ${t}"
done
[[ -d "$BUILD_DIR" ]] || die "没有已编译目录 ${BUILD_DIR}，先跑 ./build-x64.sh 或用 BUILD_DIR= 指向别的"
for f in "$APP_NAME" libgoso.so libcso.so; do
    [[ -f "$BUILD_DIR/$f" ]] || die "缺少已编译产物 ${BUILD_DIR}/${f}"
    magic=$(LC_ALL=C od -An -tx1 -N4 "$BUILD_DIR/$f" | tr -d ' \n')
    if [ "$magic" != "7f454c46" ]; then
        die "${BUILD_DIR}/${f} 不是 ELF，x86_64 包的编译产物才对"
    fi
done
[[ -f "$QTDIR/lib/libQt6Core.so.6" ]] || die "没有 ${QTDIR}/lib/libQt6Core.so.6，QTDIR=${QTDIR}"
[[ -f "$QTDIR/plugins/platforms/libqxcb.so" ]] || die "没有 libqxcb.so，xcb 平台插件是必需项"
[[ -f "$QSKDIR/lib/qskinny/libqskinny.so.0.8" ]] || die "没有 QSkinny，QSKDIR=${QSKDIR}"
[[ -d "$QSKDIR/lib/qskinny/plugins/skins" ]] || die "没有 QSkinny skins 目录"
[[ -f "$ICON_SRC" ]] || die "没有图标 ${ICON_SRC}，ICON_SRC= 可指定别的"

command -v convert >/dev/null || command -v magick >/dev/null || die "缺少 ImageMagick，要把 128 图标放大到 256"

# 版本：取 version_config.h（与 About 页显示一致），可用环境变量覆盖
if [ -z "$VERSION" ]; then
    VERSION=$(sed -n 's/^#define APP_VERSION_NAME[[:space:]]*"\(.*\)"/\1/p' "$VC" 2>/dev/null | head -1)
fi
if [ -z "$BUILDNUM" ]; then
    BUILDNUM=$(awk '/^#define APP_VERSION_CODE/{print $3; exit}' "$VC" 2>/dev/null)
fi
if [ -z "$BUILDNUM" ]; then
    BUILDNUM=$(git rev-list --count HEAD 2>/dev/null || echo 1)
fi
[ -n "$VERSION" ] || die "取不到版本号，${VC} 里没有 APP_VERSION_NAME，用 VERSION= 手工指定"

PKG="${APP_NAME}-${VERSION}-linux-x86_64"
APPDIR="${OUT}/${PKG}.AppDir"
WORK="${OUT}/.pkgwork"
TGZ="${OUT}/${PKG}.tar.gz"
APPIMAGE="${OUT}/${PKG}.AppImage"

# ── 1. 准备 dist 与 AppImage runtime ─────────────────────────────────

step "准备 ${OUT}"
mkdir -p "$OUT"
# 下面第 9 步有两个子 shell 会先 cd 到 $WORK 再执行 $APPIMAGE，而 $APPIMAGE
# 是相对路径（dist/xxx.AppImage），cd 之后就找不到了 —— 会报
# "No such file or directory"。所以这里先固化一份绝对路径给那两处用。
OUT_ABS="$(cd "$OUT" && pwd)"
APPIMAGE_ABS="${OUT_ABS}/$(basename "$APPIMAGE")"
RUNTIME_CACHE_ABS="${OUT_ABS}/$(basename "$RUNTIME_CACHE")"
rm -rf "$APPDIR" "$WORK" "$TGZ" "$APPIMAGE"
mkdir -p "${APPDIR}/usr/lib/qskinny/plugins" "${APPDIR}/usr/plugins" "$WORK"
# runtime 缓存优先：dist/ 里已有且 sha256 符合 pin 就直接用，此后构建不再联网；
# 不符或没有则下一份到 $WORK 校验，通过才 mv 进缓存。缓存与 pin 不符时重新下一份
# ——pin 才是事实来源，缓存只是缓存；下载的字节没通过 pin 校验就绝不会被当 runtime 用。
runtime_sha_ok() {
    [ -f "$1" ] && [ "$(sha256sum "$1" | cut -d' ' -f1)" = "$RUNTIME_SHA256" ]
}
if ! runtime_sha_ok "$RUNTIME_CACHE"; then
    if [ -f "$RUNTIME_CACHE" ]; then
        echo "  缓存 ${RUNTIME_CACHE} sha256 与 pin 不符，重新下一份"
    else
        echo "  本机无 runtime（${RUNTIME_CACHE}），从官方下一份（仅此一次，之后离线可用）"
    fi
    command -v curl >/dev/null \
        || die "缺少 curl，取不到 runtime；可手动放置 ${RUNTIME_CACHE} 并用 RUNTIME_SHA256= 指定其 sha256"
    # 按 sha256 重试而不是只试一次：本机直连 GitHub 实测会被截断，残流必然对不上 pin。
    rt_ok=0
    for rt_try in 1 2 3; do
        if curl -fsSL -o "${WORK}/runtime.part" "$RUNTIME_URL"; then
            if runtime_sha_ok "${WORK}/runtime.part"; then
                rt_ok=1
                break
            fi
            echo "  第 ${rt_try} 次下的字节与 pin 不符（多半是被截断的残流），重试"
        else
            echo "  第 ${rt_try} 次下载失败，重试"
        fi
    done
    if [ "$rt_ok" != 1 ]; then
        got=$(sha256sum "${WORK}/runtime.part" 2>/dev/null | cut -d' ' -f1)
        rm -f "${WORK}/runtime.part"
        die "3 次都没拿到与 pin 相符的字节（最后一次 sha256 ${got:-无}），期望 ${RUNTIME_SHA256}。continuous 是滚动标签，官方换字节就改 RUNTIME_SHA256；网络不通可手动放置该文件并用 RUNTIME_SHA256= 指定其 sha256"
    fi
    mv "${WORK}/runtime.part" "$RUNTIME_CACHE"
fi
# payload 偏移 = runtime 文件大小。appimagetool 也是这么干的（appimagetool.c：
# readFile(runtime_file,&size,&data) -> mksquashfs -offset size -> fwrite 到 0），
# 而 cat runtime sqfs 与它的产物字节等价，故不必绕 -offset。
# 别改回 e_shoff+shnum*64：runtime 实际用 libappimage 的 appimage_get_elf_size()
# = max(section table 末, 最后一个 section 末)，那个公式只是近似。
# 第 9 步 --appimage-offset 自检取的就是 runtime 自己算的值，与 RT_LEN 不符即失败。
RT_LEN=$(stat -c%s "$RUNTIME_CACHE")
echo "runtime ${RT_LEN} 字节，${RUNTIME_CACHE}（sha256 与 pin 相符）"

# ── 2. 收二进制与随包库 ─────────────────────────────────────────────
step "暂存 AppDir"
cp -a "${BUILD_DIR}/${APP_NAME}" "${APPDIR}/${APP_NAME}"

# Qt6：只收闭包里真实链到的那些（libqskinny 还会拖进 DBus/Qml/Quick/OpenGL）。
# glob 带版本尾是为了连真实文件带 symlink 一起收；少一条 libQt6Core.so ->
# libQt6Core.so.6.7.3 的链，运行时只能靠 LD_LIBRARY_PATH 兜。
for l in Core Gui Network Qml QmlModels Quick OpenGL Concurrent Xml DBus; do
    set -- "${QTDIR}"/lib/libQt6"${l}".so.6*
    if [ ! -e "$1" ]; then
        die "${QTDIR}/lib 里没有 libQt6${l}.so.6"
    fi
    cp -a "$@" "${APPDIR}/usr/lib/"
done

# ICU：QSkinny 链 libicui18n/libicuuc/libicudata，宿主不保证同版本，必须随包
shopt -s nullglob
icu=("${QTDIR}"/lib/libicu*.so.73*)
shopt -u nullglob
if [ ${#icu[@]} -eq 0 ]; then
    die "${QTDIR}/lib 里没有 libicu*.so.73，QSkinny 依赖 ICU"
fi
cp -a "${icu[@]}" "${APPDIR}/usr/lib/"

for pat in libqskinny.so.0.8 libqskqmlexport.so.0.8; do
    set -- "${QSKDIR}/lib/qskinny/${pat}"*
    if [ ! -e "$1" ]; then
        die "${QSKDIR}/lib/qskinny 里没有 ${pat}"
    fi
    cp -a "$@" "${APPDIR}/usr/lib/"
done

cp -a "${BUILD_DIR}/libgoso.so" "${BUILD_DIR}/libcso.so" "${APPDIR}/usr/lib/"
# Go 产物在 build-x64 里是 644，ldd 会打 no execution permission 警告；
# 共享库不需要执行位，这里显式钉死，免得复制到什么就是什么
chmod 644 "${APPDIR}/usr/lib/libgoso.so" "${APPDIR}/usr/lib/libcso.so"

# QSkinny 插件：保持与编译机上完全相同的相对布局（skins 在 libqskinny 旁边的
# plugins/skins 下），别想当然搬到 Qt 的 plugins/ 里去
cp -a "${QSKDIR}/lib/qskinny/plugins/skins" "${APPDIR}/usr/lib/qskinny/plugins/"
cp -a "${QSKDIR}/lib/qskinny/plugins/platforminputcontexts" "${APPDIR}/usr/lib/qskinny/plugins/"

# ── 3. Qt 插件（白名单，逐条有理由）────────────────────────────────
# platforms：xcb 必需；wayland 两件套给 Wayland 会话；minimal/offscreen 给冒烟
# 测试与无显示环境；linuxfb 给帧缓冲。eglfs/vkkhr/lvgl/vnc 是特定硬件，排除
mkdir -p "${APPDIR}/usr/plugins/platforms"
for p in libqxcb.so libqwayland-egl.so libqwayland-generic.so libqminimal.so libqoffscreen.so libqlinuxfb.so; do
    if [ ! -f "${QTDIR}/plugins/platforms/${p}" ]; then
        die "缺少平台插件 ${p}"
    fi
    cp -a "${QTDIR}/plugins/platforms/${p}" "${APPDIR}/usr/plugins/platforms/"
done

# imageformats 全收：贴纸与表情图就靠这些解码，漏一个就是一类图打不开
mkdir -p "${APPDIR}/usr/plugins/imageformats"
cp -a "${QTDIR}"/plugins/imageformats/*.so "${APPDIR}/usr/plugins/imageformats/"

# 单件插件，Qt 按目录分类加载，目录名不能改
one() {   # $1=插件目录 $2=文件名
    if [ ! -f "${QTDIR}/plugins/$1/$2" ]; then
        die "缺少插件 ${1}/${2}"
    fi
    mkdir -p "${APPDIR}/usr/plugins/$1"
    cp -a "${QTDIR}/plugins/$1/$2" "${APPDIR}/usr/plugins/$1/"
}
one iconengines libqsvgicon.so                        # svg 图标
one tls libqopensslbackend.so                         # Qt Network 走 HTTPS 要它
one platforminputcontexts libcomposeplatforminputcontextplugin.so
one platforminputcontexts libibusplatforminputcontextplugin.so
one platformthemes libqxdgdesktopportal.so           # Wayland 主题；qgtk3 不收，要 GTK3

# fcitx 平台输入法插件 libfcitxplatforminputcontextplugin-qt6.so 本轮不打包：
# 编译机上的 fcitx-qt5 源码还没编出插件，build-x64 里根本没有这个产物

mkdir -p "${APPDIR}/usr/plugins/xcbglintegrations"
cp -a "${QTDIR}"/plugins/xcbglintegrations/*.so "${APPDIR}/usr/plugins/xcbglintegrations/"
for d in wayland-decoration-client wayland-graphics-integration-client wayland-shell-integration; do
    mkdir -p "${APPDIR}/usr/plugins/$d"
    cp -a "${QTDIR}"/plugins/"$d"/*.so "${APPDIR}/usr/plugins/$d/"
done

# ── 3.5 Qt 私有模块闭包 ────────────────────────────────────────────
# 插件会 DT_NEEDED 一堆白名单里根本没有的 Qt 私有模块：libqxcb.so 要
# libQt6XcbQpa、libqsvg.so 要 libQt6Svg、wayland 那批要 libQt6WaylandClient /
# libQt6WlShellIntegration / libQt6WaylandEglClientHwIntegration。
# 漏掉的后果特别隐蔽：插件的 RUNPATH 指向 usr/lib 扑空后，加载器退回
# ld.so 缓存挑宿主那份 —— 本机 /usr/lib 装着 Qt 6.11.2，于是 6.11 的
# XcbQpa 去链包内 6.7.3 的 Core，ldd 报
#   libQt6Core.so.6: version `Qt_6.11' not found
# xcb 插件初始化直接失败，而主程序 ldd 干干净净，冒烟（offscreen）也过。
# 所以这里照 linuxdeployqt 的做法（它内部就用 ldd 决定拷哪些库）跑到不动点，
# 不硬编码库名。只从 $QTDIR/lib 取源，所以 xcb/wayland 系统库自动排除在外，
# 仍由宿主提供。
step "补 Qt 私有模块闭包"
elf_list() {
    find "$APPDIR" \( -name '*.so' -o -name '*.so.*' -o -name "$APP_NAME" \) -type f 2>/dev/null
}
# 用 readelf 读 DT_NEEDED，不要用 ldd 刮输出。实测 ldd 两条毛病：
#   1) 它会递归跟进解析到的库，于是一路跟进宿主 Qt 6.11 的 XcbQpa，把
#      libQt6QmlMeta.so.6（6.7.3 里根本不存在）和 libicu*.so.78 一起吐出来；
#   2) 碰上符号版本不匹配时行为不稳定，那 5 个真正要补的 soname 一个都没进列表。
# DT_NEEDED 才是加载器真正会去查的东西，也是闭包该依据的 ground truth。
needed() {
    local f
    while IFS= read -r f; do
        readelf -d "$f" 2>/dev/null | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'
    done < <(elf_list)
}
# 用 nullglob 显式收集匹配文件。连 .so.6 symlink 一起拷，下一轮判「已在包内」
# 就为真，循环才能收敛。
collect() {   # $1=目录 $2=文件名基名；把存在的匹配文件塞进全局数组 REPLY
    REPLY=()
    local f
    for f in "$1/$2" "$1/$2".*; do
        if [ -e "$f" ]; then
            REPLY+=("$f")
        fi
    done
}
shopt -s nullglob
for round in 1 2 3 4 5; do
    added=0
    while IFS= read -r soname; do
        [ -n "$soname" ] || continue
        collect "${APPDIR}/usr/lib" "$soname"
        if [ ${#REPLY[@]} -gt 0 ]; then
            continue
        fi
        collect "${QTDIR}/lib" "$soname"
        if [ ${#REPLY[@]} -eq 0 ]; then
            continue
        fi
        cp -a "${REPLY[@]}" "${APPDIR}/usr/lib/"
        echo "  补 ${soname}"
        added=$((added + 1))
    done < <(needed | sort -u)
    if [ "$added" -eq 0 ]; then
        echo "  第 ${round} 轮无新增，闭包收敛"
        break
    fi
done
shopt -u nullglob

# ── 4. 桌面文件、图标、AppRun ───────────────────────────────────────
step "写 .desktop、图标、AppRun"
printf '%s\n' \
    '[Desktop Entry]' \
    'Type=Application' \
    'Name=Anystik' \
    'GenericName=Sticker Manager' \
    'Comment=Cross-platform sticker and emoji manager client' \
    "Exec=${APP_NAME}" \
    "Icon=${APP_NAME}" \
    'Terminal=false' \
    'Categories=Utility;' \
    'StartupNotify=true' \
    "StartupWMClass=${APP_NAME}" \
    "X-AppImage-Version=${VERSION}" \
    > "${APPDIR}/${APP_NAME}.desktop"
mkdir -p "${APPDIR}/usr/share/applications"
cp -a "${APPDIR}/${APP_NAME}.desktop" "${APPDIR}/usr/share/applications/"
desktop-file-validate "${APPDIR}/${APP_NAME}.desktop"

mkdir -p "${APPDIR}/usr/share/icons/hicolor/128x128/apps"
mkdir -p "${APPDIR}/usr/share/icons/hicolor/256x256/apps"
cp -a "$ICON_SRC" "${APPDIR}/usr/share/icons/hicolor/128x128/apps/${APP_NAME}.png"
# IM7 起 convert 是 magick 的兼容入口，会打 deprecation 告警；优先用 magick
if command -v magick >/dev/null; then
    magick "$ICON_SRC" -resize 256x256 "${APPDIR}/usr/share/icons/hicolor/256x256/apps/${APP_NAME}.png"
else
    convert "$ICON_SRC" -resize 256x256 "${APPDIR}/usr/share/icons/hicolor/256x256/apps/${APP_NAME}.png"
fi
cp -a "${APPDIR}/usr/share/icons/hicolor/256x256/apps/${APP_NAME}.png" "${APPDIR}/.DirIcon"

# AppRun 全用单引号字面量写入，只在最后一行注入程序名，避免引号被吃掉
printf '%s\n' \
    '#!/usr/bin/env bash' \
    '# AppDir 启动器：摆好运行期路径再 exec 真程序。' \
    '# 有了 patchelf 的 rpath 其实不依赖 LD_LIBRARY_PATH，这里仍然设，' \
    '# 是为了让「用户改过 rpath / 用 LD_PRELOAD 排查」的场景也能起来。' \
    'set -eu' \
    'HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"' \
    'export LD_LIBRARY_PATH="${HERE}/usr/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"' \
    'export QT_PLUGIN_PATH="${HERE}/usr/plugins"' \
    'export QT_QPA_PLATFORM_PLUGIN_PATH="${HERE}/usr/plugins/platforms"' \
    "exec \"\${HERE}/${APP_NAME}\" \"\$@\"" \
    > "${APPDIR}/AppRun"
chmod +x "${APPDIR}/AppRun"

# ── 5. patchelf 收 rpath ───────────────────────────────────────────
step "写 rpath，脱离编译机目录也要能解析"
patchelf --set-rpath '$ORIGIN/usr/lib' "${APPDIR}/${APP_NAME}"
for so in "${APPDIR}/usr/lib/"*.so*; do
    if [ -L "$so" ]; then
        continue
    fi
    patchelf --set-rpath '$ORIGIN' "$so"
done
for so in "${APPDIR}/usr/lib/qskinny/plugins/skins/"*.so "${APPDIR}/usr/lib/qskinny/plugins/platforminputcontexts/"*.so; do
    patchelf --set-rpath '$ORIGIN/../../..' "$so"
done

# ── 6. 自包含校验：ldd 不能指向编译机、不能缺库、Qt 不能串味 ────────
step "校验自包含性"
check_ldd() {   # $1=待检 ELF $2=场景名 $3=包根绝对路径
    local root="$3"
    out=$(LD_LIBRARY_PATH= ldd "$1" 2>&1 || true)

    # 门 1：符号版本错。ldd 把它打成一行普通 note，既不含 "not found" 也不含
    # 路径，上一版就是漏在这里 —— 包内 Qt 6.7.3 的 Core 被宿主 6.11 的模块
    # 拉进来时报 version `Qt_6.11' not found，xcb 插件起不来。
    if printf '%s\n' "$out" | grep -qE "version .*Qt_[0-9.]+' not found"; then
        printf '%s\n' "$out" | grep -E "version .*' not found" >&2
        die "${2} 符号版本对不上，多半是宿主另一份 Qt 被挑走了"
    fi

    # 门 2：缺库
    if printf '%s\n' "$out" | grep -q "not found"; then
        printf '%s\n' "$out" | grep "not found" >&2
        die "${2} 有库没解析到"
    fi

    # 门 3：还指向编译机
    bad=$(printf '%s\n' "$out" | grep -E "${QTDIR}|${QSKDIR}|/mnt/sda5" || true)
    if [ -n "$bad" ]; then
        printf '%s\n' "$bad" >&2
        die "${2} 还指向编译机目录，rpath 没生效？"
    fi

    # 门 4：Qt/ICU/QSkinny 一律不许落到包外。宿主 /usr/lib 装着 Qt 6.11.2，
    # 只要有一个模块漏打包，就会被它顶掉而运行时炸在符号版本上。
    # 注意不能直接 grep -v "$APPDIR/usr/lib/"：ldd 把包内库显示成
    # .../usr/plugins/platforms/../../lib/libQt6Core.so.6，字符串对不上会误报，
    # 必须 readlink -f 归一化后再比前缀。
    # 末尾的 `|| true` 不是摆设：管道末端的 while 零次迭代时返回 1
    # （grep 一无所获就会这样），而 set -e 下一条赋值语句返回 1 会静默杀掉
    # 整个脚本 —— 上一版就是这么无声无息停在「校验自包含性」的。
    # sed 第二段剥掉 ldd 附的地址后缀 (0x...)。
    esc=$(printf '%s\n' "$out" \
        | grep -E '(libQt6|libicu|libqskinny)' \
        | sed -n 's/.*=> *//p' \
        | sed 's/ (0x[0-9a-fA-F]*)$//' \
        | while IFS= read -r p; do
            case "$(readlink -f "$p" 2>/dev/null)" in
                "${root}"/*) ;;
                *) printf '%s\n' "      $p" ;;
            esac
        done || true)
    if [ -n "$esc" ]; then
        printf '%s\n' "$esc" >&2
        die "${2} 的 Qt/ICU/QSkinny 有落到包外，闭包没收干净"
    fi
    # 上面最后一个命令是 if，条件为假时函数返回 1；set -e 下这会让调用方
    # （check_all_ldd 的循环体）直接静默退出脚本。必须显式返回 0。
    return 0
}
check_all_ldd() {   # $1=场景名
    n=0
    while IFS= read -r f; do
        check_ldd "$f" "${1}/$(basename "$f")" "$APPDIR_ABS"
        n=$((n + 1))
    done < <(elf_list)
    echo "  ${1}：${n} 个 ELF 全部通过"
}
APPDIR_ABS="$(cd "$APPDIR" && pwd)"
check_all_ldd "AppDir"

# ── 7. 冒烟：能起来才算数 ──────────────────────────────────────────
step "冒烟运行（offscreen）：须活到时限，且打出标志行"
rc=0
( ulimit -c 0; env -i HOME="$WORK" PATH=/usr/bin:/bin QT_QPA_PLATFORM=offscreen \
    timeout "$SMOKE_TIMEOUT" "${APPDIR}/AppRun" ) > "${WORK}/smoke.log" 2>&1 || rc=$?
if [ "$rc" = "127" ] || ! grep -qE "$READY_RE" "${WORK}/smoke.log"; then
    tail -20 "${WORK}/smoke.log" >&2
    die "冒烟失败，退出码 ${rc}，且没等到标志行（${READY_RE}）；127 是缺库或 AppRun 不可执行"
fi
echo "  标志行命中，退出码 ${rc}（124=活到时限，正常）"

# 冒烟走 offscreen，根本不加载 xcb 插件，所以上面那道门抓不到「插件加载失败」
# 这一类问题（上一轮就因此冒烟全过、真启动炸在 Qt_6.11 符号版本上）。
# 这里必须给一个真实显示才能验出结论：早先用 env -i 跑，DISPLAY 被剥掉，
# 日志里「could not connect to display」接着就是插件加载失败，两件事混在一起
# 分不清是包坏了还是没显示。改用 xvfb-run 起一次性虚拟显示，不碰用户会话。
step "xcb 专项检查（真实显示下须打出标志行；标志行在 QGuiApplication 之后）"
rc=0
xcb_ran=0
if command -v xvfb-run >/dev/null; then
    xcb_ran=1
    ( ulimit -c 0; HOME="$WORK" QT_QPA_PLATFORM=xcb \
        xvfb-run -a timeout "$SMOKE_TIMEOUT" "${APPDIR}/AppRun" ) > "${WORK}/xcb.log" 2>&1 || rc=$?
elif [ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]; then
    xcb_ran=1
    ( ulimit -c 0; HOME="$WORK" QT_QPA_PLATFORM=xcb \
        timeout "$SMOKE_TIMEOUT" "${APPDIR}/AppRun" ) > "${WORK}/xcb.log" 2>&1 || rc=$?
fi
if [ "$xcb_ran" = "0" ]; then
    echo "  跳过：既无 xvfb-run 也无显示环境，xcb 这道验不了"
else
    if grep -qE 'Could not load the Qt platform plugin|no Qt platform plugin could be initialized' "${WORK}/xcb.log"; then
        tail -20 "${WORK}/xcb.log" >&2
        die "xcb 平台插件没起来（退出码 ${rc}），闭包多半又漏了 Qt 私有模块"
    fi
    if ! grep -qE "$READY_RE" "${WORK}/xcb.log"; then
        tail -20 "${WORK}/xcb.log" >&2
        die "xcb 下没等到标志行（${READY_RE}），退出码 ${rc}：插件没报错，但应用没真正起来"
    fi
    echo "  xcb 插件已加载且标志行命中，退出码 ${rc}（124=活到时限，正常）"
fi

# ── 8. tgz ─────────────────────────────────────────────────────────
step "打 tgz"
tar -czf "$TGZ" -C "$OUT" "${PKG}.AppDir"
echo "  ${TGZ}，$(du -h "$TGZ" | cut -f1)"

# ── 9. AppImage：runtime + squashfs ────────────────────────────────
step "打 AppImage"
SQ="${WORK}/${PKG}.squashfs"
mksquashfs "$APPDIR" "$SQ" -root-owned -noappend -no-progress -comp "$MKSQ_COMP"
cat "$RUNTIME_CACHE_ABS" "$SQ" > "$APPIMAGE"
chmod +x "$APPIMAGE"
got=$("$APPIMAGE" --appimage-offset)
if [ "$got" != "$RT_LEN" ]; then
    die "AppImage 自报 payload 偏移 ${got}，与 runtime 长度 ${RT_LEN} 不符"
fi
echo "  ${APPIMAGE}，$(du -h "$APPIMAGE" | cut -f1)，payload 偏移 ${got} 与 runtime 长度一致"

step "回验 AppImage 内的程序"
( cd "$WORK" && rm -rf squashfs-root && "$APPIMAGE_ABS" --appimage-extract >/dev/null )
[[ -f "${WORK}/squashfs-root/${APP_NAME}" ]] || die "AppImage 里解不出 ${APP_NAME}"
EXTRACT_ABS="$(cd "${WORK}/squashfs-root" && pwd)"
check_ldd "${EXTRACT_ABS}/${APP_NAME}" "AppImage 内主程序" "$EXTRACT_ABS"
rc=0
# TMPDIR 必须指到 $WORK：runtime 的 extract-and-run 解包目录是
# $TMPDIR/appimage_extracted_<md5>，解包完才自己清；而这里的 timeout 会在
# 应用还活着时 SIGTERM，runtime 来不及收尾，实测每次留下 220MB 垃圾在 /tmp。
# 指进 $WORK 就由末尾那句 rm -rf "$WORK" 连带清掉。
( cd "$WORK" && env -i HOME="$WORK" PATH=/usr/bin:/bin TMPDIR="$WORK" QT_QPA_PLATFORM=offscreen \
    timeout "$APPIMAGE_SMOKE_TIMEOUT" "$APPIMAGE_ABS" --appimage-extract-and-run ) > "${WORK}/smoke2.log" 2>&1 || rc=$?
if ! grep -qE "$READY_RE" "${WORK}/smoke2.log"; then
    tail -20 "${WORK}/smoke2.log" >&2
    die "AppImage 冒烟失败，退出码 ${rc}，且没等到标志行（${READY_RE}）"
fi
echo "  AppImage 标志行命中，退出码 ${rc}"

# ── 10. STRIP 可选，须排在两次校验之后 ─────────────────────────────
if [ "$STRIP" = "1" ]; then
    step "strip"
    find "$APPDIR/usr/lib" "$APPDIR/usr/plugins" -name '*.so*' -type f -exec strip --strip-unneeded {} +
    strip --strip-unneeded "${APPDIR}/${APP_NAME}"
fi

# ── 11. 校验和 ─────────────────────────────────────────────────────
step "写 sha256sums.txt"
( cd "$OUT" && sha256sum "$(basename "$TGZ")" "$(basename "$APPIMAGE")" > sha256sums.txt )
cat "${OUT}/sha256sums.txt"

step "完成"
ls -lh "$TGZ" "$APPIMAGE"
rm -rf "$WORK"
echo "tgz 解压后跑 ${PKG}.AppDir/AppRun 或 ${PKG}.AppDir/${APP_NAME}"