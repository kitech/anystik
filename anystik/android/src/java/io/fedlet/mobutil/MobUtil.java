package io.fedlet.mobutil;

import android.app.Activity;
import android.view.WindowInsets;
import android.graphics.Point;

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
}
