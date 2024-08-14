debug_resource_loader = static_library(
  'debug_resource_loader',
  'BlockingResourceLoader.cxx',
  'FailingResourceLoader.cxx',
  'MirrorResourceLoader.cxx',
  include_directories: inc,
  dependencies: [
    pool_dep,
    istream_api_dep,
  ],
)

debug_resource_loader_dep = declare_dependency(
  link_with: debug_resource_loader,
  dependencies: [
    pool_dep,
    istream_api_dep,
  ],
)
