package io.fedlet.mobutil;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;
import android.os.Handler;
import android.os.Looper;

import java.util.HashSet;

public class NetworkMonitor {
    // JNI：三布尔入参 = WiFi / 移动数据 / 以太网 通道是否在位，仅状态变化时回调
    private static native void onTransportChanged(boolean wifiConnected, boolean mobileConnected, boolean ethernetConnected);

    private static ConnectivityManager connectivityManager;
    private static Handler handler;
    private static boolean started = false;

    private static final int[] TRANSPORTS = {
        NetworkCapabilities.TRANSPORT_WIFI,
        NetworkCapabilities.TRANSPORT_CELLULAR,
        NetworkCapabilities.TRANSPORT_ETHERNET
    };
    private static final int COUNT = TRANSPORTS.length;

    // 每个通道当前在位的网络集合（区分"WiFi 期间切换 AP"等短暂抖动）
    @SuppressWarnings("unchecked")
    private static final HashSet<Network>[] networks =
        new HashSet[]{ new HashSet<>(), new HashSet<>(), new HashSet<>() };
    private static final boolean[] committed = { false, false, false };
    private static Runnable[] pendingDown = new Runnable[COUNT];
    private static ConnectivityManager.NetworkCallback[] callbacks;

    // 确认窗口：通道从"在"变"不在"后延迟复查，防息屏/Doze 射频挂起假消失
    private static final int CONFIRM_MS = 5000;

    public static void startMonitoring(Context ctx) {
        if (started) return;
        connectivityManager = (ConnectivityManager)
            ctx.getSystemService(Context.CONNECTIVITY_SERVICE);
        handler = new Handler(Looper.getMainLooper());

        callbacks = new ConnectivityManager.NetworkCallback[COUNT];
        for (int i = 0; i < COUNT; i++) {
            final int index = i;
            NetworkRequest request = new NetworkRequest.Builder()
                .addTransportType(TRANSPORTS[i])
                .build();
            callbacks[i] = new ConnectivityManager.NetworkCallback() {
                @Override
                public void onAvailable(Network network) {
                    networks[index].add(network);
                    cancelConfirm(index);
                    if (!committed[index]) { committed[index] = true; notifyNative(); }
                }

                @Override
                public void onLost(Network network) {
                    networks[index].remove(network);
                    if (!networks[index].isEmpty()) {
                        cancelConfirm(index); // 该通道仍有其它网络，非真断
                        return;
                    }
                    scheduleConfirm(index);
                }

                @Override
                public void onCapabilitiesChanged(Network network, NetworkCapabilities caps) {
                    rescan(true);
                }
            };
            connectivityManager.registerNetworkCallback(request, callbacks[i]);
        }

        rescan(false); // 静默基线：启动不触发 native（不弹 toast）
        started = true;
    }

    private static void notifyNative() {
        onTransportChanged(committed[0], committed[1], committed[2]);
    }

    private static boolean transportPresent(int index) {
        if (connectivityManager == null) return false;
        for (Network n : connectivityManager.getAllNetworks()) {
            NetworkCapabilities caps = connectivityManager.getNetworkCapabilities(n);
            if (caps != null && caps.hasTransport(TRANSPORTS[index])) return true;
        }
        return false;
    }

    private static void scheduleConfirm(int index) {
        if (!committed[index] || pendingDown[index] != null) return;
        pendingDown[index] = new Runnable() {
            @Override
            public void run() {
                pendingDown[index] = null;
                if (!transportPresent(index)) { // 到点快照复核，仍不在才判定断开
                    committed[index] = false;
                    notifyNative();
                }
            }
        };
        handler.postDelayed(pendingDown[index], CONFIRM_MS);
    }

    private static void cancelConfirm(int index) {
        if (pendingDown[index] != null) {
            handler.removeCallbacks(pendingDown[index]);
            pendingDown[index] = null;
        }
    }

    private static void rescan(boolean notify) {
        if (connectivityManager == null) return;
        for (int i = 0; i < COUNT; i++) {
            if (transportPresent(i)) {
                cancelConfirm(i);
                if (!committed[i]) { committed[i] = true; if (notify) notifyNative(); }
            } else {
                scheduleConfirm(i);
            }
        }
    }

    // 回前台时重扫：补验收/息屏期间被漏掉的通道状态
    public static void checkCurrentNetwork(Context ctx) {
        if (connectivityManager == null) return;
        rescan(true);
    }

    public static void stopMonitoring(Context ctx) {
        started = false;
        for (int i = 0; i < COUNT; i++) cancelConfirm(i);
        if (connectivityManager == null) return;
        if (callbacks != null) {
            for (ConnectivityManager.NetworkCallback cb : callbacks) {
                if (cb != null) connectivityManager.unregisterNetworkCallback(cb);
            }
        }
        callbacks = null;
        connectivityManager = null;
    }
}