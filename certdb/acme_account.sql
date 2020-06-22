CREATE TABLE acme_account (
    --------------------------------
    -- Internal PostgreSQL columns
    --------------------------------

    id serial PRIMARY KEY,

    --------------------------------
    -- ACME account attributes
    --------------------------------

    -- Is this a let's encrypt staging account?
    staging boolean NOT NULL,

    -- the "contact/mailto" value
    email varchar(256) NULL,

    -- the "Location" header of the "new-reg" response
    location varchar NULL,

    --------------------------------
    -- The actual RSA key
    --------------------------------

    -- the RSA key in DER format
    key_der bytea NOT NULL,

    --------------------------------
    -- Key encryption
    --------------------------------

    -- a name referring to an AES key stored in local configuration
    key_wrap_name varchar(32) NOT NULL,

    --------------------------------
    -- Runtime data
    --------------------------------

    enabled boolean NOT NULL DEFAULT TRUE,

    -- when was this account was created?
    time_created timestamp NOT NULL DEFAULT now(),

    -- when was this account most recently used?
    time_used timestamp NULL,

    -- when was this account most recently rejected?
    time_rejected timestamp NULL
);

-- for obtaining the least recently used account
CREATE INDEX acme_account_used ON acme_account(time_used) WHERE enabled;

-- for obtaining the least recently used account
CREATE UNIQUE INDEX acme_account_location ON acme_account(location) WHERE enabled;
