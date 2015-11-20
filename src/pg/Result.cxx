/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Result.hxx"

std::string
PgResult::GetOnlyStringChecked() const
{
    if (!IsQuerySuccessful() || GetRowCount() == 0)
        return std::string();

    const char *value = GetValue(0, 0);
    if (value == nullptr)
        return std::string();

    return value;
}
