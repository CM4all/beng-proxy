widget_class = static_library(
  'widget_class',
  'Cache.cxx',
  'Class.cxx',
  'View.cxx',
  'VList.cxx',
  include_directories: inc,
  dependencies: [
    memory_dep,
  ],
)

widget_class_dep = declare_dependency(
  link_with: widget_class,
  dependencies: [
    putil_dep,
    raddress_dep,
    translation_dep,
  ],
)

widget = static_library(
  'widget',
  'Widget.cxx',
  'Init.cxx',
  'Root.cxx',
  'Ptr.cxx',
  'Context.cxx',
  'ForwardHeaders.cxx',
  'CSession.cxx',
  'Untrusted.cxx',
  'Ref.cxx',
  'Session.cxx',
  'Uri.cxx',
  'Approval.cxx',
  'Error.cxx',
  '../uri/Args.cxx',
  include_directories: inc,
  dependencies: [
    widget_class_dep,
    fmt_dep,
  ],
)

widget_dep = declare_dependency(
  link_with: widget,
  dependencies: [
    putil_dep,
  ],
)
