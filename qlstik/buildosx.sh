set -e fail
set -x

export PKG_CONFIG_PATH=/opt/vcpkg/installed/x64-osx/lib/pkgconfig
QMAKE=${QMAKE:-/opt/qt/5.15.2/clang_64/bin/qmake}
QMAKE_EXTRA=""
if [ x"$1" == x"asan" ]; then
    QMAKE_EXTRA="CONFIG+=asan"
fi
"$QMAKE" -r $QMAKE_EXTRA
make

# package
mkdir -p qlstik.app/Contents/Resources
QT_TRANS=$("$QMAKE" -query QT_INSTALL_TRANSLATIONS)
cp -f "$QT_TRANS/qt_zh_CN.qm" "$QT_TRANS/qt_zh_TW.qm" qlstik.app/Contents/Resources/ 2>/dev/null || true
tar zcf qlstik-qt5-osx-x64.tar.gz qlstik.app/
ls -lh qlstik-*.gz
ls -lh qlstik.app/Contents/MacOS/
