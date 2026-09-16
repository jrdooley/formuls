/*
 * formuls_static_externals.c
 *
 * Registers the Pd externals that the Android build links in statically
 * (see CMakeLists.txt in this folder for why they are not loaded from
 * pd/externals as on the desktop).
 *
 * Call formuls_setup_static_externals() after libpd_init() and before
 * opening _main.pd. It is safe to call more than once: Pd classes can only
 * be registered once per process, and libpd itself can only be initialised
 * once, so repeated calls after the first do nothing.
 */

#include "m_pd.h"

/* Defined by the faust-generated sources (faust_setup(mydsp) with
   -Dmydsp=<name>) and by abl_link~.cpp. All are extern "C". */
void formuls_tilde_setup (void);
void f_reverb_tilde_setup (void);
void f_repeater_tilde_setup (void);
void abl_link_tilde_setup (void);

void formuls_setup_static_externals (void)
{
    static int done = 0;

    if (done)
        return;

    done = 1;

    formuls_tilde_setup();
    f_reverb_tilde_setup();
    f_repeater_tilde_setup();
    abl_link_tilde_setup();
}
