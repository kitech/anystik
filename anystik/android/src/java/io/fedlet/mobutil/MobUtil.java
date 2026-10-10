package io.fedlet.mobutil;

import android.app.Activity;
import android.view.WindowInsets;
import android.graphics.Point;
import android.content.Context;
import android.os.BatteryManager;

public class MobUtil {

    public static int statusBarHeight(Activity activity) {
        WindowInsets insets = activity.getWindow().getDecorView()
            .getRootWindowInsets();
        if (insets != null) {
            return insets.getInsets(WindowInsets.Type.statusBars()).top;
        }
        return 0;
    }

    public static int navBarHeight(Activity activity) {
        WindowInsets insets = activity.getWindow().getDecorView()
            .getRootWindowInsets();
        if (insets != null) {
            return insets.getInsets(WindowInsets.Type.navigationBars()).bottom;
        }
        return 0;
    }

    public static int[] availableScreenSize(Activity activity) {
        Point size = new Point();
        activity.getWindowManager().getDefaultDisplay()
            .getSize(size);
        return new int[]{size.x, size.y};
    }

    public static void toast(Activity activity, String message) {
        android.widget.Toast.makeText(activity, message,
            android.widget.Toast.LENGTH_SHORT).show();
    }

    // 是否处于充电状态（minSdk=23，BatteryManager.isCharging 自 API 23 可用）
    public static boolean isCharging(Context context) {
        try {
            BatteryManager bm = (BatteryManager)
                context.getSystemService(Context.BATTERY_SERVICE);
            return bm != null && bm.isCharging();
        } catch (Exception e) {
            return false;
        }
    }

    // 启发式 Root 判定：常见 su 路径 / PATH 中 which su / test-keys 构建。
    // App 沙箱内无法 100% 判定，仅作状态提示。
    public static boolean isRooted() {
        String[] suPaths = {
            "/system/bin/su", "/system/xbin/su", "/system/sbin/su",
            "/sbin/su", "/su/bin/su", "/data/local/bin/su",
            "/data/local/xbin/su", "/system/sd/xbin/su",
            "/system/bin/failsafe/su", "/system/su", "/vendor/bin/su"
        };
        for (String p : suPaths) {
            try {
                if (new java.io.File(p).exists()) return true;
            } catch (Exception ignored) {
            }
        }
        try {
            Process proc = Runtime.getRuntime().exec(new String[]{"which", "su"});
            if (proc.waitFor() == 0) {
                java.io.BufferedReader r = new java.io.BufferedReader(
                    new java.io.InputStreamReader(proc.getInputStream()));
                String line = r.readLine();
                if (line != null && line.contains("su")) return true;
            }
        } catch (Exception ignored) {
        }
        try {
            String tags = android.os.Build.TAGS;
            if (tags != null && tags.contains("test-keys")) return true;
        } catch (Exception ignored) {
        }
        return false;
    }
}
