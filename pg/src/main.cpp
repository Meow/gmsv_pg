#include "main.h"
#include "connection.hpp"

using namespace GarrysMod::Lua;

GMOD_MODULE_OPEN() {
  DatabaseConnection::Register(LUA, "pg.DatabaseConnection");
  DatabaseQuery::Register(LUA, "pg.DatabaseQuery");
  PreparedQuery::Register(LUA, "pg.PreparedQuery");

  LUA->PushSpecial(SPECIAL_GLOB);
    LUA->CreateTable();
      LUA->PushCFunction(DatabaseConnection::create);
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
  // The queries that are still queued get to finish first. Their results have
  // nowhere to go anymore, but what they wrote should not be lost.
  Session::Shutdown();
  LuaEventEmitterManager::Close(LUA);

  return 0;
}
