#ifndef _SESSION_HPP
#define _SESSION_HPP

#include "interfaces.h"

using namespace GarrysMod::Lua;

// What is left of a query once it ran.
struct QueryResult {
  bool success = false;
  std::string error;
  LuaValue::table_t rows;
  int size = 0;
};

// The connection to the database, shared by a DatabaseConnection and the
// queries that were made from it, together with the thread that runs queries
// in the background.
//
// libpq wants a connection to be used by one thread at a time. Everything that
// talks to the server holds _mtx while it does, so queries run one by one, and
// a synchronous query waits for the asynchronous one that is running.
//
// Sessions are created and destroyed by the main thread only.
class Session {
public:
  typedef std::function<pqxx::result(pqxx::work &)> statement_t;
  typedef std::function<void(QueryResult)> callback_t;
private:
  std::mutex _mtx;
  // Guards the pointer, not the connection, see Peek.
  std::mutex _connection_mtx;
  std::shared_ptr<pqxx::connection> _connection;
  std::string _options;
  std::string _encoding;
  std::map<std::string, std::string> _prepared;
  std::unordered_map<pqxx::oid, char> _types;
  // Closed on purpose, as opposed to a connection that was lost.
  bool _closed = false;
  // Whether the client encoding is UTF8, for escaping.
  std::atomic<bool> _utf8{false};

  std::mutex _queue_mtx;
  std::condition_variable _queue_cv;
  std::deque<std::function<void()>> _queue;
  std::thread _worker;
  bool _finishing = false;

  static std::set<Session *> &sessions() {
    static std::set<Session *> all;
    return all;
  }

  static constexpr long long MAX_EXACT = 1LL << 53;

  static std::runtime_error no_connection() {
    return std::runtime_error("pg - no connection, connect to a database first.");
  }

  // Connects with the options, encoding and prepared statements on record.
  // Call with _mtx held.
  void open() {
    auto connection = std::make_shared<pqxx::connection>(_options);
    std::unordered_map<pqxx::oid, char> types;

    if (!_encoding.empty())
      connection->set_client_encoding(_encoding);

    // The category of a type decides which Lua type its values end up as.
    {
      pqxx::work work(*connection);
      pqxx::result res = work.exec("select oid, typcategory from pg_type");
      work.commit();

      for (auto row : res) {
        std::string_view category = row[1].view();

        if (!category.empty())
          types[row[0].as<pqxx::oid>()] = category[0];
      }
    }

    // The server forgets prepared statements along with the connection. One
    // that can no longer be prepared, say because its table is gone, must not
    // keep the connection from coming back: it fails once it is run instead.
    for (const auto &statement : _prepared) {
      try {
        connection->prepare(statement.first, statement.second);
      } catch (const pqxx::sql_error &) {
      }
    }

    note_encoding(*connection);

    std::lock_guard<std::mutex> lock(_connection_mtx);
    _connection = std::move(connection);
    _types = std::move(types);
  }

  // Queries are free to change the client encoding. Call with _mtx held.
  void note_encoding(const pqxx::connection &connection) {
    try {
      _utf8 = connection.get_client_encoding() == "UTF8";
    } catch (const std::exception &) {
      _utf8 = false;
    }
  }

  // Whether libpq is going to find the text well formed UTF8. This has to be
  // as strict as libpq is, see escaping.
  static bool valid_utf8(std::string_view text) {
    auto at = reinterpret_cast<const unsigned char *>(text.data());
    size_t left = text.size();

    while (left > 0) {
      unsigned char lead = at[0], low = 0x80, high = 0xBF;
      size_t length;

      if (lead < 0x80) {
        at++;
        left--;
        continue;
      }

      if (lead >= 0xC2 && lead <= 0xDF)
        length = 2;
      else if (lead >= 0xE0 && lead <= 0xEF)
        length = 3;
      else if (lead >= 0xF0 && lead <= 0xF4)
        length = 4;
      else
        return false;

      if (left < length)
        return false;

      // Neither overlong forms, nor surrogates, nor anything past U+10FFFF.
      if (lead == 0xE0)
        low = 0xA0;
      else if (lead == 0xED)
        high = 0x9F;
      else if (lead == 0xF0)
        low = 0x90;
      else if (lead == 0xF4)
        high = 0x8F;

      if (at[1] < low || at[1] > high)
        return false;

      for (size_t i = 2; i < length; i++) {
        if (at[i] < 0x80 || at[i] > 0xBF)
          return false;
      }

      at += length;
      left -= length;
    }

    return true;
  }

  static bool ascii(std::string_view text) {
    for (unsigned char c : text) {
      if (c >= 0x80)
        return false;
    }

    return true;
  }

  // To be held while libpq escapes the text. Escaping does not wait for the
  // query that may be running, building a query must not stall the main thread
  // on one. That is fine as long as libpq only reads the connection for it,
  // which is the case for text that is well formed. For text that is not it
  // writes an error message into the connection, which the running query may
  // be doing just then, so such text does wait: the lock that is returned is
  // locked. What is well formed is only known here for UTF8, in any other
  // encoding that goes for everything but ASCII.
  std::unique_lock<std::mutex> escaping(std::string_view text) {
    std::unique_lock<std::mutex> lock(_mtx, std::defer_lock);

    if (!(_utf8 ? valid_utf8(text) : ascii(text)))
      lock.lock();

    return lock;
  }

  // The connection, ready for use. libpqxx no longer reconnects by itself, so
  // a connection that was lost is replaced here. Call with _mtx held.
  pqxx::connection &ready() {
    if (!_connection)
      throw no_connection();

    if (!_connection->is_open()) {
      if (_closed)
        throw std::runtime_error("pg - connection is closed, connect to the database again.");

      open();
    }

    return *_connection;
  }

  // Not everything in the numeric category reads as a number, think of money
  // or regclass. Those are left as they are.
  static LuaValue number(std::string_view text) {
    double value = 0;
    const char *end = text.data() + text.size();

    // Neither does every number fit a Lua number, which holds whole numbers
    // up to 2^53 exactly. Beyond that it would be rounded to another number,
    // which a 64-bit SteamID does not survive, so those stay text as well.
    long long whole = 0;
    auto parsed_whole = std::from_chars(text.data(), end, whole);

    if (parsed_whole.ptr == end && (parsed_whole.ec != std::errc() || whole > MAX_EXACT || whole < -MAX_EXACT))
      return LuaValue(std::string(text));

    auto parsed = std::from_chars(text.data(), end, value);

    if (parsed.ec == std::errc() && parsed.ptr == end)
      return LuaValue(value);

    return LuaValue(std::string(text));
  }

  // Builds the table that Lua gets: a list of rows, each a table of its
  // fields by column name. Call with _mtx held.
  LuaValue::table_t convert(const pqxx::result &result) const {
    LuaValue::table_t rows;
    const int columns = (int)result.columns();
    std::vector<LuaValue> names;
    std::vector<char> categories;

    for (int column = 0; column < columns; column++) {
      auto type = _types.find(result.column_type(column));

      names.emplace_back(result.column_name(column));
      categories.push_back(type != _types.end() ? type->second : '\0');
    }

    int i = 1; // since Lua tables start at 1

    for (auto row : result) {
      LuaValue::table_t fields;

      for (int column = 0; column < columns; column++) {
        auto field = row[column];

        // NULL is nil, which is the same as not being in the table.
        if (field.is_null())
          continue;

        switch (categories[column]) {
        case TYPCATEGORY_BOOLEAN:
          fields.emplace(names[column], field.as<bool>());
          break;
        case TYPCATEGORY_NUMERIC:
          fields.emplace(names[column], number(field.view()));
          break;
        default:
          fields.emplace(names[column], std::string(field.view()));
        }
      }

      rows.emplace_hint(rows.end(), i++, std::move(fields));
    }

    return rows;
  }

  void work() {
    for (;;) {
      std::function<void()> job;

      {
        std::unique_lock<std::mutex> lock(_queue_mtx);
        _queue_cv.wait(lock, [this] { return _finishing || !_queue.empty(); });

        // Queries that were queued before Finish still get to run.
        if (_queue.empty())
          return;

        job = std::move(_queue.front());
        _queue.pop_front();
      }

      // Nothing is there to catch an exception that leaves the thread.
      try {
        job();
      } catch (...) {
      }
    }
  }
public:
  Session() { sessions().insert(this); }

  ~Session() {
    sessions().erase(this);
    Finish();
  }

  Session(const Session &) = delete;
  Session &operator=(const Session &) = delete;

  // Connects to the database, replacing the current connection if any. If
  // that fails, the current connection stays the way it is.
  void Connect(std::string options) {
    std::lock_guard<std::mutex> lock(_mtx);

    std::swap(_options, options);
    auto encoding = std::exchange(_encoding, {});
    auto prepared = std::exchange(_prepared, {});

    try {
      open();
    } catch (...) {
      _options = std::move(options);
      _encoding = std::move(encoding);
      _prepared = std::move(prepared);
      throw;
    }

    _closed = false;
  }

  void Disconnect() {
    std::lock_guard<std::mutex> lock(_mtx);

    if (!_connection)
      throw no_connection();

    _closed = true;
    _connection->close();
  }

  // Makes sure that the connection is open.
  void Activate() {
    std::lock_guard<std::mutex> lock(_mtx);

    if (!_connection)
      throw no_connection();

    _closed = false;
    ready();
  }

  // Closes the connection until something needs it again.
  void Deactivate() {
    std::lock_guard<std::mutex> lock(_mtx);

    if (!_connection)
      throw no_connection();

    _closed = false;
    _connection->close();
  }

  void Prepare(const std::string &name, const std::string &definition) {
    // The statement without a name is the one that a query with parameters
    // uses, it would not last.
    if (name.empty())
      throw std::invalid_argument("pg - prepared query name is empty");

    std::lock_guard<std::mutex> lock(_mtx);

    ready().prepare(name, definition);
    _prepared[name] = definition;
  }

  void Unprepare(const std::string &name) {
    std::lock_guard<std::mutex> lock(_mtx);
    auto &connection = ready();

    // Off the record first: whether the server still knew the statement or
    // not, it is not to come back with the next connection.
    _prepared.erase(name);
    connection.unprepare(name);
  }

  void SetEncoding(const std::string &encoding) {
    std::lock_guard<std::mutex> lock(_mtx);
    auto &connection = ready();

    connection.set_client_encoding(encoding);
    _encoding = encoding;
    note_encoding(connection);
  }

  std::string Escape(const std::string &text) {
    auto lock = escaping(text);

    return Peek()->esc(text);
  }

  std::string Quote(const std::string &text) {
    auto lock = escaping(text);

    return Peek()->quote(text);
  }

  std::string QuoteName(const std::string &text) {
    auto lock = escaping(text);
    auto connection = Peek();

    if (lock.owns_lock())
      return connection->quote_name(text);

    // libpq clears the error message of the connection for this one, whatever
    // the text is, so it only gets the text that waited for the running query.
    // What it does to well formed text is simple enough. Like everything that
    // goes to the server, the text ends at the first zero byte.
    std::string quoted = "\"";

    for (char c : std::string_view(text).substr(0, text.find('\0'))) {
      if (c == '"')
        quoted += c;

      quoted += c;
    }

    quoted += '"';
    return quoted;
  }

  // The connection, for what does not send anything to the server: escaping,
  // looking at its state, cancelling. This does not wait for the query that
  // may be running, building a query must not stall the main thread on one.
  std::shared_ptr<pqxx::connection> Peek() {
    std::lock_guard<std::mutex> lock(_connection_mtx);

    if (!_connection)
      throw no_connection();

    return _connection;
  }

  // Runs the statement in a transaction of its own and waits for the result.
  QueryResult Execute(const statement_t &statement) {
    QueryResult result;
    std::lock_guard<std::mutex> lock(_mtx);

    try {
      pqxx::work work(ready());
      pqxx::result res = statement(work);
      work.commit();

      result.size = (int)res.size();
      result.rows = convert(res);
      result.success = true;
    } catch (const std::exception &e) {
      result.error = e.what();
    }

    if (_connection)
      note_encoding(*_connection);

    return result;
  }

  // Runs the statement on the worker thread, after the ones queued before it.
  // done is called on that thread too.
  void Enqueue(statement_t statement, callback_t done) {
    if (!_worker.joinable())
      _worker = std::thread(&Session::work, this);

    {
      std::lock_guard<std::mutex> lock(_queue_mtx);

      _queue.push_back([this, statement = std::move(statement), done = std::move(done)] {
        done(Execute(statement));
      });
    }

    _queue_cv.notify_one();
  }

  // Waits for the queued queries to finish and stops the worker thread.
  void Finish() {
    if (!_worker.joinable())
      return;

    {
      std::lock_guard<std::mutex> lock(_queue_mtx);
      _finishing = true;
    }

    _queue_cv.notify_all();
    _worker.join();
    _finishing = false;
  }

  // For when the module is closed: no thread may be left running its code,
  // and the queries that are still queued should not be lost.
  static void Shutdown() {
    for (Session *session : sessions()) {
      session->Finish();

      std::lock_guard<std::mutex> lock(session->_mtx);

      if (session->_connection) {
        session->_closed = true;
        session->_connection->close();
      }
    }
  }
};

// Runs the statement for the query at stack position 1, the way it is set to:
// either right away, returning the result, or in the background, emitting
// "success" or "error" once done.
inline int run_statement(ILuaBase *LUA, Session &session, bool sync, Session::statement_t statement) {
  if (sync) {
    QueryResult result = session.Execute(statement);

    if (!result.success) {
      LUA->PushBool(false);
      push_string(LUA, result.error);
      return 2;
    }

    LUA->PushBool(true);
    LuaValue(std::move(result.rows)).Push(LUA);
    LUA->PushNumber(result.size);
    return 3;
  }

  auto events = LuaEventEmitterManager::Current(LUA);
  // The query has to stay around until its listeners were called, even if
  // nothing in Lua refers to it anymore.
  int self = events->Hold(LUA, 1);

  try {
    session.Enqueue(std::move(statement), [events, self](QueryResult result) {
      std::vector<LuaValue> args;

      if (result.success) {
        args.emplace_back(std::move(result.rows));
        args.emplace_back(result.size);
        events->Emit(self, "success", std::move(args));
      } else {
        args.emplace_back(std::move(result.error));
        events->Emit(self, "error", std::move(args));
      }
    });
  } catch (...) {
    LUA->ReferenceFree(self);
    throw;
  }

  return 0;
}

#endif
