package io.fedlet.mobutil;

import android.content.Context;
import android.content.SharedPreferences;
import java.util.Set;
import org.unifiedpush.android.connector.PushService;
import org.unifiedpush.android.connector.data.PushEndpoint;
import org.unifiedpush.android.connector.data.PushMessage;
import org.unifiedpush.android.connector.FailedReason;

public class PushServiceImpl extends PushService {
    static { System.loadLibrary("anystik_arm64-v8a"); }

    private static String s_activeDistributor = "";

    public static void setActiveDistributor(String dist) {
        s_activeDistributor = dist;
    }

    public static boolean isInstanceRegistered(Context context, String instance) {
        SharedPreferences prefs = context.getSharedPreferences("unifiedpush.connector", Context.MODE_PRIVATE);
        Set<String> instances = prefs.getStringSet("unifiedpush.instances", null);
        boolean result = instances != null && instances.contains(instance);
        android.util.Log.d("PushServiceImpl", "isInstanceRegistered: instance=" + instance
                + " instances=" + instances + " result=" + result);
        return result;
    }

    public static boolean replaceDistributor(Context context, String distributor) {
        android.database.sqlite.SQLiteDatabase db = context.openOrCreateDatabase(
            "unifiedpush-connector", Context.MODE_PRIVATE, null);
        // 关闭外键级联，DELETE 不会删 token
        db.execSQL("PRAGMA foreign_keys = OFF");
        db.delete("distributors", null, null);
        android.content.ContentValues values = new android.content.ContentValues();
        values.put("distributor", distributor);
        values.put("fallback_from", (String) null);
        values.put("ack", 0);
        values.put("date_insertion", System.currentTimeMillis());
        db.insert("distributors", null, values);
        // 恢复外键
        db.execSQL("PRAGMA foreign_keys = ON");
        db.close();
        return true;
    }

    private static native void onNewEndpointNative(String endpoint, String instance);
    private static native void onMessageNative(byte[] message, String instance, String distributor);
    private static native void onRegistrationFailedNative(String reason, String instance);
    private static native void onUnregisteredNative(String instance);

    @Override
    public void onNewEndpoint(PushEndpoint endpoint, String instance) {
        onNewEndpointNative(endpoint.getUrl(), instance);
    }

    @Override
    public void onMessage(PushMessage message, String instance) {
        onMessageNative(message.getContent(), instance, s_activeDistributor);
    }

    @Override
    public void onRegistrationFailed(FailedReason reason, String instance) {
        onRegistrationFailedNative(reason.name(), instance);
    }

    @Override
    public void onUnregistered(String instance) {
        onUnregisteredNative(instance);
    }
}
