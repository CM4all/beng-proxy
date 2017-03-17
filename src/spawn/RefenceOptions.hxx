/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_REFENCE_OPTIONS_HXX
#define BENG_PROXY_REFENCE_OPTIONS_HXX

#include "util/StringView.hxx"

class AllocatorPtr;
class FileDescriptor;

/**
 * Options for Refence.
 */
class RefenceOptions {
    StringView data = nullptr;

public:
    RefenceOptions() = default;
    RefenceOptions(AllocatorPtr alloc, const RefenceOptions &src);

    bool IsEmpty() const {
        return data.IsEmpty();
    }

    constexpr StringView Get() const {
        return data;
    }

    void Set(StringView _data) {
        data = _data;
    }

    char *MakeId(char *p) const;

    void Apply() const;

private:
    unsigned GetHash() const;

    void Apply(FileDescriptor fd) const;
};

#endif
