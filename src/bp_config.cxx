/*
 * Configuration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "bp_config.hxx"
#include "util/StringView.hxx"
#include "util/StringParser.hxx"

void
BpConfig::HandleSet(StringView name, const char *value)
{
    if (name.Equals("max_connections")) {
        max_connections = ParsePositiveLong(value, 1024 * 1024);
    } else if (name.Equals("tcp_stock_limit")) {
        tcp_stock_limit = ParseUnsignedLong(value);
    } else if (name.Equals("fcgi_stock_limit")) {
        fcgi_stock_limit = ParseUnsignedLong(value);
    } else if (name.Equals("fcgi_stock_max_idle")) {
        fcgi_stock_max_idle = ParseUnsignedLong(value);
    } else if (name.Equals("was_stock_limit")) {
        was_stock_limit = ParseUnsignedLong(value);
    } else if (name.Equals("was_stock_max_idle")) {
        was_stock_max_idle = ParseUnsignedLong(value);
    } else if (name.Equals("http_cache_size")) {
        http_cache_size = ParseSize(value);
        http_cache_size_set = true;
    } else if (name.Equals("filter_cache_size")) {
        filter_cache_size = ParseSize(value);
#ifdef HAVE_LIBNFS
    } else if (name.Equals("nfs_cache_size")) {
        nfs_cache_size = ParseSize(value);
#endif
    } else if (name.Equals("translate_cache_size")) {
        translate_cache_size = ParseUnsignedLong(value);
    } else if (name.Equals("translate_stock_limit")) {
        translate_stock_limit = ParseUnsignedLong(value);
    } else if (name.Equals("stopwatch")) {
        stopwatch = ParseBool(value);
    } else if (name.Equals("dump_widget_tree")) {
        dump_widget_tree = ParseBool(value);
    } else if (name.Equals("verbose_response")) {
        verbose_response = ParseBool(value);
    } else if (name.Equals("session_cookie")) {
        if (*value == 0)
            throw std::runtime_error("Invalid value");

        session_cookie = value;
    } else if (name.Equals("dynamic_session_cookie")) {
        dynamic_session_cookie = ParseBool(value);
    } else if (name.Equals("session_idle_timeout")) {
        session_idle_timeout = ParsePositiveDuration(value);
    } else if (name.Equals("session_save_path")) {
        session_save_path = value;
    } else
        throw std::runtime_error("Unknown variable");
}
