Serve static files with Cache-Control
=====================================

**You'll build:** an app that serves a directory of static assets
with correct ``Content-Type`` (from :doc:`polycpp/mime
<../api/middleware>`), ``ETag`` + ``Last-Modified`` revalidation, and
long-lived ``Cache-Control`` for fingerprinted bundles.

**You'll use:**
:cpp:func:`polycpp::express::static_`,
:cpp:struct:`polycpp::express::StaticOptions`.

Step 1 — a basic static mount
-----------------------------

The one-liner: serve the ``public/`` directory at the root.

.. code-block:: cpp

   app.use(static_("public"));

Defaults: index files (``index.html``) served on directory requests,
``Content-Type`` inferred from extension, weak ETag enabled.

Step 2 — long-lived caching for hashed files
--------------------------------------------

When your build tool emits fingerprinted names
(``app.4f3c9a.js``), point those at a nested mount with a different
``Cache-Control``:

.. code-block:: cpp

   StaticOptions hashedOpts;
   hashedOpts.maxAge   = std::chrono::hours(24 * 365);   // 1 year
   hashedOpts.immutable = true;
   app.use("/assets", static_("public/assets", hashedOpts));

   // Serve everything else (index.html, robots.txt) with short TTLs.
   StaticOptions rootOpts;
   rootOpts.maxAge = std::chrono::minutes(5);
   app.use(static_("public", rootOpts));

Mount order matters — express tries ``/assets`` first because it
was registered first.

Step 3 — custom headers per file
--------------------------------

:cpp:struct:`polycpp::express::StaticOptions::setHeaders` runs for
every file response. Use it to add ``Strict-Transport-Security``,
``Content-Security-Policy``, or project-specific markers:

.. code-block:: cpp

   StaticOptions opts;
   opts.setHeaders = [](auto& res, const std::string& path, auto& /*stat*/) {
       if (path.ends_with(".js")) {
           res.setHeader("X-Asset-Kind", "javascript");
       }
       res.setHeader("Strict-Transport-Security",
                     "max-age=31536000; includeSubDomains");
   };
   app.use(static_("public", opts));

Step 4 — fall through to an SPA
-------------------------------

For single-page apps, declare all static + API routes first, then a
terminal catch-all that returns ``index.html``:

.. code-block:: cpp

   app.use("/api", apiRouter);
   app.use(static_("public"));

   app.get("/*", [](auto& req, auto& res) {
       res.sendFile("public/index.html");
   });

The catch-all is a single terminal handler — it only runs because
nothing above matched.

What you learned
----------------

- ``static_(root, opts)`` is the middleware factory. Mount at a path
  or at ``/`` for a full-tree serve.
- Use two mounts with different ``Cache-Control`` to separate
  hashed-bundle traffic from short-lived root files.
- ``setHeaders`` is the right place for response-shaping headers; the
  static middleware calls it after it has filled the common ones.
