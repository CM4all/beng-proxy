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
  'SubstIstream.cxx',
  'ZeroIstream.cxx',
  'istream_catch.cxx',
  'istream_hold.cxx',
  'istream_memory.cxx',
  'istream_notify.cxx',
  'istream_null.cxx',
  'istream_string.cxx',
  'istream_trace.cxx',

  # IstreamSink implementations
  'StringSink.cxx',
  'sink_null.cxx',
  'sink_buffer.cxx',
  'sink_header.cxx',

  include_directories: inc,
)

istream_basic_dep = declare_dependency(
  link_with: istream_basic,
  dependencies: [
    istream_api_dep,
  ],
)

istream = static_library(
  'istream',
  'TimeoutIstream.cxx',
  'istream_pause.cxx',
  'SliceIstream.cxx',
  'FileIstream.cxx',
  'PipeLeaseIstream.cxx',
  'ChunkedIstream.cxx',
  'DechunkIstream.cxx',
  'AutoPipeIstream.cxx',
  'DelayedIstream.cxx',
  'istream_deflate.cxx',
  'YamlSubstIstream.cxx',
  'BufferedIstream.cxx',
  'istream_iconv.cxx',
  'istream_later.cxx',
  'TeeIstream.cxx',
  'ReplaceIstream.cxx',
  'sink_fd.cxx',
  'ToBucketIstream.cxx',
  'FromBucketIstream.cxx',
  include_directories: inc,
  dependencies: [
    zlib,
    libyamlcpp,
  ],
)

istream_dep = declare_dependency(
  link_with: istream,
  dependencies: [
    istream_api_dep,
    istream_basic_dep,
    event_dep,
    system_dep,
  ],
)
