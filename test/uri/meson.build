test(
  'TestRedirectHttps',
  executable(
    'TestRedirectHttps',
    'TestRedirectHttps.cxx',
    include_directories: inc,
    dependencies: [
      gtest,
      putil_dep,
      net_dep,
    ],
  ),
)

test(
  't_relocate_uri',
  executable(
    't_relocate_uri',
    't_relocate_uri.cxx',
    '../../src/uri/Relocate.cxx',
    include_directories: inc,
    dependencies: [
      gtest,
      putil_dep,
    ],
  ),
)

test(
  'TestWidget',
  executable(
    'TestWidget',
    'TestArgs.cxx',
    include_directories: inc,
    dependencies: [
      gtest,
      widget_dep,
    ],
  ),
)

test(
  't_uri',
  executable(
    't_uri',
    'TestUriPRelative.cxx',
    'TestUriRelative.cxx',
    't_uri_compare.cxx',
    'TestUriDissect.cxx',
    include_directories: inc,
    dependencies: [
      gtest,
      util_dep,
      putil_dep,
    ],
  ),
)
