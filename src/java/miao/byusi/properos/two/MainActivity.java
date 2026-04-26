package miao.byusi.properos.two;

import android.app.Activity;
import android.app.ActivityOptions;
import android.app.AlertDialog;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.graphics.Bitmap;
import android.graphics.Color;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.view.animation.Animation;
import android.view.animation.ScaleAnimation;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import java.io.BufferedReader;
import java.io.DataOutputStream;
import java.io.InputStreamReader;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;

public class MainActivity extends Activity {

    private static final long SPLASH_DURATION = 3000; // 减少到3秒
    private static final String PREF_NAME = "root_permission_pref";
    private static final String KEY_DONT_ASK_AGAIN = "dont_ask_again";
    private static final String KEY_REQUEST_COUNT = "request_count";
    private static final String KEY_ROOT_GRANTED = "root_granted"; // 记录是否已授权过
    
    private FrameLayout splashContainer;
    private FrameLayout cardContainer;
    private ImageView logoImage;
    private Handler mainHandler = new Handler(Looper.getMainLooper());
    private String appVersion = "";
    private SharedPreferences sharedPreferences;
    
    private ExecutorService executorService = Executors.newSingleThreadExecutor();
    private AtomicBoolean isRequesting = new AtomicBoolean(false);
    private AtomicBoolean hasRoot = new AtomicBoolean(false);
    private AtomicBoolean isDestroyed = new AtomicBoolean(false);
    private AtomicBoolean hasNavigated = new AtomicBoolean(false);

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        
        isDestroyed.set(false);
        hasNavigated.set(false);
        
        // 初始化 SharedPreferences
        sharedPreferences = getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE);
        
        // 获取应用版本信息
        getAppVersionInfo();
        
        // 设置全屏透明窗口
        setupFullScreenTransparent();
        
        // 创建启动布局（先显示界面，不卡顿）
        createSplashLayout();
        
        // 延迟一点点让界面先渲染完成
        mainHandler.postDelayed(new Runnable() {
            @Override
            public void run() {
                if (!isDestroyed.get()) {
                    // 请求 Root 权限
                    requestRootPermission();
                }
            }
        }, 100);
    }
    
    /**
     * 获取应用版本信息
     */
    private void getAppVersionInfo() {
        try {
            PackageManager pm = getPackageManager();
            PackageInfo packageInfo = pm.getPackageInfo(getPackageName(), 0);
            appVersion = packageInfo.versionName;
        } catch (Exception e) {
            e.printStackTrace();
            appVersion = "1.0.0";
        }
    }
    
    /**
     * 请求 Root 权限（完全异步，不阻塞UI）
     */
    private void requestRootPermission() {
        // 检查是否已经授权过
        boolean rootGranted = sharedPreferences.getBoolean(KEY_ROOT_GRANTED, false);
        if (rootGranted) {
            hasRoot.set(true);
            startSplashTimer();
            return;
        }
        
        // 异步检查 root 权限
        executorService.execute(new Runnable() {
            @Override
            public void run() {
                if (isDestroyed.get()) return;
                
                boolean hasRootAccess = checkRootAccess();
                
                mainHandler.post(new Runnable() {
                    @Override
                    public void run() {
                        if (isDestroyed.get()) return;
                        
                        if (hasRootAccess) {
                            hasRoot.set(true);
                            sharedPreferences.edit().putBoolean(KEY_ROOT_GRANTED, true).apply();
                            startSplashTimer();
                        } else {
                            // 没有 root 权限，尝试请求
                            tryRequestRoot();
                        }
                    }
                });
            }
        });
    }
    
    /**
     * 检查是否已有 Root 访问权限（轻量级检查）
     */
    private boolean checkRootAccess() {
        Process process = null;
        DataOutputStream os = null;
        try {
            process = Runtime.getRuntime().exec("su");
            os = new DataOutputStream(process.getOutputStream());
            os.writeBytes("exit\n");
            os.flush();
            int exitCode = process.waitFor();
            return exitCode == 0;
        } catch (Exception e) {
            return false;
        } finally {
            try {
                if (os != null) os.close();
                if (process != null) process.destroy();
            } catch (Exception e) {
                // ignore
            }
        }
    }
    
    /**
     * 尝试请求 Root 权限
     */
    private void tryRequestRoot() {
        if (isRequesting.get()) return;
        
        // 检查是否选择了"不再提示"
        boolean dontAskAgain = sharedPreferences.getBoolean(KEY_DONT_ASK_AGAIN, false);
        if (dontAskAgain) {
            startSplashTimer();
            return;
        }
        
        // 获取请求次数
        int requestCount = sharedPreferences.getInt(KEY_REQUEST_COUNT, 0);
        
        if (requestCount == 0) {
            // 第一次请求，静默尝试
            sharedPreferences.edit().putInt(KEY_REQUEST_COUNT, 1).apply();
            performRootRequest();
        } else {
            // 第二次及之后，弹出对话框询问
            showRootRequestDialog();
        }
    }
    
    /**
     * 执行 Root 权限请求（异步）
     */
    private void performRootRequest() {
        if (isRequesting.getAndSet(true)) return;
        
        executorService.execute(new Runnable() {
            @Override
            public void run() {
                if (isDestroyed.get()) return;
                
                boolean success = false;
                Process process = null;
                DataOutputStream os = null;
                
                try {
                    process = Runtime.getRuntime().exec("su");
                    os = new DataOutputStream(process.getOutputStream());
                    os.writeBytes("echo test\n");
                    os.writeBytes("exit\n");
                    os.flush();
                    
                    int exitCode = process.waitFor();
                    success = (exitCode == 0);
                    
                } catch (Exception e) {
                    // ignore
                } finally {
                    try {
                        if (os != null) os.close();
                        if (process != null) process.destroy();
                    } catch (Exception e) {
                        // ignore
                    }
                }
                
                final boolean finalSuccess = success;
                mainHandler.post(new Runnable() {
                    @Override
                    public void run() {
                        if (isDestroyed.get()) return;
                        
                        isRequesting.set(false);
                        
                        if (finalSuccess) {
                            hasRoot.set(true);
                            sharedPreferences.edit().putBoolean(KEY_ROOT_GRANTED, true).apply();
                            Toast.makeText(MainActivity.this, "Root 权限已获取", Toast.LENGTH_SHORT).show();
                            startSplashTimer();
                        } else {
                            startSplashTimer();
                        }
                    }
                });
            }
        });
    }
    
    /**
     * 显示 Root 权限请求对话框
     */
    private void showRootRequestDialog() {
        AlertDialog.Builder builder = new AlertDialog.Builder(this);
        builder.setTitle("请求 Root 权限");
        builder.setMessage("本应用需要 Root 权限才能正常工作。\n\n" +
                "Root 权限将用于：\n" +
                "• Xposed 模块激活\n" +
                "• 系统功能增强\n" +
                "• 应用行为记录\n\n" +
                "是否现在授权？");
        
        builder.setPositiveButton("授权", new DialogInterface.OnClickListener() {
            @Override
            public void onClick(DialogInterface dialog, int which) {
                performRootRequest();
            }
        });
        
        builder.setNegativeButton("拒绝", new DialogInterface.OnClickListener() {
            @Override
            public void onClick(DialogInterface dialog, int which) {
                startSplashTimer();
            }
        });
        
        builder.setNeutralButton("不再提示", new DialogInterface.OnClickListener() {
            @Override
            public void onClick(DialogInterface dialog, int which) {
                sharedPreferences.edit().putBoolean(KEY_DONT_ASK_AGAIN, true).apply();
                startSplashTimer();
            }
        });
        
        builder.setCancelable(false);
        
        try {
            builder.show();
        } catch (Exception e) {
            startSplashTimer();
        }
    }
    
    /**
     * 启动跳转计时器
     */
    private void startSplashTimer() {
        mainHandler.postDelayed(new Runnable() {
            @Override
            public void run() {
                if (!isDestroyed.get() && !hasNavigated.getAndSet(true)) {
                    navigateToMainActivity();
                }
            }
        }, SPLASH_DURATION);
    }
    
    /**
     * 激活 Xposed 模块
     */
    private void activateXposedModule() {
        if (!hasRoot.get()) return;
        
        executorService.execute(new Runnable() {
            @Override
            public void run() {
                String modulePackage = getPackageName();
                String command = "echo \"" + modulePackage + "\" >> /data/data/de.robv.android.xposed.installer/conf/modules.list 2>/dev/null\n" +
                        "chmod 644 /data/data/de.robv.android.xposed.installer/conf/modules.list 2>/dev/null\n" +
                        "echo \"" + modulePackage + "\" >> /data/data/org.lsposed.manager/conf/modules.list 2>/dev/null\n";
                
                Process process = null;
                DataOutputStream os = null;
                try {
                    process = Runtime.getRuntime().exec("su");
                    os = new DataOutputStream(process.getOutputStream());
                    os.writeBytes(command);
                    os.writeBytes("exit\n");
                    os.flush();
                    process.waitFor();
                } catch (Exception e) {
                    // ignore
                } finally {
                    try {
                        if (os != null) os.close();
                        if (process != null) process.destroy();
                    } catch (Exception e) {
                        // ignore
                    }
                }
            }
        });
    }
    
    /**
     * 设置全屏透明窗口
     */
    private void setupFullScreenTransparent() {
        getWindow().setBackgroundDrawable(null);
        
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            getWindow().clearFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND);
            getWindow().setFlags(
                WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS,
                WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS
            );
            
            View decorView = getWindow().getDecorView();
            int uiOptions = View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY;
            
            decorView.setSystemUiVisibility(uiOptions);
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_TRANSLUCENT_NAVIGATION);
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_TRANSLUCENT_STATUS);
            getWindow().setStatusBarColor(Color.TRANSPARENT);
            getWindow().setNavigationBarColor(Color.TRANSPARENT);
        }
        
        getWindow().setBackgroundDrawableResource(android.R.color.transparent);
    }
    
    private void createSplashLayout() {
        // 创建容器
        splashContainer = new FrameLayout(this);
        splashContainer.setLayoutParams(new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
        splashContainer.setBackgroundColor(Color.TRANSPARENT);
        
        // 创建卡片容器
        cardContainer = new FrameLayout(this);
        FrameLayout.LayoutParams cardParams = new FrameLayout.LayoutParams(
                dpToPx(280),
                dpToPx(320));
        cardParams.gravity = android.view.Gravity.CENTER;
        cardContainer.setLayoutParams(cardParams);
        cardContainer.setBackgroundColor(Color.parseColor("#E6FFFFFF"));
        
        // 创建内部布局
        LinearLayout innerLayout = new LinearLayout(this);
        innerLayout.setOrientation(LinearLayout.VERTICAL);
        innerLayout.setLayoutParams(new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
        innerLayout.setGravity(android.view.Gravity.CENTER);
        innerLayout.setPadding(dpToPx(24), dpToPx(32), dpToPx(24), dpToPx(32));
        
        // Logo 容器
        FrameLayout circleContainer = new FrameLayout(this);
        LinearLayout.LayoutParams circleParams = new LinearLayout.LayoutParams(
                dpToPx(120),
                dpToPx(120));
        circleParams.gravity = android.view.Gravity.CENTER_HORIZONTAL;
        circleParams.bottomMargin = dpToPx(24);
        circleContainer.setLayoutParams(circleParams);
        circleContainer.setBackgroundColor(Color.parseColor("#B3FFFFFF"));
        
        // Logo 图片
        logoImage = new ImageView(this);
        FrameLayout.LayoutParams logoParams = new FrameLayout.LayoutParams(
                dpToPx(100),
                dpToPx(100));
        logoParams.gravity = android.view.Gravity.CENTER;
        logoImage.setLayoutParams(logoParams);
        logoImage.setScaleType(ImageView.ScaleType.CENTER_CROP);
        
        int logoResId = getResources().getIdentifier("properos2", "drawable", getPackageName());
        if (logoResId == 0) {
            logoResId = getResources().getIdentifier("logo", "drawable", getPackageName());
        }
        if (logoResId == 0) {
            logoResId = getApplicationInfo().icon;
        }
        logoImage.setImageResource(logoResId);
        
        circleContainer.addView(logoImage);
        innerLayout.addView(circleContainer);
        
        // 应用名称
        TextView appName = new TextView(this);
        LinearLayout.LayoutParams nameParams = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        nameParams.gravity = android.view.Gravity.CENTER_HORIZONTAL;
        nameParams.topMargin = dpToPx(8);
        nameParams.bottomMargin = dpToPx(8);
        appName.setLayoutParams(nameParams);
        appName.setText(getString(R.string.app_name));
        appName.setTextSize(20);
        appName.setTextColor(Color.parseColor("#333333"));
        appName.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        innerLayout.addView(appName);
        
        // 版本信息
        TextView versionText = new TextView(this);
        LinearLayout.LayoutParams versionParams = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        versionParams.gravity = android.view.Gravity.CENTER_HORIZONTAL;
        versionText.setLayoutParams(versionParams);
        versionText.setText("Version " + appVersion);
        versionText.setTextSize(12);
        versionText.setTextColor(Color.parseColor("#888888"));
        innerLayout.addView(versionText);
        
        cardContainer.addView(innerLayout);
        splashContainer.addView(cardContainer);
        
        setContentView(splashContainer);
        
        // 动画
        startScaleAnimation();
    }
    
    private void startScaleAnimation() {
        ScaleAnimation cardScaleAnim = new ScaleAnimation(
                0.7f, 1.0f, 0.7f, 1.0f,
                Animation.RELATIVE_TO_SELF, 0.5f,
                Animation.RELATIVE_TO_SELF, 0.5f);
        cardScaleAnim.setDuration(600);
        cardScaleAnim.setFillAfter(true);
        
        ScaleAnimation logoScaleAnim = new ScaleAnimation(
                0.5f, 1.0f, 0.5f, 1.0f,
                Animation.RELATIVE_TO_SELF, 0.5f,
                Animation.RELATIVE_TO_SELF, 0.5f);
        logoScaleAnim.setDuration(500);
        logoScaleAnim.setStartOffset(80);
        logoScaleAnim.setFillAfter(true);
        
        if (cardContainer != null) {
            cardContainer.startAnimation(cardScaleAnim);
        }
        if (logoImage != null) {
            logoImage.startAnimation(logoScaleAnim);
        }
    }
    
    private void navigateToMainActivity() {
        // 激活 Xposed 模块（后台执行）
        activateXposedModule();
        
        Intent intent = new Intent();
        try {
            intent.setClassName(getPackageName(), "com.iapp.app.run.mian");
            if (getPackageManager().resolveActivity(intent, 0) == null) {
                intent.setClassName(getPackageName(), "com.iapp.app.run.main");
                if (getPackageManager().resolveActivity(intent, 0) == null) {
                    intent = getPackageManager().getLaunchIntentForPackage(getPackageName());
                }
            }
        } catch (Exception e) {
            intent = getPackageManager().getLaunchIntentForPackage(getPackageName());
        }
        
        if (intent != null) {
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            intent.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP);
            startActivity(intent);
        }
        
        finish();
    }
    
    private int dpToPx(int dp) {
        float density = getResources().getDisplayMetrics().density;
        return (int) (dp * density + 0.5f);
    }
    
    @Override
    protected void onDestroy() {
        super.onDestroy();
        isDestroyed.set(true);
        mainHandler.removeCallbacksAndMessages(null);
        if (executorService != null && !executorService.isShutdown()) {
            executorService.shutdownNow();
        }
    }
}