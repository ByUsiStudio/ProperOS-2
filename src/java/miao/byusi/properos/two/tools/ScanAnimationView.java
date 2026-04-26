package miao.byusi.properos.two.tools;

import android.animation.ValueAnimator;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Shader;
import android.view.View;
import android.view.animation.LinearInterpolator;

/**
 * 扫描线动画视图
 */
public class ScanAnimationView extends View {
    
    private Paint paint;
    private ValueAnimator animator;
    private float currentY = 0;
    private int viewHeight = 0;
    
    public ScanAnimationView(Context context) {
        super(context);
        init();
    }
    
    private void init() {
        paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    }
    
    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        viewHeight = h;
        
        // 创建渐变画笔
        LinearGradient gradient = new LinearGradient(
                0, 0, 0, h,
                Color.parseColor("#0000FF00"),
                Color.parseColor("#00FF00"),
                Shader.TileMode.CLAMP);
        paint.setShader(gradient);
    }
    
    public void startAnimation() {
        if (animator != null && animator.isRunning()) {
            animator.cancel();
        }
        
        animator = ValueAnimator.ofFloat(0, viewHeight == 0 ? 200 : viewHeight);
        animator.setDuration(2000);
        animator.setRepeatCount(ValueAnimator.INFINITE);
        animator.setRepeatMode(ValueAnimator.RESTART);
        animator.setInterpolator(new LinearInterpolator());
        animator.addUpdateListener(animation -> {
            currentY = (float) animation.getAnimatedValue();
            invalidate();
        });
        animator.start();
    }
    
    public void stopAnimation() {
        if (animator != null && animator.isRunning()) {
            animator.cancel();
        }
    }
    
    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if (currentY > 0 && currentY < getHeight()) {
            canvas.drawRect(0, currentY - dpToPx(2), getWidth(), currentY + dpToPx(2), paint);
        }
    }
    
    private int dpToPx(int dp) {
        float density = getContext().getResources().getDisplayMetrics().density;
        return (int) (dp * density + 0.5f);
    }
}