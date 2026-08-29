package io.fedlet.mobutil;

import android.Manifest;
import android.app.Activity;
import android.content.pm.PackageManager;
import android.os.Build;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

public class PermissionHelper {
    private static final int REQ_NOTIFICATION = 9001;
    private static final int REQ_MEDIA = 9002;
    private static final int REQ_PHONE_CALL = 9003;

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
        if (Build.VERSION.SDK_INT < 33)
            return true;
        return ContextCompat.checkSelfPermission(activity,
            Manifest.permission.READ_MEDIA_IMAGES) == PackageManager.PERMISSION_GRANTED;
    }

    public static void requestMediaPermission(Activity activity) {
        if (Build.VERSION.SDK_INT < 33)
            return;
        if (hasMediaPermission(activity))
            return;
        ActivityCompat.requestPermissions(activity,
            new String[]{Manifest.permission.READ_MEDIA_IMAGES}, REQ_MEDIA);
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
