package io.fedlet.mobutil;

import android.content.ClipData;
import android.content.ClipDescription;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.util.Log;
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

    // 分享图片最大导入张数（超出部分忽略并提示）
    private static final int MAX_SHARE_IMAGES = 32;

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
        boolean imageMime = type != null && type.startsWith("image/");

        // 收集图片 URI：ClipData + EXTRA_STREAM(单/多) + getData 三来源合并去重，
        // 兼容只走 EXTRA_STREAM 的分享 app（如 Google Photos）。
        java.util.LinkedHashSet<Uri> imageUris = new java.util.LinkedHashSet<>();
        try {
            ClipData clipData = intent.getClipData();
            if (clipData != null) {
                for (int i = 0; i < clipData.getItemCount(); i++) {
                    Uri uri = clipData.getItemAt(i).getUri();
                    if (uri != null) imageUris.add(uri);
                }
            }
            // 单图：EXTRA_STREAM 是单个 Uri
            Object single = android.os.Build.VERSION.SDK_INT >= 33
                ? intent.getParcelableExtra(Intent.EXTRA_STREAM, Uri.class)
                : intent.getParcelableExtra(Intent.EXTRA_STREAM);
            if (single instanceof Uri) imageUris.add((Uri) single);
            // 多图：EXTRA_STREAM 是 ArrayList<Uri>
            @SuppressWarnings("deprecation")
            java.util.ArrayList<Uri> multi = android.os.Build.VERSION.SDK_INT >= 33
                ? intent.getParcelableArrayListExtra(Intent.EXTRA_STREAM, Uri.class)
                : intent.getParcelableArrayListExtra(Intent.EXTRA_STREAM);
            if (multi != null) {
                for (Uri u : multi) {
                    if (u != null) imageUris.add(u);
                }
            }
            if (intent.getData() != null) imageUris.add(intent.getData());
        } catch (Exception e) {
            // ignore malformed intent extras
        }
        for (Uri u : imageUris) uris.put(u.toString());

        // 图片：后台线程逐张读字节并逐个入队批量导入，
        // 避免在主线程阻塞 I/O（ANR）。主线程此处立即返回。
        if (imageMime && !imageUris.isEmpty()) {
            final Uri[] arr = imageUris.toArray(new Uri[0]);
            final int importCount = Math.min(arr.length, MAX_SHARE_IMAGES);
            final boolean exceeded = arr.length > MAX_SHARE_IMAGES;
            new Thread(new Runnable() {
                @Override
                public void run() {
                    int imported = 0;
                    for (int i = 0; i < importCount; i++) {
                        byte[] b = readUriBytes(ShareActivity.this, arr[i]);
                        if (b != null && b.length > 0) {
                            final byte[] fb = b;
                            runOnUiThread(new Runnable() {
                                @Override
                                public void run() {
                                    onShareImageReceived(fb, type);
                                }
                            });
                            imported++;
                        }
                    }
                    if (imported == 0) {
                        runOnUiThread(new Runnable() {
                            @Override
                            public void run() {
                                onShareIntentReceived(action, type, text,
                                    uris.toString());
                            }
                        });
                        return;
                    }
                    if (exceeded) {
                        runOnUiThread(new Runnable() {
                            @Override
                            public void run() {
                                android.widget.Toast.makeText(ShareActivity.this,
                                    "图片超过 " + MAX_SHARE_IMAGES
                                        + " 张，已导入前 " + MAX_SHARE_IMAGES + " 张",
                                    android.widget.Toast.LENGTH_SHORT).show();
                            }
                        });
                    }
                }
            }, "share-read").start();
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

            for (int i = 0; i < cd.getItemCount(); i++) {
                ClipData.Item it = cd.getItemAt(i);
                if (it == null) continue;
                Uri uri = it.getUri();
                if (uri != null) {
                    try {
                        ctx.getContentResolver().takePersistableUriPermission(
                            uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
                    } catch (Exception ignored) { }
                    byte[] b = readUriBytes(ctx, uri);
                    if (b != null && b.length > 0) return b;
                } else {
                    CharSequence t = it.getText();
                    String tag = "AnystikClipboard";
                    if (t != null) {
                        String s = t.toString();
                        Log.w(tag, "text-type clipboard item, prefix="
                            + (s.length() > 80 ? s.substring(0, 80) : s));
                    } else {
                        Log.w(tag, "clipboard item has no uri and no text");
                    }
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

    // 拷贝到剪贴板：复制贴纸为 image/* content URI（经 FileProvider），
    // 使支持图片粘贴的应用及本应用 readClipboardImageBytes 均可按 Uri 读字节
    public static boolean copyImageToClipboard(Context ctx, String path) {
        if (ctx == null || path == null || path.isEmpty()) return false;
        try {
            File file = new File(path);
            if (!file.isFile()) return false;
            Uri uri = FileProvider.getUriForFile(ctx,
                ctx.getPackageName() + ".qtprovider", file);
            Intent intent = new Intent();
            intent.setClipData(ClipData.newUri(ctx.getContentResolver(), "sticker", uri));
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
            ClipData clip = intent.getClipData();
            if (clip == null) return false;
            ClipboardManager cm =
                (ClipboardManager) ctx.getSystemService(Context.CLIPBOARD_SERVICE);
            if (cm == null) return false;
            cm.setPrimaryClip(clip);
            return true;
        } catch (Exception e) {
            return false;
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

    // 打开目录：成功拉起文件管理器返回 true，否则 false。
    // 先尝试 FileProvider content URI，失败回退 file://；整段捕获异常避免崩溃。
    public static boolean openDir(Context ctx, String path) {
        if (ctx == null || path == null || path.isEmpty()) return false;
        File dir = new File(path);
        if (!dir.isDirectory()) return false;
        try {
            Intent intent = new Intent(Intent.ACTION_VIEW);
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            try {
                Uri contentUri = FileProvider.getUriForFile(ctx,
                    ctx.getPackageName() + ".qtprovider", dir);
                intent.setData(contentUri);
                intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            } catch (Exception e1) {
                intent.setData(Uri.fromFile(dir));
            }
            ctx.startActivity(intent);
            return true;
        } catch (Exception e) {
            return false;
        }
    }
}
