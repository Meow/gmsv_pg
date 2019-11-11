#include "connection.h"
#include "debug.h"

#define GET_CONNECTION_OBJECT gmpg_connection *c;                                                                \
                              gmpg_connection **__c = (gmpg_connection**)luaL_checkudata(L, 1, "pg_connection"); \
                              c = *__c;
#define PG_CHAR_GETTER_FUNC(what) int gmpg_##what(lua_State *L) { \
  GET_CONNECTION_OBJECT                                           \
  lua_pushstring(L, PQ##what(c->conn));                           \
  return 1;                                                       \
}

int gmpg_connection_push_mt(lua_State *L) {
  if (luaL_newmetatable(L, "pg_connection")) {
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");

    luaL_Reg pg_connection_funcs[] = {
      "__gc", gmpg_gc,
      "connect", gmpg_connect,
      "disconnect", gmpg_disconnect,
      "connected", gmpg_connected,
      "protocol_version", gmpg_protocol_version,
      "server_version", gmpg_server_version,
      "error_message", gmpg_error_message,
      "set_encoding", gmpg_set_encoding,
      "db", gmpg_db,
      "user", gmpg_user,
      "pass", gmpg_pass,
      "host", gmpg_host,
      "port", gmpg_port,
      NULL, NULL
    };

    luaL_register(L, 0, pg_connection_funcs);
  }

  return 0;
}

PG_CHAR_GETTER_FUNC(db)
PG_CHAR_GETTER_FUNC(user)
PG_CHAR_GETTER_FUNC(pass)
PG_CHAR_GETTER_FUNC(host)
PG_CHAR_GETTER_FUNC(port)

void perform_disconnect(gmpg_connection *c) {
  if (c->conn != NULL) {
    PQfinish(c->conn);
    c->conn = NULL;
  }

  if (c->res != NULL) {
    PQclear(c->res);
    c->res = NULL;
  }
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

  perform_disconnect(c);
  free(c);

  return 0;
}

int gmpg_connect(lua_State *L) {
  const char *connection_string;
  PGresult *res;
  int i, rows, status, row_id;

  GET_CONNECTION_OBJECT

  connection_string = lua_check_string(lua_get_base(L), 2);
  status = 0;

  c->conn = PQconnectdb(connection_string);

  if (PQstatus(c->conn) != CONNECTION_OK)
    printf("pg2 - failed to connect to database!\n(%s)\n", PQerrorMessage(c->conn));
  else {
    res = PQexec(c->conn, "SELECT oid, typcategory FROM pg_type;");

    if (PQresultStatus(res) != PGRES_TUPLES_OK)
      printf("pg2 - PostgreSQL server does not appear to have any types (version too old?)\n");
    else {
      rows = PQntuples(res);

      if (rows >= MAX_PG_TYPES || rows == 0)
        if (rows != 0)
          printf("pg2 - PostgreSQL server appears to have out-of-range types (%i). Report an issue at GitHub!\n", rows);
        else
          printf("pg2 - PostgreSQL server appears to have 0 types (version too old?)\n");
      else {
        status = 1;

        for (i = 0; i < rows; i++) {
          if (!PQgetisnull(res, i, 0) && !PQgetisnull(res, i, 1)) {
            row_id = atoi(PQgetvalue(res, i, 0));

            if (row_id >= MAX_PG_TYPES) {
              printf("pg2 - PostgreSQL server appears to have out-of-range types (%i). Report an issue at GitHub!\n", row_id);
              status = 0;
              break;
            }

            c->types[row_id] = PQgetvalue(res, i, 1);
          }
        }
      }
    }

    PQclear(res);
  }

  if (!status)
    perform_disconnect(c);

  lua_pushboolean(L, status);

  return 1;
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

int gmpg_disconnect(lua_State *L) {
  GET_CONNECTION_OBJECT

  perform_disconnect(c);

  return 0;
}

int gmpg_protocol_version(lua_State *L) {
  GET_CONNECTION_OBJECT

  lua_pushnumber(L, PQprotocolVersion(c->conn));

  return 1;
}

int gmpg_server_version(lua_State *L) {
  GET_CONNECTION_OBJECT

  lua_pushnumber(L, PQserverVersion(c->conn));

  return 1;
}

int gmpg_error_message(lua_State *L) {
  GET_CONNECTION_OBJECT

  lua_pushstring(L, PQerrorMessage(c->conn));

  return 1;
}

int gmpg_set_encoding(lua_State *L) {
  char encoding_query[64];
  PGresult *res;

  GET_CONNECTION_OBJECT

  sprintf(encoding_query, "SET CLIENT_ENCODING TO '%s';", lua_check_string(lua_get_base(L), 2));

  res = PQexec(c->conn, encoding_query);

  if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    printf("pg2 - failed to set encoding!\n(%s)\n", PQerrorMessage(c->conn));
    lua_pushboolean(L, 0);
  } else
    lua_pushboolean(L, 1);

  PQclear(res);

  return 1;
}
