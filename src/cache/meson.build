cache = static_library(
  'cache',
  'Cache.cxx',
  'Item.cxx',
  include_directories: inc,
  dependencies: [
    event_dep,
  ],
)

cache_dep = declare_dependency(
  link_with: cache,
  dependencies: [
    event_dep,
  ],
)
