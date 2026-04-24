Register a template engine
==========================

**When to reach for this:** you want server-side-rendered HTML with
locals merged in.

Register a callable for each file extension with
:cpp:func:`polycpp::express::Application::engine`, then set
``view engine`` and ``views``:

.. code-block:: cpp

   app.engine("hbs", [](auto path, auto opts, auto cb) {
       // read file, render with your handlebars-like engine, call cb
       auto tpl = readFile(path);
       auto html = renderHandlebars(tpl, opts);
       cb(std::nullopt, std::move(html));
   });

   app.set("view engine", "hbs");
   app.set("views",       "./views");

   app.get("/", [](auto& req, auto& res) {
       res.render("index", {{"title", "hello"}});
   });

:cpp:func:`polycpp::express::Response::render` merges locals in
priority order (``app.locals`` → ``res.locals`` → the options object
passed to ``render``). Enable ``app.set("view cache", true)`` to
memoise the resolved view path across requests.

The library ships no engine out of the box — bring your own
(Handlebars-like, Mustache, ``fmt::format``, etc.). The
:cpp:class:`polycpp::express::View` class is what handles path
resolution and extension matching.
