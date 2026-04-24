Add CORS and compression
========================

**When to reach for this:** you're serving a browser client from a
different origin, or you want gzip/brotli on text responses.

Both are factory functions that return
:cpp:type:`polycpp::express::MiddlewareHandler` — hand them to
``app.use`` at the top of the stack.

CORS
----

Defaults permit any origin + the common methods (GET/HEAD/PUT/POST/
DELETE/PATCH) — fine for dev, too permissive for prod. Lock it down:

.. code-block:: cpp

   CorsOptions opts;
   opts.origin      = "https://app.example.com";
   opts.methods     = {"GET", "POST"};
   opts.credentials = true;
   app.use(cors(opts));

The middleware handles the ``OPTIONS`` preflight itself; you don't
need a separate route.

Compression
-----------

The compression middleware negotiates based on ``Accept-Encoding`` and
encodes text-like responses above ``threshold`` bytes.

.. code-block:: cpp

   CompressionOptions opts;
   opts.threshold = 1024;          // skip payloads < 1 KiB
   opts.level     = 6;             // gzip/deflate level
   app.use(compress(opts));

Both zlib (gzip/deflate) and brotli back-ends are wired through the
polycpp ``zlib`` module. If your application emits binary
(pre-compressed) payloads exclusively, you can skip this middleware
entirely.

Install ``cors`` *before* ``compress`` — the CORS headers are
response-time state and must be set before the body is serialised.
