package miao.byusi.properos.two.tools;

import android.content.Context;
import android.hardware.Camera;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

import java.io.IOException;

/**
 * 相机预览视图
 */
public class CameraPreview extends SurfaceView implements SurfaceHolder.Callback {
    
    private Camera camera;
    private SurfaceHolder holder;
    private PreviewFrameCallback frameCallback;
    
    public CameraPreview(Context context) {
        super(context);
        holder = getHolder();
        holder.addCallback(this);
        holder.setType(SurfaceHolder.SURFACE_TYPE_PUSH_BUFFERS);
    }
    
    public void setCamera(Camera camera) {
        this.camera = camera;
        if (camera != null) {
            startPreview();
        }
    }
    
    public void setPreviewCallback(PreviewFrameCallback callback) {
        this.frameCallback = callback;
        if (camera != null && callback != null) {
            camera.setPreviewCallback((data, cam) -> {
                if (frameCallback != null) {
                    Camera.Size size = cam.getParameters().getPreviewSize();
                    frameCallback.onPreviewFrame(data, size.width, size.height);
                }
            });
        }
    }
    
    private void startPreview() {
        if (camera != null && holder.getSurface() != null) {
            try {
                camera.setPreviewDisplay(holder);
                setupCameraParameters();
                camera.startPreview();
            } catch (IOException e) {
                e.printStackTrace();
            }
        }
    }
    
    private void setupCameraParameters() {
        if (camera == null) return;
        
        Camera.Parameters params = camera.getParameters();
        
        // 设置预览尺寸
        Camera.Size bestSize = getOptimalPreviewSize(
                params.getSupportedPreviewSizes(), 
                getWidth(), getHeight());
        if (bestSize != null) {
            params.setPreviewSize(bestSize.width, bestSize.height);
        }
        
        // 设置对焦模式
        if (params.getSupportedFocusModes().contains(Camera.Parameters.FOCUS_MODE_CONTINUOUS_PICTURE)) {
            params.setFocusMode(Camera.Parameters.FOCUS_MODE_CONTINUOUS_PICTURE);
        } else if (params.getSupportedFocusModes().contains(Camera.Parameters.FOCUS_MODE_AUTO)) {
            params.setFocusMode(Camera.Parameters.FOCUS_MODE_AUTO);
        }
        
        camera.setParameters(params);
    }
    
    private Camera.Size getOptimalPreviewSize(java.util.List<Camera.Size> sizes, int w, int h) {
        final double ASPECT_TOLERANCE = 0.1;
        double targetRatio = (double) w / h;
        
        Camera.Size optimalSize = null;
        double minDiff = Double.MAX_VALUE;
        
        for (Camera.Size size : sizes) {
            double ratio = (double) size.width / size.height;
            if (Math.abs(ratio - targetRatio) > ASPECT_TOLERANCE) continue;
            
            if (Math.abs(size.height - h) < minDiff) {
                optimalSize = size;
                minDiff = Math.abs(size.height - h);
            }
        }
        
        if (optimalSize == null) {
            minDiff = Double.MAX_VALUE;
            for (Camera.Size size : sizes) {
                if (Math.abs(size.height - h) < minDiff) {
                    optimalSize = size;
                    minDiff = Math.abs(size.height - h);
                }
            }
        }
        
        return optimalSize;
    }
    
    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        startPreview();
    }
    
    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        if (camera != null) {
            camera.stopPreview();
            startPreview();
        }
    }
    
    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        // 释放资源
    }
    
    public interface PreviewFrameCallback {
        void onPreviewFrame(byte[] data, int width, int height);
    }
}