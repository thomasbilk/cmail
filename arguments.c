bool arguments_parse(ARGUMENTS* args, int argc, char** argv){
	unsigned i;

	for(i=0;i<argc;i++){
		if(!strcmp(argv[i], "nodrop")){
			args->drop_privileges=false;
		}
		else if(!strcmp(argv[i], "nodetach")){
			args->detach=false;
		}
		else{
			args->config_file=argv[i];
		}
	}

	return args->config_file!=NULL;
}

void arguments_free(ARGUMENTS* args){
	if(args->config_file){
		//currently not needed.
		//free(args->config_file);
	}
}
