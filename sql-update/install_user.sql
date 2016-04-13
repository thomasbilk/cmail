.bail on
.echo on

BEGIN TRANSACTION;

	-- create tables --
	CREATE TABLE meta (
		[key]	TEXT	PRIMARY KEY
				NOT NULL
				UNIQUE,
		value	TEXT
	);

	CREATE TABLE mailbox (

		mail_id			INTEGER	PRIMARY KEY AUTOINCREMENT
						NOT NULL
						UNIQUE,
		mail_user		TEXT	NOT NULL,
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

	CREATE TABLE mailbox_names (
		mailbox_id		INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL UNIQUE CHECK(mailbox_id >= 0),
		mailbox_user		TEXT,
		mailbox_name		TEXT NOT NULL,
		mailbox_parent		INTEGER REFERENCES mailbox_names (mailbox_id) ON DELETE CASCADE ON UPDATE CASCADE,
		mailbox_uid_validity	INTEGER NOT NULL DEFAULT (strftime( '%s', 'now' )), 
		mailbox_uid_next	INTEGER NOT NULL DEFAULT (1),
		CONSTRAINT mbx_unique_per_user UNIQUE (mailbox_user, mailbox_name, mailbox_parent) ON CONFLICT FAIL
	);

	INSERT INTO mailbox_names (mailbox_id, mailbox_name) VALUES (0, 'INBOX');

	CREATE TABLE mailbox_mapping (
		mail_id			INTEGER NOT NULL REFERENCES mailbox (mail_id) ON DELETE CASCADE ON UPDATE CASCADE,
		mailbox_id		INTEGER NOT NULL REFERENCES mailbox_names (mailbox_id) ON DELETE CASCADE ON UPDATE CASCADE,
		CONSTRAINT map_unique UNIQUE (mail_id, mailbox_id) ON CONFLICT IGNORE
	);

	CREATE TABLE flags (
		mail_id			INTEGER NOT NULL REFERENCES mailbox (mail_id) ON DELETE CASCADE ON UPDATE CASCADE,
		flag			TEXT NOT NULL,
		CONSTRAINT flag_unique_per_mail UNIQUE (mail_id, flag) ON CONFLICT FAIL
	);

	INSERT INTO meta (key, value) VALUES ('schema_version', '11');
COMMIT;
