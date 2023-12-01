test(
  't_uri',
  executable(
    't_uri',
    'TestArgs.cxx',
    'TestUriPRelative.cxx',
    'TestUriRelative.cxx',
    't_uri_compare.cxx',
    'TestUriDissect.cxx',
    'TestRedirectHttps.cxx',
    't_relocate_uri.cxx',
    '../../src/uri/Args.cxx',
    '../../src/uri/Relocate.cxx',
    include_directories: inc,
    dependencies: [
      gtest,
      util_dep,
      putil_dep,
      net_dep,
    ],
  ),
)
