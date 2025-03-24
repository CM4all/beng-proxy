beng-proxy
==========

``beng-proxy`` is a web server designed for shared web hosting.  This
repository also contains ``beng-lb``, a load balancer.

For more information, `read the manual
<https://beng-proxy.readthedocs.io/en/latest/>`__ in the `doc`
directory.


Building beng-proxy
-------------------

You need:

- Linux kernel 5.12 or later
- a C++23 compliant compiler (e.g. GCC 12 or clang 14)
- `libfmt <https://fmt.dev/>`__
- `libpq <https://www.postgresql.org/>`__
- `CURL <https://curl.haxx.se/>`__
- `D-Bus <https://www.freedesktop.org/wiki/Software/dbus/>`__
- `systemd <https://www.freedesktop.org/wiki/Software/systemd/>`__
- `OpenSSL <https://www.openssl.org/>`__
- `libsodium <https://www.libsodium.org/>`__
- `Meson 1.2 <http://mesonbuild.com/>`__ and `Ninja <https://ninja-build.org/>`__

Optional dependencies:

- `nghttp2 <https://nghttp2.org/>`__
- `libcap2 <https://sites.google.com/site/fullycapable/>`__ for
  dropping unnecessary Linux capabilities
- `libseccomp <https://github.com/seccomp/libseccomp>`__ for system
  call filter support
- `liburing <https://github.com/axboe/liburing>`__
- `libwas <https://github.com/CM4all/libwas>`__
- `LuaJIT <http://luajit.org/>`__
- `Avahi <https://www.avahi.org/>`__

Get the source code::

 git clone --recursive https://github.com/CM4all/beng-proxy.git

Run ``meson``::

 meson setup output

Compile and install::

 ninja -C output
 ninja -C output install


Building the Debian package
---------------------------

After installing the build dependencies (``dpkg-checkbuilddeps``),
run::

 dpkg-buildpackage -rfakeroot -b -uc -us
