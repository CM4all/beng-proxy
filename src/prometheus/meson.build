prometheus = static_library(
  'prometheus',
  'Stats.cxx',
  'HttpStats.cxx',
  'SpawnStats.cxx',
  'StockStats.cxx',
  include_directories: inc,
  dependencies: [
    memory_dep,
  ],
)

prometheus_dep = declare_dependency(
  link_with: prometheus,
  dependencies: [
    memory_dep,
  ],
)
