prometheus = static_library(
  'prometheus',
  'Stats.cxx',
  'HttpStats.cxx',
  include_directories: inc,
)

prometheus_dep = declare_dependency(
  link_with: prometheus,
  dependencies: [
    memory_dep,
  ],
)
