#ifndef _CONNECTION_H
#define _CONNECTION_H

#include "interfaces.h"
#include "session.hpp"
#include "query.hpp"
#include "prepared_query.hpp"

using namespace GarrysMod::Lua;

class DatabaseConnection : public LuaEventEmitter<DatabaseConnection> {
private:
  std::string _host;
  std::string _database;
  std::string _user;
  std::string _password;
  std::string _port;
  std::shared_ptr<Session> _session = std::make_shared<Session>();

  // Runs fn, which is something that either works or throws, and tells Lua
  // which one it was: true, or false and the error message.
  template<typename Fn>
  static int attempt(ILuaBase *LUA, Fn fn) {
    try {
      fn();
    } catch (const std::exception &e) {
      LUA->PushBool(false);
      LUA->PushString(e.what());
      return 2;
    }

    LUA->PushBool(true);
    return 1;
  }
public:
  std::string name() override { return "#<DatabaseConnection>"; }
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
public:
  LUA_METHOD(create) {
    return Make()->Push(LUA);
  }

  LUA_METHOD(query) {
    auto obj = Pop(LUA, 1);
    obj->_session->Peek();

    return DatabaseQuery::Make(obj->_session, check_string(LUA, 2, "pg - query string is invalid"))->Push(LUA);
  }

  LUA_METHOD(query_prepared) {
    auto obj = Pop(LUA, 1);
    obj->_session->Peek();

    return PreparedQuery::Make(obj->_session, check_string(LUA, 2, "pg - prepared query name is invalid"))->Push(LUA);
  }

  LUA_METHOD(connect) {
    auto obj      = Pop(LUA, 1);
    auto hostname = LuaValue::Pop(LUA, 2);
    auto username = LuaValue::Pop(LUA, 3);
    auto password = LuaValue::Pop(LUA, 4);
    auto database = LuaValue::Pop(LUA, 5);
    auto port     = LuaValue::Pop(LUA, 6);
    auto extra    = LuaValue::Pop(LUA, 7);
    std::string connection_string = "";

    obj->_host = hostname.type() == Type::String ? std::string(hostname) : "127.0.0.1";
    obj->_user = username.type() == Type::String ? std::string(username) : "postgres";
    obj->_password = password.type() == Type::String ? std::string(password) : "";
    obj->_database = database.type() == Type::String ? std::string(database) : "";

    if (port.type() == Type::String)
      obj->_port = std::string(port);
    else if (port.type() == Type::Number)
      obj->_port = std::to_string((int)port);
    else
      obj->_port = "";

    connection_string += "host=" + obj->_host;
    connection_string += " user=" + obj->_user;

    if (password.type() == Type::String)
      connection_string += " password=" + obj->_password;

    if (database.type() == Type::String)
      connection_string += " dbname=" + obj->_database;

    if (!obj->_port.empty())
      connection_string += " port=" + obj->_port;

    // Goes last, because libpq uses the last occurrence of a keyword: what is
    // in extra overrides the arguments above. A hostaddr in it is the address
    // that gets connected to, whatever the host is.
    if (extra.type() == Type::String)
      connection_string += " " + std::string(extra);

    return attempt(LUA, [&] { obj->_session->Connect(connection_string); });
  }

  LUA_METHOD(escape) {
    auto connection = Pop(LUA, 1)->_session->Peek();

    if (!LUA->IsType(2, Type::String))
      return 0;

    push_string(LUA, connection->esc(check_string(LUA, 2, "")));
    return 1;
  }

  LUA_METHOD(unescape) {
    auto connection = Pop(LUA, 1)->_session->Peek();

    if (!LUA->IsType(2, Type::String))
      return 0;

    pqxx::bytes raw = connection->unesc_bin(check_string(LUA, 2, ""));
    push_string(LUA, std::string_view(reinterpret_cast<const char *>(raw.data()), raw.size()));
    return 1;
  }

  LUA_METHOD(quote) {
    auto connection = Pop(LUA, 1)->_session->Peek();

    if (!LUA->IsType(2, Type::String))
      return 0;

    push_string(LUA, connection->quote(check_string(LUA, 2, "")));
    return 1;
  }

  LUA_METHOD(quote_name) {
    auto connection = Pop(LUA, 1)->_session->Peek();

    if (!LUA->IsType(2, Type::String))
      return 0;

    push_string(LUA, connection->quote_name(check_string(LUA, 2, "")));
    return 1;
  }

  LUA_METHOD(disconnect) {
    auto obj = Pop(LUA, 1);
    obj->_session->Peek();

    return attempt(LUA, [&] { obj->_session->Disconnect(); });
  }

  LUA_METHOD(activate) {
    auto obj = Pop(LUA, 1);
    obj->_session->Peek();

    return attempt(LUA, [&] { obj->_session->Activate(); });
  }

  LUA_METHOD(deactivate) {
    auto obj = Pop(LUA, 1);
    obj->_session->Peek();

    return attempt(LUA, [&] { obj->_session->Deactivate(); });
  }

  LUA_METHOD(is_open) {
    LUA->PushBool(Pop(LUA, 1)->_session->Peek()->is_open());
    return 1;
  }

  LUA_METHOD(protocol_version) {
    LUA->PushNumber(Pop(LUA, 1)->_session->Peek()->protocol_version());
    return 1;
  }

  LUA_METHOD(server_version) {
    LUA->PushNumber(Pop(LUA, 1)->_session->Peek()->server_version());
    return 1;
  }

  LUA_METHOD(cancel) {
    auto connection = Pop(LUA, 1)->_session->Peek();

    return attempt(LUA, [&] { connection->cancel_query(); });
  }

  LUA_METHOD(prepare) {
    auto obj = Pop(LUA, 1);
    obj->_session->Peek();

    return attempt(LUA, [&] {
      std::string name = check_string(LUA, 2, "pg - prepared query name is invalid");
      std::string definition = check_string(LUA, 3, "pg - prepared query definition is invalid");

      obj->_session->Prepare(name, definition);
    });
  }

  LUA_METHOD(unprepare) {
    auto obj = Pop(LUA, 1);
    obj->_session->Peek();

    return attempt(LUA, [&] {
      obj->_session->Unprepare(check_string(LUA, 2, "pg - prepared query name is invalid"));
    });
  }

  LUA_METHOD(set_encoding) {
    auto obj = Pop(LUA, 1);
    obj->_session->Peek();

    return attempt(LUA, [&] {
      obj->_session->SetEncoding(check_string(LUA, 2, "invalid encoding"));
    });
  }

  STRING_GETTER(_host, get_host)
  STRING_GETTER(_database, get_database)
  STRING_GETTER(_user, get_user)
  STRING_GETTER(_password, get_password)
  STRING_GETTER(_port, get_port)
};

#endif
