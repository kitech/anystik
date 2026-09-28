#!/bin/bash
# ── anystik macOS(x86_64) 打包：只打包，不编译 ──────────────────────
# 直接用 build-x64/ 里已编好的二进制（anystik + libcso.dylib + libgoso.dylib），
# 靠 macdeployqt + install_name_tool 把 /opt/qt、Homebrew openssl、nix libc++ 这些
# 编译机上的绝对依赖收进 bundle、改写成 @rpath，产出能在别的 Mac 上双击运行的包。
# 产物：dist/anystik-<ver>-osx-x64.{app,tgz,dmg} + dist/sha256sums.txt
#
# 用法：./package-osx.sh
# 覆盖： BUILD_DIR= QTDIR= QSKDIR= VERSION= BUILDNUM= CODESIGN_ID= STRIP=1 ./package-osx.sh
# LIBCXX=system（默认）把 nix 的 libc++ 引用改指 /usr/lib/libc++.1.dylib，且不打包；
#         若启动报 "dyld: Symbol not found"（二进制是拿更新版 libc++ 头文件编的，
#         而系统自带的那份偏旧），改用 LIBCXX=bundle ./package-osx.sh 收一份进 Frameworks。
set -euo pipefail
# macOS 自带的 /bin/bash 是 3.2，在 UTF-8 locale 下解析「$变量 + 多字节字符」（如 "$dep" 紧跟全角括号）时
# 会把那些字节吞进变量名，导致变量变成未赋值、set -u 直接报 unbound variable。
# 规则：本脚本里凡是变量引用后面紧跟中文/全角标点，一律写成 ${变量}。
# LC_ALL=C 让 bash 走 MB_CUR_MAX=1 的纯字节分支，整类问题根除；只影响打包环境，不影响用户运行 app。
export LC_ALL=C
cd "$(dirname "$0")"

APP_NAME=anystik
BUNDLE_ID="${BUNDLE_ID:-io.fedlet.anystik}"   # 与 Android 包名一致
BUILD_DIR="${BUILD_DIR:-build-x64}"
QTDIR="${QTDIR:-/opt/qt/6.7.3/macos}"
QSKDIR="${QSKDIR:-/opt/qt/qskinny}"
MIN_MACOS="${MIN_MACOS:-11.0}"                # 与 build-x64 的 CMAKE_OSX_DEPLOYMENT_TARGET 一致
CODESIGN_ID="${CODESIGN_ID:--}"                # - = ad-hoc；对外分发用 "Developer ID Application: ..."
STRIP="${STRIP:-0}"
# libc++ 策略：本项目【本来就不该链 nix 的 libc++】——Qt 与 QSkinny 都是 /usr/lib/libc++.1.dylib，
# 只有 anystik/libcso.dylib 被构建环境漏进来的 nix 搜索路径污染了（CMakeLists 已加 -stdlib=libc++）。
#   system = 改指 macOS 自带的 libc++（默认，不打包；macOS 11+ 自带）
#   bundle = 把 nix store 里的实体收进 Frameworks（应急：系统 libc++ 太旧、
#            缺二进制要用的符号，启动报 "dyld: Symbol not found" 时用）
LIBCXX="${LIBCXX:-system}"
SYSLIB="${SYSLIB:-/usr/lib}"                   # 系统 dylib 所在目录（正常不用改）
OUT=dist
VC=src/version_config.h

step() { echo; echo "=== $* ==="; }
die()  { echo "ERROR: $*" >&2; exit 1; }

# 注意：本脚本开着 set -e，任何 `测试 && 命令` 里测试为假都会直接终止脚本，
#       所以下面所有条件分支都写成 `if ... fi`，不写 `&&` 短接。
is_macho() {  # Mach-O 魔数：64/32 位、fat
    local m
    m=$(LC_ALL=C od -An -tx1 -N4 "$1" 2>/dev/null | tr -d ' \n' || true)
    case "$m" in cffaedfe|feedface|feedfacf|cafebabe|cafebabf) return 0 ;; *) return 1 ;; esac
}
deps_of()   { otool -L  "$1" 2>/dev/null | awk 'NR>1 {print $1}' || true; }   # 依赖名（跳过首行文件名）
rpaths_of() { otool -l  "$1" 2>/dev/null | awk '/cmd LC_RPATH/{getline; getline; sub(/^ *path /,""); sub(/ \(offset.*/,""); print}' || true; }
id_of()     { otool -D  "$1" 2>/dev/null | awk 'NR>1 {print $1; exit}' || true; }
has_dep()   { deps_of "$1" | grep -qxF "$2"; }                                # 精确匹配（-change 要求）
list_macho() {  # bundle 内所有 Mach-O（framework 主体在 Versions/A 下，名字不固定，逐个试探）
    local f
    while IFS= read -r f; do if is_macho "$f"; then echo "$f"; fi; done \
        < <(find "$APP" -type f 2>/dev/null)
}
ensure_rpath() {  # $1=文件 $2=rpath；install_name_tool 遇重复 rpath 会报错退出 1，所以必须先查
    local f="$1" r="$2" have
    have=" $(rpaths_of "$f" | tr '\n' ' ') "
    case "$have" in *" $r "*) return 0 ;; esac
    install_name_tool -add_rpath "$r" "$f"
}
del_rpath() {     # $1=文件 $2=rpath；真删掉返回 0，本来就没有返回 1
    local f="$1" r="$2" have
    have=" $(rpaths_of "$f" | tr '\n' ' ') "
    case "$have" in *" $r "*) install_name_tool -delete_rpath "$r" "$f" ;; *) return 1 ;; esac
}
find_alt() {      # $1=basename：编译机上路径失效时（nix store 被 gc/换 hash、Homebrew 换前缀）找替代
    local base="$1" c
    # ① libc++ 系列：改指 macOS 自带的那份，不打包。
    #    本项目【本来就不该链 nix 的 libc++】——Qt 与 QSkinny 都是 /usr/lib/libc++.1.dylib，
    #    只有我们自己链的 anystik/libcso.dylib 被构建环境漏进来的 nix 路径污染了（CMakeLists
    #    已加 -stdlib=libc++ 钉死）。这里兜住存量二进制：直接改引用，不把库塞进 bundle。
    #
    #    【不要用 [ -f ] 判断它存不存在】：macOS 11+ 的系统库都收在 dyld shared cache 里，
    #    磁盘上可以没有这个文件，dyld 照样能按这个路径解析。证据：macdeployqt 写进它部署的
    #    Qt framework 里的依赖就是 /usr/lib/libc++.1.dylib，而那份 Qt 在本机是能跑的。
    #    （曾经在这里判过 -f，结果在这台 Mac 上直接报"系统 libc++ 不在 /usr/lib"而卡住。）
    if [ "$LIBCXX" = system ]; then
        case "${base}" in
            libc++.1.0.dylib)    printf '%s\n' "$SYSLIB/libc++.1.dylib"; return 0 ;;
            libc++abi.1.0.dylib) printf '%s\n' "$SYSLIB/libc++abi.dylib"; return 0 ;;
        esac
    fi
    # ② 同名兜底（应急网，正常不该走到）
    for c in /nix/store/*/lib/"${base}" /usr/local/opt/*/lib/"${base}" /opt/homebrew/opt/*/lib/"${base}" \
             "${QTDIR}/lib/${base}"; do
        if [ -f "$c" ]; then printf '%s\n' "$c"; return 0; fi
    done
    return 1
}

# ── 0. 前置检查 ─────────────────────────────────────────────────────
[[ "$(uname)" == Darwin ]] || die "必须在 macOS 上跑（依赖 macdeployqt / install_name_tool / hdiutil）"
for t in otool install_name_tool codesign hdiutil shasum plutil lipo tar; do
    command -v "$t" >/dev/null || die "缺少工具 $t"
done
[[ -d "$BUILD_DIR" ]] || die "没有已编译目录 ${BUILD_DIR}（先跑 ./build-x64.sh，或 BUILD_DIR= 指向别的）"
for f in "$APP_NAME" libgoso.dylib libcso.dylib; do
    [[ -f "$BUILD_DIR/$f" ]] || die "缺少已编译产物 $BUILD_DIR/$f"
    is_macho "$BUILD_DIR/$f" || die "$BUILD_DIR/$f 不是 Mach-O（x86_64 包的编译产物才对）"
done
MACDEPLOYQT="$QTDIR/bin/macdeployqt6"
if [ ! -x "$MACDEPLOYQT" ]; then MACDEPLOYQT="$QTDIR/bin/macdeployqt"; fi
if [ ! -x "$MACDEPLOYQT" ]; then MACDEPLOYQT="$(command -v macdeployqt6 || command -v macdeployqt || true)"; fi
[[ -n "$MACDEPLOYQT" ]] || die "找不到 macdeployqt（QTDIR=${QTDIR}）"
[[ -d "$QTDIR/plugins/platforms" ]] || die "没有 ${QTDIR}/plugins/platforms"
[[ -f "$QSKDIR/lib/qskinny/libqskinny.0.8.dylib" ]] || die "没有 ${QSKDIR}/lib/qskinny/libqskinny.0.8.dylib"
[[ -d "$QSKDIR/lib/qskinny/plugins/skins" ]] || die "没有 ${QSKDIR}/lib/qskinny/plugins/skins"

# 版本：取 version_config.h（与 About 页显示一致），可用环境变量覆盖
VERSION="${VERSION:-$(awk -F'"' '/^#define APP_VERSION_NAME/{print $2; exit}' "$VC" 2>/dev/null || true)}"
BUILDNUM="${BUILDNUM:-$(awk '/^#define APP_VERSION_CODE/{print $3; exit}' "$VC" 2>/dev/null || true)}"
if [ -z "$BUILDNUM" ]; then BUILDNUM="$(git rev-list --count HEAD 2>/dev/null || echo 1)"; fi
VERSION="${VERSION:-0.0}"

PKG="$APP_NAME-$VERSION-osx-x64"
APP="$OUT/$APP_NAME.app"
CONTENTS="$APP/Contents"
MACOS_DIR="$CONTENTS/MacOS"
FW="$CONTENTS/Frameworks"
PLUGINS="$CONTENTS/PlugIns"
RES="$CONTENTS/Resources"
EXE="$MACOS_DIR/$APP_NAME"
SIGN_NOTE=""
if [ "$CODESIGN_ID" = "-" ]; then SIGN_NOTE="（ad-hoc，不可公证）"; fi

step "打包 $PKG"
echo "  编译产物 : $BUILD_DIR   arch=$(lipo -archs "$BUILD_DIR/$APP_NAME")"
echo "  Qt       : $QTDIR"
echo "  QSkinny  : $QSKDIR"
echo "  版本     : $VERSION ($BUILDNUM)    bundle id: $BUNDLE_ID"
echo "  签名     : $CODESIGN_ID$SIGN_NOTE"

rm -rf "$OUT"
mkdir -p "$MACOS_DIR" "$FW" "$PLUGINS" "$RES"

# ── 1. 装入二进制 ───────────────────────────────────────────────────
step "1/9 装入二进制"
cp "$BUILD_DIR/$APP_NAME"     "$EXE"
cp "$BUILD_DIR/libgoso.dylib" "$FW/libgoso.dylib"
cp "$BUILD_DIR/libcso.dylib"  "$FW/libcso.dylib"
cp -L "$QSKDIR/lib/qskinny/libqskinny.0.8.dylib" "$FW/libqskinny.0.8.dylib"   # 软链指到 0.8.0 实体
chmod 755 "$EXE" "$FW/libgoso.dylib" "$FW/libcso.dylib" "$FW/libqskinny.0.8.dylib"
# libqskinny 自己的 rpath 只有 /opt/qt/qskinny/lib/qskinny，macdeployqt 遍历到它时解析不了
# @rpath/QtQuick 等（报 "Cannot resolve rpath"）。先临时补一条 Qt 目录让部署过程安静，
# 第 6 步会把所有 /opt/* 绝对 rpath 删掉。
ensure_rpath "$FW/libqskinny.0.8.dylib" "${QTDIR}/lib"
# 图标实体放 Contents/Resources/：Contents/ 顶层本身就是 code 位置
# （Apple TN2206 "Nested Code" Table 3 第一行：Contents — Top content directory of the bundle，
#   并注明 "These places are expected to contain only code. Putting arbitrary data files there
#   will cause them to be rejected (since they're unsigned)"）。
# 所以把数据文件留在 Contents/app_icon.png 会被 codesign 判成"未签名的嵌套 code"并拒签：
#   In subcomponent: .../Contents/app_icon.png
# 真拷贝到 Resources/ 是允许的；src/main.cpp 现在读 ../Resources/app_icon.png。
# 【不能用 cp】：源文件在共享盘上是 0770，cp 会把这个模式带进 bundle；
# 改成"先建空文件、再灌内容"：mode 走文件系统的创建默认，不继承源文件。
: > "$RES/app_icon.png"
cat app_icon.png > "$RES/app_icon.png"
chmod 644 "$RES/app_icon.png"
# macOS 上 Dock/窗口图标取的是 bundle 的 .icns（CFBundleIconFile），不是 main.cpp 的
# setWindowIcon 那份 png（那份在没重编前读不到 Contents/app_icon.png）。
# 生成用 Apple 官方通道 iconutil：先做 .iconset 目录（标准 10 个尺寸），再合成 icns。
# 不直接用 sips -s format icns：部分 macOS/源图尺寸下它产出的 icns 连 sips 自己都读不回。
ICONSET="$OUT/.appicon.iconset"
rm -rf "$ICONSET"
mkdir -p "$ICONSET"
for s in 16 32 128 256 512; do
    sips -z "$s" "$s" app_icon.png --out "$ICONSET/icon_${s}x${s}.png" >/dev/null 2>&1
    sips -z "$((s*2))" "$((s*2))" app_icon.png --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null 2>&1
done
iconutil -c icns "$ICONSET" -o "$RES/app_icon.icns" || die "iconutil 合成 icns 失败"
rm -rf "$ICONSET"
chmod 644 "$RES/app_icon.icns"

# ── 2. Info.plist（图标由 app_icon.icns 提供，登记 CFBundleIconFile）────
step "2/9 写 Info.plist"
cat > "$CONTENTS/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key>     <string>en</string>
	<key>CFBundleExecutable</key>            <string>$APP_NAME</string>
	<key>CFBundleIdentifier</key>            <string>$BUNDLE_ID</string>
	<key>CFBundleInfoDictionaryVersion</key> <string>6.0</string>
	<key>CFBundleName</key>                  <string>$APP_NAME</string>
	<key>CFBundleDisplayName</key>           <string>Anystik</string>
	<key>CFBundlePackageType</key>           <string>APPL</string>
	<key>CFBundleShortVersionString</key>    <string>$VERSION</string>
	<key>CFBundleVersion</key>               <string>$BUILDNUM</string>
	<key>LSMinimumSystemVersion</key>        <string>$MIN_MACOS</string>
	<key>LSApplicationCategoryType</key>     <string>public.app-category.social-networking</string>
	<key>NSHighResolutionCapable</key>       <true/>
</dict>
</plist>
PLIST
plutil -lint "$CONTENTS/Info.plist" >/dev/null || die "Info.plist 不合法"

# ── 3. macdeployqt 收 Qt frameworks（顺带改写它自己那套 rpath）──────
# 两个坑：
#  a) 参数顺序：macdeployqt 把 argv[1] 当 app bundle 路径，选项必须放在 bundle 之后。
#     写成 `macdeployqt -no-plugins My.app` 时 argv[1] 以 "-" 开头，它直接打 usage 并非 0 退出。
#  b) Qt 6.7 的 macdeployqt 没有 -no-translations（也没有 -no-quicklook / -no-icu 等），传了同样报 usage。
step "3/9 macdeployqt 部署 Qt frameworks"
MDQ_LOG="$OUT/macdeployqt.log"
if ! "$MACDEPLOYQT" "$PWD/$APP" -no-plugins >"$MDQ_LOG" 2>&1; then
    cat "$MDQ_LOG" >&2
    die "macdeployqt 失败（完整日志：${MDQ_LOG}）"
fi
if [ ! -d "$FW/QtCore.framework" ]; then   # 退出码 0 却没部署 = 静默失败，在这里就炸掉
    cat "$MDQ_LOG" >&2
    die "macdeployqt 没部署任何 framework（${FW}/QtCore.framework 不存在），日志：${MDQ_LOG}"
fi
# 过滤两类已知无害的噪音：libqskinny 解析不到 Qt（主程序已把 Qt framework 全拉进 bundle，
# 第 7 步自检会再确认一次）、裸名 libgoso.dylib 在 /usr/lib 找不到（第 5 步会改成 @rpath）。
MDQ_WARN="$(grep -Ei "error|warning" "$MDQ_LOG" 2>/dev/null | grep -v -e "Cannot resolve rpath" -e "using QList" -e "no file at" || true)"
if [ -n "$MDQ_WARN" ]; then
    printf '%s\n' "$MDQ_WARN" | head -5
    echo "  ! macdeployqt 有告警，详见 ${MDQ_LOG}"
fi
# 3.0 macdeployqt 会重写 Info.plist，可能把登记的任何图标键吞掉；所以 CFBundleIconFile
#     挪到部署之后用 plutil 补写（-remove 兜住"键还在"的情况，再 -insert 保证最终值是 app_icon）。
#     校验用 plutil -p | grep 纯文本比对：plutil -extract raw 需要 macOS 12+，
#     这台 MIN_MACOS=11 的机器上会报 "<unknown error>"（实测）。
plutil -remove CFBundleIconFile "$CONTENTS/Info.plist" 2>/dev/null || true
plutil -insert CFBundleIconFile -string app_icon "$CONTENTS/Info.plist" \
    || { plutil -p "$CONTENTS/Info.plist" | grep -i icon; die "无法写入 CFBundleIconFile"; }
plutil -lint "$CONTENTS/Info.plist" >/dev/null || die "Info.plist 不合法"
plutil -p "$CONTENTS/Info.plist" | grep -Eq 'CFBundleIconFile[[:space:]]*=>[[:space:]]*app_icon' \
    || {
        echo "  ⚠ CFBundleIconFile 未写入（macdeployqt 之后没有该键）→ 整文件重写兜底"
        cat > "$CONTENTS/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key>     <string>en</string>
	<key>CFBundleExecutable</key>            <string>$APP_NAME</string>
	<key>CFBundleIdentifier</key>            <string>$BUNDLE_ID</string>
	<key>CFBundleInfoDictionaryVersion</key> <string>6.0</string>
	<key>CFBundleName</key>                  <string>$APP_NAME</string>
	<key>CFBundleDisplayName</key>           <string>Anystik</string>
	<key>CFBundlePackageType</key>           <string>APPL</string>
	<key>CFBundleShortVersionString</key>    <string>$VERSION</string>
	<key>CFBundleVersion</key>               <string>$BUILDNUM</string>
	<key>LSMinimumSystemVersion</key>        <string>$MIN_MACOS</string>
	<key>LSApplicationCategoryType</key>     <string>public.app-category.social-networking</string>
	<key>CFBundleIconFile</key>              <string>app_icon</string>
	<key>NSHighResolutionCapable</key>       <true/>
</dict>
</plist>
PLIST
        plutil -lint "$CONTENTS/Info.plist" >/dev/null || die "兜底重写 Info.plist 不合法"
    }
# 3.1 macdeployqt 只按【主程序】的依赖决定 framework，而 QtSvg/QtWidgets 只被插件需要：
#     -no-plugins 让它不碰插件，于是它俩不会被部署，可 libqsvg/libqsvgicon/libqmacstyle 链的就是它们。
#     $QTDIR 里这两个 framework 原生已是部署态（rpath=@loader_path/../../../，依赖全是 @rpath/QtXxx），
#     所以直接 cp -R 即可，不需要任何 install_name_tool 手术（也不会破坏 macdeployqt 的签名）。
#     拷完由第 7 步自检兜底验证。
for fw in QtSvg QtWidgets; do
    if [ ! -d "$FW/$fw.framework" ]; then
        if [ ! -d "$QTDIR/lib/$fw.framework" ]; then
            echo "  ! $QTDIR 里没有 $fw.framework，插件里链它的会加载失败"
            continue
        fi
        cp -R "$QTDIR/lib/$fw.framework" "$FW/$fw.framework"     # -R 保留 Versions/Current 等软链
        echo "  + $fw.framework（macdeployqt -no-plugins 不会为插件补它）"
    fi
done

# ── 4. 精简 Qt 插件（全量 plugins 288MB，Designer/WebEngine 用不到）──
step "4/9 拷精简 Qt 插件"
copy_plugin() {  # $1=插件子目录  $2..=文件名（省略=该目录下全部 .dylib）
    local sub="$1"; shift
    local src="$QTDIR/plugins/$sub" dst="$PLUGINS/$sub" f
    if [ ! -d "$src" ]; then echo "  ! 缺插件目录 $sub"; return 0; fi
    mkdir -p "$dst"
    if [ $# -gt 0 ]; then
        for f in "$@"; do
            if [ -f "$src/$f" ]; then
                cp -p "$src/$f" "$dst/"
            else
                echo "  ! 缺 $sub/$f"
            fi
        done
    else
        for f in "$src"/*.dylib; do
            if [ -f "$f" ]; then cp -p "$f" "$dst/"; fi
        done
    fi
}
copy_plugin platforms libqcocoa.dylib                        # 唯一需要的平台插件
copy_plugin imageformats libqgif.dylib libqjpeg.dylib libqicns.dylib libqico.dylib \
                         libqwebp.dylib libqtiff.dylib libqtga.dylib libqsvg.dylib
copy_plugin tls libqopensslbackend.dylib libqsecuretransportbackend.dylib
copy_plugin iconengines
copy_plugin styles
copy_plugin generic

# QSkinny 皮肤：app 把 QCoreApplication::libraryPaths() 交给 QskSkinManager（main.cpp:284），
# 而 bundle 的 Frameworks 在其中。同一份放两处：Frameworks/skins/（<path>/skins 约定）
# 与 Frameworks/（直接扫目录），工厂按 factoryId 去重，不会重复加载。
step "  qskinny 皮肤插件"
mkdir -p "$FW/skins"
for f in "$QSKDIR/lib/qskinny/plugins/skins/"*.dylib; do
    if [ -f "$f" ]; then
        cp -p "$f" "$FW/$(basename "$f")"
        cp -p "$f" "$FW/skins/$(basename "$f")"
    fi
done

cat > "$RES/qt.conf" <<'CONF'
[Paths]
Prefix = ../Frameworks
Plugins = ../PlugIns
CONF

# ── 5. 依赖闭包：把编译机上的绝对依赖收进 Frameworks ────────────────
# 编译产物直接引用 /opt/qt/...、/usr/local/opt/openssl@3/...、/nix/store/.../libc++，
# 换台 Mac 双击就是 "Library not loaded"。这里按依赖闭包逐个拷进 Frameworks 并改写引用。
step "5/9 收敛依赖闭包（openssl / libc++ / go dylib）"
ensure_id() {  # id 统一用 @rpath/<名字>：与第 5 步改写出来的 @rpath/libX.dylib 一致。
    # 解析靠调用方的 rpath（第 6 步给每个非 framework 镜像都补了 ../Frameworks），与 id 无关；
    # 裸文件名 id 其实也能被已加载镜像按 leaf 匹配到，但统一成 @rpath 更好读、也跟自检对得上。
    local f="$1" cur
    cur="$(id_of "$f")"
    if [ "$cur" != "@rpath/$(basename "$f")" ]; then
        install_name_tool -id "@rpath/$(basename "$f")" "$f"
    fi
}
WORK="$OUT/.worklist"; SEEN="$OUT/.seen"
nixmiss=0        # nix 依赖没改掉的条数：只警告、不中断，跑完统一汇总
: > "$WORK"; : > "$SEEN"
enqueue() {
    if [ -f "$1" ] && ! grep -qxF "$1" "$SEEN" 2>/dev/null; then printf '%s\n' "$1" >> "$WORK"; fi
}
fix_image() {  # 修一个镜像的依赖
    # dep = 加载命令里【原本的】那个串（-change 只能拿它去匹配）；
    # src = 实际要复制的文件（兜底找到的可能是别处的同名文件）。两者不能混。
    local img="$1" dep src base dst alt
    while IFS= read -r dep; do
        case "$dep" in
            /System/*|/usr/lib/*|@rpath/*|@loader_path/*|@executable_path/*) continue ;;
            *.framework/*) die "框架依赖 ${dep} 应由 macdeployqt 处理（${img}）" ;;
        esac
        base="${dep##*/}"
        dst="$FW/${base}"
        src="${dep}"
        if [ ! -f "${dst}" ] && [ ! -f "${src}" ]; then
            # 编译机上路径变了（nix store 被 gc/换 hash、Homebrew 换前缀）→ 找同名文件
            if alt="$(find_alt "${base}")"; then
                case "${alt}" in
                    /usr/lib/*|/System/*)   # OS 自带就别打包，直接把引用指过去
                        echo "  · ${base}：改用系统自带 ${alt}（不打包）"
                        if has_dep "${img}" "${dep}"; then
                            install_name_tool -change "${dep}" "${alt}" "${img}"
                            echo "    ${img##*/}: ${dep} → ${alt}"
                        fi
                        continue ;;
                esac
                echo "  ~ 兜底找到 ${base}：${alt}"
                src="${alt}"
            fi
        fi
        if [ ! -f "${dst}" ]; then
            if [ ! -f "${src}" ]; then
                # nix 路径单独一档：只警告、不停（这轮优先把包产出来），
                # 但必须喊清楚——这种依赖在别的 Mac 上必然 "Library not loaded"。
                case "${dep}" in
                    /nix/*) echo "  ⚠ 依赖是 nix 路径且本机没有：${dep}（${img}）——没改掉，别的 Mac 上会加载失败"
                            nixmiss=$((nixmiss+1)) ;;
                    *)      echo "  ! 依赖缺失，无法收进 bundle：${dep}（${img}）" ;;
                esac
                continue
            fi
            cp -p "${src}" "${dst}"
            chmod u+w "${dst}"
            chmod 755 "${dst}"
            echo "  + $base  ←  $src"
        fi
        ensure_id "$dst"
        if has_dep "$img" "$dep"; then
            install_name_tool -change "$dep" "@rpath/$base" "$img"
            echo "  · $(basename "$img"): $dep → @rpath/$base"
        fi
        enqueue "$dst"        # 递归：新收进来的库还有自己的依赖
    done < <(deps_of "$img")
}
# 入队整个 bundle 的 Mach-O（含已就位的 libcso/libgoso/libqskinny 与各插件），
# 而不只是主程序 —— 它们自己也可能硬链编译机上的绝对路径。
while IFS= read -r m; do enqueue "$m"; done < <(list_macho)
while [ -s "$WORK" ]; do
    img=$(head -1 "$WORK")
    tail -n +2 "$WORK" > "$WORK.tmp"
    mv "$WORK.tmp" "$WORK"
    printf '%s\n' "$img" >> "$SEEN"
    fix_image "$img"
done

# ── 6. rpath 收尾 ──────────────────────────────────────────────────
step "6/9 修 rpath"
# 6.1/6.2 合并成一个循环，遍历 bundle 内除 framework 主体外的所有 Mach-O：
#     · @executable_path = Contents/MacOS，@executable_path/../Frameworks 才是库目录。
#       macdeployqt 已经给主程序加过后者，install_name_tool 遇重复 rpath 会报错退出，
#       所以必须先查再加（ensure_rpath）。
#     · 删掉 /opt/qt/...、<编译机>/build-x64、/nix/... 这类绝对 rpath，否则换机仍会去编译机找库。
#     · 遍历用 list_macho 而不是 find -name '*.dylib'：libcso/libgoso/libqskinny 名字不固定，
#       而且第 1 步给 libqskinny 临时加的 Qt rpath 也得在这里被删掉。
#     · 跳过 framework 主体（Versions/A/*）：它们已被 macdeployqt 改成
#       @loader_path/../../../ + 纯 @rpath 依赖（自检已确认），再改会破坏它的签名。
while IFS= read -r f; do
    case "$f" in *.framework/*) continue ;; esac
    ensure_rpath "$f" "@executable_path"
    ensure_rpath "$f" "@executable_path/../Frameworks"
    # id 也统一收口：第 5 步的 ensure_id 只覆盖"它自己收进来的库"，
    # 第 1 步手工 cp 的 libgoso/libcso/libqskinny、拷进来的插件都还带着编译机绝对 id
    # （libcso.dylib 的 id 就是 /Users/gzleo/.../build-x64/libcso.dylib）。
    ensure_id "$f"
    for rp in $(rpaths_of "$f"); do
        case "$rp" in
            /opt/*|/Users/*|/nix/*)
                if del_rpath "$f" "$rp"; then echo "  · ${f##*/}: 删编译机 rpath ${rp}"; fi ;;
        esac
    done
done < <(list_macho)
# 收尾断言：bundle 内不允许还残留编译机绝对 rpath（换了机器就找不到 Qt/库）
left=""
while IFS= read -r f; do
    for rp in $(rpaths_of "$f"); do
        case "$rp" in /opt/*|/Users/*|/nix/*) left="${left}
      ${f}: ${rp}" ;; esac
    done
done < <(list_macho)
if [ -n "$left" ]; then printf '%s\n' "$left"; die "还有编译机 rpath 没删干净"; fi

# ── 7. 自检 + 签名 ──────────────────────────────────────────────────
step "7/9 依赖自检"
bad=0
while IFS= read -r f; do
    while IFS= read -r dep; do
        case "$dep" in
            /System/*|/usr/lib/*) continue ;;
            @rpath/*)                        # 必须在 Frameworks 里有实体
                b="${dep#@rpath/}"
                if [ ! -f "$FW/$b" ]; then echo "  ✗ ${f} → ${dep}（bundle 内无此文件）"; bad=1; fi ;;
            @loader_path/*|@executable_path/*) ;;
            # nix 路径：只警告、不中断（本轮优先出包）。但一定要喊清楚——这种依赖
            # 在别的 Mac 上必然 "Library not loaded"，是"能出包"而不是"包是好的"。
            /nix/*) echo "  ⚠ ${f} → ${dep}（nix 路径没改掉：本机能跑、换机会 Library not loaded）"
                    nixmiss=$((nixmiss+1)) ;;
            */*)    echo "  ✗ ${f} → ${dep}（bundle 外绝对路径，换机必崩）"; bad=1 ;;
            # 裸名（无斜杠）：dyld 按 leaf 名匹配已加载镜像 / 搜 rpath，所以只要 bundle 里有同名文件即可
            *)   b="${dep}"
                 if [ ! -f "$FW/$b" ] && [ ! -f "$(dirname "$f")/$b" ]; then
                     echo "  ✗ ${f} → ${dep}（裸名，bundle 内找不到同名文件）"; bad=1
                 fi ;;
        esac
    done < <(deps_of "$f")
    # id 不能是 bundle 外的绝对路径：libcso/libgoso/libqskinny/插件都是第 1 步手工 cp 的，
    # 带着 /Users/gzleo/.../build-x64/... 这种编译机 id（换机虽不影响 dyld，但已对不上包内布局）
    case "$f" in *.framework/*) continue ;; esac   # framework 主体的 id 是 macdeployqt 的地盘，不查
    id="$(id_of "$f" 2>/dev/null || true)"
    if [ -n "$id" ]; then
        case "$id" in
            @rpath/*) ;;
            *) echo "  ✗ ${f} 的 id=${id}（非 @rpath/…）"; bad=1 ;;
        esac
    fi
done < <(list_macho)
if [ "$bad" != 0 ]; then die "依赖自检未通过，先解决上面 ✗ 项"; fi
if [ "$nixmiss" = 0 ]; then
    echo "  ✓ 所有依赖都已收进 bundle"
else
    echo "  ⚠ 其余依赖都已收进 bundle，但有 ${nixmiss} 条 nix 依赖没改掉（就是上面那些 ⚠）"
    echo "    这包在本机能跑，换台 Mac 会 Library not loaded；根治要重编（CMakeLists 已钉 -stdlib=libc++）"
fi

if [ "$STRIP" = 1 ]; then
    strip -S -x "$EXE" "$FW/libgoso.dylib" "$FW/libcso.dylib" 2>/dev/null || true
    echo "  · 已 strip 主程序与自带 dylib"
fi

# 结构断言：code 位置（Contents/ 顶层、Contents/MacOS/）里只许有 code。
# Apple TN2206：这些位置"只放 code"，塞进去的数据文件会被当成未签名的嵌套 code，
# codesign 整个 .app 拒签（In subcomponent: ...）。图标那个坑就是这么炸的。
# 例外：Info.plist / PkgInfo 规定就住在 Contents/；软链不是 -type f，天然不查。
stray=""
while IFS= read -r f; do
    is_macho "$f" || stray="${stray}
    ${f}"
done < <(find "$CONTENTS" -maxdepth 1 -type f ! -name 'Info.plist' ! -name 'PkgInfo'
         find "$CONTENTS/MacOS" -type f)
if [ -n "$stray" ]; then
    printf '%s\n' "$stray"
    die "code 位置里有非 code 文件（见上），codesign 会拒签 .app；数据文件请放 Contents/Resources/"
fi

# 签名顺序是硬约束：内层 code 必须全部先签完，最后签 .app。
# 之前这版有三个问题（都会导致 "code object is not signed at all"）：
#   ① find 的 -prune 把 framework 整个跳过了 → framework 从来没被签；
#      而 $QTDIR 里的 Qt framework 本身是【没有签名】的（没有 _CodeSignature），
#      macdeployqt 会给自己部署的那些签上，但 3.1 步 cp -R 补的 QtSvg/QtWidgets 是裸拷过来的。
#   ② codesign 的输出被 2>/dev/null || true 吞掉，真报错只能看到一句没头没尾的 In subcomponent。
#   ③ framework 要先签 Versions/A/<名> 主体、再签 framework 包本身，顺序反了包签名就失效。
if [ "$CODESIGN_ID" = "-" ]; then
    SIGN_ARGS=(--force --sign -)
else
    SIGN_ARGS=(--force --options runtime --timestamp -s "$CODESIGN_ID")
fi
sign1() {   # 签一个；失败就把 codesign 原话打出来并计数。
    # 永远返回 0：失败是"只警告"策略的一部分，不能让 set -e 顺手把脚本掐掉。
    if ! codesign "${SIGN_ARGS[@]}" "$1"; then
        echo "  ! 签名失败: $1" >&2
        signfail=$((signfail+1))
    fi
    return 0
}
signfail=0
# ① framework：主体 → 包
while IFS= read -r fw; do
    nm="$(basename "$fw" .framework)"
    if [ -f "$fw/Versions/A/$nm" ]; then
        sign1 "$fw/Versions/A/$nm"
    fi
    sign1 "$fw"
done < <(find "$FW" -maxdepth 1 -type d -name '*.framework' | sort)
# ② Frameworks 下所有散着的 dylib：libgoso/libcso/libqskinny/openssl 兜底收进来的，
#    以及 skins/ 里的皮肤插件（必须递归：QSkinny 皮肤落在 Frameworks/skins/ 子目录，
#    早先写成 -maxdepth 1，结果皮肤插件全都没签，.app 签名直接失败）
while IFS= read -r d; do sign1 "$d"; done \
    < <(find "$FW" -type f -name '*.dylib' ! -path '*.framework/*' | sort)
# ③ 插件与皮肤
while IFS= read -r d; do sign1 "$d"; done \
    < <(find "$PLUGINS" -type f -name '*.dylib' | sort)
# ④ 主程序 ⑤ app 本体
sign1 "$EXE"
sign1 "$APP"
# 本轮策略：签名问题【只警告、不中断】——先拿到 app/tgz/dmg。
# 代价说清楚：seal 不完整的包在别的 Mac 上过不了 Gatekeeper（本机双击不受影响）。
# 想恢复严格模式（签不过就 die）就把下面两处 die 加回来。
if [ "$signfail" != 0 ]; then
    echo "  ⚠ 有 ${signfail} 个 code 没签成功（上面 codesign 有原话）"
    echo "    先出包，但这包的签名 seal 不完整：别的 Mac 上过不了 Gatekeeper"
fi
rm -f "$WORK" "$WORK.tmp" "$SEEN"

# ── 8. .tgz ────────────────────────────────────────────────────────
step "8/9 生成 .tgz"
# COPYFILE_DISABLE=1：共享目录（非 HFS 卷）上避免打出 AppleDouble 的 ._xxx 条目
COPYFILE_DISABLE=1 tar -C "$OUT" -czf "$OUT/$PKG.tgz" "$APP_NAME.app"
echo "  → $OUT/$PKG.tgz  ($(du -h "$OUT/$PKG.tgz" | cut -f1))"

# ── 9. .dmg ────────────────────────────────────────────────────────
step "9/9 生成 .dmg"
DMG_STAGE="$OUT/.dmgstage"
rm -rf "$DMG_STAGE"
mkdir -p "$DMG_STAGE"
# -c = APFS 克隆，不额外占盘；非 APFS 退回普通拷贝
if ! cp -c -R "$APP" "$DMG_STAGE/" 2>/dev/null; then
    rm -rf "$DMG_STAGE/$APP_NAME.app"
    cp -R "$APP" "$DMG_STAGE/"
fi
ln -s /Applications "$DMG_STAGE/Applications"
hdiutil create -volname "$APP_NAME $VERSION" -srcfolder "$DMG_STAGE" -ov -format UDZO "$OUT/$PKG.dmg" >/dev/null
rm -rf "$DMG_STAGE"
echo "  → $OUT/$PKG.dmg  ($(du -h "$OUT/$PKG.dmg" | cut -f1))"

( cd "$OUT" && shasum -a 256 "$PKG.tgz" "$PKG.dmg" > sha256sums.txt )

step "完成"
cat "$OUT/sha256sums.txt"
echo
echo "  app 体积（含 Qt frameworks）: $(du -sh "$APP" | cut -f1)"
echo "  本机试跑 : open $APP"
echo "  安装     : 双击 .dmg 拖进 Applications"
echo "  换机分发 : 需真证书 + 公证，CODESIGN_ID='Developer ID Application: xxx' 重跑"
echo "  ⚠️ 若 HTTPS 报 TLS 初始化失败：删掉 $PLUGINS/tls/libqopensslbackend.dylib 重打包，"
echo "     让 Qt 退回系统 SecureTransport（包内已带 openssl，但 dlopen 有时挑不到）"

# ── 最终产物校验（打包全部完成后收尾）────────────────────────────────
# 图标：CFBundleIconFile 登记 + .icns 合法 + 包内 png 与源图一致。
# 签名：codesign --verify（本机 seal 校验）。打包已完成，失败只警告、不中断。
step "最终产物校验"
ok=1
[ -s "$RES/app_icon.icns" ] \
    || { echo "  ✗ 包内缺 $RES/app_icon.icns"; ok=0; }
[ "$(head -c4 "$RES/app_icon.icns")" = "icns" ] \
    || { echo "  ✗ $RES/app_icon.icns 不是 icns（magic 应为 icns）"; ok=0; }
plutil -p "$CONTENTS/Info.plist" | grep -Eq 'CFBundleIconFile[[:space:]]*=>[[:space:]]*app_icon' \
    || { echo "  ✗ CFBundleIconFile 应为 app_icon（plist 全文的 icon 行见下）"; plutil -p "$CONTENTS/Info.plist" | grep -i icon; ok=0; }
cmp -s app_icon.png "$RES/app_icon.png" \
    || { echo "  ✗ $RES/app_icon.png 与源图不一致"; ok=0; }
if [ "$ok" = 1 ]; then
    echo "  ✓ 图标自检通过：app_icon.icns + CFBundleIconFile=app_icon + 源图一致"
else
    die "图标自检未通过（见上）"
fi
if codesign --verify --strict "$APP" 2>/dev/null; then
    echo "  ✓ 最终产物签名校验通过（--strict）"
else
    echo "  ⚠ 最终产物签名校验（--strict）没过："
    codesign --verify --strict "$APP" 2>&1 | sed 's/^/    /'
fi
codesign --verify --deep --strict "$APP" 2>/dev/null \
    && echo "  ✓ 最终产物签名校验通过（--deep --strict）" \
    || echo "  · --deep 复核没过（本机运行不受影响，仅记录）"
