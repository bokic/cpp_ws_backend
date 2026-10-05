# cpp_ws_backend

`cpp_ws_backend` is a high-performance, lightweight C++23 library for building web backend services and microservices that interface with web servers (such as Nginx or Apache) via the FastCGI protocol.

---

## Features

- **High Performance & Minimal Resource Footprint**: Native C++ execution with negligible memory and CPU overhead.
- **Multi-Threaded Architecture**: Configurable thread pool with synchronized worker queues for high-concurrency request handling.
- **Expressive URI Routing**: Regex-based and literal path routing with HTTP method filtering (`GET`, `POST`, `PUT`, `PATCH`, `DELETE`) and capture parameter extraction (e.g., matching `^/api/users/([0-9]+)$` extracts the user ID directly into `uri_params`).
- **Modern C++23**: Type-safe, RAII-driven design leveraging `std::string_view`, `std::function`, and standard exception hierarchies.
- **Graceful Lifecycle Management**: Clean signal handling (`SIGINT`, `SIGTERM`, `SIGUSR1`) for worker draining and server shutdown.

---

## Prerequisites

- **C++ Compiler**: GCC $\ge$ 13 or Clang $\ge$ 16 (with C++23 support)
- **CMake**: $\ge$ 3.16
- **Libraries**:
  - `libfcgi` / `libfcgi-dev` (FastCGI development library)
  - `pthread` (POSIX threads)

### On Debian / Ubuntu:
```bash
sudo apt update
sudo apt install build-essential cmake libfcgi-dev
```

### On Fedora / RHEL:
```bash
sudo dnf install gcc-c++ cmake fcgi-devel
```

### On Arch Linux:
```bash
sudo pacman -S base-devel cmake fcgi
```

---

## Building and Installing

### Quick Build
Use the root build script to configure and compile the library and demo applications:

```bash
./build.sh
```

### CMake Build
Alternatively, configure and build manually with CMake:

```bash
mkdir -p build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

To install the static library and header files into the system:
```bash
sudo cmake --install build
```

---

## Quick Start

```cpp
#include <backend/wsserver.h>
#include <backend/wsworker.h>
#include <backend/wsroute.h>
#include <fcgio.h>
#include <cstring>

void handle_hello(backend::wsworker *worker,
                  const std::map<std::string, std::string> &header,
                  const std::list<std::string> &uri_params)
{
    const char *body = "{\"message\": \"Hello from cpp_ws_backend!\"}";
    FCGX_FPrintF(worker->out(),
                 "Status: 200 OK\r\n"
                 "Content-Type: application/json\r\n"
                 "Content-Length: %d\r\n\r\n%s",
                 static_cast<int>(std::strlen(body)), body);
}

// Handler with capture parameter extraction: ^/api/users/([0-9]+)$
void handle_user(backend::wsworker *worker,
                 const std::map<std::string, std::string> &header,
                 const std::list<std::string> &uri_params)
{
    // uri_params contains regex capture groups in order
    std::string user_id = uri_params.empty() ? "" : uri_params.front();
    std::string body = "{\"user_id\": " + user_id + "}";
    FCGX_FPrintF(worker->out(),
                 "Status: 200 OK\r\n"
                 "Content-Type: application/json\r\n"
                 "Content-Length: %d\r\n\r\n%s",
                 static_cast<int>(body.length()), body.c_str());
}

int main()
{
    backend::wsserver server;

    // Register routes: static paths and regex patterns with capture groups
    server.add_routes({
        {"/api/hello", backend::GET, handle_hello},
        {"^/api/users/([0-9]+)$", backend::GET, handle_user},
    });

    // Initialize server on port 9000 with a backlog of 100 and 4 worker threads
    server.init(":9000", 100, 4);

    return server.run();
}
```

---

## Demo Applications

Complete runnable demo projects are provided in the [`demos/`](demos) directory:
- **[SQLite Demo](demos/sqlite-demo)**: Full database-backed web application using SQLite (`chinook.db`), `json-c`, and jqGrid frontend.
- **[System Journal Demo](demos/journal-demo)**: FastCGI service exposing systemd logs via `sd_journal` as JSON.

For details on architecture, endpoints, and running instructions, see [DEMOS.md](DEMOS.md).

---

## Roadmap

Upcoming features and architectural goals are tracked in [TODO.md](TODO.md).

---

## License

This project is licensed under the [LGPL-3.0 License](LICENSE).
