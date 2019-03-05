# gmsv_pg
A PostgreSQL adapter for Garry's Mod.

## Installation
The pre-compiled binaries are located in the `pg/bin` folder.

Copy-paste the `gmsv_pg_*.dll` to your server's `lua/bin` folder, where `*` if your platform's suffix (win32 or linux). Then follow the platform-specific instructions below:

### Windows
1. Make sure you have Microsoft Visual C++ 2015 Redist installed
2. Navigate to the `runtime_depends/windows` folder
3. Copy-paste the folder's contents to your server's root (where srcds.exe is)

### Linux
On Linux there are two ways to install dependencies.

**via apt**
```sh
# make sure you have postgresql repositories added beforehand!
sudo apt-get install libpq-dev:i386
```

**the lazy way**
1. Navigate to `runtime_depends/linux`
2. Copy-paste the `libpq.so.5` file to your server's root (where srcds_linux is)

Please note that even if you do the "lazy way" it might still be necessary to `apt-get` the `libpq-dev:i386` package. If you run into issues, please make sure that it's the first step that you take before complaining.

## Usage
This module doesn't have all of the features implemented yet, it's being worked on.

**Most of the functions are able to throw Lua errors in case of bad input. Be careful!**

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

-- Activate this connection
--
-- Avoid using this as it's done automatically most of the time.
-- Use only if you know what you're doing.
function DatabaseConnection:activate()

-- Deactivate the currect connection
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
function PreparedQuery:run(...)
```
