http_util = static_library(
  'http_util',
  'Tokenizer.cxx',
  'PTokenizer.cxx',
  'Quote.cxx',
  'Upgrade.cxx',
  'PDigestHeader.cxx',
  'PList.cxx',
  'PHeaderUtil.cxx',
  'HeaderUtil.cxx',
  'HeaderParser.cxx',
  'HeaderWriter.cxx',
  'CombinedHeaders.cxx',
  'XForwardedFor.cxx',
  'ChunkParser.cxx',
  include_directories: inc,
  dependencies: [
    memory_dep,
    net_dep,
    fmt_dep,
  ],
)

http_util_dep = declare_dependency(
  link_with: http_util,
  dependencies: [
    pool_dep,
    putil_dep,
    http_dep,
  ],
)

http_extra = static_library(
  'http_extra',
  'WaitTracker.cxx',
  include_directories: inc,
  dependencies: [
    event_dep,
  ],
)

http_extra_dep = declare_dependency(
  link_with: http_extra,
)

cookie_compile_args = []

libpsl = dependency('libpsl', required: get_option('psl'))
if libpsl.found()
  cookie_compile_args += '-DHAVE_LIBPSL'
endif

cookie = static_library(
  'cookie',
  'CookieJar.cxx',
  'CookieClient.cxx',
  'CookieExtract.cxx',
  'CookieSameSite.cxx',
  'CookieServer.cxx',
  'CookieString.cxx',
  cpp_args: cookie_compile_args,
  include_directories: inc,
  dependencies: [
    libpsl,
  ],
)

cookie_dep = declare_dependency(
  compile_args: cookie_compile_args,
  link_with: cookie,
  dependencies: [
    http_util_dep,
    putil_dep,
    libpsl,
  ],
)
