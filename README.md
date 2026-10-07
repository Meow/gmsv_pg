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

## Building

If you have Docker installed, just run `./build.sh`. It builds the binaries for all four platforms in a container, with no other tools needed on your machine, and places them in the `pg/bin` folder. The first build takes a few minutes, because OpenSSL and libpq are built from source for each platform.

The versions of the libraries are set at the top of the `Dockerfile`.

To build without Docker:
1. Install premake5 and a compiler that supports C++17 (GCC 11 or newer).
2. Build static libraries of libpq (version 17 or newer) and OpenSSL, and put them into `pg/deps/<system>-<architecture>/lib`, with their headers in `pg/deps/<system>-<architecture>/include` (e.g. `pg/deps/linux-x86_64`). `docker/build-deps.sh` shows how they are built for the releases. Use the `--deps=path` option of premake to keep them somewhere else.
3. In the `pg` folder, run `premake5 gmake`.
4. Navigate to the `project` folder and run `make config=x86` or `make config=x86_64`.
5. The compiled binary should be in the `pg/bin` folder.

## Usage

This module doesn't have all of the features implemented yet, it's being worked on.

**Most of the functions are able to throw Lua errors in case of bad input. Be careful!**

Queries are asynchronous by default: they run on a background thread, one after another, and their callbacks are called from the `Think` hook. An empty server only thinks if `sv_hibernate_think` is set to `1`.

Whatever is synchronous (a query that was set to be, `prepare`, `unprepare`, `listen`, `unlisten`, `set_encoding`) waits for the query that is running, but not for the ones that are queued behind it.

A connection that was lost is opened again when the next query needs it, with its encoding, its prepared statements and the channels it listens to. The query that was running when it was lost fails, and the details of its error say that this is why.

A query goes to the server as it is, with no transaction put around it: what it changes is there to stay as soon as it is done. Several statements in one query succeed or fail together. For a transaction that spans queries, run `BEGIN` and then `COMMIT` or `ROLLBACK` like any other query. Everything that the connection runs in between is a part of it, so whatever is not meant to be has to go over another connection. A transaction ends with the connection it was started on, whether that is lost or closed.

The data of a `COPY` can only be in a file on the server. `COPY ... FROM STDIN` and `COPY ... TO STDOUT` fail.

Values that come from outside, like the name of a player, are best kept out of the query itself and passed to `run` as parameters instead:

```lua
local query = db:query("select * from players where name = $1 and score > $2")
query:on("success", function(rows, size) end)
query:run(ply:Nick(), 100)
```

What the server has to say about a query that is no error, a warning or the output of `RAISE NOTICE`, goes to the "notice" listeners of that query. They are called before "success" or "error" is, in the order the notices arrived.

A connection can listen to what other connections, or it itself, send with `NOTIFY` or `pg_notify()`:

```lua
db:on("notification", function(channel, payload, pid) end)
db:listen("players")
```

Notifications arrive whether the connection has queries to run or not, though not in the middle of one: what comes in while a query runs is passed on once it is over. Their listeners are called from the `Think` hook, like those of a query, so on a server that does not think they pile up until it does.

A connection that listens is not garbage collected: it stays, and its listeners are called, even if nothing refers to it anymore. That ends when `unlisten` was called for each of its channels, or with the next `connect`.

It also does not wait for a query when it is lost. It is opened again as soon as the loss is noticed, and if that does not work, it is tried again every 5 seconds. Whatever is synchronous waits for such an attempt the way it waits for a query. Nothing of what was sent while the connection was away arrives later, which is what the "reconnect" listeners are there to be told.

In the rows of a result, booleans are booleans, numbers are numbers and everything else is a string. NULL is nil. Whole numbers beyond 2^53 are strings too, because a Lua number would round them: a 64-bit SteamID from a `bigint` column comes back as `"76561198012345678"`.

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

-- DatabaseConnection class

-- Connect to the specified database.
--
-- host, user, password and db are taken as they are, spaces and quotes
-- included. extra_string_to_append is a piece of libpq connection string,
-- e.g. "sslmode=require connect_timeout=10". What it sets wins over the
-- other arguments.
--
-- This blocks until the server has answered. A server that does not is given
-- up on after 5 seconds, unless connect_timeout says otherwise. The same goes
-- for a lost connection that is opened again.
--
-- Returns true if successful, false and the error message otherwise.
-- If it fails, a connection that was there before stays as it was. If it
-- works, the prepared statements and the channels of that connection are
-- forgotten.
function DatabaseConnection:connect(host, user, password, db, port, extra_string_to_append)

-- Disconnect from current database.
function DatabaseConnection:disconnect()

-- Create a SQL query
--
-- query_string: SQL to execute
--
-- Returns a DatabaseQuery object
function DatabaseConnection:query(query_string)

-- Create a prepared query
--
-- name: ID of the prepared statement
--
-- Returns a PreparedQuery object
function DatabaseConnection:query_prepared(name)

-- Escape dangerous characters in a string.
-- Throws an error if the string is not valid in the encoding of the connection.
--
-- Returns an escaped string
function DatabaseConnection:escape(str)

-- Turn binary data into the text that a bytea column takes ("\x00ff"),
-- as a parameter or, quoted, inside of a query.
--
-- Returns a string
function DatabaseConnection:escape_bytea(data)

-- Turn the value of a bytea column back into binary data
--
-- Returns a string
function DatabaseConnection:unescape(escaped_str)

-- Quote a string
--
-- Returns a quoted string
function DatabaseConnection:quote(str)

-- Quote a column name
--
-- Returns a quoted string
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
-- Returns protocol version
function DatabaseConnection:protocol_version()

-- Get the server server version
--
-- Returns server version
function DatabaseConnection:server_version()

-- Activate this connection: open it again if it was closed or lost
--
-- Avoid using this as it's done automatically most of the time.
-- Use only if you know what you're doing.
function DatabaseConnection:activate()

-- Deactivate the currect connection: close it until a query needs it again.
-- Listening is no such need, nothing arrives until then.
function DatabaseConnection:deactivate()

-- Get whether the connection is open
function DatabaseConnection:is_open()

-- Prepare a query (register it with the server)
--
-- name: ID of the prepared statement, must not be empty
-- definition: the query itself, with $1, $2 and so on for its parameters
--
-- Returns true if successful
function DatabaseConnection:prepare(name, definition)

-- Unprepare the query (unregister it on the server)
--
-- name: ID of the prepared statement
--
-- Returns true if successful
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

-- Set the current connection encoding
-- Literally all this does:
-- "SET CLIENT_ENCODING TO '" + new_encoding + "';"
--
-- encoding: Encoding to set (default utf8)
function DatabaseConnection:set_encoding(encoding)

-- DatabaseQuery class

-- Execute the current query
--
-- vararg: the values of $1, $2 and so on in the query, if it has any.
-- See PreparedQuery:run for what they can be. A query that is given
-- parameters has to be a single statement.
--
-- Returns nothing, unless the query is synchronous:
-- true, the result table, the amount of items in it and the amount of
-- affected rows if successful,
-- false, the error message and the error details otherwise.
-- These are the arguments of the "success" and "error" callbacks below.
function DatabaseQuery:run(...)

-- Set the query to be synchronous.
-- This will lock the current thread while the query is being executed.
-- USE WITH CAUTION
--
-- sync: set to false for default async behavior
function DatabaseQuery:set_sync(sync)

-- Called once the query returns.
--
-- Callback arguments:
-- result: the result table. Numerically indexed (e.g. { [1] = {...}, [2] = {...} })
-- size: the amount of items in the results table
-- affected: the amount of rows that were inserted, updated or deleted. For a
--   query that returns rows this is their amount, for anything else it is 0.
--
-- If the query was several statements, all of this is about the last one.
DatabaseQuery:on("success", function(result, size, affected) end)

-- Called if the query has failed.
--
-- Callback arguments:
-- error: the error message returned by the server
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

-- PreparedQuery
-- PreparedQuery shares all of the members with DatabaseQuery, except for #run:

-- Execute the prepared query
--
-- vararg: which arguments to place into the blank spots of the prepared query.
-- Strings, numbers and booleans are supported, nil is NULL. Anything else
-- throws an error, and so does a string with a zero byte in it: binary data
-- goes through DatabaseConnection:escape_bytea first.
--
-- The server has to know the type of every parameter. Where it cannot tell
-- from the query, say so: "select $1::int".
function PreparedQuery:run(...)
```

## License

gmsv_pg is under the MIT license, see `LICENSE.md`. The binaries also contain gloo (MIT), libpq (PostgreSQL License) and OpenSSL (Apache License 2.0), and the Windows ones the MinGW-w64 runtime (mostly Zope Public License 2.1). `./build.sh` writes the license texts of all of them to `pg/bin/LICENSES.txt`, which has to be distributed together with the binaries.
