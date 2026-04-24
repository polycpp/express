Serve static files
==================

Mount a directory as ``/public`` with sensible cache-control defaults
and an index-html fallback. Handy for front-ends built elsewhere
(Vite, esbuild, webpack) that you want to serve next to an API.

.. literalinclude:: ../../../examples/static_files.cpp
   :language: cpp
   :linenos:

Build and run:

.. code-block:: bash

   mkdir -p public && echo '<h1>hi</h1>' > public/index.html
   cmake -B build -G Ninja -DPOLYCPP_EXPRESS_BUILD_EXAMPLES=ON
   cmake --build build --target static_files
   ./build/examples/static_files
