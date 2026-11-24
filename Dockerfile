# Serves the documentation and the playground, which are mialudoc, a program that is written in Mialu
# for Windows. It runs on Linux under Wine, and so does the compiler that it starts for the
# playground, so both are built for Windows here:
#
# - the compiler of Linux, built with GCC, builds mialudoc, as it builds any program for the
#   x64-win32 target, which is a file that it only writes,
# - the compiler of Windows, built with MinGW-w64, is the one that mialudoc runs.

FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc libc6-dev make cmake gcc-mingw-w64-x86-64 \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY CMakeLists.txt VERSION ./
COPY src ./src
RUN cmake -S . -B build-linux && cmake --build build-linux
RUN cmake -S . -B build -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
        -DCMAKE_EXE_LINKER_FLAGS=-static -DMIALU_BUILD_MIALUDOC=OFF \
    && cmake --build build
RUN build-linux/mialuc build --target x64-win32 -o build/mialudoc.exe src/mialudoc/mialudoc.mu

FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends wine64 \
    && rm -rf /var/lib/apt/lists/*
# Wine makes its prefix, a Windows installation, in the first run, and does not need the packages
# that it would ask to download for Internet Explorer and .NET.
# The programs of Wine are in a directory that is not in the PATH on Debian.
ENV PATH=/usr/lib/wine:$PATH WINEPREFIX=/wine WINEDEBUG=-all WINEDLLOVERRIDES=mscoree,mshtml=
RUN wine64 wineboot --init; wineserver64 --wait || true
WORKDIR /app
COPY --from=build /app/build/mialuc.exe /app/build/mialudoc.exe ./build/
# The sources of the project, which the compiler reads the standard library from, the page of the
# internals of the compiler is made from, and the page of the source statistics counts the lines of,
# with the tests, the scripts and the examples that it counts too.
COPY src ./src
COPY scripts ./scripts
COPY examples ./examples
COPY test-code ./test-code
# The pages of the playground, the documents and the logo.
COPY tests/wasm ./tests/wasm
COPY docs ./docs
COPY media/mialu-logo.png ./media/mialu-logo.png
# Inside a container the server has to listen on all interfaces to be reachable through the
# published port. docker-compose.yml publishes it on the loopback of the host only.
EXPOSE 8008
CMD ["wine64", "build/mialudoc.exe", "8008", ".", "0.0.0.0"]
