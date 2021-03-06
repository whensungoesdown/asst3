/*
  Simple File System

  This code is derived from function prototypes found /usr/include/fuse/fuse.h
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  His code is licensed under the LGPLv2.

*/

#include "params.h"
#include "block.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#ifdef HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#endif

#include "log.h"

//-----------------------------------------------------------------------------//
// super block at block 0, it takes 1 block
// inode array starts at block 1, 16 blocks long
// 0-16 blocks will be occupied once the file system established

#define BLOCK_SIZE  512

// 16MB file system
#define MAX_FS_SIZE     16 * 1024 * 1024
#define MAX_FS_BLOCK_NUM   (MAX_FS_SIZE / BLOCK_SIZE)


#define INODE_BLOCKS    16

// maximum 128 files
#define MAX_INODE_NUM   128

// max file size 4kb
#define MAX_FILE_BLOCK_NUM  8  


#define MAGIC   'SF3A'


struct super_block {
    int magic;
    int free_block_num;
    int inode_num;
    char block[MAX_FS_BLOCK_NUM]; //charmap
};

struct inode {
    int used;
    unsigned int filesize;
    char name[12]; // 8.3
    int block[MAX_FILE_BLOCK_NUM];
};
//-----------------------------------------------------------------------------//
#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
//-----------------------------------------------------------------------------//
int alloc_block()
{
    int i = 0;

    if (SFS_DATA->sb->free_block_num <= 0) {
        return 0;
    }

    for (i = 0; i < MAX_FS_BLOCK_NUM; i++) {
        if (1 == SFS_DATA->sb->block[i]) {
            continue;
        }
        else {
            SFS_DATA->sb->free_block_num -= 1;
            SFS_DATA->sb->block[i] = 1;

            return i;
        }
    }
}

void free_block(int blocknum)
{
    SFS_DATA->sb->block[blocknum] = 0;
    SFS_DATA->sb->free_block_num += 1;
}
//-----------------------------------------------------------------------------//
struct inode * find_inode (char* name)
{
    int i = 0;

    for (i = 0; i < MAX_INODE_NUM; i++) {

        if (SFS_DATA->inodes[i].used) {
            if (0 == strcmp(SFS_DATA->inodes[i].name, name)) {
                return &SFS_DATA->inodes[i];
            }
        }
    }

    return NULL;
}
//-----------------------------------------------------------------------------//
struct inode* create_inode (char* name)
{
    int i = 0;

    if (SFS_DATA->sb->inode_num >= MAX_INODE_NUM) {
        return NULL;
    }

    SFS_DATA->sb->inode_num += 1;

    for (i = 0; i < MAX_INODE_NUM; i++) {
        if (0 == SFS_DATA->inodes[i].used) {
            memset(&(SFS_DATA->inodes[i]), 0, sizeof(struct inode));
            SFS_DATA->inodes[i].used = 1;
            strcpy(SFS_DATA->inodes[i].name, name);
            SFS_DATA->inodes[i].filesize = 0;

            return &(SFS_DATA->inodes[i]);
        }
    }

    return NULL;
}

void remove_inode (struct inode* in)
{
    int i = 0;

    for (i = 0; i < MAX_FILE_BLOCK_NUM; i++) {
        if (0 != in->block[i]) {
            free_block(in->block[i]);
        }
    }

    SFS_DATA->sb->inode_num -= 1;

    memset((void*)in, 0, sizeof(struct inode));
}
//-----------------------------------------------------------------------------//
void writeback_sb()
{
    block_write(0, SFS_DATA->sb);
}

void writeback_inodes()
{
    int i = 0;

    for (i = 0; i < 16; i++) {
        block_write(1 + i, (char*)SFS_DATA->inodes + i * BLOCK_SIZE);
    }
}
//-----------------------------------------------------------------------------//
///////////////////////////////////////////////////////////
//
// Prototypes for all these functions, and the C-style comments,
// come indirectly from /usr/include/fuse.h
//

/**
 * Initialize filesystem
 *
 * The return value will passed in the private_data field of
 * fuse_context to all file operations and as a parameter to the
 * destroy() method.
 *
 * Introduced in version 2.3
 * Changed in version 2.6
 */
void *sfs_init(struct fuse_conn_info *conn)
{
    int ret = 0;
    int i = 0;

    fprintf(stderr, "in bb-init\n");
    log_msg("\nsfs_init()\n");
    log_msg("\n uty: test\n");
    
    printf("sfs_init\n");
    
    log_conn(conn);
    log_fuse_context(fuse_get_context());


    SFS_DATA->sb = malloc(BLOCK_SIZE);
    if (NULL == SFS_DATA->sb) {
        log_msg("\n!!!!!!!!!!! ERROR, sfs_init: malloc fail \n");
    }

    disk_open(SFS_DATA->diskfile);

    ret = block_read(0, SFS_DATA->sb);
    if (BLOCK_SIZE != ret) {
        log_msg("\n!!!!!!!!!!! ERROR, sfs_init: diskopen fail \n");
    }


    SFS_DATA->inodes = malloc(BLOCK_SIZE * INODE_BLOCKS);
    if (NULL == SFS_DATA->inodes) {
        log_msg("\n!!!!!!!!!!! ERROR, sfs_init: malloc fail \n");
    }

    for (i = 0; i < 16; i++) {
        ret = block_read(1 + i, (char*)(SFS_DATA->inodes) + BLOCK_SIZE * i); 
        if (BLOCK_SIZE != ret) {
            log_msg("\n!!!!!!!!!!! ERROR, sfs_init: diskopen fail \n");
        }
    }


    if (MAGIC != SFS_DATA->sb->magic) {
        // init a3fs filesystem
        log_msg("uty: new filesystem\n");
        log_msg("uty: format\n");

        SFS_DATA->sb->magic = MAGIC;

        SFS_DATA->sb->free_block_num = MAX_FS_BLOCK_NUM - 17;
        SFS_DATA->sb->inode_num = 0;

        for (i = 0; i < 17; i++) {
            SFS_DATA->sb->block[i] = 1;
        }

        // write back
        block_write(0, SFS_DATA->sb);

    } else {
        log_msg("uty: recognized file system, a3fs\n");
    }
    

    return SFS_DATA;
}

/**
 * Clean up filesystem
 *
 * Called on filesystem exit.
 *
 * Introduced in version 2.3
 */
void sfs_destroy(void *userdata)
{
    log_msg("\nsfs_destroy(userdata=0x%08x)\n", userdata);

    free(SFS_DATA->sb);
    free(SFS_DATA->inodes);
    disk_close();
}

/** Get file attributes.
 *
 * Similar to stat().  The 'st_dev' and 'st_blksize' fields are
 * ignored.  The 'st_ino' field is ignored except if the 'use_ino'
 * mount option is given.
 */
int sfs_getattr(const char *path, struct stat *statbuf)
{
    int retstat = 0;
    char name[13] = {0};
    struct inode* in = NULL;

    
    log_msg("\nsfs_getattr(path=\"%s\", statbuf=0x%08x)\n",
	  path, statbuf);

    printf("uty: sfs_getattr()\n");


    statbuf->st_uid = getuid();
    statbuf->st_gid = getgid();
    statbuf->st_atime = time(NULL);
    statbuf->st_mtime = time(NULL);

    if (0 == strcmp(path, "/")) {
        statbuf->st_mode = S_IFDIR | 0755;
        statbuf->st_nlink = 2;
    } else {
        strcpy(name, path + 1);
        in = find_inode(name);
        if (NULL == in) {
            statbuf->st_size = 0;
        }
        else {
            statbuf->st_size = in->filesize;
        }

        statbuf->st_mode = S_IFREG | 0644;
        statbuf->st_nlink = 1;

    }
    
    return 0;
}

/**
 * Create and open a file
 *
 * If the file does not exist, first create it with the specified
 * mode, and then open it.
 *
 * If this method is not implemented or under Linux kernel
 * versions earlier than 2.6.15, the mknod() and open() methods
 * will be called instead.
 *
 * Introduced in version 2.5
 */
int sfs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_create(path=\"%s\", mode=0%03o, fi=0x%08x)\n",
	    path, mode, fi);
    
    
    return retstat;
}

/** Remove a file */
int sfs_unlink(const char *path)
{
    int retstat = 0;

    char name[13] = {0};
    struct inode* in = NULL;

    log_msg("sfs_unlink(path=\"%s\")\n", path);

    if (strlen(path) > 13) {
        return -ENOENT;
    }

    strcpy(name, path + 1);

    in = find_inode(name);
    if (NULL == in) {
        return -ENOENT;
    } else {
        remove_inode(in);

        writeback_sb();
        writeback_inodes();
    }
    
    return retstat;
}

/** File open operation
 *
 * No creation, or truncation flags (O_CREAT, O_EXCL, O_TRUNC)
 * will be passed to open().  Open should check if the operation
 * is permitted for the given flags.  Optionally open may also
 * return an arbitrary filehandle in the fuse_file_info structure,
 * which will be passed to all file operations.
 *
 * Changed in version 2.2
 */
int sfs_open(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;
    char name[13] = {0};
    struct inode* in = NULL;

    log_msg("\nsfs_open(path\"%s\", fi=0x%08x)\n",
	    path, fi);

    log_msg("uty: fi->flags 0x%x\n", fi->flags);

    if (strlen(path) > 13) {
        // if file name is not 8.3 format
        log_msg("uty: file name is not 8.3 format, too long\n");
        return -1;
    }
    
    strcpy(name, path + 1); // get rid of the "/" 
    in = find_inode(name);
    if (NULL == in) {
        log_msg("uty: no such file exist\n");
        if ((fi->flags & O_WRONLY) || (fi->flags & O_RDWR)) {
            in = create_inode(name);
            if (NULL == in) {
                return -ENFILE; // too many files
            }
            writeback_sb();
            writeback_inodes();
        }
    }


    
    return retstat;
}

/** Release an open file
 *
 * Release is called when there are no more references to an open
 * file: all file descriptors are closed and all memory mappings
 * are unmapped.
 *
 * For every open() call there will be exactly one release() call
 * with the same flags and file descriptor.  It is possible to
 * have a file opened more than once, in which case only the last
 * release will mean, that no more reads/writes will happen on the
 * file.  The return value of release is ignored.
 *
 * Changed in version 2.2
 */
int sfs_release(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_release(path=\"%s\", fi=0x%08x)\n",
	  path, fi);
    

    return retstat;
}

/** Read data from an open file
 *
 * Read should return exactly the number of bytes requested except
 * on EOF or error, otherwise the rest of the data will be
 * substituted with zeroes.  An exception to this is when the
 * 'direct_io' mount option is specified, in which case the return
 * value of the read system call will reflect the return value of
 * this operation.
 *
 * Changed in version 2.2
 */
int sfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    int retstat = 0;

    char name[13] = {0};
    int n = 0;
    struct inode* in = NULL;
    unsigned int readlen = 0;
    char* tmpbuf = 0;
    int i = 0;

    log_msg("\nsfs_read(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n",
	    path, buf, size, offset, fi);

    if (strlen(path) > 13) {
        return -ENOENT;
    }

    strcpy(name, path + 1);

    in = find_inode(name);
    if (NULL == in) {
        return -ENOENT;
    }

    readlen = MIN(offset + size, in->filesize);
    log_msg("uty: filesize %d, readlen %d\n", in->filesize, readlen);

    n = (readlen) / BLOCK_SIZE;
    if ((readlen) % BLOCK_SIZE != 0) {
        n += 1;
    }
    
    log_msg("uty: read %d blocks\n", n);


    tmpbuf = malloc(n * BLOCK_SIZE);
    memset(tmpbuf, 0, n * BLOCK_SIZE);

    for (i = 0; i < n; i++) {
        log_msg("uty: %d blocknum %d\n", i, in->block[i]);
        block_read(in->block[i], tmpbuf + i * BLOCK_SIZE);
    }

    memcpy(buf, tmpbuf + offset, readlen - offset);

    free(tmpbuf);
    return readlen - offset;
}

/** Write data to an open file
 *
 * Write should return exactly the number of bytes requested
 * except on error.  An exception to this is when the 'direct_io'
 * mount option is specified (see read operation).
 *
 * Changed in version 2.2
 */
int sfs_write(const char *path, const char *buf, size_t size, off_t offset,
	     struct fuse_file_info *fi)
{
    int retstat = 0;

    char name[13] = {0};
    struct inode* in = NULL;

    int n = 0;
    char* tmpbuf = NULL;
    int blocknum = 0;

    int i = 0;


    log_msg("\nsfs_write(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n",
	    path, buf, size, offset, fi);

    log_msg("uty: buf: %s\n", buf);

    if (strlen(path) > 13) {
        return -ENOENT;
    }

    strcpy(name, path + 1);

    log_msg("uty: name %s\n", name);

    in = find_inode(name);

    if (NULL == in) {
        in = create_inode(name);
        if (NULL == in) {
            return -ENFILE; // too many files
        }
    }
    log_msg("uty: inode 0x%x\n", in);

    n = (offset + size) / BLOCK_SIZE;
    if ((offset + size) % BLOCK_SIZE != 0) {
        n += 1;
    }

    if (n > MAX_FILE_BLOCK_NUM) {
        return -EFBIG;
    }

    in->filesize = offset + size;

    tmpbuf = malloc(n * BLOCK_SIZE);
    memset(tmpbuf, 0, n * BLOCK_SIZE);

    memcpy(tmpbuf + offset, buf, size);
    

    for (i = 0; i < n; i++) {
        blocknum = alloc_block();
        if (0 == blocknum) {
            return -ENOSPC;
        }

        log_msg("uty: block %d, block num %d\n", i, blocknum);
        log_msg("uty: write buffer %s\n", tmpbuf + i * BLOCK_SIZE);

        in->block[i] = blocknum;

        block_write(blocknum, tmpbuf + i * BLOCK_SIZE);
    }
    
    free(tmpbuf);

    writeback_sb();
    writeback_inodes();

    return size;
}


/** Create a directory */
int sfs_mkdir(const char *path, mode_t mode)
{
    int retstat = 0;
    log_msg("\nsfs_mkdir(path=\"%s\", mode=0%3o)\n",
	    path, mode);
   
    
    return retstat;
}


/** Remove a directory */
int sfs_rmdir(const char *path)
{
    int retstat = 0;
    log_msg("sfs_rmdir(path=\"%s\")\n",
	    path);
    
    
    return retstat;
}


/** Open directory
 *
 * This method should check if the open operation is permitted for
 * this  directory
 *
 * Introduced in version 2.3
 */
int sfs_opendir(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_opendir(path=\"%s\", fi=0x%08x)\n",
	  path, fi);
    
    
    return retstat;
}

/** Read directory
 *
 * This supersedes the old getdir() interface.  New applications
 * should use this.
 *
 * The filesystem may choose between two modes of operation:
 *
 * 1) The readdir implementation ignores the offset parameter, and
 * passes zero to the filler function's offset.  The filler
 * function will not return '1' (unless an error happens), so the
 * whole directory is read in a single readdir operation.  This
 * works just like the old getdir() method.
 *
 * 2) The readdir implementation keeps track of the offsets of the
 * directory entries.  It uses the offset parameter and always
 * passes non-zero offset to the filler function.  When the buffer
 * is full (or an error happens) the filler function will return
 * '1'.
 *
 * Introduced in version 2.3
 */
int sfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
	       struct fuse_file_info *fi)
{
    int retstat = 0;
    int i = 0;
    
    fprintf(stderr, "uty: sfs_readdir, \n");
    printf("uty: sfs_readdir\n");

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    if (0 == strcmp(path, "/")) {
        // go through indoes
        for (i = 0; i < MAX_INODE_NUM; i++) {
            if (1 == SFS_DATA->inodes[i].used) {
                filler(buf, SFS_DATA->inodes[i].name, NULL, 0);
            }
        }
    }
    
    return retstat;
}

/** Release directory
 *
 * Introduced in version 2.3
 */
int sfs_releasedir(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;

    
    return retstat;
}

int sfs_truncate(const char* path, off_t size)
{
    char name[13] = {0};

    struct inode* in = NULL;

    log_msg("uty: sfs_truncate path %s, size %d\n", path, size);
    
    in = find_inode(name);
    if (NULL == in) {
        return 0;
    } else {
        in->filesize = size;

        writeback_inodes();
    }

    return 0;
}

int sfs_utimens (const char *path, const struct timespec tv[2])
{
    return 0;
}

struct fuse_operations sfs_oper = {
  .init = sfs_init,
  .destroy = sfs_destroy,

  .getattr = sfs_getattr,
  .create = sfs_create,
  .unlink = sfs_unlink,
  .open = sfs_open,
  .release = sfs_release,
  .read = sfs_read,
  .write = sfs_write,

  .rmdir = sfs_rmdir,
  .mkdir = sfs_mkdir,

  .opendir = sfs_opendir,
  .readdir = sfs_readdir,
  .releasedir = sfs_releasedir,

  .truncate = sfs_truncate,
  .utimens = sfs_utimens
};

void sfs_usage()
{
    fprintf(stderr, "usage:  sfs [FUSE and mount options] diskFile mountPoint\n");
    abort();
}

int main(int argc, char *argv[])
{
    int fuse_stat;
    struct sfs_state *sfs_data;
    
    // sanity checking on the command line
    if ((argc < 3) || (argv[argc-2][0] == '-') || (argv[argc-1][0] == '-'))
	sfs_usage();

    sfs_data = malloc(sizeof(struct sfs_state));
    if (sfs_data == NULL) {
	perror("main calloc");
	abort();
    }

    // Pull the diskfile and save it in internal data
    //sfs_data->diskfile = argv[argc-2];
    sfs_data->diskfile = malloc(strlen(argv[argc-2]));
    memset(sfs_data->diskfile, 0, strlen(argv[argc-2]));
    strcpy(sfs_data->diskfile, argv[argc-2]);

    argv[argc-2] = argv[argc-1];
    argv[argc-1] = NULL;
    argc--;
    
    sfs_data->logfile = log_open();
    
    // turn over control to fuse
    fprintf(stderr, "about to call fuse_main, %s \n", sfs_data->diskfile);
    fuse_stat = fuse_main(argc, argv, &sfs_oper, sfs_data);
    fprintf(stderr, "fuse_main returned %d\n", fuse_stat);
    
    return fuse_stat;
}
