#ifndef GMODC_LUA_INTERFACE_H
#define GMODC_LUA_INTERFACE_H

#include <GarrysMod/Lua/CCompat.h>
#include "source_compat.h"
#include "types.h"
#include "lua.h"

enum {
  LUA_SPECIAL_GLOB, /*  Global table      */
  LUA_SPECIAL_ENV,  /*  Environment table */
  LUA_SPECIAL_REG   /*  Registry table    */
};

#ifndef GMOD
#ifdef _WIN32
#define DLL_EXPORT __declspec(dllexport)
#else
#define DLL_EXPORT __attribute__((visibility("default")))
#endif

#ifdef GMOD_MODULE_OPEN
#undef GMOD_MODULE_OPEN
#endif

#ifdef GMOD_MODULE_CLOSE
#undef GMOD_MODULE_CLOSE
#endif

#ifdef LUA_FUNCTION
#undef LUA_FUNCTION
#endif

#define GMOD_MODULE_OPEN()                      \
  int gmod13_open__Imp(luabase_t *LUA);         \
  DLL_EXPORT int gmod13_open(lua_State *L)      \
  {                                             \
    return gmod13_open__Imp(lua_get_base(L));   \
  }                                             \
  int gmod13_open__Imp(luabase_t *LUA)

#define GMOD_MODULE_CLOSE()                     \
  int gmod13_close__Imp(luabase_t *LUA);        \
  DLL_EXPORT int gmod13_close(lua_State *L)     \
  {                                             \
    return gmod13_close__Imp(lua_get_base(L));  \
  }                                             \
  int gmod13_close__Imp(luabase_t *LUA)

#define LUA_FUNCTION(FUNC)                      \
  int FUNC##__Imp(luabase_t *LUA);              \
  int FUNC(lua_State *L)                        \
  {                                             \
    luabase_t *LUA = lua_get_base(L);           \
    lua_set_state(LUA, L);                      \
    return FUNC##__Imp(LUA);                    \
  }                                             \
  int FUNC##__Imp(luabase_t *LUA)
#endif

#endif
