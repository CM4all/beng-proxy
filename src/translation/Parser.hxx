/*
 * Parse translation response packets.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_TRANSLATE_PARSER_HXX
#define BENG_PROXY_TRANSLATE_PARSER_HXX

#include "PReader.hxx"
#include "Response.hxx"
#include "Request.hxx"
#include "ExpandableStringList.hxx"

struct FileAddress;
struct CgiAddress;
struct HttpAddress;
struct LhttpAddress;
struct NfsAddress;
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
    FileAddress *file_address;

    /** the current HTTP/AJP address being edited */
    HttpAddress *http_address;

    /** the current CGI/FastCGI/WAS address being edited */
    CgiAddress *cgi_address;

    /** the current NFS address being edited */
    NfsAddress *nfs_address;

    /** the current "local HTTP" address being edited */
    LhttpAddress *lhttp_address;

    /** the current address list being edited */
    AddressList *address_list;

    ExpandableStringList::Builder env_builder, args_builder, params_builder;

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
        :pool(&_pool), from_request(r) {
        response.status = (http_status_t)-1;
    }

    size_t Feed(const uint8_t *data, size_t length) {
        return reader.Feed(pool, data, length);
    }

    enum class Result {
        MORE,
        DONE,
    };

    /**
     * Throws std::runtime_error on error.
     */
    Result Process();

    TranslateResponse &GetResponse() {
        return response;
    }

private:
    void SetChildOptions(ChildOptions &_child_options);
    void SetCgiAddress(ResourceAddress::Type type, const char *path);

    /**
     * Throws std::runtime_error on error.
     */
    void AddView(const char *name);

    /**
     * Finish the settings in the current view, i.e. copy attributes
     * from the "parent" view.
     *
     * Throws std::runtime_error on error.
     */
    void FinishView();

    Transformation *AddTransformation();
    ResourceAddress *AddFilter();

    void HandleBindMount(const char *payload, size_t payload_length,
                         bool expand, bool writable);

    void HandleRefence(StringView payload);

    void HandleWant(const uint16_t *payload, size_t payload_length);

    void HandleContentTypeLookup(ConstBuffer<void> payload);

    void HandleRegularPacket(enum beng_translation_command command,
                             const void *const _payload, size_t payload_length);

    void HandleUidGid(ConstBuffer<void> payload);

    void HandleCgroupSet(StringView payload);

    Result HandlePacket(enum beng_translation_command command,
                        const void *const _payload, size_t payload_length);
};

#endif
