// 文件路径: miao/byusi/properos/two/tools/scanTheQRcode.java
package miao.byusi.properos.two.tools;

import android.animation.Animator;
import android.animation.AnimatorListenerAdapter;
import android.animation.ValueAnimator;
import android.app.Activity;
import android.content.Context;
import android.content.pm.PackageManager;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.PorterDuff;
import android.graphics.PorterDuffXfermode;
import android.graphics.Rect;
import android.graphics.RectF;
import android.hardware.Camera;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.RelativeLayout;
import android.widget.TextView;
import android.widget.Toast;

import com.google.zxing.BinaryBitmap;
import com.google.zxing.LuminanceSource;
import com.google.zxing.MultiFormatReader;
import com.google.zxing.NotFoundException;
import com.google.zxing.PlanarYUVLuminanceSource;
import com.google.zxing.Result;
import com.google.zxing.common.HybridBinarizer;

import java.io.IOException;

/**
 * 微信风格二维码扫描组件
 * 完全内存扫描，不保存图片
 */
public class scanTheQRcode {
    
    private Activity activity;
    private Object yuc;
    private Context context;
    
    // 相机相关
    private Camera camera;
    private CameraPreview cameraPreview;
    private int cameraId = Camera.CameraInfo.CAMERA_FACING_BACK;
    
    // 界面组件
    private FrameLayout scanContainer;
    private View scanOverlay;
    private ScanAnimationView scanLineView;
    private View maskView;
    
    // 扫描区域
    private Rect scanRect;
    private int scanAreaSize = 280; // dp单位
    private int scanAreaMarginTop = -1; // dp单位，-1表示自动居中
    
    // 扫描回调
    private ScanResultCallback resultCallback;
    private boolean isScanning = false;
    private Handler scanHandler;
    private Runnable scanRunnable;
    
    // 闪光灯状态
    private boolean torchEnabled = false;
    
    // 权限请求码
    private static final int REQUEST_CAMERA_PERMISSION = 1001;
    
    public scanTheQRcode(Activity activity, Object yuc) {
        this.activity = activity;
        this.yuc = yuc;
        this.context = activity.getApplicationContext();
        this.scanHandler = new Handler(Looper.getMainLooper());
    }
    
    /**
     * 设置扫描区域大小（单位：dp）
     */
    public scanTheQRcode setScanAreaSize(int sizeDp) {
        this.scanAreaSize = sizeDp;
        return this;
    }
    
    /**
     * 设置扫描区域顶部边距（单位：dp，-1表示自动居中）
     */
    public scanTheQRcode setScanAreaMarginTop(int marginTopDp) {
        this.scanAreaMarginTop = marginTopDp;
        return this;
    }
    
    /**
     * 设置扫描回调
     */
    public scanTheQRcode setCallback(ScanResultCallback callback) {
        this.resultCallback = callback;
        return this;
    }
    
    /**
     * 开始扫描
     */
    public void startScan() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            if (activity.checkSelfPermission(android.Manifest.permission.CAMERA) 
                    != PackageManager.PERMISSION_GRANTED) {
                activity.requestPermissions(new String[]{android.Manifest.permission.CAMERA}, 
                        REQUEST_CAMERA_PERMISSION);
                return;
            }
        }
        initScanUI();
        openCamera();
    }
    
    /**
     * 初始化扫描界面
     */
    private void initScanUI() {
        // 创建主容器
        scanContainer = new FrameLayout(activity);
        scanContainer.setLayoutParams(new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
        scanContainer.setBackgroundColor(Color.BLACK);
        
        // 添加相机预览
        cameraPreview = new CameraPreview(activity);
        scanContainer.addView(cameraPreview, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
        
        // 添加遮罩层
        maskView = createMaskView();
        scanContainer.addView(maskView);
        
        // 添加扫描边框和动画
        addScanBorderAndAnimation();
        
        // 添加底部按钮栏
        addBottomBar();
        
        // 添加顶部标题栏
        addTopBar();
        
        // 添加到Activity
        activity.addContentView(scanContainer, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
    }
    
    /**
     * 创建遮罩层（四周半透明，中间透明）
     */
    private View createMaskView() {
        return new View(activity) {
            private Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
            private Paint clearPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
            private int screenWidth;
            private int screenHeight;
            private int scanTop;
            private int scanBottom;
            private int scanLeft;
            private int scanRight;
            
            {
                clearPaint.setXfermode(new PorterDuffXfermode(PorterDuff.Mode.CLEAR));
                setLayerType(View.LAYER_TYPE_SOFTWARE, null);
            }
            
            @Override
            protected void onSizeChanged(int w, int h, int oldw, int oldh) {
                super.onSizeChanged(w, h, oldw, oldh);
                screenWidth = w;
                screenHeight = h;
                
                int sizePx = dpToPx(scanAreaSize);
                int marginTopPx;
                if (scanAreaMarginTop != -1) {
                    marginTopPx = dpToPx(scanAreaMarginTop);
                } else {
                    marginTopPx = (screenHeight - sizePx) / 3;
                }
                
                scanTop = marginTopPx;
                scanBottom = scanTop + sizePx;
                scanLeft = (screenWidth - sizePx) / 2;
                scanRight = scanLeft + sizePx;
                
                scanRect = new Rect(scanLeft, scanTop, scanRight, scanBottom);
            }
            
            @Override
            protected void onDraw(Canvas canvas) {
                super.onDraw(canvas);
                
                // 绘制半透明遮罩
                paint.setColor(Color.parseColor("#99000000"));
                canvas.drawRect(0, 0, screenWidth, scanTop, paint);
                canvas.drawRect(0, scanBottom, screenWidth, screenHeight, paint);
                canvas.drawRect(0, scanTop, scanLeft, scanBottom, paint);
                canvas.drawRect(scanRight, scanTop, screenWidth, scanBottom, paint);
                
                // 绘制扫描边框
                drawScanBorder(canvas, scanLeft, scanTop, scanRight, scanBottom);
            }
            
            private void drawScanBorder(Canvas canvas, int left, int top, int right, int bottom) {
                Paint borderPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
                borderPaint.setColor(Color.parseColor("#00FF00"));
                borderPaint.setStrokeWidth(dpToPx(2));
                borderPaint.setStyle(Paint.Style.STROKE);
                
                int cornerLength = dpToPx(25);
                int lineWidth = dpToPx(4);
                
                Paint cornerPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
                cornerPaint.setColor(Color.parseColor("#00FF00"));
                cornerPaint.setStrokeWidth(lineWidth);
                cornerPaint.setStyle(Paint.Style.STROKE);
                
                // 左上角
                canvas.drawLine(left, top + cornerLength, left, top, cornerPaint);
                canvas.drawLine(left, top, left + cornerLength, top, cornerPaint);
                
                // 右上角
                canvas.drawLine(right - cornerLength, top, right, top, cornerPaint);
                canvas.drawLine(right, top, right, top + cornerLength, cornerPaint);
                
                // 左下角
                canvas.drawLine(left, bottom - cornerLength, left, bottom, cornerPaint);
                canvas.drawLine(left, bottom, left + cornerLength, bottom, cornerPaint);
                
                // 右下角
                canvas.drawLine(right - cornerLength, bottom, right, bottom, cornerPaint);
                canvas.drawLine(right, bottom - cornerLength, right, bottom, cornerPaint);
            }
        };
    }
    
    /**
     * 添加扫描边框和动画
     */
    private void addScanBorderAndAnimation() {
        RelativeLayout borderContainer = new RelativeLayout(activity);
        FrameLayout.LayoutParams containerParams = new FrameLayout.LayoutParams(
                dpToPx(scanAreaSize), dpToPx(scanAreaSize));
        if (scanAreaMarginTop != -1) {
            containerParams.topMargin = dpToPx(scanAreaMarginTop);
        } else {
            containerParams.topMargin = (getScreenHeight() - dpToPx(scanAreaSize)) / 3;
        }
        containerParams.leftMargin = (getScreenWidth() - dpToPx(scanAreaSize)) / 2;
        borderContainer.setLayoutParams(containerParams);
        
        // 创建扫描线动画
        scanLineView = new ScanAnimationView(activity);
        RelativeLayout.LayoutParams lineParams = new RelativeLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dpToPx(2));
        lineParams.addRule(RelativeLayout.ALIGN_PARENT_TOP);
        scanLineView.setLayoutParams(lineParams);
        borderContainer.addView(scanLineView);
        
        // 添加四个角的装饰
        addCornerDecorations(borderContainer);
        
        scanContainer.addView(borderContainer);
        
        // 开始扫描线动画
        scanLineView.startAnimation();
    }
    
    /**
     * 添加四角装饰
     */
    private void addCornerDecorations(RelativeLayout container) {
        int cornerSize = dpToPx(20);
        int lineWidth = dpToPx(3);
        
        // 左上角
        View cornerTL = new View(activity);
        RelativeLayout.LayoutParams tlParams = new RelativeLayout.LayoutParams(cornerSize, cornerSize);
        tlParams.addRule(RelativeLayout.ALIGN_PARENT_TOP);
        tlParams.addRule(RelativeLayout.ALIGN_PARENT_LEFT);
        cornerTL.setLayoutParams(tlParams);
        cornerTL.setBackground(createCornerDrawable(true, true, lineWidth));
        container.addView(cornerTL);
        
        // 右上角
        View cornerTR = new View(activity);
        RelativeLayout.LayoutParams trParams = new RelativeLayout.LayoutParams(cornerSize, cornerSize);
        trParams.addRule(RelativeLayout.ALIGN_PARENT_TOP);
        trParams.addRule(RelativeLayout.ALIGN_PARENT_RIGHT);
        cornerTR.setLayoutParams(trParams);
        cornerTR.setBackground(createCornerDrawable(true, false, lineWidth));
        container.addView(cornerTR);
        
        // 左下角
        View cornerBL = new View(activity);
        RelativeLayout.LayoutParams blParams = new RelativeLayout.LayoutParams(cornerSize, cornerSize);
        blParams.addRule(RelativeLayout.ALIGN_PARENT_BOTTOM);
        blParams.addRule(RelativeLayout.ALIGN_PARENT_LEFT);
        cornerBL.setLayoutParams(blParams);
        cornerBL.setBackground(createCornerDrawable(false, true, lineWidth));
        container.addView(cornerBL);
        
        // 右下角
        View cornerBR = new View(activity);
        RelativeLayout.LayoutParams brParams = new RelativeLayout.LayoutParams(cornerSize, cornerSize);
        brParams.addRule(RelativeLayout.ALIGN_PARENT_BOTTOM);
        brParams.addRule(RelativeLayout.ALIGN_PARENT_RIGHT);
        cornerBR.setLayoutParams(brParams);
        cornerBR.setBackground(createCornerDrawable(false, false, lineWidth));
        container.addView(cornerBR);
    }
    
    /**
     * 创建角标背景
     */
    private android.graphics.drawable.Drawable createCornerDrawable(boolean isTop, boolean isLeft, int lineWidth) {
        android.graphics.drawable.GradientDrawable drawable = new android.graphics.drawable.GradientDrawable();
        drawable.setColor(Color.TRANSPARENT);
        drawable.setStroke(lineWidth, Color.parseColor("#00FF00"));
        
        float[] radii = new float[8];
        if (isTop && isLeft) {
            radii[0] = radii[1] = dpToPx(8);
        } else if (isTop && !isLeft) {
            radii[2] = radii[3] = dpToPx(8);
        } else if (!isTop && isLeft) {
            radii[4] = radii[5] = dpToPx(8);
        } else {
            radii[6] = radii[7] = dpToPx(8);
        }
        drawable.setCornerRadii(radii);
        
        return drawable;
    }
    
    /**
     * 添加顶部标题栏
     */
    private void addTopBar() {
        RelativeLayout topBar = new RelativeLayout(activity);
        FrameLayout.LayoutParams topParams = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dpToPx(88));
        topBar.setLayoutParams(topParams);
        topBar.setBackgroundColor(Color.parseColor("#CC000000"));
        
        // 返回按钮
        TextView backBtn = new TextView(activity);
        backBtn.setText("←");
        backBtn.setTextSize(28);
        backBtn.setTextColor(Color.WHITE);
        backBtn.setPadding(dpToPx(16), dpToPx(16), dpToPx(16), dpToPx(16));
        RelativeLayout.LayoutParams backParams = new RelativeLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        backParams.addRule(RelativeLayout.ALIGN_PARENT_LEFT);
        backParams.addRule(RelativeLayout.CENTER_VERTICAL);
        backBtn.setLayoutParams(backParams);
        backBtn.setOnClickListener(v -> closeScan());
        topBar.addView(backBtn);
        
        // 标题
        TextView title = new TextView(activity);
        title.setText("扫一扫");
        title.setTextSize(18);
        title.setTextColor(Color.WHITE);
        RelativeLayout.LayoutParams titleParams = new RelativeLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        titleParams.addRule(RelativeLayout.CENTER_IN_PARENT);
        title.setLayoutParams(titleParams);
        topBar.addView(title);
        
        // 相册按钮（可选，如果需要从相册识别）
        TextView albumBtn = new TextView(activity);
        albumBtn.setText("相册");
        albumBtn.setTextSize(16);
        albumBtn.setTextColor(Color.parseColor("#00FF00"));
        albumBtn.setPadding(dpToPx(16), dpToPx(16), dpToPx(16), dpToPx(16));
        RelativeLayout.LayoutParams albumParams = new RelativeLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        albumParams.addRule(RelativeLayout.ALIGN_PARENT_RIGHT);
        albumParams.addRule(RelativeLayout.CENTER_VERTICAL);
        albumBtn.setLayoutParams(albumParams);
        albumBtn.setOnClickListener(v -> {
            // 可选：从相册选择图片识别
            Toast.makeText(activity, "请使用相机扫描", Toast.LENGTH_SHORT).show();
        });
        topBar.addView(albumBtn);
        
        scanContainer.addView(topBar);
    }
    
    /**
     * 添加底部按钮栏
     */
    private void addBottomBar() {
        LinearLayout bottomBar = new LinearLayout(activity);
        bottomBar.setOrientation(LinearLayout.HORIZONTAL);
        bottomBar.setGravity(Gravity.CENTER);
        bottomBar.setBackgroundColor(Color.parseColor("#CC000000"));
        FrameLayout.LayoutParams bottomParams = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dpToPx(100));
        bottomParams.gravity = Gravity.BOTTOM;
        bottomBar.setLayoutParams(bottomParams);
        bottomBar.setPadding(dpToPx(20), dpToPx(10), dpToPx(20), dpToPx(30));
        
        // 闪光灯按钮
        LinearLayout torchLayout = createBottomButton("🔦", "开灯");
        torchLayout.setOnClickListener(v -> toggleTorch());
        bottomBar.addView(torchLayout, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1));
        
        // 我的二维码按钮（可选）
        LinearLayout myQrLayout = createBottomButton("📱", "我的二维码");
        myQrLayout.setOnClickListener(v -> {
            Toast.makeText(activity, "我的二维码功能", Toast.LENGTH_SHORT).show();
        });
        bottomBar.addView(myQrLayout, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1));
        
        scanContainer.addView(bottomBar);
    }
    
    /**
     * 创建底部按钮
     */
    private LinearLayout createBottomButton(String icon, String text) {
        LinearLayout layout = new LinearLayout(activity);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setGravity(Gravity.CENTER);
        
        TextView iconView = new TextView(activity);
        iconView.setText(icon);
        iconView.setTextSize(24);
        iconView.setTextColor(Color.WHITE);
        layout.addView(iconView);
        
        TextView textView = new TextView(activity);
        textView.setText(text);
        textView.setTextSize(12);
        textView.setTextColor(Color.parseColor("#CCCCCC"));
        layout.addView(textView);
        
        return layout;
    }
    
    /**
     * 打开相机
     */
    private void openCamera() {
        try {
            camera = Camera.open(cameraId);
            if (camera != null && cameraPreview != null) {
                cameraPreview.setCamera(camera);
                startFrameScan();
            }
        } catch (Exception e) {
            e.printStackTrace();
            if (resultCallback != null) {
                resultCallback.onError("相机打开失败: " + e.getMessage());
            }
            closeScan();
        }
    }
    
    /**
     * 开始帧扫描
     */
    private void startFrameScan() {
        isScanning = true;
        if (cameraPreview != null) {
            cameraPreview.setPreviewCallback(new CameraPreview.PreviewFrameCallback() {
                @Override
                public void onPreviewFrame(byte[] data, int width, int height) {
                    if (!isScanning || scanRect == null) return;
                    scanQRCode(data, width, height);
                }
            });
        }
    }
    
    /**
     * 扫描二维码（内存扫描，不保存图片）
     */
    private void scanQRCode(byte[] data, int width, int height) {
        try {
            // 计算扫描区域在预览帧中的位置
            int previewWidth = width;
            int previewHeight = height;
            
            // 获取扫描区域的实际像素位置
            int left = (int) ((float) scanRect.left / getScreenWidth() * previewWidth);
            int top = (int) ((float) scanRect.top / getScreenHeight() * previewHeight);
            int right = (int) ((float) scanRect.right / getScreenWidth() * previewWidth);
            int bottom = (int) ((float) scanRect.bottom / getScreenHeight() * previewHeight);
            
            int scanWidth = right - left;
            int scanHeight = bottom - top;
            
            if (scanWidth <= 0 || scanHeight <= 0) return;
            
            // 提取扫描区域的YUV数据
            byte[] scanData = extractScanAreaData(data, width, height, left, top, scanWidth, scanHeight);
            
            // 创建LuminanceSource
            PlanarYUVLuminanceSource source = new PlanarYUVLuminanceSource(
                    scanData, scanWidth, scanHeight, 0, 0, scanWidth, scanHeight, false);
            
            BinaryBitmap bitmap = new BinaryBitmap(new HybridBinarizer(source));
            MultiFormatReader reader = new MultiFormatReader();
            
            Result result = reader.decode(bitmap);
            String qrText = result.getText();
            
            if (qrText != null && !qrText.isEmpty()) {
                onScanSuccess(qrText);
            }
            
        } catch (NotFoundException e) {
            // 未找到二维码，继续扫描
        } catch (Exception e) {
            e.printStackTrace();
        }
    }
    
    /**
     * 提取扫描区域的YUV数据
     */
    private byte[] extractScanAreaData(byte[] data, int width, int height, 
                                        int left, int top, int scanWidth, int scanHeight) {
        byte[] scanData = new byte[scanWidth * scanHeight];
        for (int y = 0; y < scanHeight; y++) {
            int srcY = top + y;
            if (srcY >= height) break;
            System.arraycopy(data, srcY * width + left, scanData, y * scanWidth, scanWidth);
        }
        return scanData;
    }
    
    /**
     * 扫描成功回调
     */
    private void onScanSuccess(String result) {
        if (!isScanning) return;
        
        isScanning = false;
        
        // 播放成功动画
        if (scanLineView != null) {
            scanLineView.stopAnimation();
        }
        
        // 震动提示
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            android.os.VibrationEffect effect = android.os.VibrationEffect.createOneShot(100, 100);
            android.os.Vibrator vibrator = (android.os.Vibrator) context.getSystemService(Context.VIBRATOR_SERVICE);
            if (vibrator != null) vibrator.vibrate(effect);
        }
        
        // 回调结果
        if (resultCallback != null) {
            resultCallback.onSuccess(result);
        }
        
        // 延迟关闭扫描界面
        scanHandler.postDelayed(() -> {
            closeScan();
            if (resultCallback != null) {
                resultCallback.onComplete(result);
            }
        }, 500);
    }
    
    /**
     * 切换闪光灯
     */
    private void toggleTorch() {
        if (camera == null) return;
        
        torchEnabled = !torchEnabled;
        Camera.Parameters params = camera.getParameters();
        
        if (torchEnabled) {
            params.setFlashMode(Camera.Parameters.FLASH_MODE_TORCH);
        } else {
            params.setFlashMode(Camera.Parameters.FLASH_MODE_OFF);
        }
        
        camera.setParameters(params);
        
        // 更新按钮文字
        Toast.makeText(activity, torchEnabled ? "闪光灯已开启" : "闪光灯已关闭", Toast.LENGTH_SHORT).show();
    }
    
    /**
     * 关闭扫描界面
     */
    public void closeScan() {
        isScanning = false;
        
        if (scanLineView != null) {
            scanLineView.stopAnimation();
        }
        
        if (camera != null) {
            try {
                if (cameraPreview != null) {
                    cameraPreview.setPreviewCallback(null);
                }
                camera.setPreviewCallback(null);
                camera.stopPreview();
                camera.release();
            } catch (Exception e) {
                e.printStackTrace();
            }
            camera = null;
        }
        
        if (scanContainer != null) {
            try {
                ViewGroup parent = (ViewGroup) scanContainer.getParent();
                if (parent != null) {
                    parent.removeView(scanContainer);
                }
            } catch (Exception e) {
                e.printStackTrace();
            }
        }
    }
    
    private int dpToPx(int dp) {
        float density = context.getResources().getDisplayMetrics().density;
        return (int) (dp * density + 0.5f);
    }
    
    private int getScreenWidth() {
        return context.getResources().getDisplayMetrics().widthPixels;
    }
    
    private int getScreenHeight() {
        return context.getResources().getDisplayMetrics().heightPixels;
    }
    
    /**
     * 扫描结果回调接口
     */
    public interface ScanResultCallback {
        void onSuccess(String result);
        void onError(String error);
        void onComplete(String result);
    }
}