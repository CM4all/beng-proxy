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
  'XForwardedFor.cxx',
  'ChunkParser.cxx',
  include_directories: inc,
  dependencies: [
    memory_dep,
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

cookie = static_library(
  'cookie',
  'CookieJar.cxx',
  'CookieClient.cxx',
  'CookieExtract.cxx',
  'CookieSameSite.cxx',
  'CookieServer.cxx',
  'CookieString.cxx',
  'PCookieString.cxx',
  include_directories: inc,
)

cookie_dep = declare_dependency(
  link_with: cookie,
  dependencies: [
    http_util_dep,
    putil_dep,
  ],
)
