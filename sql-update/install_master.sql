.bail on
.echo on

BEGIN TRANSACTION;

	-- create tables --
	CREATE TABLE users (
		user_name	TEXT NOT NULL
				UNIQUE,
		user_authdata	TEXT,
		user_database	TEXT
	);

	CREATE TABLE addresses (
		address_expression	TEXT	NOT NULL
						UNIQUE,
		address_order		INTEGER	PRIMARY KEY AUTOINCREMENT
						NOT NULL
						UNIQUE,
		address_user		TEXT	NOT NULL
						REFERENCES users ( user_name )	ON DELETE CASCADE
										ON UPDATE CASCADE
	);

	CREATE TABLE mailbox (

		mail_id			INTEGER	PRIMARY KEY AUTOINCREMENT
						NOT NULL
						UNIQUE,
		mail_user		TEXT	NOT NULL
						REFERENCES users ( user_name )	ON DELETE CASCADE
										ON UPDATE CASCADE,
		mail_read		BOOLEAN	NOT NULL
						DEFAULT ( 0 ),
		mail_ident		TEXT,
		mail_envelopeto		TEXT	NOT NULL,
		mail_envelopefrom	TEXT	NOT NULL,
		mail_submission		TEXT	NOT NULL
						DEFAULT ( strftime( '%s', 'now' ) ),
		mail_submitter		TEXT,
		mail_proto		TEXT	NOT NULL
						DEFAULT ( 'smtp' ),
		mail_data		BLOB
	);

	CREATE TABLE meta (
		[key]	TEXT	PRIMARY KEY
				NOT NULL
				UNIQUE,
		value	TEXT
	);

	CREATE TABLE msa (
		msa_user		TEXT	PRIMARY KEY
						NOT NULL
						REFERENCES users ( user_name )	ON DELETE CASCADE
										ON UPDATE CASCADE,
		msa_inrouter		TEXT	NOT NULL
						DEFAULT ( 'store' ),
		msa_inroute		TEXT,
		msa_outrouter		TEXT	NOT NULL
						DEFAULT ( 'drop' ),
		msa_outroute		TEXT
	);

	CREATE TABLE outbox (
		mail_id			INTEGER PRIMARY KEY AUTOINCREMENT
						NOT NULL
						UNIQUE,
		mail_remote		TEXT,
		mail_envelopefrom	TEXT,
		mail_envelopeto		TEXT	NOT NULL,
		mail_submission		INTEGER	NOT NULL
						DEFAULT ( strftime ( '%s', 'now' ) ),
		mail_submitter		TEXT,
		mail_attempts		INTEGER	NOT NULL
						DEFAULT ( 0 ),
		mail_lastattempt	INTEGER,
		mail_data		TEXT
	);

	CREATE TABLE popd (
		pop_user 		TEXT	PRIMARY KEY
						NOT NULL
						UNIQUE
						REFERENCES users ( user_name )	ON DELETE CASCADE
										ON UPDATE CASCADE,
		pop_lock		BOOLEAN NOT NULL
						DEFAULT ( 0 )
	);

	INSERT INTO meta (key, value) VALUES ('schema_version', '4');

COMMIT;