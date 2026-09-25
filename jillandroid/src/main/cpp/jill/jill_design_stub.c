/*
 * jill_design_stub.c -- TEMPORARY stand-in for design() (DESIGN.C's own level-editor entry point).
 * NOT part of the upstream decomp -- this project's own file, same reasoning as HOSTANDROID.c's
 * rexit()/k_read() stand-ins and the now-deleted jill_jungle_stubs.c (see jill/JUNGLE.c's own
 * header comment for what retired that file).
 *
 * DESIGN.C is Jill's built-in level editor -- infname()/printobjinfo()/objdesign()/design() --
 * reachable only through jmenu()'s hidden dev key '5' (see jill/JUNGLE.c's jmenu(), the "else if
 * (key == 5)" branch). It's dev-only tooling, not part of playing the game, so it's out of scope
 * for this port for now -- same call already made for DESIGN.C back when JUNGLE.C's own dependency
 * list was first scoped out. design() is JUNGLE.C's only real link dependency on it (infname()/
 * printobjinfo()/objdesign() are DESIGN.C's own internal helpers, not called from outside it), so
 * this file provides just that one function, as a no-op.
 *
 * DELETE THIS the moment DESIGN.C is properly ported -- a duplicate-symbol link error at that
 * point is the intended signal to come remove it.
 */

#include <android/log.h>

#define LOG_TAG "jillhost"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

void design(void)
{
    LOGI("design() stand-in (see this file's own comment) -- Jill's level editor isn't ported; "
         "returning to the menu immediately");
}
