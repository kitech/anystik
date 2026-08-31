// android_tls_bootstrap.h - TLS 库预加载器（仅 Android 生效）
#ifndef ANDROID_TLS_BOOTSTRAP_H
#define ANDROID_TLS_BOOTSTRAP_H

#include <QtCore/qglobal.h>

#ifdef Q_OS_ANDROID
void android_tls_bootstrap();
#endif

#endif