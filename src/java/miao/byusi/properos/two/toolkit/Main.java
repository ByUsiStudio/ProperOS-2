package miao.byusi.properos.two.toolkit;

import android.app.Activity;
import android.content.Context;

import org.json.JSONObject;

import java.util.HashMap;

/**
 * ProperOS 2 安装模块
 * 对应裕语言的 install 模块
 */
public class Main {
    
    // 裕语言交互变量
    private static HashMap<String, Object> s;   // 局部变量
    private static HashMap<String, Object> ss;  // 界面变量
    private static HashMap<String, Object> sss; // 全局变量
    private static Activity activity;
    private static Context context;
    private static Object yuc;
    
    /**
     * 初始化上下文（在载入事件中调用）
     */
    public static void init(Activity act, Context ctx) {
        activity = act;
        context = ctx;
        s = new HashMap<String, Object>();
        ss = new HashMap<String, Object>();
        sss = new HashMap<String, Object>();
    }
    
    /**
     * 安装模块主方法
     * @param jspk 安装包路径
     */
    public static void install(final String jspk) {
        syso("传入路径：" + jspk);
        
        if (jspk != null && !jspk.isEmpty()) {
            final String time = String.valueOf(System.currentTimeMillis());
            final String tempDir = "$temp/" + time;
            
            // 确保临时目录存在
            FileUtils.createDir(tempDir);
            
            // 解压 MainIndex.json
            boolean fuzjspk = FileUtils.unzipSingleFile(jspk, "MainIndex.json", tempDir);
            
            if (fuzjspk) {
                String nr = FileUtils.readTextFile(tempDir + "/MainIndex.json");
                
                if (nr != null && !nr.isEmpty()) {
                    try {
                        JSONObject json = new JSONObject(nr);
                        boolean system = json.optBoolean("system", false);
                        
                        if (system) {
                            installSystemApp(json, tempDir, jspk);
                        } else {
                            installNormalApp(json, tempDir, jspk);
                        }
                    } catch (Exception e) {
                        syso("JSON解析错误: " + e.getMessage());
                        DialogUtils.tw2(context, "安装包解析失败", 1);
                    }
                } else {
                    DialogUtils.tw(context, "主文件读取失败");
                }
            } else {
                DialogUtils.tw(context, "主文件解压失败");
            }
        } else {
            DialogUtils.tw(context, "无 .jspk 文件路径");
        }
    }
    
    /**
     * 安装系统级应用
     */
    private static void installSystemApp(final JSONObject json, final String tempDir, final String jspkPath) {
        try {
            JSONObject sign = json.optJSONObject("sign");
            if (sign == null) {
                DialogUtils.tw2(context, "签名信息缺失", 1);
                return;
            }
            
            JSONObject byusiUserSystem = sign.optJSONObject("byusi");
            if (byusiUserSystem == null) {
                DialogUtils.tw2(context, "用户系统信息缺失", 1);
                return;
            }
            
            final String uuid = byusiUserSystem.optString("uuid", "");
            final String email = byusiUserSystem.optString("email", "");
            
            new Thread(new Runnable() {
                @Override
                public void run() {
                    DialogUtils.showLoading(context, "请求开发者信息中...");
                    
                    String name1 = HttpUtils.httpRequest(
                        "https://api.www.cdifit.cn/user/query.php",
                        "action=query_username&uuid=" + uuid, "utf-8");
                    String name2 = HttpUtils.httpRequest(
                        "https://api.www.cdifit.cn/user/query.php",
                        "action=query_username&email=" + email, "utf-8");
                    
                    DialogUtils.dismissLoading(context);
                    
                    if (name1 != null && name2 != null) {
                        try {
                            JSONObject name1Json = new JSONObject(name1);
                            String status = name1Json.optString("status", "");
                            
                            if ("success".equals(status)) {
                                String username = name1Json.optString("username", "");
                                syso("UserName：" + username);
                                
                                String signUuid = sign.optString("uuid", "");
                                boolean yzfc = FileUtils.copyFile(
                                    tempDir + "/MainIndex.json",
                                    "$ProperOS/home/Desktop/" + signUuid + ".plink", true);
                                
                                if (yzfc) {
                                    String pkg = json.optString("package", "");
                                    DialogUtils.showLoading(context, "安装应用中...");
                                    
                                    boolean fuzs = FileUtils.unzipFull(
                                        jspkPath, "$user/data/software/" + pkg, true);
                                    
                                    DialogUtils.dismissLoading(context);
                                    
                                    if (fuzs) {
                                        DialogUtils.tw(context, "安装完成");
                                    }
                                }
                            } else {
                                DialogUtils.tw2(context, "非法应用无法进行安装", 1);
                            }
                        } catch (Exception e) {
                            syso("解析错误: " + e.getMessage());
                        }
                    } else {
                        DialogUtils.tw2(context, "请联网进行应用安装", 1);
                    }
                }
            }).start();
            
        } catch (Exception e) {
            syso("系统应用安装错误: " + e.getMessage());
        }
    }
    
    /**
     * 安装普通应用（带动态弹窗确认）
     */
    private static void installNormalApp(final JSONObject json, final String tempDir, final String jspkPath) {
        try {
            final String appName = json.optString("name", "");
            final String appPackage = json.optString("package", "");
            JSONObject appSign = json.optJSONObject("sign");
            
            if (appSign == null || appName.isEmpty() || appPackage.isEmpty()) {
                DialogUtils.tw2(context, "错误的安装包", 1);
                return;
            }
            
            final String appUuid = appSign.optString("uuid", "");
            JSONObject userByusi = appSign.optJSONObject("byusi");
            
            if (userByusi == null) {
                DialogUtils.tw2(context, "用户信息缺失", 1);
                return;
            }
            
            final String byusiUuid = userByusi.optString("uuid", "");
            
            new Thread(new Runnable() {
                @Override
                public void run() {
                    DialogUtils.showLoading(context, "请求开发者信息中...");
                    
                    String hd = HttpUtils.httpRequest(
                        "https://api.www.cdifit.cn/user/query.php",
                        "action=query_username&uuid=" + byusiUuid, "utf-8");
                    
                    DialogUtils.dismissLoading(context);
                    
                    if (hd != null) {
                        try {
                            JSONObject hdd = new JSONObject(hd);
                            String status = hdd.optString("status", "");
                            
                            if ("success".equals(status)) {
                                final String username = hdd.optString("username", "");
                                
                                // 动态弹窗确认安装
                                String message = "应用名：" + appName + "\n包名：" + appPackage + 
                                                 "\n开发者：" + username + "\n\n确定安装该应用吗？";
                                
                                DialogUtils.showConfirmDialog(context, null, message, "确定", "取消",
                                    new DialogUtils.OnDialogListener() {
                                        @Override
                                        public void onConfirm() {
                                            performInstall(tempDir, jspkPath, appUuid, appPackage);
                                        }
                                        @Override
                                        public void onCancel() {
                                            syso("用户取消安装");
                                        }
                                    });
                            } else {
                                DialogUtils.tw2(context, "查询失败，非法安装包", 1);
                            }
                        } catch (Exception e) {
                            syso("解析错误: " + e.getMessage());
                        }
                    } else {
                        DialogUtils.tw2(context, "服务端错误", 1);
                    }
                }
            }).start();
            
        } catch (Exception e) {
            syso("普通应用安装错误: " + e.getMessage());
        }
    }
    
    /**
     * 执行安装
     */
    private static void performInstall(String tempDir, String jspkPath, String appUuid, String appPackage) {
        DialogUtils.showLoading(context, "安装应用中...");
        
        boolean yzfc = FileUtils.copyFile(
            tempDir + "/MainIndex.json",
            "$ProperOS/home/Desktop/" + appUuid + ".plink", true);
        boolean fuzs = FileUtils.unzipFull(
            jspkPath, "$user/data/software/" + appPackage, true);
        
        DialogUtils.dismissLoading(context);
        
        if (yzfc && fuzs) {
            DialogUtils.tw(context, "安装成功");
        } else {
            DialogUtils.tw2(context, "安装失败", 1);
        }
    }
    
    /**
     * 打印输出
     */
    private static void syso(String log) {
        System.out.println("[ProperOS] " + log);
    }
}