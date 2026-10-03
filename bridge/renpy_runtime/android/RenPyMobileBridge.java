package org.github.krkr2.aetherkiri;

import android.app.Activity;
import android.content.Context;

import org.libsdl.app.SDLActivity;

import java.io.File;

/**
 * Host-owned handoff point for the staged Ren'Py Android support package.
 *
 * The Godot Activity remains the only Activity. A future lifecycle adapter may
 * call bindHostActivity(this) from that Activity and then drive Ren'Py through
 * the existing surface. This class intentionally does not reference or launch
 * org.renpy.android.PythonSDLActivity.
 */
public final class RenPyMobileBridge {
    private static boolean nativeBridgeLoaded;

    static {
        nativeBridgeLoaded = tryLoadNativeBridge();
    }

    private RenPyMobileBridge() {
    }

    /** Bind the already-running Godot Activity; never constructs an Activity. */
    public static void bindHostActivity(Activity activity) {
        if (activity == null) {
            throw new IllegalArgumentException("host Activity must not be null");
        }
        ensureNativeBridge();
        SDLActivity.bindHostActivity(activity);
        nativeSetHostActivity(activity);
    }

    /** Clear the host Activity during destruction/configuration teardown. */
    public static void clearHostActivity() {
        if (nativeBridgeLoaded || tryLoadNativeBridge()) {
            nativeSetHostActivity(null);
        }
        SDLActivity.clearHostActivity();
    }

    /** Returns true when the native bridge has loaded in the host process. */
    public static boolean isNativeBridgeLoaded() {
        return nativeBridgeLoaded;
    }

    /**
     * Package smoke helper. It only checks that the staged arm64 payload is in
     * the APK's native library directory; loading it is deliberately deferred
     * until SDL/lifecycle/surface isolation is implemented.
     */
    public static boolean hasBundledRenPy(Context context) {
        if (context == null || context.getApplicationInfo() == null) {
            return false;
        }
        String nativeDir = context.getApplicationInfo().nativeLibraryDir;
        return nativeDir != null && new File(nativeDir, "librenpython.so").isFile();
    }

    private static void ensureNativeBridge() {
        if (!nativeBridgeLoaded && !tryLoadNativeBridge()) {
            throw new IllegalStateException("AetherKiri engine_api JNI bridge is unavailable");
        }
    }

    private static synchronized boolean tryLoadNativeBridge() {
        if (nativeBridgeLoaded) {
            return true;
        }
        try {
            System.loadLibrary("engine_api");
            nativeBridgeLoaded = true;
        } catch (LinkageError ignored) {
            // Godot may load this class before the extension. Retry from
            // ensureNativeBridge once the host has finished loading libraries.
        }
        return nativeBridgeLoaded;
    }

    private static native void nativeSetHostActivity(Activity activity);
}
