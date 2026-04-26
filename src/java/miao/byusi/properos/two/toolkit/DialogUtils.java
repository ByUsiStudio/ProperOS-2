package miao.byusi.properos.two.toolkit;

import android.app.AlertDialog;
import android.app.ProgressDialog;
import android.content.Context;
import android.content.DialogInterface;
import android.widget.Toast;
import android.app.Activity;

/**
 * 动态弹窗工具类
 * 对应裕语言的 fn tw 和 fn lib 模块
 */
public class DialogUtils {
    
    private static ProgressDialog progressDialog;
    private static AlertDialog alertDialog;
    
    /**
     * 普通提示（短时）
     * 对应 fn tw.tw()
     */
    public static void tw(final Context context, final String msg) {
        if (context != null) {
            try {
                // 尝试获取 Activity
                final Activity activity = getActivityFromContext(context);
                if (activity != null) {
                    activity.runOnUiThread(new Runnable() {
                        @Override
                        public void run() {
                            Toast.makeText(context, msg, Toast.LENGTH_SHORT).show();
                        }
                    });
                } else {
                    Toast.makeText(context, msg, Toast.LENGTH_SHORT).show();
                }
            } catch (Exception e) {
                System.out.println("[TW] 提示失败: " + e.getMessage());
            }
        }
        System.out.println("[TW] " + msg);
    }
    
    /**
     * 长时间提示
     * 对应 fn tw.tw2(msg, duration)
     * duration: 0=短时, 1=长时
     */
    public static void tw2(final Context context, final String msg, final int duration) {
        if (context != null) {
            try {
                final Activity activity = getActivityFromContext(context);
                final int len = (duration == 1) ? Toast.LENGTH_LONG : Toast.LENGTH_SHORT;
                if (activity != null) {
                    activity.runOnUiThread(new Runnable() {
                        @Override
                        public void run() {
                            Toast.makeText(context, msg, len).show();
                        }
                    });
                } else {
                    Toast.makeText(context, msg, len).show();
                }
            } catch (Exception e) {
                System.out.println("[TW2] 提示失败: " + e.getMessage());
            }
        }
        System.out.println("[TW2] " + msg);
    }
    
    /**
     * 加载弹窗（不可取消）
     * 对应 fn lib.loading()
     */
    public static void showLoading(final Context context, final String msg) {
        if (context != null) {
            try {
                final Activity activity = getActivityFromContext(context);
                if (activity != null) {
                    activity.runOnUiThread(new Runnable() {
                        @Override
                        public void run() {
                            if (progressDialog != null && progressDialog.isShowing()) {
                                progressDialog.dismiss();
                            }
                            progressDialog = new ProgressDialog(context);
                            progressDialog.setMessage(msg);
                            progressDialog.setCancelable(false);
                            progressDialog.setCanceledOnTouchOutside(false);
                            progressDialog.show();
                        }
                    });
                }
            } catch (Exception e) {
                System.out.println("[LOADING] 显示失败: " + e.getMessage());
            }
        }
        System.out.println("[LOADING] " + msg);
    }
    
    /**
     * 关闭加载弹窗
     * 对应 endutw()
     */
    public static void dismissLoading(final Context context) {
        if (context != null) {
            try {
                final Activity activity = getActivityFromContext(context);
                if (activity != null) {
                    activity.runOnUiThread(new Runnable() {
                        @Override
                        public void run() {
                            if (progressDialog != null && progressDialog.isShowing()) {
                                progressDialog.dismiss();
                                progressDialog = null;
                            }
                        }
                    });
                }
            } catch (Exception e) {
                System.out.println("[DISMISS] 关闭失败: " + e.getMessage());
            }
        }
    }
    
    /**
     * 确认对话框（双按钮）
     * 对应裕语言的 utw 带确定/取消
     */
    public static void showConfirmDialog(final Context context, 
                                          final String title, 
                                          final String message,
                                          final String confirmText,
                                          final String cancelText,
                                          final OnDialogListener listener) {
        if (context != null) {
            try {
                final Activity activity = getActivityFromContext(context);
                if (activity != null) {
                    activity.runOnUiThread(new Runnable() {
                        @Override
                        public void run() {
                            AlertDialog.Builder builder = new AlertDialog.Builder(context);
                            if (title != null && !title.isEmpty()) {
                                builder.setTitle(title);
                            }
                            builder.setMessage(message);
                            builder.setPositiveButton(confirmText, new DialogInterface.OnClickListener() {
                                @Override
                                public void onClick(DialogInterface dialog, int which) {
                                    dialog.dismiss();
                                    if (listener != null) listener.onConfirm();
                                }
                            });
                            builder.setNegativeButton(cancelText, new DialogInterface.OnClickListener() {
                                @Override
                                public void onClick(DialogInterface dialog, int which) {
                                    dialog.dismiss();
                                    if (listener != null) listener.onCancel();
                                }
                            });
                            builder.setCancelable(false);
                            alertDialog = builder.show();
                        }
                    });
                }
            } catch (Exception e) {
                System.out.println("[DIALOG] 显示失败: " + e.getMessage());
            }
        }
    }
    
    /**
     * 信息对话框（单按钮）
     */
    public static void showInfoDialog(final Context context, 
                                       final String title, 
                                       final String message,
                                       final String buttonText,
                                       final OnDialogListener listener) {
        if (context != null) {
            try {
                final Activity activity = getActivityFromContext(context);
                if (activity != null) {
                    activity.runOnUiThread(new Runnable() {
                        @Override
                        public void run() {
                            AlertDialog.Builder builder = new AlertDialog.Builder(context);
                            if (title != null && !title.isEmpty()) {
                                builder.setTitle(title);
                            }
                            builder.setMessage(message);
                            builder.setPositiveButton(buttonText, new DialogInterface.OnClickListener() {
                                @Override
                                public void onClick(DialogInterface dialog, int which) {
                                    dialog.dismiss();
                                    if (listener != null) listener.onConfirm();
                                }
                            });
                            builder.setCancelable(false);
                            alertDialog = builder.show();
                        }
                    });
                }
            } catch (Exception e) {
                System.out.println("[DIALOG] 显示失败: " + e.getMessage());
            }
        }
    }
    
    /**
     * 从 Context 获取 Activity
     * iApp 环境中的兼容性处理
     */
    private static Activity getActivityFromContext(Context context) {
        if (context == null) {
            return null;
        }
        
        // 如果 context 本身就是 Activity
        if (context instanceof Activity) {
            return (Activity) context;
        }
        
        // 尝试通过反射获取（iApp 环境兼容）
        try {
            Class<?> cls = Class.forName("android.app.Activity");
            if (cls.isInstance(context)) {
                return (Activity) context;
            }
        } catch (Exception e) {
            // 忽略
        }
        
        return null;
    }
    
    /**
     * 对话框监听接口
     */
    public interface OnDialogListener {
        void onConfirm();
        void onCancel();
    }
    
    /**
     * 空实现适配器
     */
    public static abstract class SimpleDialogListener implements OnDialogListener {
        @Override
        public void onCancel() {}
    }
}