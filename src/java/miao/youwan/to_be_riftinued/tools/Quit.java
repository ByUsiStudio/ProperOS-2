package miao.youwan.to_be_riftinued.tools;

import android.app.Activity;
import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.os.Process;

/**
 * 退出应用程序的工具类
 * 适配裕语言V3交互规范
 * 可用于结束当前软件进程
 */
public class Quit {

    /**
     * 结束当前应用程序进程
     * 适配裕语言V3交互，会在UI线程中执行finish操作
     * 
     * @param activity 当前Activity对象（裕语言预设变量）
     */
    public static void exitApp(final Activity activity) {
        if (activity != null) {
            // 确保在UI线程中执行finish操作
            if (Looper.myLooper() == Looper.getMainLooper()) {
                activity.finishAffinity();
            } else {
                new Handler(Looper.getMainLooper()).post(new Runnable() {
                    @Override
                    public void run() {
                        activity.finishAffinity();
                    }
                });
                // 给UI线程一点时间执行finish
                try {
                    Thread.sleep(100);
                } catch (InterruptedException e) {
                    e.printStackTrace();
                }
            }
        }
        
        // 杀死进程
        Process.killProcess(Process.myPid());
        System.exit(0);
    }

    /**
     * 静默退出应用程序
     * 不调用Activity的finish方法，直接结束进程
     * 适用于后台服务或非界面线程
     */
    public static void exitSilent() {
        Process.killProcess(Process.myPid());
        System.exit(0);
    }

    /**
     * 延迟退出应用程序
     * 在指定延迟后退出应用
     * 
     * @param activity 当前Activity对象（裕语言预设变量）
     * @param delayMillis 延迟毫秒数
     */
    public static void exitAppDelayed(final Activity activity, final long delayMillis) {
        new Thread(new Runnable() {
            @Override
            public void run() {
                try {
                    Thread.sleep(delayMillis);
                } catch (InterruptedException e) {
                    e.printStackTrace();
                }
                exitApp(activity);
            }
        }).start();
    }
    
    /**
     * 安全退出（带Context参数，适配裕语言）
     * 此方法会尝试优雅地关闭所有Activity
     * 
     * @param activity Activity对象
     * @param context Context对象（裕语言预设变量）
     */
    public static void exitAppSafe(Activity activity, Context context) {
        if (activity != null) {
            exitApp(activity);
        } else if (context instanceof Activity) {
            exitApp((Activity) context);
        } else {
            exitSilent();
        }
    }
}