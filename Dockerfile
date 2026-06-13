# Multi-stage build:
#   toolchain - GCC 14 + CMake + libcurl headers (shared by build and test)
#   build     - release binary
#   test      - debug build with coverage; runs unit tests and enforces the
#               60% line-coverage gate (aspirational target: 70%)
#   runtime   - slim image with just the binary and runtime deps
#
#   docker build --target runtime -t rocket-watcher .         # the app
#   docker build --target test --progress=plain .             # tests + coverage

FROM gcc:14 AS toolchain
RUN apt-get update && apt-get install -y --no-install-recommends \
      cmake ninja-build libcurl4-openssl-dev ca-certificates tzdata \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY CMakeLists.txt ./
COPY include ./include
COPY src ./src
COPY tests ./tests

FROM toolchain AS build
RUN cmake -S . -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DROCKET_WATCHER_BUILD_TESTS=OFF \
    && cmake --build build

FROM toolchain AS test
RUN apt-get update && apt-get install -y --no-install-recommends \
      gcovr lcov clang-format clang-tidy \
    && rm -rf /var/lib/apt/lists/*
COPY .clang-format .clang-tidy ./
RUN cmake -S . -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=Debug \
      -DROCKET_WATCHER_COVERAGE=ON \
    && cmake --build build
RUN ctest --test-dir build --output-on-failure
# Coverage over the project's own code (FetchContent deps and main.cpp's
# argv shim excluded). Fails the image build under 60% line coverage.
RUN mkdir -p coverage && gcovr --root . \
      --filter 'src/' --filter 'include/' \
      --exclude 'src/main.cpp' \
      --print-summary \
      --fail-under-line 60 \
      --html-details coverage/coverage.html

# Same Debian release as the gcc:14 build image, so glibc versions match.
FROM debian:trixie-slim AS runtime
RUN apt-get update && apt-get install -y --no-install-recommends \
      libcurl4 ca-certificates tzdata \
    && rm -rf /var/lib/apt/lists/*
COPY --from=build /app/build/rocket-watcher /usr/local/bin/rocket-watcher

# Behavior config is mounted at /app/config.toml; secrets come from the
# environment (e.g. docker run --env-file .env). Alert records and logs
# live under /app/data and /app/logs - mount volumes to persist them.
WORKDIR /app
VOLUME ["/app/data", "/app/logs"]

ENTRYPOINT ["rocket-watcher"]
CMD ["watch"]
