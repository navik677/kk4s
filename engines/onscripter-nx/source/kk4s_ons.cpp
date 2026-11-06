/* -*- C++ -*-
 *
 *  kk4s_ons.cpp - kk4s additions to ONScripter-NX, see kk4s_ons.h
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */
#include "kk4s_ons.h"

#include <string.h>
#include <sys/stat.h>

char *kk4s_save_dir = NULL;

static bool kk4sSaveName( const char *name, char *out, size_t out_len )
{
    if ( !kk4s_save_dir ) return false;
    snprintf( out, out_len, "%s%s", kk4s_save_dir, name );
    return true;
}

bool kk4sSaveOpen( const char *name, const char *mode, FILE **fp )
{
    char path[1024];
    if ( !kk4sSaveName( name, path, sizeof(path) ) ) return false;
    if ( strchr( mode, 'w' ) || strchr( mode, 'a' ) ){
        *fp = ::fopen( path, mode );
        if ( !*fp ) fprintf( stderr, "kk4s: cannot write %s\n", path );
        return true;
    }
    *fp = ::fopen( path, mode );
    return *fp != NULL;
}

void kk4sSavePath( const char *name, const char *fallback, char *out, size_t out_len )
{
    struct stat st;
    if ( kk4sSaveName( name, out, out_len ) ){
        if ( stat( out, &st ) == 0 || stat( fallback, &st ) != 0 ) return;
    }
    snprintf( out, out_len, "%s", fallback );
}
