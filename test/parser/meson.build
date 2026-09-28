test(
  'TestXmlParser',
  executable(
    'TestXmlParser',
    'TestXmlParser.cxx',
    '../../src/parser/XmlParser.cxx',
    include_directories: inc,
    dependencies: [
      gtest,
      putil_dep,
    ],
  ),
)

test(
  'TestCssParser',
  executable(
    'TestCssParser',
    'TestCssParser.cxx',
    '../../src/parser/CssParser.cxx',
    include_directories: inc,
    dependencies: [
      gtest,
      util_dep,
    ],
  ),
  timeout: 10,
)
