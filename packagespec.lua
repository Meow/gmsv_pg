Package:describe(function(s)
  s.id          = 'gmsv_pg'
  s.name        = 'gmsv_pg'
  s.version     = '2.0.0'
  s.date        = '2026-10-07'
  s.summary     = 'A PostgreSQL adapter for Garry\'s Mod.'
  s.description = 'A binary module around libpq: asynchronous and synchronous queries with parameters, '..
                  'prepared statements, pipelining, LISTEN/NOTIFY and reconnects, with libpq, OpenSSL '..
                  'and the C++ runtime linked in.'
  s.authors     = { 'TeslaCloud Studios', 'Luna Fox' }
  s.website     = 'https://github.com/Meow/gmsv_pg'
  s.license     = 'MIT'
  s.serverside  = true
  s.binaries    = 'pg/bin'
end)
