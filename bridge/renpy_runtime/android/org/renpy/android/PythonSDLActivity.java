package org.renpy.android;

/**
 * Host-side signature shim for the official RAPT PythonSDLActivity JNI hook.
 *
 * The real RAPT Activity remains an asset and is never a manifest entry. This
 * class is intentionally not an Activity: it only lets the native preflight
 * verify the nativeSetEnv callback without creating a second UI owner.
 */
public final class PythonSDLActivity {
    public static final String AETHERKIRI_HOST_SHIM = "renpy-python-host-shim-v1";

    public PythonSDLActivity() {
    }

    public native void nativeSetEnv(String variable, String value);
}
