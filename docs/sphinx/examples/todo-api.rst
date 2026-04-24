Todo REST API
=============

A compact but realistic REST surface with:

- JSON body parsing (``express::json()`` middleware)
- Path parameters (``/todos/:id``)
- Router mounting (``/api`` prefix)
- Per-route error handling
- 404 fallthrough

.. literalinclude:: ../../../examples/todo_api.cpp
   :language: cpp
   :linenos:

Build and run:

.. code-block:: bash

   cmake -B build -G Ninja -DPOLYCPP_EXPRESS_BUILD_EXAMPLES=ON
   cmake --build build --target todo_api
   ./build/examples/todo_api &
   curl -X POST -H 'content-type: application/json' \
        -d '{"title":"buy milk"}' http://localhost:3000/api/todos
   curl http://localhost:3000/api/todos
