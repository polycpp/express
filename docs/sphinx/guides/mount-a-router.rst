Mount a Router
==============

**When to reach for this:** you want to split a growing ``app.*``
setup into named modules that can be mounted at different paths.

Create a :cpp:class:`polycpp::express::Router`, register routes on
it, then ``app.use(path, router)``.

.. code-block:: cpp

   Router api;
   api.get("/users",         listUsers);
   api.get("/users/:id",     getUser);
   api.post("/users",        createUser);

   app.use("/v1", api);
   app.use("/v2", anotherApiRouter);

Requests to ``/v1/users`` reach ``listUsers``. Routers support the
full set of HTTP verbs + ``all`` and ``use``, the same as
:cpp:class:`polycpp::express::Application`.

Sub-apps work too:

.. code-block:: cpp

   auto admin = express();
   admin.get("/dashboard", showDashboard);
   app.use("/admin", admin);     // sub-apps are mounted as middleware

Mount paths are stripped before handlers run — ``req.path()`` in
``showDashboard`` is ``/dashboard``, not ``/admin/dashboard``.
``req.baseUrl()`` has the prefix if you need it.
