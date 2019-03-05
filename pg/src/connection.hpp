#ifndef _CONNECTION_H
#define _CONNECTION_H

#include "interfaces.h"
#include "query.hpp"
#include "prepared_query.hpp"

using namespace GarrysMod::Lua;

class DatabaseConnection : public LuaEventEmitter<239, DatabaseConnection> {
private:
  std::string _host;
  std::string _database;
  std::string _user;
  std::string _password;
  std::string _port;
  char _type_map[MAX_PG_TYPES];
public:
  std::string name() override { return "#<DatabaseConnection>"; }
  pqxx::connection* _connection = nullptr;
public:
  DatabaseConnection() : LuaEventEmitter() {
    AddGetter("host", get_host);
    AddGetter("database", get_database);
    AddGetter("user", get_user);
    AddGetter("password", get_password);
    AddGetter("port", get_port);
    AddMethod("query", query);
    AddMethod("query_prepared", query_prepared);
    AddMethod("connect", connect);
    AddMethod("escape", escape);
    AddMethod("unescape", unescape);
    AddMethod("quote", quote);
    AddMethod("quote_name", quote_name);
    AddMethod("disconnect", disconnect);
    AddMethod("cancel", cancel);
    AddMethod("protocol_version", protocol_version);
    AddMethod("server_version", server_version);
    AddMethod("deactivate", deactivate);
    AddMethod("activate", activate);
    AddMethod("is_open", is_open);
    AddMethod("prepare", prepare);
    AddMethod("unprepare", unprepare);
    AddMethod("set_encoding", set_encoding);
  }

  ~DatabaseConnection() { }
public:
  LUA_METHOD(query) {
    CHECK_CONNECTION
    auto query_string = LuaValue::Pop(state, 2);

    if (query_string.type() != Type::STRING) {
      LUA->ThrowError("pg - query string is invalid\n");
      return 0;
    }

    std::shared_ptr<DatabaseQuery> query_obj = DatabaseQuery::Make(query_string);
    query_obj->_set_connection(obj->_connection);
    query_obj->type_map = obj->_type_map;

    return query_obj->Push(state);
  }

  LUA_METHOD(query_prepared) {
    CHECK_CONNECTION
    auto name = LuaValue::Pop(state, 2);

    if (name.type() != Type::STRING) {
      LUA->ThrowError("pg - prepared query name is invalid\n");
      return 0;
    }

    std::shared_ptr<PreparedQuery> query_obj = PreparedQuery::Make(name);
    query_obj->_set_connection(obj->_connection);

    return query_obj->Push(state);
  }

  LUA_METHOD(connect) {
    auto obj      = Pop(state, 1);
    auto hostname = LuaValue::Pop(state, 2);
    auto username = LuaValue::Pop(state, 3);
    auto password = LuaValue::Pop(state, 4);
    auto database = LuaValue::Pop(state, 5);
    auto port     = LuaValue::Pop(state, 6);
    auto extra    = LuaValue::Pop(state, 7);
    std::string connection_string = "";

    if (hostname.type() == Type::STRING)
      connection_string += "hostaddr=" + std::string(hostname);
    else
      connection_string += "hostaddr=127.0.0.1";

    if (username.type() == Type::STRING)
      connection_string += " user=" + std::string(username);
    else
      connection_string += " user=postgres";

    if (password.type() == Type::STRING)
      connection_string += " password=" + std::string(password);

    if (database.type() == Type::STRING)
      connection_string += " dbname=" + std::string(database);

    if (port.type() == Type::STRING)
      connection_string += " port=" + std::string(port);
    else if (port.type() == Type::NUMBER) {
      int _port = port;
      connection_string += " port=" + std::to_string(_port);
    }

    if (extra.type() == Type::STRING)
      connection_string += " " + std::string(extra);

    try {
      obj->_connection = new pqxx::connection(connection_string);

      // build type maps
      pqxx::work wrk(*obj->_connection);
      pqxx::result res = wrk.exec("select oid, typcategory from pg_type;");
      wrk.commit();

      if (res.size() > 0) {
        for (auto row : res) {
          int res_id = row[0].as<int>();

          if (res_id < MAX_PG_TYPES)
            obj->_type_map[res_id] = row[1].c_str()[0];
          else
            throw std::runtime_error((std::string("type oid out-of-range: ") + row[1].c_str()).c_str());
        }
      } else
        throw std::runtime_error("PostgreSQL server does not appear to have any types (version too old?)");
    } catch (const std::exception &e) {
      LUA->PushBool(false);
      LUA->PushString(e.what());
      return 2;
    }

    LUA->PushBool(true);
    return 1;
  }

  LUA_METHOD(escape) {
    CHECK_CONNECTION

    auto value = LuaValue::Pop(state, 2);

    if (value.type() == Type::STRING) {
      std::string _txt = value;
      LUA->PushString(obj->_connection->esc(_txt).c_str());
      return 1;
    }

    return 0;
  }

  LUA_METHOD(unescape) {
    CHECK_CONNECTION

    auto value = LuaValue::Pop(state, 2);

    if (value.type() == Type::STRING) {
      std::string _txt = value;
      LUA->PushString(obj->_connection->unesc_raw(_txt).c_str());
      return 1;
    }

    return 0;
  }

  LUA_METHOD(quote) {
    CHECK_CONNECTION

    auto value = LuaValue::Pop(state, 2);

    if (value.type() == Type::STRING) {
      std::string _txt = value;
      LUA->PushString(obj->_connection->quote(_txt).c_str());
      return 1;
    }

    return 0;
  }

  LUA_METHOD(quote_name) {
    CHECK_CONNECTION

    auto value = LuaValue::Pop(state, 2);

    if (value.type() == Type::STRING) {
      std::string _txt = value;
      LUA->PushString(obj->_connection->quote_name(_txt).c_str());
      return 1;
    }

    return 0;
  }

  LUA_METHOD(disconnect) {
    CHECK_CONNECTION

    try {
      obj->_connection->disconnect();
    } catch (std::exception& e) {
      LUA->PushBool(false);
      LUA->PushString(e.what());
      return 2;
    }

    LUA->PushBool(true);
    return 1;
  }

  LUA_METHOD(activate) {
    CHECK_CONNECTION

    try {
      obj->_connection->activate();
    } catch (std::exception& e) {
      LUA->PushBool(false);
      LUA->PushString(e.what());
      return 2;
    }

    LUA->PushBool(true);
    return 1;
  }

  LUA_METHOD(deactivate) {
    CHECK_CONNECTION

    try {
      obj->_connection->deactivate();
    } catch (std::exception& e) {
      LUA->PushBool(false);
      LUA->PushString(e.what());
      return 2;
    }

    LUA->PushBool(true);
    return 1;
  }

  LUA_METHOD(is_open) {
    CHECK_CONNECTION
    LUA->PushBool(obj->_connection->is_open());
    return 1;
  }

  LUA_METHOD(protocol_version) {
    CHECK_CONNECTION
    LUA->PushNumber(obj->_connection->protocol_version());
    return 1;
  }

  LUA_METHOD(server_version) {
    CHECK_CONNECTION
    LUA->PushNumber(obj->_connection->server_version());
    return 1;
  }

  LUA_METHOD(cancel) {
    CHECK_CONNECTION

    try {
      obj->_connection->cancel_query();
    } catch (std::exception& e) {
      LUA->PushBool(false);
      LUA->PushString(e.what());
      return 2;
    }

    LUA->PushBool(true);
    return 1;
  }

  LUA_METHOD(prepare) {
    CHECK_CONNECTION

    auto name = LuaValue::Pop(state, 2);
    auto definition = LuaValue::Pop(state, 2);

    try {
      name.AssertType(Type::STRING);
      definition.AssertType(Type::STRING);
      obj->_connection->prepare(name, definition);
    } catch (std::exception &e) {
      LUA->PushBool(false);
      LUA->PushString(e.what());
      return 2;
    }

    LUA->PushBool(true);
    return 1;
  }

  LUA_METHOD(unprepare) {
    CHECK_CONNECTION

    auto name = LuaValue::Pop(state, 2);

    try {
      name.AssertType(Type::STRING);
      obj->_connection->unprepare(name);
    } catch (std::exception &e) {
      LUA->PushBool(false);
      LUA->PushString(e.what());
      return 2;
    }

    LUA->PushBool(true);
    return 1;
  }

  LUA_METHOD(set_encoding) {
    CHECK_CONNECTION

    auto new_encoding = LuaValue::Pop(state, 2);

    if (new_encoding.type() == Type::STRING) {
      std::string _new_encoding = new_encoding;
      try {
        pqxx::work wrk(*obj->_connection);
        pqxx::result res = wrk.exec("SET CLIENT_ENCODING TO '" + _new_encoding + "';");
        wrk.commit();
      } catch (const std::exception &e) {
        LUA->PushBool(false);
        LUA->PushString(e.what());
        return 2;
      }
      LUA->PushBool(true);
      return 1;
    }

    LUA->PushBool(false);
    LUA->PushBool("invalid encoding");
    return 2;
  }

  STRING_GETTER(_host, get_host)
  STRING_GETTER(_database, get_database)
  STRING_GETTER(_user, get_user)
  STRING_GETTER(_password, get_password)
  STRING_GETTER(_port, get_port)
};

#endif
