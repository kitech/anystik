package io.fedlet.mobutil;

import android.app.Activity;
import android.os.Bundle;
import org.qtproject.qt.android.bindings.QtApplication;

// 值 B 来源：Android 生命周期权威判定"主 ShareActivity 是否运行"。
// 继承 QtApplication（保留 Qt 初始化继承链），仅新增 ActivityLifecycleCallbacks。
// 不用 lambda（工程 desugar 限制，用匿名类）。
public class AnystikApplication extends QtApplication {

    private static volatile boolean sMainCreated;
    private static volatile boolean sMainVisible;

    @Override
    public void onCreate() {
        super.onCreate();
        registerActivityLifecycleCallbacks(new android.app.Application.ActivityLifecycleCallbacks() {
            @Override public void onActivityCreated(Activity a, Bundle b) {
                if (a instanceof ShareActivity) sMainCreated = true;
            }
            @Override public void onActivityStarted(Activity a) {
                if (a instanceof ShareActivity) sMainVisible = true;
            }
            @Override public void onActivityStopped(Activity a) {
                if (a instanceof ShareActivity) sMainVisible = false;
            }
            @Override public void onActivityDestroyed(Activity a) {
                if (a instanceof ShareActivity) sMainCreated = false;
            }
            @Override public void onActivityResumed(Activity a) { }
            @Override public void onActivityPaused(Activity a) { }
            @Override public void onActivitySaveInstanceState(Activity a, Bundle b) { }
        });
    }

    // 是否已创建且未销毁（"正在运行"）
    public static boolean isMainActivityRunning() { return sMainCreated; }

    // 是否已 start 且未 stop（"前台可见"）
    public static boolean isMainActivityVisible()  { return sMainVisible; }
}
