local config = require "config"

workspace "pg"
  location "./project"
  configurations { "Release" }
  flags { "NoPCH", "NoImportLib"}
  symbols "On"
  editandcontinue "Off"
  vectorextensions "SSE"

  if os.target() ~= 'windows' then
    linkoptions{ "-static-libstdc++" }
  end

  configuration 'Release'
    defines { 'NDEBUG' }
    optimize "On"
    floatingpoint "Fast"
    architecture 'x86'
project "pg"
  kind "SharedLib"
  language "C++"
  location "./project"
  targetdir "./bin"
  libdirs { 'lib/'..os.target() }
  includedirs { 'include' }

  files {
    "src/**.cpp",
    "src/**.h",
    "src/**.hpp"
  }

  include "../premake5.lua"

  if os.target() == 'windows' then
    links { 'ws2_32', 'libeay32', 'libpqxx_static', 'libpq' }
  else
    pic "On"
    links { 'pthread', 'pq', 'pqxx' }
  end

  postbuildcommands {
    '{COPY} "%{cfg.buildtarget.abspath}" "'..config.garrysmod..'/garrysmod/lua/bin/'..config.libname..'"*',
  }
