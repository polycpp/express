Middleware order and the error funnel
=====================================

**You'll build:** an app that demonstrates the four handler shapes
(terminal, middleware, error-handler, param-handler), the order in
which they run, and the rule for turning a thrown exception into a
``500`` response.

**You'll use:**
:cpp:class:`polycpp::express::Router`,
:cpp:class:`polycpp::express::Application`,
:cpp:class:`polycpp::express::HttpError`,
:cpp:func:`polycpp::express::Application::param`.

The execution model
-------------------

Express arranges handlers on a :cpp:class:`polycpp::express::Layer`
chain. Each request walks the chain in declaration order. A handler
is one of four shapes:

=========================  =======================================================
Shape                      Runs when
=========================  =======================================================
``(req, res)``             method + path match (terminal route handler).
``(req, res, next)``       path matches (non-terminal middleware).
``(err, req, res, next)``  preceding handler called ``next(err)``.
``(req, res, next, value)``a path param with a registered ``param`` handler.
=========================  =======================================================

Call ``next(std::nullopt)`` to continue; call ``next(HttpError{...})``
to jump to the first error handler downstream.

Step 1 — a chatty middleware stack
----------------------------------

.. code-block:: cpp

   app.use([](auto& req, auto& res, auto next) {
       std::cerr << "[1] before " << req.url() << '\n';
       next(std::nullopt);
       std::cerr << "[1] after\n";
   });

   app.use([](auto& req, auto& res, auto next) {
       std::cerr << "[2] middle\n";
       next(std::nullopt);
   });

   app.get("/ok", [](auto& req, auto& res) {
       res.send("ok");
   });

Request to ``/ok`` prints:

.. code-block:: text

   [1] before /ok
   [2] middle
   [1] after

Middleware ``[1]`` runs *twice* — once before and once after the
chain, because synchronous handlers don't yield the loop. If your
handler is async, capture the response and continue in the callback.

Step 2 — funnel errors
----------------------

Error handlers have four parameters. They run only when an upstream
handler calls ``next(HttpError{...})``.

.. code-block:: cpp

   app.get("/boom", [](auto& req, auto& res) {
       throw HttpError(418, "i'm a teapot");
   });

   app.use([](const HttpError& err, auto& req, auto& res, auto next) {
       res.status(err.statusCode())
          .json({{"code", err.statusCode()}, {"msg", err.what()}});
   });

Throwing an :cpp:class:`polycpp::express::HttpError` is the
idiomatic way to bail out of a handler — the framework catches it,
promotes it through ``next(err)``, and reaches your error handler.
Any other exception becomes a ``500 Internal Server Error``.

Step 3 — param handlers
-----------------------

``app.param("id", cb)`` installs a handler that runs before any route
with a matching ``:id`` param.

.. code-block:: cpp

   app.param("id", [&](auto& req, auto& res, auto next, const std::string& id) {
       // Validate / look up once; store on the request.
       if (id.empty()) return next(HttpError(400, "missing id"));
       req.locals()["note"] = loadNote(id);      // custom state bag
       next(std::nullopt);
   });

   app.get("/notes/:id", [&](auto& req, auto& res) {
       res.json(req.locals()["note"]);
   });

The param handler is one layer of de-duplication — any number of
``:id`` routes share the same validation + lookup.

What you learned
----------------

- Four handler shapes; three continuation verbs: return, ``next()``,
  ``next(err)``.
- Order is declaration order. Put error handlers *last*.
- Throwing :cpp:class:`polycpp::express::HttpError` is the cleanest
  way to abort a handler with a specific status.
- ``app.param`` de-duplicates shared per-param work.
