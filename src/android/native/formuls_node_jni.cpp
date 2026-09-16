/*
 * formuls_node_jni.cpp
 *
 * Runs the Open Stage Control server on nodejs-mobile, for
 * net.formuls.android.GuiServerService (see that class for the process
 * model: this code only ever runs in the app's separate ":gui" process).
 *
 * On the desktop the JUCE app spawns a bundled `node` binary as a child
 * process. Android apps cannot exec binaries shipped in the APK, so node is
 * loaded as a library (libnode.so from nodejs-mobile) and node::Start() is
 * called directly, with the same argument list the desktop build passes on
 * the command line.
 *
 * Two node-specific details are handled here:
 *
 *   - node::Start() (libuv's uv_setup_args, to be precise) assumes argv's
 *     strings sit in one contiguous block of memory, as they do for a real
 *     process, and rewrites that block for process.title. So the arguments
 *     are copied into a single buffer rather than passed as separate
 *     JNI-derived strings.
 *
 *   - An Android process has no terminal: stdout and stderr go nowhere.
 *     Both are redirected into logcat (tag "formuls-gui") so Open Stage
 *     Control's log -- including any startup error -- can be read with
 *     `adb logcat -s formuls-gui`.
 *
 * node::Start() can only be called once per process, which is exactly why
 * node lives in a process of its own: stopping the GUI kills that process,
 * and starting it again gets a fresh one.
 */

#include <jni.h>
#include <android/log.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

#include "node.h"

namespace
{

constexpr const char* logTag = "formuls-gui";

/** Copies a Java String[] into std::strings. */
std::vector<std::string> toStrings (JNIEnv* env, jobjectArray array)
{
    std::vector<std::string> result;

    if (array == nullptr)
        return result;

    const auto count = env->GetArrayLength (array);

    for (jsize i = 0; i < count; ++i)
    {
        auto jstr = (jstring) env->GetObjectArrayElement (array, i);

        if (jstr == nullptr)
        {
            result.emplace_back();
            continue;
        }

        const char* utf = env->GetStringUTFChars (jstr, nullptr);
        result.emplace_back (utf != nullptr ? utf : "");

        if (utf != nullptr)
            env->ReleaseStringUTFChars (jstr, utf);

        env->DeleteLocalRef (jstr);
    }

    return result;
}

/** Sends everything written to stdout/stderr to logcat, one line per entry. */
void redirectStdioToLogcat()
{
    static bool redirected = false;

    if (redirected)
        return;

    redirected = true;

    setvbuf (stdout, nullptr, _IOLBF, 0);
    setvbuf (stderr, nullptr, _IONBF, 0);

    int fds[2];

    if (pipe (fds) != 0)
        return;

    dup2 (fds[1], STDOUT_FILENO);
    dup2 (fds[1], STDERR_FILENO);

    std::thread ([readFd = fds[0]]
    {
        char buffer[2048];
        std::string pending;

        for (;;)
        {
            const auto n = read (readFd, buffer, sizeof (buffer));

            if (n <= 0)
                break;

            pending.append (buffer, (size_t) n);

            for (auto newline = pending.find ('\n'); newline != std::string::npos;
                 newline = pending.find ('\n'))
            {
                __android_log_write (ANDROID_LOG_INFO, logTag, pending.substr (0, newline).c_str());
                pending.erase (0, newline + 1);
            }
        }
    }).detach();
}

} // namespace

extern "C" JNIEXPORT jint JNICALL
Java_net_formuls_android_GuiServerService_nativeStartNode (JNIEnv* env, jclass,
                                                          jobjectArray jargs,
                                                          jobjectArray jenv)
{
    redirectStdioToLogcat();

    // "NAME=value" pairs: HOME, TMPDIR and XDG_CONFIG_HOME, so that Open
    // Stage Control's config folder and node's temp files land inside the
    // app's private storage rather than in paths that do not exist here.
    for (const auto& pair : toStrings (env, jenv))
    {
        const auto eq = pair.find ('=');

        if (eq != std::string::npos && eq > 0)
            setenv (pair.substr (0, eq).c_str(), pair.substr (eq + 1).c_str(), 1);
    }

    const auto args = toStrings (env, jargs);

    if (args.empty())
    {
        __android_log_write (ANDROID_LOG_ERROR, logTag, "no arguments given to node");
        return -1;
    }

    // One contiguous block, as a real process would have (see the header).
    size_t total = 0;

    for (const auto& a : args)
        total += a.size() + 1;

    // Intentionally never freed: node keeps pointers into argv for the life
    // of the process, and this process ends when node does.
    auto* block = static_cast<char*> (calloc (total, 1));
    std::vector<char*> argv;
    argv.reserve (args.size() + 1);

    char* cursor = block;

    for (const auto& a : args)
    {
        memcpy (cursor, a.c_str(), a.size());
        argv.push_back (cursor);
        cursor += a.size() + 1;
    }

    argv.push_back (nullptr);

    std::string commandLine;

    for (const auto& a : args)
        commandLine += (commandLine.empty() ? "" : " ") + a;

    __android_log_print (ANDROID_LOG_INFO, logTag, "starting node: %s", commandLine.c_str());

    const auto exitCode = node::Start ((int) args.size(), argv.data());

    __android_log_print (ANDROID_LOG_INFO, logTag, "node exited with code %d", exitCode);
    return exitCode;
}
