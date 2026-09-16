/*
 * faust_android_prefix.h
 *
 * Force-included (-include) ahead of each faust-generated Pd external when
 * it is compiled for Android. See src/android/native/CMakeLists.txt.
 *
 * Faust's puredata.cpp architecture looks up Pd's multichannel API at run
 * time instead of linking against it, so one binary works with old and new
 * Pd versions:
 *
 *     g_signal_setmultiout = (t_signal_setmultiout)dlsym(
 *         dlopen(nullptr, RTLD_NOW), "signal_setmultiout");
 *
 * On macOS and Linux that finds the symbol in the Pd host. On Android it
 * cannot: libpd is linked statically into libjuce_jni.so, which Java loads
 * with RTLD_LOCAL, so a global lookup from the main program never sees it.
 * The objects would then silently fall back to single-channel mode -- a
 * different behaviour from the desktop build.
 *
 * Here the externals are compiled against the exact libpd they run with, so
 * the run-time check is unnecessary: this header answers it at compile time
 * by pointing dlsym() at the real function. <dlfcn.h> is included first so
 * its own declaration of dlsym is not affected by the macro.
 */

#pragma once

#include <dlfcn.h>
#include "m_pd.h"

#define dlsym(handle, name) ((void) (handle), (void *) &signal_setmultiout)
