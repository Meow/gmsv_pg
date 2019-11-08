#include "connection.h"

#define GET_CONNECTION_OBJECT gmpg_connection *c; gmpg_connection **__c = (gmpg_connection**)luaL_checkudata(L, 1, "pg_connection"); c = *__c;

int gmpg_connection_push_mt(lua_State *L) {
  if (luaL_newmetatable(L, "pg_connection")) {
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");

    luaL_Reg pg_connection_funcs[] = {
      "__gc", gmpg_gc,
      "connect", gmpg_connect,
      NULL, NULL
    };
    luaL_register(L, 0, pg_connection_funcs);
  }

  return 0;
}

GMOD_FUNC_WITH_STATE(gmpg_new) {
  luaL_loadstring(L, "print('test')");
  lua_call(L, 0, 0);
  lua_pop(L, 1);

  gmpg_connection *conn = (gmpg_connection*)malloc(sizeof(gmpg_connection));
  gmpg_connection **c = (gmpg_connection**)lua_newuserdata(L, sizeof(gmpg_connection*));
  *c = conn;

  gmpg_connection_push_mt(L);
  lua_setmetatable(L, -2);

  return 1;
}

int gmpg_gc(lua_State *L) {
  GET_CONNECTION_OBJECT

  free(c);

  return 0;
}

int gmpg_connect(lua_State *L) {
  GET_CONNECTION_OBJECT

  return 0;
}
