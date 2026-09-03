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
import org.qtproject.qt.android.bindings.QtActivity;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

public class ShareActivity extends QtActivity {

    // 通知 Qt：pending_shares 目录有新的分享落盘，供 Qt 扫描并弹确认框
    private static native void notifyPendingShare();

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
    }

    @Override
    public void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        // 热启动：receiver 已把分享内容落盘，通知 Qt 扫描 pending_shares
        notifyPendingShare();
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
