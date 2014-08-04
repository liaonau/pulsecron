#define _GNU_SOURCE
#define UNUSED __attribute__((unused))

#include <stdbool.h>

#include <basedir_fs.h>
#include <basedir.h>

#include <pulse/pulseaudio.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <string.h>
#include <unistd.h>

#include "pulsecron.h"

inline static void async_wait(pulseaudio_t* pulse, pa_operation* op)
{
    while (pa_operation_get_state(op) == PA_OPERATION_RUNNING)
        pa_threaded_mainloop_wait(pulse->mainloop);
    pa_operation_unref(op);
    pa_threaded_mainloop_unlock(pulse->mainloop);
}

static void state_cb(pa_context *context, void *userdata)
{
    pulseaudio_t* pulse = (pulseaudio_t*) userdata;
    switch (pa_context_get_state(context))
    {
    case PA_CONTEXT_READY:
        pulse->connected = 1;
        break;
    case PA_CONTEXT_FAILED:
    case PA_CONTEXT_TERMINATED:
    case PA_CONTEXT_UNCONNECTED:
    case PA_CONTEXT_CONNECTING:
    case PA_CONTEXT_AUTHORIZING:
    case PA_CONTEXT_SETTING_NAME:
        pulse->connected = 0;
        break;
    }
}

static void try_init_call(lua_State* L)
{
    int top = lua_gettop(L);
    lua_getglobal(L, "init");
    if (lua_isfunction(L, -1))
        lua_pcall(L, 0, 0, 0);
    lua_settop(L, top);
}

static void connect_state_cb(pa_context *context, void *userdata)
{
    pulseaudio_t* pulse = (pulseaudio_t*) userdata;
    pulse->subscribed = 0;
    switch (pa_context_get_state(context))
    {
    case PA_CONTEXT_READY:
        pulse->connected = 1;
        try_init_call(pulse->L);
        pa_threaded_mainloop_signal(pulse->mainloop, 0);
        break;
    case PA_CONTEXT_FAILED:
    case PA_CONTEXT_TERMINATED:
        pulse->connected = 0;
        pa_threaded_mainloop_signal(pulse->mainloop, 0);
        break;

    case PA_CONTEXT_UNCONNECTED:
    case PA_CONTEXT_CONNECTING:
    case PA_CONTEXT_AUTHORIZING:
    case PA_CONTEXT_SETTING_NAME:
        pulse->connected = 0;
        break;
    }
}

static const char* event_type(pa_subscription_event_type_t t)
{
    switch (t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK)
    {
    case PA_SUBSCRIPTION_EVENT_SINK:
        return "sink";
    case PA_SUBSCRIPTION_EVENT_SOURCE:
        return "source";
    case PA_SUBSCRIPTION_EVENT_SINK_INPUT:
        return "sink_input";
    case PA_SUBSCRIPTION_EVENT_SOURCE_OUTPUT:
        return "source_output";
    case PA_SUBSCRIPTION_EVENT_MODULE:
        return "module";
    case PA_SUBSCRIPTION_EVENT_CLIENT:
        return "client";
    case PA_SUBSCRIPTION_EVENT_SAMPLE_CACHE:
        return "sample_cache";
    case PA_SUBSCRIPTION_EVENT_SERVER:
        return "server";
    case PA_SUBSCRIPTION_EVENT_CARD:
        return "card";
    }
    return "unknown";
}

static const char* event_operation(pa_subscription_event_type_t t)
{
    switch (t & PA_SUBSCRIPTION_EVENT_TYPE_MASK)
    {
    case PA_SUBSCRIPTION_EVENT_NEW:
        return "new";
    case PA_SUBSCRIPTION_EVENT_CHANGE:
        return "change";
    case PA_SUBSCRIPTION_EVENT_REMOVE:
        return "remove";
    }
    return "unknown";
}

static void try_call(lua_State* L, const char* type, const char* operation, const char* ltype, const char* loperation)
{
    int top = lua_gettop(L);
    lua_getglobal(L, type);
    lua_pushstring(L, operation);
    if (lua_istable(L, -2))
    {
        lua_gettable(L, -2);
        if (lua_isfunction(L, -1))
        {
            lua_pushstring(L, ltype);
            lua_pushstring(L, loperation);
            lua_pcall(L, 2, 0, 0);
        }
    }
    lua_settop(L, top);
}

static void subscribe_cb(UNUSED pa_context* c, pa_subscription_event_type_t t, UNUSED uint32_t idx, void* userdata)
{
    pulseaudio_t* pulse = (pulseaudio_t*) userdata;
    if (pulse->connected)
    {
        lua_State* L = pulse->L;
        const char* type = event_type(t);
        const char* operation = event_operation(t);

        try_call(L, type , operation, type, operation);
        try_call(L, type , "any"    , type, operation);
        try_call(L, "any", operation, type, operation);
        try_call(L, "any", "any"    , type, operation);
    }
}

static void success_cb(pa_context UNUSED *c, UNUSED int success, void *userdata)
{
    pulseaudio_t* pulse = (pulseaudio_t*) userdata;
    pulse->subscribed = success;
    pa_threaded_mainloop_signal(pulse->mainloop, 0);
}

static void pc_init(pulseaudio_t* pulse)
{
    pulse->mainloop = pa_threaded_mainloop_new();
    pulse->context = pa_context_new(pa_threaded_mainloop_get_api(pulse->mainloop), "pulsecron");

    pa_context_set_state_callback(pulse->context, state_cb, pulse);

    pa_threaded_mainloop_start(pulse->mainloop);
}

static void pc_connect(pulseaudio_t* pulse)
{
    pa_threaded_mainloop_lock(pulse->mainloop);
    pa_context_set_state_callback(pulse->context, connect_state_cb, pulse);
    pa_context_connect(pulse->context, pulse->server_name, PA_CONTEXT_NOFLAGS, NULL);
    pa_threaded_mainloop_wait(pulse->mainloop);

    pa_threaded_mainloop_unlock(pulse->mainloop);
    pa_context_set_state_callback(pulse->context, state_cb, pulse);
}

static void pc_deinit(pulseaudio_t* pulse)
{
    pa_context_unref(pulse->context);
    pulse->context = NULL;

    pa_threaded_mainloop_stop(pulse->mainloop);
    pa_threaded_mainloop_free(pulse->mainloop);
    pulse->mainloop = NULL;
}

static void pc_subscribe(pulseaudio_t* pulse)
{
    pa_threaded_mainloop_lock(pulse->mainloop);
    pa_operation* op = pa_context_subscribe(pulse->context, PA_SUBSCRIPTION_MASK_ALL, success_cb, (void*) pulse);
    async_wait(pulse, op);

    pa_context_set_subscribe_callback(pulse->context, subscribe_cb, (void*) pulse);
}

static inline void loop(pulseaudio_t* pulse)
{
    while(true)
    {
        if (!pulse->connected)
        {
            if (pulse->context != NULL)
                pc_deinit(pulse);
            pc_init(pulse);
            pc_connect(pulse);
        }
        if (pulse->connected && !pulse->subscribed)
            pc_subscribe(pulse);

        sleep(pulse->update_time);
    }
}

static bool lua_init(pulseaudio_t* pulse)
{
    lua_State* L = luaL_newstate();
    if (L == NULL)
    {
        fprintf(stderr, "%s\n", "couldn't initialize Lua, exiting");
        return false;
    }
    pulse->L = L;
    luaL_openlibs(L);

    return true;
}

static bool lua_loadrc(pulseaudio_t* pulse)
{
    lua_State* L = pulse->L;
    xdgHandle xdg;
    xdgInitHandle(&xdg);
    char* rel_path = "pulsecron/rc.lua";
    char* confpath = xdgConfigFind(rel_path, &xdg);
    xdgWipeHandle(&xdg);
    if (strlen(confpath) == 0)
    {
        fprintf(stderr, "couldn't find config file \"%s\" in XDG config directories\n", rel_path);
        free(confpath);
        return false;
    }
    if (luaL_dofile(L, confpath) != 0)
    {
        fprintf(stderr, "couldn't parse config file \"%s\"\n", confpath);
        free(confpath);
        return false;
    }
    free(confpath);

    lua_getglobal(L, "update_time");
    pulse->update_time = lua_tonumber(L, -1);
    if (pulse->update_time <= 0)
        pulse->update_time = DEFAULT_UPDATE_TIME;
    lua_getglobal(L, "server_name");
    pulse->server_name = lua_isstring(L, -1) ? strdup(lua_tostring(L, -1)) : NULL;
    lua_pop(L, 2);

    return true;
}

static inline void fatal(const char* m)
{
    fprintf(stderr, "%s\n", m);
    exit(EXIT_FAILURE);
}

int main(UNUSED int argc, UNUSED char** argv)
{
    pulseaudio_t pulse;

    pulse.connected  = 0;
    pulse.subscribed = 0;
    pulse.context    = NULL;
    pulse.mainloop   = NULL;

    if (!lua_init(&pulse))
        exit(EXIT_FAILURE);
    if (!lua_loadrc(&pulse))
        exit(EXIT_FAILURE);

    loop(&pulse);
    return 0;
}

