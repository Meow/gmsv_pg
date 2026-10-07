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
  // The events of the connection itself come when they like, with no query
  // around, and its userdata has to be there for them. It is kept for as long
  // as the session listens: _self stands for it then and is 0 otherwise.
  std::shared_ptr<LuaEventEmitterManager> _events;
  LuaEventEmitterManager::kept_t _self = 0;

  // Keeps the userdata at stack position 1 if it is not kept yet. Returns
  // what takes the events of the session to its listeners.
  Session::emitter_t hold(ILuaBase *LUA) {
    if (!_self) {
      _events = LuaEventEmitterManager::Current(LUA);
      _self = _events->Keep(LUA, 1);
    }

    return [events = _events, self = _self](const char *name, std::vector<LuaValue> args) {
      events->EmitKept(self, name, std::move(args));
    };
  }

  // Lets go of the userdata if the session listens to nothing, which is to
  // be seen to after everything that may have changed that. The events that
  // are still on their way are not delivered then.
  void settle(ILuaBase *LUA) {
    if (!_self || _session->Listening())
      return;

    _events->Release(LUA, _self);
    _events.reset();
    _self = 0;
  }

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

  // A value the way the connection string takes any: in quotes, which are
  // what an empty one or one with spaces in it needs, with a backslash in
  // front of the two characters that mean something inside of them.
  static std::string conninfo_value(const std::string &value) {
    std::string quoted = "'";

    for (char c : value) {
      if (c == '\'' || c == '\\')
        quoted += '\\';

      quoted += c;
    }

    quoted += '\'';
    return quoted;
  }

  // The value of a hex digit, or -1 if it is not one.
  static int hex_value(char digit) {
    if (digit >= '0' && digit <= '9')
      return digit - '0';

    if (digit >= 'a' && digit <= 'f')
      return digit - 'a' + 10;

    if (digit >= 'A' && digit <= 'F')
      return digit - 'A' + 10;

    return -1;
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
    AddMethod("escape_bytea", escape_bytea);
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
    AddMethod("listen", listen);
    AddMethod("unlisten", unlisten);
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
    // Without a timeout a server that does not answer would stall the main
    // thread for minutes. This goes first, so that extra can change it.
    std::string connection_string = "connect_timeout=5 ";

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

    connection_string += "host=" + conninfo_value(obj->_host);
    connection_string += " user=" + conninfo_value(obj->_user);

    if (password.type() == Type::String)
      connection_string += " password=" + conninfo_value(obj->_password);

    if (database.type() == Type::String)
      connection_string += " dbname=" + conninfo_value(obj->_database);

    if (!obj->_port.empty())
      connection_string += " port=" + conninfo_value(obj->_port);

    // Goes last, because libpq uses the last occurrence of a keyword: what is
    // in extra overrides everything above. A hostaddr in it is the address
    // that gets connected to, whatever the host is. Unlike the arguments, it
    // is a piece of connection string already and goes in as it is.
    if (extra.type() == Type::String)
      connection_string += " " + std::string(extra);

    int results = attempt(LUA, [&] { obj->_session->Connect(connection_string); });

    // The channels stay with the database that was connected to before.
    obj->settle(LUA);
    return results;
  }

  LUA_METHOD(escape) {
    auto obj = Pop(LUA, 1);
    obj->_session->Peek();

    if (!LUA->IsType(2, Type::String))
      return 0;

    push_string(LUA, obj->_session->Escape(check_string(LUA, 2, "")));
    return 1;
  }

  // The counterpart of unescape: binary data as the text that a bytea takes,
  // be it as a parameter or, quoted, inside of a query. That is its hex
  // format, the one that servers send unless bytea_output tells them not to.
  LUA_METHOD(escape_bytea) {
    Pop(LUA, 1)->_session->Peek();

    if (!LUA->IsType(2, Type::String))
      return 0;

    static const char digits[] = "0123456789abcdef";
    std::string raw = check_string(LUA, 2, "");
    std::string escaped = "\\x";

    escaped.reserve(2 + raw.size() * 2);

    for (unsigned char byte : raw) {
      escaped += digits[byte >> 4];
      escaped += digits[byte & 0x0F];
    }

    push_string(LUA, escaped);
    return 1;
  }

  LUA_METHOD(unescape) {
    Pop(LUA, 1)->_session->Peek();

    if (!LUA->IsType(2, Type::String))
      return 0;

    std::string escaped = check_string(LUA, 2, "");
    std::string raw;

    if (escaped.size() < 2 || escaped[0] != '\\' || escaped[1] != 'x')
      throw std::invalid_argument("pg - binary data has to start with \\x");

    if (escaped.size() % 2 != 0)
      throw std::invalid_argument("pg - binary data is cut off");

    raw.reserve(escaped.size() / 2 - 1);

    for (size_t i = 2; i < escaped.size(); i += 2) {
      int high = hex_value(escaped[i]), low = hex_value(escaped[i + 1]);

      if (high < 0 || low < 0)
        throw std::invalid_argument("pg - binary data has something other than hex digits in it");

      raw += (char)(high << 4 | low);
    }

    push_string(LUA, raw);
    return 1;
  }

  LUA_METHOD(quote) {
    auto obj = Pop(LUA, 1);
    obj->_session->Peek();

    if (!LUA->IsType(2, Type::String))
      return 0;

    push_string(LUA, obj->_session->Quote(check_string(LUA, 2, "")));
    return 1;
  }

  LUA_METHOD(quote_name) {
    auto obj = Pop(LUA, 1);
    obj->_session->Peek();

    if (!LUA->IsType(2, Type::String))
      return 0;

    push_string(LUA, obj->_session->QuoteName(check_string(LUA, 2, "")));
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
    LUA->PushNumber(PQprotocolVersion(Pop(LUA, 1)->_session->Peek()->conn));
    return 1;
  }

  LUA_METHOD(server_version) {
    LUA->PushNumber(PQserverVersion(Pop(LUA, 1)->_session->Peek()->conn));
    return 1;
  }

  LUA_METHOD(cancel) {
    auto obj = Pop(LUA, 1);
    obj->_session->Peek();

    return attempt(LUA, [&] { obj->_session->Cancel(); });
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

  LUA_METHOD(listen) {
    auto obj = Pop(LUA, 1);
    obj->_session->Peek();

    int results = attempt(LUA, [&] {
      std::string channel = check_string(LUA, 2, "pg - channel name is invalid");

      // Held before the server is asked: a notification may be there before
      // this returns.
      obj->_session->Listen(channel, obj->hold(LUA));
    });

    // Not if that did not work and there is no other channel.
    obj->settle(LUA);
    return results;
  }

  LUA_METHOD(unlisten) {
    auto obj = Pop(LUA, 1);
    obj->_session->Peek();

    int results = attempt(LUA, [&] {
      obj->_session->Unlisten(check_string(LUA, 2, "pg - channel name is invalid"));
    });

    obj->settle(LUA);
    return results;
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
