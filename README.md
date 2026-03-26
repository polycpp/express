# polycpp/express

C++ port of [Express.js](https://expressjs.com/) for polycpp -- a fast, minimalist web framework for C++20.

## Quick Start

```cpp
#include <polycpp/express/express.hpp>
#include <polycpp/event_loop.hpp>
#include <print>

int main() {
    using namespace polycpp::express;

    auto app = express();

    app.get("/", [](auto& req, auto& res) {
        res.send("Hello World!");
    });

    app.get("/json", [](auto& req, auto& res) {
        res.json({{"message", "Hello"}});
    });

    app.listen(3000, []() {
        std::println("Server running on port 3000");
    });

    polycpp::EventLoop::run();
    return 0;
}
```

## Features

- **Routing** -- GET, POST, PUT, DELETE, PATCH, HEAD, OPTIONS, ALL
- **Middleware** -- use(), error handlers, param handlers
- **Sub-routers** -- Router class for modular routing
- **Request** -- params, query, body, headers, content negotiation, IP
- **Response** -- send, json, redirect, cookie, sendFile, type, status
- **Body parsing** -- json(), urlencoded(), raw(), text()
- **Static files** -- static_() middleware for serving directories
- **Settings** -- set/get/enable/disable application settings
- **Sub-apps** -- mount Express apps at path prefixes

## Build

Requires CMake 3.20+ and a C++20 compiler (GCC 13+ or Clang 16+).

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
cd build && ctest --output-on-failure
```

## Dependencies

- [polycpp](https://github.com/enricohuang/polycpp) -- Core C++ Node.js API
- [polycpp/path-to-regexp](https://github.com/polycpp/path-to-regexp) -- URL pattern matching
- [polycpp/qs](https://github.com/polycpp/qs) -- Nested query string parsing
- [polycpp/cookie](https://github.com/polycpp/cookie) -- HTTP cookie handling
- [polycpp/mime](https://github.com/polycpp/mime) -- MIME type database
- [polycpp/negotiate](https://github.com/polycpp/negotiate) -- Content negotiation
- [polycpp/ipaddr](https://github.com/polycpp/ipaddr) -- IP address parsing

All dependencies are fetched automatically via CMake FetchContent.

## License

MIT
