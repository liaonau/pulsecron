#pragma once
#define _GNU_SOURCE

#include <pulse/pulseaudio.h>
#include <lua.h>

#define DEFAULT_UPDATE_TIME 5

typedef struct
{
    pa_threaded_mainloop* mainloop;
    pa_context* context;

    int subscribed;
    int connected;

    char* server_name;
    unsigned long update_time;

    lua_State* L;
} pulseaudio_t;
