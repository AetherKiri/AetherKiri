package org.libsdl.app;

import android.app.Activity;
import android.content.Context;
import android.view.Surface;
import java.lang.ref.WeakReference;

/**
 * AetherKiri's host-owned RAPT callback shim.
 *
 * This is deliberately not an Activity and is never declared in the Android
 * manifest. The official SDLActivity source remains available under
 * assets/renpy_mobile/rapt for inspection, while this class only supplies the
 * JNI callback signatures that librenpython resolves during preflight. A
 * future lifecycle bridge may implement these calls against the existing
 * Godot Activity and SurfaceView; this shim must not launch a second Activity.
 */
public final class SDLActivity {
    public static final String AETHERKIRI_HOST_SHIM = "renpy-sdl-host-shim-v1";

    private static WeakReference<Activity> hostActivity = new WeakReference<>(null);

    private SDLActivity() {
    }

    public static void bindHostActivity(Activity activity) {
        hostActivity = new WeakReference<>(activity);
    }

    public static void clearHostActivity() {
        hostActivity = new WeakReference<>(null);
    }

    public static Activity getHostActivity() {
        return hostActivity.get();
    }

    /** Returns the Surface already bound by EngineBridge, never a new view. */
    public static native Surface getNativeSurface();

    /** Returns the existing Application Context held by EngineBridge. */
    public static native Context getContext();

    public static native int nativeSetupJNI();
    public static native int nativeRunMain(String library, String function,
                                           Object arguments);
    public static native void nativeLowMemory();
    public static native void nativeSendQuit();
    public static native void nativeQuit();
    public static native void nativePause();
    public static native void nativeResume();
    public static native void nativeFocusChanged(boolean hasFocus);
    public static native void onNativeDropFile(String filename);
    public static native void nativeSetScreenResolution(int surfaceWidth,
                                                        int surfaceHeight,
                                                        int deviceWidth,
                                                        int deviceHeight,
                                                        float rate);
    public static native void onNativeResize();
    public static native void onNativeKeyDown(int keycode);
    public static native void onNativeKeyUp(int keycode);
    public static native boolean onNativeSoftReturnKey();
    public static native void onNativeKeyboardFocusLost();
    public static native void nativeSetenv(String name, String value);
    public static native void onNativeTouch(int touchDevId,
                                            int pointerFingerId,
                                            int action,
                                            float x,
                                            float y,
                                            float pressure);
    public static native void onNativeMouse(int button,
                                            int action,
                                            float x,
                                            float y,
                                            boolean relative);
    public static native void onNativeAccel(float x, float y, float z);
    public static native void onNativeClipboardChanged();
    public static native void onNativeSurfaceCreated();
    public static native void onNativeSurfaceChanged();
    public static native void onNativeSurfaceDestroyed();
    public static native String nativeGetHint(String name);
    public static native boolean nativeGetHintBoolean(String name,
                                                      boolean defaultValue);
    public static native void onNativeOrientationChanged(int orientation);
    public static native void nativeAddTouch(int touchId, String name);
    public static native void nativePermissionResult(int requestCode,
                                                     boolean result);
    public static native void onNativeLocaleChanged();
}
