#define FUSE_USE_VERSION 26

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef linux
/* For pread()/pwrite() */
#define _XOPEN_SOURCE 500
#endif

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif

#include "log_fs.h"
#include "Cache.h"


#define LOAD_PATH "/home/vino/Desktop/serverfilesystem"
#define MDATA_PATH "/home/vino/Desktop/ESS/metadata/"
#define CHUNK_PATH "/home/vino/Desktop/ESS/Chunks/"
#define MDATA_CBLK_WRITE_THROUGH 1

static char load_path[500];
CACHE meta_data_cache;
CACHE buffer_cache;


int write_metadata_to_disk(MDATA *mdata, char *mdata_path)
{
	char filepath[MAX_PATH_NAME_SIZE];
	strcpy(filepath,mdata_path);
	strcat(filepath,mdata->file_name);
	
	int fd,offset=0;
	fd = open(filepath,O_CREAT|O_RDWR,0777);
	char buf[MAX_MDATA_FILE_SIZE];
	strcpy(buf,"Filename:");
	strcat(buf,mdata->file_name);
	strcat(buf,"\n");
	strcat(buf,"NumPaths:");
        sprintf(buf+strlen(buf),"%d",mdata->num_paths);
        strcat(buf,"\n");
	strcat(buf,"Size:");
	sprintf(buf+strlen(buf),"%d",mdata->size);
        strcat(buf,"\n");
	strcat(buf,"Paths:");
	int i;
	for(i=0;i < mdata->num_paths;i++)
	{
		strcat(buf,mdata->path[i]);
		strcat(buf,";");
	}
	strcat(buf,"\n");
	int bytes_written;
	bytes_written = write(fd,buf,strlen(buf));
        if(bytes_written < 0)
        	return -1;
	else
		return bytes_written;
	close(fd);
}

CBLK mdata_from_disk_to_memory(char *filepath)
{
	int result;
	char mdata_file_path[MAX_PATH_NAME_SIZE];
	strcpy(mdata_file_path,MDATA_PATH);
	strcat(mdata_file_path,filepath);		
	CBLK new_mdata_block = get_free_cache_block(meta_data_cache,&result);
	if(result == WRITE_BACK)
	{
		write_metadata_to_disk(new_mdata_block->mdata,MDATA_PATH);	
	}
	int fd;
	fd = open(mdata_file_path,O_RDONLY);
	if(fd<0)
	{
	 perror("mdata_from_disk_to_memory:fd:");
	 exit(1);
	}
	printf("\nmdata file path : %s\n",mdata_file_path);
	char buf[MAX_MDATA_FILE_SIZE];
	int bytes_read;
	bytes_read = read(fd,buf,MAX_MDATA_FILE_SIZE);
	printf("\nBytes read: %d\n",bytes_read);
	if(bytes_read < 0)
		return NULL;
	else
	{
		char paths[MAX_MDATA_FILE_SIZE];
		if ( sscanf(buf,"Filename:%s\nNumPaths:%d\nSize:%d\nPaths:%s",new_mdata_block->mdata->file_name,&(new_mdata_block->mdata->num_paths),&(new_mdata_block->mdata->size),paths) != 4 ) {
			printf("parsing metadata file failed\n");
			return NULL;
		} else {
			printf("Metadata parsed for file:%s , numpaths : %d all paths:%s\n",new_mdata_block->mdata->file_name,&(new_mdata_block->mdata->num_paths),paths);
		}
		
		int i=0;
		int j=0;
		int offset=0;
		for(i=0;i < new_mdata_block->mdata->num_paths;i++) {
			offset=0;
			while(paths[j]!= ';') {
				new_mdata_block->mdata->path[i][offset] = paths[j];
				offset++;
				j++;
			}
			j++;
			new_mdata_block->mdata->path[i][offset] = '\0';
		}
		update_lru(meta_data_cache,new_mdata_block);
		return new_mdata_block;
	}	
}






static int lfs_getattr(const char *path, struct stat *stbuf)
{
	int res;
	char final_path[MAX_PATH_NAME_SIZE];

	strcpy(final_path,MDATA_PATH);
	strcat(final_path,path);
	res = lstat(final_path, stbuf);

	if (res == -1)
		return -errno;

	if(strcmp(path,"/") == 0 )
		return 0;

	if(path[1] == '.' ) // For all special files starting with '.' just return the stbuf that is returned from lstat
		return 0;

	CBLK meta_data_block = find_meta_data_block(meta_data_cache,path+1);

	if ( meta_data_block == NULL ) {
		meta_data_block = mdata_from_disk_to_memory(path);
		assert(meta_data_block);
                printf("GETATTR : meta_data_block is not found in cache , hence allocating new\n");
	} else {
		update_lru(meta_data_cache,meta_data_block);
	}
        print_cache_block(meta_data_block);

	print_cache(buffer_cache);
	CBLK wbuf_data_block = find_meta_data_block(buffer_cache,path+1);

        if( wbuf_data_block == NULL )
        {
		stbuf->st_size = meta_data_block->mdata->size;
        } else {
	
		update_lru(buffer_cache,wbuf_data_block);	
                printf("GETATTR: wbuf_data_block already found in cache\n");
		print_cache_block(wbuf_data_block);
		stbuf->st_size = meta_data_block->mdata->size + wbuf_data_block->offset;
        }

	stbuf->st_size += meta_data_block->mdata->num_paths;
	printf("ST_BUF_SIZE : %d\n",stbuf->st_size); 

	return 0;
}

static int lfs_access(const char *path, int mask)
{
	int res;
	strcpy(load_path,LOAD_PATH);
	strcat(load_path,path);
	res = access(load_path, mask);
	if (res == -1)
		return -errno;

	return 0;
}

static int lfs_readlink(const char *path, char *buf, size_t size)
{
	int res;
	strcpy(load_path,LOAD_PATH);
	strcat(load_path,path);
	res = readlink(load_path, buf, size - 1);
	if (res == -1)
		return -errno;

	buf[res] = '\0';
	return 0;
}


static int lfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi)
{
	DIR *dp;
	struct dirent *de;

	(void) offset;
	(void) fi;
	strcpy(load_path,LOAD_PATH);
	strcat(load_path,path);
	dp = opendir(load_path);
	if (dp == NULL)
		return -errno;

	while ((de = readdir(dp)) != NULL) {
		struct stat st;
		memset(&st, 0, sizeof(st));
		st.st_ino = de->d_ino;
		st.st_mode = de->d_type << 12;
		printf("%s\n",de->d_name);
		if (filler(buf, de->d_name, &st, 0))
			break;
	}

	closedir(dp);
	return 0;
}

static int lfs_mknod(const char *path, mode_t mode, dev_t rdev)
{
	int res;
	int mdata_file_res;
	char file_mdata_path[500];

	strcpy(load_path,LOAD_PATH);
	strcat(load_path,path);

	if (S_ISREG(mode)) {
		res = open(load_path, O_CREAT | O_EXCL | O_WRONLY, mode);
		strcpy(file_mdata_path,MDATA_PATH);
		strcat(file_mdata_path,path);
	//	mdata_file_res=open(file_mdata_path,O_CREAT|O_RDWR,0777);
	        int result;
                CBLK new_block = get_free_cache_block(meta_data_cache,&result);
		strcpy(new_block->mdata->file_name,path+1);
		new_block->mdata->num_paths = 0;
		new_block->mdata->size = 0;
		update_lru(meta_data_cache,new_block);

		write_metadata_to_disk(new_block->mdata,MDATA_PATH);	
	//	if(mdata_file_res < 0)
	//		perror("file_metadata_open_error");			
	//	else
	//		close(mdata_file_res);

		if (res >= 0)
			res = close(res);
	} else if (S_ISFIFO(mode))
		res = mkfifo(path, mode);
	else
		res = mknod(path, mode, rdev);
	if (res == -1)
		return -errno;

	return 0;
}

static int lfs_mkdir(const char *path, mode_t mode)
{
	int res;
	strcpy(load_path,LOAD_PATH);
	strcat(load_path,path);
	res = mkdir(load_path, mode);
	if (res == -1)
		return -errno;

	return 0;
}

static int lfs_unlink(const char *path)
{
	int res;
	strcpy(load_path,LOAD_PATH);
	strcat(load_path,path);
	res = unlink(load_path);
	if (res == -1)
		return -errno;

	return 0;
}

static int lfs_rmdir(const char *path)
{
	int res;
	strcpy(load_path,LOAD_PATH);
	strcat(load_path,path);
	res = rmdir(load_path);
	if (res == -1)
		return -errno;

	return 0;
}

static int lfs_symlink(const char *from, const char *to)
{
	int res;

	res = symlink(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int lfs_rename(const char *from, const char *to)
{
	int res;

	res = rename(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int lfs_link(const char *from, const char *to)
{
	int res;

	res = link(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int lfs_chmod(const char *path, mode_t mode)
{
	int res;
	strcpy(load_path,LOAD_PATH);
	strcat(load_path,path);
	res = chmod(load_path, mode);
	if (res == -1)
		return -errno;

	return 0;
}

static int lfs_chown(const char *path, uid_t uid, gid_t gid)
{
	int res;
	strcpy(load_path,LOAD_PATH);
	strcat(load_path,path);
	res = lchown(load_path, uid, gid);
	if (res == -1)
		return -errno;

	return 0;
}

static int lfs_truncate(const char *path, off_t size)
{
	int res;
	strcpy(load_path,LOAD_PATH);
	strcat(load_path,path);
	res = truncate(load_path, size);
	if (res == -1)
		return -errno;

	return 0;
}

static int lfs_utimens(const char *path, const struct timespec ts[2])
{
	int res;
	struct timeval tv[2];

	tv[0].tv_sec = ts[0].tv_sec;
	tv[0].tv_usec = ts[0].tv_nsec / 1000;
	tv[1].tv_sec = ts[1].tv_sec;
	tv[1].tv_usec = ts[1].tv_nsec / 1000;
	strcpy(load_path,LOAD_PATH);
	strcat(load_path,path);
	res = utimes(load_path, tv);
	if (res == -1)
		return -errno;

	return 0;
}

static int lfs_open(const char *path, struct fuse_file_info *fi)
{
	int res;
	strcpy(load_path,LOAD_PATH);
	strcat(load_path,path);
	res = open(load_path, fi->flags);
	if (res == -1)
		return -errno;

	close(res);
	return 0;
}

static int lfs_read(const char *path, char *buf, size_t size, off_t offset,
		    struct fuse_file_info *fi)
{
	int fd;
	int res;
	char temp1[1000];
       
	(void) fi;

	printf("\n\n\nIn lfs_read. size %d offset %d \n\n",size,offset);

        CBLK meta_data_block = find_meta_data_block(meta_data_cache,path+1);

        if ( meta_data_block == NULL ) {
                meta_data_block = mdata_from_disk_to_memory(path);
                assert(meta_data_block);
                printf("GETATTR : meta_data_block is not found in cache , hence allocating new\n");
        } else {
                update_lru(meta_data_cache,meta_data_block);
        }

//        print_cache_block(meta_data_block);

  //      print_cache(buffer_cache);


	FILE *fptr;
	int numBytes = 0;
	int bufOffset = 0;

	int i;
	for(i=0;i<meta_data_block->mdata->num_paths;i++) {
		fptr = fopen(meta_data_block->mdata->path[i],"r");
		printf("\nbufOffset : %d\n",bufOffset);
		numBytes = fread(buf+bufOffset,1,meta_data_block->mdata->size,fptr);
		buf[bufOffset+numBytes] = '\n';
//		buf[bufOffset+numBytes+1] = '\0';
		bufOffset += numBytes+1 ;
		fclose(fptr);


	}

	buf[bufOffset] = '\0';
	
        CBLK wbuf_data_block = find_meta_data_block(buffer_cache,path+1);
	if(wbuf_data_block != NULL) {
		strcat(buf,wbuf_data_block->buf);
		bufOffset += strlen(wbuf_data_block->buf);
	}

	return bufOffset;

/*	char spath[500];
	strcpy(spath,"/home/vino/Desktop/serverfilesystem");
	strcat(spath,path);
//	fd = open(spath, O_RDONLY);
//	if (fd == -1)
//		return -errno;
	//strcpy(temp1,"vinoprasanna");
	
//	res = pread(fd, buf, size, offset);
//	printf("\nres is %d\n",res);
	printf("\n\n%s\n\n",buf);
	printf("offset is %d\n",offset);
	printf("size is %d\n",size);
	printf("\nstrlen(buf) is %d\n",strlen(buf));
	char temp2[4096];
	strcpy(temp1,"_padding_");
	strcpy(temp2,buf);
	//buf=(char*)malloc(1000);
	strcat(temp1,temp2);
	printf("now temp1 is %s and has len: %d\n",temp1,strlen(temp1));
	        
	memcpy(buf,temp1,strlen(temp1));
	printf("now content in buf is %s and has len: %d ",buf,strlen(buf));
	printf("size is %d",size);
	if (res == -1)
		res = -errno;
	memset(buf,0,40);
	strcpy(buf,"helllllllllllllllllllllllllllllllllo");

	//close(fd);
	return strlen(buf);
*/
}

static int lfs_write(const char *path, const char *buf, size_t size,
		     off_t offset, struct fuse_file_info *fi)
{
	int fd;
	int res,result;
	strcat(load_path,path);
	
	CBLK meta_data_block = find_meta_data_block(meta_data_cache,path+1);
	if( meta_data_block == NULL )
        {
		printf("meta_data_block is not found in cache , hence allocating new\n");
		meta_data_block = mdata_from_disk_to_memory(path);
		assert(meta_data_block);
		print_cache_block(meta_data_block);
	} else {
		printf("meta_data_block already found in cache\n");
		update_lru(meta_data_cache,meta_data_block);
	}

	CBLK wbuf_data_block = find_meta_data_block(buffer_cache,path+1);
	if(wbuf_data_block == NULL)
	{
		printf("wbuf is not found in cache and hence allocating new\n");
		wbuf_data_block = get_free_cache_block(buffer_cache,&result);

		assert(wbuf_data_block);
		if(result == WRITE_BACK)
		{
			write_buffer_to_disk(wbuf_data_block,CHUNK_PATH,buffer_cache);
#ifdef MDATA_CBLK_WRITE_THROUGH
			write_metadata_to_disk(wbuf_data_block->mdata,MDATA_PATH);	
#endif
			printf("Evicting old cache block\n");
		}
		wbuf_data_block->mdata = meta_data_block->mdata;
	} else {
		printf("wbuf already found in cache\n");
		
	}
	
	if(wbuf_data_block->offset + strlen(buf) > buffer_cache->cache_block_size)
	{
		printf("cache_block buffer full and is written to disk\n");
		write_buffer_to_disk(wbuf_data_block,CHUNK_PATH,buffer_cache);
#ifdef MDATA_CBLK_WRITE_THROUGH
		write_metadata_to_disk(wbuf_data_block->mdata,MDATA_PATH);	
#endif

	} else {
		printf("appending to cache block buffer\n");
	}

	update_lru(buffer_cache,wbuf_data_block);	
	strcat(wbuf_data_block->buf,buf);
	wbuf_data_block->offset += strlen(buf);
	printf("Buffer contents after writing to disk:%s********\n",wbuf_data_block->buf);

	(void) fi;
	//fd = open(path, O_WRONLY);
	//if (fd == -1)
	//	return -errno;

	//res = pwrite(fd, buf, size, offset);
        
	//if (res == -1)
	//	res = -errno;

	//close(fd);
	return strlen(buf);
}

static int lfs_statfs(const char *path, struct statvfs *stbuf)
{
	int res;
	strcpy(load_path,LOAD_PATH);
	strcat(load_path,path);
	res = statvfs(load_path, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int lfs_release(const char *path, struct fuse_file_info *fi)
{
	/* Just a stub.	 This method is optional and can safely be left
	   unimplemented */

	(void) path;
	(void) fi;
	return 0;
}

static int lfs_fsync(const char *path, int isdatasync,
		     struct fuse_file_info *fi)
{
	/* Just a stub.	 This method is optional and can safely be left
	   unimplemented */

	(void) path;
	(void) isdatasync;
	(void) fi;
	return 0;
}

#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
static int lfs_setxattr(const char *path, const char *name, const char *value,
			size_t size, int flags)
{
	int res = lsetxattr(path, name, value, size, flags);
	if (res == -1)
		return -errno;
	return 0;
}

static int lfs_getxattr(const char *path, const char *name, char *value,
			size_t size)
{
	int res = lgetxattr(path, name, value, size);
	if (res == -1)
		return -errno;
	return res;
}

static int lfs_listxattr(const char *path, char *list, size_t size)
{
	int res = llistxattr(path, list, size);
	if (res == -1)
		return -errno;
	return res;
}

static int lfs_removexattr(const char *path, const char *name)
{
	int res = lremovexattr(path, name);
	if (res == -1)
		return -errno;
	return 0;
}
#endif /* HAVE_SETXATTR */

static struct fuse_operations lfs_oper = {
	.getattr	= lfs_getattr,
	.access		= lfs_access,
	.readlink	= lfs_readlink,
	.readdir	= lfs_readdir,
	.mknod		= lfs_mknod,
	.mkdir		= lfs_mkdir,
	.symlink	= lfs_symlink,
	.unlink		= lfs_unlink,
	.rmdir		= lfs_rmdir,
	.rename		= lfs_rename,
	.link		= lfs_link,
	.chmod		= lfs_chmod,
	.chown		= lfs_chown,
	.truncate	= lfs_truncate,
	.utimens	= lfs_utimens,
	.open		= lfs_open,
	.read		= lfs_read,
	.write		= lfs_write,
	.statfs		= lfs_statfs,
	.release	= lfs_release,
	.fsync		= lfs_fsync,
#ifdef HAVE_SETXATTR
	.setxattr	= lfs_setxattr,
	.getxattr	= lfs_getxattr,
	.listxattr	= lfs_listxattr,
	.removexattr	= lfs_removexattr,
#endif
};

int main(int argc, char *argv[])
{
	umask(0);

        buffer_cache = create_cache(4096*10,4096,WRITE_BUFFER);
        
        meta_data_cache = create_cache(100,1,METADATA_CACHE);
	printf("meta_data_cache : %p\n",meta_data_cache);

	return fuse_main(argc, argv, &lfs_oper, NULL);
}
