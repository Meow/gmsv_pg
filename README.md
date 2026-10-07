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

That's all there is to it. libpq, libpqxx, OpenSSL and the C++ runtime are all linked into the module, so nothing else has to be installed on the server.

The Linux binaries need glibc 2.35 or newer, which means Debian 12, Ubuntu 22.04 or anything more recent.

## Building

If you have Docker installed, just run `./build.sh`. It builds the binaries for all four platforms in a container, with no other tools needed on your machine, and places them in the `pg/bin` folder. The first build takes a few minutes, because OpenSSL, libpq and libpqxx are built from source for each platform.

The versions of the libraries are set at the top of the `Dockerfile`.

To build without Docker:
1. Install premake5 and a compiler that supports C++20 (GCC 13 or newer).
2. Build static libraries of libpqxx 8, libpq and OpenSSL, and put them into `pg/deps/<system>-<architecture>/lib`, with their headers in `pg/deps/<system>-<architecture>/include` (e.g. `pg/deps/linux-x86_64`). `docker/build-deps.sh` shows how they are built for the releases. Use the `--deps=path` option of premake to keep them somewhere else.
3. In the `pg` folder, run `premake5 gmake`.
4. Navigate to the `project` folder and run `make config=x86` or `make config=x86_64`.
5. The compiled binary should be in the `pg/bin` folder.

## Usage

This module doesn't have all of the features implemented yet, it's being worked on.

**Most of the functions are able to throw Lua errors in case of bad input. Be careful!**

Queries are asynchronous by default: they run on a background thread, one after another, and their callbacks are called from the `Think` hook. An empty server only thinks if `sv_hibernate_think` is set to `1`.

A connection that was lost is opened again when the next query needs it. The query that was running when it was lost fails.

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
--
-- Returns an escaped string
function DatabaseConnection:escape(str)

-- Return the escaped string back to normal
--
-- Returns a normal string
function DatabaseConnection:unescape(escaped_str)

-- Quote a string
--
-- Returns a quoted string
function DatabaseConnection:quote(str)

-- Quote a column name
--
-- Returns a quoted string
function DatabaseConnection:quote_name(str)

-- Cancel the current query, if any.
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

-- Deactivate the currect connection: close it until a query needs it again
function DatabaseConnection:deactivate()

-- Get whether the connection is open
function DatabaseConnection:is_open()

-- Prepare a query (register it with the server)
--
-- name: ID of the prepared statement
-- definition: the query itself
--
-- Returns true if successful
function DatabaseConnection:prepare(name, definition)

-- Unprepare the query (unregister it on the server)
--
-- name: ID of the prepared statement
--
-- Returns true if successful
function DatabaseConnection:unprepare(name)

-- Set the current connection encoding
-- Literally all this does:
-- "SET CLIENT_ENCODING TO '" + new_encoding + "';"
--
-- encoding: Encoding to set (default utf8)
function DatabaseConnection:set_encoding(encoding)

-- DatabaseQuery class

-- Execute the current query
--
-- Returns nothing, unless the query is synchronous:
-- true, the result table and the amount of items in it if successful,
-- false and the error message otherwise
function DatabaseQuery:run()

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
DatabaseQuery:on("success", function(result, size) end)

-- Called if the query has failed.
--
-- Callback arguments:
-- error: the error message returned by the server
DatabaseQuery:on("error", function(error) end)

-- PreparedQuery
-- PreparedQuery shares all of the members with DatabaseQuery, except for #run:

-- Execute the prepared query
--
-- vararg: which arguments to place into the blank spots of the prepared query.
-- Strings, numbers and booleans are supported, nil is NULL.
function PreparedQuery:run(...)
```

## License

gmsv_pg is under the MIT license, see `LICENSE.md`. The binaries also contain gloo (MIT), libpqxx (BSD 3-Clause), libpq (PostgreSQL License) and OpenSSL (Apache License 2.0), and the Windows ones the MinGW-w64 runtime (mostly Zope Public License 2.1). `./build.sh` writes the license texts of all of them to `pg/bin/LICENSES.txt`, which has to be distributed together with the binaries.
