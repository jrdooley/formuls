/*
 * AndroidPlatform.cpp -- see AndroidPlatform.h for an overview.
 *
 * Compiles to nothing outside Android.
 */

// JUCE only exposes its JNI helpers (getEnv, getAppContext, LocalRef...) to
// code that asks for them before the first JUCE header is included.
#define JUCE_CORE_INCLUDE_JNI_HELPERS 1

#include "AndroidPlatform.h"

#if JUCE_ANDROID

#include <cstdio>

namespace formuls::android
{

using juce::LocalRef;

namespace
{

constexpr const char* helperClassName = "net.formuls.android.FormulsAndroid";

// Resources are packed under this prefix in the APK (build-android.sh stages
// them into the Projucer's "Extra Android Assets" folder).
constexpr const char* assetPrefix = "assets/formuls/";

// Written last by extractResources(): its presence and content say that the
// extracted folder is complete and belongs to the installed APK.
constexpr const char* stampFileName = ".formuls-extracted";

//==============================================================================
/** Logs and clears a pending Java exception. Returns true if there was one. */
bool clearJavaException (JNIEnv* env, const char* what)
{
    if (! env->ExceptionCheck())
        return false;

    env->ExceptionDescribe();   // stack trace to logcat
    env->ExceptionClear();
    juce::Logger::writeToLog (juce::String ("Java exception in ") + what);
    return true;
}

/** Loads one of formuls' own Java classes through the app's class loader.
    JNIEnv::FindClass is not used because it only sees the app's classes when
    called from a thread that Java itself started with the right loader. */
LocalRef<jclass> loadAppClass (JNIEnv* env, const char* dottedName)
{
    auto context = juce::getAppContext();

    if (context == nullptr)
        return {};

    LocalRef<jclass> contextClass (env->GetObjectClass (context.get()));
    const auto getClassLoader = env->GetMethodID (contextClass.get(), "getClassLoader",
                                                  "()Ljava/lang/ClassLoader;");
    LocalRef<jobject> loader (env->CallObjectMethod (context.get(), getClassLoader));

    if (clearJavaException (env, "getClassLoader") || loader == nullptr)
        return {};

    LocalRef<jclass> loaderClass (env->FindClass ("java/lang/ClassLoader"));
    const auto loadClass = env->GetMethodID (loaderClass.get(), "loadClass",
                                             "(Ljava/lang/String;)Ljava/lang/Class;");

    LocalRef<jclass> result ((jclass) env->CallObjectMethod (loader.get(), loadClass,
                                                             juce::javaString (dottedName).get()));

    if (clearJavaException (env, dottedName))
        return {};

    return result;
}

/** The FormulsAndroid helper class, looked up once and kept. */
jclass getHelperClass (JNIEnv* env)
{
    static juce::GlobalRef helper;

    if (helper == nullptr)
    {
        auto cls = loadAppClass (env, helperClassName);

        if (cls == nullptr)
        {
            juce::Logger::writeToLog ("formuls: Java helper class not found: "
                                      + juce::String (helperClassName));
            return nullptr;
        }

        helper = juce::GlobalRef (LocalRef<jobject> ((jobject) env->NewLocalRef (cls.get())));
    }

    return (jclass) helper.get();
}

jmethodID getHelperMethod (JNIEnv* env, const char* name, const char* signature)
{
    auto* cls = getHelperClass (env);

    if (cls == nullptr)
        return nullptr;

    const auto method = env->GetStaticMethodID (cls, name, signature);
    clearJavaException (env, name);
    return method;
}

/** The activity if there is one, otherwise the application context. */
LocalRef<jobject> getBestContext()
{
    auto activity = juce::getCurrentActivity();
    return activity != nullptr ? activity : juce::getAppContext();
}

juce::File callStringGetter (const char* name, const char* signature, jstring optionalArg = nullptr,
                             bool passArg = false)
{
    auto* env = juce::getEnv();
    const auto method = getHelperMethod (env, name, signature);

    if (method == nullptr)
        return {};

    auto context = juce::getAppContext();

    LocalRef<jstring> result (passArg
        ? (jstring) env->CallStaticObjectMethod (getHelperClass (env), method, context.get(), optionalArg)
        : (jstring) env->CallStaticObjectMethod (getHelperClass (env), method, context.get()));

    if (clearJavaException (env, name) || result == nullptr)
        return {};

    return juce::File (juce::juceString (result.get()));
}

//==============================================================================
/** Identifies the installed APK, so an update or reinstall is noticed. */
juce::String describeInstalledApk()
{
    const auto apk = juce::File::getSpecialLocation (juce::File::currentApplicationFile);

    return juce::String (ProjectInfo::versionString)
         + "|" + apk.getFullPathName()
         + "|" + juce::String (apk.getSize())
         + "|" + juce::String (apk.getLastModificationTime().toMilliseconds());
}

} // namespace

std::function<bool()> onBackButtonFromWebView;

//==============================================================================
juce::File getResourceRoot()
{
    return getFilesDir().getChildFile ("formuls-resources");
}

bool areResourcesReady()
{
    const auto stamp = getResourceRoot().getChildFile (stampFileName);
    return stamp.existsAsFile() && stamp.loadFileAsString() == describeInstalledApk();
}

juce::Result extractResources()
{
    if (areResourcesReady())
        return juce::Result::ok();

    const auto apk = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
    juce::ZipFile zip (apk);

    if (zip.getNumEntries() == 0)
        return juce::Result::fail ("Could not open the installed app package: " + apk.getFullPathName());

    const auto root = getResourceRoot();

    // Extract beside the final folder and swap it in only when complete, so
    // an extraction interrupted by the app being killed is never mistaken
    // for a finished one.
    const auto staging = root.getSiblingFile (root.getFileName() + ".partial");
    staging.deleteRecursively();

    if (auto r = staging.createDirectory(); r.failed())
        return juce::Result::fail ("Could not create " + staging.getFullPathName() + ": " + r.getErrorMessage());

    int extracted = 0;

    for (int i = 0; i < zip.getNumEntries(); ++i)
    {
        const auto* entry = zip.getEntry (i);

        if (entry == nullptr || ! entry->filename.startsWith (assetPrefix)
             || entry->filename.endsWithChar ('/'))
            continue;

        const auto relative = entry->filename.substring ((int) std::strlen (assetPrefix));

        // Belt and braces against a malformed entry escaping the folder.
        if (relative.isEmpty() || relative.startsWith ("../") || relative.contains ("/../"))
            continue;

        const auto target = staging.getChildFile (relative);

        if (auto r = target.getParentDirectory().createDirectory(); r.failed())
            return juce::Result::fail ("Could not create " + target.getParentDirectory().getFullPathName());

        std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (i));

        if (in == nullptr)
            return juce::Result::fail ("Could not extract " + relative);

        // Plain stdio rather than juce::FileOutputStream: on Android, JUCE
        // hands every file that stream closes to the media scanner, which is
        // pointless (and slow, and noisy in logcat) for ~100 private files.
        juce::MemoryBlock data;
        in->readIntoMemoryBlock (data);

        auto* out = std::fopen (target.getFullPathName().toRawUTF8(), "wb");
        const auto written = out != nullptr ? std::fwrite (data.getData(), 1, data.getSize(), out) : 0;
        const auto closedOk = out != nullptr && std::fclose (out) == 0;

        if (! closedOk || written != data.getSize())
            return juce::Result::fail ("Could not write " + target.getFullPathName());

        ++extracted;
    }

    if (extracted == 0)
        return juce::Result::fail ("The app package contains no formuls resources ("
                                   + juce::String (assetPrefix) + "). Was it built with build-android.sh?");

    if (! staging.getChildFile (stampFileName).replaceWithText (describeInstalledApk()))
        return juce::Result::fail ("Could not finish extracting the resources");

    root.deleteRecursively();

    if (! staging.moveFileTo (root))
        return juce::Result::fail ("Could not move the extracted resources into place");

    juce::Logger::writeToLog ("formuls: extracted " + juce::String (extracted)
                              + " resource files to " + root.getFullPathName());
    return juce::Result::ok();
}

//==============================================================================
void startAudioService()
{
    auto* env = juce::getEnv();

    if (const auto method = getHelperMethod (env, "startAudioService", "(Landroid/content/Context;)V"))
    {
        env->CallStaticVoidMethod (getHelperClass (env), method, juce::getAppContext().get());
        clearJavaException (env, "startAudioService");
    }
}

void stopAudioService()
{
    auto* env = juce::getEnv();

    if (const auto method = getHelperMethod (env, "stopAudioService", "(Landroid/content/Context;)V"))
    {
        env->CallStaticVoidMethod (getHelperClass (env), method, juce::getAppContext().get());
        clearJavaException (env, "stopAudioService");
    }
}

juce::Result startGuiServer (const juce::StringArray& nodeArgs, const juce::StringArray& environment)
{
    auto* env = juce::getEnv();
    const auto method = getHelperMethod (env, "startGuiServer",
                                         "(Landroid/content/Context;[Ljava/lang/String;[Ljava/lang/String;)Z");

    if (method == nullptr)
        return juce::Result::fail ("The GUI server launcher is missing from this build");

    const auto bound = env->CallStaticBooleanMethod (getHelperClass (env), method,
                                                     juce::getAppContext().get(),
                                                     juce::juceStringArrayToJava (nodeArgs).get(),
                                                     juce::juceStringArrayToJava (environment).get());

    if (clearJavaException (env, "startGuiServer") || ! bound)
        return juce::Result::fail ("Could not start the GUI server service");

    return juce::Result::ok();
}

int stopGuiServer (int timeoutMs)
{
    auto* env = juce::getEnv();
    const auto method = getHelperMethod (env, "stopGuiServer", "(Landroid/content/Context;I)I");

    if (method == nullptr)
        return 0;

    const auto killed = env->CallStaticIntMethod (getHelperClass (env), method,
                                                  juce::getAppContext().get(), (jint) timeoutMs);

    return clearJavaException (env, "stopGuiServer") ? 0 : (int) killed;
}

bool isGuiServerRunning()
{
    auto* env = juce::getEnv();
    const auto method = getHelperMethod (env, "isGuiServerRunning", "(Landroid/content/Context;)Z");

    if (method == nullptr)
        return false;

    const auto running = env->CallStaticBooleanMethod (getHelperClass (env), method,
                                                       juce::getAppContext().get());

    return ! clearJavaException (env, "isGuiServerRunning") && running;
}

//==============================================================================
juce::File getFilesDir()
{
    static const auto dir = callStringGetter ("getFilesDir", "(Landroid/content/Context;)Ljava/lang/String;");
    return dir;
}

juce::File getCacheDir()
{
    static const auto dir = callStringGetter ("getCacheDir", "(Landroid/content/Context;)Ljava/lang/String;");
    return dir;
}

juce::File getExternalFilesDir (const juce::String& type)
{
    auto typeString = type.isNotEmpty() ? juce::javaString (type) : LocalRef<jstring>();

    return callStringGetter ("getExternalFilesDir",
                             "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;",
                             typeString.get(), true);
}

juce::String publishRecording (const juce::File& recording)
{
    auto* env = juce::getEnv();
    const auto method = getHelperMethod (env, "publishRecording",
                                         "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;");

    if (method == nullptr)
        return {};

    LocalRef<jstring> result ((jstring) env->CallStaticObjectMethod (getHelperClass (env), method,
                                                                     juce::getAppContext().get(),
                                                                     juce::javaString (recording.getFullPathName()).get()));

    if (clearJavaException (env, "publishRecording") || result == nullptr)
        return {};

    return juce::juceString (result.get());
}

//==============================================================================
void requestNotificationPermission()
{
    auto* env = juce::getEnv();

    if (const auto method = getHelperMethod (env, "requestNotificationPermission", "(Landroid/content/Context;)V"))
    {
        env->CallStaticVoidMethod (getHelperClass (env), method, getBestContext().get());
        clearJavaException (env, "requestNotificationPermission");
    }
}

void moveToBackground()
{
    auto* env = juce::getEnv();

    if (const auto method = getHelperMethod (env, "moveToBackground", "(Landroid/content/Context;)V"))
    {
        env->CallStaticVoidMethod (getHelperClass (env), method, getBestContext().get());
        clearJavaException (env, "moveToBackground");
    }
}

//==============================================================================
GuiWebView::GuiWebView()
{
    auto* env = juce::getEnv();

    if (const auto method = getHelperMethod (env, "createWebView",
                                             "(Landroid/content/Context;)Landroid/view/View;"))
    {
        LocalRef<jobject> view (env->CallStaticObjectMethod (getHelperClass (env), method,
                                                             getBestContext().get()));

        if (! clearJavaException (env, "createWebView") && view != nullptr)
            setView (view.get());
    }
}

GuiWebView::~GuiWebView()
{
    auto* env = juce::getEnv();

    if (getView() == nullptr)
        return;

    // Keep a reference past setView(nullptr), which drops the component's.
    LocalRef<jobject> view (env->NewLocalRef ((jobject) getView()));
    setView (nullptr);

    if (const auto method = getHelperMethod (env, "destroyWebView", "(Landroid/view/View;)V"))
    {
        env->CallStaticVoidMethod (getHelperClass (env), method, view.get());
        clearJavaException (env, "destroyWebView");
    }
}

void GuiWebView::loadUrl (const juce::String& url)
{
    auto* env = juce::getEnv();

    if (getView() == nullptr)
        return;

    if (const auto method = getHelperMethod (env, "loadUrl", "(Landroid/view/View;Ljava/lang/String;)V"))
    {
        env->CallStaticVoidMethod (getHelperClass (env), method, (jobject) getView(),
                                   juce::javaString (url).get());
        clearJavaException (env, "loadUrl");
    }
}

void GuiWebView::reload()
{
    auto* env = juce::getEnv();

    if (getView() == nullptr)
        return;

    if (const auto method = getHelperMethod (env, "reload", "(Landroid/view/View;)V"))
    {
        env->CallStaticVoidMethod (getHelperClass (env), method, (jobject) getView());
        clearJavaException (env, "reload");
    }
}

} // namespace formuls::android

//==============================================================================
// Called by FormulsWebView.onKeyDown when Back is pressed while the embedded
// GUI has keyboard focus. Returns true if formuls handled it; otherwise the
// Java side lets the activity handle it as usual.
extern "C" JNIEXPORT jboolean JNICALL
Java_net_formuls_android_FormulsWebView_nativeBackPressed (JNIEnv*, jclass)
{
    const auto& handler = formuls::android::onBackButtonFromWebView;
    return (handler != nullptr && handler()) ? JNI_TRUE : JNI_FALSE;
}

#endif // JUCE_ANDROID
