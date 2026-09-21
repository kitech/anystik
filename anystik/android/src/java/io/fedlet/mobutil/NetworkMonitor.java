package io.fedlet.mobutil;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.os.Handler;
import android.os.Looper;

public class NetworkMonitor {
    private static native void onNetworkChanged(boolean isConnected, String networkType);
    private static ConnectivityManager.NetworkCallback networkCallback;
    private static ConnectivityManager connectivityManager;
    private static boolean lastConnected = false;
    private static String lastType = "Unknown";
    private static Handler handler;
    private static Runnable pendingOffline;
    private static final int OFFLINE_DELAY_MS = 3000;

    public static void startMonitoring(Context ctx) {
        if (networkCallback != null) return;
        connectivityManager = (ConnectivityManager)
            ctx.getSystemService(Context.CONNECTIVITY_SERVICE);

        networkCallback = new ConnectivityManager.NetworkCallback() {
            @Override
            public void onAvailable(Network network) {
                if (pendingOffline != null) {
                    handler.removeCallbacks(pendingOffline);
                    pendingOffline = null;
                }
                updateState(network);
            }

            @Override
            public void onLost(Network network) {
                // 立即复核当前活跃网络：若已自愈/回调乱序（如恢复前台瞬间），
                // 提前取消离线判定，避免误报
                if (hasValidatedNetwork()) {
                    if (pendingOffline != null) {
                        handler.removeCallbacks(pendingOffline);
                        pendingOffline = null;
                    }
                    return;
                }
                if (pendingOffline != null) return;
                pendingOffline = new Runnable() {
                    @Override
                    public void run() {
                        pendingOffline = null;
                        // 到点重新实测：后台冻结的 delay 任务会随前台恢复一起补跑，
                        // 此时 WiFi 往往已可用，不得只凭 lastConnected 判离线
                        if (hasValidatedNetwork()) return;
                        if (lastConnected) {
                            lastConnected = false;
                            lastType = "Unknown";
                            onNetworkChanged(false, "Unknown");
                        }
                    }
                };
                handler.postDelayed(pendingOffline, OFFLINE_DELAY_MS);
            }

            @Override
            public void onCapabilitiesChanged(Network network, NetworkCapabilities caps) {
                if (pendingOffline != null) {
                    handler.removeCallbacks(pendingOffline);
                    pendingOffline = null;
                }
                updateStateFromCaps(caps);
            }
        };

        connectivityManager.registerDefaultNetworkCallback(networkCallback);
        handler = new Handler(Looper.getMainLooper());

        // 初始状态检查
        Network active = connectivityManager.getActiveNetwork();
        if (active != null) {
            NetworkCapabilities caps = connectivityManager.getNetworkCapabilities(active);
            if (caps != null) {
                updateStateFromCaps(caps);
            }
        }
    }

    private static void updateState(Network network) {
        if (network == null) {
            if (lastConnected) {
                lastConnected = false;
                lastType = "Unknown";
                onNetworkChanged(false, "Unknown");
            }
            return;
        }
        NetworkCapabilities caps = connectivityManager.getNetworkCapabilities(network);
        if (caps != null) {
            updateStateFromCaps(caps);
        }
    }

    private static void updateStateFromCaps(NetworkCapabilities caps) {
        boolean connected = caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
                         && caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED);
        String type = "Unknown";
        if (connected) {
            if (caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)) {
                type = "WiFi";
            } else if (caps.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR)) {
                type = "Mobile";
            } else if (caps.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET)) {
                type = "Ethernet";
            } else {
                type = "Other";
            }
        }
        if (connected != lastConnected || !type.equals(lastType)) {
            lastConnected = connected;
            lastType = type;
            onNetworkChanged(connected, type);
        }
    }

    // 活跃网络是否具备已验证的互联网能力（与 updateStateFromCaps 判定一致）
    private static boolean hasValidatedNetwork() {
        if (connectivityManager == null) return false;
        Network active = connectivityManager.getActiveNetwork();
        if (active == null) return false;
        NetworkCapabilities caps = connectivityManager.getNetworkCapabilities(active);
        return caps != null
            && caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
            && caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED);
    }

    public static void checkCurrentNetwork(Context ctx) {
        if (pendingOffline != null) {
            handler.removeCallbacks(pendingOffline);
            pendingOffline = null;
        }
        if (connectivityManager == null) return;
        Network active = connectivityManager.getActiveNetwork();
        if (active != null) {
            NetworkCapabilities caps = connectivityManager.getNetworkCapabilities(active);
            if (caps != null) {
                updateStateFromCaps(caps);
                return;
            }
        }
        // 没有活跃网络
        if (lastConnected) {
            lastConnected = false;
            lastType = "Unknown";
            onNetworkChanged(false, "Unknown");
        }
    }

    public static void stopMonitoring(Context ctx) {
        if (pendingOffline != null) {
            handler.removeCallbacks(pendingOffline);
            pendingOffline = null;
        }
        if (networkCallback != null && connectivityManager != null) {
            connectivityManager.unregisterNetworkCallback(networkCallback);
            networkCallback = null;
            connectivityManager = null;
        }
    }
}
