t_istream_filter_deps = [
  gtest,
  test_instance_dep,
  istream_dep,
]

istream_test_sources = []
if libbrotlienc.found()
  istream_test_sources += 'TestBrotliEncoderIstream.cxx'
  t_istream_filter_deps += dependency('libbrotlidec')
endif

test(
  'IstreamFilterTest',
  executable(
    'IstreamFilterTest',
    'IstreamFilterTest.cxx',
    'TestByteIstream.cxx',
    'TestConcatIstream.cxx',
    't_istream_catch.cxx',
    'TestChunkedIstream.cxx',
    'TestDechunkIstream.cxx',
    'TestGzipIstream.cxx',
    't_istream_delayed.cxx',
    't_istream_fcgi.cxx',
    '../../src/fcgi/istream_fcgi.cxx',
    't_istream_hold.cxx',
    't_istream_html_escape.cxx',
    '../../src/escape/Istream.cxx',
    '../../src/istream/sink_close.cxx',
    't_istream_iconv.cxx',
    't_istream_replace.cxx',
    't_istream_subst.cxx',
    't_istream_tee.cxx',
    'TestAutoPipeIstream.cxx',
    'TestBufferedIstream.cxx',
    'TestLengthIstream.cxx',
    'TestThreadIstream.cxx',
    istream_test_sources,
    include_directories: inc,
    dependencies: [
      t_istream_filter_deps,
      istream_pipe_dep,
      istream_extra_dep,
      stock_dep,
      zlib,
      thread_pool_dep,
    ],
  ),
)

test(
  't_istream_processor',
  executable(
    't_istream_processor',
    't_istream_processor.cxx',
    'IstreamFilterTest.cxx',
    '../../src/widget/FromSession.cxx',
    '../../src/widget/FromRequest.cxx',
    '../../src/widget/Resolver.cxx',
    '../../src/widget/RewriteUri.cxx',
    '../../src/escape/Istream.cxx',
    include_directories: inc,
    dependencies: [
      gtest,
      processor_dep,
      widget_dep,
      session_dep,
      debug_resource_loader_dep,
    ] + t_istream_filter_deps,
  ),
)

test(
  'TestCssProcessor',
  executable(
    'TestCssProcessor',
    'TestCssProcessor.cxx',
    'IstreamFilterTest.cxx',
    '../../src/widget/FromSession.cxx',
    '../../src/widget/FromRequest.cxx',
    '../../src/widget/Resolver.cxx',
    '../../src/widget/RewriteUri.cxx',
    '../../src/escape/Istream.cxx',
    include_directories: inc,
    dependencies: [
      gtest,
      processor_dep,
      widget_dep,
      session_dep,
      debug_resource_loader_dep,
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
