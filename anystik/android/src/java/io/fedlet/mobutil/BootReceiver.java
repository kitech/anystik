package io.fedlet.mobutil;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;

public class BootReceiver extends BroadcastReceiver {
    private static final String PREFS_NAME = "anystik_prefs";
    private static final String KEY_PHONE_ANSWER = "phoneAnswer";

    @Override
    public void onReceive(Context ctx, Intent intent) {
        if (intent == null) return;
        if (Intent.ACTION_BOOT_COMPLETED.equals(intent.getAction())) {
            SharedPreferences prefs = ctx.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
            int mode = prefs.getInt(KEY_PHONE_ANSWER, 0);
            if (mode != 0) {
                PhoneStateReceiver.registerReceiver(ctx);
            }
        }
    }
}
