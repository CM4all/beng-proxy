libnfs = dependency('libnfs', required: get_option('nfs'))
if not libnfs.found()
  nfs_client_dep = libnfs
  subdir_done()
endif

nfs_client = static_library(
  'nfs_client',
  'Stock.cxx',
  'Client.cxx',
  'Error.cxx',
  'Cache.cxx',
  'Glue.cxx',
  include_directories: inc,
  dependencies: [
    libnfs,
  ],
)

nfs_client_dep = declare_dependency(
  compile_args: '-DHAVE_LIBNFS',
  link_with: nfs_client,
  dependencies: [
    event_dep,
  ],
)
