http_cache = static_library(
  'http_cache',
  'Public.cxx',
  'Document.cxx',
  'Age.cxx',
  'Heap.cxx',
  'Item.cxx',
  'Info.cxx',
  'RFC.cxx',
  include_directories: inc,
  dependencies: [
    fmt_dep,
    cache_dep,
  ],
)

http_cache_dep = declare_dependency(
  link_with: http_cache,
  dependencies: [
    cache_dep,
    http_util_dep,
    istream_dep,
    memory_istream_dep,
    raddress_dep,
    stopwatch_dep,
  ],
)
