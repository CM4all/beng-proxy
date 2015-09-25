/*
 * Parse translation response packets.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_TRANSLATE_PARSER_HXX
#define BENG_PROXY_TRANSLATE_PARSER_HXX

#include "translate_reader.hxx"
#include "translate_response.hxx"
#include "translate_request.hxx"
#include "glibfwd.hxx"

struct HttpAddress;
struct LhttpAddress;
struct JailParams;
struct ChildOptions;
struct NamespaceOptions;
struct MountList;
struct AddressList;
struct Transformation;
struct StringView;

class TranslateParser {
    struct pool *pool;

    struct FromRequest {
        const char *uri;

        bool want_full_uri;

        bool want;

        bool content_type_lookup;

        explicit FromRequest(const TranslateRequest &r)
            :uri(r.uri),
             want_full_uri(!r.want_full_uri.IsNull()),
             want(!r.want.IsEmpty()),
             content_type_lookup(!r.content_type_lookup.IsNull()) {}
    } from_request;

    TranslatePacketReader reader;
    TranslateResponse response;

    enum beng_translation_command previous_command;

    /** the current resource address being edited */
    ResourceAddress *resource_address;

    /** the current JailCGI parameters being edited */
    JailParams *jail;

    /** the current child process options being edited */
    ChildOptions *child_options;

    /** the current namespace options being edited */
    NamespaceOptions *ns_options;

    /** the tail of the current mount_list */
    MountList **mount_list;

    /** the current local file address being edited */
    struct file_address *file_address;

    /** the current HTTP/AJP address being edited */
    HttpAddress *http_address;

    /** the current CGI/FastCGI/WAS address being edited */
    struct cgi_address *cgi_address;

    /** the current NFS address being edited */
    struct nfs_address *nfs_address;

    /** the current "local HTTP" address being edited */
    LhttpAddress *lhttp_address;

    /** the current address list being edited */
    AddressList *address_list;

    /**
     * Default port for #TRANSLATE_ADDRESS_STRING.
     */
    int default_port;

    /** the current widget view */
    WidgetView *view;

    /** pointer to the tail of the transformation view linked list */
    WidgetView **widget_view_tail;

    /** the current transformation */
    Transformation *transformation;

    /** pointer to the tail of the transformation linked list */
    Transformation **transformation_tail;

public:
    TranslateParser(struct pool &_pool, const TranslateRequest &r)
        :pool(&_pool), from_request(r) {}

    void Init() {
        reader.Init();
        response.status = (http_status_t)-1;
    }

    size_t Feed(const uint8_t *data, size_t length) {
        return reader.Feed(pool, data, length);
    }

    enum class Result {
        ERROR,
        MORE,
        DONE,
    };

    Result Process(GError **error_r);

    TranslateResponse &GetResponse() {
        return response;
    }

private:
    bool AddView(const char *name, GError **error_r);

    /**
     * Finish the settings in the current view, i.e. copy attributes
     * from the "parent" view.
     */
    bool FinishView(GError **error_r);

    Transformation *AddTransformation();

    bool HandleBindMount(const char *payload, size_t payload_length,
                         bool expand, bool writable, GError **error_r);

    bool HandleRefence(StringView payload, GError **error_r);

    bool HandleWant(const uint16_t *payload, size_t payload_length,
                    GError **error_r);

    bool HandleContentTypeLookup(ConstBuffer<void> payload, GError **error_r);

    bool HandleRegularPacket(enum beng_translation_command command,
                             const void *const _payload, size_t payload_length,
                             GError **error_r);

    Result HandlePacket(enum beng_translation_command command,
                        const void *const _payload, size_t payload_length,
                        GError **error_r);
};

#endif
