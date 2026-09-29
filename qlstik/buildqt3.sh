set -x

# for qt3
if [ x"$QTDIR" == x"" ]; then
    QTDIR=/opt/qt338sh
    export QTDIR
fi
mkdir -p build-qt3 && cd build-qt3
QMAKE_EXTRA=""
if [ x"$1" == x"asan" ]; then
    QMAKE_EXTRA="CONFIG+=asan"
fi
/opt/qt338sh/bin/qmake -makefile $QMAKE_EXTRA ../qlstik.pro
if [ x"$1" == x"c" ]; then
	make clean
fi
make
ret=$?
if [ x"$ret" == x"0" ] && [ -f "qlstik" ]; then
	cp -v qlstik q3stik
fi
ls -lh q*stik
