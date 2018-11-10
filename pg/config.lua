return {
  libname = "gmsv_pg_"..(os.target() == 'windows' and 'win32' or 'linux')..".dll",
  garrysmod = "/home/meow/Flux"
}
