Trust a front-proxy's headers
=============================

**When to reach for this:** your app is behind a reverse proxy (nginx,
HAProxy, a cloud load-balancer) and you want
:cpp:func:`polycpp::express::Request::ip`,
:cpp:func:`polycpp::express::Request::protocol`, and
:cpp:func:`polycpp::express::Request::hostname` to reflect the real
client.

``app.set("trust proxy", value)`` toggles proxy-header parsing. The
value is one of:

* ``false`` (default) — ignore ``X-Forwarded-*``.
* ``true`` — trust every hop.
* An IPv4/CIDR string or a list — only trust these front-proxies.
* A number — trust this many hops back from the socket.

.. code-block:: cpp

   app.set("trust proxy", "10.0.0.0/8, 192.168.0.0/16");
   app.set("trust proxy", 1);        // single trusted hop

With the setting on, ``req.ip()`` reads the left-most untrusted address
from ``X-Forwarded-For``, ``req.protocol()`` reads
``X-Forwarded-Proto``, and ``req.hostname()`` reads
``X-Forwarded-Host``. The CIDR matching is provided by the
``polycpp/ipaddr`` companion.

.. warning::

   Only turn this on when you control the front-proxy. Trusting
   ``X-Forwarded-For`` from arbitrary peers lets a client spoof its IP.
