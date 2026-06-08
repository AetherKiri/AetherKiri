package org.github.krkr2.aetherkiri;

import android.app.Activity;
import android.app.ActivityManager;
import android.app.AlertDialog;
import android.content.DialogInterface;
import android.content.Context;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.util.Log;
import android.view.Surface;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import android.widget.FrameLayout;

import java.io.File;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.Locale;

public final class EngineBridge {
    private static final String TAG = "AetherKiriBridge";
    private static final Handler UI_HANDLER = new Handler(Looper.getMainLooper());

    private Context applicationContext;

    public EngineBridge(Context context) {
        applicationContext = context == null ? null : context.getApplicationContext();
        if (applicationContext != null) {
            nativeSetApplicationContext(applicationContext);
        }
    }

    public void setApplicationContext(Context context) {
        applicationContext = context == null ? null : context.getApplicationContext();
        if (applicationContext != null) {
            nativeSetApplicationContext(applicationContext);
        }
    }

    public void setSurface(Surface surface, int width, int height) {
        nativeSetSurface(surface, width, height);
    }

    public void detachSurface() {
        nativeDetachSurface();
    }

    public static void updateMemoryInfo() {
    }

    public static long getAvailMemory() {
        Context context = currentApplication();
        ActivityManager manager = context == null ? null
                : (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);
        if (manager == null) {
            return 0L;
        }
        ActivityManager.MemoryInfo info = new ActivityManager.MemoryInfo();
        manager.getMemoryInfo(info);
        return info.availMem;
    }

    public static long getUsedMemory() {
        Context context = currentApplication();
        ActivityManager manager = context == null ? null
                : (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);
        if (manager == null) {
            return 0L;
        }
        ActivityManager.MemoryInfo info = new ActivityManager.MemoryInfo();
        manager.getMemoryInfo(info);
        return info.totalMem - info.availMem;
    }

    public static String getDeviceId() {
        Context context = currentApplication();
        if (context == null) {
            return "";
        }
        String id = Settings.Secure.getString(
                context.getContentResolver(), Settings.Secure.ANDROID_ID);
        return id == null ? "" : id;
    }

    public static String GetVersion() {
        Context context = currentApplication();
        if (context == null) {
            return "";
        }
        try {
            if (Build.VERSION.SDK_INT >= 33) {
                return String.valueOf(context.getPackageManager()
                        .getPackageInfo(context.getPackageName(),
                                android.content.pm.PackageManager.PackageInfoFlags.of(0))
                        .versionName);
            }
            return String.valueOf(context.getPackageManager()
                    .getPackageInfo(context.getPackageName(), 0)
                    .versionName);
        } catch (Throwable error) {
            Log.w(TAG, "GetVersion failed", error);
            return "";
        }
    }

    public static String getLocaleName() {
        return Locale.getDefault().getLanguage();
    }

    public static String[] getStoragePath() {
        Context context = currentApplication();
        if (context == null) {
            return new String[0];
        }
        ArrayList<String> paths = new ArrayList<>();
        File[] externalFiles = context.getExternalFilesDirs(null);
        if (externalFiles != null) {
            for (File file : externalFiles) {
                if (file != null) {
                    paths.add(file.getAbsolutePath());
                }
            }
        }
        File internal = context.getFilesDir();
        if (internal != null) {
            paths.add(internal.getAbsolutePath());
        }
        return paths.toArray(new String[0]);
    }

    public static boolean isWritableNormalOrSaf(String path) {
        try {
            File file = new File(path);
            if (!file.exists() && !file.mkdirs()) {
                return false;
            }
            return file.canWrite();
        } catch (Throwable error) {
            Log.w(TAG, "isWritableNormalOrSaf failed", error);
            return false;
        }
    }

    public static boolean CreateFolders(String path) {
        try {
            File file = new File(path);
            return file.isDirectory() || file.mkdirs();
        } catch (Throwable error) {
            Log.w(TAG, "CreateFolders failed", error);
            return false;
        }
    }

    public static boolean WriteFile(String path, byte[] data) {
        try {
            java.io.FileOutputStream output = new java.io.FileOutputStream(path);
            try {
                output.write(data);
            } finally {
                output.close();
            }
            return true;
        } catch (Throwable error) {
            Log.w(TAG, "WriteFile failed", error);
            return false;
        }
    }

    public static boolean DeleteFile(String path) {
        try {
            return new File(path).delete();
        } catch (Throwable error) {
            Log.w(TAG, "DeleteFile failed", error);
            return false;
        }
    }

    public static boolean RenameFile(String from, String to) {
        try {
            return new File(from).renameTo(new File(to));
        } catch (Throwable error) {
            Log.w(TAG, "RenameFile failed", error);
            return false;
        }
    }

    public static void MessageController(int adType, int arg1, int arg2) {
        Log.d(TAG, "MessageController ignored: " + adType + " " + arg1 + " " + arg2);
    }

    public static void exit() {
        Activity activity = resolveActivity();
        if (activity != null) {
            activity.finish();
        }
    }

    public static void ShowMessageBox(final String title, final String message,
                                      final String[] buttons) {
        final String[] safeButtons = normalizeButtons(buttons);
        UI_HANDLER.post(new Runnable() {
            @Override
            public void run() {
                Activity activity = resolveActivity();
                if (activity == null) {
                    Log.w(TAG, "ShowMessageBox: no Activity, auto-confirming");
                    onMessageBoxResult(0);
                    return;
                }

                try {
                    AlertDialog.Builder builder = new AlertDialog.Builder(activity)
                            .setTitle(title == null ? "" : title)
                            .setMessage(message == null ? "" : message)
                            .setCancelable(false);
                    addButtons(builder, safeButtons);
                    builder.show();
                } catch (Throwable error) {
                    Log.e(TAG, "ShowMessageBox failed", error);
                    onMessageBoxResult(0);
                }
            }
        });
    }

    public static void ShowInputBox(final String title, final String prompt,
                                    final String initText,
                                    final String[] buttons) {
        final String[] safeButtons = normalizeButtons(buttons);
        UI_HANDLER.post(new Runnable() {
            @Override
            public void run() {
                Activity activity = resolveActivity();
                if (activity == null) {
                    Log.w(TAG, "ShowInputBox: no Activity, auto-confirming");
                    onInputBoxResult(0, initText == null ? "" : initText);
                    return;
                }

                try {
                    final EditText editText = new EditText(activity);
                    editText.setSingleLine(false);
                    editText.setText(initText == null ? "" : initText);
                    editText.setSelection(editText.getText().length());

                    int padding = Math.round(24.0f * activity.getResources()
                            .getDisplayMetrics().density);
                    FrameLayout container = new FrameLayout(activity);
                    container.setPadding(padding, 0, padding, 0);
                    container.addView(editText, new FrameLayout.LayoutParams(
                            FrameLayout.LayoutParams.MATCH_PARENT,
                            FrameLayout.LayoutParams.WRAP_CONTENT));

                    AlertDialog.Builder builder = new AlertDialog.Builder(activity)
                            .setTitle(title == null ? "" : title)
                            .setMessage(prompt == null ? "" : prompt)
                            .setView(container)
                            .setCancelable(false);
                    addInputButtons(builder, safeButtons, editText);
                    AlertDialog dialog = builder.show();
                    editText.requestFocus();
                    if (dialog.getWindow() != null) {
                        dialog.getWindow().setSoftInputMode(
                                android.view.WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE);
                    }
                    InputMethodManager imm = (InputMethodManager) activity.getSystemService(
                            Context.INPUT_METHOD_SERVICE);
                    if (imm != null) {
                        imm.showSoftInput(editText, InputMethodManager.SHOW_IMPLICIT);
                    }
                } catch (Throwable error) {
                    Log.e(TAG, "ShowInputBox failed", error);
                    onInputBoxResult(0, initText == null ? "" : initText);
                }
            }
        });
    }

    public static void hideTextInput() {
        Activity activity = resolveActivity();
        if (activity == null || activity.getCurrentFocus() == null) {
            return;
        }
        InputMethodManager imm = (InputMethodManager) activity.getSystemService(
                Context.INPUT_METHOD_SERVICE);
        if (imm != null) {
            imm.hideSoftInputFromWindow(activity.getCurrentFocus().getWindowToken(), 0);
        }
    }

    public static void showTextInput(int x, int y, int width, int height) {
        Activity activity = resolveActivity();
        if (activity == null || activity.getCurrentFocus() == null) {
            return;
        }
        InputMethodManager imm = (InputMethodManager) activity.getSystemService(
                Context.INPUT_METHOD_SERVICE);
        if (imm != null) {
            imm.showSoftInput(activity.getCurrentFocus(), InputMethodManager.SHOW_IMPLICIT);
        }
    }

    private static String[] normalizeButtons(String[] buttons) {
        if (buttons == null || buttons.length == 0) {
            return new String[]{"OK"};
        }
        return buttons;
    }

    private static void addButtons(AlertDialog.Builder builder, String[] buttons) {
        builder.setPositiveButton(buttons[0], new DialogInterface.OnClickListener() {
            @Override
            public void onClick(DialogInterface dialog, int which) {
                onMessageBoxResult(0);
            }
        });
        if (buttons.length >= 2) {
            builder.setNeutralButton(buttons[1], new DialogInterface.OnClickListener() {
                @Override
                public void onClick(DialogInterface dialog, int which) {
                    onMessageBoxResult(1);
                }
            });
        }
        if (buttons.length >= 3) {
            builder.setNegativeButton(buttons[2], new DialogInterface.OnClickListener() {
                @Override
                public void onClick(DialogInterface dialog, int which) {
                    onMessageBoxResult(2);
                }
            });
        }
    }

    private static void addInputButtons(AlertDialog.Builder builder, String[] buttons,
                                        EditText editText) {
        builder.setPositiveButton(buttons[0], new DialogInterface.OnClickListener() {
            @Override
            public void onClick(DialogInterface dialog, int which) {
                onInputBoxResult(0, editText.getText().toString());
            }
        });
        if (buttons.length >= 2) {
            builder.setNeutralButton(buttons[1], new DialogInterface.OnClickListener() {
                @Override
                public void onClick(DialogInterface dialog, int which) {
                    onInputBoxResult(1, editText.getText().toString());
                }
            });
        }
        if (buttons.length >= 3) {
            builder.setNegativeButton(buttons[2], new DialogInterface.OnClickListener() {
                @Override
                public void onClick(DialogInterface dialog, int which) {
                    onInputBoxResult(2, editText.getText().toString());
                }
            });
        }
    }

    private static Activity resolveActivity() {
        Context app = currentApplication();
        if (app instanceof Activity) {
            return (Activity) app;
        }
        if (app == null) {
            return null;
        }

        try {
            Class<?> godotClass = Class.forName("org.godotengine.godot.Godot");
            Method getInstance = godotClass.getMethod("getInstance", Context.class);
            Object godot = getInstance.invoke(null, app);
            if (godot == null) {
                return null;
            }
            Method getActivity = godotClass.getMethod("getActivity");
            Object activity = getActivity.invoke(godot);
            return activity instanceof Activity ? (Activity) activity : null;
        } catch (Throwable error) {
            Log.w(TAG, "resolveActivity failed", error);
            return null;
        }
    }

    private static Context currentApplication() {
        try {
            Class<?> activityThread = Class.forName("android.app.ActivityThread");
            Method currentApplication = activityThread.getMethod("currentApplication");
            Object app = currentApplication.invoke(null);
            return app instanceof Context ? (Context) app : null;
        } catch (Throwable error) {
            Log.w(TAG, "currentApplication failed", error);
            return null;
        }
    }

    private static void onMessageBoxResult(int result) {
        try {
            nativeOnMessageBoxResult(result);
        } catch (UnsatisfiedLinkError error) {
            Log.e(TAG, "nativeOnMessageBoxResult missing", error);
        }
    }

    private static void onInputBoxResult(int result, String text) {
        try {
            nativeOnInputBoxResult(result, text == null ? "" : text);
        } catch (UnsatisfiedLinkError error) {
            Log.e(TAG, "nativeOnInputBoxResult missing", error);
        }
    }

    private native void nativeSetSurface(Surface surface, int width, int height);
    private native void nativeDetachSurface();
    private native void nativeSetApplicationContext(Context context);
    private static native void nativeOnMessageBoxResult(int result);
    private static native void nativeOnInputBoxResult(int result, String text);
}
