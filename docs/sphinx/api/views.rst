Views
=====

Template resolution. The ``View`` class walks the registered
``views`` directories, picks a file matching the ``view engine``
extension, and delegates rendering to the engine callback.

.. doxygenclass:: polycpp::express::View
   :members:
