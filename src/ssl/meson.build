ssl2 = static_library(
  'ssl2',
  'Basic.cxx',
  'Client.cxx',
  'Factory.cxx',
  'AlpnSelect.cxx',
  'AlpnEnable.cxx',
  'FifoBufferBio.cxx',
  'Filter.cxx',
  'Init.cxx',
  ssl2_sources,
  include_directories: inc,
  dependencies: [
    ssl_dep,
    pg_dep,
  ],
)

ssl_dep = declare_dependency(
  link_with: ssl2,
  dependencies: [
    ssl_dep,
  ],
)
