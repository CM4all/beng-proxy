beng-proxy
==========

``beng-proxy`` is a web server designed for shared web hosting.  This
repository also contains ``beng-lb``, a load balancer.

For more information, read the manual in the `doc` directory.


Building beng-proxy
-------------------

You need:

- Linux kernel 5.4 or later
- a C++20 compliant compiler (e.g. GCC 10 or clang 10)
- `libcap2 <https://sites.google.com/site/fullycapable/>`__
- `libpq <https://www.postgresql.org/>`__
- `Boost <http://www.boost.org/>`__
- `CURL <https://curl.haxx.se/>`__
- `D-Bus <https://www.freedesktop.org/wiki/Software/dbus/>`__
- `systemd <https://www.freedesktop.org/wiki/Software/systemd/>`__
- `OpenSSL <https://www.openssl.org/>`__
- `libsodium <https://www.libsodium.org/>`__
- `Meson 0.47 <http://mesonbuild.com/>`__ and `Ninja <https://ninja-build.org/>`__

Optional dependencies:
- `nghttp2 <https://nghttp2.org/>`__
- `libnfs <https://github.com/sahlberg/libnfs>`__
- `libwas <https://github.com/CM4all/libwas>`__
- `Avahi <https://www.avahi.org/>`__

Run ``meson``::

 meson . output

Compile and install::

 ninja -C output
 ninja -C output install


Building the Debian package
---------------------------

After installing the build dependencies (``dpkg-checkbuilddeps``),
run::

 dpkg-buildpackage -rfakeroot -b -uc -us
