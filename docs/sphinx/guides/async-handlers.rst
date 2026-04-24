Write an async handler
======================

**When to reach for this:** your handler has to wait on the event
loop — a DB query, an outbound HTTP call, a file read. You must keep
a reference to the response until you finish writing it.

Capture ``res`` by reference (or its backing shared ptr) in the
callback; return from the handler synchronously, and reply later:

.. code-block:: cpp

   app.get("/users/:id", [&](auto& req, auto& res) {
       auto id = req.params().at("id");
       db.findUser(id, [&res](auto err, auto user) {
           if (err)      { res.status(500).json({{"error", err->what()}}); return; }
           if (!user)    { res.status(404).end(); return; }
           res.json(*user);
       });
   });

If the async op might throw, wrap it in a ``try/catch`` and call
``next(HttpError{...})`` — the framework only catches what bubbles up
synchronously.

.. code-block:: cpp

   app.get("/risky", [&](auto& req, auto& res, auto next) {
       doAsync([next](auto err, auto result) {
           if (err) { next(HttpError(502, err->what())); return; }
           // res captured earlier; respond here.
       });
   });

.. note::

   ``Request`` and ``Response`` reference lifetimes mirror the
   underlying ``http::IncomingMessage`` / ``http::ServerResponse``.
   Keeping a reference across a callback boundary is safe as long as
   you ``end()`` the response before letting it go — the framework
   will 500 if an abandoned request ever times out.
