package io.fedlet.mobutil;

import android.content.ClipData;
import android.content.ClipDescription;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import org.json.JSONArray;
import org.qtproject.qt.android.bindings.QtActivity;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;

public class ShareActivity extends QtActivity {
    private static native void onShareIntentReceived(
        String action, String mimeType, String text, String urisJson);
    private static native void onShareImageReceived(byte[] imageBytes, String mimeType);

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        handleIntent(getIntent());
    }

    @Override
    public void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        handleIntent(intent);
    }

    private void handleIntent(Intent intent) {
        String action = intent.getAction();
        if (action == null) return;

        if (!Intent.ACTION_SEND.equals(action)
                && !Intent.ACTION_SEND_MULTIPLE.equals(action)) {
            return;
        }

        String type = intent.getType();
        String text = intent.getStringExtra(Intent.EXTRA_TEXT);
        JSONArray uris = new JSONArray();
        byte[] imageBytes = null;
        boolean imageMime = type != null && type.startsWith("image/");

        ClipData clipData = intent.getClipData();
        try {
            if (clipData != null) {
                for (int i = 0; i < clipData.getItemCount(); i++) {
                    Uri uri = clipData.getItemAt(i).getUri();
                    if (uri != null) {
                        uris.put(uri.toString());
                        if (imageMime && imageBytes == null) {
                            imageBytes = readUriBytes(this, uri);
                        }
                    }
                }
            }

            if (imageBytes == null && imageMime) {
                imageBytes = readUriBytes(this, intent.getData());
            }
        } catch (Exception e) {
            // ignore share reading failures, fall back to intent payload below
        }
        if (imageMime && imageBytes != null) {
            onShareImageReceived(imageBytes, type);
            return;
        }

        onShareIntentReceived(action, type, text, uris.toString());
    }

    public static byte[] readClipboardImageBytes(Context ctx) {
        if (ctx == null) return null;
        try {
            ClipboardManager cm =
                (ClipboardManager) ctx.getSystemService(Context.CLIPBOARD_SERVICE);
            if (cm == null) return null;
            ClipData cd = cm.getPrimaryClip();
            if (cd == null || cd.getItemCount() == 0) return null;

            ClipDescription desc = cd.getDescription();
            if (desc == null || !desc.hasMimeType("image/*")) return null;

            for (int i = 0; i < cd.getItemCount(); i++) {
                ClipData.Item it = cd.getItemAt(i);
                if (it == null) continue;
                Uri uri = it.getUri();
                if (uri != null) {
                    byte[] b = readUriBytes(ctx, uri);
                    if (b != null && b.length > 0) return b;
                }
            }
        } catch (Exception e) {
            return null;
        }
        return null;
    }

    private static byte[] readUriBytes(Context ctx, Uri uri) {
        if (ctx == null || uri == null) return null;
        try (InputStream in = ctx.getContentResolver().openInputStream(uri)) {
            if (in == null) return null;
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buf = new byte[16384];
            int n;
            while ((n = in.read(buf)) != -1) {
                out.write(buf, 0, n);
            }
            return out.toByteArray();
        } catch (Exception e) {
            return null;
        }
    }
}
