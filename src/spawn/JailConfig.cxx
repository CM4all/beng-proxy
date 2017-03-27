/*
 * JailCGI integration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "JailConfig.hxx"
#include "AllocatorPtr.hxx"
#include "util/CharUtil.hxx"
#include "util/StringUtil.hxx"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static char *
next_word(char *p)
{
    while (!IsWhitespaceOrNull(*p))
        ++p;

    if (*p == 0)
        return nullptr;

    *p++ = 0;

    p = StripLeft(p);

    if (*p == 0)
        return nullptr;

    return p;
}

bool
JailConfig::Load(const char *path)
{
    assert(root_dir.empty());
    assert(jailed_home.empty());

    FILE *file = fopen(path, "r");
    if (file == nullptr)
        return false;

    char line[4096], *p, *q;
    while ((p = fgets(line, sizeof(line), file)) != nullptr) {
        p = StripLeft(p);

        if (*p == 0 || *p == '#')
            /* ignore comments */
            continue;

        q = next_word(p);
        if (q == nullptr || next_word(q) != nullptr)
            /* silently ignore syntax errors */
            continue;

        if (strcmp(p, "RootDir") == 0)
            root_dir = q;
        else if (strcmp(p, "JailedHome") == 0)
            jailed_home = q;
    }

    fclose(file);
    return !root_dir.empty() && !jailed_home.empty();
}

static const char *
jail_try_translate_path(const char *path,
                        const char *global_prefix, const char *jailed_prefix,
                        AllocatorPtr alloc)
{
    if (jailed_prefix == nullptr)
        return nullptr;

    size_t global_prefix_length = strlen(global_prefix);
    if (memcmp(path, global_prefix, global_prefix_length) != 0)
        return nullptr;

    if (path[global_prefix_length] == '/')
        return alloc.Concat(jailed_prefix, path + global_prefix_length);
    else if (path[global_prefix_length] == 0)
        return jailed_prefix;
    else
        return nullptr;
}

const char *
JailConfig::TranslatePath(const char *path,
                          const char *document_root, AllocatorPtr alloc) const
{
    const char *translated =
        jail_try_translate_path(path, document_root,
                                jailed_home.c_str(), alloc);
    if (translated == nullptr)
        translated = jail_try_translate_path(path, root_dir.c_str(),
                                             "", alloc);
    return translated;
}
