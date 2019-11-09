#include "connection.h"
#include <stdio.h>

#define PG_VERSION "2.0.0"
#define PG_VERSION_MAJOR "2"
#define PG_VERSION_MINOR "0"
#define PG_VERSION_PATCH "0"
#define PG_VERSION_SUFFX "development"

LUA_BASELESS_FUNC(gmpg_new_connection) {
  return gmpg_new(L);
}

DLL_EXPORT int gmod13_open(lua_State *state) {
  luabase_t *LUA = lua_get_base(state);

  load_lua_shared();

  lua_push_special(LUA, LUA_SPECIAL_GLOB);
  lua_create_table(LUA);
  lua_push_cfunc(LUA, gmpg_new_connection);
  lua_set_field(LUA, -2, "new_connection");
  lua_push_string(LUA, PG_VERSION, 0);
  lua_set_field(LUA, -2, "version");
  lua_push_string(LUA, PG_VERSION_MAJOR, 0);
  lua_set_field(LUA, -2, "version_major");
  lua_push_string(LUA, PG_VERSION_MINOR, 0);
  lua_set_field(LUA, -2, "version_minor");
  lua_push_string(LUA, PG_VERSION_PATCH, 0);
  lua_set_field(LUA, -2, "version_patch");
  lua_push_string(LUA, PG_VERSION_SUFFX, 0);
  lua_set_field(LUA, -2, "version_suffix");
  lua_set_field(LUA, -2, "pg");
  lua_pop(LUA, 1);

  return 0;
}

GMOD_MODULE_CLOSE() {
  return 0;
}
