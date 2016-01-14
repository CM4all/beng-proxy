CREATE TABLE server_certificates (
    --------------------------------
    -- Internal PostgreSQL columns
    --------------------------------

    id serial PRIMARY KEY,

    --------------------------------
    -- Metadata
    --------------------------------

    -- Was this certificate deleted?  Deleted certificate records must
    -- not be deleted physically (using SQL DELETE) until all clients
    -- have removed them from their local caches.
    deleted boolean NOT NULL DEFAULT FALSE,

    -- this column must be updated whenever the record is modified
    modified timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,

    --------------------------------
    -- Mirrors of X.509 attributes
    --------------------------------

    common_name varchar(256) NOT NULL,

    alt_names varchar(256)[],

    not_before timestamp NOT NULL,
    not_after timestamp NOT NULL,

    --------------------------------
    -- The actual X.509 certificate
    --------------------------------

    -- the X.509 certificate in DER format
    certificate_der bytea NOT NULL,

    -- the RSA key in DER format
    key_der bytea NOT NULL
);

-- for looking up a certificate by its name
CREATE UNIQUE INDEX server_certificates_name ON server_certificates(common_name);

-- for looking up a certificate by its alternative name
CREATE INDEX server_certificates_alt_name ON server_certificates USING gin(alt_names) WHERE alt_names IS NOT NULL;

-- for getting the latest updates
CREATE INDEX server_certificates_modified ON server_certificates(modified);

-- for finding expired certificates
CREATE INDEX server_certificates_not_after ON server_certificates(not_after);
