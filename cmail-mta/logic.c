int logic_generate_bounces(LOGGER log, DATABASE* database, MTA_SETTINGS settings){
	int status, rv = 0;
	unsigned bounces = 0;

	logprintf(log, LOG_INFO, "Entering bounce handling logic\n");

	if(sqlite3_bind_int(database->query_bounce_candidates, 1, settings.mail_retries) != SQLITE_OK){
		logprintf(log, LOG_ERROR, "Failed to bind retry amount parameter %d: %s\n", settings.mail_retries, sqlite3_errmsg(database->conn));
		return -1;
	}

	//query all candidates (FIXME collate by mail/sender in order to catch mult-recipient mails. or dont.)
	do{
		status = sqlite3_step(database->query_bounce_candidates);
		switch(status){
			case SQLITE_ROW:
				logprintf(log, LOG_DEBUG, "Bouncing message %d from %s retries %d fatality %d\n", sqlite3_column_int(database->query_bounce_candidates, 0), sqlite3_column_text(database->query_bounce_candidates, 1), sqlite3_column_int(database->query_bounce_candidates, 3), sqlite3_column_int(database->query_bounce_candidates, 4));

				//TODO fetch reasons
				//TODO create bounce message
				//TODO insert into outbox

				//delete original message
				mail_delete(log, database, sqlite3_column_int(database->query_bounce_candidates, 0));

				bounces++;
				//reset condition to continue
				status = SQLITE_ROW;
				break;
			case SQLITE_DONE:
				logprintf(log, LOG_INFO, "Generated %d bounce messages\n", bounces);
				break;
			default:
				logprintf(log, LOG_WARNING, "Unhandled database response in bounce generation: %s\n", sqlite3_errmsg(database->conn));
				rv = -1;
				break;
		}
	}
	while(status==SQLITE_ROW);

	sqlite3_reset(database->query_bounce_candidates);
	sqlite3_clear_bindings(database->query_bounce_candidates);
	
	return (rv<0) ? rv:bounces;
}

int logic_handle_transaction(LOGGER log, DATABASE* database, CONNECTION* conn, MAIL* transaction){
	int delivered_mails=0;
	unsigned i;
	CONNDATA* conn_data=(CONNDATA*)conn->aux_data;

	if(mail_dispatch(log, database, transaction, conn)<0){
		logprintf(log, LOG_WARNING, "Failed to dispatch transaction\n");
		//need to reset transaction here because greylisting may fail the first connection,
		//but we'd still like to try the next
		smtp_rset(log, conn);
		return -1;
	}

	//reset in case any transaction follows
	smtp_rset(log, conn);

	//handle failcount increase
	for(i=0;i<transaction->recipients;i++){
		switch(transaction->rcpt[i].status){
			case RCPT_OK:
				//delivery done, remove from database
				if(mail_delete(log, database, transaction->rcpt[i].dbid)<0){
					logprintf(log, LOG_WARNING, "Failed to delete delivered mail id %d\n", transaction->rcpt[i].dbid);
				}
			
				delivered_mails++;
				break;
			case RCPT_FAIL_TEMPORARY:
			case RCPT_FAIL_PERMANENT:
				//handled by bouncehandler
				break;
			case RCPT_READY:
				logprintf(log, LOG_WARNING, "Recipient %d not touched by dispatch loop\n", i);
				//this happens if the peer rejects the MAIL command
				mail_failure(log, database, transaction->rcpt[i].dbid, conn_data->reply.response_text, false);
				break;
		}
	}


	logprintf(log, LOG_INFO, "Handled %d mails in transaction\n", delivered_mails);
	return delivered_mails;	
}

int logic_handle_remote(LOGGER log, DATABASE* database, MTA_SETTINGS settings, REMOTE remote){
	int status, delivered_mails = -1;
	unsigned current_mx = 0, mx_count = 0, port, i, c;
	sqlite3_stmt* tx_statement = (remote.mode == DELIVER_DOMAIN) ? database->query_domain:database->query_remote;

	CONNDATA conn_data = {
	};

	CONNECTION conn = {
		.aux_data = &conn_data
	};

	connection_reset(log, &conn, false);

	adns_state resolver = NULL;
	adns_answer* resolver_answer = NULL;
	char* resolver_remote = NULL;

	unsigned tx_allocated = 0, tx_active = 0;
	MAIL* mails = NULL;

	if(!remote.host || strlen(remote.host) < 2){
		logprintf(log, LOG_ERROR, "No valid hostname provided\n");
		return -1;
	}

	logprintf(log, LOG_INFO, "Entering mail remote handling for host %s in mode %s\n", remote.host, (remote.mode == DELIVER_DOMAIN) ? "domain":"handoff");

	//prepare mail data query
	if(sqlite3_bind_int(tx_statement, 1, settings.retry_interval) != SQLITE_OK){
		logprintf(log, LOG_ERROR, "Failed to bind timeout parameter\n");
		return -1;
	}

	if(sqlite3_bind_text(tx_statement, 2, remote.host, -1, SQLITE_STATIC) != SQLITE_OK){
		logprintf(log, LOG_ERROR, "Failed to bind host parameter\n");
		return -1;
	}

	//read all mail transactions for this host
	do{
		status = sqlite3_step(tx_statement);
		switch(status){
			case SQLITE_ROW:
				if(tx_active>=tx_allocated){
					//reallocate transaction array
					mails = realloc(mails, (tx_allocated+CMAIL_REALLOC_CHUNK) * sizeof(MAIL));
					if(!mails){
						logprintf(log, LOG_ERROR, "Failed to allocate memory for mail transaction array\n");
						return -1;
					}
					
					//clear freshly allocated mails
					for(c=0;c<CMAIL_REALLOC_CHUNK;c++){
						mail_reset(mails+c, false);
					}
					
					tx_allocated += CMAIL_REALLOC_CHUNK;
				}
				
				//read transaction
				if(mail_dbread(log, mails+tx_active, tx_statement)<0){
					logprintf(log, LOG_ERROR, "Failed to read transaction %s, database status %s\n", (char*)sqlite3_column_text(tx_statement, 0), sqlite3_errmsg(database->conn));
				}

				tx_active++;
				break;
			case SQLITE_DONE:
				logprintf(log, LOG_INFO, "Connection handles %d transactions\n", tx_active);
				break;
			default:
				logprintf(log, LOG_WARNING, "Unhandled response from mail transaction query: %s\n", sqlite3_errmsg(database->conn));
				break;
		}
	}
	while(status == SQLITE_ROW);
	sqlite3_reset(tx_statement);
	sqlite3_clear_bindings(tx_statement);

	//resolve host for MX records
	resolver_remote = remote.host;
	if(remote.mode == DELIVER_DOMAIN){
		status = adns_init(&resolver, adns_if_none, log.stream);
		if(status != 0){
			logprintf(log, LOG_ERROR, "Failed to initialize adns: %s\n", strerror(status));
			return -1;
		}

		status = adns_synchronous(resolver, remote.host, adns_r_mx, adns_qf_cname_loose, &resolver_answer);
		if(status != 0){
			logprintf(log, LOG_ERROR, "Failed to run query: %s\n", strerror(status));
			return -1;
		}

		logprintf(log, LOG_DEBUG, "%d records in DNS response\n", resolver_answer->nrrs);

		if(resolver_answer->nrrs <= 0){
			logprintf(log, LOG_ERROR, "No MX records for domain %s found, falling back to HANDOFF strategy\n", remote.host);

			//free resolver data
			free(resolver_answer);
			adns_finish(resolver);

			remote.mode = DELIVER_HANDOFF;
			//TODO report error type
		}
		else{
			for(c=0;c<resolver_answer->nrrs;c++){
				logprintf(log, LOG_DEBUG, "MX %d: %s\n", c, resolver_answer->rrs.inthostaddr[c].ha.host);
			}

			mx_count = resolver_answer->nrrs;
		}
	}

	//connect to remote
	current_mx = 0;
	do{
		if(remote.mode == DELIVER_DOMAIN){
			resolver_remote = resolver_answer->rrs.inthostaddr[current_mx].ha.host;
		}
		logprintf(log, LOG_INFO, "Trying to connect to MX %d: %s\n", current_mx, resolver_remote);

		for(port=0;settings.port_list[port].port;port++){
			#ifndef CMAIL_NO_TLS
			logprintf(log, LOG_INFO, "Trying port %d TLS mode %s\n", settings.port_list[port].port, tls_modestring(settings.port_list[port].tls_mode));
			#else
			logprintf(log, LOG_INFO, "Trying port %d\n", settings.port_list[port].port);
			#endif

			conn.fd = network_connect(log, resolver_remote, settings.port_list[port].port);
			//TODO only reconnect if port or remote have changed

			if(conn.fd>0){
				//negotiate smtp
				if(smtp_negotiate(log, settings, resolver_remote, &conn, settings.port_list[port])<0){
					logprintf(log, LOG_INFO, "Failed to negotiate required protocol level, trying next\n");
					//FIXME might want to gracefully close smtp here
					connection_reset(log, &conn, true);
					continue;
				}

				//connected, run the delivery loop
				delivered_mails=0;
				for(c=0;c<tx_active;c++){
					status = logic_handle_transaction(log, database, &conn, mails+c);
					if(status < 0){
						logprintf(log, LOG_WARNING, "Mail transaction failed, continuing\n");
					}
					else{
						delivered_mails += status;
					}
				}
					
				logprintf(log, LOG_INFO, "Delivered %d mails in %d transactions for this remote\n", delivered_mails, tx_active);
				
				//FIXME might want to continue if not all mail could be delivered
				current_mx = mx_count; //break the outer loop
				break;
			}
		}

		current_mx++;
	}
	while(current_mx < mx_count);
	logprintf(log, LOG_INFO, "Finished delivery handling of host %s\n", remote.host);


	if(delivered_mails < 0){
		logprintf(log, LOG_WARNING, "Could not reach any MX for %s\n", remote.host);
		for(i=0;i<tx_active;i++){
			for(c=0;c<mails[i].recipients;c++){
				mail_failure(log, database, mails[i].rcpt[c].dbid, "Failed to reach any MX", false);
			}
		}
	}

	if(remote.mode == DELIVER_DOMAIN){
		free(resolver_answer);
		adns_finish(resolver);
	}

	//free mail transactions
	for(c=0;c<tx_active;c++){
		mail_reset(mails+c, true);
	}
	free(mails);

	connection_reset(log, &conn, true);
	logprintf(log, LOG_INFO, "Mail delivery for %s done\n", remote.host);
	return delivered_mails;
}

int logic_loop_hosts(LOGGER log, DATABASE* database, MTA_SETTINGS settings){
	int status;
	unsigned i;
	unsigned mails_delivered = 0;
	int bounces_generated = 0;

	unsigned remotes_allocated = 0;
	unsigned remotes_active = 0;
	REMOTE* remotes = NULL;

	logprintf(log, LOG_INFO, "Entering core loop\n");

	do{
		mails_delivered = 0;
		remotes_active = 0;
	
		//bind the timeout parameter
		if(sqlite3_bind_int(database->query_outbound_hosts, 1, settings.retry_interval) != SQLITE_OK){
			logprintf(log, LOG_ERROR, "Failed to bind timeout parameter\n");
			break;
		}

		//fetch all hosts to be delivered to
		do{
			status=sqlite3_step(database->query_outbound_hosts);
			switch(status){
				case SQLITE_ROW:
					if(remotes_active>=remotes_allocated){
						//reallocate remote array
						remotes=realloc(remotes, (remotes_allocated+CMAIL_REALLOC_CHUNK)*sizeof(REMOTE));
						if(!remotes){
							logprintf(log, LOG_ERROR, "Failed to allocate memory for remote array\n");
							return -1;
						}
						//clear memory
						memset(remotes+remotes_allocated, 0, CMAIL_REALLOC_CHUNK*sizeof(REMOTE));
						remotes_allocated+=CMAIL_REALLOC_CHUNK;
					}

					if(remotes[remotes_active].host){
						free(remotes[remotes_active].host);
					}
					
					remotes[remotes_active].mode=DELIVER_HANDOFF;
					
					if(sqlite3_column_text(database->query_outbound_hosts, 0)){
						remotes[remotes_active].host=common_strdup((char*)sqlite3_column_text(database->query_outbound_hosts, 0));
					}
					else if(sqlite3_column_text(database->query_outbound_hosts, 1)){
						remotes[remotes_active].mode=DELIVER_DOMAIN;
						remotes[remotes_active].host=common_strdup((char*)sqlite3_column_text(database->query_outbound_hosts, 1));
					}

					if(!remotes[remotes_active].host){
						logprintf(log, LOG_ERROR, "Failed to allocate memory for remote host part\n");
						continue;
					}

					remotes_active++;
					break;
				case SQLITE_DONE:
					logprintf(log, LOG_INFO, "Interval contains %d remotes\n", remotes_active);
					break;
				default:
					logprintf(log, LOG_WARNING, "Unhandled outbound host query result: %s\n", sqlite3_errmsg(database->conn));
					break;
			}
		}
		while(status==SQLITE_ROW);
		sqlite3_reset(database->query_outbound_hosts);
		sqlite3_clear_bindings(database->query_outbound_hosts);

		//deliver all remotes
		for(i=0;i<remotes_active;i++){
			logprintf(log, LOG_INFO, "Starting delivery for %s in mode %s\n", remotes[i].host, (remotes[i].mode == DELIVER_DOMAIN) ? "domain":"handoff");
			
			//TODO implement multi-threading here
			status = logic_handle_remote(log, database, settings, remotes[i]);

			if(status<0){
				logprintf(log, LOG_WARNING, "Delivery procedure returned an error\n");
			}
			else{
				mails_delivered += status;
			}
		}
		
		bounces_generated = logic_generate_bounces(log, database, settings);
		if(bounces_generated < 0){
			logprintf(log, LOG_WARNING, "Bounce generation reported an error\n");
			bounces_generated = 0;
		}

		logprintf(log, LOG_INFO, "Core interval done, delivered %d mails, generated %d bounces\n", mails_delivered, bounces_generated);
		
		sleep(settings.check_interval);
	}
	while(!abort_signaled);

	//free allocated remotes
	for(i=0;i<remotes_allocated;i++){
		if(remotes[i].host){
			free(remotes[i].host);
		}
	}
	free(remotes);

	logprintf(log, LOG_INFO, "Core loop exiting cleanly\n");
	return 0;
}
