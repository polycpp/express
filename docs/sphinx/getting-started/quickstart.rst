Quickstart
==========

This page walks through a minimal express program end-to-end. Copy
the snippet, run it, then jump to :doc:`../tutorials/index` for
task-oriented walkthroughs or :doc:`../api/index` for the full
reference.

We'll stand up a small HTTP server with two routes — one static, one
parameterised — JSON body parsing, and a graceful fallthrough to a
404 handler. Everything is real-runnable; no pseudocode.

Full example
------------

.. code-block:: cpp

   #include <polycpp/express/express.hpp>
   #include <iostream>

   using namespace polycpp::express;

   int main() {
       auto app = express();

       app.use(json());                     // parse application/json bodies
       app.use([](auto& req, auto& res, auto next) {
           std::cerr << req.method() << ' ' << req.url() << '\n';
           next(std::nullopt);
       });

       app.get("/", [](auto& req, auto& res) {
           res.send("hello from polycpp express");
       });

       app.get("/hello/:name", [](auto& req, auto& res) {
           res.json({{"msg", "hello " + req.params().at("name")}});
       });

       app.post("/echo", [](auto& req, auto& res) {
           res.json(req.body());            // parsed by json() middleware
       });

       // Error funnel (last) — all `next(HttpError)` ends up here.
       app.use([](const HttpError& err, auto& req, auto& res, auto next) {
           res.status(err.statusCode()).json({{"error", err.what()}});
       });

       auto& server = app.listen(0, []() { /* ready */ });
       std::cout << "listening\n";
       polycpp::EventLoop::run();
   }

Compile with the CMake wiring from :doc:`installation`:

.. code-block:: bash

   cmake -B build -G Ninja
   cmake --build build
   ./build/my_app

In a separate shell:

.. code-block:: bash

   $ curl -s http://127.0.0.1:<port>/hello/world
   {"msg":"hello world"}
   $ curl -s -H 'content-type: application/json' -d '{"ping":"pong"}' \
          http://127.0.0.1:<port>/echo
   {"ping":"pong"}

What just happened
------------------

1. ``express()`` returns a fresh
   :cpp:class:`polycpp::express::Application`. Under the hood it
   constructs a ``Router`` and prepares default settings
   (``etag=weak``, ``trust proxy=false``, …).

2. ``app.use(json())`` installs the JSON body parser as middleware.
   It runs on every request; when it sees ``content-type:
   application/json`` it fills :cpp:func:`polycpp::express::Request::body`
   with a :cpp:class:`polycpp::JsonValue`.

3. ``app.get("/hello/:name", ...)`` registers a route. Path patterns
   come from ``path-to-regexp`` — the same parser the npm package
   uses — so ``:name``, ``:name?``, ``:name+`` and their cousins all
   work.

4. The last ``app.use`` in the snippet is an **error handler**
   (four-argument ``(err, req, res, next)`` form). Middleware that
   calls ``next(HttpError{...})`` funnels here.

Next steps
----------

- :doc:`../tutorials/index` — step-by-step walkthroughs of common tasks.
- :doc:`../guides/index` — short how-tos for specific problems.
- :doc:`../api/index` — every public type, function, and option.
- :doc:`../examples/index` — runnable programs you can drop into a sandbox.
