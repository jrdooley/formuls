/*
 * GuiServerService.java
 *
 * Hosts the Open Stage Control server (the formuls control GUI) on the
 * Android build.
 *
 * Process model
 * -------------
 * The manifest declares this service with android:process=":gui", so it runs
 * in a process of its own, separate from the JUCE app and its audio engine.
 * This mirrors the desktop build, where node is a child process, and it is
 * not optional:
 *
 *   - node::Start() can only be called once per process. Stopping formuls
 *     and starting it again needs a fresh node, i.e. a fresh process.
 *   - If Open Stage Control exits or crashes, node ends the whole process it
 *     is running in. In a process of its own that takes the GUI down; in the
 *     app's process it would take the audio engine down with it.
 *
 * Lifecycle
 * ---------
 * FormulsAndroid.startGuiServer() binds to this service from the app process
 * with BIND_AUTO_CREATE | BIND_IMPORTANT. Android creates the ":gui" process
 * and calls onBind(), which starts node on a thread of its own. The binding
 * also lends this process the priority of the app process, which holds a
 * foreground service while the engine runs -- so the GUI server keeps
 * running when formuls is in the background (for example while the GUI is
 * open in a separate browser app).
 *
 * FormulsAndroid.stopGuiServer() unbinds and then kills this process, which
 * closes node's sockets and frees port 9001 at once.
 *
 * If node exits on its own, this process ends itself (node cannot be
 * restarted in-process). The binding is still held, so Android restarts the
 * service in a new process and node starts again: the GUI server comes back
 * after a crash without the user having to press anything.
 */

package net.formuls.android;

import android.app.Service;
import android.content.Intent;
import android.os.Binder;
import android.os.IBinder;
import android.os.Process;
import android.util.Log;

public class GuiServerService extends Service
{
    static final String EXTRA_ARGS = "net.formuls.android.args";
    static final String EXTRA_ENV  = "net.formuls.android.env";

    private static final String TAG = "formuls-gui";

    /** node wants far more stack than a default Java thread offers. */
    private static final long NODE_THREAD_STACK_BYTES = 64L * 1024L * 1024L;

    private static boolean nodeStarted = false;

    private final IBinder binder = new Binder();

    private static native int nativeStartNode (String[] args, String[] env);

    @Override
    public IBinder onBind (Intent intent)
    {
        startNodeOnce (intent);
        return binder;
    }

    @Override
    public boolean onUnbind (Intent intent)
    {
        // The app let go of the GUI server: that is the stop signal.
        Log.i (TAG, "unbound; ending the GUI server process");
        stopSelf();
        Process.killProcess (Process.myPid());
        return false;
    }

    @Override
    public void onDestroy()
    {
        super.onDestroy();
        Process.killProcess (Process.myPid());
    }

    private synchronized void startNodeOnce (Intent intent)
    {
        if (nodeStarted)
            return;

        final String[] args = intent != null ? intent.getStringArrayExtra (EXTRA_ARGS) : null;
        final String[] env  = intent != null ? intent.getStringArrayExtra (EXTRA_ENV)  : null;

        if (args == null || args.length == 0)
        {
            Log.e (TAG, "GuiServerService bound without node arguments");
            return;
        }

        nodeStarted = true;

        Thread nodeThread = new Thread (null, new Runnable()
        {
            @Override
            public void run()
            {
                int exitCode = -1;

                try
                {
                    System.loadLibrary ("node");
                    System.loadLibrary ("formuls_node");
                    exitCode = nativeStartNode (args, env);
                }
                catch (Throwable t)
                {
                    Log.e (TAG, "could not run node", t);
                }

                // node cannot be started twice in one process, so the only
                // way to run it again is a new process.
                Log.w (TAG, "node exited (" + exitCode + "); ending the GUI server process");
                Process.killProcess (Process.myPid());
            }
        }, "formuls-node", NODE_THREAD_STACK_BYTES);

        nodeThread.start();
    }
}
