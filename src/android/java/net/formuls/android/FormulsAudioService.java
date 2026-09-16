/*
 * FormulsAudioService.java
 *
 * A foreground service that keeps formuls making sound while it is not the
 * app on screen.
 *
 * Android treats an app whose activity is not visible as a background app:
 * its process may be frozen, deprioritised or killed, and the CPU and Wi-Fi
 * may be allowed to sleep. None of that is acceptable for an instrument
 * whose GUI might be open in a browser app in front of it. While the engine
 * runs, formuls therefore holds this foreground service, which
 *
 *   - shows an ongoing notification (tap it to return to formuls),
 *   - is typed "mediaPlayback", the type Android expects for audio output,
 *   - holds a partial wake lock, so the audio thread keeps running with the
 *     screen off,
 *   - holds a low-latency Wi-Fi lock, so OSC and WebSocket traffic from a
 *     tablet on the network is not delayed by Wi-Fi power saving, and
 *   - holds a multicast lock, which many devices need before they deliver
 *     the multicast packets Ableton Link peers find each other with.
 *
 * The GUI server process (GuiServerService) is bound from the app process,
 * so it inherits this raised priority too.
 *
 * It is started and stopped from native code, through FormulsAndroid.
 */

package net.formuls.android;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.IBinder;
import android.os.PowerManager;
import android.os.Process;
import android.util.Log;

public class FormulsAudioService extends Service
{
    private static final String TAG = "formuls";
    private static final String CHANNEL_ID = "formuls-engine";
    private static final int NOTIFICATION_ID = 1;

    private PowerManager.WakeLock wakeLock;
    private WifiManager.WifiLock wifiLock;
    private WifiManager.MulticastLock multicastLock;

    @Override
    public IBinder onBind (Intent intent)
    {
        return null;
    }

    @Override
    public int onStartCommand (Intent intent, int flags, int startId)
    {
        Notification notification = buildNotification();

        if (Build.VERSION.SDK_INT >= 29)
            startForeground (NOTIFICATION_ID, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK);
        else
            startForeground (NOTIFICATION_ID, notification);

        acquireLocks();

        // If Android ever kills the app process, the engine is gone with it;
        // restarting a service with nothing to keep alive would be pointless.
        return START_NOT_STICKY;
    }

    @Override
    public void onDestroy()
    {
        releaseLocks();

        if (Build.VERSION.SDK_INT >= 24)
            stopForeground (STOP_FOREGROUND_REMOVE);
        else
            stopForeground (true);

        super.onDestroy();
    }

    /** Swiping formuls away in the recent-apps list is the Android
        equivalent of closing its window on the desktop, which quits the app.
        Without this the engine would carry on with no way back to it other
        than the notification. */
    @Override
    public void onTaskRemoved (Intent rootIntent)
    {
        Log.i (TAG, "task removed; quitting formuls");
        FormulsAndroid.stopGuiServer (this);
        stopSelf();
        Process.killProcess (Process.myPid());
    }

    //==========================================================================
    private Notification buildNotification()
    {
        NotificationManager manager = (NotificationManager) getSystemService (Context.NOTIFICATION_SERVICE);

        if (Build.VERSION.SDK_INT >= 26 && manager != null
             && manager.getNotificationChannel (CHANNEL_ID) == null)
        {
            NotificationChannel channel = new NotificationChannel (CHANNEL_ID, "formuls engine",
                                                                   NotificationManager.IMPORTANCE_LOW);
            channel.setDescription ("Shown while the formuls sound engine is running");
            channel.setShowBadge (false);
            manager.createNotificationChannel (channel);
        }

        Intent launch = getPackageManager().getLaunchIntentForPackage (getPackageName());
        PendingIntent contentIntent = null;

        if (launch != null)
        {
            launch.addFlags (Intent.FLAG_ACTIVITY_SINGLE_TOP | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT);
            contentIntent = PendingIntent.getActivity (this, 0, launch,
                                                       PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        }

        int icon = getApplicationInfo().icon != 0 ? getApplicationInfo().icon
                                                  : android.R.drawable.ic_media_play;

        Notification.Builder builder = Build.VERSION.SDK_INT >= 26
                                         ? new Notification.Builder (this, CHANNEL_ID)
                                         : new Notification.Builder (this);

        builder.setContentTitle ("formuls is running")
               .setContentText ("Control GUI at http://127.0.0.1:" + FormulsAndroid.GUI_PORT)
               .setSmallIcon (icon)
               .setOngoing (true)
               .setCategory (Notification.CATEGORY_SERVICE);

        if (contentIntent != null)
            builder.setContentIntent (contentIntent);

        if (Build.VERSION.SDK_INT >= 31)
            builder.setForegroundServiceBehavior (Notification.FOREGROUND_SERVICE_IMMEDIATE);

        return builder.build();
    }

    @SuppressWarnings ("deprecation")
    private void acquireLocks()
    {
        try
        {
            if (wakeLock == null)
            {
                PowerManager pm = (PowerManager) getSystemService (Context.POWER_SERVICE);
                wakeLock = pm.newWakeLock (PowerManager.PARTIAL_WAKE_LOCK, "formuls:engine");
                wakeLock.setReferenceCounted (false);
                wakeLock.acquire();
            }

            WifiManager wifi = (WifiManager) getApplicationContext().getSystemService (Context.WIFI_SERVICE);

            if (wifi != null && wifiLock == null)
            {
                int mode = Build.VERSION.SDK_INT >= 29 ? WifiManager.WIFI_MODE_FULL_LOW_LATENCY
                                                       : WifiManager.WIFI_MODE_FULL_HIGH_PERF;
                wifiLock = wifi.createWifiLock (mode, "formuls:gui");
                wifiLock.setReferenceCounted (false);
                wifiLock.acquire();
            }

            if (wifi != null && multicastLock == null)
            {
                multicastLock = wifi.createMulticastLock ("formuls:link");
                multicastLock.setReferenceCounted (false);
                multicastLock.acquire();
            }
        }
        catch (RuntimeException e)
        {
            // A missing lock costs performance, not function: carry on.
            Log.w (TAG, "could not acquire all locks", e);
        }
    }

    private void releaseLocks()
    {
        if (wakeLock != null && wakeLock.isHeld())           wakeLock.release();
        if (wifiLock != null && wifiLock.isHeld())           wifiLock.release();
        if (multicastLock != null && multicastLock.isHeld()) multicastLock.release();

        wakeLock = null;
        wifiLock = null;
        multicastLock = null;
    }
}
