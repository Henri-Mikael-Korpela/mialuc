# Serves the playground: builds the compiler, then runs tests/wasm/serve.py with it.

FROM gcc:14 AS build
RUN apt-get update && apt-get install -y --no-install-recommends cmake make \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY CMakeLists.txt ./
COPY src ./src
RUN cmake -S . -B build && cmake --build build

FROM python:3-slim
WORKDIR /app
COPY --from=build /app/build/mialuc ./build/mialuc
COPY tests/wasm ./tests/wasm
COPY test-code ./test-code
# Inside a container the server has to listen on all interfaces to be reachable through the
# published port. docker-compose.yml publishes it on the loopback of the host only.
ENV MIALU_HOST=0.0.0.0
EXPOSE 8008
CMD ["python", "tests/wasm/serve.py"]
