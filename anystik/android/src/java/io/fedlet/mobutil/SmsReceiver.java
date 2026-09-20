package io.fedlet.mobutil;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.provider.Telephony;
import android.telephony.SmsMessage;

public class SmsReceiver extends BroadcastReceiver {
    private static native void onSmsReceivedNative(String sender, String body);

    private static BroadcastReceiver s_receiver;

    @Override
    public void onReceive(Context ctx, Intent intent) {
        if (intent == null) return;
        if (!Telephony.Sms.Intents.SMS_RECEIVED_ACTION.equals(intent.getAction())) return;
        SmsMessage[] msgs = Telephony.Sms.Intents.getMessagesFromIntent(intent);
        if (msgs == null || msgs.length == 0) return;
        String sender = null;
        StringBuilder body = new StringBuilder();
        for (SmsMessage m : msgs) {
            if (m == null) continue;
            if (sender == null) sender = m.getOriginatingAddress();
            body.append(m.getMessageBody());
        }
        onSmsReceivedNative(sender != null ? sender : "", body.toString());
    }

    public static void registerReceiver(Context ctx) {
        if (s_receiver != null) return;
        s_receiver = new SmsReceiver();
        ctx.registerReceiver(s_receiver,
            new IntentFilter(Telephony.Sms.Intents.SMS_RECEIVED_ACTION));
    }

    public static void unregisterReceiver(Context ctx) {
        if (s_receiver != null) {
            try {
                ctx.unregisterReceiver(s_receiver);
            } catch (Exception ignored) {
                // ignore
            }
            s_receiver = null;
        }
    }
}