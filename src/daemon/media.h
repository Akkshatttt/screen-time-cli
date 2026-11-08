#ifndef MEDIA_H
#define MEDIA_H
#include <windows.h>
int stc_audio_playing(const wchar_t *fg_exe_path); /* foreground app has an audible session */
int stc_fullscreen(HWND h);                        /* window covers its whole monitor */
#endif
