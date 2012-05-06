#include "Cache.h"
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
//#include <unistd.h>
#include <fcntl.h>
#include <string.h>

CACHE  create_cache(int size,int block_size,char type) {
	
	page_size = sysconf(_SC_PAGESIZE);

	int numBlocks;

	switch(type) {
		case METADATA_CACHE:
			numBlocks = size;
			break;
		case WRITE_BUFFER:
			if(block_size%page_size != 0 ) {
				fprintf(stderr,"ERROR:block_size must be multiple of page_size: %d\n",page_size);
				exit(2);
			}

			if( size%block_size != 0 ) {
				fprintf(stderr,"ERROR:size must be multiple of block_size: %d\n",block_size);
				exit(3);
			}

			numBlocks = size/block_size;
			

			break;
		default:

			break;
	
	}
	
	
	CACHE newCache;

	newCache = malloc(sizeof(struct cache));
	assert(newCache!=NULL);

	newCache->type = type;
	newCache->num_blocks = numBlocks;
	newCache->cache_block_size = block_size;

	newCache->cblocks = malloc(sizeof(struct cache_block) * numBlocks );
	assert(newCache->cblocks!=NULL);
	int i=0;
	for(i=0;i<numBlocks;i++) {
		init_cache_block((newCache->cblocks)+i,block_size,type);	
	}	

	
	return newCache;
	
}

static int init_cache_block(CBLK cache_blk,int block_size,char type) {
	
	switch(type) {
		case METADATA_CACHE:
			cache_blk->mdata = malloc(sizeof(MDATA));
			assert(cache_blk->mdata != NULL);
			break;
		case WRITE_BUFFER:
			cache_blk->mdata= NULL;
			int res = posix_memalign((void **)&(cache_blk->buf),page_size,block_size);
			assert(res == 0);
			break;
		default:
			fprintf(stderr,"ERROR:Invalide cache type .Should be METADATA_CACHE or WRITE_BUFFER\n");
			break;
	}

	cache_blk->free_flag = true;
	cache_blk->offset  = 0;
	cache_blk->lru_counter = 0;
		

}

int  write_cache_block(CACHE c,MDATA *meta_data,char *in_buf,int buf_len  ) {
	CBLK cblk = find_cache_block(c,meta_data);
	if(cblk->offset+buf_len >= c->cache_block_size) {
		return -1; // write the cache block to disk
	} 

	memcpy(cblk->buf + cblk->offset , in_buf,buf_len);
	cblk->offset += buf_len;
	update_lru(c,cblk);
	return 0; 
}

CBLK get_free_cache_block(CACHE c , int *result) {

	int max=-1;
	int index=-1;
	int i=0;
	for(i=0;i<c->num_blocks;i++) {
		if ( c->cblocks[i].free_flag == true) {
			index = i;
			break;
		}

		if( max < c->cblocks[i].lru_counter) {
			max = c->cblocks[i].lru_counter;
			index = i;
		}
	}

/*
	When no free blocks are present
*/
	if(c->cblocks[index].free_flag == false ) {
		*result = WRITE_BACK;
	} else {
		*result = NORMAL;
	}

	c->cblocks[index].free_flag = false; // allocated
#ifdef DEBUG_CACHE
	printf("Returning block : %d\n",index);
#endif
	return c->cblocks+index; // if result == WRITE_BACK , write to disk then use the cache_block
	
}


int read_cache_block(CACHE c,MDATA *meta_data,char *out_buf,int *buf_len ) {
	CBLK cblk = find_cache_block(c,meta_data);
	
	if(cblk == NULL) {
		return -1; // cache block not present in memory
	}

	strcpy(out_buf, cblk->buf);
#ifdef DEBUG_CACHE
	printf("read_cache_block() cache read:%s\n",cblk->buf);
#endif
	*buf_len = cblk->offset;
	update_lru(c,cblk);
	return 0;
	

}

CBLK find_meta_data_block(CACHE c,char *file_name) {
//	if(c->type == METADATA_CACHE) {
	int i=0;
	for(i=0;i<c->num_blocks;i++) {
		if(c->cblocks[i].free_flag == false && c->cblocks[i].mdata!= NULL && strcmp(c->cblocks[i].mdata->file_name,file_name) ==0 ) {
			return c->cblocks+i;
		}
	}

	//Searching for hard link
	int j=0;
	for(i=0;i<c->num_blocks;i++) {
		if(c->cblocks[i].free_flag == false && c->cblocks[i].mdata!= NULL ) {
			for(j=0;j < c->cblocks[i].mdata->num_links;j++) 
				if(strcmp(c->cblocks[i].mdata->link_names[j],file_name)==0 )
					return c->cblocks+i;
		}
	}
/*	} else {
		fprintf(stderr,"ERROR:Wrong type of cache\n");
		return NULL;
	}
*/
	return NULL; // if NULL allocate new block

}

int update_lru(CACHE c,CBLK cache_blk ) {
	int i=0;

	for(i=0;i<c->num_blocks;i++) {
		if( c->cblocks+i != cache_blk && c->cblocks[i].lru_counter <= cache_blk->lru_counter) {
//			printf("<<<< %d : %d >>>>\n",c->cblocks[i].lru_counter , cache_blk->lru_counter);	
			c->cblocks[i].lru_counter = (c->cblocks[i].lru_counter+1)%c->num_blocks;
		}
	}

	cache_blk->lru_counter = 0; 
}

/*
int update_cache_block(CACHE c,MDATA *meta_data,char *add_buf,int buf_len) {

}
*/
static CBLK find_cache_block(CACHE c,MDATA *meta_data) {
	int i=0;
	for(i=0;i<c->num_blocks;i++) {
		if(c->cblocks[i].mdata == meta_data) {
			return c->cblocks+i;
		}
	}

	return NULL;

}

/*
static int evict_cache_block(CACHE c,CBLK cache_blk) {

}
*/
void print_cache_block(CBLK cblock) {

	printf("========= CACHE BLOCK CONTENTS =============\n");	
	if(cblock->mdata != NULL) {
		printf("File Name: %s\n",cblock->mdata->file_name);
		printf("Number of paths: %d\n",cblock->mdata->num_paths);
		printf("Size: %d\n",cblock->mdata->size);
		int i=0;
		for(i=0;i < cblock->mdata->num_paths;i++) {
			printf("%s\n",cblock->mdata->path[i]);
		} 
	
	} else {
		fprintf(stderr,"ERROR:mdata is NULL\n");
	}

	printf("Free flag: %d\n",cblock->free_flag);
	printf("lru_counter:%d\n",cblock->lru_counter);
	if( cblock->buf!=NULL ) {
		printf("Offset : %d\n",cblock->offset);
		printf("Buffer content:%s\n",cblock->buf);
	}
	printf("=============================================\n");
}

void print_cache(CACHE c) {
	int i=0;
	for(i=0;i<c->num_blocks;i++) {
		if(c->cblocks[i].free_flag == false) {
			printf("Cache Block:%d\n",i);
			print_cache_block(c->cblocks+i);
			printf("===============\n");
		}
	}

}

int create_date_folder(char *time_stamp,char *folder_path,char *folder_name) {
	char month[15];
	char date[3];
	char time[12];
	char year[5];
	sscanf(time_stamp,"%*[^ ] %[^ ] %[^ ] %[^ ] %*[^ ] %s",month,date,time,year);
	printf("month : %s date : %s time: %s year: %s\n",month,date,time,year);

	char tmp[50];
	char path[MAX_PATH_NAME_SIZE];
	sprintf(tmp,"%s-%s-%s",month,date,year);	

	strcpy(folder_name,tmp);

	strcpy(path,folder_path);
	strcat(path,tmp);

	struct stat statbuf;
//	int isDir = 0;
#ifdef DEBUG_CACHE
	printf("Folder path: %s\n",path);
#endif
	if (stat(path, &statbuf) == -1) {
		mkdir(path,0777);
		return 0;	
	} else {
		printf("Folder already present\n");
		return 1;
	}

}

int is_dates_equal(char *timestamp1,char *timestamp2) {
	char month1[20];
	int date1;
	int year1;
	char month2[20];
	int date2;
	int year2;

	sscanf(timestamp1,"%*[^ ] %[^ ] %d %*[^ ] %*[^ ] %d",month1,&date1,&year1);
	sscanf(timestamp2,"%*[^ ] %[^ ] %d %*[^ ] %*[^ ] %d",month2,&date2,&year2);

//	printf("Month1: %s , date1: %d , year1: %d\n",month1,date1,year1);
//	printf("Month1: %s , date1: %d , year1: %d\n",month2,date2,year2);

	if( !strcmp(month1,month2) && date1 == date2 && year1 == year2)
		return 1;
	else
		return 0;


}

int write_buffer_to_disk( CBLK wb_block ,char *chunk_path,CACHE buffer_cache) {

	if (wb_block->offset == 0) // The buffer might be flushed due to periodic_flush
		return 0;
	int fd; // = open("sample-log",O_RDONLY);
	//read(fd,wb_block->buf,4096);
	
	//printf("Buffer length : %d\n",strlen(wb_block->buf));
	//close(fd);

	int startOffset = 0;
	int offset=0;
	char line[MAX_LINE_SIZE];
	
	memset(line,0,MAX_LINE_SIZE);

	char timestamp[50];
	char lastSeenDate[50]="";
	char startTime[50];
	char folderName[MAX_FILE_NAME_SIZE];
	char chunk_name[MAX_FILE_NAME_SIZE];
//	printf("Result : %d\n",sscanf(wb_block->buf+offset,"%[^\n]\n",line));
//	offset+=strlen(line)+1;
//	printf("Result : %d\n",sscanf(wb_block->buf+offset,"%[^\n]\n",line));

	
	if ( sscanf(wb_block->buf+offset,"%[^\n]\n",line) == 1 ) {
//              printf("%s\n",line);
                if ( wb_block->buf[offset+strlen(line)] == '\n' && sscanf(line,"%*[^|]|%[^|]|%*s",timestamp) == 1)  {
			if ( sscanf(timestamp,"%*[^ ] %*[^ ] %*[^ ] %[^ ] %*[^ ] %*s",startTime) == 1 ) {
				strcpy(lastSeenDate,timestamp);
				create_date_folder(lastSeenDate,chunk_path,folderName);
				offset += strlen(line) + 1 ;
			} else {
#ifdef DEBUG_CACHE
				printf("Fetching startTime from timestamp failed.Line is not complete\n"); 
#endif
			}
		} else {
#ifdef DEBUG_CACHE
			printf("Fetching timestamp from line failed.Line is not complete\n"); 
#endif
		}
	}


	int i;
	int id=0;
	char chunk_file_name[MAX_FILE_NAME_SIZE];
	struct stat statbuf;
	int lineInCompleteFlag = 0;

	while(sscanf(wb_block->buf+offset,"%[^\n]\n",line) == 1 ) {
//		printf("%s\n",line);
		if ( wb_block->buf[offset+strlen(line)] != '\n' ||    sscanf(line,"%*[^|]|%[^|]|%*s",timestamp) != 1 ) {
#ifdef DEBUG_CACHE
			printf("LINE IS NOT COMPLETE\n");
#endif
			lineInCompleteFlag = 1;
			break;
		}
		//printf("Time : %s\n",timestamp);
		if(  is_dates_equal(timestamp,lastSeenDate) == 0 ) {
			strcpy(chunk_file_name,chunk_path);
			strcat(chunk_file_name,folderName);
			strcat(chunk_file_name,"/");
	
			strcpy(chunk_name,wb_block->mdata->file_name);	
			for( i=1;chunk_name[i]!= '\0';i++) {
				if(chunk_name[i] == '/')
					chunk_name[i] = '-';
			}

			strcat(chunk_file_name,chunk_name);
			strcat(chunk_file_name,"-");
			strcat(chunk_file_name,startTime);

			if ( stat(chunk_file_name,&statbuf) == -1 ) {		

				fd = open(chunk_file_name,O_CREAT|O_RDWR,0777);
#ifdef DEBUG_CACHE
				printf("NEW file : %s File descriptor:%d\n",chunk_file_name,fd);
#endif
				write(fd,wb_block->buf+startOffset,offset-startOffset);		
				close(fd);

				strcpy(wb_block->mdata->path[wb_block->mdata->num_paths++],chunk_file_name);
			} else {
				fd = open(chunk_file_name,O_APPEND|O_RDWR,0777);
#ifdef DEBUG_CACHE
				printf("APPENDING file : %s File descriptor:%d\n",chunk_file_name,fd);
#endif
				write(fd,wb_block->buf+startOffset,offset-startOffset);		
				close(fd);

			}
			wb_block->mdata->size += offset-startOffset;


#ifdef DEBUG_CACHE
			printf("Current buf start : %d , buf end : %d\n",startOffset,offset);
			write(1,wb_block->buf+startOffset,offset-startOffset);
			printf("==============\n");
#endif
			sscanf(timestamp,"%*[^ ] %*[^ ] %*[^ ] %[^ ] %*[^ ] %*s",startTime);
			startOffset = offset; 
			strcpy(lastSeenDate,timestamp);
	//		memset(folderName,0,MAX_FILE_NAME_SIZE);
			create_date_folder(lastSeenDate,chunk_path,folderName);

		}

		offset += strlen(line) + 1 ;

#ifdef DEBUG_CACHE
		printf("Offset: %d line : %s\n",offset,line);
#endif		
	}


	strcpy(chunk_file_name,chunk_path);
	strcat(chunk_file_name,folderName);
	strcat(chunk_file_name,"/");

	strcpy(chunk_name,wb_block->mdata->file_name);	
	for( i=1;chunk_name[i]!= '\0';i++) {
		if(chunk_name[i] == '/')
			chunk_name[i] = '-';
	}

	strcat(chunk_file_name,chunk_name);
	strcat(chunk_file_name,"-");
	strcat(chunk_file_name,startTime);

	if ( stat(chunk_file_name,&statbuf) == -1 ) {
		fd = open(chunk_file_name,O_CREAT|O_RDWR,0777);
		strcpy(wb_block->mdata->path[wb_block->mdata->num_paths++],chunk_file_name);
	} else {
		fd = open(chunk_file_name,O_APPEND|O_RDWR,0777);
	
	}
	write(fd,wb_block->buf+startOffset,offset-startOffset);
	wb_block->mdata->size += offset-startOffset;


#ifdef DEBUG_CACHE
	printf("Current buf start : %d , buf end : %d\n",startOffset,offset);
	write(1,wb_block->buf+startOffset,offset-startOffset);
	printf("+++++++++++\n");
#endif
	close(fd);


//	if ( wb_block->buf[offset+strlen(line)] != '\n') {
	if ( lineInCompleteFlag == 1 ) {
		memset(wb_block->buf,0,buffer_cache->cache_block_size);
		strcpy(wb_block->buf,line);
#ifdef DEBUG_CACHE
		printf("REMAINING BUF CONTENT : %s\n",wb_block->buf);
#endif
		wb_block->offset = strlen(line);
	} else {
		memset(wb_block->buf,0,buffer_cache->cache_block_size);
	        wb_block->offset = 0;
	}
	
}

int free_cache_block(CACHE c,CBLK cache_blk) {
	cache_blk->free_flag = true;
	if ( c->type == WRITE_BUFFER ) {
		memset(cache_blk->buf,0,c->cache_block_size);
		cache_blk->offset = 0;
		cache_blk->mdata = NULL;
	}

	cache_blk->lru_counter = 0;
	
}
/*
int main() {

	int result;
	CACHE buffer_cache = create_cache(4096*10,4096,WRITE_BUFFER);
	
	CACHE meta_data_cache = create_cache(100,1,METADATA_CACHE);

	CBLK new_block = get_free_cache_block(meta_data_cache,&result);

	strcpy(new_block->mdata->file_name,"file1.txt");

	new_block->mdata->num_paths = 2;

	strcpy(new_block->mdata->path[0],"/home/vino/lfs-store/16-03-2012/file1.txt-12:03-12:30");
	strcpy(new_block->mdata->path[1],"/home/vino/lfs-store/16-03-2012/file1.txt-12:31-12:45");

	update_lru(meta_data_cache,new_block);
	CBLK new_block2 = get_free_cache_block(meta_data_cache,&result);

	strcpy(new_block2->mdata->file_name,"file2.txt");

	new_block2->mdata->num_paths = 2;

	strcpy(new_block2->mdata->path[0],"/home/vino/lfs-store/16-03-2012/file2.txt-12:03-12:30");
	strcpy(new_block2->mdata->path[1],"/home/vino/lfs-store/16-03-2012/file2.txt-12:31-12:45");


//	print_cache_block(new_block);

	CBLK new_wb_block = get_free_cache_block(buffer_cache,&result);
	new_wb_block->mdata = new_block->mdata;

	char string[200] = "Content 1";
	write_cache_block(buffer_cache,new_block->mdata,string,strlen(string));
	//strcpy(new_wb_block->buf,"Content 1");
	//new_wb_block->offset = 8;
	//new_wb_block->free_flag = false;

//	update_lru(buffer_cache,new_wb_block);

//	print_cache_block(new_wb_block);
	CBLK new_wb_block2 = get_free_cache_block(buffer_cache,&result);
	new_wb_block2->mdata = new_block2->mdata;
	strcpy(string,"ERROR|Sat Mar 17 20:33:43 EDT 2012|File Not Found\nERROR|Sat Mar 17 20:33:49 EDT 2012|Folder not found\n");
	write_cache_block(buffer_cache,new_block2->mdata,string,strlen(string));
	write_cache_block(buffer_cache,new_block2->mdata,string,strlen(string));
	

	strcpy(string,"Content X");
	write_cache_block(buffer_cache,new_block->mdata,string,strlen(string));
//	strcpy(new_wb_block2->buf,"Content 2");
//	new_wb_block2->offset = 8;
//	new_wb_block2->free_flag = false;


//	update_lru(buffer_cache,new_wb_block2);
//	update_lru(buffer_cache,new_wb_block);
//	print_cache(buffer_cache);
	


	printf("----------------\n");
  //      update_lru(buffer_cache,new_wb_block);
        print_cache(buffer_cache);

	char out[2000];
	int len;
	printf("Result : %d\n",read_cache_block(buffer_cache,new_block->mdata,out,&len));

	printf("Read content : %s\n",out);

	if (find_meta_data_block(meta_data_cache,"file2.txt") == NULL ) {
		printf("Find returned NULL\n");
	} else { 
		print_cache_block(find_meta_data_block(meta_data_cache,"file2.txt"));
	}

	char *buf = "FileName:file1\nNumPaths:2\nPaths:path1;path2;";
	char fileName[100];
	int num_path;
	char paths[200*1000];
	sscanf(buf,"FileName:%s\nNumPaths:%d\nPaths:%s\n",fileName,&num_path,paths);

	printf("File name : %s num_path : %d paths: %s\n",fileName,num_path,paths);

	char all_paths[1000][200];
	int i=0;
	int j=0;
	int offset=0;
	for(i=0;i<num_path;i++) {
		offset=0;
		while(paths[j]!= ';') {
			all_paths[i][offset] = paths[j];
			offset++;
			j++;
		}
		j++;
		all_paths[i][offset]='\0';
	}	

	for(i=0;i<num_path;i++) {
		printf("Path %d : %s\n",i,all_paths[i]);
	}

	//create_date_folder("Sat Mar 17 20:33:43 EDT 2012","/home/vino/projects/ess/chunk_folder/");

	write_buffer_to_disk(new_wb_block2,"/home/vino/projects/ess/chunk_folder/");

	printf("------Meta data cache--------\n");
        print_cache(meta_data_cache);
//	printf("Retrun val : %d\n",is_dates_equal("Sat Mar 17 20:33:43 EDT 2012","Sat Mar 18 20:33:43 EDT 2012"));
} 
*/

