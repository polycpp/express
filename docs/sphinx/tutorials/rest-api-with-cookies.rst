Build a REST API with routing, JSON, and cookies
================================================

**You'll build:** a toy "notes" REST API. GETs and POSTs for a list
resource, a path-param route for a single note, JSON body parsing,
and a cookie-backed session that increments a per-client counter.
Under 80 lines of C++.

**You'll use:**
:cpp:class:`polycpp::express::Application`,
:cpp:class:`polycpp::express::Request`,
:cpp:class:`polycpp::express::Response`,
:cpp:func:`polycpp::express::json`,
:cpp:func:`polycpp::express::Response::cookie`,
:cpp:func:`polycpp::express::Response::status`.

**Prerequisites:** the build wiring from
:doc:`../getting-started/installation`.

Step 1 — the app skeleton
-------------------------

Start with an ``Application`` and the JSON body parser. Every route
handler that reads the request body expects ``json()`` upstream.

.. code-block:: cpp

   #include <polycpp/express/express.hpp>
   #include <mutex>
   #include <vector>
   #include <string>

   using namespace polycpp::express;

   struct Note { int id; std::string text; };

   int main() {
       auto app = express();
       app.use(json());

       std::mutex mu;
       std::vector<Note> notes;
       int nextId = 1;

       // ...routes go here...

       app.listen(3000, []() { /* ready */ });
       polycpp::EventLoop::run();
   }

Step 2 — list + create
----------------------

``Router::get`` / ``Router::post`` take either a terminal
``(req, res)`` handler or a middleware-style ``(req, res, next)`` one.
For read/write endpoints the terminal form is cleanest.

.. code-block:: cpp

   app.get("/notes", [&](auto& req, auto& res) {
       std::lock_guard lg(mu);
       JsonArray out;
       for (auto& n : notes) out.push_back({{"id", n.id}, {"text", n.text}});
       res.json(out);
   });

   app.post("/notes", [&](auto& req, auto& res) {
       auto& body = req.body();
       if (!body.isObject() || !body["text"].isString()) {
           res.status(400).json({{"error", "expected { text: string }"}});
           return;
       }
       std::lock_guard lg(mu);
       notes.push_back({nextId++, body["text"].asString()});
       res.status(201).json({{"id", notes.back().id}});
   });

Step 3 — a parameterised route
------------------------------

``:id`` is a path parameter. Its parsed value is in
:cpp:member:`polycpp::express::Request::params`.

.. code-block:: cpp

   app.get("/notes/:id", [&](auto& req, auto& res) {
       int id = std::stoi(req.params().at("id"));
       std::lock_guard lg(mu);
       for (auto& n : notes) {
           if (n.id == id) { res.json({{"id", n.id}, {"text", n.text}}); return; }
       }
       res.status(404).json({{"error", "not found"}});
   });

Step 4 — a cookie-backed hit counter
------------------------------------

Bump a ``hits`` cookie on every request. Pass attributes via
:cpp:struct:`polycpp::express::CookieOptions` — ``httpOnly``,
``sameSite``, ``maxAge``, etc. Under the hood this delegates to
``polycpp::cookie::serialize``.

.. code-block:: cpp

   app.use([](auto& req, auto& res, auto next) {
       int n = 0;
       if (auto c = req.cookies().find("hits"); c != req.cookies().end()) {
           try { n = std::stoi(c->second); } catch (...) {}
       }
       CookieOptions co;
       co.httpOnly = true;
       co.maxAge   = std::chrono::seconds(60 * 60 * 24 * 30);
       res.cookie("hits", std::to_string(n + 1), co);
       next(std::nullopt);
   });

Attach the middleware *before* your route handlers — middleware order
matters, and ``next(std::nullopt)`` is how you say "continue".

What you learned
----------------

- ``app.get`` / ``app.post`` wire a handler to a method + path. Path
  params are available via :cpp:member:`polycpp::express::Request::params`.
- The body parser is opt-in via
  ``app.use(json())``. Without it,
  :cpp:member:`polycpp::express::Request::body` stays empty.
- Cookies round-trip through
  :cpp:member:`polycpp::express::Request::cookies` (read) and
  :cpp:func:`polycpp::express::Response::cookie` (write). Both are
  thin wrappers over the ``polycpp/cookie`` companion.
