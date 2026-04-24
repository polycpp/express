Installation
============

express targets C++20 and builds with clang ≥ 16 or gcc ≥ 13. It
pulls in:

- the base `polycpp <https://github.com/enricohuang/polycpp>`_ library
  (``http::Server``, ``stream``, ``events``, ``JsonValue``, …), and
- six companion libraries — ``path-to-regexp``, ``qs``, ``cookie``,
  ``mime``, ``negotiate``, ``ipaddr`` — all fetched transitively.

You don't need to declare the companions in your own
``CMakeLists.txt`` unless you use them directly from your application
code; express's CMake glue deduplicates them under their ``polycpp_*``
fetch names.

CMake FetchContent (recommended)
--------------------------------

Add the library to your ``CMakeLists.txt``:

.. code-block:: cmake

   include(FetchContent)

   FetchContent_Declare(
       polycpp_express
       GIT_REPOSITORY https://github.com/polycpp/express.git
       GIT_TAG        master
   )
   FetchContent_MakeAvailable(polycpp_express)

   add_executable(my_app main.cpp)
   target_link_libraries(my_app PRIVATE polycpp::express)

The first configure pulls all seven git repos; subsequent builds reuse
them. Pin ``GIT_TAG`` on each ``FetchContent_Declare`` for
reproducible builds.

Using local clones
------------------

If you have express + polycpp + the six companion libs checked out
side by side, tell CMake to use them:

.. code-block:: bash

   cmake -B build -G Ninja \
       -DFETCHCONTENT_SOURCE_DIR_POLYCPP=/path/to/polycpp \
       -DFETCHCONTENT_SOURCE_DIR_POLYCPP_PATH_TO_REGEXP=/path/to/path-to-regexp \
       -DFETCHCONTENT_SOURCE_DIR_POLYCPP_QS=/path/to/qs \
       -DFETCHCONTENT_SOURCE_DIR_POLYCPP_COOKIE=/path/to/cookie \
       -DFETCHCONTENT_SOURCE_DIR_POLYCPP_MIME=/path/to/mime \
       -DFETCHCONTENT_SOURCE_DIR_POLYCPP_NEGOTIATE=/path/to/negotiate \
       -DFETCHCONTENT_SOURCE_DIR_POLYCPP_IPADDR=/path/to/ipaddr \
       -DFETCHCONTENT_SOURCE_DIR_POLYCPP_EXPRESS=/path/to/express

This is the path CI uses for the test suite — see ``tests/`` in the
repo.

Build options
-------------

``POLYCPP_EXPRESS_BUILD_TESTS``
    Build the GoogleTest suite. Defaults to ``ON`` for standalone
    builds and ``OFF`` when consumed via FetchContent.

``POLYCPP_IO``
    ``asio`` (default) or ``libuv`` — inherited from polycpp.

``POLYCPP_SSL_BACKEND``
    ``boringssl`` (default) or ``openssl`` — inherited from polycpp.

Verifying the install
---------------------

.. code-block:: bash

   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
   cmake --build build
   ctest --test-dir build --output-on-failure

The suite exercises route matching (path-to-regexp parity), header
negotiation, body parsers, error funnelling, mount paths, and view
rendering. If a test fails, include the compiler version, the failing
test name, and ``ctest``'s log when filing an issue.
