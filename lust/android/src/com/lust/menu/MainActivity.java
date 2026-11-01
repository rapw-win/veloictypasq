package com.lust.menu;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.view.Gravity;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

// Start screen: one "Start" button. It asks for "Display over other apps" first, then launches the overlay
// (the menu appears right away; the white line at the bottom of the screen shows / hides it).
public class MainActivity extends Activity {
    private boolean waitingForPermission = false;

    private int dp(float v) { return (int) (v * getResources().getDisplayMetrics().density + 0.5f); }

    @Override protected void onCreate(Bundle s) {
        super.onCreate(s);
        getWindow().setStatusBarColor(0xFF15161C);
        getWindow().setNavigationBarColor(0xFF15161C);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER);
        GradientDrawable bg = new GradientDrawable(GradientDrawable.Orientation.TL_BR, new int[] { 0xFF23242E, 0xFF15161C });
        root.setBackground(bg);

        ImageView logo = new ImageView(this);
        logo.setImageResource(R.drawable.icon);
        root.addView(logo, new LinearLayout.LayoutParams(dp(84), dp(84)));

        TextView title = new TextView(this);
        title.setText("lust");
        title.setTextColor(Color.WHITE);
        title.setTextSize(30);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        title.setGravity(Gravity.CENTER);
        title.setPadding(0, dp(10), 0, 0);
        root.addView(title);

        TextView channel = new TextView(this);
        channel.setText("t.me/lustriped");
        channel.setTextColor(0xFFFF9A8A);
        channel.setTextSize(14);
        channel.setGravity(Gravity.CENTER);
        channel.setPadding(0, dp(2), 0, dp(26));
        root.addView(channel);

        Button start = new Button(this);
        start.setText("Start");
        start.setAllCaps(false);
        start.setTextColor(Color.WHITE);
        start.setTextSize(17);
        start.setTypeface(Typeface.DEFAULT_BOLD);
        GradientDrawable g = new GradientDrawable(GradientDrawable.Orientation.LEFT_RIGHT, new int[] { 0xFFE8656B, 0xFFF2A15E });
        g.setCornerRadius(dp(12));
        start.setBackground(g);
        start.setStateListAnimator(null);
        root.addView(start, new LinearLayout.LayoutParams(dp(240), dp(52)));
        start.setOnClickListener(v -> onStartPressed());

        TextView hint = new TextView(this);
        hint.setText("After Start the menu opens over other apps.\nTap the white line at the bottom of the screen to show / hide it.");
        hint.setTextColor(0xFF8B8E9A);
        hint.setTextSize(12);
        hint.setGravity(Gravity.CENTER);
        hint.setPadding(dp(24), dp(18), dp(24), 0);
        root.addView(hint);

        setContentView(root);
    }

    private void onStartPressed() {
        if (Settings.canDrawOverlays(this)) { launch(); return; }
        waitingForPermission = true;
        Toast.makeText(this, "Allow \"Display over other apps\" for lust", Toast.LENGTH_LONG).show();
        startActivity(new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION, Uri.parse("package:" + getPackageName())));
    }

    @Override protected void onResume() {
        super.onResume();
        if (waitingForPermission && Settings.canDrawOverlays(this)) {   // permission was just granted: open the menu
            waitingForPermission = false;
            launch();
        }
    }

    private void launch() {
        Intent i = new Intent(this, OverlayService.class);
        if (Build.VERSION.SDK_INT >= 26) startForegroundService(i); else startService(i);
        moveTaskToBack(true);
    }
}
