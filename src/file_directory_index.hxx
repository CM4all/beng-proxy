/*
 * Implementation of TRANSLATE_DIRECTORY_INDEX.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_FILE_DIRECTORY_INDEX_HXX
#define BENG_PROXY_FILE_DIRECTORY_INDEX_HXX

struct request;
struct TranslateResponse;

/**
 * The #TranslateResponse contains #TRANSLATE_DIRECTORY_INDEX.  Check
 * if the file is a directory, and if it is, retranslate.
 *
 * @return true to continue handling the request, false on error or if
 * retranslation has been triggered
 */
bool
check_directory_index(struct request &request,
                      const TranslateResponse &response);

#endif
