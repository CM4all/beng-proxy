# the core Istream API
istream_api = static_library(
  'istream_api',
  'Handler.cxx',
  'Debug.cxx',
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

  'FacadeIstream.cxx',
  'ForwardIstream.cxx',

  # Istream implementations
  'ApproveIstream.cxx',
  'BlockIstream.cxx',
  'ByteIstream.cxx',
  'CatchIstream.cxx',
  'ConcatIstream.cxx',
  'FailIstream.cxx',
  'FourIstream.cxx',
  'HeadIstream.cxx',
  'InjectIstream.cxx',
  'LengthIstream.cxx',
  'MemoryIstream.cxx',
  'OptionalIstream.cxx',
  'RefIstream.cxx',
  'SharedLeaseIstream.cxx',
  'SubstIstream.cxx',
  'ZeroIstream.cxx',
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

if uring_dep.found()
  istream_sources += [
    'UringIstream.cxx',
    'UringSpliceIstream.cxx',
  ]
endif

istream = static_library(
  'istream',

  # Istream implementations
  'ChunkedIstream.cxx',
  'DechunkIstream.cxx',
  'DelayedIstream.cxx',
  'FdIstream.cxx',
  'FifoBufferIstream.cxx',
  'FifoBufferSink.cxx',
  'FileIstream.cxx',
  'MultiFifoBufferIstream.cxx',
  'OpenFileIstream.cxx',
  'ReadyIstream.cxx',
  'ReplaceIstream.cxx',
  'SliceIstream.cxx',
  'SocketPairIstream.cxx',
  'TeeIstream.cxx',
  'SuspendIstream.cxx',
  'HalfSuspendIstream.cxx',
  'TimeoutIstream.cxx',

  'ToBucketIstream.cxx',
  'FromBucketIstream.cxx',

  'istream_iconv.cxx',
  'istream_later.cxx',
  'PauseIstream.cxx',

  # IstreamSink implementations
  'sink_fd.cxx',

  istream_sources,

  include_directories: inc,
  dependencies: [
    fmt_dep,
    net_dep,
    http_util_dep, # for http/ChunkParser
  ],
)

istream_dep = declare_dependency(
  link_with: istream,
  dependencies: [
    istream_api_dep,
    istream_basic_dep,
    event_dep,
  ],
)

# Istream implementations that depend on PipeLease
istream_pipe = static_library(
  'istream_pipe',
  'BufferedIstream.cxx',
  'PipeLeaseIstream.cxx',
  'AutoPipeIstream.cxx',
  include_directories: inc,
)

istream_pipe_dep = declare_dependency(
  link_with: istream_pipe,
  dependencies: [
    istream_basic_dep,
    event_dep,
    pipe_dep,
  ],
)

# Istream implementations with heavy dependencies
istream_extra_sources = []
istream_extra_compile_args = []

libbrotlienc = dependency('libbrotlienc',
                          required: get_option('brotli'))
if libbrotlienc.found()
  istream_extra_compile_args += '-DHAVE_BROTLI'
  istream_extra_sources += 'BrotliEncoderIstream.cxx'
endif

istream_extra = static_library(
  'istream_extra',
  istream_extra_sources,
  'GzipIstream.cxx',
  'ThreadIstream.cxx',
  'SimpleThreadIstreamFilter.cxx',
  include_directories: inc,
  dependencies: [
    fmt_dep,
    zlib,
    libbrotlienc,
  ],
)

istream_extra_dep = declare_dependency(
  compile_args: istream_extra_compile_args,
  link_with: istream_extra,
  dependencies: [
    istream_api_dep,
    event_dep,
  ],
)
