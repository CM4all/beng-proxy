test(
  't_rewrite_uri',
  executable(
    't_rewrite_uri',
    't_rewrite_uri.cxx',
    '../../src/PInstance.cxx',
    '../../src/widget/RewriteUri.cxx',
    '../../src/bp/session/Lease.cxx',
    '../../src/escape/Istream.cxx',
    include_directories: inc,
    dependencies: [
      gtest,
      putil_dep,
      widget_dep,
      istream_dep,
      translation_dep,
      debug_resource_loader_dep,
    ],
  ),
)

test(
  't_widget_registry',
  executable(
    't_widget_registry',
    't_widget_registry.cxx',
    '../../src/PInstance.cxx',
    '../../src/widget/Registry.cxx',
    include_directories: inc,
    dependencies: [
      gtest,
      widget_class_dep,
    ],
  ),
)

test(
  't_widget_resolver',
  executable(
    't_widget_resolver',
    't_widget_resolver.cxx',
    '../../src/widget/Resolver.cxx',
    '../../src/widget/Init.cxx',
    '../../src/widget/Root.cxx',
    '../../src/widget/Ptr.cxx',
    include_directories: inc,
    dependencies: [
      gtest,
      putil_dep,
      istream_dep,
    ],
  ),
)

test(
  't_wembed',
  executable(
    't_wembed',
    't_wembed.cxx',
    '../../src/widget/Inline.cxx',
    '../../src/escape/Istream.cxx',
    '../../src/bp/Global.cxx',
    include_directories: inc,
    dependencies: [
      test_instance_dep,
      fmt_dep,
      widget_dep,
      istream_dep,
      http_util_dep,
      debug_resource_loader_dep,
    ],
  ),
)

test(
  't_widget_http',
  executable(
    't_widget_http',
    't_widget_http.cxx',
    '../../src/widget/Request.cxx',
    '../../src/widget/FromSession.cxx',
    '../../src/widget/FromRequest.cxx',
    '../../src/bp/ForwardHeaders.cxx',
    '../../src/resource_tag.cxx',
    '../../src/http/ResponseHandler.cxx',
    include_directories: inc,
    dependencies: [
      gtest,
      test_instance_dep,
      fmt_dep,
      widget_dep,
      istream_dep,
      http_util_dep,
      session_dep,
      processor_dep,
      time_dep,
      http_dep,
    ],
  ),
)
