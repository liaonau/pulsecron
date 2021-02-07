#pragma once
#define _GNU_SOURCE
#define UNUSED __attribute__((unused))

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <pthread.h>
#include <pulse/pulseaudio.h>

#define DEFAULT_UPDATE_TIME 5

typedef struct
{
    pa_threaded_mainloop* mainloop;
    pa_context*           context;

    int subscribed;
    int connected;

    char*         server_name;
    unsigned long update_time;

    lua_State*      L;
    sd_bus*         bus;
    pthread_mutex_t lock;
} pulseaudio_t;
