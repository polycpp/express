Router, Route, Layer
====================

:cpp:class:`polycpp::express::Router` owns the middleware stack.
Each HTTP-method binding becomes a
:cpp:class:`polycpp::express::Route`, which itself contains one or
more :cpp:class:`polycpp::express::Layer` objects (one per handler).

Router
------

.. doxygenclass:: polycpp::express::Router
   :members:

Route
-----

.. doxygenclass:: polycpp::express::Route
   :members:

Layer
-----

.. doxygenclass:: polycpp::express::Layer
   :members:
