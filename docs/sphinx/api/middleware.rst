Built-in middleware
===================

All middleware factories return a
:cpp:type:`polycpp::express::MiddlewareHandler`. Install with
``app.use(factory(options))``.

Body parsers
------------

.. doxygenfunction:: polycpp::express::jsonParser

.. doxygenfunction:: polycpp::express::urlencodedParser

.. doxygenfunction:: polycpp::express::rawParser

.. doxygenfunction:: polycpp::express::textParser

.. doxygenfunction:: polycpp::express::json

.. doxygenfunction:: polycpp::express::urlencoded

.. doxygenfunction:: polycpp::express::raw

.. doxygenfunction:: polycpp::express::text

CORS
----

.. doxygenfunction:: polycpp::express::corsMiddleware

.. doxygenfunction:: polycpp::express::cors

Compression
-----------

.. doxygenfunction:: polycpp::express::compressionMiddleware

.. doxygenfunction:: polycpp::express::compress

Static files
------------

.. doxygenfunction:: polycpp::express::serveStatic

.. doxygenfunction:: polycpp::express::static_

.. note::

   ``multipart/form-data`` parsing is intentionally *not* in the
   built-in set — bring a third-party multipart middleware if you
   need it.
