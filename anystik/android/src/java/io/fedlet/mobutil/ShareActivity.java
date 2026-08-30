package io.fedlet.mobutil;

import android.content.ClipData;
import android.content.ClipDescription;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import androidx.core.content.FileProvider;
import org.json.JSONArray;
import org.qtproject.qt.android.bindings.QtActivity;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

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

    // 出向分享：经 FileProvider 提供 content:// URI 拉起系统分享面板
    public static void shareLocalImage(Context ctx, String path) {
        if (ctx == null || path == null || path.isEmpty()) return;
        try {
            File src = new File(path);
            if (!src.isFile()) return;

            File file;
            String filesDir = ctx.getFilesDir().getAbsolutePath();
            if (!path.startsWith(filesDir)) {
                // 外部文件（如目录导入的原路径）：先拷入私有存储再分享
                File shares = new File(ctx.getFilesDir(), "shares");
                if (!shares.exists() && !shares.mkdirs()) return;
                file = new File(shares, src.getName());
                try (InputStream in = new FileInputStream(src);
                        OutputStream out = new FileOutputStream(file)) {
                    byte[] buf = new byte[16384];
                    int n;
                    while ((n = in.read(buf)) != -1) out.write(buf, 0, n);
                }
            } else {
                file = src;
            }

            Uri uri = FileProvider.getUriForFile(ctx,
                ctx.getPackageName() + ".qtprovider", file);
            Intent send = new Intent(Intent.ACTION_SEND);
            send.setType(mimeFor(file));
            send.putExtra(Intent.EXTRA_STREAM, uri);
            send.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            ctx.startActivity(Intent.createChooser(send, "分享贴纸"));
        } catch (Exception e) {
            // ignore share failures
        }
    }

    private static String mimeFor(File f) {
        String n = f.getName().toLowerCase();
        if (n.endsWith(".png"))  return "image/png";
        if (n.endsWith(".jpg") || n.endsWith(".jpeg")) return "image/jpeg";
        if (n.endsWith(".gif"))  return "image/gif";
        if (n.endsWith(".webp")) return "image/webp";
        if (n.endsWith(".bmp"))  return "image/bmp";
        return "image/*";
    }
}
