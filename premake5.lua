local suffixes = {
  linux   = "_linux",
  macosx  = "_macosx",
  windows = "_win32",
}

defines "GMMODULE"
cppdialect "C++11"

includedirs {
  "include",
  "vendor/gmod-module-base/include",
  "vendor/variant/include/mpark"
}

targetprefix "gmsv_"
targetsuffix(suffixes[os.target()])
targetextension ".dll"
