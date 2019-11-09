#include "connection.h"

#define GET_CONNECTION_OBJECT gmpg_connection *c; gmpg_connection **__c = (gmpg_connection**)luaL_checkudata(L, 1, "pg_connection"); c = *__c;

int gmpg_connection_push_mt(lua_State *L) {
  if (luaL_newmetatable(L, "pg_connection")) {
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");

    luaL_Reg pg_connection_funcs[] = {
      "__gc", gmpg_gc,
      "connect", gmpg_connect,
      "connected", gmpg_connected,
      NULL, NULL
    };
    luaL_register(L, 0, pg_connection_funcs);
  }

  return 0;
}

LUA_BASELESS_FUNC(gmpg_new) {
  gmpg_connection *conn = (gmpg_connection*)malloc(sizeof(gmpg_connection));
  gmpg_connection **c = (gmpg_connection**)lua_newuserdata(L, sizeof(gmpg_connection*));
  conn->conn = NULL;
  conn->res = NULL;
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
  const char *connection_string;
  GET_CONNECTION_OBJECT

  connection_string = lua_check_string(lua_get_base(L), 2);

  c->conn = PQconnectdb(connection_string);

  if (PQstatus(c->conn) != CONNECTION_OK)
    printf("pg2 - failed to connect to database!\n(%s)\n", PQerrorMessage(c->conn));

  return 0;
}

int gmpg_connected(lua_State *L) {
  int status;
  GET_CONNECTION_OBJECT

  status = 0;

  if (c->conn != NULL) {
    status = PQstatus(c->conn) == CONNECTION_OK ? 1 : 0;
  }

  lua_pushboolean(L, status);

  return 1;
}
