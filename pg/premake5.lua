newoption {
  trigger = 'deps',
  value = 'path',
  default = './deps',
  description = 'Where the static libraries the module is linked with are, '..
    'as <path>/<system>-<architecture>/{include,lib}'
}

-- Garry's Mod picks the binary to load by its platform suffix:
-- https://wiki.facepunch.com/gmod/Creating_Binary_Modules:_Premake
local suffixes = {
  windows = { x86 = '_win32', x86_64 = '_win64' },
  linux = { x86 = '_linux', x86_64 = '_linux64' }
}

local is_windows = os.target() == 'windows'
local is_linux = os.target() == 'linux'
local suffix = assert(suffixes[os.target()], 'unsupported target system: '..os.target())
local deps = path.getabsolute(_OPTIONS['deps'])..'/'..os.target()

workspace 'pg'
  location './project'
  configurations { 'x86', 'x86_64' }
  symbols 'Off'
  editandcontinue 'Off'
  defines { 'NDEBUG' }
  optimize 'On'

  -- Everything is linked statically for compatibility, the server comes with
  -- a C++ runtime of its own that the module must not depend on.
  if is_linux then
    linkoptions {
      '-static-libstdc++',
      '-static-libgcc',
      -- Keep the libraries to the module itself. Whatever else is loaded into
      -- the server may bring a different version of the same library along.
      '-Wl,--exclude-libs,ALL',
      '-Wl,--no-undefined'
    }
  elseif is_windows then
    staticruntime 'on'

    -- MinGW ignores staticruntime, so its runtime libraries are named here.
    filter 'action:gmake*'
      linkoptions { '-static', '-static-libstdc++', '-static-libgcc' }
    filter {}
  end

  filter 'configurations:x86'
    architecture 'x86'
    vectorextensions 'SSE'
    targetsuffix(suffix.x86)
    includedirs { deps..'-x86/include' }
    libdirs { deps..'-x86/lib' }

  filter 'configurations:x86_64'
    architecture 'x86_64'
    targetsuffix(suffix.x86_64)
    includedirs { deps..'-x86_64/include' }
    libdirs { deps..'-x86_64/lib' }

project 'pg'
  kind 'SharedLib'
  language 'C++'
  cppdialect 'C++17'
  location './project'
  targetdir './bin'
  includedirs { '../include' }
  targetprefix 'gmsv_'
  targetextension '.dll'
  -- Only gmod13_open and gmod13_close are exported.
  visibility 'Hidden'
  inlinesvisibility 'Hidden'

  files {
    'src/**.cpp',
    'src/**.h',
    'src/**.hpp'
  }

  -- The order matters, a library has to come before the ones it depends on.
  links { 'pq', 'pgcommon_shlib', 'pgport_shlib', 'ssl', 'crypto' }

  if is_windows then
    links { 'ws2_32', 'secur32', 'shell32', 'crypt32', 'gdi32', 'advapi32', 'user32' }
  else
    pic 'On'
    links { 'm', 'pthread', 'dl' }
  end
