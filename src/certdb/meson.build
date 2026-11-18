libcertdb = static_library(
  'certdb',
  'CertDatabase.cxx',
  'CoCertDatabase.cxx',
  'Config.cxx',
  'FromResult.cxx',
  'WrapKey.cxx',
  'Wildcard.cxx',
  include_directories: inc,
  dependencies: [
    crypto_dep,
    pg_dep,
    coroutines_dep,
    util_dep,
    sodium_dep,
    io_config_dep,
    fmt_dep,
  ],
)

libcertdb_dep = declare_dependency(
  link_with: libcertdb,
  dependencies: [
    crypto_dep,
    pg_dep,
  ],
)
