#ifndef _QUERY_HPP
#define _QUERY_HPP

#include "interfaces.h"
#include "session.hpp"

using namespace GarrysMod::Lua;

class DatabaseQuery : public LuaEventEmitter<DatabaseQuery> {
private:
  std::shared_ptr<Session> _session;
  std::string _query_string = "";
  bool _sync = false;
public:
  std::string name() override { return "#<DatabaseQuery>"; }
public:
  DatabaseQuery(std::shared_ptr<Session> session, std::string query_string) : LuaEventEmitter() {
    this->_session = std::move(session);
    this->_query_string = std::move(query_string);
    AddMethod("run", run);
    AddMethod("set_sync", set_sync);
  }

  LUA_METHOD(run) {
    auto obj = Pop(LUA, 1);
    std::string query_string = obj->_query_string;

    return run_statement(LUA, *obj->_session, obj->_sync, [query_string](pqxx::work &work) {
      return work.exec(query_string);
    });
  }

  LUA_METHOD(set_sync) {
    Pop(LUA, 1)->_sync = LUA->GetBool(2);
    return 0;
  }
};

#endif
