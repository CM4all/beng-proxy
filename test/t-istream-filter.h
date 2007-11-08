static int should_exit = 0;

/*
 * istream handler
 *
 */

static size_t
my_istream_data(const void *data, size_t length, void *ctx)
{
    (void)data;
    (void)ctx;
    printf("data(%zu)\n", length);
    return length;
}

static ssize_t
my_istream_direct(istream_direct_t type, int fd, size_t max_length, void *ctx)
{
    (void)fd;
    (void)ctx;
    printf("direct(%u, %zu)\n", type, max_length);
    return max_length;
}

static void
my_istream_eof(void *ctx)
{
    (void)ctx;
    printf("eof\n");
    should_exit = 1;
}

static void
my_istream_abort(void *ctx)
{
    (void)ctx;
    printf("abort\n");
    should_exit = 1;
}

static const struct istream_handler my_istream_handler = {
    .data = my_istream_data,
    .direct = my_istream_direct,
    .eof = my_istream_eof,
    .abort = my_istream_abort,
};


/*
 * main
 *
 */


int main(int argc, char **argv) {
    pool_t root_pool, pool;
    istream_t istream;

    (void)argc;
    (void)argv;

    root_pool = pool_new_libc(NULL, "root");

    /* normal run */

    should_exit = 0;

    pool = pool_new_linear(root_pool, "test", 8192);

    istream = create_test(pool, create_input(pool));
    istream_handler_set(istream, &my_istream_handler, NULL, 0);

    pool_unref(pool);
    pool_commit();

    while (!should_exit)
        istream_read(istream);

    pool_commit();

    /* now with istream_byte */

    should_exit = 0;

    pool = pool_new_linear(root_pool, "test", 8192);

    istream = create_test(pool, istream_byte_new(pool, create_input(pool)));
    istream_handler_set(istream, &my_istream_handler, NULL, 0);

    pool_unref(pool);
    pool_commit();

    while (!should_exit)
        istream_read(istream);

    pool_commit();

    /* now with fail */

    should_exit = 0;

    pool = pool_new_linear(root_pool, "test", 8192);

    istream = create_test(pool, istream_fail_new(pool));
    istream_handler_set(istream, &my_istream_handler, NULL, 0);

    pool_unref(pool);
    pool_commit();

    while (!should_exit)
        istream_read(istream);

    pool_commit();

    /* fail after 1 byte of input */

    should_exit = 0;

    pool = pool_new_linear(root_pool, "test", 8192);

    istream = create_test(pool,
                          istream_cat_new(pool,
                                          istream_head_new(pool, create_input(pool), 1),
                                          istream_fail_new(pool),
                                          NULL));
    istream_handler_set(istream, &my_istream_handler, NULL, 0);

    pool_unref(pool);
    pool_commit();

    while (!should_exit)
        istream_read(istream);

    pool_commit();

    /* abort without handler */

    should_exit = 0;

    pool = pool_new_linear(root_pool, "test", 8192);

    istream = create_test(pool, create_input(pool));
    istream_close(istream);

    pool_unref(pool);
    pool_commit();

    assert(!should_exit);

    /* abort with handler */

    should_exit = 0;

    pool = pool_new_linear(root_pool, "test", 8192);

    istream = create_test(pool, create_input(pool));
    istream_handler_set(istream, &my_istream_handler, NULL, 0);

    istream_close(istream);

    pool_unref(pool);
    pool_commit();

    assert(should_exit);

    /* abort after 1 byte of output */

    should_exit = 0;

    pool = pool_new_linear(root_pool, "test", 8192);

    istream = istream_head_new(pool,
                               create_test(pool,
                                           create_input(pool)),
                               1);

    istream_handler_set(istream, &my_istream_handler, NULL, 0);

    pool_unref(pool);
    pool_commit();

    while (!should_exit)
        istream_read(istream);

    pool_commit();

    /* cleanup */

    pool_unref(root_pool);
    pool_commit();

    pool_recycler_clear();
}
