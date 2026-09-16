/*
 * FormulsWebView.java
 *
 * The browser the control GUI is shown in when it is opened inside the app.
 *
 * This is used instead of juce::WebBrowserComponent because that component
 * configures its WebView for general web pages, and two of its choices break
 * a multi-touch instrument surface:
 *
 *   - built-in pinch zoom is enabled, so a two-finger gesture on two faders
 *     zooms the page instead of moving both faders;
 *   - DOM storage is left disabled, and Open Stage Control's client keeps
 *     its settings in localStorage.
 *
 * Everything else is plain WebView: the GUI is a local web page served by
 * Open Stage Control at http://127.0.0.1:9001, exactly as in a browser app.
 */

package net.formuls.android;

import android.annotation.SuppressLint;
import android.content.Context;
import android.graphics.Color;
import android.os.Build;
import android.view.KeyEvent;
import android.view.View;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceRequest;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;

public class FormulsWebView extends WebView
{
    @SuppressLint ("SetJavaScriptEnabled")
    public FormulsWebView (Context context)
    {
        super (context);

        WebSettings settings = getSettings();
        settings.setJavaScriptEnabled (true);
        settings.setDomStorageEnabled (true);
        settings.setDatabaseEnabled (true);
        settings.setMediaPlaybackRequiresUserGesture (false);

        // A control surface, not a document: no zooming, no overview mode.
        settings.setSupportZoom (false);
        settings.setBuiltInZoomControls (false);
        settings.setDisplayZoomControls (false);
        settings.setLoadWithOverviewMode (false);
        settings.setUseWideViewPort (true);
        settings.setTextZoom (100);

        // Always fetch the live GUI from the local server.
        settings.setCacheMode (WebSettings.LOAD_NO_CACHE);

        setBackgroundColor (Color.BLACK);
        setOverScrollMode (View.OVER_SCROLL_NEVER);
        setHapticFeedbackEnabled (false);
        setLongClickable (false);
        setOnLongClickListener (new View.OnLongClickListener()
        {
            @Override public boolean onLongClick (View v) { return true; }   // no text-selection popups
        });

        if (Build.VERSION.SDK_INT >= 26)
            setImportantForAutofill (View.IMPORTANT_FOR_AUTOFILL_NO_EXCLUDE_DESCENDANTS);

        // Open every link in this view rather than handing it to a browser.
        setWebViewClient (new WebViewClient()
        {
            @Override
            public boolean shouldOverrideUrlLoading (WebView view, WebResourceRequest request)
            {
                return false;
            }
        });

        setWebChromeClient (new WebChromeClient());

        // Lets chrome://inspect attach to the GUI for debugging.
        WebView.setWebContentsDebuggingEnabled (true);
    }

    /** Implemented in AndroidPlatform.cpp. Returns true if formuls handled
        the Back button. */
    private static native boolean nativeBackPressed();

    /** While the GUI has focus, key presses come here rather than to JUCE's
        own view, so Back would otherwise go straight to the activity and
        close formuls. Hand it to the app first. */
    @Override
    public boolean onKeyDown (int keyCode, KeyEvent event)
    {
        if (keyCode == KeyEvent.KEYCODE_BACK)
        {
            boolean handled = false;

            try
            {
                handled = nativeBackPressed();
            }
            catch (UnsatisfiedLinkError e)
            {
                // Native side not loaded: fall through to the default.
            }

            if (handled)
                return true;
        }

        return super.onKeyDown (keyCode, event);
    }
}
