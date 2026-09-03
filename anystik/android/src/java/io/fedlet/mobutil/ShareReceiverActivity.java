package io.fedlet.mobutil;

import android.app.Activity;
import android.content.ClipData;
import android.content.Intent;
import android.net.Uri;
import android.os.Binder;
import android.os.Bundle;
import android.util.Log;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.LinkedHashSet;

// 独立分享接收 activity（纯 Java，不继承 QtActivity）。
// 目的：外部分享（SEND / SEND_MULTIPLE）一律先进这里，读字节落盘后再转交主
// QtActivity。这样外部 share 永不触发主 QtActivity 的 onCreate 二次创建，
// 从而从根上规避 QtActivityBase.onCreate 里的 restartApplication() 导致的黑屏
// 卡死（START MAIN/LAUNCHER + Runtime.exit(0)）。本 activity 本体是普通
// Activity，自带 finish，最坏也只是自身结束，绝不会拖死 Qt 主实例。
public class ShareReceiverActivity extends Activity {

    private static final String TAG = "ShareReceiver";
    private static final String PENDING_DIR = "pending_shares";
    private static final String META_FILE = "pending_meta.json";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        Intent intent = getIntent();
        final String action = intent.getAction();
        if (!Intent.ACTION_SEND.equals(action)
                && !Intent.ACTION_SEND_MULTIPLE.equals(action)) {
            finish();
            return;
        }

        final String mime = intent.getType();
        final String text = intent.getStringExtra(Intent.EXTRA_TEXT);
        final LinkedHashSet<Uri> uris = collectUris(intent);
        final String source = resolveSource(intent);

        // 后台线程读字节落盘，避免在 UI 线程读大图/多图导致 ANR。
        new Thread(new Runnable() {
            @Override
            public void run() {
                File dir = new File(getFilesDir(), PENDING_DIR);
                if (!dir.exists() && !dir.mkdirs()) {
                    Log.w(TAG, "failed to mkdir " + dir);
                }

                ArrayList<String> files = new ArrayList<>();
                int idx = 0;
                long totalBytes = 0;
                String displayName = "";
                for (Uri u : uris) {
                    byte[] b = readUriBytes(u);
                    if (b != null && b.length > 0) {
                        totalBytes += b.length;
                        if (displayName.isEmpty()) {
                            String dn = queryDisplayName(u);
                            if (dn == null || dn.isEmpty()) {
                                dn = "文件_" + (idx + 1);
                            }
                            displayName = dn;
                        }
                        try {
                            File f = new File(dir,
                                System.currentTimeMillis() + "_" + (idx++) + ".bin");
                            try (FileOutputStream out = new FileOutputStream(f)) {
                                out.write(b);
                            }
                            files.add(f.getName());
                        } catch (Exception e) {
                            Log.w(TAG, "write file failed: " + u, e);
                        }
                    }
                }

                long receivedAt = System.currentTimeMillis();
                writeMeta(dir, action, mime, text, source,
                    uris.size(), files, receivedAt, totalBytes, displayName);

                runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        Intent toMain = new Intent(ShareReceiverActivity.this, ShareActivity.class);
                        toMain.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK
                            | Intent.FLAG_ACTIVITY_CLEAR_TOP
                            | Intent.FLAG_ACTIVITY_SINGLE_TOP);
                        try {
                            startActivity(toMain);
                        } catch (Exception e) {
                            Log.w(TAG, "start main failed", e);
                        }
                        finish();
                    }
                });
            }
        }).start();
    }

    // ── 收集图片/文件 URI（ClipData + EXTRA_STREAM 单/多 + getData，去重）──
    private LinkedHashSet<Uri> collectUris(Intent intent) {
        LinkedHashSet<Uri> uris = new LinkedHashSet<>();
        try {
            ClipData clipData = intent.getClipData();
            if (clipData != null) {
                for (int i = 0; i < clipData.getItemCount(); i++) {
                    Uri uri = clipData.getItemAt(i).getUri();
                    if (uri != null) uris.add(uri);
                }
            }
            Object single = android.os.Build.VERSION.SDK_INT >= 33
                ? intent.getParcelableExtra(Intent.EXTRA_STREAM, Uri.class)
                : intent.getParcelableExtra(Intent.EXTRA_STREAM);
            if (single instanceof Uri) uris.add((Uri) single);
            ArrayList<Uri> multi = android.os.Build.VERSION.SDK_INT >= 33
                ? intent.getParcelableArrayListExtra(Intent.EXTRA_STREAM, Uri.class)
                : intent.getParcelableArrayListExtra(Intent.EXTRA_STREAM);
            if (multi != null) {
                for (Uri u : multi) {
                    if (u != null) uris.add(u);
                }
            }
            if (intent.getData() != null) uris.add(intent.getData());
        } catch (Exception e) {
            // ignore malformed intent extras
        }
        return uris;
    }

    // ── 来源 app 解析（返回全包名）：getCallingPackage → callingUid 反查 → REFERRER host → 兜底 ──
    private String resolveSource(Intent intent) {
        // 1. getCallingPackage（权威包名）
        try {
            String pkg = getCallingPackage();
            if (pkg != null && !pkg.isEmpty()) return pkg;
        } catch (Exception ignored) {}

        // 2. callingUid 反查包名
        try {
            int uid = Binder.getCallingUid();
            if (uid > 0) {
                String pkg = getPackageManager().getNameForUid(uid);
                if (pkg != null && !pkg.isEmpty()) return pkg;
            }
        } catch (Exception ignored) {}

        // 3. EXTRA_REFERRER 的 host（形如包名/域，作为兜底展示）
        try {
            Uri ref = intent.getParcelableExtra(Intent.EXTRA_REFERRER);
            if (ref != null && ref.getHost() != null && !ref.getHost().isEmpty()) {
                return ref.getHost();
            }
        } catch (Exception ignored) {}

        // 兜底：不 fallback 自身包名，避免把 anystik 自己误判为来源
        return "未知来源";
    }

    private byte[] readUriBytes(Uri uri) {
        if (uri == null) return null;
        try (InputStream in = getContentResolver().openInputStream(uri)) {
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

    // ── 尝试从 content:// 查询原始文件名（OpenableColumns.DISPLAY_NAME）──
    private String queryDisplayName(Uri uri) {
        if (uri == null) return null;
        try (android.database.Cursor c = getContentResolver().query(uri, null,
                null, null, null)) {
            if (c != null && c.moveToFirst()) {
                int idx = c.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME);
                if (idx >= 0) return c.getString(idx);
            }
        } catch (Exception ignored) {}
        return null;
    }

    private void writeMeta(File dir, String action, String mime, String text,
            String source, int count, ArrayList<String> files,
            long receivedAt, long totalBytes, String displayName) {
        try {
            File target = new File(dir, META_FILE);
            File tmp = new File(dir, META_FILE + ".tmp");

            JSONObject meta = new JSONObject();
            meta.put("action", action == null ? "" : action);
            meta.put("mime", mime == null ? "" : mime);
            meta.put("text", text == null ? "" : text);
            meta.put("sourceApp", source == null ? "未知来源" : source);
            meta.put("imageCount", count);
            meta.put("receivedAt", receivedAt);
            meta.put("totalBytes", totalBytes);
            meta.put("displayName", displayName == null ? "" : displayName);
            meta.put("files", new JSONArray(files));

            try (FileOutputStream out = new FileOutputStream(tmp)) {
                out.write(meta.toString().getBytes("UTF-8"));
            }
            if (tmp.exists() && !tmp.renameTo(target)) {
                // rename 失败则直接写 target
                try (FileOutputStream out = new FileOutputStream(target)) {
                    out.write(meta.toString().getBytes("UTF-8"));
                }
            }
        } catch (Exception e) {
            Log.w(TAG, "write meta failed", e);
        }
    }
}
