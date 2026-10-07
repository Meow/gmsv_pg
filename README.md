# gmsv_pg

A PostgreSQL adapter for Garry's Mod.

## Installing

1. Go to the "Releases" of this GitHub repository.
2. Download the latest `.dll` file for your platform (see the table below).
3. Copy that file to `garrysmod/lua/bin` of your server.

| Server branch    | Windows             | Linux                 |
|------------------|---------------------|-----------------------|
| default (32-bit) | `gmsv_pg_win32.dll` | `gmsv_pg_linux.dll`   |
| x86-64 (64-bit)  | `gmsv_pg_win64.dll` | `gmsv_pg_linux64.dll` |

That's all there is to it. libpq, OpenSSL and the C++ runtime are all linked into the module, so nothing else has to be installed on the server.

The Linux binaries need glibc 2.35 or newer, which means Debian 12, Ubuntu 22.04 or anything more recent.

The database can be any version of PostgreSQL that is supported upstream, which currently means 14 and newer.

## Building

If you have Docker installed, just run `./build.sh`. It builds the binaries for all four platforms in a container, with no other tools needed on your machine, and places them in the `pg/bin` folder. The first build takes a few minutes, because OpenSSL and libpq are built from source for each platform.

The versions of the libraries are set at the top of the `Dockerfile`.

To build without Docker, on Linux and for Linux:
1. Install premake5 and a compiler that supports C++17 (GCC 11 or newer). The 32-bit build on a 64-bit system needs `gcc-multilib` and `g++-multilib` as well.
2. Build static libraries of libpq (version 17 or newer) and OpenSSL. The module is linked with `libpq.a`, `libpgcommon_shlib.a`, `libpgport_shlib.a`, `libssl.a` and `libcrypto.a`, which go into `pg/deps/<system>-<architecture>/lib`, and compiled with `libpq-fe.h` and `postgres_ext.h`, which go into `pg/deps/<system>-<architecture>/include`. The system is `linux` or `windows`, the architecture `x86` or `x86_64`, e.g. `pg/deps/linux-x86_64`. The `postgres_ext.h` of libpq 17 wants `pg_config_ext.h` next to it, that of libpq 18 gets by alone. `docker/build-deps.sh` shows how the libraries are built for the releases. Use the `--deps=path` option of premake to keep them somewhere else.
3. In the `pg` folder, run `premake5 gmake`.
4. Navigate to the `project` folder and run `make config=x86` or `make config=x86_64`.
5. The compiled binary should be in the `pg/bin` folder.

The Windows binaries are built on Linux as well, with MinGW-w64, from libraries in `pg/deps/windows-x86` and `pg/deps/windows-x86_64`. Run `premake5 --os=windows gmake` in step 3, and tell make which compiler that is in step 4, the way the `Dockerfile` does: `make config=x86_64 CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++ AR=x86_64-w64-mingw32-ar`, and the same with `i686-w64-mingw32` for `config=x86`.

## Usage

```lua
require("pg")

local db = pg.new_connection()
local ok, err = db:connect("127.0.0.1", "postgres", "password", "gmod", 5432)

if not ok then
  error("could not connect to the database: " .. err)
end

local query = db:query("select name, score from players where score > $1")

query:on("success", function(rows, size)
  for i = 1, size do
    print(rows[i].name, rows[i].score)
  end
end)

query:on("error", function(message)
  print("the query failed: " .. message)
end)

query:run(100)
```

`pg.new_connection` returns a connection that is not connected to anything yet. Before the first `connect` that works, every other method of it throws the Lua error "pg - no connection, connect to a database first.", `is_open` included. Only the methods that add and remove listeners do not.

From then on, a Lua error is thrown for what the module cannot work with: a query string or the name of an event that is not a string, a listener that is not a function, a parameter that is neither a string, a number, a boolean nor nil, text for `escape`, `quote` or `quote_name` that is not valid in the encoding of the connection, a method that is called on the wrong kind of object, as in `db.query("select 1")` with a dot. A zero byte is such a thing as well, in a query string, in the name of a prepared statement and in the text for `escape`, `quote` or `quote_name`, like it is in a parameter: text ends there for the server, and what is behind it would be gone without a word. Binary data goes through `escape_bytea`. What can fail at the server does not throw. `connect`, `prepare`, `listen` and the other methods of a connection that talk to the server return true, or false and the error message. A synchronous query does the same, and an asynchronous one calls its "error" listeners.

Queries are asynchronous by default. `run` puts the query into a queue and returns at once, a background thread sends it to the server, and its listeners are called from the `Think` hook once the result is there. Every connection has a queue and a thread of its own: the queries of one connection run one after another, in the order they were queued, and those of different connections run side by side. An empty server only thinks if `sv_hibernate_think` is set to `1`.

Queries that are waiting in that queue are sent to the server up to eight at a time, if they have parameters or were prepared, instead of each waiting for the result of the one before. A server that saves the data of all its players at once spends far less time on the way to the database and back like that. Nothing else changes by it: they run in the order they were queued, each succeeds or fails by itself, and each gets its own result.

A query can be set to be synchronous instead. `run` then waits for it and returns what the listeners would have been called with: true, the rows, their amount and the amount of affected rows, or false, the error message and the details of the error. Its "success" and "error" listeners are not called. The server stands still for as long as such a query takes, so this is for when the server starts rather than for when players are on it:

```lua
local query = db:query("create table if not exists players (steamid bigint primary key, name text, score int)")
query:set_sync(true)

local ok, err = query:run()
```

Values that come from outside, like the name of a player, are best kept out of the query itself and passed to `run` as parameters instead. They take the place of `$1`, `$2` and so on in the query, and nothing in them has to be escaped:

```lua
local query = db:query("select * from players where name = $1 and score > $2")
query:on("success", function(rows, size) end)
query:run(ply:Nick(), 100)
```

A parameter can be a string, a number or a boolean, and nil is NULL. A query that is given parameters has to be a single statement. What a parameter cannot stand for, like the name of a table, goes into the query through `quote_name`, and a value that has to be in the query itself through `quote`.

A statement that is run often can be given a name with `prepare`, and is run by that name from then on, which saves the server from reading the same query over and over:

```lua
db:prepare("add_score", "update players set score = score + $2 where steamid = $1")

local query = db:query_prepared("add_score")
query:run("76561197988658543", 10)
```

The result of a query is a list of its rows, `rows[1]` to `rows[size]`. A row is a table of its values by the names of their columns, as in `rows[1].name`. A column that is NULL is not in that table, which makes it nil. Of two columns with the same name only the first is in it, and in a row where that one is NULL the name is nil, whatever the second has. Give them names of their own to have both: `select a.name, b.name as other_name`.

In a row, booleans are booleans, numbers are numbers and everything else is a string. Whole numbers beyond 2^53 are strings too, because a Lua number would round them: a 64-bit SteamID from a `bigint` column comes back as `"76561197988658543"`. A `numeric` that is not a whole number has no such way out. With more digits than a Lua number holds, which is about 15, it is rounded, unless the query casts it to `text`.

Next to the rows there is the amount of rows that were affected, the ones that the query inserted, updated or deleted. If the query was several statements, all of this is about the last one.

A query that fails has an error message and a table of details. The `sqlstate` in there is what to tell errors apart by:

```lua
query:on("error", function(message, details)
  if details.sqlstate == "23505" then
    -- there is a row with that key already
  end
end)
```

What the server has to say about a query that is no error, a warning or the output of `RAISE NOTICE`, goes to the "notice" listeners of that query. They are called before "success" or "error" is, in the order the notices arrived.

An object can have several listeners for an event, and they stay with it: a query that is run again calls them again.

A query goes to the server as it is, with no transaction put around it: what it changes is there to stay as soon as it is done. Several statements in one query succeed or fail together. For a transaction that spans queries, run `BEGIN` and then `COMMIT` or `ROLLBACK` like any other query. Everything that the connection runs in between is a part of it, so whatever is not meant to be has to go over another connection. A transaction ends with the connection it was started on, whether that is lost or closed.

The data of a `COPY` can only be in a file on the server. `COPY ... FROM STDIN` and `COPY ... TO STDOUT` fail.

A connection can listen to what other connections, or it itself, send with `NOTIFY` or `pg_notify()`:

```lua
db:on("notification", function(channel, payload, pid) end)
db:listen("players")
```

Notifications arrive whether the connection has queries to run or not, though not in the middle of one: what comes in while a query runs is passed on once it is over. Their listeners are called from the `Think` hook, like those of a query, so on a server that does not think they pile up until it does.

A connection that listens is not garbage collected: it stays, and its listeners are called, even if nothing refers to it anymore. That ends when `unlisten` was called for each of its channels, or with the next `connect` that works. `disconnect` and `deactivate` do not end it, though nothing arrives while the connection is closed.

A connection that was lost is opened again when the next query needs it, with its encoding, its prepared statements and the channels it listens to. The queries that were on their way to the server when it was lost fail, and the details of their errors say that this is why. They are not sent again, because there is no telling which of them the server ran.

A connection that the server ended while nothing was running, because it was restarted or because the connection had been idle for too long, is lost as well. The next query finds that out before it is sent, and runs on a new connection. If a transaction was open, it is gone with the old connection, and the next query fails instead, like one that was on its way: it was going to be a part of that transaction. The query after that runs on a new connection.

A connection that listens does not wait for a query when it is lost. It is opened again as soon as the loss is noticed, and if that does not work, it is tried again every 5 seconds. Whatever is synchronous waits for such an attempt the way it waits for a query. Nothing of what was sent while the connection was away arrives later, which is what the "reconnect" listeners are there to be told.

While the server cannot be reached, not every query waits for an attempt of its own to open the connection. After an attempt that failed, whatever needs the connection fails at once with the error of that attempt, for as long as that attempt took. So a queue of queries waits for a server that does not answer once, not once for every query. `connect` and `activate` always try.

A connection that was closed with `disconnect` is not opened again by a query. Its queries fail, the ones that were still queued included, until `activate` or `connect` is called. `deactivate` closes it until something needs it. Either way it comes back like one that was lost, with its encoding, its prepared statements and its channels. A `connect` that works is what forgets all three.

Whatever is synchronous waits for the queries that are on their way to the server, eight at most, but not for the ones that are queued behind them. That is a query that was set to be synchronous, and `connect`, `disconnect`, `activate`, `deactivate`, `prepare`, `unprepare`, `listen`, `unlisten` and `set_encoding`. `cancel`, `is_open`, `server_version` and `protocol_version` wait for nothing. Neither do `escape`, `quote` and `quote_name` as a rule, so that building a query does not hold up the server while another one runs. They do wait for text that is not valid UTF-8, and for all text that is not plain ASCII if the encoding of the connection is another one than UTF8. `escape` and `quote` also wait where they have to open the connection first, after `deactivate`.

### Reference

Here's a list of everything that is present:

```lua
-- Returns a new DatabaseConnection object for PostgreSQL.
function pg.new_connection()

-- Various version strings
pg.version
pg.version_major
pg.version_minor
pg.version_patch
pg.version_suffix

-- Listeners
--
-- A DatabaseConnection, a DatabaseQuery and a PreparedQuery all have the four
-- methods below. Which events there are is listed with each class.

-- Add a listener: a function that is called every time the event happens.
--
-- event: the name of the event, e.g. "success"
-- fn: the function to call
--
-- An object can have any number of listeners for an event. They are called
-- in the order they were added, the ones that were added with once after
-- all the others. An error in a listener is printed and does not keep the
-- others from being called.
--
-- Listeners stay with their object until remove_listeners is called: a query
-- that is run again calls them again. A listener that is added or removed
-- while an event is delivered makes no difference to that event.
--
-- Throws an error if event is not a string or fn is not a function.
function object:on(event, fn)

-- Add a listener that is called the next time the event happens, and
-- forgotten after that.
function object:once(event, fn)

-- The same as on, or as once if once is true.
function object:add_listener(event, fn, once)

-- Remove all listeners of the object, those of every event.
function object:remove_listeners()

-- DatabaseConnection class

-- Connect to the specified database.
--
-- host: the name or the address of the server, "127.0.0.1" if it is not a
--   string
-- user: "postgres" if it is not a string
-- password, db: left out if they are not strings
-- port: a number or a string, left out if it is neither
-- extra_string_to_append: a piece of libpq connection string, e.g.
--   "sslmode=require connect_timeout=10". What it sets wins over the other
--   arguments.
--
-- host, user, password and db are taken as they are, spaces and quotes
-- included. What is left out is for libpq to decide. Unless its environment
-- variables (PGPASSWORD, PGDATABASE, PGPORT) or its password file say
-- otherwise, that is no password, the database that is named like the user,
-- and port 5432.
--
-- This blocks until the server has answered. A server that does not is given
-- up on after 5 seconds, unless connect_timeout says otherwise. The same goes
-- for a lost connection that is opened again.
--
-- Returns true if successful, false and the error message otherwise.
-- If it fails, a connection that was there before stays as it was. If it
-- works, the encoding that was set, the prepared statements and the channels
-- of that connection are forgotten.
function DatabaseConnection:connect(host, user, password, db, port, extra_string_to_append)

-- What connect was last called with, whether it worked or not. These are
-- fields, not methods, and all of them are strings, port too. They are
-- "127.0.0.1" and "postgres" for a host and a user that were not given, and
-- "" for a password, a database or a port that was not. Before the first
-- connect all five are "". What extra_string_to_append set is not in them.
DatabaseConnection.host
DatabaseConnection.user
DatabaseConnection.password
DatabaseConnection.database
DatabaseConnection.port

-- Disconnect from current database.
--
-- This waits for the queries that are on their way to the server. The ones
-- that are queued behind them fail, and so does every query that is run
-- afterwards, with the error "pg - connection is closed, connect to the
-- database again.". That lasts until activate opens the connection again,
-- or connect a new one.
--
-- Returns true
function DatabaseConnection:disconnect()

-- Create a SQL query. Nothing is sent to the server before it is run.
--
-- query_string: SQL to execute
--
-- Returns a DatabaseQuery object
function DatabaseConnection:query(query_string)

-- Create a prepared query. Whether there is such a statement only shows
-- when it is run.
--
-- name: ID of the prepared statement, see DatabaseConnection:prepare. Throws
--   an error if it is empty.
--
-- Returns a PreparedQuery object
function DatabaseConnection:query_prepared(name)

-- Escape dangerous characters in a string, for use between single quotes in
-- a query.
--
-- Throws an error if the string has a zero byte in it or is not valid in the
-- encoding of the connection, and while the connection is closed after
-- disconnect: how to escape depends on the server. A connection that was
-- lost still escapes. One that was closed with deactivate is opened again
-- for this, the way it is for a query, and the error is thrown if that does
-- not work.
--
-- Returns an escaped string, or nothing if str is not a string
function DatabaseConnection:escape(str)

-- Turn binary data into the text that a bytea column takes ("\x00ff"),
-- as a parameter or, quoted, inside of a query.
--
-- Returns a string, or nothing if data is not a string
function DatabaseConnection:escape_bytea(data)

-- Turn the value of a bytea column back into binary data
--
-- This only reads the hex form, "\x" and two hex digits for every byte,
-- which is what servers send unless bytea_output was changed. Throws an
-- error for anything else.
--
-- Returns a string, or nothing if escaped_str is not a string
function DatabaseConnection:unescape(escaped_str)

-- Quote a string: escape it and put single quotes around it. What goes for
-- escape goes for this.
--
-- Returns a quoted string, or nothing if str is not a string
function DatabaseConnection:quote(str)

-- Quote a column name, or any other name, in double quotes.
--
-- Throws an error if the string has a zero byte in it or is not valid in the
-- encoding of the connection.
--
-- Returns a quoted string, or nothing if str is not a string
function DatabaseConnection:quote_name(str)

-- Cancel the current query, if any. It fails with an error then, the
-- sqlstate of which is "57014".
--
-- This does not wait for the query. The request is sent over a connection of
-- its own, which is encrypted if the one of the query is.
--
-- Returns true if the request was sent, false and the error message otherwise
function DatabaseConnection:cancel()

-- Get the server protocol version
--
-- Returns protocol version as a number, which is 3, or 0 while the connection
-- is not open
function DatabaseConnection:protocol_version()

-- Get the server version
--
-- Returns server version as a number, e.g. 180006 for 18.6 and 140019 for
-- 14.19, or 0 while the connection is not open
function DatabaseConnection:server_version()

-- Activate this connection: open it again if it was closed or lost
--
-- Avoid using this as it's done automatically most of the time.
-- Use only if you know what you're doing.
--
-- Returns true if successful, false and the error message otherwise
function DatabaseConnection:activate()

-- Deactivate the current connection: close it until a query needs it again,
-- or escape or quote. Listening is no such need, nothing arrives until then.
--
-- Returns true
function DatabaseConnection:deactivate()

-- Get whether the connection is open
--
-- Returns true or false
function DatabaseConnection:is_open()

-- Prepare a query (register it with the server)
--
-- name: ID of the prepared statement, must not be empty
-- definition: the query itself, with $1, $2 and so on for its parameters
--
-- Returns true if successful, false and the error message otherwise
function DatabaseConnection:prepare(name, definition)

-- Unprepare the query (unregister it on the server)
--
-- name: ID of the prepared statement
--
-- Returns true if successful, false and the error message otherwise.
-- Either way the statement is not prepared again when the connection is
-- opened the next time.
function DatabaseConnection:unprepare(name)

-- Listen to a channel: what any connection sends to it with NOTIFY or
-- pg_notify() goes to the "notification" listeners from then on.
--
-- channel: the name of the channel. It is taken as it is, capitals and
-- spaces included, the way pg_notify() takes it. NOTIFY needs such a name
-- in double quotes.
--
-- The connection is not garbage collected as long as it listens to a
-- channel.
--
-- Returns true if successful, false and the error message otherwise
function DatabaseConnection:listen(channel)

-- Stop listening to a channel
--
-- Notifications that are on their way may still arrive after this, unless
-- this was the last channel.
--
-- Returns true if successful, false and the error message otherwise.
-- Either way the channel is not listened to again when the connection is
-- opened the next time.
function DatabaseConnection:unlisten(channel)

-- Called for every notification from a channel that is listened to.
--
-- Callback arguments:
-- channel: the name of the channel
-- payload: the text that was sent along, an empty string if there was none
-- pid: the process ID of the server process that sent it, which is what
--   pg_backend_pid() returns on the connection of the sender
DatabaseConnection:on("notification", function(channel, payload, pid) end)

-- Called when a connection that listens was opened again: after it was lost,
-- and also after disconnect or deactivate. Whatever was sent to the channels
-- in between did not arrive, and is not going to.
DatabaseConnection:on("reconnect", function() end)

-- Set the current connection encoding (client_encoding): the one that the
-- server takes queries and parameters to be in, and that results come back
-- in.
--
-- encoding: Encoding to set, e.g. "UTF8" or "LATIN1". There is no default.
--
-- The encoding is kept on record and set again whenever the connection is
-- opened again, until the next connect that works. A connection that was
-- never given one has what the server gives it, which as a rule is the
-- encoding of the database.
--
-- Returns true if successful, false and the error message otherwise, which
-- is "invalid encoding" if encoding is not a string
function DatabaseConnection:set_encoding(encoding)

-- DatabaseQuery class

-- Execute the current query. It can be run any number of times.
--
-- vararg: the values of $1, $2 and so on in the query, if it has any.
-- Strings, numbers and booleans are supported, nil is NULL. Anything else
-- throws an error, and so does a string with a zero byte in it: binary data
-- goes through DatabaseConnection:escape_bytea first.
--
-- The server has to know the type of every parameter. Where it cannot tell
-- from the query, say so: "select $1::int".
--
-- A query that is given parameters has to be a single statement.
--
-- Returns nothing, unless the query is synchronous:
-- true, the result table, the amount of items in it and the amount of
-- affected rows if successful,
-- false, the error message and the error details otherwise.
-- These are the arguments of the "success" and "error" callbacks below,
-- which are not called for a synchronous query. The "notice" ones are.
function DatabaseQuery:run(...)

-- Set the query to be synchronous.
-- This will lock the current thread while the query is being executed.
-- USE WITH CAUTION
--
-- sync: true for synchronous, false for default async behavior
function DatabaseQuery:set_sync(sync)

-- Called once the query returns, unless it is synchronous.
--
-- Callback arguments:
-- result: the result table. Numerically indexed (e.g. { [1] = {...}, [2] = {...} }),
--   each row a table of its values by column name
-- size: the amount of items in the results table
-- affected: the amount of rows that were inserted, updated or deleted, or
--   that a COPY to or from a file or a program on the server copied. For a
--   query that returns rows this is their amount. It is 0 for a statement
--   that has no such amount, like CREATE INDEX.
--
-- If the query was several statements, all of this is about the last one.
DatabaseQuery:on("success", function(result, size, affected) end)

-- Called if the query has failed, unless it is synchronous.
--
-- Callback arguments:
-- error: the error message. It is that of the server, or that of libpq or
--   of the module if the server was not what failed, e.g. "pg - connection
--   is closed, connect to the database again."
-- details: a table of what else is known about the error. It is always a
--   table, with nothing in it if nothing is known. Of an error that came from
--   the server it has the following, each only if the server sent it:
--     sqlstate: the code of the error, e.g. "23505" for a duplicate key, see
--       https://www.postgresql.org/docs/current/errcodes-appendix.html
--     severity: "ERROR", "FATAL" or "PANIC"
--     message: the error message itself, without what "error" has around it
--     detail, hint, context: more about the error
--     position: where in the query the error is, in characters, as a number.
--       The first character is 1.
--     schema, table, column, datatype, constraint: the name of what the
--       error is about
--   If the query failed because the connection was lost, or was lost before
--   and could not be opened again, connection_lost is true.
DatabaseQuery:on("error", function(error, details) end)

-- Called for every warning or notice that the server sent while the query
-- ran, before "success" or "error" is. If the query is synchronous, it is
-- called before run returns.
--
-- Callback arguments:
-- message: the text of the notice, e.g.
--   'NOTICE:  table "players" does not exist, skipping'
-- details: a table like the one of "error". severity is "WARNING", "NOTICE",
--   "DEBUG", "INFO" or "LOG".
DatabaseQuery:on("notice", function(message, details) end)

-- PreparedQuery class
--
-- A PreparedQuery has what a DatabaseQuery has: run, set_sync and the
-- "success", "error" and "notice" events.

-- Execute the prepared query
--
-- vararg: which arguments to place into the blank spots of the prepared query.
-- See DatabaseQuery:run for what they can be, and for what this returns.
function PreparedQuery:run(...)
```

## License

gmsv_pg is under the MIT license, see `LICENSE.md`. The binaries also contain gloo (MIT), libpq (PostgreSQL License) and OpenSSL (Apache License 2.0), and the Windows ones the MinGW-w64 runtime (mostly Zope Public License 2.1). `./build.sh` writes the license texts of all of them to `pg/bin/LICENSES.txt`, which has to be distributed together with the binaries. libstdc++ and libgcc are linked in too. They need no notice, because the GCC Runtime Library Exception lets them be distributed as a part of the binaries under any terms.
