/*
 * istream implementation which blocks indefinitely until closed.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_block.hxx"
#include "istream.hxx"

class BlockIstream final : public Istream {
public:
    explicit BlockIstream(struct pool &p):Istream(p) {}

    /* virtual methods from class Istream */

    void _Read() override {
    }
};

Istream *
istream_block_new(struct pool &pool)
{
    return NewIstream<BlockIstream>(pool);
}
