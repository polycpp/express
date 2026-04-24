express
=======

**Node.js Express-style HTTP server framework**

A faithful C++20 port of Node.js Express. Application / Router / Route / Layer / Request / Response, middleware chains with error funnelling, built-in body parsers, CORS, compression, static file serving, and template-engine support — all on top of polycpp's HTTP server.

.. code-block:: cpp

   #include <polycpp/express/express.hpp>
   using namespace polycpp::express;

   int main() {
       auto app = express();

       app.use(json());
       app.get("/hello/:name", [](auto& req, auto& res) {
           res.json({{"msg", "hello " + req.params().at("name")}});
       });

       app.listen(0, []() { /* ready */ });
       polycpp::EventLoop::run();
   }

.. grid:: 2

   .. grid-item-card:: Drop-in familiarity
      :margin: 1

      Mirrors expressjs — ``app.get``, ``app.use``, ``res.json``,
      ``req.params``, ``next(err)`` — with polycpp's ``http::Server``
      under the hood and the six companion libs (path-to-regexp, qs,
      cookie, mime, negotiate, ipaddr) supplying the npm-equivalent
      behaviour.

   .. grid-item-card:: C++20 native
      :margin: 1

      Header-only where possible, zero-overhead abstractions, ``constexpr``
      and ``std::string_view`` throughout.

   .. grid-item-card:: Tested
      :margin: 1

      Route matching (path-to-regexp parity), header negotiation, body parsers, error funnelling, mount paths, and view rendering are covered by the test suite. A handful of middleware modules are still stubs — noted inline on the relevant pages.

   .. grid-item-card:: Plays well with polycpp
      :margin: 1

      Uses the same JSON value, error, and typed-event types as the rest of
      the polycpp ecosystem — no impedance mismatch.

Getting started
---------------

.. code-block:: bash

   # With FetchContent (recommended)
   FetchContent_Declare(
       polycpp_express
       GIT_REPOSITORY https://github.com/polycpp/express.git
       GIT_TAG        master
   )
   FetchContent_MakeAvailable(polycpp_express)
   target_link_libraries(my_app PRIVATE polycpp::express)

:doc:`Installation <getting-started/installation>` · :doc:`Quickstart <getting-started/quickstart>` · :doc:`Tutorials <tutorials/index>` · :doc:`API reference <api/index>`

.. toctree::
   :hidden:
   :caption: Getting started

   getting-started/installation
   getting-started/quickstart

.. toctree::
   :hidden:
   :caption: Tutorials

   tutorials/index

.. toctree::
   :hidden:
   :caption: How-to guides

   guides/index

.. toctree::
   :hidden:
   :caption: API reference

   api/index

.. toctree::
   :hidden:
   :caption: Examples

   examples/index
