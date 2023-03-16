session = static_library(
  'session',
  'Session.cxx',
  'Lease.cxx',
  'Id.cxx',
  'Manager.cxx',
  'Write.cxx',
  'Read.cxx',
  'Save.cxx',
  include_directories: inc,
  dependencies: [
    cookie_dep,
    event_dep,
    io_dep,
    system_dep,
  ],
)

session_dep = declare_dependency(
  link_with: session,
  dependencies: [
    event_dep,
    cookie_dep,
    raddress_dep,
  ],
)
