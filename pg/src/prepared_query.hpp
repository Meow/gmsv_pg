#ifndef _QUERY_PREPARED_HPP
#define _QUERY_PREPARED_HPP

#include "interfaces.h"
#include "session.hpp"
#include "params.hpp"

using namespace GarrysMod::Lua;

class PreparedQuery : public LuaEventEmitter<PreparedQuery> {
private:
  std::shared_ptr<Session> _session;
  std::string _query_string = "";
  bool _sync = false;
public:
  std::string name() override { return "#<PreparedQuery>"; }
public:
  PreparedQuery(std::shared_ptr<Session> session, std::string query_string) : LuaEventEmitter() {
    this->_session = std::move(session);
    this->_query_string = std::move(query_string);
    AddMethod("run", run);
    AddMethod("set_sync", set_sync);
  }

  LUA_METHOD(run) {
    auto obj = Pop(LUA, 1);
    std::string name = obj->_query_string;
    auto params = pop_params(LUA, 2);

    return run_statement(LUA, *obj->_session, obj->_sync, [name, params](pqxx::work &work) {
      return work.exec(pqxx::prepped{name}, *params);
    });
  }

  LUA_METHOD(set_sync) {
    Pop(LUA, 1)->_sync = LUA->GetBool(2);
    return 0;
  }
};

#endif
