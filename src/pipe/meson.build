pipe = static_library(
  'pipe',
  'Stock.cxx',
  'Lease.cxx',
  include_directories: inc,
)

pipe_dep = declare_dependency(
  link_with: pipe,
  dependencies: [
    io_dep,
    stock_dep,
  ],
)
