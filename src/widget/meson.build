widget = static_library(
  'widget',
  'Widget.cxx',
  'Init.cxx',
  'Root.cxx',
  'Ptr.cxx',
  'Cache.cxx',
  'Class.cxx',
  'Context.cxx',
  'ForwardHeaders.cxx',
  'CSession.cxx',
  'Untrusted.cxx',
  'View.cxx',
  'Ref.cxx',
  'Session.cxx',
  'Uri.cxx',
  'Approval.cxx',
  'Error.cxx',
  '../uri/Args.cxx',
  include_directories: inc,
  dependencies: [
    fmt_dep,
  ],
)

widget_dep = declare_dependency(
  link_with: widget,
  dependencies: [
    putil_dep,
    raddress_dep,
    translation_dep,
  ],
)
