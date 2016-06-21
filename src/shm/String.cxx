/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "String.hxx"
#include "dpool.hxx"

void
DString::Clear(struct dpool &pool) noexcept
{
    if (value != nullptr) {
        d_free(pool, value);
        value = nullptr;
    }
}

void
DString::Set(struct dpool &pool, StringView new_value) throw(std::bad_alloc)
{
    if (value != nullptr && !new_value.IsNull() && new_value.Equals(value))
        /* same value as before: no-op */
        return;

    Clear(pool);

    if (!new_value.IsNull()) {
        value = d_strdup(pool, new_value);
        if (value == nullptr)
            throw std::bad_alloc();
    }
}

bool
DString::SetNoExcept(struct dpool &pool, StringView new_value) noexcept
{
    try {
        Set(pool, new_value);
        return true;
    } catch (std::bad_alloc) {
        return false;
    }
}
