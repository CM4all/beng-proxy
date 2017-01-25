/*
 * Definitions for the beng-proxy translation protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_PROXY_TRANSLATION_H
#define __BENG_PROXY_TRANSLATION_H

#include <stdint.h>

enum beng_translation_command {
    /**
     * Beginning of a request/response.  The optional payload is a
     * uint8_t specifying the protocol version.
     */
    TRANSLATE_BEGIN = 1,

    TRANSLATE_END = 2,
    TRANSLATE_HOST = 3,
    TRANSLATE_URI = 4,
    TRANSLATE_STATUS = 5,
    TRANSLATE_PATH = 6,
    TRANSLATE_CONTENT_TYPE = 7,
    TRANSLATE_HTTP = 8,
    TRANSLATE_REDIRECT = 9,
    TRANSLATE_FILTER = 10,
    TRANSLATE_PROCESS = 11,
    TRANSLATE_SESSION = 12,
    TRANSLATE_PARAM = 13,
    TRANSLATE_USER = 14,
    TRANSLATE_LANGUAGE = 15,
    TRANSLATE_REMOTE_HOST = 16,
    TRANSLATE_PATH_INFO = 17,
    TRANSLATE_SITE = 18,
    TRANSLATE_CGI = 19,
    TRANSLATE_DOCUMENT_ROOT = 20,
    TRANSLATE_WIDGET_TYPE = 21,
    TRANSLATE_CONTAINER = 22,
    TRANSLATE_ADDRESS = 23,
    TRANSLATE_ADDRESS_STRING = 24,
    TRANSLATE_JAILCGI = 26,
    TRANSLATE_INTERPRETER = 27,
    TRANSLATE_ACTION = 28,
    TRANSLATE_SCRIPT_NAME = 29,
    TRANSLATE_AJP = 30,

    /** deprecated */
    TRANSLATE_DOMAIN = 31,

    TRANSLATE_STATEFUL = 32,
    TRANSLATE_FASTCGI = 33,
    TRANSLATE_VIEW = 34,
    TRANSLATE_USER_AGENT = 35,
    TRANSLATE_MAX_AGE = 36,
    TRANSLATE_VARY = 37,
    TRANSLATE_QUERY_STRING = 38,
    TRANSLATE_PIPE = 39,
    TRANSLATE_BASE = 40,
    TRANSLATE_DELEGATE = 41,
    TRANSLATE_INVALIDATE = 42,
    TRANSLATE_LOCAL_ADDRESS = 43,
    TRANSLATE_LOCAL_ADDRESS_STRING = 44,
    TRANSLATE_APPEND = 45,
    TRANSLATE_DISCARD_SESSION = 46,
    TRANSLATE_SCHEME = 47,
    TRANSLATE_REQUEST_HEADER_FORWARD = 48,
    TRANSLATE_RESPONSE_HEADER_FORWARD = 49,
    TRANSLATE_DEFLATED = 50,
    TRANSLATE_GZIPPED = 51,
    TRANSLATE_PAIR = 52,
    TRANSLATE_UNTRUSTED = 53,
    TRANSLATE_BOUNCE = 54,
    TRANSLATE_ARGS = 55,

    /**
     * The value of the "WWW-Authenticate" HTTP response header.
     */
    TRANSLATE_WWW_AUTHENTICATE = 56,

    /**
     * The value of the "Authentication-Info" HTTP response header.
     */
    TRANSLATE_AUTHENTICATION_INFO = 57,

    /**
     * The value of the "Authorization" HTTP request header.
     */
    TRANSLATE_AUTHORIZATION = 58,

    /**
     * A custom HTTP response header sent to the client.
     */
    TRANSLATE_HEADER = 59,

    TRANSLATE_UNTRUSTED_PREFIX = 60,

    /**
     * Set the "secure" flag on the session cookie.
     */
    TRANSLATE_SECURE_COOKIE = 61,

    /**
     * Enable filtering of client errors (status 4xx).  Without this
     * flag, only successful responses (2xx) are filtered.  Only
     * useful when at least one FILTER was specified.
     */
    TRANSLATE_FILTER_4XX = 62,

    /**
     * Support for custom error documents.  In the response, this is a
     * flag which enables custom error documents (i.e. if the HTTP
     * response is not successful, the translation server is asked to
     * provide a custom error document).  In a request, it queries the
     * location of the error document.
     */
    TRANSLATE_ERROR_DOCUMENT = 63,

    /**
     * Response: causes beng-proxy to submit the same translation
     * request again, with this packet appended.  The current response
     * is remembered, to be used when the second response contains the
     * PREVIOUS packet.
     *
     * Request: repeated request after CHECK was received.  The server
     * may respond with PREVIOUS.
     */
    TRANSLATE_CHECK = 64,

    /**
     * Tells beng-proxy to use the resource address of the previous
     * translation response.
     */
    TRANSLATE_PREVIOUS = 65,

    /**
     * Launch a WAS application to handle the request.
     */
    TRANSLATE_WAS = 66,

    /**
     * The absolute location of the home directory of the site owner
     * (hosting account).
     */
    TRANSLATE_HOME = 67,

    /**
     * Specifies the session realm.  An existing session matches only
     * if its realm matches the current request's realm.
     */
    TRANSLATE_REALM = 68,

    TRANSLATE_UNTRUSTED_SITE_SUFFIX = 69,

    /**
     * Transparent proxy: forward URI arguments to the request handler
     * instead of using them.
     */
    TRANSLATE_TRANSPARENT = 70,

    /**
     * Make the resource address "sticky", i.e. attempt to forward all
     * requests of a session to the same worker.
     */
    TRANSLATE_STICKY = 71,

    /**
     * Enable header dumps for the widget: on a HTTP request, the
     * request and response headers will be logged.  Only for
     * debugging purposes.
     */
    TRANSLATE_DUMP_HEADERS = 72,

    /**
     * Override the cookie host name.  This host name is used for
     * storing and looking up cookies in the jar.  It is especially
     * useful for protocols that don't have a host name, such as CGI.
     */
    TRANSLATE_COOKIE_HOST = 73,

    /**
     * Run the CSS processor.
     */
    TRANSLATE_PROCESS_CSS = 74,

    /**
     * Rewrite CSS class names with a leading underscore?
     */
    TRANSLATE_PREFIX_CSS_CLASS = 75,

    /**
     * Default URI rewrite mode is base=widget mode=focus.
     */
    TRANSLATE_FOCUS_WIDGET = 76,

    /**
     * Absolute URI paths are considered relative to the base URI of
     * the widget.
     */
    TRANSLATE_ANCHOR_ABSOLUTE = 77,

    /**
     * Rewrite XML ids with a leading underscore?
     */
    TRANSLATE_PREFIX_XML_ID = 78,

    /**
     * Reuse a cached response only if the request \verb|URI| matches
     * the specified regular expression (Perl compatible).
     */
    TRANSLATE_REGEX = 79,

    /**
     * Don't apply the cached response if the request URI matches the
     * specified regular expression (Perl compatible).
     */
    TRANSLATE_INVERSE_REGEX = 80,

    /**
     * Run the text processor to expand entity references.
     */
    TRANSLATE_PROCESS_TEXT = 81,

    /**
     * Send widget metadata (id, prefix, type) to the widget server.
     */
    TRANSLATE_WIDGET_INFO = 82,

    /**
     * Expand #TRANSLATE_REGEX match strings in this PATH_INFO value.
     * Sub-strings in the form "\1" will be replaced.  It can be used
     * to copy URI parts to a filter.
     */
    TRANSLATE_EXPAND_PATH_INFO = 83,

    /**
     * Expand #TRANSLATE_REGEX match strings in this PATH value (only
     * CGI, FastCGI, WAS).  Sub-strings in the form "\1" will be
     * replaced.
     */
    TRANSLATE_EXPAND_PATH = 84,

    /**
     * Set the session cookie's "Domain" attribute.
     */
    TRANSLATE_COOKIE_DOMAIN = 85,

    /**
     * The URI of the "local" location of a widget class.  This may
     * refer to a location that serves static resources.  It is used
     * by the processor for rewriting URIs.
     */
    TRANSLATE_LOCAL_URI = 86,

    /**
     * Enable CGI auto-base.
     */
    TRANSLATE_AUTO_BASE = 87,

    /**
     * A classification for the User-Agent request header.
     */
    TRANSLATE_UA_CLASS = 88,

    /**
     * Shall the XML/HTML processor invoke the CSS processor for
     * "style" element contents?
     */
    TRANSLATE_PROCESS_STYLE = 89,

    /**
     * Does this widget support new-style direct URI addressing?
     *
     * Example: http://localhost/template.html;frame=foo/bar - this
     * requests the widget "foo" and with path-info "/bar".
     */
    TRANSLATE_DIRECT_ADDRESSING = 90,

    /**
     * Allow this widget to embed more instances of its own class.
     */
    TRANSLATE_SELF_CONTAINER = 91,

    /**
     * Allow this widget to embed instances of this group.  This can
     * be specified multiple times to allow more than one group.  It
     * can be combined with #TRANSLATE_SELF_CONTAINER.
     */
    TRANSLATE_GROUP_CONTAINER = 92,

    /**
     * Assign a group name to the widget type.  This is used by
     * #TRANSLATE_GROUP_CONTAINER.
     */
    TRANSLATE_WIDGET_GROUP = 93,

    /**
     * A cached response is valid only if the file specified in this
     * packet is not modified.
     *
     * The first 8 bytes is the mtime (seconds since UNIX epoch), the
     * rest is the absolute path to a regular file (symlinks not
     * supported).  The translation fails when the file does not exist
     * or is inaccessible.
     */
    TRANSLATE_VALIDATE_MTIME = 94,

    /**
     * Mount a NFS share.  This packet specifies the server (IP
     * address).
     */
    TRANSLATE_NFS_SERVER = 95,

    /**
     * Mount a NFS share.  This packet specifies the export path to be
     * mounted from the server.
     */
    TRANSLATE_NFS_EXPORT = 96,

    /**
     * The path of a HTTP server program that will be launched.
     */
    TRANSLATE_LHTTP_PATH = 97,

    /**
     * The URI that will be requested on the given HTTP server
     * program.
     */
    TRANSLATE_LHTTP_URI = 98,

    /**
     * Expand #TRANSLATE_REGEX match strings in this
     * #TRANSLATE_LHTTP_URI value.  Sub-strings in the form "\1" will
     * be replaced.
     */
    TRANSLATE_EXPAND_LHTTP_URI = 99,

    /**
     * The "Host" request header for the #TRANSLATE_LHTTP_PATH.
     */
    TRANSLATE_LHTTP_HOST = 100,

    /**
     * How many concurrent requests will be handled by the
     * aforementioned process?
     */
    TRANSLATE_CONCURRENCY = 101,

    /**
     * The translation server sends this packet when it wants to have
     * the full request URI.  beng-proxy then sends another
     * translation request, echoing this packet (including its
     * payload), and #TRANSLATE_URI containing the full request URI
     * (not including the query string)
     */
    TRANSLATE_WANT_FULL_URI = 102,

    /**
     * Start the child process in a new user namespace?
     */
    TRANSLATE_USER_NAMESPACE = 103,

    /**
     * Start the child process in a new network namespace?
     */
    TRANSLATE_NETWORK_NAMESPACE = 104,

    /**
     * Add expansion for the preceding #TRANSLATE_APPEND.
     */
    TRANSLATE_EXPAND_APPEND = 105,

    /**
     * Add expansion for the preceding #TRANSLATE_PAIR.
     */
    TRANSLATE_EXPAND_PAIR = 106,

    /**
     * Start the child process in a new PID namespace?
     */
    TRANSLATE_PID_NAMESPACE = 107,

    /**
     * Starts the child process in a new mount namespace and invokes
     * pivot_root().  Payload is the new root directory, which must
     * contain a directory called "mnt".
     */
    TRANSLATE_PIVOT_ROOT = 108,

    /**
     * Mount the proc filesystem on /proc?
     */
    TRANSLATE_MOUNT_PROC = 109,

    /**
     * Mount the specified home directory?  Payload is the mount
     * point.
     */
    TRANSLATE_MOUNT_HOME = 110,

    /**
     * Mount a new tmpfs on /tmp?
     */
    TRANSLATE_MOUNT_TMP_TMPFS = 111,

    /**
     * Create a new UTS namespace?  Payload is the host name inside
     * the namespace.
     */
    TRANSLATE_UTS_NAMESPACE = 112,

    /**
     * Bind-mount a directory.  Payload is source and target separated
     * by a null byte.
     */
    TRANSLATE_BIND_MOUNT = 113,

    /**
     * Set resource limits via setrlimit().
     */
    TRANSLATE_RLIMITS = 114,

    /**
     * The translation server wishes to have the specified data:
     * payload is an array of uint16_t containing translation
     * commands.
     */
    TRANSLATE_WANT = 115,

    /**
     * Modifier for #TRANSLATE_BASE: do not perform any safety checks
     * on the tail string.
     */
    TRANSLATE_UNSAFE_BASE = 116,

    /**
     * Enables "easy" mode for #TRANSLATE_BASE or
     * #TRANSLATE_UNSAFE_BASE: the returned resource address refers to
     * the base, not to the actual request URI.
     */
    TRANSLATE_EASY_BASE = 117,

    /**
     * Apply #TRANSLATE_REGEX and #TRANSLATE_INVERSE_REGEX to the
     * remaining URI following #TRANSLATE_BASE instead of the whole
     * request URI?
     */
    TRANSLATE_REGEX_TAIL = 118,

    /**
     * Unescape the URI for #TRANSLATE_REGEX and
     * #TRANSLATE_INVERSE_REGEX?
     */
    TRANSLATE_REGEX_UNESCAPE = 119,

    /**
     * Retranslate if the file does not exist.
     */
    TRANSLATE_FILE_NOT_FOUND = 120,

    /**
     * Translation server indicates that Content-Type lookup should be
     * performed for static files.  Upon request, this packet is
     * echoed to the translation server, accompanied by a
     * #TRANSLATE_SUFFIX packet.
     */
    TRANSLATE_CONTENT_TYPE_LOOKUP = 121,

    /**
     * Payload is the file name suffix without the dot.  Part of a
     * #TRANSLATE_CONTENT_TYPE_LOOKUP translation request.
     */
    TRANSLATE_SUFFIX = 122,

    /**
     * Retranslate if the file is a directory.
     */
    TRANSLATE_DIRECTORY_INDEX = 123,

    /**
     * Generate an "Expires" header for static files.  Payload is a 32
     * bit integer specifying the number of seconds from now on.
     */
    TRANSLATE_EXPIRES_RELATIVE = 124,

    TRANSLATE_EXPAND_REDIRECT = 125,

    TRANSLATE_EXPAND_SCRIPT_NAME = 126,

    /**
     * Override the path to be tested by #TRANSLATE_FILE_NOT_FOUND.
     */
    TRANSLATE_TEST_PATH = 127,

    /**
     * Expansion for #TRANSLATE_TEST_PATH.
     */
    TRANSLATE_EXPAND_TEST_PATH = 128,

    /**
     * Copy the query string to the redirect URI?
     */
    TRANSLATE_REDIRECT_QUERY_STRING = 129,

    /**
     * Negotiate how to handle requests to regular file with
     * path info.
     */
    TRANSLATE_ENOTDIR = 130,

    /**
     * An absolute path where STDERR output will be appended.
     */
    TRANSLATE_STDERR_PATH = 131,

    /**
     * Set the session cookie's "Path" attribute.
     */
    TRANSLATE_COOKIE_PATH = 132,

    /**
     * Advanced authentication protocol through the translation
     * server.
     */
    TRANSLATE_AUTH = 133,

    /**
     * Set an evironment variable.  Unlike #TRANSLATE_PAIR, this works
     * even for FastCGI and WAS.
     */
    TRANSLATE_SETENV = 134,

    /**
     * Expansion for #TRANSLATE_SETENV.
     */
    TRANSLATE_EXPAND_SETENV = 135,

    /**
     * Expansion for #TRANSLATE_URI.
     */
    TRANSLATE_EXPAND_URI = 136,

    /**
     * Expansion for #TRANSLATE_SITE.
     */
    TRANSLATE_EXPAND_SITE = 137,

    /**
     * Send an addtional request header to the backend server.
     */
    TRANSLATE_REQUEST_HEADER = 138,

    /**
     * Expansion for #TRANSLATE_REQUEST_HEADER.
     */
    TRANSLATE_EXPAND_REQUEST_HEADER = 139,

    /**
     * Build the "gzipped" path automatically by appending ".gz" to
     * the "regular" path.
     */
    TRANSLATE_AUTO_GZIPPED = 140,

    /**
     * Expansion for #TRANSLATE_DOCUMENT_ROOT.
     */
    TRANSLATE_EXPAND_DOCUMENT_ROOT = 141,

    /**
     * Check if the #TRANSLATE_TEST_PATH (or
     * #TRANSLATE_EXPAND_TEST_PATH) plus one of the suffixes from
     * #TRANSLATE_PROBE_SUFFIX exists (regular files only).
     * beng-proxy will send another translation request, echoing this
     * packet and echoing the #TRANSLATE_PROBE_SUFFIX that was found.
     *
     * This packet must be followed by at least two
     * #TRANSLATE_PROBE_SUFFIX packets.
     */
    TRANSLATE_PROBE_PATH_SUFFIXES = 142,

    /**
     * @see #TRANSLATE_PROBE_PATH_SUFFIXES
     */
    TRANSLATE_PROBE_SUFFIX = 143,

    /**
     * Load #TRANSLATE_AUTH from a file.
     */
    TRANSLATE_AUTH_FILE = 144,

    /**
     * Expansion for #TRANSLATE_AUTH_FILE.
     */
    TRANSLATE_EXPAND_AUTH_FILE = 145,

    /**
     * Append the payload to #TRANSLATE_AUTH_FILE data.
     */
    TRANSLATE_APPEND_AUTH = 146,

    /**
     * Expansion for #TRANSLATE_APPEND_AUTH.
     */
    TRANSLATE_EXPAND_APPEND_AUTH = 147,

    /**
     * Indicates which listener accepted the connection.
     */
    TRANSLATE_LISTENER_TAG = 148,

    /**
     * Expansion for #TRANSLATE_COOKIE_HOST.
     */
    TRANSLATE_EXPAND_COOKIE_HOST = 149,

    /**
     * Expansion for #TRANSLATE_BIND_MOUNT.
     */
    TRANSLATE_EXPAND_BIND_MOUNT = 150,

    /**
     * Pass non-blocking socket to child process?
     */
    TRANSLATE_NON_BLOCKING = 151,

    /**
     * Read a file and return its contents to the translation server.
     */
    TRANSLATE_READ_FILE = 152,

    /**
     * Expansion for #TRANSLATE_READ_FILE.
     */
    TRANSLATE_EXPAND_READ_FILE = 153,

    /**
     * Expansion for #TRANSLATE_HEADER.
     */
    TRANSLATE_EXPAND_HEADER = 154,

    /**
     * If present, the use HOST+URI as input for #TRANSLATE_REGEX and
     * not just the URI.
     */
    TRANSLATE_REGEX_ON_HOST_URI = 155,

    /**
     * Set a session-wide site name.
     */
    TRANSLATE_SESSION_SITE = 156,

    /**
     * Start the child process in a new IPC namespace?
     */
    TRANSLATE_IPC_NAMESPACE = 157,

    /**
     * Deflate the response on-the-fly if the client accepts it.
     */
    TRANSLATE_AUTO_DEFLATE = 158,

    /**
     * Expansion for #TRANSLATE_HOME.
     */
    TRANSLATE_EXPAND_HOME = 159,

    /**
     * Expansion for #TRANSLATE_STDERR_PATH.
     */
    TRANSLATE_EXPAND_STDERR_PATH = 160,

    /**
     * If present, the use USER+'@'+URI as input for #TRANSLATE_REGEX
     * and not just the URI.
     */
    TRANSLATE_REGEX_ON_USER_URI = 161,

    /**
     * Gzip-compress the response on-the-fly if the client accepts it.
     */
    TRANSLATE_AUTO_GZIP = 162,

    /**
     * Re-translate with the URI specified by #TRANSLATE_URI or
     * #TRANSLATE_EXPAND_URI.
     */
    TRANSLATE_INTERNAL_REDIRECT = 163,

    /**
     * Obtain information for interactive login.  Must be followed by
     * #TRANSLATE_USER.
     */
    TRANSLATE_LOGIN = 164,

    /**
     * Specify uid and gid (and supplementary groups) for the child
     * process.  Payload is an array of 32 bit integers.
     */
    TRANSLATE_UID_GID = 165,

    /**
     * A password for #TRANSLATE_LOGIN / #TRANSLATE_USER that shall be
     * verified by the translation server.
     */
    TRANSLATE_PASSWORD = 166,

    /**
     * Configure a refence limit for the child process.
     */
    TRANSLATE_REFENCE = 167,

    /**
     * Payload specifies the service that wants to log in (see
     * #TRANSLATE_LOGIN), e.g. "ssh" or "ftp".
     */
    TRANSLATE_SERVICE = 168,

    /**
     * Unescape the URI for #TRANSLATE_INVERSE_REGEX?
     */
    TRANSLATE_INVERSE_REGEX_UNESCAPE = 169,

    /**
     * Same as #TRANSLATE_BIND_MOUNT, but don't set the "read-only" flag.
     */
    TRANSLATE_BIND_MOUNT_RW = 170,

    /**
     * Same as #TRANSLATE_EXPAND_BIND_MOUNT, but don't set the
     * "read-only" flag.
     */
    TRANSLATE_EXPAND_BIND_MOUNT_RW = 171,

    TRANSLATE_UNTRUSTED_RAW_SITE_SUFFIX = 172,

    /**
     * Mount a new tmpfs on the given path.
     */
    TRANSLATE_MOUNT_TMPFS = 173,

    /**
     * Send the X-CM4all-BENG-User header to the filter?
     */
    TRANSLATE_REVEAL_USER = 174,

    /**
     * Copy #TRANSLATE_AUTH or #TRANSLATE_AUTH_FILE (without
     * #TRANSLATE_APPEND_AUTH) to #TRANSLATE_REALM
     */
    TRANSLATE_REALM_FROM_AUTH_BASE = 175,

    /**
     * Permanently disable new privileges for the child process.
     */
    TRANSLATE_NO_NEW_PRIVS = 176,

    /**
     * Move the child process into a cgroup (payload is the cgroup's
     * base name).
     */
    TRANSLATE_CGROUP = 177,

    /**
     * Set a cgroup attribute.  Payload is in the form
     * "controller.name=value", e.g. "cpu.shares=42".
     */
    TRANSLATE_CGROUP_SET = 178,

    /**
     * A http:// URL for this session in an external session manager.
     * GET refreshes the session
     * (#TRANSLATE_EXTERNAL_SESSION_KEEPALIVE), DELETE discards it
     * (#TRANSLATE_DISCARD_SESSION).
     */
    TRANSLATE_EXTERNAL_SESSION_MANAGER = 179,

    /**
     * 16 bit integer specifying the number of seconds between to
     * refresh (GET) calls on #TRANSLATE_EXTERNAL_SESSION_MANAGER.
     */
    TRANSLATE_EXTERNAL_SESSION_KEEPALIVE = 180,

    /**
     * Mark this request as a "cron job" request.  No payload.
     */
    TRANSLATE_CRON = 181,

    /**
     * Same as #TRANSLATE_BIND_MOUNT, but don't set the "noexec" flag.
     */
    TRANSLATE_BIND_MOUNT_EXEC = 182,

    /**
     * Same as #TRANSLATE_EXPAND_BIND_MOUNT, but don't set the
     * "noexec" flag.
     */
    TRANSLATE_EXPAND_BIND_MOUNT_EXEC = 183,

    /**
     * Redirect STDERR to /dev/null?
     */
    TRANSLATE_STDERR_NULL = 184,

    /**
     * Execute the specified program.  May be followed by
     * #TRANSLATE_APPEND packets.  This is used by Workshop/Cron.
     */
    TRANSLATE_EXECUTE = 185,
};

struct beng_translation_header {
    uint16_t length;
    uint16_t command;
};

#endif
