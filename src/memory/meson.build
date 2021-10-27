memory = static_library(
  'memory',
  'fb_pool.cxx',
  'SlicePool.cxx',
  'SliceAllocation.cxx',
  'SliceFifoBuffer.cxx',
  'MultiFifoBuffer.cxx',
  '../DefaultFifoBuffer.cxx',
  'DefaultChunkAllocator.cxx',
  'ExpansibleBuffer.cxx',
  'GrowingBuffer.cxx',
  'Rubber.cxx',
  include_directories: inc,
)

memory_dep = declare_dependency(
  link_with: memory,
  dependencies: [system_dep],
)
