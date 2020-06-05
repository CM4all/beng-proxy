sphinx = find_program('sphinx-build', required: get_option('documentation'))
if not sphinx.found()
  subdir_done()
endif

custom_target(
  'HTML documentation',
  output: 'html',
  input: [
    'index.rst',
    'bp.rst',
    'lb.rst',
    'log.rst',
    'control.rst',
    'translation.rst',
    'certdb.rst',
  ],
  command: [sphinx, '-q', '-b', 'html', '-d', '@OUTDIR@/doctrees', meson.current_source_dir(), '@OUTPUT@'],
  build_by_default: true,
  install: true,
  install_dir: join_paths(get_option('datadir'), 'doc', meson.project_name()),
)
