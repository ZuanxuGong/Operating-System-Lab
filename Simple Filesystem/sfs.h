#include <linux/list.h>
#define DEBUG

#ifdef DEBUG
#define printk(x...)   printk(x)
#else         
#define printk(x...)
#endif

#define SFS_ROOT_INO         2
#define SFS_NAMELEN          12
#define SFS_BLOCKSIZE        512
#define SFS_INODESIZE        128
#define SFS_DENTRYSIZE       16
#define MAGIC            0x77fadace
#define SFS_MAX_FILESIZE     8*1024  //8K
#define SFS_INO_NUM          1024
#define SFS_MAX_IMAGESIZE    16*1024*1024
#define SFS_MIN_IMAGESIZE     1*1024*1024

#define INO_BLOCK_LEN    (SFS_MAX_FILESIZE/SFS_BLOCKSIZE)    //16
#define SFS_INO_PER_BLOCK        (SFS_BLOCKSIZE/SFS_INODESIZE)   //4
#define SFS_DENTRY_PER_BLOCK     (SFS_BLOCKSIZE/SFS_DENTRYSIZE)  //32

#define DATAMAP_START 1
#define  INODE_BLOCK_START    (DATAMAP_START+8)
#define  DENTRY_BLOCKS_START   (INODE_BLOCK_START+SFS_INO_NUM/SFS_INO_PER_BLOCK)
#define  DATA_START            (DENTRY_BLOCKS_START+SFS_INO_NUM/SFS_DENTRY_PER_BLOCK)

// super block - 512 bytes
struct sfs_sb {
    uint32_t s_magic;   //4 bytes, magic
    uint32_t s_start;   //4 bytes, block offset of start of data
    uint32_t s_end;     //4 bytes

    char s_fsname[SFS_NAMELEN]; //12
    char i_nodemap[SFS_INO_NUM/8]; //128
    char dentry_map[SFS_INO_NUM/8]; //128

    char s_pad[232];
};

// inode - 128 bytes
struct sfs_ino {
    uint32_t di_ino;
    uint32_t di_endsize;   //offset of the last block

    //inode type, file or dir
    uint32_t is_dir, di_mode; //16 bytes so far

    uint32_t di_uid, di_gid, di_nlink;  
    uint32_t di_atime, di_mtime, di_ctime;   //40 bytes so far
    
    uint32_t di_blocknum;    //number of data blocks, max = 16
    uint32_t di_blocks[INO_BLOCK_LEN];  //64 bytes, data block numbers
    
    char i_pad[20];
};

// directory entry - 16 bytes
struct sfs_dentry {
    uint32_t ino;
    char name[SFS_NAMELEN];
};

//data_map
struct sfs_datamap{
    char map[8*SFS_BLOCKSIZE];
};
#define DATA_MAP_BLOCK_NUM  sizeof(struct sfs_datamap)/SFS_BLOCKSIZE

#ifdef __KERNEL__
struct sfs_inode_info {
    unsigned long bi_dsk_ino;
    uint32_t bi_blocks[INO_BLOCK_LEN];
    struct inode vfs_inode;
};

static inline struct sfs_inode_info *INODE_TO_SFSINODE(struct inode *inode) {
    return list_entry(inode, struct sfs_inode_info, vfs_inode);
}
#endif
//i_blocks is number of datablocks of the file
#define D_SIZE(inode) (inode->i_blocks * SFS_DENTRYSIZE)

//get the file size from the blocknum and the offset of the last block
#define SFS_FILESIZE(di) (di->di_blocknum == 0? 0 : (di->di_blocknum-1)*SFS_BLOCKSIZE + di->di_endsize)

/* file.c */
extern struct inode_operations sfs_file_inops;
extern struct file_operations sfs_file_operations;
extern struct address_space_operations sfs_aops;

/* dir.c */
extern struct inode_operations sfs_dir_inops;
extern struct file_operations sfs_dir_operations;
void clear_inodemap_bit(struct inode *inodep);
struct buffer_head* get_super_bh(struct super_block *s);
char* get_inodemap(struct buffer_head *bh_sb);
char* get_dentrymap(struct buffer_head *bh_sb);