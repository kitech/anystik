#ifndef ANDROID_UTILS_H
#define ANDROID_UTILS_H

#include <QString>

void showAndroidToast(const QString& message);
void installLogHandler();

// 仅 Android：返回系统相册 Pictures/anystik 的绝对路径；非 Android 返回空。
QString androidPicturesStickerBaseDir();

// Android 存储访问授权判定/请求（分版本）：
//   API>=30 : MANAGE_EXTERNAL_STORAGE（特殊权限，引导设置页）
//   API 23-29: WRITE_EXTERNAL_STORAGE（运行时权限）
// 非 Android 平台恒返回 true（无需授权）。
bool androidStorageAccessGranted();
void requestAndroidStorageAccess();

#endif
