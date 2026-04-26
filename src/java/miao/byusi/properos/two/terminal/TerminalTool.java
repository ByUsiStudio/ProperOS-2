// 包名: miao.byusi.properos.two.terminal
// 文件名: TerminalTool.java

package miao.byusi.properos.two.terminal;

import android.app.Activity;
import android.content.Context;
import android.graphics.Color;
import android.os.Build;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewParent;
import android.view.Window;
import android.view.WindowManager;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.File;
import java.lang.reflect.Constructor;
import java.lang.reflect.Method;

/**
 * Termux 原生终端工具类 (适配 termux-view-0.114.aar)
 */
public class TerminalTool {

    private Activity activity;
    private View terminalView;
    private Object terminalSession;
    private boolean isInitialized = false;
    private FrameLayout rootContainer;
    
    private static final int CONTAINER_ID = 0x7F0F0001;
    private static final int TERMINAL_VIEW_ID = 0x7F0F0002;
    private static final int EXTRA_KEYS_ID = 0x7F0F0003;
    
    private static final int DEFAULT_COLUMNS = 80;
    private static final int DEFAULT_ROWS = 40;
    
    public TerminalTool(Activity activity) {
        this.activity = activity;
    }
    
    public void showTerminal() {
        if (activity == null || activity.isFinishing()) {
            return;
        }
        
        setStatusBarBlack();
        createRootContainer();
        addToActivityRoot();
        initTermuxTerminal();
    }
    
    private void setStatusBarBlack() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            Window window = activity.getWindow();
            window.addFlags(WindowManager.LayoutParams.FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS);
            window.setStatusBarColor(Color.BLACK);
            
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                View decorView = window.getDecorView();
                decorView.setSystemUiVisibility(
                    decorView.getSystemUiVisibility() & ~View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR
                );
            }
        }
    }
    
    private void createRootContainer() {
        LinearLayout mainLayout = new LinearLayout(activity);
        mainLayout.setId(CONTAINER_ID);
        mainLayout.setOrientation(LinearLayout.VERTICAL);
        mainLayout.setLayoutParams(new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.MATCH_PARENT
        ));
        mainLayout.setBackgroundColor(Color.BLACK);
        
        FrameLayout terminalContainer = new FrameLayout(activity);
        terminalContainer.setId(TERMINAL_VIEW_ID);
        LinearLayout.LayoutParams termParams = new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            0,
            1f
        );
        terminalContainer.setLayoutParams(termParams);
        terminalContainer.setBackgroundColor(Color.BLACK);
        
        mainLayout.addView(terminalContainer);
        
        LinearLayout extraKeysContainer = createExtraKeysContainer();
        if (extraKeysContainer != null) {
            mainLayout.addView(extraKeysContainer);
        }
        
        this.rootContainer = new FrameLayout(activity);
        this.rootContainer.addView(mainLayout);
    }
    
    private LinearLayout createExtraKeysContainer() {
        LinearLayout extraKeysLayout = new LinearLayout(activity);
        extraKeysLayout.setId(EXTRA_KEYS_ID);
        extraKeysLayout.setOrientation(LinearLayout.HORIZONTAL);
        extraKeysLayout.setLayoutParams(new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.WRAP_CONTENT
        ));
        extraKeysLayout.setBackgroundColor(Color.parseColor("#1a1a1a"));
        extraKeysLayout.setPadding(8, 4, 8, 4);
        
        String[] defaultKeys = {"ESC", "TAB", "CTRL", "ALT", "↑", "↓", "←", "→", "HOME", "END"};
        
        for (String key : defaultKeys) {
            Button btn = new Button(activity);
            btn.setText(key);
            btn.setTextColor(Color.WHITE);
            btn.setTextSize(12f);
            btn.setBackgroundColor(Color.parseColor("#333333"));
            LinearLayout.LayoutParams btnParams = new LinearLayout.LayoutParams(
                0,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                1f
            );
            btnParams.setMargins(2, 2, 2, 2);
            btn.setLayoutParams(btnParams);
            btn.setAllCaps(false);
            
            btn.setOnClickListener(v -> {
                if (terminalSession != null) {
                    sendSpecialKey(key);
                }
            });
            
            extraKeysLayout.addView(btn);
        }
        
        return extraKeysLayout;
    }
    
    private void sendSpecialKey(String key) {
        try {
            if (terminalSession == null) return;
            
            Method writeMethod = terminalSession.getClass().getMethod("write", byte[].class);
            
            switch (key) {
                case "ESC":
                    writeMethod.invoke(terminalSession, new byte[]{27});
                    break;
                case "TAB":
                    writeMethod.invoke(terminalSession, new byte[]{9});
                    break;
                case "CTRL":
                    // Ctrl 组合键示例：发送 Ctrl+C
                    writeMethod.invoke(terminalSession, new byte[]{3});
                    break;
                case "ALT":
                    // Alt 组合键示例
                    writeMethod.invoke(terminalSession, new byte[]{27});
                    break;
                case "↑":
                    writeMethod.invoke(terminalSession, new byte[]{27, 91, 65});
                    break;
                case "↓":
                    writeMethod.invoke(terminalSession, new byte[]{27, 91, 66});
                    break;
                case "←":
                    writeMethod.invoke(terminalSession, new byte[]{27, 91, 68});
                    break;
                case "→":
                    writeMethod.invoke(terminalSession, new byte[]{27, 91, 67});
                    break;
                case "HOME":
                    writeMethod.invoke(terminalSession, new byte[]{27, 91, 72});
                    break;
                case "END":
                    writeMethod.invoke(terminalSession, new byte[]{27, 91, 70});
                    break;
            }
        } catch (Exception e) {
            e.printStackTrace();
        }
    }
    
    private void addToActivityRoot() {
        if (rootContainer == null) return;
        
        ViewGroup rootView = (ViewGroup) activity
            .getWindow()
            .getDecorView()
            .findViewById(android.R.id.content);
        
        View existing = activity.findViewById(CONTAINER_ID);
        if (existing != null && existing.getParent() != null) {
            ((ViewGroup) existing.getParent()).removeView(existing);
        }
        
        rootView.addView(rootContainer);
    }
    
    /**
     * 初始化 Termux 终端 - 根据错误信息修正构造函数参数
     */
    private void initTermuxTerminal() {
        try {
            // 1. 加载类
            Class<?> terminalSessionClass = Class.forName("com.termux.terminal.TerminalSession");
            Class<?> terminalViewClass = Class.forName("com.termux.view.TerminalView");
            
            // 2. 获取工作目录
            File filesDir = activity.getFilesDir();
            File homeDir = new File(filesDir, "termux-home");
            if (!homeDir.exists()) {
                homeDir.mkdirs();
            }
            
            // 3. 为 shell 设置环境变量
            String[] env = new String[]{
                "TERM=xterm-256color",
                "HOME=" + homeDir.getAbsolutePath(),
                "PATH=/system/bin:/system/xbin:" + homeDir.getAbsolutePath() + "/bin",
                "PREFIX=" + homeDir.getAbsolutePath()
            };
            
            // 4. 创建 TerminalSession - 根据错误信息，参数顺序可能是:
            //    (String, String, String[], String[], int, int, File, String, boolean)
            //    注意：第4个参数期望 String[] 类型
            
            Object session = null;
            Constructor<?>[] constructors = terminalSessionClass.getConstructors();
            
            // 打印所有构造函数用于调试
            for (Constructor<?> cons : constructors) {
                Class<?>[] paramTypes = cons.getParameterTypes();
                StringBuilder sb = new StringBuilder();
                for (Class<?> pt : paramTypes) {
                    sb.append(pt.getSimpleName()).append(", ");
                }
                android.util.Log.d("TerminalTool", "Constructor: " + sb.toString());
            }
            
            // 尝试匹配正确的构造函数
            for (Constructor<?> cons : constructors) {
                Class<?>[] paramTypes = cons.getParameterTypes();
                
                // 尝试匹配有9个参数的构造函数
                if (paramTypes.length == 9) {
                    // 参数顺序可能是: 
                    // 0: String (executablePath)
                    // 1: String (cwd)
                    // 2: String[] (env)
                    // 3: String[] (args)  <-- 之前把 int 传给了这里
                    // 4: int (columns)
                    // 5: int (rows)
                    // 6: File (homeDir)
                    // 7: String (charset)
                    // 8: boolean (utf8?)
                    
                    if (paramTypes[0] == String.class && 
                        paramTypes[2] == String[].class &&
                        paramTypes[3] == String[].class &&
                        paramTypes[4] == int.class &&
                        paramTypes[5] == int.class) {
                        
                        String[] emptyArgs = new String[0];
                        session = cons.newInstance(null, null, env, emptyArgs, DEFAULT_COLUMNS, DEFAULT_ROWS, homeDir, "UTF-8", true);
                        break;
                    }
                }
                // 尝试匹配有8个参数的构造函数
                else if (paramTypes.length == 8) {
                    if (paramTypes[0] == String.class && 
                        paramTypes[2] == String[].class &&
                        paramTypes[3] == int.class &&
                        paramTypes[4] == int.class) {
                        
                        session = cons.newInstance(null, null, env, DEFAULT_COLUMNS, DEFAULT_ROWS, homeDir, "UTF-8", true);
                        break;
                    }
                }
                // 尝试匹配有7个参数的构造函数
                else if (paramTypes.length == 7) {
                    if (paramTypes[0] == String.class && 
                        paramTypes[2] == String[].class &&
                        paramTypes[3] == int.class &&
                        paramTypes[4] == int.class) {
                        
                        session = cons.newInstance(null, null, env, DEFAULT_COLUMNS, DEFAULT_ROWS, homeDir, "UTF-8");
                        break;
                    }
                }
                // 尝试匹配有6个参数的构造函数
                else if (paramTypes.length == 6) {
                    if (paramTypes[0] == String.class && 
                        paramTypes[2] == String[].class &&
                        paramTypes[3] == int.class &&
                        paramTypes[4] == int.class) {
                        
                        session = cons.newInstance(null, null, env, DEFAULT_COLUMNS, DEFAULT_ROWS, homeDir);
                        break;
                    }
                }
            }
            
            if (session == null) {
                // 最后的尝试：使用默认构造函数
                try {
                    session = terminalSessionClass.newInstance();
                } catch (Exception e) {
                    showErrorMessage("无法创建终端会话: " + e.getMessage());
                    return;
                }
            }
            
            terminalSession = session;
            
            // 5. 创建 TerminalView
            Object terminalView = null;
            Constructor<?>[] viewConstructors = terminalViewClass.getConstructors();
            
            for (Constructor<?> cons : viewConstructors) {
                Class<?>[] paramTypes = cons.getParameterTypes();
                if (paramTypes.length == 2 && paramTypes[1] == terminalSessionClass) {
                    terminalView = cons.newInstance(activity, terminalSession);
                    break;
                } else if (paramTypes.length == 1 && paramTypes[0] == Context.class) {
                    terminalView = cons.newInstance(activity);
                    break;
                }
            }
            
            if (terminalView == null) {
                showErrorMessage("无法创建 TerminalView");
                return;
            }
            
            this.terminalView = (View) terminalView;
            this.terminalView.setId(TERMINAL_VIEW_ID);
            
            // 6. 配置终端样式
            try {
                terminalViewClass.getMethod("setTextSize", float.class).invoke(terminalView, 14f);
            } catch (Exception e) {}
            
            try {
                terminalViewClass.getMethod("setKeepScreenOn", boolean.class).invoke(terminalView, true);
            } catch (Exception e) {}
            
            // 设置光标样式
            try {
                terminalViewClass.getMethod("setTerminalCursorStyle", int.class).invoke(terminalView, 2);
            } catch (Exception e) {}
            
            // 7. 添加到布局
            FrameLayout termContainer = activity.findViewById(TERMINAL_VIEW_ID);
            if (termContainer != null) {
                termContainer.removeAllViews();
                this.terminalView.setLayoutParams(new FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.MATCH_PARENT
                ));
                termContainer.addView(this.terminalView);
            }
            
            // 8. 请求焦点并显示键盘
            this.terminalView.requestFocus();
            showSoftKeyboard();
            
            // 9. 启动终端
            try {
                Method onResume = terminalViewClass.getMethod("onResume");
                onResume.invoke(terminalView);
            } catch (Exception e) {}
            
            isInitialized = true;
            
            // 启动 shell
            try {
                Method startShell = terminalSessionClass.getMethod("startShell");
                startShell.invoke(terminalSession);
            } catch (Exception e) {}
            
        } catch (Exception e) {
            e.printStackTrace();
            showErrorMessage("Termux 终端初始化失败: " + e.getMessage());
        }
    }
    
    private void showSoftKeyboard() {
        if (terminalView == null) return;
        
        InputMethodManager imm = (InputMethodManager) activity.getSystemService(Context.INPUT_METHOD_SERVICE);
        if (imm != null) {
            terminalView.requestFocus();
            imm.showSoftInput(terminalView, InputMethodManager.SHOW_IMPLICIT);
        }
    }
    
    private void hideSoftKeyboard() {
        if (terminalView == null) return;
        
        InputMethodManager imm = (InputMethodManager) activity.getSystemService(Context.INPUT_METHOD_SERVICE);
        if (imm != null) {
            imm.hideSoftInputFromWindow(terminalView.getWindowToken(), 0);
        }
    }
    
    private void showErrorMessage(String message) {
        FrameLayout termContainer = activity.findViewById(TERMINAL_VIEW_ID);
        if (termContainer != null) {
            termContainer.removeAllViews();
            TextView errorView = new TextView(activity);
            errorView.setText(message);
            errorView.setTextColor(Color.RED);
            errorView.setTextSize(14f);
            errorView.setGravity(Gravity.CENTER);
            errorView.setLayoutParams(new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            ));
            termContainer.addView(errorView);
        }
    }
    
    public void sendCommand(String command) {
        if (!isInitialized || terminalSession == null) return;
        
        try {
            Method writeMethod = terminalSession.getClass().getMethod("write", byte[].class);
            writeMethod.invoke(terminalSession, (command + "\n").getBytes());
        } catch (Exception e) {
            e.printStackTrace();
        }
    }
    
    public void changeFontSize(boolean increase) {
        if (!isInitialized || terminalView == null) return;
        
        try {
            Method changeFontSize = terminalView.getClass().getMethod("changeFontSize", boolean.class);
            changeFontSize.invoke(terminalView, increase);
        } catch (Exception e) {
            e.printStackTrace();
        }
    }
    
    public void closeTerminal() {
        try {
            if (terminalView != null) {
                try {
                    Method onStop = terminalView.getClass().getMethod("onStop");
                    onStop.invoke(terminalView);
                } catch (Exception e) {}
                
                ViewParent parent = terminalView.getParent();
                if (parent instanceof ViewGroup) {
                    ((ViewGroup) parent).removeView(terminalView);
                }
                terminalView = null;
            }
            
            if (terminalSession != null) {
                try {
                    Method finish = terminalSession.getClass().getMethod("finishIfRunning");
                    finish.invoke(terminalSession);
                } catch (Exception e) {}
                terminalSession = null;
            }
            
            if (rootContainer != null && rootContainer.getParent() != null) {
                ((ViewGroup) rootContainer.getParent()).removeView(rootContainer);
            }
            
            isInitialized = false;
        } catch (Exception e) {
            e.printStackTrace();
        }
    }
    
    public void close() {
        closeTerminal();
    }
    
    public boolean isInitialized() {
        return isInitialized;
    }
    
    public View getTerminalView() {
        return terminalView;
    }
    
    public void showExtraKeys(boolean show) {
        LinearLayout extraKeys = activity != null ? activity.findViewById(EXTRA_KEYS_ID) : null;
        if (extraKeys != null) {
            extraKeys.setVisibility(show ? View.VISIBLE : View.GONE);
        }
    }
}