/*
 * Implementation of TRANSLATE_FILE_NOT_FOUND.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_FILE_NOT_FOUND_HXX
#define BENG_PROXY_FILE_NOT_FOUND_HXX

struct request;
struct TranslateResponse;

/**
 * The #TranslateResponse contains #TRANSLATE_FILE_NOT_FOUND.  Check
 * if the file exists, and if not, retranslate.
 *
 * @return true to continue handling the request, false on error or if
 * retranslation has been triggered
 */
bool
check_file_not_found(struct request &request,
                     const TranslateResponse &response);

#endif
