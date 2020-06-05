.. _certdb:

Certificate Database
####################

The certificate database is useful for bulk hosting. The database may
contain a dynamic list of X.509 server certificates.

See :ref:`certdbconfig` for instructions on how to configure the
database in :program:`beng-lb`.

Managing the Certificate Database
=================================

Creating the Database
---------------------

Create a PostgreSQL database and run the file ``certdb.sql`` to create
the table.

Migrating the Database
----------------------

To update the database schema after an update, type::

   cm4all-certdb migrate

After the schema update, all users of the database should be updated
quickly to the same version. While it is attempted to keep read-only
backwards compatibility as much as possible, applications with write
access may cease to work after the migration.

Managing Certificates
---------------------

The program ``cm4all-certdb`` is a frontend for the database. It loads
the PostgreSQL connect string and the ``wrap_key`` settings from the
:program:`beng-lb` configuration file
(i.e. :file:`/etc/cm4all/beng/lb.conf`). The connect string can be
overridden from the one-line text file
:file:`/etc/cm4all/beng/certdb.connect`, just in case this
command-line tool needs a different setting.

Load a new certificate into the database::

   cm4all-certdb load cert.pem key.pem

Find a certificate for the given name::

   cm4all-certdb find www.example.com

Monitor database changes::

   cm4all-certdb monitor

Let’s Encrypt
=============

The ``cm4all-certdb`` program includes an ACME (Automatic Certificate
Management Environment) client, the protocol implemented by the *Let’s
Encrypt* project.

To get started, register an account::

   openssl genrsa -out /etc/cm4all/acme/account.key  4096
   cm4all-certdb acme --staging new-account foo@example.com

This prints the account "location", which must be specified with the
``--account-key-id`` option in every successive call.  To look up the
location of an existing account, type::

  cm4all-certdb acme --staging get-account

Note: examples listed here will use the “staging” server. Omit the
``–-staging`` option to use the Let’s Encrypt production server.

To obtain a signed certificate, type::

   cm4all-certdb acme --staging \
     --challenge-directory /var/www/acme-challenge \
     --account-key-id https://staging.api.letsencrypt.org/acme/acct/42 \
     new-order example www.example.com example.com foo.example.com

To update all names in an existing certificate, use the command
``renew-cert`` and specify only the handle (``example`` here)::

   cm4all-certdb acme --staging \
     --challenge-directory /var/www/acme-challenge \
     --account-key-id https://staging.api.letsencrypt.org/acme/acct/42 \
     renew-cert example

This requires that the URL
``http://example.com/.well-known/acme-challenge/`` maps to the
specified ``--challenge-directory`` path (on all domains).

After the program finishes, the new certificate should be usable
immediately.

Wildcards
---------

To obtain a certificate for a wildcard, the ACME client needs to use
DNS-based authorization (``dns-01``) instead of HTTP-based
(``http-01``).  Use the command-line option ``--dns-txt-program`` to
specify a program which updates the ``TXT`` record of an ACME
challenge host::

   cm4all-certdb acme --staging \
     --dns-txt-program /usr/lib/cm4all/bin/set-acme-challenge-dns-txt \
     --account-key-id https://staging.api.letsencrypt.org/acme/acct/42 \
     new-order example *.example.com

This program is invoked twice: once to set a ``TXT`` record and again
to delete the ``TXT`` record after finishing authorization.  It
accepts the following parameters:

1. the full-qualified DNS host name (the program shall prepend the
   prefix ``_acme-challenge.``)
2. ``TXT`` record values

All ``TXT`` records but the given ones are removed.  If given just the
DNS host name and no ``TXT`` record value, then all existing ``TXT``
records are deleted.
