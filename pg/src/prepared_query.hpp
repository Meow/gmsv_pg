#ifndef _QUERY_PREPARED_HPP
#define _QUERY_PREPARED_HPP

#include "interfaces.h"

using namespace GarrysMod::Lua;

class PreparedQuery : public LuaEventEmitter<239, PreparedQuery> {
private:
  std::string _query_string = "";
  std::thread* _worker_thread = nullptr;
  pqxx::work* _query_work = nullptr;
  pqxx::prepare::invocation* _inv = nullptr;
  bool _sync = false;

  char oid_to_type(int oid) {
    return this->type_map[oid];
  }
public:
  char* type_map = nullptr;
  pqxx::connection* _connection = nullptr;
  std::string name() override { return "#<PreparedQuery>"; }

  void _set_connection(pqxx::connection* c) { this->_connection = c; }
public:
  PreparedQuery(std::string query_string) : LuaEventEmitter() {
    this->_query_string = query_string;
    AddMethod("run", run);
    AddMethod("set_sync", set_sync);
  }

  ~PreparedQuery() {
    if (this->_worker_thread != nullptr) {
      this->_worker_thread->join();
      delete this->_worker_thread;
      this->_worker_thread = nullptr;
    }
  }

  int _spawn_thread(lua_State* state) {
    // Just to make sure we're not doing something horribly wrong
    if (this->_worker_thread != nullptr) {
      this->_worker_thread->join();
      delete this->_worker_thread;
      delete this->_query_work;
      this->_worker_thread = nullptr;
      this->_query_work = nullptr;
    }

    if (!this->_sync)
      this->_worker_thread = new std::thread(&PreparedQuery::run_query, this, state);
    else
      return this->run_query(state);
    return 0;
  }

  int run_query(lua_State* state) {
    if (this->_connection == nullptr) {
      Emit("error", "database connection destroyed during query");
      return 0;
    }

    try {
      pqxx::result res = this->_inv->exec();
      this->_query_work->commit();

      int size = res.size();
      int i = 1; // since Lua tables start at 1
      LuaValue::table_t res_t;

      if (size > 0) {
        for (auto row : res) {
          LuaValue::table_t row_t;
          for (auto col : row) {
            char ts = this->oid_to_type(col.type());

            if (col.is_null()) {
              row_t.emplace(std::make_pair(col.name(), LuaValue::Make(Type::NIL)));
            } else {
              switch (ts) {
              case TYPCATEGORY_BOOLEAN:
                row_t.emplace(std::make_pair(col.name(), col.as<bool>()));
                break;
              case TYPCATEGORY_NUMERIC:
              case TYPCATEGORY_ENUM:
                row_t.emplace(std::make_pair(col.name(), col.as<double>()));
                break;
              case TYPCATEGORY_INVALID:
                row_t.emplace(std::make_pair(col.name(), LuaValue::Make(Type::NIL)));
                break;
              default:
                row_t.emplace(std::make_pair(col.name(), col.c_str()));
              }
            }
          }
          res_t[i++] = row_t;
        }
      }

      if (!this->_sync)
        Emit("success", res_t, size);
      else {
        LUA->PushBool(true);
        LuaValue(res_t).PushTable(state);
        LUA->PushNumber(size);
        return 3;
      }
    } catch (std::exception &e) {
      if (!this->_sync)
        Emit("error", e.what());
      else {
        LUA->PushBool(false);
        LUA->PushString(e.what());
        return 2;
      }
    }

    if (this->_sync) {
      LUA->PushBool(false);
      LUA->PushString("pg - no error");
      return 2;
    } else {
      return 0;
    }
  }

  static int run(lua_State* state) {
    CHECK_CONNECTION

      // Just to make sure we're not doing something horribly wrong
      if (obj->_worker_thread != nullptr) {
        obj->_worker_thread->join();
        delete obj->_worker_thread;
        delete obj->_query_work;
        obj->_worker_thread = nullptr;
        obj->_query_work = nullptr;
      }

    auto name = LuaValue::Pop(state, 2);

    try {
      obj->_query_work = new pqxx::work(*obj->_connection);
      pqxx::prepare::invocation inv = obj->_query_work->prepared(name);

      int i = 3;
      auto val = LuaValue::Pop(state, 3);

      while (val.type() != Type::NIL) {
        inv(std::string(val));
        val = LuaValue::Pop(state, ++i);
      }

      obj->_inv = &inv;
    } catch (std::exception &e) {
      LUA->ThrowError(e.what());
      return 0;
    }

    obj->_spawn_thread(state);

    return 0;
  }

  LUA_METHOD(set_sync) {
    CHECK_CONNECTION
      bool should_sync = LuaValue::Pop(state, 2);

    obj->_sync = should_sync;

    return 0;
  }
private:
  void test() {

  }
};

#endif
