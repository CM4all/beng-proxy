/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef ACME_CONFIG_HXX
#define ACME_CONFIG_HXX

struct AcmeConfig {
    std::string agreement_url = "https://letsencrypt.org/documents/LE-SA-v1.1.1-August-1-2016.pdf";

    bool staging = false;

    bool fake = false;
};

#endif
