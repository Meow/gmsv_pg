#include "main.h"
#include "connection.hpp"

void* DatabaseQuery::last_query = nullptr;

#ifdef _WIN32
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "libeay32.lib")
#pragma comment(lib, "ssleay32.lib")
#pragma comment(lib, "libpqxx_static.lib")
#pragma comment(lib, "libpq.lib")
#endif

using namespace GarrysMod::Lua;

int _pg_create(lua_State *state) {
  return DatabaseConnection::Make()->Push(state);
}

GMOD_MODULE_OPEN() {
  LUA->PushSpecial(SPECIAL_GLOB);
    LUA->CreateTable();
      LUA->PushCFunction(_pg_create);
      LUA->SetField(-2, "new_connection");
      LUA->PushString(PG_VERSION);
      LUA->SetField(-2, "version");
      LUA->PushString(PG_VERSION_MAJOR);
      LUA->SetField(-2, "version_major");
      LUA->PushString(PG_VERSION_MINOR);
      LUA->SetField(-2, "version_minor");
      LUA->PushString(PG_VERSION_PATCH);
      LUA->SetField(-2, "version_patch");
      LUA->PushString(PG_VERSION_SUFFX);
      LUA->SetField(-2, "version_suffix");
    LUA->SetField(-2, "pg");
  LUA->Pop();

  return 0;
}

GMOD_MODULE_CLOSE() {
  return 0;
}
