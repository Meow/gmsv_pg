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
int gmpg_protocol_version(lua_State *L);
int gmpg_server_version(lua_State *L);
int gmpg_error_message(lua_State *L);
int gmpg_db(lua_State *L);
int gmpg_user(lua_State *L);
int gmpg_pass(lua_State *L);
int gmpg_host(lua_State *L);
int gmpg_port(lua_State *L);

#endif
