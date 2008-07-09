/* Copyright (C) 2007, 2008 MySQL AB */

#include <string.h>
 

/**
 * expose the chassis functions into the lua space
 * Also moves the global print function to the 'os' table and
 * replaces print with our logging function at a log level equal to
 * the current chassis minimum log level, so that we always see the
 * output of scripts.
 */


#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <glib.h>

#include "chassis-mainloop.h"

static int lua_chassis_set_shutdown (lua_State *L) {
	chassis_set_shutdown();

	return 0;
}

/**
 * Log a message via the chassis log facility instead of using STDOUT.
 * This is more expensive than just printing to STDOUT, but generally logging
 * in a script would be protected by an 'if(debug)' or be important enough to
 * warrant the extra CPU cycles.
 *
 * Lua parameters: loglevel (first), message (second)
 */
static int lua_chassis_log(lua_State *L) {
    static const char *const log_names[] = {"error", "critical",
        "warning", "message", "info", "debug", NULL};
	static const int log_levels[] = {G_LOG_LEVEL_ERROR, G_LOG_LEVEL_CRITICAL,
        G_LOG_LEVEL_WARNING, G_LOG_LEVEL_MESSAGE,
        G_LOG_LEVEL_INFO, G_LOG_LEVEL_DEBUG};

    int option = luaL_checkoption(L, 1, "message", log_names);
	const char *log_message = luaL_optstring(L, 2, "nil");
	const char *source = NULL;
	const char *first_source = "unknown";
	int currentline = -1;
	int first_line = -1;
	int stackdepth = 1;
	lua_Debug ar;
	chassis *chas;
	
	/* try to get some information about who logs this message */
	do {
		/* walk up the stack to try to find a file name */
        if (!lua_getstack(L, stackdepth, &ar)) break;
        if (!lua_getinfo(L, "Sl", &ar)) break;

		currentline = ar.currentline;
        source = ar.source;
		/* save the first short_src we have encountered,
		   in case we exceed our max stackdepth to check
		 */
		if (stackdepth == 1) {
			first_source = ar.short_src;
			first_line = ar.currentline;
		}
		/* below: '@' comes from Lua's dofile, our lua-load-factory doesn't set it when we load a file. */
	} while (++stackdepth < 11 && source && source[0] != '/' && source[0] != '@'); /* limit walking the stack to a sensible value */

	if (source) {
		if (source[0] == '@') {
			/* skip Lua's "this is from a file" indicator */
			source++;
		}
        lua_getfield(L, LUA_REGISTRYINDEX, "chassis");
        chas = (chassis*) lua_topointer(L, -1);
        lua_pop(L, 1);
        if (chas && chas->base_dir) {
            if (g_str_has_prefix(source, chas->base_dir)) {
                source += strlen(chas->base_dir);
                /* skip a leading dir separator */
                if (source[0] == G_DIR_SEPARATOR) source++;
            }
        }
	}
    g_log(G_LOG_DOMAIN, log_levels[option], "(%s:%d) %s", (source ? source : first_source),
			(source ? currentline : first_line), log_message);
	
	return 0;
}

/**
 * these wrapper functions insert the appropriate log level into the stack
 * and then simply call lua_chassis_log()
 */
#define CHASSIS_LUA_LOG(level) static int lua_chassis_log_ ## level(lua_State *L) {\
	int n = lua_gettop(L);\
	int retval;\
	lua_pushliteral(L, #level);\
	lua_insert(L, 1);\
	retval = lua_chassis_log(L);\
	lua_remove(L, 1);\
	g_assert(n == lua_gettop(L));\
	return retval;\
}

CHASSIS_LUA_LOG(error)
CHASSIS_LUA_LOG(critical)
CHASSIS_LUA_LOG(warning)
/* CHASSIS_LUA_LOG(message)*/
CHASSIS_LUA_LOG(info)
CHASSIS_LUA_LOG(debug)

#undef CHASSIS_LUA_LOG


static int lua_chassis_log_message(lua_State *L) {
	int n = lua_gettop(L);
	int retval;
	lua_pushliteral(L, "message");
	lua_insert(L, 1);
	retval = lua_chassis_log(L);
	lua_remove(L, 1);
	g_assert(n == lua_gettop(L));
	return retval;
}
/*
** Assumes the table is on top of the stack.
*/
static void set_info (lua_State *L) {
	lua_pushliteral (L, "_COPYRIGHT");
	lua_pushliteral (L, "Copyright (C) 2008 MySQL AB");
	lua_settable (L, -3);
	lua_pushliteral (L, "_DESCRIPTION");
	lua_pushliteral (L, "export chassis-functions as chassis.*");
	lua_settable (L, -3);
	lua_pushliteral (L, "_VERSION");
	lua_pushliteral (L, "LuaChassis 0.1");
	lua_settable (L, -3);
}

#define CHASSIS_LUA_LOG_FUNC(level) {#level, lua_chassis_log_ ## level}

static const struct luaL_reg chassislib[] = {
	{"set_shutdown", lua_chassis_set_shutdown},
	{"log", lua_chassis_log},
/* we don't really want g_error being exposed, since it abort()s */
/*    CHASSIS_LUA_LOG_FUNC(error), */
    CHASSIS_LUA_LOG_FUNC(critical),
    CHASSIS_LUA_LOG_FUNC(warning),
    CHASSIS_LUA_LOG_FUNC(message),
    CHASSIS_LUA_LOG_FUNC(info),
    CHASSIS_LUA_LOG_FUNC(debug),
	{NULL, NULL},
};

#undef CHASSIS_LUA_LOG_FUNC

/**
 * moves the global function 'print' to the 'os' table and
 * places 'lua_chassis_log_message' it its place.
 */
static void remap_print(lua_State *L) {
	int n = lua_gettop(L);

	lua_getglobal(L, "os"); /* sp = 1 */
	lua_getglobal(L, "print"); /* sp = 2 */
	lua_setfield(L, -2, "print"); /* sp = 1*/
    lua_pop(L, 1); /* table os. sp = 0*/
	
	lua_register(L, "print", lua_chassis_log_message);
	
	g_assert(n == lua_gettop(L));
}

#if defined(_WIN32)
# define LUAEXT_API __declspec(dllexport)
#else
# define LUAEXT_API extern
#endif

LUAEXT_API int luaopen_chassis (lua_State *L) {
	luaL_register (L, "chassis", chassislib);
	set_info (L);
	remap_print(L);
	return 1;
}
