/*
 * Dumping widget information to the log file.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WIDGET_DUMP_H
#define BENG_PROXY_WIDGET_DUMP_H

struct pool;
struct istream;
struct widget;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Dump the widget tree to the log file after the istream is done.
 */
struct istream *
widget_dump_tree_after_istream(struct pool *pool, struct istream *istream,
                               struct widget *widget);

#ifdef __cplusplus
}
#endif

#endif
