# Playground

The playground provides a code editor and is able to compile Mialu to WebAssembly in the browser real-time. There are two options to run the playground:

- Docker container that builds the compiler and serves the page (using Docker).
- Python script that starts a server for the playground (without Docker).

See [Getting Started](getting-started.md) for the prerequisites of each platform.

## Launching the Playground using Docker

On Linux, install [Docker Engine](https://docs.docker.com/engine/install/) with the Compose plugin, then in the root of the repository:

```
docker compose up --build
```

If your user is not in the `docker` group, put `sudo` before the command.

On Windows, install [Docker Desktop](https://docs.docker.com/desktop/setup/install/windows-install/) and start it. Then in PowerShell, in the root of the repository:

```
docker compose up --build
```

Once the build is complete, open http://localhost:8008/playground.html in your browser when the log says the server is running. To stop the server, press Ctrl+C, or run `docker compose down` if you started the container in the background (`docker compose up --build -d`).

## Launching the Playground without Dockerw

The playground can run as a web page that is served by a small Python script, [tests/wasm/serve.py](../tests/wasm/serve.py). Without Docker, you need Python 3 and a Mialu compiler that you have built yourself.

1. Build the compiler as described in [Getting Started](getting-started.md#building-the-compiler). The script does not build it, and it answers every request with an error when `build/mialuc.exe` (build target executable) is missing.
2. In the root of the repository, start the server:

    ```
    python tests/wasm/serve.py
    ```

    A different port than the default 8008 can be given after the script: `python tests/wasm/serve.py 8080`.
3. Open http://localhost:8008/playground.html in your browser. Stop the server with Ctrl+C.
