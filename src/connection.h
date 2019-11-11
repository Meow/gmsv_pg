#ifndef _PG_CONNECTION_H
#define _PG_CONNECTION_H

#define MAX_PG_TYPES 131070

#include <libpq/libpq-fe.h>
#include "lua_shared.h"

typedef struct {
  const char *info;
  char *types[MAX_PG_TYPES];
  PGconn *conn;
  PGresult *res;
} gmpg_connection;

int gmpg_connection_push_mt(lua_State *L);
int gmpg_new(lua_State *L);
int gmpg_gc(lua_State *L);
int gmpg_connect(lua_State *L);
int gmpg_disconnect(lua_State *L);
int gmpg_connected(lua_State *L);
int gmpg_disconnect(lua_State *L);

#endif
