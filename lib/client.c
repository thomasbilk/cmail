ssize_t client_send_raw(LOGGER log, CONNECTION* client, char* data, ssize_t bytes){
	ssize_t bytes_sent=0, bytes_written;
	CLIENT* client_data=(CLIENT*)client->aux_data;

	//early bail saves some syscalls
	if(bytes==0){
		return 0;
	}

	if(bytes<0){
		bytes=strlen(data);
	}

	logprintf(log, LOG_DEBUG, "Sending %d raw bytes\n", bytes);
	
	do{
		#ifndef CMAIL_NO_TLS
		switch(client_data->tls_mode){
			case TLS_NONE:
				bytes_written=send(client->fd, data+bytes_sent, bytes-bytes_sent, 0);
				break;
			case TLS_NEGOTIATE:
				logprintf(log, LOG_WARNING, "Not sending data while negotiation is in progess\n");
				break;
			case TLS_ONLY:
				bytes_written=gnutls_record_send(client_data->tls_session, data+bytes_sent, bytes-bytes_sent);
				break;
		}
		#else
		bytes_written=send(client->fd, data+bytes_sent, bytes-bytes_sent, 0);
		#endif

		if(bytes_written<bytes){
			logprintf(log, LOG_DEBUG, "Partial write (%d for %d/%d)\n", bytes_written, bytes_sent, bytes);
		}

		if(bytes_written<0){
			logprintf(log, LOG_ERROR, "Write failed: %s\n", strerror(errno));
			break;
		}

		bytes_sent+=bytes_written;
	}
	while(bytes_sent<bytes);
	
	logprintf(log, LOG_ALL_IO, "<< %.*s", bytes, data);
	logprintf(log, LOG_DEBUG, "Sent %d bytes of %d\n", bytes_sent, bytes);

	return bytes_sent;
}

int client_send(LOGGER log, CONNECTION* client, char* fmt, ...){
	va_list args, copy;
	ssize_t bytes=0;
	char static_send_buffer[STATIC_SEND_BUFFER_LENGTH+1];
	char* dynamic_send_buffer=NULL;
	char* send_buffer=static_send_buffer;

	va_start(args, fmt);
	va_copy(copy, args);
	//check if the buffer was long enough, if not, allocate a new one
	bytes=vsnprintf(send_buffer, STATIC_SEND_BUFFER_LENGTH, fmt, args);
	
	if(bytes>=STATIC_SEND_BUFFER_LENGTH){
		dynamic_send_buffer=calloc(bytes+2, sizeof(char));
		if(!dynamic_send_buffer){
			logprintf(log, LOG_ERROR, "Failed to allocate dynamic send buffer\n");
			return -1;
		}
		send_buffer=dynamic_send_buffer;
		bytes=vsnprintf(send_buffer, bytes+1, fmt, copy);
	}

	if(bytes<0){
		logprintf(log, LOG_ERROR, "Failed to render client output data string\n");
		return -1;
	}

	bytes=client_send_raw(log, client, send_buffer, bytes);

	if(dynamic_send_buffer){
		free(dynamic_send_buffer);
	}

	va_end(args);
	va_end(copy);
	return bytes;
}
