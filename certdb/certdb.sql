CREATE TABLE server_certificate (
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
    -- Additional (external) data
    --------------------------------

    -- an identifier string assigned by the administrator to address
    -- this certificate
    handle varchar(256) NULL,

    -- if non-NULL, then this is a "special" certificate which is not
    -- supposed to be used for regular connections
    special varchar(64) NULL,

    --------------------------------
    -- Mirrors of X.509 attributes
    --------------------------------

    common_name varchar(256) NOT NULL,
    issuer_common_name varchar(256) NULL,

    not_before timestamp NOT NULL,
    not_after timestamp NOT NULL,

    --------------------------------
    -- The actual X.509 certificate
    --------------------------------

    -- the X.509 certificate in DER format
    certificate_der bytea NOT NULL,

    -- the RSA key in DER format
    key_der bytea NOT NULL,

    --------------------------------
    -- Key encryption
    --------------------------------

    -- a name referring to an AES key stored in local configuration
    key_wrap_name varchar(32) NULL
);

CREATE TABLE server_certificate_alt_name (
    --------------------------------
    -- Internal PostgreSQL columns
    --------------------------------

    id serial PRIMARY KEY,

    --------------------------------
    -- Relational columns
    --------------------------------

    server_certificate_id integer NULL REFERENCES server_certificate(id) ON DELETE CASCADE,

    --------------------------------
    -- Data
    --------------------------------

    name varchar(256) NOT NULL
);

-- for looking up a certificate by its name
CREATE UNIQUE INDEX server_certificate_name_special ON server_certificate(common_name, special);

-- for looking up a certificate by its handle
CREATE UNIQUE INDEX server_certificate_handle ON server_certificate(handle);

-- for looking up a certificate by its alternative name
CREATE INDEX server_certificate_alt_name_name ON server_certificate_alt_name(name);

-- for getting the latest updates
CREATE INDEX server_certificate_modified ON server_certificate(modified);

-- for finding expired certificates
CREATE INDEX server_certificate_not_after ON server_certificate(not_after);
