# Changelog

All notable changes to gmsv_pg in 2026. The previous release, 1.0.2, dates from March 2019.

## 2.0.0 - 2026-10-07

A rewrite of the module around libpq itself, built for all four Garry's Mod server platforms. Most of the Lua API is unchanged, but see "Breaking changes" before upgrading.

### Breaking changes

- The `connect` host argument is passed to libpq as `host` rather than `hostaddr`, so hostnames work. Anything that relied on `hostaddr` semantics can set it in the extra connection string, which overrides the other arguments.
- A connection string now starts with `connect_timeout=5`. A server that does not answer is given up on after five seconds instead of stalling the main thread for minutes. The extra string can change it.
- Misuse throws a Lua error: a query string, event name or prepared statement name that is not a string, a listener that is not a function, a parameter of an unsupported type, a string with a zero byte in it where the server expects text, or a method called with a dot instead of a colon. Failures at the server do not throw. `connect`, `prepare`, `listen` and the other methods that talk to the server return `true`, or `false` and the error message.
- Result rows are typed. Booleans are booleans, numbers are numbers, NULL is nil (the key is absent), and everything else is a string. Whole numbers beyond 2^53, such as a 64-bit SteamID in a `bigint` column, are strings so that Lua does not round them. The type map that 1.x read from `pg_type` on connect is gone.
- Of two result columns with the same name only the first is in the row. A NULL in it no longer falls through to the second column.
- Prepared statement names must not be empty. The nameless statement is the one that a parameterised query leaves behind.
- The Linux binaries need glibc 2.35 or newer (Debian 12, Ubuntu 22.04 or later).

### Added

- Builds for 64-bit servers: `gmsv_pg_win64.dll` and `gmsv_pg_linux64.dll` alongside the 32-bit ones.
- Query parameters. `DatabaseQuery:run(...)` takes values for `$1`, `$2` and so on, like `PreparedQuery:run` already did. Strings, numbers, booleans and nil are supported. Whole numbers are sent as integers rather than as `1e+05`.
- `DatabaseConnection:escape_bytea(data)` turns binary data into the `\x` hex text that a `bytea` column takes. `unescape` is its counterpart and now validates its input.
- `LISTEN`/`NOTIFY` support: `DatabaseConnection:listen(channel)`, `DatabaseConnection:unlisten(channel)` and a `"notification"` event with `channel`, `payload` and `pid`. A connection that listens is not garbage collected until it stops listening or connects anew.
- A `"reconnect"` event on connections, for a listening connection that was opened again after it was lost, disconnected or deactivated.
- A `"notice"` event on queries, for warnings and `RAISE NOTICE` output from the server. It is called before `"success"` or `"error"`.
- The `"success"` callback receives a third argument, the number of affected rows.
- The `"error"` callback receives a `details` table: `sqlstate`, `severity`, `message`, `detail`, `hint`, `context`, `position`, `schema`, `table`, `column`, `datatype`, `constraint` as far as the server sent them, and `connection_lost` when that was the cause.
- Pipelining. Queued queries that have parameters or are prepared are sent to the server up to eight at a time instead of each waiting for the result of the one before. Order, results and error handling are unchanged.
- `cancel` sends its request over a connection of its own, encrypted if the query's connection is, and does not wait for the query. A cancelled query fails with sqlstate `57014`.
- A Docker build. `./build.sh` produces the binaries for all four platforms in a container, with OpenSSL and libpq built from source, and writes `pg/bin/LICENSES.txt` with the license texts of everything linked into them.

### Changed

- libpqxx is no longer used. The module talks to libpq directly, which also removes libpqxx from the binaries.
- libpq, OpenSSL and the C++ runtime are linked statically. Servers no longer need the Visual C++ redistributable, `libpq.so.5` or the `libpq-dev:i386` package.
- `connect` quotes its arguments, so passwords and other values with spaces or quotes in them work. The extra string is appended last and wins over the other arguments.
- A port given as a number is formatted as a whole number. Arguments that are neither strings nor, for the port, numbers are ignored.
- A failed `connect` leaves an existing connection as it was. A successful one forgets the prepared statements and channels of the previous connection.
- A lost connection is opened again when the next query needs it, with its encoding, prepared statements and channels. Queries that were on their way to the server fail with `connection_lost` and are not resent, because there is no telling which of them the server ran.
- A connection that the server ended while idle, after a restart or a timeout, is noticed before the next query is sent, and that query runs on a new connection. If a transaction was open, the next query fails instead and the one after runs on a new connection.
- After a failed attempt to reach the server, everything that needs the connection fails at once with that error, for as long as the attempt took. A queue of queries waits for an unreachable server once, not once per query. `connect` and `activate` always try.
- A listening connection that is lost is reopened as soon as the loss is noticed, and retried every five seconds.
- `escape`, `quote` and `quote_name` do not wait for a running query as a rule. They do wait for text that is not valid UTF-8, for non-ASCII text under another encoding, and when they have to reopen a deactivated connection. A lost connection still escapes.
- `deactivate` closes the connection until a query, `escape` or `quote` needs it. Listening is no such need.
- Synchronous calls wait for the queries on their way to the server, eight at most, but not for the ones queued behind them.
- The worker thread closes the connection cleanly on shutdown.
- Updated to the current Garry's Mod module headers and the `gloo` object and event helpers, built as C++17 (GCC 11 or newer).
- The copyright in `LICENSE.md` now names both TeslaCloud Studios and Luna Fox.

### Removed

- The 1.x prebuilt binaries, the bundled libpq and libpqxx headers, `runtime_depends` and `config.lua` are gone from the repository, about a thousand files.

### Documentation

- The README was rewritten: installation table by server branch and platform, build instructions with and without Docker, a usage guide covering parameters, result types, transactions, `COPY`, notifications, reconnection and pipelining, a full API reference and a license section.

### Build

- The Dockerfile's dependency check accepts `gdi32.dll`, which the Windows binaries import.
