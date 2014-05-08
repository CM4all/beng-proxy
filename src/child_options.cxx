/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "child_options.hxx"

void
child_options::CopyFrom(struct pool *pool, const struct child_options *src)
{
    rlimit_options_copy(&rlimits, &src->rlimits);
    namespace_options_copy(pool, &ns, &src->ns);
    jail_params_copy(pool, &jail, &src->jail);
}

char *
child_options::MakeId(char *p) const
{
    p = rlimit_options_id(&rlimits, p);
    p = namespace_options_id(&ns, p);
    p = jail_params_id(&jail, p);
    return p;
}
