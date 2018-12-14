#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/uaccess.h>
#include <linux/fs.h>
#include <linux/vfs.h>
#include <linux/init.h>
#include <linux/smp_lock.h>
#include <linux/buffer_head.h>
#include <asm/bitops.h>

#include "sfs.h"

char* get_inodemap(struct buffer_head *bh_sb){
    struct sfs_sb *super_block=(struct sfs_sb *)(bh_sb->b_data);
    if(super_block){
        return super_block->i_nodemap;
    }
    return 0;
}
struct buffer_head* get_super_bh(struct super_block *s){
    return  sb_bread(s, 0);
}
char* get_dentrymap(struct buffer_head *bh_sb){
    struct sfs_sb *super_block=(struct sfs_sb *)(bh_sb->b_data);
    if(super_block){
        return super_block->dentry_map;
    }
    return 0;
}

static void sfs_read_inode(struct inode *inode) {
    int ino = (int)inode->i_ino;
    struct sfs_ino *di, ri;
    struct buffer_head *bh;
    int block, off;
    int i = 0;
      
    if (ino < SFS_ROOT_INO){
        printk("Bad inode number\n");
        make_bad_inode(inode);
        return;
    }
    block = INODE_BLOCK_START + ino/SFS_INO_PER_BLOCK;
    bh = sb_bread(inode->i_sb, block);
    if (!bh) {
        printk("Unable to read inode %d\n", ino);
        make_bad_inode(inode);
        return;
    }
    off = (ino % SFS_INO_PER_BLOCK) * SFS_INODESIZE;
    di = (struct sfs_ino *)(bh->b_data + off);

    ri = *di;
    //todo: may merge sfs_file_inops
    inode->i_mode = 0x0000ffff & di->di_mode;
    if (di->is_dir ) {
        printk("DIR_TYPE\n");
        inode->i_mode |= S_IFDIR;
        inode->i_op = &sfs_dir_inops;
        inode->i_fop = &sfs_dir_operations;
    } else  {
        printk("LAB5FS_REG_TYPE\n");
        inode->i_mode |= S_IFREG;
        inode->i_op = &sfs_file_inops;
        inode->i_fop = &sfs_file_operations;
        inode->i_mapping->a_ops = &sfs_aops;
    }

    inode->i_uid = di->di_uid;
    inode->i_gid = di->di_gid;
    inode->i_nlink = di->di_nlink;
    //todo： why bother to change i_size to block_num and last offset
    if (ino != SFS_ROOT_INO)
        inode->i_size = SFS_FILESIZE(di);
    else
        inode->i_size = 4096;
    inode->i_blksize = PAGE_SIZE;
    inode->i_atime.tv_sec = di->di_atime;
    inode->i_mtime.tv_sec = di->di_mtime;
    inode->i_ctime.tv_sec = di->di_ctime;
    inode->i_atime.tv_nsec = 0;
    inode->i_mtime.tv_nsec = 0;
    inode->i_ctime.tv_nsec = 0;
    INODE_TO_SFSINODE(inode)->bi_dsk_ino = di->di_ino;
    memcpy(INODE_TO_SFSINODE(inode)->bi_blocks, di->di_blocks, sizeof(uint32_t)*INO_BLOCK_LEN);
    inode->i_blocks = di->di_blocknum;
    mark_inode_dirty(inode);

out:
    brelse(bh);
    printk("\n");
}

static int sfs_write_inode(struct inode *inode, int unused) {
    int ino_block_index, ino_within_block_index;
    struct buffer_head *bh_inode_block;
    struct sfs_ino *di;
    int block;
    unsigned long ino = inode->i_ino;

    printk("sfs_write_inode(), ino = %lu\n", ino);
    
    lock_kernel();
    
    block = INODE_BLOCK_START;
    ino_block_index = ino / SFS_INO_PER_BLOCK;
    ino_within_block_index = ino % SFS_INO_PER_BLOCK;
    printk("ino_block_index = %d, ino_within_block_index = %d\n",
        ino_block_index, ino_within_block_index);

    bh_inode_block = sb_bread(inode->i_sb, block + ino_block_index);
    if (!bh_inode_block) {
        printk("Unable to read inode block\n");
        unlock_kernel();
        return -EIO;
    }

    di = (struct sfs_ino *) (bh_inode_block->b_data);
    di += ino_within_block_index;

    if (inode->i_ino == SFS_ROOT_INO)
        di->is_dir = 1;
    else
        di->is_dir = 0;
    
    di->di_ino = inode->i_ino;
    di->di_mode = inode->i_mode;
    di->di_uid = inode->i_uid;
    di->di_gid = inode->i_gid;
    di->di_nlink = inode->i_nlink;
    di->di_atime = inode->i_atime.tv_sec;
    di->di_mtime = inode->i_mtime.tv_sec;
    di->di_ctime = inode->i_ctime.tv_sec;
    
    if (di->di_ino != SFS_ROOT_INO) {
        printk("inode->i_size = %llu\n", inode->i_size);
        di->di_endsize = (inode->i_size == 0) ? 0 : (inode->i_size % SFS_BLOCKSIZE);
        di->di_blocknum = (inode->i_size == 0) ? 0 : (inode->i_size / SFS_BLOCKSIZE) + 1;
    } else {
        di->di_endsize = 0; // not matter
        di->di_blocknum = inode->i_blocks;
    }

    memcpy(di->di_blocks, INODE_TO_SFSINODE(inode)->bi_blocks, sizeof(uint32_t)*INO_BLOCK_LEN);

    // flush inode to disk
    mark_buffer_dirty(bh_inode_block);
    brelse(bh_inode_block);
    unlock_kernel();

    return 0;
}

static void sfs_delete_inode(struct inode *inode) {
    unsigned long ino = inode->i_ino;
    struct buffer_head *bh_inode_block;
    struct sfs_ino * di;
    int block;
    int ino_block_index, ino_within_block_index;
    
    struct buffer_head * bh_data_map, *bh_data_block;
    struct sfs_datamap * data_map_p;
    int i;
    int block_data_start = DATA_START; // 297
    void *data_block;
    
    printk("sfs_delete_inode(), ino=%lu\n", ino);
    
    if (ino < SFS_ROOT_INO){
        printk("Bad inode number\n");
        return;
    }
    
    // clear data and data bitmap
    block = DATAMAP_START;
    bh_data_map = sb_bread(inode->i_sb, block);
    if (!bh_data_map) {
        printk("Unable to read data map\n");
    }
    data_map_p = (struct sfs_datamap *) (bh_data_map->b_data);

    for (i=0; i<INO_BLOCK_LEN; i++) {
        //bi_blocks[i] is the absolute block offset
        if (INODE_TO_SFSINODE(inode)->bi_blocks[i] > 0) {
            int index = (int)(INODE_TO_SFSINODE(inode)->bi_blocks[i]) - block_data_start;
            clear_bit(index, data_map_p);
            mark_buffer_dirty(bh_data_map);

        }
    }
    brelse(bh_data_map);
    
    inode->i_size = 0;
    inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
    lock_kernel();
    mark_inode_dirty(inode);
    clear_inodemap_bit(inode);
    unlock_kernel();
    clear_inode(inode);

    printk("\n");
}

static void sfs_put_super(struct super_block *s) {
    printk("sfs_put_super()\n");
}
        
static kmem_cache_t * lab5fs_inode_cachep;

static struct inode *sfs_alloc_inode(struct super_block *sb) {
    struct sfs_inode_info *bi;
    
    printk("sfs_alloc_inode()\n");
    bi = kmem_cache_alloc(lab5fs_inode_cachep, SLAB_KERNEL);
    memset(bi->bi_blocks, 0, sizeof(uint32_t)*INO_BLOCK_LEN);
    printk("\n");
    if (!bi)
        return NULL;
    return &bi->vfs_inode;
}

static void sfs_destroy_inode(struct inode *inode) {
    kmem_cache_free(lab5fs_inode_cachep, INODE_TO_SFSINODE(inode));
}

static void init_once(void * foo, kmem_cache_t * cachep, unsigned long flags) {
    struct sfs_inode_info *bi = foo;
    
    if ((flags & (SLAB_CTOR_VERIFY|SLAB_CTOR_CONSTRUCTOR)) ==
            SLAB_CTOR_CONSTRUCTOR)
        inode_init_once(&bi->vfs_inode);
}

static int init_inodecache(void) {
    printk("init_inodecache()\n");
    lab5fs_inode_cachep = kmem_cache_create("lab5fs_inode_cache",
                            sizeof(struct sfs_inode_info),
                            0, SLAB_RECLAIM_ACCOUNT,
                            init_once, NULL);
    if (lab5fs_inode_cachep == NULL)
        return -ENOMEM;
    return 0;
}

static void destroy_inodecache(void) {
    printk("destroy_inodecache()\n");
    if (kmem_cache_destroy(lab5fs_inode_cachep))
        printk(KERN_INFO "lab5fs_inode_cache: not all structures were freed\n");
    printk("\n");
}

static struct super_operations lab5fs_sops = {
    .alloc_inode    = sfs_alloc_inode,
    .destroy_inode  = sfs_destroy_inode,
    .read_inode     = sfs_read_inode,
    .write_inode    = sfs_write_inode,
    .delete_inode   = sfs_delete_inode,
    .put_super      = sfs_put_super,
};


void clear_inodemap_bit(struct inode *inodep){
    struct buffer_head *bh_sb=get_super_bh(inodep->i_sb);
    if(bh_sb){
       char *inode_map_p = get_inodemap(bh_sb);
        clear_bit(inodep->i_ino,inode_map_p);
        mark_buffer_dirty(bh_sb);
        brelse(bh_sb);
    }

}

static int sfs_fill_super(struct super_block *s, void *data, int silent){
    struct buffer_head *bh;
    struct sfs_sb *mem_sb;
    struct inode *inode;

    sb_set_blocksize(s, SFS_BLOCKSIZE);
    bh = sb_bread(s, 0);
    if (!bh)
        goto out;
    mem_sb = (struct sfs_sb *)bh->b_data;
    if (mem_sb->s_magic != MAGIC){
        if (!silent)
            printk("No LAB5FS filesystem on %s (magic=%08x)\n", s->s_id, mem_sb->s_magic);
        goto out;
    }

    s->s_magic = MAGIC;
    
    // read_inode op
    s->s_op = &lab5fs_sops;
    
    // create root inode
    inode = iget(s, SFS_ROOT_INO);
    if (!inode){
        printk("iget error\n");
        goto out;
    }
    s->s_root = d_alloc_root(inode);
    if (!s->s_root){
        iput(inode);
        goto out;
    }
    //release bh here
    brelse(bh);
    return 0; 

out:
    brelse(bh);
    return -EINVAL;
}

static struct super_block *lab5fs_get_sb(struct file_system_type *fs_type,int flags, const char *dev_name, void *data) {
    return get_sb_bdev(fs_type, flags, dev_name, data, sfs_fill_super);
}

static struct file_system_type lab5fs_fs_type = {
    .owner = THIS_MODULE,
    .name = "lab5fs",
    .get_sb = lab5fs_get_sb,
    .kill_sb = kill_block_super,
    .fs_flags = FS_REQUIRES_DEV,
};


int sfs_init(void) {
    int err;
    
    printk("sfs_init()\n");
    init_inodecache();
    
    err = register_filesystem(&lab5fs_fs_type);

    if (err) {
        printk("register error\n");
        return err;
    }
    printk("register successfully\n");
    return 0;
}

void sfs_exit(void) {
    printk(KERN_INFO "sfs_exit()\n");
    unregister_filesystem(&lab5fs_fs_type);
    destroy_inodecache();
}

MODULE_LICENSE("GPL");   
module_init(sfs_init);
module_exit(sfs_exit);
