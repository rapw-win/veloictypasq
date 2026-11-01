package com.lust.menu;

public class Native {
    static { System.loadLibrary("lust"); }
    public static native void setFilesDir(String dir);               // configs / scripts folder
    public static native void init();                                // GL thread, surface created
    public static native void resize(int w, int h);
    public static native int frame();                                // bit0: menu open or animating
    public static native void touch(int action, float x, float y);   // 0 down, 1 move, 2 up
    public static native void command(int cmd);                      // 1 show / hide the menu
    public static native int bindButtons(float[] out);               // on-screen bind buttons: x, y, w, h each
    public static native void bindTouch(int index, int down);        // finger on / off a bind button
}
