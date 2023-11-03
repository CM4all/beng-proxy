openssl = find_program('openssl', required: false)
if not openssl.found()
  subdir_done()
endif

acme_ca_key = custom_target(
  'ca-key.pem',
  output: 'ca-key.pem',
  command: [
    openssl, 'ecparam', '-name', 'prime256v1', '-genkey', '-noout', '-out', '@OUTPUT@',
  ],
  build_by_default: false,
)

acme_ca_cert = custom_target(
  'ca-cert.pem',
  output: 'ca-cert.pem',
  command: [
    openssl, 'req', '-new', '-batch', '-x509',
    '-key', acme_ca_key, '-out', '@OUTPUT@',
  ],
  build_by_default: false,
)

pebble_key = custom_target(
  'pebble-key.pem',
  output: 'pebble-key.pem',
  command: [
    openssl, 'ecparam', '-name', 'prime256v1', '-genkey', '-noout', '-out', '@OUTPUT@',
  ],
  build_by_default: false,
)

pebble_cert = custom_target(
  'pebble-cert.pem',
  output: 'pebble-cert.pem',
  command: [
    openssl, 'req', '-new', '-batch', '-x509',
    '-CA', acme_ca_cert, '-CAkey', acme_ca_key,
    '-key', pebble_key, '-out', '@OUTPUT@',
    '-subj', '/CN=0.0.0.0',
  ],
  build_by_default: false,
)

pebble_conf = configure_file(
  input: 'pebble-config.json',
  output: 'pebble-config.json',
  configuration: {
    'certificate': pebble_cert.full_path(),
    'privateKey': pebble_key.full_path(),
  },
)
