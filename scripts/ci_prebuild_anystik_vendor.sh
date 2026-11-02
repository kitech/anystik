#!/bin/bash
# 归位 anystik Android 构建所需的 vendor 预编译库到 vendor/
# 来源与 SHA256 记录见 vendor/vendorinfos.md
set -eux

pwd
PROJDIR=$PWD
VENDOR_DIR="$PROJDIR/vendor"
LIB_DIR="$VENDOR_DIR/lib"
ABI_DIR="$LIB_DIR/arm64-v8a"
V7A_DIR="$LIB_DIR/armeabi-v7a"
X64_DIR="$LIB_DIR/x86_64"
X86_DIR="$LIB_DIR/x86"
# GitHub 直连被阻断，下载一律走 gh-proxy 前缀
GH_RAW="https://gh-proxy.org/https://github.com"

mkdir -p "$VENDOR_DIR/include" "$ABI_DIR" "$V7A_DIR" "$X64_DIR" "$X86_DIR"

# ── curl 8.19.0 + OpenSSL 3.6.x (XDcobra prebuilt) ──
# 下载到 /tmp，避免污染 workspace
wget -q -O /tmp/libcurl-android.zip \
    "$GH_RAW/XDcobra/libcurl-ios-android-prebuilt-and-buildscripts/releases/download/v8.19.0-1/libcurl-android.zip"

rm -rf /tmp/libcurl-android && mkdir -p /tmp/libcurl-android
unzip -q /tmp/libcurl-android.zip -d /tmp/libcurl-android

# jniLibs 目录名实测（不凭 README 假设；任一 ABI 缺失即中止）
# v8.19.0-1 README 明列 4 ABI（arm64-v8a/armeabi-v7a/x86/x86_64），此处兜底防上游改版
JNILIB_ROOT=$(find /tmp/libcurl-android -type d -name arm64-v8a | head -1)
if [ -z "$JNILIB_ROOT" ]; then
    echo "FATAL: arm64-v8a dir not found in libcurl-android.zip"
    exit 1
fi
for d in armeabi-v7a x86_64 x86; do
    if [ ! -d "$(dirname "$JNILIB_ROOT")/$d" ]; then
        echo "FATAL: jniLibs/$d not found in libcurl-android.zip"
        exit 1
    fi
done

# 按文件名归位头文件。
# 注意：必须先对 find 结果判空再 dirname —— dirname '' 返回 '.'，
# 会触发 cp -r . 自拷贝事故（v8.19.0-1 CI 实踩）
CURL_H=$(find /tmp/libcurl-android -name curl.h -path '*curl*' | head -1)
if [ -z "$CURL_H" ]; then
    echo "FATAL: curl.h not found in libcurl-android.zip"
    exit 1
fi
cp -r "$(dirname "$CURL_H")" "$VENDOR_DIR/include/curl"

# 该 release 包不含 OpenSSL 头（v8.19.0-1 实测无 ssl.h）。
# anystik 无直接 #include <openssl/>，链接仅需 .so，缺失无影响 → 不安装 openssl 头

# .so 归位 per-ABI —— 锚定 openssl 变体目录（zip 内可能有多种 SSL 后端变体，
# 无限定会被 find 遍历顺序随机命中其它变体，v8.19.0-1 CI 实踩）。
# ABI 锚点语义：arm64 命中 'arm64|aarch64'；v7a 命中 'armeabi'（与 arm64 不同名不撞车）；
# x86_64 锚 'x86_64'（不会误命中 'x86' 段）；x86 用段锚 '/x86/'（不命中 x86_64）
for abi in arm64-v8a armeabi-v7a x86_64 x86; do
    case "$abi" in
        arm64-v8a)   ATMATCH='arm64|aarch64' ;;
        armeabi-v7a) ATMATCH='armeabi-v7a' ;;
        x86_64)      ATMATCH='x86_64' ;;
        x86)         ATMATCH='/x86/' ;;
    esac
    case "$abi" in
        arm64-v8a)   DEST="$ABI_DIR" ;;
        armeabi-v7a) DEST="$V7A_DIR" ;;
        x86_64)      DEST="$X64_DIR" ;;
        x86)         DEST="$X86_DIR" ;;
    esac
    for so in libcurl libssl libcrypto; do
        SRC=$(find /tmp/libcurl-android -name "${so}.so" \
            -path '*libcurl-openssl*' | grep -E "$ATMATCH" | head -1)
        if [ -z "$SRC" ]; then
            echo "FATAL: $so.so not found for $abi in libcurl-android.zip"
            exit 1
        fi
        cp "$SRC" "$DEST/"
    done
done

# ── SQLite 3.53.3 (sqlite3.dart prebuilt) ──
# 上游命名映射：arm64-v8a=arm64 / armeabi-v7a=arm / x86_64=x64 / x86=ia32
wget -q -O "$ABI_DIR/libsqlite3.so" \
    ${GH_RAW}/simolus3/sqlite3.dart/releases/download/sqlite3-3.4.0/libsqlite3.arm64.android.so
wget -q -O "$V7A_DIR/libsqlite3.so" \
    ${GH_RAW}/simolus3/sqlite3.dart/releases/download/sqlite3-3.4.0/libsqlite3.arm.android.so
wget -q -O "$X64_DIR/libsqlite3.so" \
    ${GH_RAW}/simolus3/sqlite3.dart/releases/download/sqlite3-3.4.0/libsqlite3.x64.android.so
wget -q -O "$X86_DIR/libsqlite3.so" \
    ${GH_RAW}/simolus3/sqlite3.dart/releases/download/sqlite3-3.4.0/libsqlite3.ia32.android.so

# ── SHA256 校验（摆错文件此处即失败）──
# arm64 四项与各 sqlite 三项为上游实取摘要；
# curl/ssl/crypto 的 armeabi-v7a/x86_64/x86 九项为 v8.19.0-1 zip 首装实测摘要
cd "$LIB_DIR"
cat > /tmp/vendor.sha256 <<'EOF'
5ceb34ff92d9f6cd6b28901cc220bc2917a53e2614e8c9f6764af18c89063b88  arm64-v8a/libcurl.so
96d844acd9b264face6529b3502269577c1c14843cef4b55031deb114db8f0a7  arm64-v8a/libssl.so
953e2c534771b09e022bf4ef3d7d3ff4c18ba10241fb430036d1147399a28a90  arm64-v8a/libcrypto.so
e99515af1d7119fb61843ae5e597344e7f258563de3a7e5a3869f627aab2887b  arm64-v8a/libsqlite3.so
6c1b8dffc1ddefaf02e771711491410bc3ab1db858d3d23d2925e0b2cd691b93  armeabi-v7a/libsqlite3.so
e5a2d46ac5e11f471e1aaedfd364f54c7961a6900432d289ea3f5781bcaaf4cd  x86_64/libsqlite3.so
6d73649ad5896eb276aca304456b8664c0f9a82d6c7785c5975d43e0462d3777  x86/libsqlite3.so
f464e7cf02fcfe5aca2be297dbdc2267766200b871460fd4eb14a08af43261a8  armeabi-v7a/libcurl.so
6d4d280c4d7a662f298b81b8876ff625586328b197851c8266ea43cd41399786  armeabi-v7a/libssl.so
ba9cdc201710dbe00f32232db88007fd12f23de62900d0aeb7172601e082a889  armeabi-v7a/libcrypto.so
3cb2369dd50e934c89eb01612034e7efe7c507c9989e6f7bf7f337b729c896d8  x86_64/libcurl.so
08ffe59ca4699e5b897d386d6ea700ff6b611f373a51cdee4deb4296ccab959d  x86_64/libssl.so
7d077de429e82061192a7bf2438aa6330f5f73e38b70beea1905227af91ffbfa  x86_64/libcrypto.so
a3447a3b8cfcda8bda6609d50657002840d009ae09332f88777ba0644e71b662  x86/libcurl.so
a31c38ea468a9d3cd880c7593663deb420ad3d55d2d9d0dccf354285daa02c32  x86/libssl.so
2712a476ac7eceaa4eb794397c7a7b9f9c82f5e93604caabc9b45de36c097f79  x86/libcrypto.so
EOF
if ! sha256sum -c /tmp/vendor.sha256; then
    echo "=== DIAGNOSIS: actual hashes of downloaded files ==="
    sha256sum arm64-v8a/*.so armeabi-v7a/*.so x86_64/*.so x86/*.so
    echo "=== DIAGNOSIS: all libcurl*.so candidates in zip ==="
    find /tmp/libcurl-android -name "libcurl*.so" -exec sha256sum {} \;
    exit 1
fi

# ── Qt 6 Android TLS 后端 libssl_3.so / libcrypto_3.so（CI 自产，见 vendorinfos.md）──
# 由上方已下载并校验过的裸名库就地派生，不入库。
# 注意：libssl_3 不仅要改 SONAME，还要把 NEEDED 由 libcrypto.so 改指 libcrypto_3.so
# （二者 NEEDED 不同，仅改 SONAME 会让 Qt 加载到裸名 libcrypto）。
# patchelf 存在「同一命令混合多个操作会相互破坏」的已知缺陷
# （NixOS/patchelf #359 / #193 / #158）→ 一律拆成独立调用
echo "patchelf: $(patchelf --version)"
for TLS_DIR in "$ABI_DIR" "$V7A_DIR" "$X64_DIR" "$X86_DIR"; do
(
    cd "$TLS_DIR"
    cp libssl.so libssl_3.so
    cp libcrypto.so libcrypto_3.so
    patchelf --set-soname libssl_3.so libssl_3.so
    patchelf --replace-needed libcrypto.so libcrypto_3.so libssl_3.so
    patchelf --set-soname libcrypto_3.so libcrypto_3.so
    # 双核验：patchelf 自读（与写工具版本一致）+ readelf（解析有效性）
    patchelf --print-soname libssl_3.so    | grep -qx 'libssl_3\.so'    || { echo "FATAL: $TLS_DIR libssl_3.so SONAME";    exit 1; }
    patchelf --print-needed libssl_3.so    | grep -qx 'libcrypto_3\.so' || { echo "FATAL: $TLS_DIR libssl_3.so NEEDED";    exit 1; }
    patchelf --print-soname libcrypto_3.so | grep -qx 'libcrypto_3\.so' || { echo "FATAL: $TLS_DIR libcrypto_3.so SONAME"; exit 1; }
    readelf -d libssl_3.so    | grep -q 'SONAME.*libssl_3\.so'    || { echo "FATAL: $TLS_DIR libssl_3.so readelf SONAME";    exit 1; }
    readelf -d libssl_3.so    | grep -q 'NEEDED.*libcrypto_3\.so' || { echo "FATAL: $TLS_DIR libssl_3.so readelf NEEDED";    exit 1; }
    readelf -d libcrypto_3.so | grep -q 'SONAME.*libcrypto_3\.so' || { echo "FATAL: $TLS_DIR libcrypto_3.so readelf SONAME"; exit 1; }
    echo "CI 自产 $(basename "$TLS_DIR") libssl_3.so / libcrypto_3.so 校验通过"
)
done