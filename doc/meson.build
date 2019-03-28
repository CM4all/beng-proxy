if get_option('documentation').disabled()
  subdir_done()
endif

custom_target('beng-proxy PDF manual',
  output: 'beng.pdf',
  input: 'beng.tex',
  command: [join_paths(meson.current_source_dir(), 'run-tex.sh'), '@INPUT@', '@OUTDIR@'],
  build_by_default: true,
  install: true,
  install_dir: join_paths(get_option('datadir'), 'doc', meson.project_name()),
)

custom_target('beng-lb PDF manual',
  output: 'lb.pdf',
  input: 'lb.tex',
  command: [join_paths(meson.current_source_dir(), 'run-tex.sh'), '@INPUT@', '@OUTDIR@'],
  build_by_default: true,
  install: true,
  install_dir: join_paths(get_option('datadir'), 'doc', meson.project_name()),
)
