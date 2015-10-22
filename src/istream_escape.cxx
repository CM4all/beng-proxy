/*
 * An istream filter that escapes the data.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_escape.hxx"
#include "istream/FacadeIstream.hxx"
#include "escape_class.hxx"
#include "util/ConstBuffer.hxx"

#include <assert.h>
#include <string.h>

class EscapeIstream final : public FacadeIstream {
    const struct escape_class &cls;

    ConstBuffer<char> escaped;

public:
    EscapeIstream(struct pool &_pool, struct istream &_input,
                  const struct escape_class &_cls)
        :FacadeIstream(_pool, _input,
                       MakeIstreamHandler<EscapeIstream>::handler, this),
         cls(_cls) {
        escaped.size = 0;
    }

    bool SendEscaped();

    /* virtual methods from class Istream */

    off_t _GetAvailable(bool partial) override {
        if (!HasInput())
            return escaped.size;

        return partial
            ? escaped.size + input.GetAvailable(partial)
            : -1;
    }

    off_t _Skip(gcc_unused off_t length) override {
        return -1;
    }

    void _Read() override;

    int _AsFd() override {
        return -1;
    }

    void _Close() override;

    /* handler */

    size_t OnData(const void *data, size_t length);

    ssize_t OnDirect(gcc_unused FdType type, gcc_unused int fd,
                     gcc_unused size_t max_length) {
        gcc_unreachable();
    }

    void OnEof() {
        ClearInput();

        if (escaped.IsEmpty())
            DestroyEof();
    }

    void OnError(GError *error) {
        ClearInput();
        DestroyError(error);
    }
};

bool
EscapeIstream::SendEscaped()
{
    assert(!escaped.IsEmpty());

    size_t nbytes = InvokeData(escaped.data, escaped.size);
    if (nbytes == 0)
        return false;

    escaped.skip_front(nbytes);
    if (!escaped.IsEmpty())
        return false;

    if (!HasInput()) {
        DestroyEof();
        return false;
    }

    return true;
}

/*
 * istream handler
 *
 */

size_t
EscapeIstream::OnData(const void *data0, size_t length)
{
    const char *data = (const char *)data0;

    if (!escaped.IsEmpty() && !SendEscaped())
        return 0;

    size_t total = 0;

    const ScopePoolRef ref(GetPool() TRACE_ARGS);

    do {
        /* find the next control character */
        const char *control = escape_find(&cls, {data, length});
        if (control == nullptr) {
            /* none found - just forward the data block to our sink */
            size_t nbytes = InvokeData(data, length);
            if (nbytes == 0 && !HasInput())
                total = 0;
            else
                total += nbytes;
            break;
        }

        if (control > data) {
            /* forward the portion before the control character */
            const size_t n = control - data;
            size_t nbytes = InvokeData(data, n);
            if (nbytes == 0 && !HasInput()) {
                total = 0;
                break;
            }

            total += nbytes;
            if (nbytes < n)
                break;
        }

        /* consume everything until after the control character */

        length -= control - data + 1;
        data = control + 1;
        ++total;

        /* insert the entity into the stream */

        escaped.data = escape_char(&cls, *control);
        escaped.size = strlen(escaped.data);

        if (!SendEscaped()) {
            if (!HasInput())
                total = 0;
            break;
        }
    } while (length > 0);

    return total;
}

/*
 * istream implementation
 *
 */

void
EscapeIstream::_Read()
{
    if (!escaped.IsEmpty() && !SendEscaped())
        return;

    input.Read();
}

void
EscapeIstream::_Close()
{
    if (HasInput())
        ClearAndCloseInput();

    Destroy();
}

/*
 * constructor
 *
 */

struct istream *
istream_escape_new(struct pool *pool, struct istream *input,
                   const struct escape_class *cls)
{
    assert(input != nullptr);
    assert(!istream_has_handler(input));
    assert(cls != nullptr);
    assert(cls->escape_find != nullptr);
    assert(cls->escape_char != nullptr);

    return NewIstream<EscapeIstream>(*pool, *input, *cls);
}
