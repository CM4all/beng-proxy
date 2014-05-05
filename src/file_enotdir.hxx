/*
 * Implementation of TRANSLATE_ENOTDIR.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ENOTDIR_HXX
#define BENG_PROXY_ENOTDIR_HXX

struct request;
struct TranslateResponse;

/**
 * The #TranslateResponse contains #TRANSLATE_ENOTDIR.  Check this
 * condition and retranslate.
 *
 * @return true to continue handling the request, false on error or if
 * retranslation has been triggered
 */
bool
check_file_enotdir(struct request &request, const TranslateResponse &response);

/**
 * Append the ENOTDIR PATH_INFO to the resource address.
 */
void
apply_file_enotdir(struct request &request);

#endif
