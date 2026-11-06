/* -*- C++ -*-
 *
 *  kk4s_ons.h - kk4s additions to ONScripter-NX (GPL-2 or later, like ONScripter)
 *
 *  Save files (save*.dat, gloval.sav, envdata, kidoku.dat) go to the folder
 *  given with --kk4s-saves; reading falls back to the game folder, so saves
 *  made before stay visible. Without --kk4s-saves nothing changes.
 */
#ifndef KK4S_ONS_H
#define KK4S_ONS_H

#include <stdio.h>

extern char *kk4s_save_dir; /* ends with '/', or NULL */

/* true when kk4s handles this file: *fp is the result (writes always, reads
   when the file is in the kk4s save folder). Otherwise the caller opens the
   file as before. */
bool kk4sSaveOpen( const char *name, const char *mode, FILE **fp );

/* The path of a save file for stat(): the kk4s save folder when the file is
   there (or saves are redirected and it is in neither place), else `fallback`. */
void kk4sSavePath( const char *name, const char *fallback, char *out, size_t out_len );

#ifdef KK4S_EMBEDDED
/* ONScripter's main() when it is built into kk4s (kk4s calls it, it never returns). */
int onscripter_main( int argc, char **argv );
#endif

#endif
