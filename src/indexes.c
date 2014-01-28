
/**
 * open_manifest
 * Opens the manifest file
 *
 * @param md       Pointer to the main MDHIM structure
 * @param flags    Flags to open the file with
 */
int open_manifest(struct mdhim_t *md, struct local_index *bi, int flags) {
	int fd;	
	char path[PATH_MAX];

	sprintf(path, "%s%d_%d", bi->type, bi->id);
	fd = open(path, flags, 00600);
	if (fd < 0) {
		mlog(MDHIM_SERVER_CRIT, "Rank: %d - Error opening manifest file", 
		     md->mdhim_rank);
	}
	
	return fd;
}

/**
 * write_manifest
 * Writes out the manifest file
 *
 * @param md       Pointer to the main MDHIM structure
 */
void write_manifest(struct mdhim_t *md, struct local_index *bi) {
	index_manifest_t manifest;
	int fd;
	int ret;
	struct remote_index *ri;

	//Range server with range server number 1, for the primary index, is in charge of the manifest
	if (bi->type != LOCAL_INDEX && 
	    ((struct remote_index *)bi)->myinfo.rangesrv_num != 1) {	
		return;
	}

	if ((fd = open_manifest(md, bi, O_RDWR | O_CREAT | O_TRUNC)) < 0) {
		mlog(MDHIM_SERVER_CRIT, "Rank: %d - Error opening manifest file", 
		     md->mdhim_rank);
		return;
	}
	
	//Populate the manifest structure
	if (bi->type != LOCAL_INDEX) {
		ri = (struct remote_index *)bi;
		manifest.num_rangesrvs = ri->num_rangesrvs;
		manifest.key_type = ri->key_type;
		manifest.db_type = ri->db_type;
		manifest.rangesrv_factor = ri->rserver_factor;
		manifest.slice_size = ri->max_recs_per_slice;
		manifest.num_nodes = ri->mdhim_comm_size;
	} else {
		manifest.num_rangesrvs = 0;
		manifest.key_type = bi->key_type;
		manifest.db_type = bi->db_type;
		manifest.rangesrv_factor = 0;
		manifest.slice_size = 0;
		manifest.num_nodes = 1;
	}
	
	if ((ret = write(fd, &manifest, sizeof(manifest))) < 0) {
		mlog(MDHIM_SERVER_CRIT, "Rank: %d - Error writing manifest file", 
		     md->mdhim_rank);
	}

	close(fd);
}

/**
 * read_manifest
 * Reads in and validates the manifest file
 *
 * @param md       Pointer to the main MDHIM structure
 * @return MDHIM_SUCCESS or MDHIM_ERROR on error
 */
int read_manifest(struct mdhim_t *md, struct local_index *bi) {
	int fd;
	int ret;
	index_manifest_t manifest;
	struct stat st;
	struct remote_index *ri;

	if ((fd = open_manifest(md, bi, O_RDWR)) < 0) {
		return MDHIM_ERROR;
	}

	if ((ret = read(fd, &manifest, sizeof(manifest))) < 0) {
		mlog(MDHIM_SERVER_CRIT, "Rank: %d - Couldn't read manifest file", 
		     md->mdhim_rank);
		return MDHIM_ERROR;
	}

	ret = MDHIM_SUCCESS;	
	mlog(MDHIM_SERVER_DBG, "Rank: %d - Manifest contents - \nnum_rangesrvs: %d, key_type: %d, " 
	     "db_type: %d, rs_factor: %u, slice_size: %lu, num_nodes: %d", 
	     md->mdhim_rank, manifest.num_rangesrvs, manifest.key_type, manifest.db_type, 
	     manifest.rangesrv_factor, manifest.slice_size, manifest.num_nodes);
	
	//Check that the manifest and the current config match
	if (manifest.key_type != bi->key_type) {
		mlog(MDHIM_SERVER_CRIT, "Rank: %d - The key type in the manifest file" 
		     " doesn't match the current key type", 
		     md->mdhim_rank);
		ret = MDHIM_ERROR;
	}
	if (manifest.db_type != bi->db_type) {
		mlog(MDHIM_SERVER_CRIT, "Rank: %d - The database type in the manifest file" 
		     " doesn't match the current database type", 
		     md->mdhim_rank);
		ret = MDHIM_ERROR;
	}

	if (bi->type != LOCAL_INDEX) {
		ri = (struct remote_index *) bi;
		if (manifest.rangesrv_factor != ri->rserver_factor) {
			mlog(MDHIM_SERVER_CRIT, "Rank: %d - The range server factor in the manifest file" 
			     " doesn't match the current range server factor", 
			     md->mdhim_rank);
			ret = MDHIM_ERROR;
		}
		if (manifest.slice_size != ri->max_recs_per_slice) {
			mlog(MDHIM_SERVER_CRIT, "Rank: %d - The slice size in the manifest file" 
			     " doesn't match the current slice size", 
			     md->mdhim_rank);
			ret = MDHIM_ERROR;
		}
		if (manifest.num_nodes != ri->mdhim_comm_size) {
			mlog(MDHIM_SERVER_CRIT, "Rank: %d - The number of nodes in this MDHIM instance" 
			     " doesn't match the number used previously", 
			     md->mdhim_rank);
			ret = MDHIM_ERROR;
		}
	}
	
	close(fd);
	return ret;
}


/**
 * update_all_stats
 * Adds or updates the given stat to the hash table
 *
 * @param md       pointer to the main MDHIM structure
 * @param key      pointer to the key we are examining
 * @param key_len  the key's length
 * @return MDHIM_SUCCESS or MDHIM_ERROR on error
 */
int update_all_stats(struct mdhim_t *md, struct local_index *bi, void *key, uint32_t key_len) {
	int slice_num;
	void *val1, *val2;
	int float_type = 0;
	struct mdhim_stat *os, *stat;

	//Acquire the lock to update the stats
	if (pthread_rwlock_wrlock(&bi->mdhim_store->mdhim_store_stats_lock) != 0) {
		mlog(MDHIM_CLIENT_CRIT, "Rank: %d - Error acquiring the mdhim_store_stats_lock", 
		     md->mdhim_rank);
		return MDHIM_ERROR;
	}

	if ((float_type = is_float_key(md->key_type)) == 1) {
		val1 = (void *) malloc(sizeof(long double));
		val2 = (void *) malloc(sizeof(long double));
	} else {
		val1 = (void *) malloc(sizeof(uint64_t));
		val2 = (void *) malloc(sizeof(uint64_t));
	}

	if (md->key_type == MDHIM_STRING_KEY) {
		*(long double *)val1 = get_str_num(key, key_len);
		*(long double *)val2 = *(long double *)val1;
	} else if (md->key_type == MDHIM_FLOAT_KEY) {
		*(long double *)val1 = *(float *) key;
		*(long double *)val2 = *(float *) key;
	} else if (md->key_type == MDHIM_DOUBLE_KEY) {
		*(long double *)val1 = *(double *) key;
		*(long double *)val2 = *(double *) key;
	} else if (md->key_type == MDHIM_INT_KEY) {
		*(uint64_t *)val1 = *(uint32_t *) key;
		*(uint64_t *)val2 = *(uint32_t *) key;
	} else if (md->key_type == MDHIM_LONG_INT_KEY) {
		*(uint64_t *)val1 = *(uint64_t *) key;
		*(uint64_t *)val2 = *(uint64_t *) key;
	} else if (md->key_type == MDHIM_BYTE_KEY) {
		*(long double *)val1 = get_byte_num(key, key_len);
		*(long double *)val2 = *(long double *)val1;
	} 

	slice_num = get_slice_num(md, key, key_len);
	HASH_FIND_INT(bi->mdhim_store->mdhim_store_stats, &slice_num, os);

	stat = malloc(sizeof(struct mdhim_stat));
	stat->min = val1;
	stat->max = val2;
	stat->num = 1;
	stat->key = slice_num;

	if (float_type && os) {
		if (*(long double *)os->min > *(long double *)val1) {
			free(os->min);
			stat->min = val1;
		} else {
			free(val1);
			stat->min = os->min;
		}

		if (*(long double *)os->max < *(long double *)val2) {
			free(os->max);
			stat->max = val2;
		} else {
			free(val2);
			stat->max = os->max;
		}
	}
	if (!float_type && os) {
		if (*(uint64_t *)os->min > *(uint64_t *)val1) {
			free(os->min);
			stat->min = val1;
		} else {
			free(val1);
			stat->min = os->min;
		}

		if (*(uint64_t *)os->max < *(uint64_t *)val2) {
			free(os->max);
			stat->max = val2;
		} else {
			free(val2);
			stat->max = os->max;
		}		
	}

	if (!os) {
		HASH_ADD_INT(bi->mdhim_store->mdhim_store_stats, key, stat);    
	} else {	
		stat->num = os->num + 1;
		//Replace the existing stat
		HASH_REPLACE_INT(bi->mdhim_store->mdhim_store_stats, key, stat, os);  
		free(os);
	}

	//Release the remote indexes lock
	pthread_rwlock_unlock(&bi->mdhim_store->mdhim_store_stats_lock);
	return MDHIM_SUCCESS;
}

/**
 * load_stats
 * Loads the statistics from the database
 *
 * @param md  Pointer to the main MDHIM structure
 * @return MDHIM_SUCCESS or MDHIM_ERROR on error
 */
int load_stats(struct mdhim_t *md, struct local_index *bi) {
	void **val;
	int *val_len, *key_len;
	struct mdhim_store_opts_t opts;
	int **slice;
	int *old_slice;
	struct mdhim_stat *stat;
	int float_type = 0;
	void *min, *max;
	int done = 0;

	float_type = is_float_key(bi->key_type);
	slice = malloc(sizeof(int *));
	*slice = NULL;
	key_len = malloc(sizeof(int));
	*key_len = sizeof(int);
	val = malloc(sizeof(struct mdhim_db_stat *));	
	val_len = malloc(sizeof(int));
	set_store_opts(md, &opts, 1);
	old_slice = NULL;
	bi->mdhim_store->mdhim_store_stats = NULL;
	while (!done) {
		//Check the db for the key/value
		*val = NULL;
		*val_len = 0;		
		bi->mdhim_store->get_next(bi->mdhim_store->db_stats, 
						    (void **) slice, key_len, (void **) val, 
						    val_len, &opts);	
		
		//Add the stat to the hash table - the value is 0 if the key was not in the db
		if (!*val || !*val_len) {
			done = 1;
			continue;
		}

		if (old_slice) {
			free(old_slice);
			old_slice = NULL;
		}

		mlog(MDHIM_SERVER_DBG, "Rank: %d - Loaded stat for slice: %d with " 
		     "imin: %lu and imax: %lu, dmin: %Lf, dmax: %Lf, and num: %lu", 
		     md->mdhim_rank, **slice, (*(struct mdhim_db_stat **)val)->imin, 
		     (*(struct mdhim_db_stat **)val)->imax, (*(struct mdhim_db_stat **)val)->dmin, 
		     (*(struct mdhim_db_stat **)val)->dmax, (*(struct mdhim_db_stat **)val)->num);
	
		stat = malloc(sizeof(struct mdhim_stat));
		if (float_type) {
			min = (void *) malloc(sizeof(long double));
			max = (void *) malloc(sizeof(long double));
			*(long double *)min = (*(struct mdhim_db_stat **)val)->dmin;
			*(long double *)max = (*(struct mdhim_db_stat **)val)->dmax;
		} else {
			min = (void *) malloc(sizeof(uint64_t));
			max = (void *) malloc(sizeof(uint64_t));
			*(uint64_t *)min = (*(struct mdhim_db_stat **)val)->imin;
			*(uint64_t *)max = (*(struct mdhim_db_stat **)val)->imax;
		}

		stat->min = min;
		stat->max = max;
		stat->num = (*(struct mdhim_db_stat **)val)->num;
		stat->key = **slice;
		old_slice = *slice;
		HASH_ADD_INT(bi->mdhim_store->mdhim_store_stats, key, stat); 
		free(*val);
	}

	free(val);
	free(val_len);
	free(key_len);
	free(*slice);
	free(slice);
	return MDHIM_SUCCESS;
}

/**
 * write_stats
 * Writes the statistics stored in a hash table to the database
 * This is done on mdhim_close
 *
 * @param md  Pointer to the main MDHIM structure
 * @return MDHIM_SUCCESS or MDHIM_ERROR on error
 */
int write_stats(struct mdhim_t *md, struct local_index *bi) {
	struct mdhim_store_opts_t opts;
	struct mdhim_stat *stat, *tmp;
	struct mdhim_db_stat *dbstat;
	int float_type = 0;

	float_type = is_float_key(md->key_type);
	set_store_opts(md, &opts, 1);

	//Iterate through the stat hash entries
	HASH_ITER(hh, bi->mdhim_store->mdhim_store_stats, stat, tmp) {	
		if (!stat) {
			continue;
		}

		dbstat = malloc(sizeof(struct mdhim_db_stat));
		if (float_type) {
			dbstat->dmax = *(long double *)stat->max;
			dbstat->dmin = *(long double *)stat->min;
			dbstat->imax = 0;
			dbstat->imin = 0;
		} else {
			dbstat->imax = *(uint64_t *)stat->max;
			dbstat->imin = *(uint64_t *)stat->min;
			dbstat->dmax = 0;
			dbstat->dmin = 0;
		}

		dbstat->slice = stat->key;
		dbstat->num = stat->num;
		//Write the key to the database		
		bi->mdhim_store->put(bi->mdhim_store->db_stats, 
					       &dbstat->slice, sizeof(int), dbstat, 
					       sizeof(struct mdhim_db_stat), &opts);	
		//Delete and free hash entry
		HASH_DEL(b->mdhim_store->mdhim_store_stats, stat); 
		free(stat->max);
		free(stat->min);
		free(stat);
		free(dbstat);
	}

	return MDHIM_SUCCESS;
}

/** 
 * open_db_store
 * Opens the database for the given idenx
 *
 * @param md     Pointer to the main MDHIM structure
 * @param rindex Pointer to the index
 * @return the initialized data store or NULL on error
 */

struct mdhim_store_t *open_db_store(struct mdhim_t *md, struct local_index *rindex) {
	char filename[PATH_MAX];
	int flags = MDHIM_CREATE;
	struct mdhim_store_opts_t opts;

	//Database filename is dependent on ranges.  This needs to be configurable and take a prefix
	if (!md->db_opts->db_paths) {
		sprintf(filename, "%s%s-%d-%d", md->db_opts->db_path, md->db_opts->db_name, 
			rindex->id, md->mdhim_rank);
	} else {
		path_num = rangesrv_num/((double) md->num_rangesrvs/(double) md->db_opts->num_paths);
	        path_num = path_num >= md->db_opts->num_paths ? md->db_opts->num_paths - 1 : path_num;
		if (path_num < 0) {
			sprintf(filename, "%s%s-%d-%d", md->db_opts->db_path, md->db_opts->db_name, rindex->id, 
				md->mdhim_rank);
		} else {
			sprintf(filename, "%s%s-%d-%d", md->db_opts->db_paths[path_num], 
				md->db_opts->db_name, rindex->id, md->mdhim_rank);
		}
	}

	//Initialize data store
	rindex->mdhim_store = mdhim_db_init(rindex->db_type);
	if (!rindex->mdhim_store) {
		mlog(MDHIM_SERVER_CRIT, "MDHIM Rank: %d - " 
		     "Error while initializing data store with file: %s",
		     md->mdhim_rank,
		     filename);
		return MDHIM_ERROR;
	}

	//Clear the options
	memset(&opts, 0, sizeof(struct mdhim_store_opts_t));
	//Set the key type
	opts.key_type = rindex->key_type;

	//Open the main database and the stats database
	if ((ret = rindex->mdhim_store->open(&rindex->mdhim_store->db_handle,
						   &rindex->mdhim_store->db_stats,
						   filename, flags, &opts)) != MDHIM_SUCCESS){
		mlog(MDHIM_SERVER_CRIT, "MDHIM Rank: %d - " 
		     "Error while opening database", 
		     md->mdhim_rank);
		return MDHIM_ERROR;
	}
	
	rindex->mdhim_store->db_ptr1 = opts.db_ptr1;
	rindex->mdhim_store->db_ptr2 = opts.db_ptr2;
	rindex->mdhim_store->db_ptr3 = opts.db_ptr3;
	rindex->mdhim_store->db_ptr4 = opts.db_ptr4;
	rindex->mdhim_store->db_ptr5 = opts.db_ptr5;
	rindex->mdhim_store->db_ptr6 = opts.db_ptr6;
	rindex->mdhim_store->db_ptr7 = opts.db_ptr7;
	rindex->mdhim_store->db_ptr8 = opts.db_ptr8;

	//Load the stats from the database
	if ((ret = load_stats(md, rindex)) != MDHIM_SUCCESS) {
		mlog(MDHIM_SERVER_CRIT, "MDHIM Rank: %d - " 
		     "Error while loading stats", 
		     md->mdhim_rank);
	}
}

/**
 * get_num_range_servers
 * Gets the number of range servers for an index
 *
 * @param md       main MDHIM struct
 * @param rindex   pointer to a remote_index struct
 * @return         MDHIM_ERROR on error, otherwise the number of range servers
 */
uint32_t get_num_range_servers(struct mdhim_t *md, struct remote_index *rindex) {
	int size;
	uint32_t num_servers = 0;
	int i = 0;
	int ret;

	if ((ret = MPI_Comm_size(md->mdhim_comm, &size)) != MPI_SUCCESS) {
		mlog(MPI_EMERG, "Rank: %d - Couldn't get the size of the comm in get_num_range_servers", 
		     md->mdhim_rank);
		return MDHIM_ERROR;
	}

	/* Get the number of range servers */
	if (size - 1 < rindex->range_server_factor) {
		//The size of the communicator is less than the RANGE_SERVER_FACTOR
		return 1;
	} 
	
	//Figure out the number of range servers, details on the algorithm are in is_range_server
	for (i = 0; i < size; i++) {
		if (i % rindex->range_server_factor == 0) {
			num_servers++;
		}
	}
      	
	return num_servers;
}

/**
 * create_local_index
 * Creates a local index only visible to the calling rank
 *
 * @param  md  main MDHIM struct
 * @return     MDHIM_ERROR on error, otherwise the index identifier
 */
int create_local_index(struct mdhim_t *md, int db_type, int key_type) {
	struct local_index *li;

	//Create a new local_index to hold our index entry
	li = malloc(sizeof(struct local_index));

	//Initialize the new index struct
	memset(li, 0, sizeof(struct local_index));
	li->id = HASH_COUNT(md->local_indexes);
	li->type = LOCAL_INDEX;
	li->key_type = key_type;
	li->db_type = db_type;

	//Add it to the hash table
	HASH_ADD_INT(md->local_indexes, id, li);
                	
	li->mdhim_store = open_db_store(md, (struct local_index *) li);
	if (!li->mdhim_store) {
		mlog(MDHIM_CLIENT_CRIT, "Rank: %d - Error opening data store for index: %d", 
		     md->mdhim_rank, li->id);
	}

	return li->id;
}

/**
 * create_remote_index
 * Collective call that creates a remote index
 *
 * @param md                 main MDHIM struct
 * @param server_factor      used in calculating the number of range servers
 * @param max_recs_per_slice the number of records per slice
 * @return                   MDHIM_ERROR on error, otherwise the index identifier
 */

int create_remote_index(struct mdhim_t *md, int server_factor, uint64_t max_recs_per_slice, 
			int db_type, int key_type) {
	uint32_t num_rangesrvs;
	int type;
	struct remote_index *ri;
	uint32_t rangesrv_num;
	int ret;
	struct rangesrv_info *rs_table;


	MPI_Barrier(md->mdhim_comm);
	//Acquire the lock to update remote_indexes
	if (pthread_rwlock_wrlock(&md->remote_indexes_lock) != 0) {
		mlog(MDHIM_CLIENT_CRIT, "Rank: %d - Error acquiring the remote_indexes_lock", 
		     md->mdhim_rank);
		return MDHIM_ERROR;
	}		

	//Create a new remote_index to hold our index entry
	ri = malloc(sizeof(struct remote_index));
	if (!ri) {
		goto done;
	}

	//Initialize the new index struct
	memset(ri, 0, sizeof(struct remote_index));
	ri->id = HASH_COUNT(md->remote_indexes);
	ri->range_server_factor = server_factor;
        ri->mdhim_max_recs_per_slice = max_recs_per_slice;
	ri->type = ri->id > 0 ? SECONDARY_INDEX : PRIMARY_INDEX;
	ri->key_type = key_type;
	ri->db_type = db_type;

	//Figure out how many range servers we could have based on the range server factor
	ri->num_rangesrvs = get_num_range_servers(md, ri);		

	//Add it to the hash table
	HASH_ADD_INT(md->remote_indexes, id, ri);                      


	//Get the range servers for this index
	rs_table = get_rangesrvs(md, ri);
	if (!rs_table) {
		mlog(MDHIM_CLIENT_CRIT, "Rank: %d - Couldn't get the range server list", 
		     md->mdhim_rank);
	}

	ri->rangesrvs = rs_table;
	//Test if I'm a range server
	if ((rangesrv_num = is_range_server(md, md->mdhim_rank, ri)) == MDHIM_ERROR) {
		free(ri);
		ri = NULL;
		goto done;
	}

	//I'm not a range server
	if (!rangesrv_num) {
		goto done;
	}	

	//Read in the manifest file if the rangesrv_num is 1 for the primary index
	if (rangesrv_num == 1 && 
	    (ret = read_manifest(md, ri)) != MDHIM_SUCCESS) {
		mlog(MDHIM_SERVER_CRIT, "MDHIM Rank: %d - " 
		     "Fatal error: There was a problem reading or validating the manifest file",
		     md->mdhim_rank);
		MPI_Abort(md->mdhim_comm, 0);
	}        

	//Populate my range server info for this index
	ri->myinfo.rank = md->mdhim_rank;
	ri->myinfo.rangesrv_num = rangesrv_num;
	//Open the data store
	ri->mdhim_store = open_db_store(md, (struct local_index *) ri);

	//Initialize the range server threads if they haven't been already
	if (!md->mdhim_rs) {
		ret = range_server_init(md);
	}
	
done:
	//Release the remote indexes lock
	pthread_rwlock_unlock(&md->remote_indexes_lock);
	if (!ri) {
		return MDHIM_ERROR;
	}

	return ri->id;
}

/**
 * get_rangesrvs
 * Creates a rangesrv_info hash table 
 *
 * @param md      in   main MDHIM struct
 * @return a list of range servers
 */
struct rangesrv_info *get_rangesrvs(struct mdhim_t *md, struct remote_index *rindex ) {
	struct rangesrv_info *rs_table = NULL;
	struct rangesrv_info *rs_entry;
	uint32_t rangesrv_num;

	rs_table = NULL;
	//Iterate through the ranks to determine which ones are range servers
	for (i = 0; i < md->mdhim_comm_size; i++) {
		//Test if the rank is range server for this index
		if ((rangesrv_num = is_range_server(md, md->mdhim_rank, rindex)) == MDHIM_ERROR) {
			return MDHIM_ERROR;		
		}

		if (!rangesrv_num) {
			continue;
		}

		//Set the master range server to be the server with the largest rank
		if (i > rindex->rangesrv_master) {
			rindex->rangesrv_master = i
		}

		rs_entry = malloc(sizeof(struct rangesrv_info));
		rs_entry->rank = i;
		rs_entry->rangesrv_num = rangesrv_num;
		
		//Add it to the hash table
		HASH_ADD_INT(rs_table, rank, rs_entry);                
	}

	return rs_table;
}

/**
 * is_range_server
 * Tests to see if the given rank is a range server for one or more indexes
 *
 * @param md      main MDHIM struct
 * @param rank    rank to find out if it is a range server
 * @return        MDHIM_ERROR on error, 0 on false, 1 or greater to represent the range server number otherwise
 */
uint32_t is_range_server(struct mdhim_t *md, int rank, struct remote_index *rindex) {
	int size;
	int ret;
	uint64_t rangesrv_num = 0;

	if ((ret = MPI_Comm_size(md->mdhim_comm, &size)) != MPI_SUCCESS) {
		mlog(MPI_EMERG, "Rank: %d - Couldn't get the size of the comm in is_range_server", 
		     md->mdhim_rank);
		return MDHIM_ERROR;
	}

	/* Get the range server number, which is just a number from 1 onward
	   It represents the ranges the server serves and is calculated with the RANGE_SERVER_FACTOR
	   
	   The RANGE_SERVER_FACTOR is a number that is divided by the rank such that if the 
	   remainder is zero, then the rank is a rank server

	   For example, if there were 8 ranks and the RANGE_SERVER_FACTOR is 2, 
	   then ranks: 2, 4, 6, 8 are range servers

	   If the size of communicator is less than the RANGE_SERVER_FACTOR, 
	   the last rank is the range server
	*/

	size -= 1;
	if (size < rindex->range_server_factor && rank == size) {
		//The size of the communicator is less than the RANGE_SERVER_FACTOR
		rangesrv_num = 1;
	} else if (size >= rindex->range_server_factor && rank % rindex->range_server_factor == 0) {
		//This is a range server, get the range server's number
		rangesrv_num = rank / range_server_factor;
		rangesrv_num++;
	}
      		
	if (rangesrv_num > rindex->num_rangesrvs) {
		rangesrv_num = 0;
	}

	return rangesrv_num;
}

/**
 * range_server_init_comm
 * Initializes the range server communicator that is used for range server to range 
 * server collectives
 * The stat flush function will use this communicator
 *
 * @param md  Pointer to the main MDHIM structure
 * @return    MDHIM_SUCCESS or MDHIM_ERROR on error
 */
int index_init_comm(struct mdhim_t *md, struct local_index *bi) {
	MPI_Group orig, new_group;
	int *ranks;
	rangesrv_info *rp;
	int i = 0, j = 0;
	int ret, server;
	int comm_size, size;
	MPI_Comm new_comm;
	struct rangesrv_info *rangsrv, *tmp;

	ranks = NULL;
	//Populate the ranks array that will be in our new comm
	if ((ret = im_range_server(md, bi)) == 1) {
		ranks = malloc(sizeof(int) * bi->num_rangesrvs);
		rp = bi->rangesrvs;
		size = 0;
		//Iterate through the stat hash entries
		HASH_ITER(hh, bi->rangesrvs, rangesrv, tmp) {	
			if (!rangesrv) {
				continue;
			}

			ranks[i] = rangesrv->rank;
			size++;			
		}
	} else {
		MPI_Comm_size(md->mdhim_comm, &comm_size);
		ranks = malloc(sizeof(int) * comm_size);
		size = j = 0;
		for (i = 0; i < comm_size; i++) {
			HASH_FIND_INT(bi->rangesrvs, &i, rangesrv);
			if (rangesrv) {
				continue;
			}

			ranks[j] = i;
			j++;
			size++;
		}
	}

	//Create a new group with the range servers only
	if ((ret = MPI_Comm_group(md->mdhim_comm, &orig)) != MPI_SUCCESS) {
		mlog(MDHIM_SERVER_CRIT, "MDHIM Rank: %d - " 
		     "Error while creating a new group in range_server_init_comm", 
		     md->mdhim_rank);
		return MDHIM_ERROR;
	}

	if ((ret = MPI_Group_incl(orig, size, ranks, &new_group)) != MPI_SUCCESS) {
		mlog(MDHIM_SERVER_CRIT, "MDHIM Rank: %d - " 
		     "Error while creating adding ranks to the new group in range_server_init_comm", 
		     md->mdhim_rank);
		return MDHIM_ERROR;
	}

	if ((ret = MPI_Comm_create(md->mdhim_comm, new_group, &new_comm)) 
	    != MPI_SUCCESS) {
		mlog(MDHIM_SERVER_CRIT, "MDHIM Rank: %d - " 
		     "Error while creating the new communicator in range_server_init_comm", 
		     md->mdhim_rank);
		return MDHIM_ERROR;
	}
	if ((ret = im_range_server(md)) == 1) {
		memcpy(&bi->rs_comm, &new_comm, sizeof(MPI_Comm));
	}

	free(ranks);
	return MDHIM_SUCCESS;
}
