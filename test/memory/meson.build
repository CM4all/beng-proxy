test(
  't_expansible_buffer',
  executable(
    't_expansible_buffer',
    't_expansible_buffer.cxx',
    include_directories: inc,
    dependencies: [
      gtest,
      putil_dep,
    ],
  ),
)

test(
  't_pool',
  executable(
    't_pool',
    't_pool.cxx',
    include_directories: inc,
    dependencies: [
      gtest,
      pool_dep,
    ],
  ),
)

test(
  't_rubber',
  executable(
    't_rubber',
    't_rubber.cxx',
    include_directories: inc,
    dependencies: [
      gtest,
      memory_dep,
    ],
  ),
)

test(
  't_sink_rubber',
  executable(
    't_sink_rubber',
    't_sink_rubber.cxx',
    include_directories: inc,
    dependencies: [
      gtest,
      istream_dep,
      memory_istream_dep,
      net_dep,
    ],
  ),
)

test(
  't_growing_buffer',
  executable(
    't_growing_buffer',
    't_growing_buffer.cxx',
    include_directories: inc,
    dependencies: [
      gtest,
      istream_api_dep,
      memory_istream_dep,
    ],
  ),
)
