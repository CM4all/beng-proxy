t_istream_filter_deps = [
  gtest,
  test_instance_dep,
  istream_dep,
]

istream_test_sources = []
if libbrotlienc.found()
  istream_test_sources += 'TestBrotliEncoderIstream.cxx'
endif

test(
  'IstreamFilterTest',
  executable(
    'IstreamFilterTest',
    'IstreamFilterTest.cxx',
    't_istream_byte.cxx',
    't_istream_cat.cxx',
    't_istream_catch.cxx',
    't_istream_chunked.cxx',
    't_istream_dechunk.cxx',
    'TestGzipIstream.cxx',
    't_istream_delayed.cxx',
    't_istream_fcgi.cxx',
    '../../src/fcgi/istream_fcgi.cxx',
    't_istream_hold.cxx',
    't_istream_html_escape.cxx',
    '../../src/escape/Istream.cxx',
    '../../src/istream_html_escape.cxx',
    '../../src/istream/sink_close.cxx',
    't_istream_iconv.cxx',
    't_istream_replace.cxx',
    't_istream_subst.cxx',
    't_istream_tee.cxx',
    'TestAutoPipeIstream.cxx',
    'TestBufferedIstream.cxx',
    'TestLengthIstream.cxx',
    istream_test_sources,
    include_directories: inc,
    dependencies: [
      t_istream_filter_deps,
      istream_pipe_dep,
      istream_extra_dep,
      stock_dep,
    ],
  ),
)

test(
  't_istream_processor',
  executable(
    't_istream_processor',
    't_istream_processor.cxx',
    'IstreamFilterTest.cxx',
    '../FailingResourceLoader.cxx',
    '../../src/widget/FromSession.cxx',
    '../../src/widget/FromRequest.cxx',
    '../../src/widget/Resolver.cxx',
    '../../src/widget/RewriteUri.cxx',
    '../../src/escape/Istream.cxx',
    '../../src/istream_html_escape.cxx',
    include_directories: inc,
    dependencies: [
      gtest,
      processor_dep,
      widget_dep,
      session_dep,
    ] + t_istream_filter_deps,
  ),
)

test(
  'TestCssProcessor',
  executable(
    'TestCssProcessor',
    'TestCssProcessor.cxx',
    'IstreamFilterTest.cxx',
    '../FailingResourceLoader.cxx',
    '../../src/widget/FromSession.cxx',
    '../../src/widget/FromRequest.cxx',
    '../../src/widget/Resolver.cxx',
    '../../src/widget/RewriteUri.cxx',
    '../../src/escape/Istream.cxx',
    '../../src/istream_html_escape.cxx',
    include_directories: inc,
    dependencies: [
      gtest,
      processor_dep,
      widget_dep,
      session_dep,
    ] + t_istream_filter_deps,
  ),
)

if uring_dep.found()
  test(
    'TestUringIstream',
    executable(
      'TestUringIstream',
      'TestUringIstream.cxx',
      include_directories: inc,
      dependencies: [
        gtest,
        test_instance_dep,
        uring_dep,
        istream_dep,
      ],
    ),
  )
endif
