package org.qtproject.qt5.android;

import android.app.Activity;
import android.app.Service;

/**
 * Minimal stand-in for Qt's own QtNative class.  OpenToonz Mobile does not use
 * a Qt user interface; it only links Qt5Core (needed by the OpenToonz core
 * libraries).  Qt5Core's JNI helpers (QtAndroidPrivate::initJNI) look this
 * class up and register a few natives on it, so it must exist with these
 * members.  The natives are never called by the app.
 */
public class QtNative {
    private static Activity sActivity;
    private static ClassLoader sClassLoader = QtNative.class.getClassLoader();

    public static void setActivity(Activity a) { sActivity = a; }

    public static Activity activity() { return sActivity; }
    public static Service service() { return null; }
    public static ClassLoader classLoader() { return sClassLoader; }
    public static void hideSplashScreen(int duration) {}
    public static void runPendingCppRunnablesOnAndroidThread() {}

    // registered by Qt5Core (RegisterNatives); declared so registration succeeds
    public static native void runPendingCppRunnables();
    public static native boolean dispatchGenericMotionEvent(android.view.MotionEvent ev);
    public static native boolean dispatchKeyEvent(android.view.KeyEvent event);
    public static native void setNativeActivity(Activity activity);
    public static native void setNativeService(Service service);
    public static native void sendRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults);
    public static native boolean updateNativeActivity();
}
