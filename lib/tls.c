/* This file is part of the cmail project (http://cmail.rocks/)
 * (c) 2015 Fabian "cbdev" Stumpf
 * License: Simplified BSD (2-Clause)
 * For further information, consult LICENSE.txt
 */

#ifndef CMAIL_NO_TLS

char* tls_modestring(TLSMODE mode){
	switch(mode){
		case TLS_NONE:
			return "TLS_NONE";
		case TLS_NEGOTIATE:
			return "TLS_NEGOTIATE";
		case TLS_ONLY:
			return "TLS_ONLY";
	}
	return "Unknown";
}

int tls_handshake(CONNECTION* conn){
	int status;

	do{
		status = gnutls_handshake(conn->tls_session);
		if(status){
			if(gnutls_error_is_fatal(status)){
				logprintf(LOG_WARNING, "Handshake failed: %s\n", gnutls_strerror(status));
				return -1;
			}
			logprintf(LOG_WARNING, "Handshake nonfatal: %s\n", gnutls_strerror(status));
		}
	}
	while(status && !gnutls_error_is_fatal(status));
	logprintf(LOG_INFO, "TLS Handshake succeeded\n");

	conn->tls_mode = TLS_ONLY;
	return 0;
}

int tls_init_clientpeer(CONNECTION* conn, char* remote, gnutls_certificate_credentials_t credentials){
	int status;

	status = gnutls_init(&(conn->tls_session), GNUTLS_CLIENT);
	if(status){
		logprintf(LOG_WARNING, "Failed to initialize TLS session for client: %s\n", gnutls_strerror(status));
		return -1;
	}

	status = gnutls_server_name_set(conn->tls_session, GNUTLS_NAME_DNS, remote, strlen(remote));
	if(status){
		logprintf(LOG_WARNING, "Failed to update TLS server name: %s\n", gnutls_strerror(status));
		return -1;
	}

	status = gnutls_credentials_set(conn->tls_session, GNUTLS_CRD_CERTIFICATE, credentials);
	if(status){
		logprintf(LOG_WARNING, "Failed to update TLS credentials: %s\n", gnutls_strerror(status));
		return -1;
	}

	status = gnutls_set_default_priority(conn->tls_session); //FIXME this should probably be configurable
	if(status){
		logprintf(LOG_WARNING, "Failed to update client TLS priorities: %s\n", gnutls_strerror(status));
		return -1;
	}

	gnutls_handshake_set_timeout(conn->tls_session, GNUTLS_DEFAULT_HANDSHAKE_TIMEOUT);
	gnutls_transport_set_int(conn->tls_session, conn->fd);
	gnutls_session_set_ptr(conn->tls_session, (void*)remote);

	return 0;
}

int tls_init_serverpeer(CONNECTION* client, gnutls_priority_t tls_priorities, gnutls_certificate_credentials_t tls_cert){
	int status;

	status = gnutls_init(&(client->tls_session), GNUTLS_SERVER);
	if(status){
		logprintf(LOG_WARNING, "Failed to initialize TLS session for client: %s\n", gnutls_strerror(status));
		return -1;
	}

	status = gnutls_priority_set(client->tls_session, tls_priorities);
	if(status){
		logprintf(LOG_WARNING, "Failed to update priority set for client: %s\n", gnutls_strerror(status));
	}

	status = gnutls_credentials_set(client->tls_session, GNUTLS_CRD_CERTIFICATE, tls_cert);
	if(status){
		logprintf(LOG_WARNING, "Failed to set credentials for client: %s\n", gnutls_strerror(status));
		return -1;
	}

	gnutls_certificate_server_set_request(client->tls_session, GNUTLS_CERT_IGNORE);
	gnutls_transport_set_int(client->tls_session, client->fd);

	return 0;
}

#ifdef CMAIL_HAVE_LISTENER_TYPE
int tls_init_listener(LISTENER* listener, char* cert, char* key, char* dh_params_file, char* priorities){
		ssize_t file_size = 0;
		uint8_t* file_buffer = NULL;

		logprintf(LOG_DEBUG, "Initializing TLS priorities\n");
		if(gnutls_priority_init(&(listener->tls_priorities), (priorities) ? priorities:"NORMAL", NULL)){
			logprintf(LOG_ERROR, "Failed to initialize TLS priorities\n");
			return -1;
		}

		logprintf(LOG_DEBUG, "Initializing TLS certificate structure\n");
		if(gnutls_certificate_allocate_credentials(&(listener->tls_cert))){
			logprintf(LOG_ERROR, "Failed to allocate storage for TLS cert structure\n");
			return -1;
		}

		logprintf(LOG_DEBUG, "Initializing TLS certificate\n");
		if(gnutls_certificate_set_x509_key_file(listener->tls_cert, cert, key, GNUTLS_X509_FMT_PEM)){
			logprintf(LOG_ERROR, "Failed to find key or certificate files\n");
			return -1;
		}

		logprintf(LOG_DEBUG, "Initializing DH parameter structure\n");
		if(gnutls_dh_params_init(&(listener->tls_dhparams))){
			logprintf(LOG_ERROR, "Failed to initialize Diffie-Hellman parameters\n");
			return -1;
		}

		if(dh_params_file){
			//read dhparams from file
			logprintf(LOG_DEBUG, "Reading Diffie-Hellman parameters from %s\n", dh_params_file);
			file_size = common_read_file(dh_params_file, &file_buffer);
			gnutls_datum_t dh_param_data = {
				.data = file_buffer,
				.size = file_size
			};

			if(file_size < 0 || !file_buffer){
				logprintf(LOG_ERROR, "Failed to read Diffie-Hellman parameters from file\n");
				return -1;
			}

			if(gnutls_dh_params_import_pkcs3(listener->tls_dhparams, &dh_param_data, GNUTLS_X509_FMT_PEM)){
				logprintf(LOG_ERROR, "Failed to import Diffie-Hellman parameters, check file format (should be PEM)\n");
				return -1;
			}

			free(file_buffer);
		}
		else{
			//generate new diffie-hellman parameters (kind of slow)
			logprintf(LOG_DEBUG, "Generating Diffie-Hellman parameters\n");
			if(gnutls_dh_params_generate2(listener->tls_dhparams, gnutls_sec_param_to_pk_bits(GNUTLS_PK_DH, TLS_PARAM_STRENGTH))){
				logprintf(LOG_ERROR, "Failed to generate Diffie-Hellman parameters\n");
				return -1;
			}
		}

		gnutls_certificate_set_dh_params(listener->tls_cert, listener->tls_dhparams);
		return 0;
}
//CMAIL_LISTENER_TYPE
#endif
//CMAIL_NO_TLS
#endif
