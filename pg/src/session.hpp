#ifndef _SESSION_HPP
#define _SESSION_HPP

#include "interfaces.h"
#include "params.hpp"

using namespace GarrysMod::Lua;

// What the server had to say while a query ran that is no error: a warning,
// or what RAISE NOTICE puts out.
struct Notice {
  std::string message;
  // The fields that the server sent along with it, the ones an error has too.
  LuaValue::table_t details;
};

// What is left of a query once it ran.
struct QueryResult {
  bool success = false;
  std::string error;
  // What is known about the error beyond its message: the fields that the
  // server sent along with it, and whether the connection is gone.
  LuaValue::table_t details;
  LuaValue::table_t rows;
  int size = 0;
  // The number of rows that the command inserted, updated, deleted or, for a
  // select, returned.
  double affected = 0;
  // In the order they arrived, all of them before the query was over.
  std::vector<Notice> notices;
};

// A statement to run. It is nothing but data, so that whatever is about to
// run it can tell what it is going to send.
struct Statement {
  enum Kind {
    // Goes out as it is, and may be several statements in one.
    Plain,
    // A single statement that has $1, $2 and so on in it.
    Parameterised,
    // One that was prepared before, text is its name.
    Prepared
  };

  Kind kind = Plain;
  std::string text;
  std::vector<param_t> params;
};

struct result_deleter {
  void operator()(PGresult *res) const { PQclear(res); }
};

typedef std::unique_ptr<PGresult, result_deleter> result_t;

// A connection of libpq, together with what it takes to cancel its queries.
// It is never closed while something holds on to it: a session closes its
// connection by putting an empty link in its place.
struct Link {
  PGconn *conn = nullptr;
  // Cancelling goes over a connection of its own, which has a mutex of its
  // own: it is not to wait for the query that it is there to cancel.
  std::mutex cancel_mtx;
  PGcancelConn *cancel = nullptr;
  // Where the notices of the server go. There is such a place while a query
  // runs, the rest of the time they go nowhere.
  std::vector<Notice> *notices = nullptr;

  Link() = default;
  Link(const Link &) = delete;
  Link &operator=(const Link &) = delete;

  ~Link() {
    if (cancel)
      PQcancelFinish(cancel);

    if (conn)
      PQfinish(conn);
  }

  bool is_open() const { return conn && PQstatus(conn) == CONNECTION_OK; }
};

// Thrown when the connection could not be opened.
struct connection_error : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// The connection to the database, shared by a DatabaseConnection and the
// queries that were made from it, together with the thread that runs queries
// in the background and, in between, looks after a connection that listens.
//
// libpq wants a connection to be used by one thread at a time. Everything that
// talks to the server holds _mtx while it does, so queries run in the order
// they were asked for, and a synchronous query waits for the asynchronous ones
// that are on their way to the server.
//
// Sessions are created and destroyed by the main thread only.
class Session {
public:
  typedef std::function<void(QueryResult)> callback_t;
  // Takes the events of the connection itself, which no query is around for:
  // the name of one and its arguments.
  typedef std::function<void(const char *, std::vector<LuaValue>)> emitter_t;
private:
  // A statement in the queue, and where its result goes.
  struct Job {
    Statement statement;
    callback_t done;
  };

  std::mutex _mtx;
  // Held while waiting for _mtx, see turn.
  std::mutex _turn_mtx;
  // Held by a query for as long as it is inside of libpq, which it is not
  // while it waits for the server, and by the worker when it is there for the
  // sake of it, see idle. Escaping holds it instead of _mtx, see run.
  std::mutex _use_mtx;
  // Guards the pointer, not the connection, see Peek.
  std::mutex _connection_mtx;
  std::shared_ptr<Link> _connection;
  std::string _options;
  std::string _encoding;
  std::map<std::string, std::string> _prepared;
  // The channels that are listened to.
  std::set<std::string> _channels;
  // Where "notification" and "reconnect" go while there are channels. It is
  // called with _mtx held, by whichever thread has it.
  emitter_t _emit;
  std::unordered_map<Oid, char> _types;
  // Closed on purpose, as opposed to a connection that was lost.
  bool _closed = false;
  // Closed until something needs it, see Deactivate. Listening does not.
  bool _deactivated = false;
  // The worker does not try to open a lost connection again before this, see
  // idle.
  std::chrono::steady_clock::time_point _retry = std::chrono::steady_clock::time_point::min();
  // Whatever else needs the connection does not try before this. Until then
  // it fails right away, with what went wrong the last time, see reopen.
  std::chrono::steady_clock::time_point _unavailable = std::chrono::steady_clock::time_point::min();
  std::string _failure;
  // What the server ending the connection came to, if it did so in the
  // middle of a transaction and nothing has failed over it yet. The next
  // statement does, see ready_to_run.
  std::optional<std::string> _orphaned;
  // Whether the client encoding is UTF8, for escaping.
  std::atomic<bool> _utf8{false};

  std::mutex _queue_mtx;
  std::condition_variable _queue_cv;
  std::deque<Job> _queue;
  std::thread _worker;
  bool _finishing = false;
  // Whether the worker closes the connection when it finishes, see Shutdown.
  bool _closing = false;
  // Whether there are channels, for the worker and for whoever must not wait
  // for _mtx to find out. Changes with both _mtx and _queue_mtx held.
  std::atomic<bool> _listening{false};

  // How long the worker leaves a connection that listens to itself, and how
  // long it gives the server before it tries to connect once more, see idle.
  static constexpr std::chrono::milliseconds IDLE_INTERVAL{50};
  static constexpr std::chrono::seconds RETRY_INTERVAL{5};
  // How many queued statements go to the server together at most, see the run
  // that takes jobs. _mtx is held until the last of them is over, which is
  // what everything synchronous waits for instead of for a single statement.
  // That wait is to stay of the same order as it was, so this is less than
  // ten. The trips to the server are what there is to gain, and of those this
  // already saves seven in eight.
  static constexpr size_t BATCH = 8;

  static std::set<Session *> &sessions() {
    static std::set<Session *> all;
    return all;
  }

  static constexpr long long MAX_EXACT = 1LL << 53;

  static std::runtime_error no_connection() {
    return std::runtime_error("pg - no connection, connect to a database first.");
  }

  static std::runtime_error closed() {
    return std::runtime_error("pg - connection is closed, connect to the database again.");
  }

  // The messages of libpq end with a line break.
  static std::string trimmed(const char *message) {
    std::string_view text = message ? message : "";

    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' || text.back() == '\t'))
      text.remove_suffix(1);

    return std::string(text);
  }

  // What went wrong, in the words of the result if it has any, and in those
  // of the connection otherwise.
  static std::string message_of(PGconn *conn, const PGresult *res = nullptr) {
    std::string message = trimmed(res ? PQresultErrorMessage(res) : nullptr);

    if (message.empty())
      message = trimmed(PQerrorMessage(conn));

    if (message.empty())
      message = "pg - unexpected response from the server";

    return message;
  }

  // The name the way it goes into a query, in double quotes. libpq writes to
  // the connection for this. Call with _mtx held.
  static std::string quoted_name(PGconn *conn, const std::string &name) {
    char *quoted = PQescapeIdentifier(conn, name.data(), name.size());

    if (!quoted)
      throw std::invalid_argument(message_of(conn));

    std::string result = quoted;

    PQfreemem(quoted);
    return result;
  }

  // libpq calls this for every notice, in the middle of reading what the
  // server sent and on whichever thread does the reading. All that is done
  // here is to put the notice where the query that is running finds it. The
  // result it comes in does not last, hence the copy. Without this, libpq
  // prints notices.
  static void receive_notice(void *link, const PGresult *res) {
    std::vector<Notice> *notices = static_cast<Link *>(link)->notices;

    if (!notices)
      return;

    // An exception has no way through libpq.
    try {
      notices->push_back(Notice{ trimmed(PQresultErrorMessage(res)), details(res) });
    } catch (...) {
    }
  }

  // Has the notices of the connection stored for as long as it is around.
  struct Noticing {
    // The connection may be dropped meanwhile.
    std::shared_ptr<Link> link;

    Noticing(std::shared_ptr<Link> link, std::vector<Notice> &notices) : link(std::move(link)) {
      this->link->notices = &notices;
    }

    Noticing(const Noticing &) = delete;
    Noticing &operator=(const Noticing &) = delete;

    ~Noticing() { link->notices = nullptr; }

    // From here on they are those of another query.
    void to(std::vector<Notice> &notices) { link->notices = &notices; }
  };

  // Lets go of a lock for as long as it is around.
  struct Released {
    std::unique_lock<std::mutex> &lock;

    explicit Released(std::unique_lock<std::mutex> &lock) : lock(lock) { lock.unlock(); }

    Released(const Released &) = delete;
    Released &operator=(const Released &) = delete;

    ~Released() { lock.lock(); }
  };

  // Locks _mtx, after those who asked for it before. A mutex makes no such
  // promise by itself: the worker, which lets go of _mtx only to take it again
  // for the next statement, would have it back before the thread that was
  // waiting for it got to run, statement after statement, and a synchronous
  // call would wait for the whole queue. Whoever waits here keeps _turn_mtx
  // until it has _mtx, so nobody else gets to ask in the meantime.
  std::unique_lock<std::mutex> turn() {
    std::lock_guard<std::mutex> waiting(_turn_mtx);

    return std::unique_lock<std::mutex>(_mtx);
  }

  // Hands over an event of the connection itself, if anyone is there for it.
  // Call with _mtx held.
  void emit(const char *name, std::vector<LuaValue> args = {}) {
    if (!_channels.empty() && _emit)
      _emit(name, std::move(args));
  }

  struct notification_deleter {
    void operator()(PGnotify *notification) const { PQfreemem(notification); }
  };

  // Hands over the notifications that libpq has read. It keeps them until
  // they are taken, whether anyone listens or not. Call with _mtx and
  // _use_mtx held.
  void notifications(PGconn *conn) {
    while (std::unique_ptr<PGnotify, notification_deleter> notification{ PQnotifies(conn) })
      emit("notification", { notification->relname, notification->extra, notification->be_pid });
  }

  // The worker waits in another way when there are channels, see work. Call
  // with _mtx held, whenever _channels has changed.
  void note_channels() {
    {
      std::lock_guard<std::mutex> lock(_queue_mtx);
      _listening = !_channels.empty();
    }

    _queue_cv.notify_all();
  }

  // Connects with the options, encoding, prepared statements and channels on
  // record. Call with _mtx held.
  void open() {
    auto link = std::make_shared<Link>();
    std::unordered_map<Oid, char> types;
    PGconn *conn = link->conn = PQconnectdb(_options.c_str());

    if (!conn)
      throw std::bad_alloc();

    if (PQstatus(conn) != CONNECTION_OK)
      throw connection_error(message_of(conn));

    PQsetNoticeReceiver(conn, receive_notice, link.get());

    if (!_encoding.empty() && PQsetClientEncoding(conn, _encoding.c_str()) != 0)
      throw connection_error(message_of(conn));

    // The category of a type decides which Lua type its values end up as.
    {
      result_t res(PQexec(conn, "select oid, typcategory from pg_type"));

      if (PQresultStatus(res.get()) != PGRES_TUPLES_OK)
        throw connection_error(message_of(conn, res.get()));

      for (int row = 0, rows = PQntuples(res.get()); row < rows; row++) {
        std::string_view type(PQgetvalue(res.get(), row, 0));
        std::string_view category(PQgetvalue(res.get(), row, 1));
        Oid oid = 0;

        if (std::from_chars(type.data(), type.data() + type.size(), oid).ec == std::errc() && !category.empty())
          types[oid] = category[0];
      }
    }

    // The server forgets prepared statements along with the connection. One
    // that can no longer be prepared, say because its table is gone, must not
    // keep the connection from coming back: it fails once it is run instead.
    for (const auto &statement : _prepared) {
      result_t res(PQprepare(conn, statement.first.c_str(), statement.second.c_str(), 0, nullptr));

      if (!link->is_open())
        throw connection_error(message_of(conn, res.get()));
    }

    // Neither does it know what was listened to. A channel that cannot be
    // listened to again is another matter than a statement, though: nothing
    // would ever fail to tell.
    for (const auto &channel : _channels) {
      std::string query = "LISTEN " + quoted_name(conn, channel);
      result_t res(PQexec(conn, query.c_str()));

      if (PQresultStatus(res.get()) != PGRES_COMMAND_OK)
        throw connection_error(message_of(conn, res.get()));
    }

    // Made here rather than when it is needed, because making it reads the
    // connection, which a query may be busy with by then.
    link->cancel = PQcancelCreate(conn);

    if (!link->cancel)
      throw std::bad_alloc();

    note_encoding(conn);
    _deactivated = false;
    _unavailable = std::chrono::steady_clock::time_point::min();

    std::lock_guard<std::mutex> lock(_connection_mtx);
    _connection.swap(link);
    _types = std::move(types);
  }

  // Replaces a connection that is gone. Whatever was sent to the channels
  // meanwhile did not get here, which those who listen are told. Call with
  // _mtx held.
  //
  // An attempt keeps everything waiting that needs _mtx, for as long as the
  // server takes to answer or not to, and there may be a queue of statements
  // that each need the connection, or a main thread that asks for it every
  // now and then. So one that failed is the answer for a while, see ready:
  // for as long as it took. A server that is not there at all, which is
  // what takes long to find out, is then waited for half of the time at
  // most, and by one statement of a queue instead of by each. And where the
  // answer was quick to come, from a server that is starting or one that
  // does not let the user in, asking again costs next to nothing, so next to
  // nothing is turned away that would have got through.
  void reopen() {
    auto began = std::chrono::steady_clock::now();

    try {
      open();
    } catch (const connection_error &e) {
      auto ended = std::chrono::steady_clock::now();

      _failure = e.what();
      _unavailable = ended + (ended - began);
      throw;
    }

    emit("reconnect");
  }

  // Closes the connection. Call with _mtx held.
  void drop() {
    auto link = std::make_shared<Link>();

    std::lock_guard<std::mutex> lock(_connection_mtx);
    _connection.swap(link);
  }

  // Queries are free to change the client encoding. Call with _mtx held.
  void note_encoding(PGconn *conn) {
    int encoding = PQclientEncoding(conn);

    _utf8 = encoding >= 0 && std::string_view(pg_encoding_to_char(encoding)) == "UTF8";
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
  // on one. It only waits for _use_mtx, which that query lets go of whenever
  // it waits for the server. That is fine as long as libpq leaves nothing in
  // the connection for it, which is the case for text that is well formed.
  // For text that is not it writes an error message into the connection, the
  // place that the errors of the running query go to as well, so such text
  // does wait: the lock that is returned is locked. What is well formed is
  // only known here for UTF8, in any other encoding that goes for everything
  // but ASCII.
  std::unique_lock<std::mutex> escaping(std::string_view text) {
    std::unique_lock<std::mutex> lock;

    if (!(_utf8 ? valid_utf8(text) : ascii(text)))
      lock = turn();

    return lock;
  }

  // Has libpq read what the server sent while nobody was looking, without
  // waiting for more. Call with _mtx and _use_mtx held.
  //
  // libpq only finds out that the server has ended a connection when it
  // reads from it, the status it has does not change before. The server says
  // why before it hangs up, and a read may get no further than that, or than
  // something else that was sent before: over an encrypted connection it
  // gets one message at a time. So this reads for as long as there is
  // something, up to the end if the connection has one. Whether there is
  // something, the socket is asked first. On a connection that nothing has
  // happened to, which is nearly every time, that one question is all this
  // costs, and it is answered at once.
  static void catch_up(PGconn *conn) {
    int socket = PQsocket(conn);

    while (socket >= 0 && PQsocketPoll(socket, 1, 0, 0) > 0) {
      if (!PQconsumeInput(conn))
        break;
    }
  }

  // The connection, ready for use. libpq does not reconnect by itself, so a
  // connection that was lost is replaced here. Call with _mtx held, and
  // without _use_mtx.
  //
  // That includes the one that the server ended while it had nothing to do,
  // after a restart or a timeout: it is found out here, before anything is
  // sent into it, and nothing is lost by opening another. Unless it was in a
  // transaction, see ready_to_run.
  //
  // insist is for when the user asks for the connection as such: opening it
  // is tried even if that has only just failed.
  PGconn *ready(bool insist = false) {
    if (!_connection)
      throw no_connection();

    if (_connection->is_open()) {
      PGconn *conn = _connection->conn;
      bool transaction = PQtransactionStatus(conn) != PQTRANS_IDLE;
      // What the server said before it went is why.
      std::vector<Notice> said;

      {
        std::lock_guard<std::mutex> use(_use_mtx);
        Noticing noticing(_connection, said);

        catch_up(conn);
        notifications(conn);
      }

      if (!_connection->is_open() && transaction) {
        std::string error;

        for (const Notice &notice : said)
          error += notice.message + '\n';

        _orphaned = error + message_of(conn);
      }
    }

    if (!_connection->is_open()) {
      if (_closed)
        throw closed();

      if (!insist && std::chrono::steady_clock::now() < _unavailable)
        throw connection_error(_failure);

      reopen();
    }

    return _connection->conn;
  }

  // The connection, ready for a statement to run on. Call with _mtx held,
  // and without _use_mtx.
  //
  // A connection that the server ended in the middle of a transaction, with
  // nothing on its way, is not just replaced. The transaction is gone with
  // it, and the statement that comes next was going to be a part of it: run
  // on the new connection, it would stand alone, and be there to stay
  // without the rest. So that statement fails, the way one fails that is
  // lost on its way, which is how the loss gets known at all. What comes
  // after it is for those who were told.
  PGconn *ready_to_run() {
    PGconn *conn;

    try {
      conn = ready();
    } catch (const connection_error &) {
      // This says as much.
      _orphaned.reset();
      throw;
    }

    if (_orphaned) {
      connection_error error(*_orphaned);

      _orphaned.reset();
      throw error;
    }

    return conn;
  }

  // The connection for libpq to escape with. lock is the one of escaping.
  //
  // One that was lost is as good for that as it was, libpq still has what
  // the server said about how to escape. Of one that was closed nothing is
  // left to ask. If that was not for good, it is due to be opened again by
  // whatever needs it next, which is this then: it waits for _mtx after all,
  // lock is locked from there on, and the connection is opened the way it is
  // for a query. Whoever escapes before running the query that would have
  // opened it is not to be stopped short of that query.
  std::shared_ptr<Link> escapable(std::unique_lock<std::mutex> &lock) {
    auto link = Peek();

    if (link->conn)
      return link;

    if (!lock.owns_lock())
      lock = turn();

    ready();
    return _connection;
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
  LuaValue::table_t convert(const PGresult *res) const {
    LuaValue::table_t rows;
    const int columns = PQnfields(res);
    std::vector<LuaValue> names;
    std::vector<char> categories;
    // Of the columns that have the same name, the first one is what a row
    // has by that name, and the others are not in it. That goes for every
    // row, also for one where the first is NULL: the name is not to stand
    // for one column here and for another there.
    std::vector<bool> hidden;
    std::set<std::string_view> taken;

    for (int column = 0; column < columns; column++) {
      auto type = _types.find(PQftype(res, column));

      names.emplace_back(PQfname(res, column));
      categories.push_back(type != _types.end() ? type->second : '\0');
      hidden.push_back(!taken.insert(PQfname(res, column)).second);
    }

    for (int row = 0, count = PQntuples(res); row < count; row++) {
      LuaValue::table_t fields;

      for (int column = 0; column < columns; column++) {
        // NULL is nil, which is the same as not being in the table.
        if (hidden[column] || PQgetisnull(res, row, column))
          continue;

        std::string_view field(PQgetvalue(res, row, column), PQgetlength(res, row, column));

        switch (categories[column]) {
        case TYPCATEGORY_BOOLEAN:
          fields.emplace(names[column], field == "t");
          break;
        case TYPCATEGORY_NUMERIC:
          fields.emplace(names[column], number(field));
          break;
        default:
          fields.emplace(names[column], std::string(field));
        }
      }

      // Lua tables start at 1.
      rows.emplace_hint(rows.end(), row + 1, std::move(fields));
    }

    return rows;
  }

  // The fields of an error the way Lua gets them, each only if the server
  // sent it.
  static LuaValue::table_t details(const PGresult *res) {
    static const std::pair<int, const char *> fields[] = {
      { PG_DIAG_SQLSTATE, "sqlstate" },
      { PG_DIAG_SEVERITY_NONLOCALIZED, "severity" },
      { PG_DIAG_MESSAGE_PRIMARY, "message" },
      { PG_DIAG_MESSAGE_DETAIL, "detail" },
      { PG_DIAG_MESSAGE_HINT, "hint" },
      { PG_DIAG_CONTEXT, "context" },
      { PG_DIAG_STATEMENT_POSITION, "position" },
      { PG_DIAG_SCHEMA_NAME, "schema" },
      { PG_DIAG_TABLE_NAME, "table" },
      { PG_DIAG_COLUMN_NAME, "column" },
      { PG_DIAG_DATATYPE_NAME, "datatype" },
      { PG_DIAG_CONSTRAINT_NAME, "constraint" }
    };

    LuaValue::table_t table;

    for (const auto &field : fields) {
      const char *value = PQresultErrorField(res, field.first);

      if (!value)
        continue;

      if (field.first == PG_DIAG_STATEMENT_POSITION)
        table.emplace(field.second, number(value));
      else
        table.emplace(field.second, std::string(value));
    }

    return table;
  }

  // The number of rows that a command says it affected, if it says so.
  static double affected(PGresult *res) {
    std::string_view text(PQcmdTuples(res));
    double count = 0;

    std::from_chars(text.data(), text.data() + text.size(), count);
    return count;
  }

  // Waits for the server to send more of what the connection is waiting for.
  // use is let go of meanwhile. Returns whether there may be more to read.
  //
  // The connection does not block while a statement runs, so it may not have
  // sent all there is to send yet, see run. The rest goes out here, as soon
  // as the server takes it.
  static bool await(PGconn *conn, std::unique_lock<std::mutex> &use) {
    int socket = PQsocket(conn);

    if (socket < 0)
      return false;

    int unsent = PQflush(conn);

    if (unsent < 0)
      return false;

    int ready;

    use.unlock();

    // A signal is no reason to stop waiting.
    do {
      errno = 0;
      ready = PQsocketPoll(socket, 1, unsent, -1);
    } while (ready < 0 && errno == EINTR);

    use.lock();

    // On a connection that does not block, this sends as well.
    return ready >= 0 && PQconsumeInput(conn);
  }

  // The next result of the query that was sent, or nothing once it is over,
  // which it also is when the connection is lost.
  static result_t next(PGconn *conn, std::unique_lock<std::mutex> &use) {
    while (PQisBusy(conn)) {
      if (await(conn, use))
        continue;

      // libpq has said why in the error message of the connection. Asked for
      // a result now, it would try to read once more and add to that.
      if (PQstatus(conn) != CONNECTION_OK)
        return nullptr;

      break;
    }

    return result_t(PQgetResult(conn));
  }

  // Lets the data of a COPY TO STDOUT pass by. Returns whether its end was
  // reached.
  static bool skip_copy(PGconn *conn, std::unique_lock<std::mutex> &use) {
    for (;;) {
      char *data = nullptr;
      int length = PQgetCopyData(conn, &data, 1);

      if (length > 0)
        PQfreemem(data);
      else if (length < 0)
        return length == -1;
      else if (!await(conn, use))
        return false;
    }
  }

  // What has come of a statement so far, while its results are read.
  struct Outcome {
    QueryResult out;
    // Whether libpq took the statement.
    bool sent = false;
    // The last result that is no error, and the first one that is.
    result_t last, failed;
    // Why the statement was not gone through with, if it was not.
    const char *refused = nullptr;
  };

  // Hands the statement to libpq, the way its kind is sent. Without that the
  // connection has the reason.
  static void send(PGconn *conn, const Statement &statement, Outcome &outcome) {
    std::vector<const char *> values;

    for (const auto &param : statement.params)
      values.push_back(param ? param->c_str() : nullptr);

    switch (statement.kind) {
    case Statement::Plain:
      outcome.sent = PQsendQuery(conn, statement.text.c_str());
      break;
    case Statement::Parameterised:
      outcome.sent = PQsendQueryParams(conn, statement.text.c_str(), (int)values.size(), nullptr, values.data(), nullptr, nullptr, 0);
      break;
    case Statement::Prepared:
      outcome.sent = PQsendQueryPrepared(conn, statement.text.c_str(), (int)values.size(), values.data(), nullptr, nullptr, 0);
      break;
    }

    if (!outcome.sent)
      outcome.out.error = message_of(conn);
  }

  // Takes in a result that is an error.
  static void fail(PGconn *conn, Outcome &outcome, result_t res) {
    if (!outcome.out.error.empty())
      outcome.out.error += '\n';

    outcome.out.error += message_of(conn, res.get());

    if (!outcome.failed)
      outcome.failed = std::move(res);
  }

  // What the statement comes to in the end. lost is whether the connection
  // went while it was on its way. Call with _mtx held, and with use, which
  // is let go of while the rows are gone through.
  QueryResult conclude(PGconn *conn, Outcome &outcome, bool lost, std::unique_lock<std::mutex> &use) const {
    QueryResult &out = outcome.out;

    if (outcome.refused) {
      out.error = outcome.refused;
    } else if (lost) {
      // The connection has all that went wrong since the query was sent,
      // which may be more than the results had to say. It may also be less,
      // if text was escaped meanwhile.
      std::string all = message_of(conn);

      if (all.compare(0, out.error.size(), out.error) == 0)
        out.error = std::move(all);
      else
        out.error += '\n' + all;
    } else if (outcome.sent && !outcome.failed) {
      out.success = true;

      if (outcome.last) {
        // All of this is in the result and none of it in the connection, so
        // escaping does not have to wait for it, which for the rows of a
        // large result is a while. Neither does it for the result to be
        // freed.
        Released released(use);

        out.size = PQntuples(outcome.last.get());
        out.rows = convert(outcome.last.get());
        out.affected = affected(outcome.last.get());
        outcome.last.reset();
      }
    }

    if (outcome.failed && !outcome.refused)
      out.details = details(outcome.failed.get());

    if (lost)
      out.details.emplace("connection_lost", true);

    return std::move(out);
  }

  // Sends the statement and waits for what comes of it. Call with _mtx held.
  //
  // _use_mtx is held as well, but not while waiting for the server, which is
  // why the waiting is done here and not left to libpq. Escaping does not
  // wait for _mtx, and libpq clears the error message of the connection for
  // it unless it has a query under way. To libpq a query is not under way yet
  // while it is being sent, and no longer once the first result of one that
  // has parameters was read. Neither is to be cut in on.
  //
  // Sending is no time to wait for the server inside of libpq either. That
  // takes a connection that does not block for the time being. One that
  // blocks waits in pqSendSome (fe-misc.c) for the server to take what does
  // not fit the socket, with _use_mtx held: for as long as a server or a
  // network that has stopped taking data keeps it up, if the statement is a
  // large one. This way libpq keeps what the socket does not take (pqPutMsgEnd,
  // which is also what sends once there are 8 kB), and it goes out in await,
  // which waits for the socket to take more as well as to bring more. To
  // libpq the query is under way by then, it is once the statement was taken.
  //
  // Once the query is over, the rows are gone through with _use_mtx let go
  // of, see conclude. Whatever the connection is asked is asked before that:
  // what escaping does to its error message from there on is of no
  // consequence.
  QueryResult run(PGconn *conn, const Statement &statement) {
    Outcome outcome;
    std::unique_lock<std::mutex> use(_use_mtx);
    // From here on, the notices that arrive are those of this query.
    Noticing noticing(_connection, outcome.out.notices);

    if (PQsetnonblocking(conn, 1) == 0)
      send(conn, statement, outcome);
    else
      outcome.out.error = message_of(conn);

    // A plain query may be several statements, each with a result of its own.
    // The last one is what the query returns, the way PQexec has it. An error
    // ends the query, so there is one at most, unless the connection is lost
    // over it, which libpq may report as another.
    bool done = !outcome.sent;

    while (!done) {
      result_t res = next(conn, use);

      if (!res)
        break;

      switch (PQresultStatus(res.get())) {
      case PGRES_EMPTY_QUERY:
      case PGRES_COMMAND_OK:
      case PGRES_TUPLES_OK:
        outcome.last = std::move(res);
        break;
      // There is nowhere for the data of a COPY to come from or to go to. The
      // connection has to get out of it again, or the next query could not
      // be sent.
      case PGRES_COPY_IN:
        outcome.refused = "pg - COPY FROM STDIN is not supported";
        // The server answers with an error of its own, which ends the query.
        done = PQputCopyEnd(conn, outcome.refused) < 0;
        break;
      case PGRES_COPY_OUT:
        outcome.refused = "pg - COPY TO STDOUT is not supported";
        done = !skip_copy(conn, use);
        break;
      case PGRES_COPY_BOTH:
        outcome.refused = "pg - replication is not supported";
        done = true;
        break;
      default:
        fail(conn, outcome, std::move(res));

        // Nothing more is going to come.
        done = PQstatus(conn) != CONNECTION_OK;
      }
    }

    // Those that came in with the results of the query, or before it.
    notifications(conn);

    bool lost = PQstatus(conn) != CONNECTION_OK;
    // One that is not lost may still be in the middle of something that there
    // was no way out of. The next query gets a new connection. So it does if
    // the connection does not go back to blocking, which is what everything
    // else that uses it expects of it.
    bool stuck = !lost && (PQtransactionStatus(conn) == PQTRANS_ACTIVE || PQsetnonblocking(conn, 0) != 0);
    QueryResult out = conclude(conn, outcome, lost, use);

    if (stuck) {
      use.unlock();
      drop();
    }

    return out;
  }

  // Whether the text has the word COPY in it, in whatever case.
  static bool copies(std::string_view text) {
    static constexpr std::string_view word = "copy";

    for (size_t at = 0; at + word.size() <= text.size(); at++) {
      size_t same = 0;

      // The letters of a keyword cannot be written in any other way.
      while (same < word.size() && (text[at + same] | 0x20) == word[same])
        same++;

      if (same == word.size())
        return true;
    }

    return false;
  }

  // Whether the statement may go to the server together with others, see the
  // run that takes jobs. Call with _mtx held.
  //
  // A plain one may not: there is no simple query in pipeline mode, and it
  // may be several statements in one.
  //
  // Neither may COPY FROM STDIN. The server reads what the client sent as the
  // data of the COPY, and that would be the statements behind it. It ends the
  // connection over the first of them (CopyGetData in copyfromparse.c of the
  // server: any message but those of a COPY is an error in the middle of
  // reading a message, which is the loss of protocol synchronization in
  // postgres.c). So whatever could be a COPY runs alone, which is everything
  // that has the word in it. A prepared statement whose text is not known here
  // was prepared with PREPARE, which does not take a COPY.
  bool pipelined(const Statement &statement) const {
    switch (statement.kind) {
    case Statement::Parameterised:
      return !copies(statement.text);
    case Statement::Prepared: {
      auto prepared = _prepared.find(statement.text);

      return prepared == _prepared.end() || !copies(prepared->second);
    }
    default:
      return false;
    }
  }

  // How many of the jobs, from the first one on, go to the server together.
  // Call with _mtx held.
  size_t together(const std::deque<Job> &jobs) const {
    size_t count = 0;

    while (count < jobs.size() && pipelined(jobs[count].statement))
      count++;

    return count > 1 ? count : 1;
  }

  // Sends the statements of the jobs one behind the other, without waiting
  // for the result of any of them, and reads the results as they come: one
  // trip to the server and back for all of them instead of one each. done of
  // each job is called once its result is there, in their order, with _mtx
  // still held. Call with _mtx held, for statements that are pipelined.
  //
  // This is the pipeline mode of libpq. Every statement is followed by a sync
  // point of its own, which is what libpq sends behind a statement that goes
  // out alone as well: the server commits there, and an error makes it skip
  // what follows up to there and no further. So the server gets to read what
  // it would have got from the other run, only sooner, and every statement is
  // as much on its own as it was. A transaction that Lua began is none of
  // this: a sync point does not end it.
  //
  // Before every statement but the first there is one more sync point. libpq
  // hands over a notice as soon as it comes across it in what it has read,
  // also while the result before it waits to be taken (pqParseInput3 in
  // fe-protocol3.c). And the server may have something to say about a
  // statement before anything else comes of it, a name in it that is too
  // long for one. Read in one go with the sync point of the statement before,
  // such a notice could not be told from what the server said when it
  // committed that one. libpq does not read past the second sync point before
  // the first one was taken, and the server says nothing in between.
  //
  // _use_mtx is held as in the other run, and nothing waits for the server
  // inside of libpq here either, on a connection that does not block for the
  // time being. Here that is more than a matter of what is held meanwhile.
  // One that blocks waits in pqSendSome (fe-misc.c) for the server to take
  // what does not fit the socket, for as long as the server is busy with a
  // statement that was sent before. As it is, what the socket does not take
  // goes out in await, which waits for the socket to take more as well as to
  // bring more. So the server never waits for results to be read while
  // nothing reads them, however much is sent and however much comes back.
  //
  // To libpq a command is under way from the first statement that it took to
  // the last sync point that was taken from it (cmd_queue_head in fe-exec.c),
  // so escaping leaves the error message of the connection alone all the way.
  // That includes the times that _use_mtx is let go of for the rows of a
  // statement, see conclude. After the last statement nothing is asked of
  // the connection that its error message matters for.
  //
  // A connection that is lost takes along every statement that has no result
  // yet. None of them is sent once more: there is no telling which of them
  // the server ran.
  void run(PGconn *conn, std::vector<Job> &jobs) {
    std::unique_lock<std::mutex> use(_use_mtx);
    std::vector<Outcome> outcomes;
    // The statement whose results are read. Those before it are done with.
    size_t at = 0;
    // Whether the connection is good for what is left.
    bool intact = false;

    // The result of a job gets where it belongs, whatever else goes wrong.
    auto finish = [&](size_t job, bool lost) {
      try {
        QueryResult result;

        try {
          if (job >= outcomes.size())
            throw std::bad_alloc();

          result = conclude(conn, outcomes[job], lost, use);
        } catch (const std::exception &e) {
          result = QueryResult();
          result.error = e.what();
        }

        jobs[job].done(std::move(result));
      } catch (...) {
      }
    };

    try {
      outcomes.resize(jobs.size());

      // From here on, the notices that arrive are those of the first query.
      Noticing noticing(_connection, outcomes[0].out.notices);

      intact = PQsetnonblocking(conn, 1) == 0 && PQenterPipelineMode(conn);

      // All of them are handed to libpq before the first result is asked for.
      // That waits for nothing, and libpq does not look at what it reads
      // meanwhile, so the connection is in no other state for any of them.
      for (size_t job = 0; intact && job < jobs.size(); job++) {
        if (job > 0)
          intact = PQsendPipelineSync(conn);

        // One that libpq does not take has its sync points all the same, for
        // whatever of it may have got out.
        if (intact) {
          send(conn, jobs[job].statement, outcomes[job]);
          intact = PQsendPipelineSync(conn);
        }
      }

      while (intact && at < jobs.size()) {
        Outcome &outcome = outcomes[at];
        // Whether the sync point in front of the statement was taken, and
        // whether the last thing libpq had was the end of a statement.
        bool reached = at == 0, ended = false;

        for (;;) {
          while (intact && PQisBusy(conn))
            intact = await(conn, use);

          // libpq has said why in the error message of the connection. Asked
          // for a result now, it would try to read once more and add to that.
          if (PQstatus(conn) != CONNECTION_OK)
            intact = false;

          if (!intact)
            break;

          result_t res(PQgetResult(conn));

          // The results of a statement end with none, its sync point comes
          // after that. None twice is libpq having nothing more at all.
          if (!res) {
            intact = !ended;
            ended = true;

            if (!intact)
              break;

            continue;
          }

          ExecStatusType status = PQresultStatus(res.get());

          ended = false;

          if (status == PGRES_PIPELINE_SYNC) {
            if (reached)
              break;

            reached = true;
          } else if (!outcome.sent) {
            // Whatever the server made of the pieces that got out, the
            // statement has failed already.
          } else if (status == PGRES_EMPTY_QUERY || status == PGRES_COMMAND_OK || status == PGRES_TUPLES_OK) {
            outcome.last = std::move(res);
          } else if (status == PGRES_COPY_IN || status == PGRES_COPY_OUT || status == PGRES_COPY_BOTH) {
            // pipelined is there to keep this from happening. There is no
            // telling what the server makes of the statements behind a COPY,
            // so the connection goes, and what is left with it.
            outcome.refused = "pg - COPY is not supported among queries that are sent together";
            intact = false;
            break;
          } else {
            fail(conn, outcome, std::move(res));
          }
        }

        if (!intact)
          break;

        // What the server says from here on is about the next statement.
        if (at + 1 < jobs.size())
          noticing.to(outcomes[at + 1].out.notices);

        // Those that came in with the results of the statement, or before it.
        notifications(conn);
        finish(at++, false);
      }

      if (intact)
        intact = PQexitPipelineMode(conn) && PQsetnonblocking(conn, 0) == 0;
      else
        notifications(conn);
    } catch (...) {
      intact = false;
    }

    while (at < jobs.size())
      finish(at++, true);

    // One that is not lost may still be in the middle of something that there
    // was no way out of. The next query gets a new connection. One that is
    // lost stays until then, as in the other run: it is still good for
    // escaping.
    if (PQstatus(conn) == CONNECTION_OK && (!intact || PQtransactionStatus(conn) == PQTRANS_ACTIVE)) {
      use.unlock();
      drop();
    }
  }

  // What the worker does every now and then while it has no query to run and
  // there are channels. It has libpq read what the server sent, which is how
  // notifications get here when no query brings them along, and it opens the
  // connection again if it is lost. Nothing else would before the next query,
  // and nobody would be listening until then.
  void idle() {
    // Whoever has the connection takes the notifications along when done
    // with it, see run. That is not waited for here.
    std::unique_lock<std::mutex> lock(_mtx, std::try_to_lock);

    if (!lock.owns_lock() || _channels.empty() || !_connection)
      return;

    if (_connection->is_open()) {
      PGconn *conn = _connection->conn;
      bool transaction = PQtransactionStatus(conn) != PQTRANS_IDLE;
      // Inside of libpq, like a query, see run. This does not wait for the
      // server.
      std::lock_guard<std::mutex> use(_use_mtx);
      bool alive = PQconsumeInput(conn);

      // What was read before the connection went is as good as any.
      notifications(conn);
      note_encoding(conn);

      if (alive)
        return;

      // As in ready.
      if (transaction)
        _orphaned = message_of(conn);
    }

    // One that was closed is not lost. And a server that is away may be so
    // for a while: connecting keeps everything waiting that needs _mtx, for
    // as long as the server takes to answer or not to.
    if (_closed || _deactivated || std::chrono::steady_clock::now() < _retry)
      return;

    try {
      reopen();
    } catch (const std::exception &) {
      // The query that needs the connection next says why.
    }

    _retry = std::chrono::steady_clock::now() + RETRY_INTERVAL;
  }

  // The worker is not started before there is something for it to do. Main
  // thread only.
  void start() {
    if (!_worker.joinable())
      _worker = std::thread(&Session::work, this);
  }

  // Runs the first of the jobs, and with it the ones behind it that can go to
  // the server together with it. What was run is taken off the list. Worker
  // thread only.
  void perform(std::deque<Job> &jobs) {
    auto lock = turn();
    size_t count = together(jobs);
    PGconn *conn = nullptr;
    QueryResult result;

    // As in Execute. A connection that cannot be had is what the first
    // statement comes to, the way it would have alone, and the ones behind it
    // are left for another attempt.
    try {
      conn = ready_to_run();

      if (count == 1)
        result = run(conn, jobs.front().statement);
    } catch (const connection_error &e) {
      result.error = e.what();
      result.details.emplace("connection_lost", true);
    } catch (const std::exception &e) {
      result.error = e.what();
    }

    if (!conn)
      count = 1;

    if (count > 1) {
      std::vector<Job> batch(std::make_move_iterator(jobs.begin()), std::make_move_iterator(jobs.begin() + count));

      jobs.erase(jobs.begin(), jobs.begin() + count);
      run(conn, batch);
    }

    if (_connection)
      note_encoding(_connection->conn);

    if (count > 1)
      return;

    Job job = std::move(jobs.front());

    jobs.pop_front();
    lock.unlock();
    job.done(std::move(result));
  }

  // Closes the connection for good, the way Disconnect does.
  void close() {
    auto lock = turn();

    if (_connection) {
      _closed = true;
      drop();
    }
  }

  // Has the worker stop once it has run what is queued, and with closing,
  // close the connection as the last thing it does. This does not wait for
  // it, see join.
  void dismiss(bool closing) {
    {
      std::lock_guard<std::mutex> lock(_queue_mtx);
      _finishing = true;
      _closing = closing;
    }

    _queue_cv.notify_all();
  }

  // Waits for a worker that was dismissed. Main thread only.
  void join() {
    _worker.join();
    _finishing = false;
    _closing = false;
  }

  void work() {
    bool closing = false;

    for (;;) {
      // What was taken off the queue, in its order.
      std::deque<Job> jobs;

      {
        std::unique_lock<std::mutex> lock(_queue_mtx);
        auto due = [this] { return _finishing || !_queue.empty(); };

        // A connection that listens is looked after in between, with the
        // queue unlocked: what is added to it meanwhile is not held up, and
        // goes first.
        while (!due()) {
          if (!_listening) {
            _queue_cv.wait(lock);
          } else if (!_queue_cv.wait_for(lock, IDLE_INTERVAL, due)) {
            lock.unlock();

            try {
              idle();
            } catch (...) {
            }

            lock.lock();
          }
        }

        // Queries that were queued before Finish still get to run.
        if (_queue.empty()) {
          closing = _closing;
          break;
        }

        // The first one, and the ones behind it for as long as they are of
        // a kind that goes to the server together. Whether they do is for
        // perform to say, which is where what was prepared may be looked at.
        do {
          jobs.push_back(std::move(_queue.front()));
          _queue.pop_front();
        } while (jobs.size() < BATCH && !_queue.empty()
          && jobs.back().statement.kind != Statement::Plain && _queue.front().statement.kind != Statement::Plain);
      }

      while (!jobs.empty()) {
        size_t left = jobs.size();

        // Nothing is there to catch an exception that leaves the thread.
        try {
          perform(jobs);
        } catch (...) {
        }

        // Whatever went wrong, it is not tried on the same one again.
        if (jobs.size() == left)
          jobs.pop_front();
      }
    }

    if (!closing)
      return;

    try {
      close();
    } catch (...) {
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
    auto lock = turn();

    std::swap(_options, options);
    auto encoding = std::exchange(_encoding, {});
    auto prepared = std::exchange(_prepared, {});
    auto channels = std::exchange(_channels, {});

    try {
      open();
    } catch (...) {
      _options = std::move(options);
      _encoding = std::move(encoding);
      _prepared = std::move(prepared);
      _channels = std::move(channels);
      throw;
    }

    _closed = false;
    _emit = nullptr;
    _orphaned.reset();
    note_channels();
  }

  void Disconnect() {
    auto lock = turn();

    if (!_connection)
      throw no_connection();

    _closed = true;
    drop();
  }

  // Makes sure that the connection is open.
  void Activate() {
    auto lock = turn();

    if (!_connection)
      throw no_connection();

    _closed = false;
    ready(true);
  }

  // Closes the connection until something needs it again.
  void Deactivate() {
    auto lock = turn();

    if (!_connection)
      throw no_connection();

    _closed = false;
    _deactivated = true;
    drop();
  }

  void Prepare(const std::string &name, const std::string &definition) {
    // The statement without a name is the one that a query with parameters
    // uses, it would not last.
    if (name.empty())
      throw std::invalid_argument("pg - prepared query name is empty");

    auto lock = turn();
    PGconn *conn = ready();
    result_t res(PQprepare(conn, name.c_str(), definition.c_str(), 0, nullptr));

    if (PQresultStatus(res.get()) != PGRES_COMMAND_OK)
      throw std::runtime_error(message_of(conn, res.get()));

    _prepared[name] = definition;
  }

  void Unprepare(const std::string &name) {
    auto lock = turn();

    // Off the record first: whatever the server makes of it, and even if it
    // cannot be asked, the statement is not to come back with the next
    // connection.
    bool known = _prepared.erase(name) > 0;
    auto before = _connection;
    PGconn *conn = ready();

    // A connection that had to be opened for this never got the statement,
    // which is all that was asked for.
    if (known && _connection != before)
      return;

    std::string query = "DEALLOCATE " + quoted_name(conn, name);
    result_t res(PQexec(conn, query.c_str()));

    if (PQresultStatus(res.get()) != PGRES_COMMAND_OK)
      throw std::runtime_error(message_of(conn, res.get()));
  }

  // Has the server tell this connection what is sent to the channel. events
  // is where that goes, together with everything else that there is to say
  // to those who listen.
  void Listen(const std::string &channel, emitter_t events) {
    // The text of a query ends at the first zero byte.
    if (channel.find('\0') != std::string::npos)
      throw std::invalid_argument("pg - channel name contains a zero byte");

    // Notifications come in when they like, which may be when there are no
    // queries, or never were any, see idle.
    start();

    auto lock = turn();
    PGconn *conn = ready();
    std::string query = "LISTEN " + quoted_name(conn, channel);
    result_t res(PQexec(conn, query.c_str()));

    if (PQresultStatus(res.get()) != PGRES_COMMAND_OK)
      throw std::runtime_error(message_of(conn, res.get()));

    _channels.insert(channel);
    _emit = std::move(events);
    note_channels();
  }

  void Unlisten(const std::string &channel) {
    if (channel.find('\0') != std::string::npos)
      throw std::invalid_argument("pg - channel name contains a zero byte");

    auto lock = turn();

    // Off the record first: whatever the server makes of it, and even if it
    // cannot be asked, the channel is not to come back with the next
    // connection.
    _channels.erase(channel);
    note_channels();

    PGconn *conn = ready();
    std::string query = "UNLISTEN " + quoted_name(conn, channel);
    result_t res(PQexec(conn, query.c_str()));

    if (PQresultStatus(res.get()) != PGRES_COMMAND_OK)
      throw std::runtime_error(message_of(conn, res.get()));
  }

  // Whether a channel is on record. This does not wait for anything. Main
  // thread only, which is the one that changes the answer.
  bool Listening() const { return _listening; }

  void SetEncoding(const std::string &encoding) {
    auto lock = turn();
    PGconn *conn = ready();

    if (PQsetClientEncoding(conn, encoding.c_str()) != 0)
      throw std::runtime_error(message_of(conn));

    _encoding = encoding;
    note_encoding(conn);
  }

  std::string Escape(const std::string &text) {
    auto lock = escaping(text);
    auto link = escapable(lock);
    std::lock_guard<std::mutex> use(_use_mtx);
    std::string escaped(text.size() * 2 + 1, '\0');
    int error = 0;
    size_t length = PQescapeStringConn(link->conn, escaped.data(), text.data(), text.size(), &error);

    if (error)
      throw std::invalid_argument(message_of(link->conn));

    escaped.resize(length);
    return escaped;
  }

  // Not PQescapeLiteral, which writes to the connection whatever the text is
  // and goes for another notation as soon as there is a backslash in it.
  std::string Quote(const std::string &text) {
    return "'" + Escape(text) + "'";
  }

  std::string QuoteName(const std::string &text) {
    auto lock = escaping(text);

    if (lock.owns_lock())
      return quoted_name(escapable(lock)->conn, text);

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
  std::shared_ptr<Link> Peek() {
    std::lock_guard<std::mutex> lock(_connection_mtx);

    if (!_connection)
      throw no_connection();

    return _connection;
  }

  // Asks the server to give up on the query that is running. This does not
  // wait for that query either, and it goes over a connection of its own,
  // which is encrypted if the one of the query is.
  void Cancel() {
    auto link = Peek();

    if (!link->is_open())
      throw std::runtime_error("pg - connection is closed, there is nothing to cancel.");

    std::lock_guard<std::mutex> lock(link->cancel_mtx);

    // Only one that could not be made is in this state between two uses.
    if (PQcancelStatus(link->cancel) == CONNECTION_BAD)
      throw std::runtime_error(trimmed(PQcancelErrorMessage(link->cancel)));

    bool sent = PQcancelBlocking(link->cancel);
    std::string error = trimmed(PQcancelErrorMessage(link->cancel));

    // Ready for the next time.
    PQcancelReset(link->cancel);

    if (!sent)
      throw std::runtime_error(error);
  }

  // Runs the statement and waits for the result. There is no transaction
  // around it other than the one the server puts around every query.
  QueryResult Execute(const Statement &statement) {
    QueryResult result;
    auto lock = turn();

    try {
      result = run(ready_to_run(), statement);
    } catch (const connection_error &e) {
      result.error = e.what();
      result.details.emplace("connection_lost", true);
    } catch (const std::exception &e) {
      result.error = e.what();
    }

    if (_connection)
      note_encoding(_connection->conn);

    return result;
  }

  // Runs the statement on the worker thread, after the ones queued before it,
  // and together with its neighbours in the queue where that is possible, see
  // the run that takes jobs. done is called on that thread too.
  void Enqueue(Statement statement, callback_t done) {
    start();

    {
      std::lock_guard<std::mutex> lock(_queue_mtx);

      _queue.push_back(Job{ std::move(statement), std::move(done) });
    }

    _queue_cv.notify_one();
  }

  // Waits for the queued queries to finish and stops the worker thread.
  void Finish() {
    if (!_worker.joinable())
      return;

    dismiss(false);
    join();
  }

  // For when the module is closed: no thread may be left running its code,
  // and the queries that are still queued should not be lost. Every
  // connection is closed.
  //
  // A queued query may be waiting for a lock that a transaction of another
  // session holds, one that was left open because the listener that was
  // going to end it is no longer called. Nothing but the end of that other
  // connection lets it go on. So no session is waited for before all of them
  // were told, and each connection is closed as soon as there is nothing
  // left to run on it: at once where there is no worker, and by the worker
  // where there is one, when it is through with the queue.
  static void Shutdown() {
    for (Session *session : sessions()) {
      if (session->_worker.joinable())
        session->dismiss(true);
      else
        session->close();
    }

    for (Session *session : sessions()) {
      if (session->_worker.joinable())
        session->join();
    }
  }
};

// The arguments of a "notice" listener.
inline std::vector<LuaValue> notice_args(Notice &notice) {
  std::vector<LuaValue> args;

  args.emplace_back(std::move(notice.message));
  args.emplace_back(std::move(notice.details));
  return args;
}

// Runs the statement for the query at stack position 1, the way it is set to:
// either right away, returning the result, or in the background, emitting
// "success" or "error" once done. Either way, "notice" comes before that.
inline int run_statement(ILuaBase *LUA, Session &session, bool sync, Statement statement) {
  if (sync) {
    QueryResult result = session.Execute(statement);

    // This is the main thread and the query is over: the listeners need not
    // wait for the next Think, by which time run has long returned.
    for (Notice &notice : result.notices)
      LuaEventEmitterManager::Call(LUA, 1, "notice", notice_args(notice));

    if (!result.success) {
      LUA->PushBool(false);
      push_string(LUA, result.error);
      LuaValue(std::move(result.details)).Push(LUA);
      return 3;
    }

    LUA->PushBool(true);
    LuaValue(std::move(result.rows)).Push(LUA);
    LUA->PushNumber(result.size);
    LUA->PushNumber(result.affected);
    return 4;
  }

  auto events = LuaEventEmitterManager::Current(LUA);
  // The query has to stay around until its listeners were called, even if
  // nothing in Lua refers to it anymore.
  int self = events->Hold(LUA, 1);

  try {
    session.Enqueue(std::move(statement), [events, self](QueryResult result) {
      std::vector<LuaValue> args;

      // The query stays held until the event after these.
      for (Notice &notice : result.notices)
        events->Emit(self, "notice", notice_args(notice), false);

      if (result.success) {
        args.emplace_back(std::move(result.rows));
        args.emplace_back(result.size);
        args.emplace_back(result.affected);
        events->Emit(self, "success", std::move(args));
      } else {
        args.emplace_back(std::move(result.error));
        args.emplace_back(std::move(result.details));
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
