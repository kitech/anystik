package io.fedlet.mobutil;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;

public class NetworkMonitor {
    private static native void onNetworkChanged(boolean isConnected, String networkType);
    private static ConnectivityManager.NetworkCallback networkCallback;
    private static ConnectivityManager connectivityManager;
    private static boolean lastConnected = false;
    private static String lastType = "Unknown";

    public static void startMonitoring(Context ctx) {
        if (networkCallback != null) return;
        connectivityManager = (ConnectivityManager)
            ctx.getSystemService(Context.CONNECTIVITY_SERVICE);

        networkCallback = new ConnectivityManager.NetworkCallback() {
            @Override
            public void onAvailable(Network network) {
                updateState(network);
            }

            @Override
            public void onLost(Network network) {
                updateState(null);
            }

            @Override
            public void onCapabilitiesChanged(Network network, NetworkCapabilities caps) {
                updateStateFromCaps(caps);
            }
        };

        connectivityManager.registerDefaultNetworkCallback(networkCallback);

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

    public static void checkCurrentNetwork(Context ctx) {
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
        if (networkCallback != null && connectivityManager != null) {
            connectivityManager.unregisterNetworkCallback(networkCallback);
            networkCallback = null;
            connectivityManager = null;
        }
    }
}
