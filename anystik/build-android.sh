#!/bin/bash
set -e
export JAVA_HOME=/opt/jdk-17.0.13+11
export ANDROID_SDK_ROOT=/opt/android-sdk
export ANDROID_NDK_ROOT=/opt/android-ndk-r26b

QT_ANDROID=/opt/qt/6.7.3/android_arm64_v8a
QSK_ANDROID=/opt/qt/qskinny-arm64
QT_HOST=/opt/qt/6.7.3/gcc_64

BUILD_DIR=build-android
# rm -rf "$BUILD_DIR"  # 保留 cmake 缓存以支持增量编译

# 1. cmake configure via qt-cmake
"$QT_ANDROID/bin/qt-cmake" -S . -B "$BUILD_DIR" \
    -DANDROID_SDK_ROOT="$ANDROID_SDK_ROOT" \
    -DANDROID_NDK_ROOT="$ANDROID_NDK_ROOT" \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-24 \
    -DCMAKE_PREFIX_PATH="$QSK_ANDROID" \
    -DQSkinny_DIR="$QSK_ANDROID/lib/cmake/QSkinny" \
    -DQT_HOST_PATH="$QT_HOST" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo

# 2. build native .so only (not apk target)
#cmake --build "$BUILD_DIR" -j$(nproc) --target anystik
cmake --build "$BUILD_DIR" -j1 --target anystik

# 3. prepare android-build dir (copy .so + Qt deps, no Gradle)
APK_DIR="$BUILD_DIR/android-build"
# rm -rf "$APK_DIR"
LIB_DIR="$APK_DIR/libs/arm64-v8a"
mkdir -p "$LIB_DIR"
cp "$BUILD_DIR/libanystik_arm64-v8a.so" "$LIB_DIR/"

# 平铺复制 qskinny 皮肤插件到 JNI lib 根目录（Android 无子目录）
cp "$QSK_ANDROID/lib/qskinny/plugins/skins/"*.so "$LIB_DIR/"
cp "$QSK_ANDROID/lib/qskinny/plugins/platforminputcontexts/"*.so "$LIB_DIR/"

# 复制 extras 共享库到 APK
cp "$BUILD_DIR/libgoso.so" "$LIB_DIR/"
cp "$BUILD_DIR/libcso.so" "$LIB_DIR/"
cp vendor/lib/${ANDROID_ABI:-arm64-v8a}/libsqlite3.so "$LIB_DIR/"
cp vendor/lib/${ANDROID_ABI:-arm64-v8a}/libcurl.so "$LIB_DIR/"
cp vendor/lib/${ANDROID_ABI:-arm64-v8a}/libssl.so "$LIB_DIR/"
cp vendor/lib/${ANDROID_ABI:-arm64-v8a}/libcrypto.so "$LIB_DIR/"
# Qt 6 Android TLS 后端 dlopen 优先探测带后缀的 libssl_3/libcrypto_3；
# 裸名 libssl.so/libcrypto.so 保留给 libcurl 的 NEEDED，二者并存不冲突。
cp vendor/lib/${ANDROID_ABI:-arm64-v8a}/libssl_3.so "$LIB_DIR/"
cp vendor/lib/${ANDROID_ABI:-arm64-v8a}/libcrypto_3.so "$LIB_DIR/"

"$QT_HOST/bin/androiddeployqt" \
    --input "$BUILD_DIR/android-anystik-deployment-settings.json" \
    --output "$APK_DIR" \
    --deployment bundled \
    --builddir "$BUILD_DIR" \
    --aux-mode

# 复制自定义 Java 源码到 Android 构建目录
mkdir -p "$APK_DIR/src/main/java"
cp -R android/src/java/* "$APK_DIR/src/main/java/"
# 复制自定义 Android res（覆盖 qt 模板默认的 qtprovider_paths.xml，加入 Pictures 目录授权）
cp -R android/res/* "$APK_DIR/res/"
# 复制应用图标到 Android res
mkdir -p "$APK_DIR/res/drawable"
cp app_icon.png "$APK_DIR/res/drawable/ic_launcher.png"

# 移除 renderscript.srcDirs 配置（build-tools 34+ 不再支持，且会干扰自定义 Java 源码）
sed -i '/renderscript\.srcDirs/d' "$APK_DIR/build.gradle"

# ── 注入 UnifiedPush connector（Maven 依赖，见 vendor/vendorinfos.md）──
# build.gradle 是 androiddeployqt 生成的产物，aux-mode 不覆写已存在的文件，
# 本地残留手工修改不会自动出现在全新生成的 CI 工程中，故每次注入并防重
if ! grep -q "org.unifiedpush.android:connector" "$APK_DIR/build.gradle"; then
    cat > /tmp/up_conf.txt <<'EOF'

configurations.configureEach {
    def tink = "com.google.crypto.tink:tink-android:1.20.0"
    resolutionStrategy {
        force(tink)
        dependencySubstitution {
           substitute module('com.google.crypto.tink:tink') using module(tink)
        }
        force 'com.google.errorprone:error_prone_annotations:2.20.0'
        // 锁定与本地一致的 androidx 版本链（CI 直连 Google Maven 会拉到
        // core 1.13.1 / experimental 1.4.0，强制要求 compileSdk ≥34）
        force 'androidx.core:core:1.10.1'
        force 'androidx.annotation:annotation-experimental:1.3.0'
    }
}
EOF
    sed -i "/^apply plugin: 'com.android.application'/r /tmp/up_conf.txt" "$APK_DIR/build.gradle"
    echo "    implementation('org.unifiedpush.android:connector:3.3.3')" > /tmp/up_dep.txt
    sed -i '/^dependencies {/r /tmp/up_dep.txt' "$APK_DIR/build.gradle"
fi

# Qt 模板生成 useLegacyPackaging true（.so DEFLATE 压缩，安装时解压到磁盘）。
# 注意：Qt 6.9 之前不支持直接从 APK 加载未压缩(STORED)的 .so —— 若改成 false，
# QtLoader 的 System.load() 无法 dlopen 平台插件，JNI_OnLoad 不执行，
# 首个 native 调用(QtDisplayManager.handleOrientationChanged)即 UnsatisfiedLinkError 闪退。
# 必须保持 true；压缩所需堆内存由下方 jvmargs/GRADLE_OPTS 提升解决。
true

# 覆盖 manifest（包含所有自定义：activity、intent-filter、权限、service）
cp android/AndroidManifest.xml "$APK_DIR/AndroidManifest.xml"

# 4. append Qt-specific properties (missing from --aux-mode)
cat >> "$APK_DIR/gradle.properties" <<PROPS
# 模板默认 -Xmx386m 过小，packageDebug 打包 Qt+vendor 大 .so 时 OOM；
# java.util.Properties 后值覆盖前值，此行覆盖模板头部的旧 jvmargs
org.gradle.jvmargs=-Xmx4096m -XX:MaxMetaspaceSize=1024m -Dfile.encoding=UTF-8
androidBuildToolsVersion=34.0.0
androidCompileSdkVersion=android-33
androidNdkVersion=26.1.10909125
androidPackageName=io.fedlet.anystik
buildDir=build
qtAndroidDir=${QT_ANDROID}/src/android/java
qtMinSdkVersion=23
qtTargetAbiList=arm64-v8a
qtTargetSdkVersion=34
PROPS

# 5. patch Gradle heap (low-memory device); DEFLATE 压缩 111MB lib 需要足够堆
sed -i 's/-Xmx[0-9]*m/-Xmx2048m/' "$APK_DIR/gradle.properties"

# 6. build debug APK (auto-signed with Android debug key)
GRADLE_OPTS="-Xmx2048m" "$APK_DIR/gradlew" --no-daemon --max-workers=2 -p "$APK_DIR" assembleDebug

# 6. verify output
APK=$(find "$APK_DIR/build/outputs" -name "*.apk" 2>/dev/null | head -1)
if [ -n "$APK" ]; then
    echo "=== APK built: $APK ==="
    ls -lh "$APK"
fi

find build-android -name *anystik*.so | xargs ls -lh
objdump -x build-android/android-build/libs/arm64-v8a/libanystik_arm64-v8a.so|grep NEEDED|grep -v libQt|grep -v libc|grep -v libm|grep -v libc 

### todo kill java of this process forked
