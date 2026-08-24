net2 = static_library(
  'net2',
  'BanList.cxx',
  'FailureManager.cxx',
  'FailureInfo.cxx',
  'FailureRef.cxx',
  'ClientAccounting.cxx',
  'ListenStreamStock.cxx',
  'ConnectSocketX.cxx',
  'BufferedSocketLease.cxx',
  'ParseBareInetAddress.cxx',
  include_directories: inc,
)

net_dep = declare_dependency(
  link_with: net2,
  dependencies: [
    net_dep,
  ],
)
