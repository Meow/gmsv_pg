# Builds gmsv_pg for win32, win64, linux x86 and linux x86_64.
# Use ./build.sh, which exports the binaries to pg/bin.

# The libraries the module is made of. They are built from source for every
# target, so that all of them can be linked into the module statically.
FROM debian:trixie-slim AS sources

RUN apt-get update \
 && apt-get install -y --no-install-recommends ca-certificates curl bzip2 \
 && rm -rf /var/lib/apt/lists/*

ARG PREMAKE_VERSION=5.0.0
ARG PREMAKE_SHA256=8e50e143402de3ce0f0fefe4bb3f4f6a7db46c7d66203dc9f134c0348ebfe6c5
RUN curl -fsSL -o /tmp/premake.tar.gz \
      "https://github.com/premake/premake-core/releases/download/v${PREMAKE_VERSION}/premake-${PREMAKE_VERSION}-linux.tar.gz" \
 && echo "${PREMAKE_SHA256}  /tmp/premake.tar.gz" | sha256sum -c - \
 && tar -xzf /tmp/premake.tar.gz -C /usr/local/bin premake5 \
 && chmod +x /usr/local/bin/premake5 \
 && rm /tmp/premake.tar.gz

ARG OPENSSL_VERSION=3.5.9
ARG OPENSSL_SHA256=603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a
ARG POSTGRES_VERSION=18.6
ARG POSTGRES_SHA256=555610c24d53e4316da5b7d3fc25c279d96856d5e0e23ee308c328c5fa881d9f
ARG LIBPQXX_VERSION=8.0.2
ARG LIBPQXX_SHA256=028c5fba4982e759fe182af1103ccd08b02bb4e1f9f12a551e01d21cac0c4440
RUN mkdir -p /deps/src/openssl /deps/src/postgresql /deps/src/libpqxx \
 && cd /tmp \
 && curl -fsSL -o openssl.tar.gz \
      "https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/openssl-${OPENSSL_VERSION}.tar.gz" \
 && curl -fsSL -o postgresql.tar.bz2 \
      "https://ftp.postgresql.org/pub/source/v${POSTGRES_VERSION}/postgresql-${POSTGRES_VERSION}.tar.bz2" \
 && curl -fsSL -o libpqxx.tar.gz \
      "https://github.com/jtv/libpqxx/archive/refs/tags/${LIBPQXX_VERSION}.tar.gz" \
 && printf '%s  %s\n' \
      "${OPENSSL_SHA256}" openssl.tar.gz \
      "${POSTGRES_SHA256}" postgresql.tar.bz2 \
      "${LIBPQXX_SHA256}" libpqxx.tar.gz \
    | sha256sum -c - \
 && tar -xzf openssl.tar.gz -C /deps/src/openssl --strip-components=1 \
 && tar -xjf postgresql.tar.bz2 -C /deps/src/postgresql --strip-components=1 \
 && tar -xzf libpqxx.tar.gz -C /deps/src/libpqxx --strip-components=1 \
 && rm openssl.tar.gz postgresql.tar.bz2 libpqxx.tar.gz

# Linux, native toolchain with multilib for x86.
#
# A binary only loads where glibc is at least as new as the one it was built
# against. That is why the Linux targets are built on an older distribution
# than the Windows ones: Ubuntu 22.04 has glibc 2.35, which makes the module
# work there, on Debian 12 and on everything newer. libpqxx needs a newer
# compiler than Ubuntu 22.04 has, GCC 14 comes from the Ubuntu Toolchain PPA.
# The /usr/include/asm link is what the gcc-multilib package would add: the
# kernel headers for x86.
FROM ubuntu:22.04 AS toolchain-linux

ARG TOOLCHAIN_PPA_KEY=60C317803A41BA51845E371A1E9377A2BA9EF27F
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      ca-certificates curl gnupg make perl bison flex \
 && curl -fsSL "https://keyserver.ubuntu.com/pks/lookup?op=get&search=0x${TOOLCHAIN_PPA_KEY}" \
    | gpg --dearmor -o /usr/share/keyrings/toolchain-ppa.gpg \
 && echo "deb [signed-by=/usr/share/keyrings/toolchain-ppa.gpg] https://ppa.launchpadcontent.net/ubuntu-toolchain-r/test/ubuntu jammy main" \
      > /etc/apt/sources.list.d/toolchain-ppa.list \
 && apt-get update \
 && apt-get install -y --no-install-recommends gcc-14-multilib g++-14-multilib \
 && ln -s gcc-14 /usr/bin/gcc \
 && ln -s g++-14 /usr/bin/g++ \
 && ln -s x86_64-linux-gnu/asm /usr/include/asm \
 && rm -rf /var/lib/apt/lists/*

COPY --from=sources /usr/local/bin/premake5 /usr/local/bin/premake5
COPY --from=sources /deps/src /deps/src
COPY docker/build-deps.sh /usr/local/bin/build-deps

# Windows, cross-compiled with MinGW-w64.
FROM debian:trixie-slim AS toolchain-windows

# Without pipelining, because deb.debian.org has been seen answering a request
# with the package of another one, which fails the whole installation.
RUN apt-get update \
 && apt-get install -y --no-install-recommends -o Acquire::http::Pipeline-Depth=0 \
      make perl bison flex binutils \
      gcc-mingw-w64-i686 g++-mingw-w64-i686 \
      gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 \
 && rm -rf /var/lib/apt/lists/*

COPY --from=sources /usr/local/bin/premake5 /usr/local/bin/premake5
COPY --from=sources /deps/src /deps/src
COPY docker/build-deps.sh /usr/local/bin/build-deps

# One stage per target, so that Docker builds them in parallel.
FROM toolchain-linux AS deps-linux-x86
RUN build-deps linux-x86

FROM toolchain-linux AS deps-linux-x86_64
RUN build-deps linux-x86_64

FROM toolchain-windows AS deps-windows-x86
RUN build-deps windows-x86

FROM toolchain-windows AS deps-windows-x86_64
RUN build-deps windows-x86_64

FROM toolchain-linux AS build-linux

COPY --from=deps-linux-x86 /deps/linux-x86 /deps/linux-x86
COPY --from=deps-linux-x86_64 /deps/linux-x86_64 /deps/linux-x86_64

WORKDIR /src
COPY include include
COPY pg/premake5.lua pg/premake5.lua
COPY pg/src pg/src
WORKDIR /src/pg

RUN premake5 --deps=/deps gmake \
 && make -C project config=x86 \
 && make -C project config=x86_64 \
 && mkdir /out \
 && cp bin/*.dll /out/

FROM toolchain-windows AS build-windows

COPY --from=deps-windows-x86 /deps/windows-x86 /deps/windows-x86
COPY --from=deps-windows-x86_64 /deps/windows-x86_64 /deps/windows-x86_64

WORKDIR /src
COPY include include
COPY pg/premake5.lua pg/premake5.lua
COPY pg/src pg/src
WORKDIR /src/pg

RUN premake5 --os=windows --deps=/deps gmake \
 && make -C project config=x86 \
      CC=i686-w64-mingw32-gcc CXX=i686-w64-mingw32-g++ AR=i686-w64-mingw32-ar \
 && make -C project config=x86_64 \
      CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++ AR=x86_64-w64-mingw32-ar \
 && mkdir /out \
 && cp bin/*.dll /out/

FROM toolchain-windows AS package

COPY --from=build-linux /out/ /out/
COPY --from=build-windows /out/ /out/

# Everything is linked statically except what cannot be: the C library on
# Linux and the system DLLs on Windows. Fail on any other dynamic dependency.
RUN for f in /out/*.dll; do \
      objdump -p "$f" > /tmp/headers || exit 1; \
      awk -v f="${f##*/}" '/NEEDED|DLL Name:/ { print f ": " $NF }' /tmp/headers; \
    done > /tmp/deps \
 && cat /tmp/deps \
 && ! grep -viE ': (libc\.so\.6|libm\.so\.6|ld-linux(-x86-64)?\.so\.2|(kernel32|msvcrt|advapi32|crypt32|secur32|shell32|user32|ws2_32)\.dll)$' /tmp/deps \
 && rm /tmp/headers /tmp/deps

# Fail if the Linux binaries ask for a newer glibc than the one they are
# meant to be built against.
ARG GLIBC_VERSION=2.35
RUN for f in /out/*_linux*.dll; do \
      needs=$(objdump -T "$f" | grep -oE 'GLIBC_[0-9.]+' | sort -uV | tail -n 1); \
      echo "${f##*/} needs $needs"; \
      [ "$(printf '%s\n' "GLIBC_${GLIBC_VERSION}" "$needs" | sort -V | tail -n 1)" = "GLIBC_${GLIBC_VERSION}" ] || exit 1; \
    done

# The binaries are not compressed with UPX, unlike in gmsv_file: packed, the
# x86 Linux module crashes the server when it is loaded.
FROM scratch
COPY --from=package /out/ /
