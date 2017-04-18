/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef ACME_CONFIG_HXX
#define ACME_CONFIG_HXX

struct AcmeConfig {
    bool staging = false;

    bool fake = false;
};

#endif
