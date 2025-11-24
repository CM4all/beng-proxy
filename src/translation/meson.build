libcommon_translation_dep = translation_dep

translation2 = static_library(
  'translation2',
  '../net/NetworkList.cxx',
  'Builder.cxx',
  'Multi.cxx',
  'Cache.cxx',
  'Stock.cxx',
  'Glue.cxx',
  'Layout.cxx',
  'Marshal.cxx',
  'Client.cxx',
  'Transformation.cxx',
  'FilterTransformation.cxx',
  'Vary.cxx',
  'InvalidateParser.cxx',
  'CoTranslate.cxx',
  'ListenStreamStockHandler.cxx',
  'FailingService.cxx',
  include_directories: inc,
  dependencies: [
    coroutines_dep,
    fmt_dep,
    pcre_dep,
  ],
)

translation_dep = declare_dependency(
  link_with: translation2,
  dependencies: [
    coroutines_dep,
    translation_dep,
    event_dep,
    putil_dep,
    cache_dep,
    raddress_dep,
    socket_dep,
    stopwatch_dep
  ],
)
