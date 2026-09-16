/*
 * FormulsAndroid.java
 *
 * The Java half of the Android platform layer. Every method is static and is
 * called from C++ (src/app/Source/AndroidPlatform.cpp) through JNI, always on
 * the app's main thread, which on Android is also JUCE's message thread.
 *
 * Nothing here holds formuls logic; it only reaches the Android APIs that
 * JUCE does not wrap: services, the WebView, app-specific storage, the media
 * store, and the notification permission.
 */

package net.formuls.android;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.ComponentName;
import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.os.IBinder;
import android.os.Process;
import android.os.SystemClock;
import android.provider.MediaStore;
import android.util.Log;
import android.view.View;

import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.List;

public final class FormulsAndroid
{
    private static final String TAG = "formuls";

    /** Must match OpenStageControlProcess::guiPort in the C++ code. */
    static final int GUI_PORT = 9001;

    private static final String GUI_PROCESS_SUFFIX = ":gui";

    private FormulsAndroid() {}

    //==========================================================================
    // Engine foreground service (see FormulsAudioService)

    public static void startAudioService (Context context)
    {
        Context app = context.getApplicationContext();
        Intent intent = new Intent (app, FormulsAudioService.class);

        try
        {
            if (Build.VERSION.SDK_INT >= 26)
                app.startForegroundService (intent);
            else
                app.startService (intent);
        }
        catch (RuntimeException e)
        {
            // Not fatal: formuls still runs, it is just more likely to be
            // stopped by the system while in the background.
            Log.w (TAG, "could not start the foreground service", e);
        }
    }

    public static void stopAudioService (Context context)
    {
        Context app = context.getApplicationContext();
        app.stopService (new Intent (app, FormulsAudioService.class));
    }

    //==========================================================================
    // GUI server (see GuiServerService for the process model)

    private static ServiceConnection guiConnection;

    /** Starts Open Stage Control in the ":gui" process with the given node
        arguments ("node", script, options...) and "NAME=value" environment.
        Returns false if the service could not be bound. */
    public static synchronized boolean startGuiServer (Context context, String[] args, String[] env)
    {
        if (guiConnection != null)
            return true;

        Context app = context.getApplicationContext();

        Intent intent = new Intent (app, GuiServerService.class);
        intent.putExtra (GuiServerService.EXTRA_ARGS, args);
        intent.putExtra (GuiServerService.EXTRA_ENV, env);

        ServiceConnection connection = new ServiceConnection()
        {
            @Override public void onServiceConnected (ComponentName name, IBinder service)
            {
                Log.i (TAG, "GUI server process connected");
            }

            @Override public void onServiceDisconnected (ComponentName name)
            {
                // The process died (node exited or crashed). The binding is
                // kept, so Android starts it again by itself.
                Log.w (TAG, "GUI server process disconnected");
            }
        };

        boolean bound = app.bindService (intent, connection,
                                         Context.BIND_AUTO_CREATE | Context.BIND_IMPORTANT);

        if (bound)
            guiConnection = connection;
        else
            Log.e (TAG, "could not bind GuiServerService");

        return bound;
    }

    /** Stops the GUI server and waits (up to timeoutMs) for its process to be
        gone, so the port is free when this returns. Also cleans up a server
        left behind by an earlier run of the app process that crashed.
        Returns the number of processes that had to be killed. */
    public static synchronized int stopGuiServer (Context context, int timeoutMs)
    {
        Context app = context.getApplicationContext();

        if (guiConnection != null)
        {
            try
            {
                app.unbindService (guiConnection);
            }
            catch (IllegalArgumentException e)
            {
                // Already unbound: nothing to do.
            }

            guiConnection = null;
        }

        int killed = 0;

        for (int pid : findGuiProcesses (app))
        {
            Process.killProcess (pid);
            ++killed;
        }

        long deadline = SystemClock.uptimeMillis() + timeoutMs;

        while (killed > 0 && findGuiProcesses (app).length > 0
                 && SystemClock.uptimeMillis() < deadline)
            SystemClock.sleep (25);

        return killed;
    }

    static int stopGuiServer (Context context)
    {
        return stopGuiServer (context, 1500);
    }

    public static boolean isGuiServerRunning (Context context)
    {
        return findGuiProcesses (context.getApplicationContext()).length > 0;
    }

    private static int[] findGuiProcesses (Context app)
    {
        ActivityManager am = (ActivityManager) app.getSystemService (Context.ACTIVITY_SERVICE);
        String name = app.getPackageName() + GUI_PROCESS_SUFFIX;

        List<ActivityManager.RunningAppProcessInfo> processes = am != null ? am.getRunningAppProcesses() : null;

        if (processes == null)
            return new int[0];

        int count = 0;
        int[] pids = new int[processes.size()];

        for (ActivityManager.RunningAppProcessInfo info : processes)
            if (name.equals (info.processName) && info.pid != Process.myPid())
                pids[count++] = info.pid;

        int[] result = new int[count];
        System.arraycopy (pids, 0, result, 0, count);
        return result;
    }

    //==========================================================================
    // Embedded browser

    /** Creates the WebView that shows the control GUI inside the app. */
    public static View createWebView (Context context)
    {
        return new FormulsWebView (context);
    }

    public static void loadUrl (View webView, String url)
    {
        if (webView instanceof FormulsWebView)
            ((FormulsWebView) webView).loadUrl (url);
    }

    public static void reload (View webView)
    {
        if (webView instanceof FormulsWebView)
            ((FormulsWebView) webView).reload();
    }

    public static void destroyWebView (View webView)
    {
        if (webView instanceof FormulsWebView)
            ((FormulsWebView) webView).destroy();
    }

    //==========================================================================
    // Storage

    /** The app's own folder on shared storage for the given type (e.g.
        Environment.DIRECTORY_MUSIC), or its private files folder if shared
        storage is unavailable. Needs no permission on any API level, and is
        readable by both the app process and the GUI server process. */
    public static String getExternalFilesDir (Context context, String type)
    {
        File dir = context.getExternalFilesDir (type);

        if (dir == null)
            dir = new File (context.getFilesDir(), type != null ? type : "files");

        dir.mkdirs();
        return dir.getAbsolutePath();
    }

    public static String getFilesDir (Context context)
    {
        return context.getFilesDir().getAbsolutePath();
    }

    public static String getCacheDir (Context context)
    {
        return context.getCacheDir().getAbsolutePath();
    }

    /** Copies a finished recording into the shared Music/formuls folder,
        where file managers, DAWs and USB file transfer can see it. Returns
        the path to show the user, or null if it could not be published (the
        recording itself is untouched either way). Android 10 and later
        only: earlier versions need a storage permission for this. */
    public static String publishRecording (Context context, String path)
    {
        if (Build.VERSION.SDK_INT < 29)
            return null;

        File source = new File (path);
        ContentResolver resolver = context.getContentResolver();

        ContentValues values = new ContentValues();
        values.put (MediaStore.Audio.Media.DISPLAY_NAME, source.getName());
        values.put (MediaStore.Audio.Media.MIME_TYPE, "audio/wav");
        values.put (MediaStore.Audio.Media.RELATIVE_PATH, Environment.DIRECTORY_MUSIC + "/formuls");
        values.put (MediaStore.Audio.Media.IS_PENDING, 1);

        Uri uri = null;

        try
        {
            uri = resolver.insert (MediaStore.Audio.Media.getContentUri (MediaStore.VOLUME_EXTERNAL_PRIMARY), values);

            if (uri == null)
                return null;

            try (InputStream in = new FileInputStream (source);
                 OutputStream out = resolver.openOutputStream (uri))
            {
                if (out == null)
                    throw new java.io.IOException ("no output stream");

                byte[] buffer = new byte[1 << 16];
                int n;

                while ((n = in.read (buffer)) > 0)
                    out.write (buffer, 0, n);
            }

            values.clear();
            values.put (MediaStore.Audio.Media.IS_PENDING, 0);
            resolver.update (uri, values, null, null);

            return Environment.DIRECTORY_MUSIC + "/formuls/" + source.getName();
        }
        catch (Exception e)
        {
            Log.w (TAG, "could not publish recording", e);

            if (uri != null)
                resolver.delete (uri, null, null);

            return null;
        }
    }

    //==========================================================================
    // Activity

    /** Like pressing Home: formuls leaves the screen but keeps running. */
    public static void moveToBackground (Context context)
    {
        if (context instanceof Activity)
            ((Activity) context).moveTaskToBack (true);
    }

    //==========================================================================
    // Permissions

    /** Android 13+ hides a foreground service's notification unless the user
        has allowed notifications. The service works either way; this only
        asks, once, so the "formuls is running" notification can be seen. */
    public static void requestNotificationPermission (Context context)
    {
        if (Build.VERSION.SDK_INT < 33 || ! (context instanceof Activity))
            return;

        final String permission = "android.permission.POST_NOTIFICATIONS";

        if (context.checkSelfPermission (permission) == PackageManager.PERMISSION_GRANTED)
            return;

        ((Activity) context).requestPermissions (new String[] { permission }, 0xF0F0);
    }
}
