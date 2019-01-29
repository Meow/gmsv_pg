#ifndef _QUERY_HPP
#define _QUERY_HPP

#include "interfaces.h"

using namespace GarrysMod::Lua;

class DatabaseQuery : public LuaEventEmitter<239, DatabaseQuery> {
private:
  std::string _query_string = "";
  std::thread* _worker_thread = nullptr;
  pqxx::work* _query_work = nullptr;
  bool _sync = false;

  char oid_to_type(int oid) {
    return this->type_map[oid];
  }
public:
  char* type_map = nullptr;
  pqxx::connection* _connection = nullptr;
  std::string name() override { return "#<DatabaseQuery>"; }

  static void* last_query;
  std::shared_ptr<DatabaseQuery> next_query = nullptr;

  void _set_connection(pqxx::connection* c) { this->_connection = c; }
public:
  DatabaseQuery(std::string query_string) : LuaEventEmitter() {
    this->_query_string = query_string;
    AddMethod("run", run);
    AddMethod("set_sync", set_sync);
  }

  ~DatabaseQuery() {
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
      this->_worker_thread = new std::thread(&DatabaseQuery::run_query, this, state);
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
      this->_query_work = new pqxx::work(*this->_connection);
      pqxx::result res = this->_query_work->exec(this->_query_string);
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
      // Run the next query in the queue.
      if (this->next_query != nullptr)
        this->next_query->_spawn_thread(state);
      else
        DatabaseQuery::last_query = nullptr; // we're the last query

      return 0;
    }
  }

  static int run(lua_State* state) {
    CHECK_CONNECTION
    if (obj->_sync)
      return obj->_spawn_thread(state);

    DatabaseQuery* last_q = (DatabaseQuery*)DatabaseQuery::last_query;

    int ret_n = 0;

    if (last_q != nullptr)
      last_q->next_query = obj;
    else
      ret_n = obj->_spawn_thread(state);
    DatabaseQuery::last_query = obj.get();
 
    return ret_n;
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
