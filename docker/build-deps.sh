#!/bin/sh
# Builds static OpenSSL, libpq and libpqxx for one target and installs them to
# /deps/<target>. Expects the unpacked sources in /deps/src, see the Dockerfile.
set -eu

target=$1
prefix=/deps/$target
build=x86_64-linux-gnu
jobs=$(nproc)

case $target in
  linux-x86)
    host=i686-linux-gnu
    openssl_target=linux-x86
    cc='gcc -m32' cxx='g++ -m32' cross=
    ;;
  linux-x86_64)
    host=x86_64-linux-gnu
    openssl_target=linux-x86_64
    cc='gcc' cxx='g++' cross=
    ;;
  windows-x86)
    host=i686-w64-mingw32
    openssl_target=mingw
    cc=$host-gcc cxx=$host-g++ cross=$host-
    ;;
  windows-x86_64)
    host=x86_64-w64-mingw32
    openssl_target=mingw64
    cc=$host-gcc cxx=$host-g++ cross=$host-
    ;;
  *)
    echo "unknown target: $target" >&2
    exit 1
    ;;
esac

case $target in
  linux-*)
    # The libraries end up in a shared object, so they have to be PIC.
    flags='-O2 -fPIC'
    syslibs='-lm'
    # Where Debian, Ubuntu and most other distributions keep the CA bundle
    # that sslrootcert=system uses.
    openssldir=/etc/ssl
    ;;
  windows-*)
    flags='-O2'
    syslibs='-lws2_32 -lsecur32 -lshell32 -lcrypt32 -lgdi32 -ladvapi32 -luser32'
    # Same as the official Windows builds. The default for MinGW would be
    # C:\usr\local\ssl, which any local user can create and put a config in.
    openssldir='C:/Program Files/Common Files/SSL'
    ;;
esac

work=$(mktemp -d)
cp -a /deps/src/. "$work"
mkdir -p "$prefix/include" "$prefix/lib"

# OpenSSL, for TLS connections.
cd "$work/openssl"
# shellcheck disable=SC2086
./Configure "$openssl_target" $flags \
  --prefix="$prefix" --libdir=lib --openssldir="$openssldir" \
  ${cross:+--cross-compile-prefix=$cross} \
  no-shared no-tests no-apps no-docs no-module no-engine no-legacy no-comp \
  no-ui-console
make -j"$jobs" build_libs
make install_dev

# libpq. Only the static client library is needed, not the server.
cd "$work/postgresql"
./configure --build="$build" --host="$host" --prefix="$prefix" \
  --without-readline --without-zlib --without-icu --with-ssl=openssl \
  CC="$cc" CFLAGS="$flags" \
  CPPFLAGS="-I$prefix/include" LDFLAGS="-L$prefix/lib" LIBS="$syslibs"
make -j"$jobs" -C src/backend generated-headers
make -j"$jobs" -C src/common all
make -j"$jobs" -C src/port all
make -j"$jobs" -C src/interfaces/libpq all-static-lib
case $target in
  windows-*)
    # Here libpq.a is the import library of libpq.dll, so the static library
    # has to be put together by hand. The two objects left out only belong
    # in the DLL.
    (
      cd src/interfaces/libpq
      # shellcheck disable=SC2046
      "${cross}ar" crs "$prefix/lib/libpq.a" \
        $(ls ./*.o | grep -v -e '_shlib\.o$' -e '/win32ver\.o$')
    )
    ;;
  *)
    cp src/interfaces/libpq/libpq.a "$prefix/lib/"
    ;;
esac
# The _shlib variants are the ones libpq itself links against.
cp src/common/libpgcommon_shlib.a src/port/libpgport_shlib.a "$prefix/lib/"
cp src/interfaces/libpq/libpq-fe.h src/interfaces/libpq/libpq-events.h \
  src/include/postgres_ext.h "$prefix/include/"

# libpqxx. Its configure wants to link a program against libpq, which takes
# more than -lpq when libpq is static. Only a static library is built here, so
# nothing gets linked for real, and the check is answered up front instead.
cd "$work/libpqxx"
./configure --build="$build" --host="$host" --prefix="$prefix" \
  --disable-shared --enable-static --disable-documentation \
  --with-postgres-include="$prefix/include" --with-postgres-lib="$prefix/lib" \
  CXX="$cxx" CXXFLAGS="-std=c++20 $flags" ac_cv_lib_pq_PQexec=yes
# Just the library, the examples are of no use here.
make -j"$jobs" src/libpqxx.la
make install-libLTLIBRARIES
make -C include install

cd /
rm -rf "$work" "$prefix/share" "$prefix/lib/pkgconfig" "$prefix/lib/cmake" \
  "$prefix"/lib/*.la
ls -l "$prefix/lib"
