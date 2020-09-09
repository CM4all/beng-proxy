# the core Istream API
istream_api = static_library(
  'istream_api',
  'Invoke.cxx',
  'Pointer.cxx',
  'UnusedPtr.cxx',
  'istream.cxx',
  include_directories: inc,
)

istream_api_dep = declare_dependency(
  link_with: istream_api,
  dependencies: [
    pool_dep,
  ],
)

# some very basic Istream implementations with no additional
# dependencies
istream_basic = static_library(
  'istream_basic',

  # Istream implementations
  'BlockIstream.cxx',
  'ByteIstream.cxx',
  'ConcatIstream.cxx',
  'FailIstream.cxx',
  'FourIstream.cxx',
  'HeadIstream.cxx',
  'InjectIstream.cxx',
  'MemoryIstream.cxx',
  'OptionalIstream.cxx',
  'RefIstream.cxx',
  'SubstIstream.cxx',
  'ZeroIstream.cxx',
  'istream_catch.cxx',
  'istream_hold.cxx',
  'istream_memory.cxx',
  'istream_null.cxx',
  'istream_string.cxx',

  # IstreamSink implementations
  'StringSink.cxx',
  'sink_null.cxx',

  include_directories: inc,
)

istream_basic_dep = declare_dependency(
  link_with: istream_basic,
  dependencies: [
    istream_api_dep,
  ],
)

istream_sources = []
istream_compile_args = []

if uring_dep.found()
  istream_sources += 'UringIstream.cxx'
endif

libyamlcpp = dependency('yaml-cpp',
                        fallback: ['yaml-cpp', 'libyamlcpp_dep'],
                        required: get_option('yaml'))
if libyamlcpp.found()
  istream_compile_args += '-DHAVE_YAML'
  istream_sources += 'YamlSubstIstream.cxx'
endif

istream = static_library(
  'istream',

  # Istream implementations
  'BufferedIstream.cxx',
  'ChunkedIstream.cxx',
  'DechunkIstream.cxx',
  'DelayedIstream.cxx',
  'FdIstream.cxx',
  'FifoBufferIstream.cxx',
  'FifoBufferSink.cxx',
  'FileIstream.cxx',
  'MultiFifoBufferIstream.cxx',
  'OpenFileIstream.cxx',
  'PipeLeaseIstream.cxx',
  'ReplaceIstream.cxx',
  'SliceIstream.cxx',
  'TeeIstream.cxx',
  'SuspendIstream.cxx',
  'TimeoutIstream.cxx',
  'AutoPipeIstream.cxx',

  'ToBucketIstream.cxx',
  'FromBucketIstream.cxx',

  'istream_deflate.cxx',
  'istream_iconv.cxx',
  'istream_later.cxx',
  'istream_pause.cxx',

  # IstreamSink implementations
  'sink_fd.cxx',

  istream_sources,

  include_directories: inc,
  dependencies: [
    zlib,
    libyamlcpp,
  ],
)

istream_dep = declare_dependency(
  compile_args: istream_compile_args,
  link_with: istream,
  dependencies: [
    istream_api_dep,
    istream_basic_dep,
    event_dep,
    system_dep,
  ],
)
