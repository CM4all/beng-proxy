/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef BENG_PROXY_WIDGET_ERROR_HXX
#define BENG_PROXY_WIDGET_ERROR_HXX

#include <stdexcept>

struct Widget;

enum class WidgetErrorCode {
    UNSPECIFIED,

    /**
     * The content-type of the server's response does not meet our
     * expectations.
     */
    WRONG_TYPE,

    /**
     * The response body is encoded in an unsupported way.
     */
    UNSUPPORTED_ENCODING,

    /**
     * The requested view does not exist.
     */
    NO_SUCH_VIEW,

    /**
     * Looking for a child widget inside a widget that is not a
     * container.
     */
    NOT_A_CONTAINER,

    /**
     * The client request is forbidden due to formal reasons.
     */
    FORBIDDEN,
};

class WidgetError : public std::runtime_error {
    WidgetErrorCode code;

public:
    WidgetError(WidgetErrorCode _code, const char *_msg)
        :std::runtime_error(_msg), code(_code) {}

    WidgetError(const Widget &widget, WidgetErrorCode _code, const char *_msg);

    WidgetErrorCode GetCode() const {
        return code;
    }
};

#endif
