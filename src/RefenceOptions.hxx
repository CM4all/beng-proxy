/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_REFENCE_OPTIONS_HXX
#define BENG_PROXY_REFENCE_OPTIONS_HXX

#include <util/ConstBuffer.hxx>

/**
 * Options for Refence.
 */
class RefenceOptions {
    ConstBuffer<char> data;

public:
    RefenceOptions() = default;

    RefenceOptions(struct pool &p, const RefenceOptions &src) {
        CopyFrom(p, src);
    }

    void Init() {
        data = nullptr;
    }

    bool IsEmpty() const {
        return data.IsEmpty();
    }

    void Set(ConstBuffer<char> _data) {
        data = _data;
    }

    void CopyFrom(struct pool &p, const RefenceOptions &src);

    char *MakeId(char *p) const;

    void Apply() const;

private:
    unsigned GetHash() const;

    void Apply(int fd) const;
};

#endif
