package org.opentoonz.mobile;

import android.app.Application;

/** Loads and initialises the native OpenToonz engine once per process. */
public class App extends Application {
    private static boolean sEngineReady;

    @Override
    public void onCreate() {
        super.onCreate();
        ensureEngine(this);
    }

    public static synchronized void ensureEngine(Application app) {
        if (sEngineReady) return;
        NativeEngine.load();
        NativeEngine.init(app.getCacheDir().getAbsolutePath(),
                app.getApplicationInfo().nativeLibraryDir);
        sEngineReady = true;
    }
}
