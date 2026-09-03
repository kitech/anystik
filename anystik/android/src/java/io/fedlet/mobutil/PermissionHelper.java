package io.fedlet.mobutil;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.Settings;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

public class PermissionHelper {
    private static final int REQ_NOTIFICATION = 9001;
    private static final int REQ_MEDIA = 9002;
    private static final int REQ_PHONE_CALL = 9003;
    private static final int REQ_DOCUMENT_TREE = 9004;

    public static boolean hasNotificationPermission(Activity activity) {
        if (Build.VERSION.SDK_INT < 33)
            return true;
        return ContextCompat.checkSelfPermission(activity,
            Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED;
    }

    public static void requestNotificationPermission(Activity activity) {
        if (Build.VERSION.SDK_INT < 33)
            return;
        if (hasNotificationPermission(activity))
            return;
        ActivityCompat.requestPermissions(activity,
            new String[]{Manifest.permission.POST_NOTIFICATIONS}, REQ_NOTIFICATION);
    }

    public static boolean hasMediaPermission(Activity activity) {
        if (Build.VERSION.SDK_INT >= 33)
            return ContextCompat.checkSelfPermission(activity,
                Manifest.permission.READ_MEDIA_IMAGES) == PackageManager.PERMISSION_GRANTED;
        if (Build.VERSION.SDK_INT >= 29)
            return ContextCompat.checkSelfPermission(activity,
                Manifest.permission.READ_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
        return true;   // API <= 28：WRITE_EXTERNAL_STORAGE 已含读权限，无需单独申请
    }

    public static void requestMediaPermission(Activity activity) {
        if (hasMediaPermission(activity)) return;
        if (Build.VERSION.SDK_INT >= 33) {
            ActivityCompat.requestPermissions(activity,
                new String[]{Manifest.permission.READ_MEDIA_IMAGES}, REQ_MEDIA);
        } else if (Build.VERSION.SDK_INT >= 29) {
            ActivityCompat.requestPermissions(activity,
                new String[]{Manifest.permission.READ_EXTERNAL_STORAGE}, REQ_MEDIA);
        }
    }

    // ── 所有文件访问（Android 11+ 任意目录）──
    // MANAGE_EXTERNAL_STORAGE 属「特殊权限」，无法用 requestPermissions 弹窗申请，
    // 只能引导用户到系统「所有文件访问」设置页手动开启。开启后 Java 拿真实绝对路径，
    // Qt 的 QDir/importDirectory 即可直接遍历 /sdcard 下任意目录。
    public static boolean hasManageExternalStorage(Activity activity) {
        if (Build.VERSION.SDK_INT < 30) return true;
        return Environment.isExternalStorageManager();
    }

    public static void requestManageExternalStorage(Activity activity) {
        if (Build.VERSION.SDK_INT < 30) return;
        if (hasManageExternalStorage(activity)) return;
        try {
            // 优先跳本 app 专属页（API 30+）
            Intent i = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                Uri.parse("package:" + activity.getPackageName()));
            activity.startActivity(i);
        } catch (Exception e) {
            // 个别厂商无 app 级入口，退到全局「所有文件访问」列表
            try {
                activity.startActivity(new Intent(
                    Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
            } catch (Exception ignored) { }
        }
    }

    // ── SAF 目录/图库选择启动（系统弹窗授权，无需 MANAGE 权限即可用）──
    // 注意：这三个方法只是启动系统选择器，结果在 onActivityResult 里拿，
    // 由调用方持有 activity 并自行处理结果回调。本类不接管结果。
    public static void launchDirectoryPicker(Activity activity) {
        try {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION
                | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
            activity.startActivityForResult(i, REQ_DOCUMENT_TREE);
        } catch (Exception ignored) { }
    }

    public static void launchImagePicker(Activity activity, boolean multiple) {
        try {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            i.setType("image/*");
            i.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, multiple);
            i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
            activity.startActivityForResult(i, REQ_MEDIA);
        } catch (Exception ignored) { }
    }

    // 持久化授权 content:// tree/uri，后续进程无需再询权即可读取
    public static void takePersistablePermission(Activity activity, Uri uri) {
        if (uri == null || activity == null) return;
        try {
            activity.getContentResolver().takePersistableUriPermission(uri,
                Intent.FLAG_GRANT_READ_URI_PERMISSION
                    | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        } catch (Exception ignored) { }
    }

    public static boolean hasPhoneCallPermission(Activity activity) {
        if (Build.VERSION.SDK_INT < 29)
            return true;
        return ContextCompat.checkSelfPermission(activity,
            Manifest.permission.ANSWER_PHONE_CALLS) == PackageManager.PERMISSION_GRANTED;
    }

    public static boolean hasReadPhoneStatePermission(Activity activity) {
        return ContextCompat.checkSelfPermission(activity,
            Manifest.permission.READ_PHONE_STATE) == PackageManager.PERMISSION_GRANTED;
    }

    public static void requestPhoneCallPermission(Activity activity) {
        java.util.List<String> perms = new java.util.ArrayList<>();
        if (Build.VERSION.SDK_INT >= 29 && !hasPhoneCallPermission(activity)) {
            perms.add(Manifest.permission.ANSWER_PHONE_CALLS);
        }
        if (!hasReadPhoneStatePermission(activity)) {
            perms.add(Manifest.permission.READ_PHONE_STATE);
        }
        if (!perms.isEmpty()) {
            ActivityCompat.requestPermissions(activity,
                perms.toArray(new String[0]), REQ_PHONE_CALL);
        }
    }
}
