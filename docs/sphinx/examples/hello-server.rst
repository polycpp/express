Hello server
============

The smallest useful express program — one route, one handler, one
listen. Run it and ``curl http://localhost:3000/`` to see the response.

.. literalinclude:: ../../../examples/hello_server.cpp
   :language: cpp
   :linenos:

Build and run:

.. code-block:: bash

   cmake -B build -G Ninja -DPOLYCPP_EXPRESS_BUILD_EXAMPLES=ON
   cmake --build build --target hello_server
   ./build/examples/hello_server
