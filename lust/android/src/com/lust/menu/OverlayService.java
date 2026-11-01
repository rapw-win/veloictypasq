package com.lust.menu;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.PixelFormat;
import android.graphics.RectF;
import android.opengl.GLSurfaceView;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.util.DisplayMetrics;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

// Two overlay windows: a full screen transparent GL surface with the menu (touchable only while the menu is open)
// and a long semi-transparent white line at the bottom of the screen that shows / hides the menu.
public class OverlayService extends Service {
    private WindowManager wm;
    private GLSurfaceView gl;
    private LineView line;
    private WindowManager.LayoutParams glp, lp;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private boolean touchable = false;
    private boolean started = false;

    @Override public IBinder onBind(Intent i) { return null; }

    private int dp(float v) { return (int) (v * getResources().getDisplayMetrics().density + 0.5f); }

    @Override public int onStartCommand(Intent intent, int flags, int id) {
        if (intent != null && "stop".equals(intent.getAction())) { stopSelf(); return START_NOT_STICKY; }
        NotificationManager nm = getSystemService(NotificationManager.class);
        nm.createNotificationChannel(new NotificationChannel("ov", "Overlay", NotificationManager.IMPORTANCE_LOW));
        PendingIntent stop = PendingIntent.getService(this, 1, new Intent(this, OverlayService.class).setAction("stop"),
                PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        Notification n = new Notification.Builder(this, "ov").setContentTitle("lust").setContentText("t.me/lustriped")
                .setSmallIcon(R.drawable.icon).setOngoing(true)
                .addAction(new Notification.Action.Builder(null, "Stop", stop).build()).build();
        startForeground(1, n);
        if (!started) {
            started = true;
            Native.setFilesDir(getFilesDir().getAbsolutePath());
            createOverlay();
        }
        return START_NOT_STICKY;
    }

    private void createOverlay() {
        wm = (WindowManager) getSystemService(WINDOW_SERVICE);
        gl = new GLSurfaceView(this);
        gl.setEGLContextClientVersion(3);
        gl.setEGLConfigChooser(8, 8, 8, 8, 0, 0);
        gl.getHolder().setFormat(PixelFormat.TRANSLUCENT);
        gl.setZOrderOnTop(true);
        gl.setPreserveEGLContextOnPause(true);
        gl.setRenderer(new MenuRenderer());
        gl.setRenderMode(GLSurfaceView.RENDERMODE_CONTINUOUSLY);
        gl.setOnTouchListener((v, e) -> {
            int a = e.getActionMasked();
            int act = a == MotionEvent.ACTION_DOWN ? 0 : a == MotionEvent.ACTION_MOVE ? 1
                    : (a == MotionEvent.ACTION_UP || a == MotionEvent.ACTION_CANCEL) ? 2 : -1;
            if (act >= 0) { final float x = e.getX(), y = e.getY(); gl.queueEvent(() -> Native.touch(act, x, y)); }
            return true;
        });

        int type = Build.VERSION.SDK_INT >= 26 ? WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY : WindowManager.LayoutParams.TYPE_PHONE;
        glp = new WindowManager.LayoutParams(WindowManager.LayoutParams.MATCH_PARENT, WindowManager.LayoutParams.MATCH_PARENT, type,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE
                        | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN | WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS
                        | WindowManager.LayoutParams.FLAG_HARDWARE_ACCELERATED,
                PixelFormat.TRANSLUCENT);
        glp.gravity = Gravity.TOP | Gravity.LEFT;
        glp.alpha = 0.8f;   // Android 12+: touches pass through to the app below only for overlays with alpha <= 0.8
        if (Build.VERSION.SDK_INT >= 28) glp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        wm.addView(gl, glp);

        // the line: wide touch strip, the bar itself is drawn thin and translucent
        line = new LineView(this);
        DisplayMetrics dm = getResources().getDisplayMetrics();
        int w = (int) (Math.min(dm.widthPixels, dm.heightPixels) * 0.62f);
        lp = new WindowManager.LayoutParams(w, dp(30), type,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN
                        | WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS,
                PixelFormat.TRANSLUCENT);
        lp.gravity = Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL;
        lp.y = dp(2);
        if (Build.VERSION.SDK_INT >= 28) lp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        line.setOnClickListener(v -> { line.flash(); gl.queueEvent(() -> Native.command(1)); });
        wm.addView(line, lp);
    }

    private final class MenuRenderer implements GLSurfaceView.Renderer {
        private int last = -1;
        @Override public void onSurfaceCreated(GL10 g, EGLConfig c) { Native.init(); }
        @Override public void onSurfaceChanged(GL10 g, int w, int h) { Native.resize(w, h); }
        @Override public void onDrawFrame(GL10 g) {
            int f = Native.frame();
            if (f != last) { last = f; ui.post(new ApplyState(f)); }
        }
    }

    private final class ApplyState implements Runnable {
        private final int state;
        ApplyState(int s) { state = s; }
        @Override public void run() { apply(state); }
    }

    // menu open: the GL overlay takes touches; closed: every touch goes to the app below (the line stays tappable)
    private void apply(int f) {
        if (gl == null) return;
        boolean want = (f & 1) != 0;
        if (want == touchable) return;
        touchable = want;
        if (want) { glp.flags &= ~WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE; glp.alpha = 1.0f; }
        else { glp.flags |= WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE; glp.alpha = 0.8f; }
        wm.updateViewLayout(gl, glp);
    }

    @Override public void onDestroy() {
        if (gl != null) { try { wm.removeView(gl); } catch (Exception ignored) {} gl = null; }
        if (line != null) { try { wm.removeView(line); } catch (Exception ignored) {} line = null; }
        super.onDestroy();
        System.exit(0);   // native menu state is global: start clean next time
    }

    // Long white bar with transparency, like the system home indicator.
    static class LineView extends View {
        private final Paint bar = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint shadow = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final RectF r = new RectF();
        private float pressed = 0.0f;
        private final float d;

        LineView(Context c) {
            super(c);
            d = c.getResources().getDisplayMetrics().density;
            bar.setColor(0xFFFFFFFF);
            shadow.setColor(0x33000000);
        }

        void flash() { pressed = 1.0f; invalidate(); }

        @Override protected void onDraw(Canvas c) {
            float h = 5.0f * d, cy = getHeight() * 0.5f;
            r.set(0, cy - h * 0.5f, getWidth(), cy + h * 0.5f);
            c.drawRoundRect(new RectF(r.left - d, r.top - d, r.right + d, r.bottom + d), h, h, shadow);
            bar.setAlpha((int) (255 * (0.55f + 0.35f * pressed)));
            c.drawRoundRect(r, h * 0.5f, h * 0.5f, bar);
            if (pressed > 0.0f) { pressed = Math.max(0.0f, pressed - 0.08f); postInvalidateOnAnimation(); }
        }
    }
}
