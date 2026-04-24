Errors
======

:cpp:class:`polycpp::express::HttpError` is the canonical error type.
Throw it from a handler, pass it to ``next()``, or construct one from
a status + message.

.. doxygenclass:: polycpp::express::HttpError
   :members:
