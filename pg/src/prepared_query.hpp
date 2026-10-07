#ifndef _QUERY_PREPARED_HPP
#define _QUERY_PREPARED_HPP

#include "interfaces.h"
#include "session.hpp"

using namespace GarrysMod::Lua;

class PreparedQuery : public LuaEventEmitter<PreparedQuery> {
private:
  std::shared_ptr<Session> _session;
  std::string _query_string = "";
  bool _sync = false;

  // Lua has a single number type. Whole numbers are sent the way an integer
  // column expects them: 100000 instead of 1e+05.
  static void append_number(pqxx::params &params, double value) {
    if (value > -1e15 && value < 1e15 && value == (double)(long long)value)
      params.append((long long)value);
    else
      params.append(value);
  }
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
    auto params = std::make_shared<pqxx::params>();

    for (int i = 2, top = LUA->Top(); i <= top; i++) {
      switch (LUA->GetType(i)) {
      case Type::Nil:
        params->append();
        break;
      case Type::Bool:
        params->append(LUA->GetBool(i));
        break;
      case Type::Number:
        append_number(*params, LUA->GetNumber(i));
        break;
      case Type::String:
        params->append(check_string(LUA, i, ""));
        break;
      default:
        throw std::invalid_argument("pg - bad parameter #" + std::to_string(i - 1) + " (string, number, boolean or nil expected)");
      }
    }

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
