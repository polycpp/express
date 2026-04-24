Custom 404 and 500 handlers
===========================

**When to reach for this:** the default HTML 404 page doesn't match
your API's response shape.

The middleware stack is walked in declaration order. Once no more
layers match:

- If no handler has been invoked at all, the framework sends a
  default 404.
- If a handler called ``next(HttpError{...})`` and no error handler
  picked it up, a default 500 is sent.

Replace both by installing them at the end of the stack:

.. code-block:: cpp

   // 404: install after every route / static mount.
   app.use([](auto& req, auto& res, auto next) {
       res.status(404).json({{"error", "not found"},
                             {"path",  req.path()}});
   });

   // 500 error funnel: install last.
   app.use([](const HttpError& err, auto& req, auto& res, auto next) {
       int code = err.statusCode();
       if (code < 400) code = 500;
       res.status(code).json({
           {"error",   err.what()},
           {"status",  code},
       });
   });

Keep the 404 above the error handler — it runs only if no earlier
layer matched; the error handler runs only if someone called
``next(err)``.

If you want stack traces in dev, check ``app.get("env")``
(``"development"`` by default) and include ``err.stack()`` or
``typeid(err).name()``. Never leak stack in production.
