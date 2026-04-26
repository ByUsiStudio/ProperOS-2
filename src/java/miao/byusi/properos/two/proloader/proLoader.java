package miao.byusi.properos.two.proloader;

import android.app.Activity;
import android.content.Context;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;

import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;
import java.util.HashMap;

/**
 * ProLoader - ProperOS 2 启动加载器
 * 对应裕语言的 ProLoader 启动脚本
 */
public class proLoader {
    
    // 裕语言交互变量
    private static HashMap<String, Object> s;   // 局部变量
    private static HashMap<String, Object> ss;  // 界面变量
    private static HashMap<String, Object> sss; // 全局变量
    private static Activity activity;
    private static Context context;
    private static Object yuc;  // 裕语言对象，用于调用裕语言代码
    
    // 路径常量
    private static final String PATH = "$dev/sboot/stboot.sboot";
    private static final String ALSO_BOOT_PATH = "$temp/module";
    private static final String PROLOADER_CONFIG_DATA_PATH = "$System/Config/ProLoader/config.json";
    
    // 默认配置数据
    private static final String CONFIG_DATA = "{\"enable_supan\":false,\"enable_strict_perm\":true,\"enable_install_dev\":true,\"enable_unlock_proldr\":false,\"enable_console\":false,\"enable_mod_stboot\":true,\"boot\":{\"sleep\":3000,\"icon\":\"@logo/byusi.png\",\"background\":\"#ffffffff\",\"stboot\":\"stboot.iyu\",\"ui\":\"nvw(512,2,\\\"图像\\\",\\\"width=-2\\\\nheight=-2\\\")\\nus(512,\\\"src\\\",\\\"&[ICON]\\\")\"}}";
    
    // Handler 用于 UI 线程操作
    private static Handler mainHandler = new Handler(Looper.getMainLooper());
    
    /**
     * 初始化上下文和变量映射
     */
    public static void init(Activity act, Context ctx, 
                            HashMap<String, Object> s_map,
                            HashMap<String, Object> ss_map,
                            HashMap<String, Object> sss_map,
                            Object yuc_obj) {
        activity = act;
        context = ctx;
        s = s_map;
        ss = ss_map;
        sss = sss_map;
        yuc = yuc_obj;
        syso("[INFO] proLoader 初始化完成");
    }
    
    /**
     * 初始化（简化版）
     */
    public static void init(Activity act, Context ctx, Object yuc_obj) {
        activity = act;
        context = ctx;
        s = new HashMap<String, Object>();
        ss = new HashMap<String, Object>();
        sss = new HashMap<String, Object>();
        yuc = yuc_obj;
        syso("[INFO] proLoader 初始化完成（使用默认变量映射）");
    }
    
    /**
     * 主启动方法 - 对应裕语言的主逻辑
     */
    public static void start() {
        syso("[INFO] ProLoader 启动");
        
        // 转换路径
        String configPath = convertPath(PROLOADER_CONFIG_DATA_PATH);
        boolean yzPlcdp = fileExists(configPath);
        String plcdpStr = readFile(configPath);
        JSONObject plcdpObj = null;
        
        // 解析 JSON
        if (plcdpStr != null && !plcdpStr.isEmpty()) {
            try {
                plcdpObj = new JSONObject(plcdpStr);
            } catch (Exception e) {
                syso("[ERROR] 解析配置 JSON 失败: " + e.getMessage());
            }
        }
        
        // 如果配置文件存在且解析成功
        if (yzPlcdp && plcdpObj != null) {
            try {
                // 读取配置项
                boolean enableSupan = plcdpObj.optBoolean("enable_supan", false);
                boolean enableStrictPerm = plcdpObj.optBoolean("enable_strict_perm", true);
                boolean enableInstallDev = plcdpObj.optBoolean("enable_install_dev", true);
                boolean enableUnlockProldr = plcdpObj.optBoolean("enable_unlock_proldr", false);
                boolean enableConsole = plcdpObj.optBoolean("enable_console", false);
                boolean enableModStboot = plcdpObj.optBoolean("enable_mod_stboot", true);
                
                // 存入全局变量
                if (sss != null) {
                    sss.put("enable_supan", enableSupan);
                    sss.put("enable_strict_perm", enableStrictPerm);
                    sss.put("enable_install_dev", enableInstallDev);
                    sss.put("enable_unlock_proldr", enableUnlockProldr);
                    sss.put("enable_console", enableConsole);
                    sss.put("enable_mod_stboot", enableModStboot);
                }
                
                // 读取 boot 配置
                JSONObject boot = plcdpObj.optJSONObject("boot");
                if (boot != null) {
                    String stboot = boot.optString("stboot", "stboot.iyu");
                    if (sss != null) {
                        sss.put("stboot", stboot);
                    }
                    
                    // 继续执行启动流程
                    executeBoot(boot);
                } else {
                    syso("[ERROR] boot 配置不存在");
                    createDefaultConfig();
                }
                
            } catch (Exception e) {
                syso("[ERROR] 读取配置失败: " + e.getMessage());
                createDefaultConfig();
            }
        } else {
            // 创建默认配置文件
            createDefaultConfig();
        }
        
        // 打印配置对象
        if (plcdpObj != null) {
            syso("[INFO] 配置对象: " + plcdpObj.toString());
        } else {
            syso("[INFO] 配置对象为 null");
        }
    }
    
    /**
     * 执行启动流程
     */
    private static void executeBoot(JSONObject boot) {
        String path = convertPath(PATH);
        String alsoBootPath = convertPath(ALSO_BOOT_PATH);
        
        boolean verificationRequired = fileExists(path);
        String abp = readFile(alsoBootPath);
        
        // 判断 also_boot_path 是否为空
        if (abp == null || abp.isEmpty()) {
            // 写入空文件
            writeFile(alsoBootPath, "");
            
            // 如果 path 不存在
            if (!verificationRequired) {
                // 读取系统启动脚本
                String stbootCodePath = convertPath("@System/BootUp/stboot.sboot");
                String stbootCode = readFile(stbootCodePath);
                if (stbootCode != null) {
                    writeFile(path, stbootCode);
                }
            }
            
            // 获取 boot 配置
            final String icon = boot.optString("icon", "@logo/byusi.png");
            final int sleep = boot.optInt("sleep", 3000);
            final String background = boot.optString("background", "#ffffffff");
            final String ui = boot.optString("ui", "");
            final String stboot = boot.optString("stboot", "stboot.iyu");
            
            // 新线程执行 UI 初始化
            new Thread(new Runnable() {
                @Override
                public void run() {
                    // 在 UI 线程更新界面
                    runOnUiThread(new Runnable() {
                        @Override
                        public void run() {
                            // 替换图标占位符
                            String finalUi = ui.replace("%[ICON]%", icon);
                            
                            // 调用 fn system.code(ui, 7)
                            callSystemCode(finalUi, 7);
                            
                            // 设置背景 us(2, "background", background)
                            setBackground(2, background);
                        }
                    });
                    
                    // 延迟执行
                    try {
                        Thread.sleep(sleep);
                    } catch (InterruptedException e) {
                        e.printStackTrace();
                    }
                    
                    // 跳转到启动界面 fn core.uigo(stboot)
                    runOnUiThread(new Runnable() {
                        @Override
                        public void run() {
                            callCoreUigo(stboot);
                        }
                    });
                }
            }).start();
            
        } else {
            // 跳转到模块启动界面 fn core.uigo(abp, 6)
            callCoreUigo(abp, 6);
            writeFile(alsoBootPath, "");
            end();
        }
    }
    
    /**
     * 调用裕语言的 fn system.code(ui, type)
     * @param ui UI代码字符串
     * @param type 类型参数
     */
    private static void callSystemCode(String ui, int type) {
        if (yuc == null) {
            syso("[ERROR] yuc 对象为 null，无法调用 fn system.code");
            return;
        }
        
        try {
            // 构建裕语言代码
            String yuCode = "fn system.code(\"" + escapeString(ui) + "\", " + type + ")";
            syso("[INFO] 调用 fn system.code: " + yuCode);
            
            // 通过反射调用裕语言的 yu 方法
            Class<?> yuClass = Class.forName("androidx.Yu3Java");
            java.lang.reflect.Method yuMethod = yuClass.getMethod("yu", Object.class, String.class);
            Object result = yuMethod.invoke(null, yuc, yuCode);
            
            if (result != null) {
                syso("[INFO] fn system.code 返回: " + result.toString());
            }
        } catch (Exception e) {
            syso("[ERROR] 调用 fn system.code 失败: " + e.getMessage());
            e.printStackTrace();
        }
    }
    
    /**
     * 调用裕语言的 fn core.uigo(uiFile)
     * @param uiFile 界面文件名
     */
    private static void callCoreUigo(String uiFile) {
        if (yuc == null) {
            syso("[ERROR] yuc 对象为 null，无法调用 fn core.uigo");
            return;
        }
        
        try {
            // 构建裕语言代码
            String yuCode = "fn core.uigo(\"" + escapeString(uiFile) + "\")";
            syso("[INFO] 调用 fn core.uigo: " + yuCode);
            
            // 通过反射调用裕语言的 yu 方法
            Class<?> yuClass = Class.forName("androidx.Yu3Java");
            java.lang.reflect.Method yuMethod = yuClass.getMethod("yu", Object.class, String.class);
            Object result = yuMethod.invoke(null, yuc, yuCode);
            
            if (result != null) {
                syso("[INFO] fn core.uigo 返回: " + result.toString());
            }
        } catch (Exception e) {
            syso("[ERROR] 调用 fn core.uigo 失败: " + e.getMessage());
            e.printStackTrace();
        }
    }
    
    /**
     * 调用裕语言的 fn core.uigo(uiFile, animType)
     * @param uiFile 界面文件名
     * @param animType 动画类型
     */
    private static void callCoreUigo(String uiFile, int animType) {
        if (yuc == null) {
            syso("[ERROR] yuc 对象为 null，无法调用 fn core.uigo");
            return;
        }
        
        try {
            // 构建裕语言代码
            String yuCode = "fn core.uigo(\"" + escapeString(uiFile) + "\", " + animType + ")";
            syso("[INFO] 调用 fn core.uigo: " + yuCode);
            
            // 通过反射调用裕语言的 yu 方法
            Class<?> yuClass = Class.forName("androidx.Yu3Java");
            java.lang.reflect.Method yuMethod = yuClass.getMethod("yu", Object.class, String.class);
            Object result = yuMethod.invoke(null, yuc, yuCode);
            
            if (result != null) {
                syso("[INFO] fn core.uigo 返回: " + result.toString());
            }
        } catch (Exception e) {
            syso("[ERROR] 调用 fn core.uigo 失败: " + e.getMessage());
            e.printStackTrace();
        }
    }
    
    /**
     * 设置控件背景
     * @param viewId 控件ID
     * @param background 背景颜色或图片
     */
    private static void setBackground(int viewId, String background) {
        if (yuc == null) {
            syso("[ERROR] yuc 对象为 null，无法设置背景");
            return;
        }
        
        try {
            // 构建裕语言代码
            String yuCode = "us(" + viewId + ", \"background\", \"" + escapeString(background) + "\")";
            syso("[INFO] 设置背景: " + yuCode);
            
            // 通过反射调用裕语言的 yu 方法
            Class<?> yuClass = Class.forName("androidx.Yu3Java");
            java.lang.reflect.Method yuMethod = yuClass.getMethod("yu", Object.class, String.class);
            Object result = yuMethod.invoke(null, yuc, yuCode);
            
            if (result != null) {
                syso("[INFO] 设置背景返回: " + result.toString());
            }
        } catch (Exception e) {
            syso("[ERROR] 设置背景失败: " + e.getMessage());
            e.printStackTrace();
        }
    }
    
    /**
     * 创建默认配置文件
     */
    private static void createDefaultConfig() {
        String configPath = convertPath(PROLOADER_CONFIG_DATA_PATH);
        boolean result = writeFile(configPath, CONFIG_DATA);
        if (result) {
            syso("[INFO] 基本配置初始化成功");
        } else {
            syso("[ERROR] 基本配置初始化失败");
        }
        
        // 调用重启方法 call(null,"mjava","restart.st",activity)
        callRestart();
    }
    
    /**
     * 调用裕语言重启方法
     */
    private static void callRestart() {
        if (yuc == null) {
            syso("[ERROR] yuc 对象为 null，无法调用重启");
            return;
        }
        
        try {
            // 构建裕语言代码 - 调用 mjava 模块的 restart.st 方法
            String yuCode = "call(null, \"mjava\", \"restart.st\", activity)";
            syso("[INFO] 调用重启: " + yuCode);
            
            // 通过反射调用裕语言的 yu 方法
            Class<?> yuClass = Class.forName("androidx.Yu3Java");
            java.lang.reflect.Method yuMethod = yuClass.getMethod("yu", Object.class, String.class);
            Object result = yuMethod.invoke(null, yuc, yuCode);
            
            if (result != null) {
                syso("[INFO] 重启调用返回: " + result.toString());
            }
        } catch (Exception e) {
            syso("[ERROR] 调用重启失败: " + e.getMessage());
            e.printStackTrace();
        }
    }
    
    /**
     * 结束当前界面
     */
    private static void end() {
        if (yuc == null) {
            syso("[ERROR] yuc 对象为 null，无法结束界面");
            return;
        }
        
        try {
            // 构建裕语言代码
            String yuCode = "end()";
            syso("[INFO] 结束界面: " + yuCode);
            
            // 通过反射调用裕语言的 yu 方法
            Class<?> yuClass = Class.forName("androidx.Yu3Java");
            java.lang.reflect.Method yuMethod = yuClass.getMethod("yu", Object.class, String.class);
            Object result = yuMethod.invoke(null, yuc, yuCode);
            
            if (result != null) {
                syso("[INFO] 结束界面返回: " + result.toString());
            }
        } catch (Exception e) {
            syso("[ERROR] 结束界面失败: " + e.getMessage());
            e.printStackTrace();
        }
    }
    
    /**
     * 转义字符串中的特殊字符
     */
    private static String escapeString(String str) {
        if (str == null) return "";
        return str.replace("\\", "\\\\")
                  .replace("\"", "\\\"")
                  .replace("\n", "\\n")
                  .replace("\r", "\\r")
                  .replace("\t", "\\t");
    }
    
    /**
     * 检查文件是否存在
     */
    private static boolean fileExists(String filePath) {
        if (filePath == null) return false;
        File file = new File(filePath);
        return file.exists();
    }
    
    /**
     * 读取文件内容
     */
    private static String readFile(String filePath) {
        if (filePath == null) return null;
        File file = new File(filePath);
        if (!file.exists()) return null;
        
        BufferedReader reader = null;
        try {
            reader = new BufferedReader(new FileReader(file));
            StringBuilder content = new StringBuilder();
            String line;
            while ((line = reader.readLine()) != null) {
                content.append(line);
            }
            return content.toString();
        } catch (Exception e) {
            syso("[ERROR] 读取文件失败: " + e.getMessage());
            return null;
        } finally {
            if (reader != null) {
                try { reader.close(); } catch (IOException e) { e.printStackTrace(); }
            }
        }
    }
    
    /**
     * 写入文件内容
     */
    private static boolean writeFile(String filePath, String content) {
        if (filePath == null || content == null) return false;
        
        File file = new File(filePath);
        FileWriter writer = null;
        try {
            File parent = file.getParentFile();
            if (parent != null && !parent.exists()) {
                parent.mkdirs();
            }
            writer = new FileWriter(file);
            writer.write(content);
            syso("[INFO] 写入文件成功: " + filePath);
            return true;
        } catch (Exception e) {
            syso("[ERROR] 写入文件失败: " + e.getMessage());
            return false;
        } finally {
            if (writer != null) {
                try { writer.close(); } catch (IOException e) { e.printStackTrace(); }
            }
        }
    }
    
    /**
     * 路径转换
     * $ -> 应用私有目录
     * % -> SD卡目录
     * @ -> 应用私有目录
     */
    private static String convertPath(String path) {
        if (path == null) return null;
        
        String sdCard = Environment.getExternalStorageDirectory().getAbsolutePath();
        String privateDir = context != null ? context.getFilesDir().getAbsolutePath() : "";
        
        if (path.startsWith("%")) {
            return path.replace("%", sdCard + "/");
        } else if (path.startsWith("@")) {
            return privateDir + "/" + path.substring(1);
        } else if (path.startsWith("$")) {
            return privateDir + "/" + path.substring(1);
        }
        
        return path;
    }
    
    /**
     * 在 UI 线程执行
     */
    private static void runOnUiThread(Runnable action) {
        if (activity != null) {
            activity.runOnUiThread(action);
        } else if (mainHandler != null) {
            mainHandler.post(action);
        } else {
            action.run();
        }
    }
    
    /**
     * 打印日志
     */
    private static void syso(String log) {
        System.out.println("[ProLoader] " + log);
    }
}