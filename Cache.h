#ifndef LOG_FS_H
#include "log_fs.h"
#endif
#include <stdbool.h>
#include <assert.h>
#define METADATA_CACHE 1
#define WRITE_BACK 4
#define NORMAL 0

#define WRITE_BUFFER 2
#define DEBUG_CACHE 1
/*
1) Cache for storing the write buffer
2) Cache for storing the in-memory meta data
*/

int page_size;
typedef struct cache_block {
	MDATA * mdata;
	bool free_flag;
	char *buf;
	int offset;
	int lru_counter;
} *CBLK ;

typedef struct cache {
	CBLK cblocks;
	int num_blocks;
	int cache_block_size;
	char type;
} *CACHE;


/*
Allocate memory and return CACHE .
type : METADATA_CACHE or WRITE_BUFFER

WRITE_BUFFER:
block_size must be multiple of PAGE_SIZE
size must be multiple of block_size

METADATA_CACHE:
block_size doesnt matter . But something should be passed
size - number of elements
*/
CACHE  create_cache(int size,int block_size,char type); 

/* Write the contents of buf of the cache block corresponding to meta_data with the in_buf */
int  write_cache_block(CACHE c,MDATA *meta_data,char *in_buf,int buf_len  );

/* Find and return a least recently used Cache blocks */
CBLK get_free_cache_block(CACHE c,int *result);

/* Copy the contents of buf of cache block to the out_buf */
int read_cache_block(CACHE c,MDATA *meta_data,char *out_buf,int *buf_len);

/* Update the lru_counter of the Cache c */
int update_lru(CACHE c,CBLK cache_cblk );

/* Find the cache block in the WRITE_BUFFER which corresponds to meta_data */
static CBLK find_cache_block(CACHE c,MDATA *meta_data);

/* For initialising the cache_blk of size block_size */
static int init_cache_block(CBLK cache_blk,int block_size,char type);

/* Find and return metadata cache block which corresponds to the file_name */
CBLK find_meta_data_block(CACHE c,char *file_name);

/* Print the content of the cache block buf */
void print_cache_block(CBLK cblock) ;

/* Print the content of entire cache c */
void print_cache(CACHE c);

/* Parses the date in the time_stamp and create a date folder in the folder_path if not present already  */
int create_date_folder(char *time_stamp,char *folder_path,char *folder_name);

/* Check whether date in timestamp1 and and timestamp2 are equal */
int is_dates_equal(char *timestamp1,char *timestamp2) ;

/* Write the buf of cache block wb_block to the disk */
int write_buffer_to_disk( CBLK wb_block ,char *chunk_path,CACHE buffer_cache);


/* Free the given cache block */

int free_cache_block(CACHE c,CBLK cache_blk);

