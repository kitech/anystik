使用qskinny和qt6.7编写hello world，主要考虑界面的现代化，与android平台

不使用qml，界面全部使用qskinny布局生成。

不移植现有代码，只写helloworld，跑通android-arm64版本打包安装运行, 
当前linux平台版本编译运行，以及macos intel x64平台的打包运行。

qskinny x64安装目录，/opt/qt/qskinny/
qt6.7 x64目录，/opt/qt/6.7.3/gcc_64/

qskiny arm64安装目录，/opt/qt/qskinny-arm64/
qt6.7 arm64目录，/opt/qt/6.7.3/android_arm64_v8a/

ndk 目录，/opt/android-ndk-r26b
tools 目录，/opt/android-sdk/
jdk 目录，/opt/jdk-17.0.13+11/


https://github.com/adoptium/temurin17-binaries/releases/download/jdk-17.0.13+11/OpenJDK17U-jdk_x64_linux_hotspot_17.0.13_11.tar.gz 

https://github.com/adoptium/temurin17-binaries/releases/download/jdk-17.0.13+11/OpenJDK17U-jdk_aarch64_linux_hotspot_17.0.13_11.tar.gz

https://mirrors.cloud.tencent.com/AndroidSDK/android-ndk-r26b-linux.zip

### ndk r26 ok


API 21 - 34 <=> Android 5 - 14

同qt6.7.3，ok
	

### ndk r18

API 23 - 34 <=> Android 6 - 14

同qt6.7.3联用，ndk r18 编译通过但运行报错，dlopen failed: cannot locate symbol "_ZTVNSt6__ndk13pmr25monotonic_buffer_resourceE" referenced by "libQt6Core_arm64-v8a.so"


### 可直接使用的qtquick组件

PinchArea / TapHandler


### qskinny 编译

/mnt/sda5/aur/qskinny-master/build-arm64.sh

8bc872f9b67268f17c9ab0e5b21a581142a8c283


### 调试

term1: ssh -p 8022 me@192.168.1.94 "su -c logcat 2>&1" > droid.log

term2: echo > droid.log && tail -f droid.log |grep -a -i tox

