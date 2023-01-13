cluster = static_library(
  'cluster',
  'AddressSticky.cxx',
  'BalancerMap.cxx',
  'FailureManagerProxy.cxx',
  include_directories: inc,
  dependencies: [
    sodium_dep,
  ],
)

cluster_dep = declare_dependency(
  link_with: cluster,
)
