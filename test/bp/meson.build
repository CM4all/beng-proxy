if not curl_dep.found()
  subdir_done()
endif

test(
  'TestBengProxy',
  executable(
    'TestBengProxy',
    'TestBengProxy.cxx',
    'StartBengProxy.cxx',
    'TranslationServerThread.cxx',
    include_directories: inc,
    dependencies: [
      fmt_dep,
      io_dep,
      net_dep,
      system_dep,
      system_linux_dep,
      util_dep,
      translation_server_dep,
      curl_dep,
      gtest,
    ],
  ),
  env: ['srcdir=' + meson.current_source_dir()],
  depends: [
    beng_proxy,
  ],
)
