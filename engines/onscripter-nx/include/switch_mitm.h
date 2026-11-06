#ifndef SWITCH_MITM_H
#define SWITCH_MITM_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <switch.h>

typedef struct {
    bool break_flag;
    int *current_mode;
} ThreadData;

// kk4s: declared here, defined once in ONScripterLabel.cpp
extern ThreadData *thread_data;
extern SDL_Thread *thread_id;

extern PadState pad;
extern PadRepeater pad_r;

void switch_init();
void switch_deinit();
int switch_getinput( void* data );

FILE *romfsOpen( const char* path, const char* mode );

int switch_check_files();
void switch_nofile_splash();


#endif