package io.fedlet.mobutil;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.os.Build;
import android.telecom.TelecomManager;
import android.telephony.TelephonyManager;

public class PhoneStateReceiver extends BroadcastReceiver {
    private static final String CHANNEL_ID = "anystik_call";
    private static final String PREFS_NAME = "anystik_prefs";
    private static final String KEY_PHONE_ANSWER = "phoneAnswer";
    private static final int NOTIF_ID = 2001;

    private static native void onCallStateChangedNative(String state, String phoneNumber);

    @Override
    public void onReceive(Context ctx, Intent intent) {
        if (intent == null) return;
        String action = intent.getAction();
        if (action == null) return;

        if (TelephonyManager.ACTION_PHONE_STATE_CHANGED.equals(action)) {
            handlePhoneState(ctx, intent);
        } else if ("io.fedlet.anystik.ACCEPT_CALL".equals(action)) {
            acceptCall(ctx);
            // Dismiss the notification
            NotificationManager nm = ctx.getSystemService(NotificationManager.class);
            if (nm != null) {
                nm.cancel(NOTIF_ID);
            }
        }
    }

    private void handlePhoneState(Context ctx, Intent intent) {
        String state = intent.getStringExtra(TelephonyManager.EXTRA_STATE);
        if (state == null) return;

        if (TelephonyManager.EXTRA_STATE_RINGING.equals(state)) {
            String number = intent.getStringExtra(TelephonyManager.EXTRA_INCOMING_NUMBER);
            SharedPreferences prefs = ctx.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
            int mode = prefs.getInt(KEY_PHONE_ANSWER, 0);

            if (mode == 2) {
                // Auto: accept immediately
                acceptCall(ctx);
            } else if (mode == 1) {
                // Manual: show notification with accept button
                showCallNotification(ctx, number);
            }
            // mode == 0: Disabled, do nothing
        }
    }

    public static void acceptCall(Context ctx) {
        try {
            TelecomManager tm = (TelecomManager) ctx.getSystemService(Context.TELECOM_SERVICE);
            if (tm != null) {
                tm.acceptRingingCall();
            }
        } catch (SecurityException e) {
            // Permission not granted, ignore
        } catch (Exception e) {
            // acceptRingingCall deprecated but still works on most devices
        }
    }

    private void showCallNotification(Context ctx, String number) {
        createNotificationChannel(ctx);

        // Intent to accept the call
        Intent acceptIntent = new Intent(ctx, PhoneStateReceiver.class);
        acceptIntent.setAction("io.fedlet.anystik.ACCEPT_CALL");
        int flags = PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE;
        PendingIntent acceptPI = PendingIntent.getBroadcast(ctx, 0, acceptIntent, flags);

        // Intent to launch app
        Intent launchIntent = ctx.getPackageManager().getLaunchIntentForPackage(ctx.getPackageName());
        if (launchIntent != null) {
            launchIntent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT);
        }
        PendingIntent launchPI = PendingIntent.getActivity(ctx, 0, launchIntent, flags);

        String displayNumber = (number != null && !number.isEmpty()) ? number : "未知号码";

        Notification.Builder builder;
        if (Build.VERSION.SDK_INT >= 26) {
            builder = new Notification.Builder(ctx, CHANNEL_ID);
        } else {
            builder = new Notification.Builder(ctx);
        }

        Notification notif = builder
            .setContentTitle("来电")
            .setContentText(displayNumber)
            .setSmallIcon(android.R.drawable.ic_menu_call)
            .setPriority(Notification.PRIORITY_HIGH)
            .setContentIntent(launchPI)
            .setOngoing(true)
            .addAction(new Notification.Action.Builder(
                null, "接听", acceptPI).build())
            .build();

        NotificationManager nm = ctx.getSystemService(NotificationManager.class);
        if (nm != null) {
            nm.notify(NOTIF_ID, notif);
        }
    }

    private void createNotificationChannel(Context ctx) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel channel = new NotificationChannel(
                CHANNEL_ID, "来电通知",
                NotificationManager.IMPORTANCE_HIGH);
            channel.setDescription("来电接听通知");
            channel.enableVibration(true);
            NotificationManager nm = ctx.getSystemService(NotificationManager.class);
            if (nm != null) {
                nm.createNotificationChannel(channel);
            }
        }
    }

    private static BroadcastReceiver s_receiver;

    public static void setPhoneAnswerMode(Context ctx, int mode) {
        SharedPreferences prefs = ctx.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
        prefs.edit().putInt(KEY_PHONE_ANSWER, mode).apply();
    }

    public static void registerReceiver(Context ctx) {
        if (s_receiver != null) return;
        s_receiver = new PhoneStateReceiver();
        IntentFilter filter = new IntentFilter(TelephonyManager.ACTION_PHONE_STATE_CHANGED);
        ctx.registerReceiver(s_receiver, filter);
    }

    public static void unregisterReceiver(Context ctx) {
        if (s_receiver != null) {
            try {
                ctx.unregisterReceiver(s_receiver);
            } catch (Exception e) {
                // ignore
            }
            s_receiver = null;
        }
    }
}
