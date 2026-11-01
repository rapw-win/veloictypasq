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

import java.util.ArrayList;
import java.util.Arrays;

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
            int delay = intent != null ? intent.getIntExtra("delay_ms", 0) : 0;
            if (delay > 0) ui.postDelayed(new Runnable() { @Override public void run() { if (started) createOverlay(); } }, delay);
            else createOverlay();
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
        private final float[] rects = new float[64 * 4];
        private float[] lastRects = new float[0];
        @Override public void onSurfaceCreated(GL10 g, EGLConfig c) { Native.init(); }
        @Override public void onSurfaceChanged(GL10 g, int w, int h) { Native.resize(w, h); }
        @Override public void onDrawFrame(GL10 g) {
            int f = Native.frame();
            if (f != last) { last = f; ui.post(new ApplyState(f)); }
            int n = Native.bindButtons(rects);
            float[] now = Arrays.copyOf(rects, n * 4);
            if (!Arrays.equals(now, lastRects)) { lastRects = now; ui.post(new SyncBindButtons(now)); }
        }
    }

    // One small transparent touch window per on-screen bind button (ImGui draws the button itself).
    private final ArrayList<View> bindViews = new ArrayList<>();
    private final ArrayList<WindowManager.LayoutParams> bindParams = new ArrayList<>();

    private final class SyncBindButtons implements Runnable {
        private final float[] r;
        SyncBindButtons(float[] rects) { r = rects; }
        @Override public void run() {
            if (wm == null || gl == null) return;
            int n = r.length / 4;
            while (bindViews.size() > n) {
                View v = bindViews.remove(bindViews.size() - 1);
                bindParams.remove(bindParams.size() - 1);
                try { wm.removeView(v); } catch (Exception ignored) {}
            }
            for (int i = 0; i < n; ++i) {
                WindowManager.LayoutParams p;
                if (i >= bindViews.size()) {
                    p = new WindowManager.LayoutParams(1, 1, overlayType(),
                            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN
                                    | WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS,
                            PixelFormat.TRANSLUCENT);
                    p.gravity = Gravity.TOP | Gravity.LEFT;
                    if (Build.VERSION.SDK_INT >= 28) p.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
                    View v = new View(OverlayService.this);
                    v.setOnTouchListener(new BindTouch(i));
                    p.x = (int) r[i * 4]; p.y = (int) r[i * 4 + 1];
                    p.width = Math.max(1, (int) r[i * 4 + 2]); p.height = Math.max(1, (int) r[i * 4 + 3]);
                    wm.addView(v, p);
                    bindViews.add(v); bindParams.add(p);
                    continue;
                }
                p = bindParams.get(i);
                p.x = (int) r[i * 4]; p.y = (int) r[i * 4 + 1];
                p.width = Math.max(1, (int) r[i * 4 + 2]); p.height = Math.max(1, (int) r[i * 4 + 3]);
                wm.updateViewLayout(bindViews.get(i), p);
            }
        }
    }

    private final class BindTouch implements View.OnTouchListener {
        private final int index;
        BindTouch(int i) { index = i; }
        @Override public boolean onTouch(View v, MotionEvent e) {
            int a = e.getActionMasked();
            if (a == MotionEvent.ACTION_DOWN) gl.queueEvent(new BindEvent(index, 1));
            else if (a == MotionEvent.ACTION_UP || a == MotionEvent.ACTION_CANCEL) gl.queueEvent(new BindEvent(index, 0));
            return true;
        }
    }

    private static final class BindEvent implements Runnable {
        private final int index, down;
        BindEvent(int i, int d) { index = i; down = d; }
        @Override public void run() { Native.bindTouch(index, down); }
    }

    private static int overlayType() {
        return Build.VERSION.SDK_INT >= 26 ? WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY : WindowManager.LayoutParams.TYPE_PHONE;
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
        started = false;
        ui.removeCallbacksAndMessages(null);
        if (gl != null) { try { wm.removeView(gl); } catch (Exception ignored) {} gl = null; }
        if (line != null) { try { wm.removeView(line); } catch (Exception ignored) {} line = null; }
        for (View v : bindViews) { try { wm.removeView(v); } catch (Exception ignored) {} }
        bindViews.clear(); bindParams.clear();
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
