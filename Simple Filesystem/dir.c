#include <linux/time.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/smp_lock.h>
#include <linux/buffer_head.h>
#include <linux/sched.h>

#include "sfs.h"

static int _dentry_index; 

static int sfs_add_entry(struct inode *dir, const char *name, int namelen, int ino) {
    int block;
    struct buffer_head *bh_sb, *bh;
    struct sfs_dentry *dentry_p;
    int dentry_index, dentry_block_index, dentry_within_block_index;
    int i;

    printk("sfs_add_entry()\n");

    // read dentry bitmap
    bh_sb=get_super_bh(dir->i_sb);
    if (!bh_sb) {
        printk("Unable to read dentry map\n");
        return -ENOSPC;
    }
    char *dentry_map = get_dentrymap(bh_sb);
    printk("Dentry map is %04x\n", *((unsigned int*)dentry_map));

    dentry_index = find_first_zero_bit(dentry_map, SFS_INO_NUM);
    printk("first zero bit index of dentry bitmap = %d\n", dentry_index);
    
    // flush dentry bitmap to disk
    set_bit(dentry_index, dentry_map);
    mark_buffer_dirty(bh_sb);
    brelse(bh_sb);
    printk("flush dentry bitmap to disk\n");

    // read dentry block
    block = DENTRY_BLOCKS_START;
    dentry_block_index = dentry_index / SFS_DENTRY_PER_BLOCK;
    dentry_within_block_index = dentry_index % SFS_DENTRY_PER_BLOCK;

    printk("\t\tdentry_index = %d, dentry_block_index = %d, dentry_within_block_index = %d\n",
           dentry_index, dentry_block_index, dentry_within_block_index);

    bh = sb_bread(dir->i_sb, block + dentry_block_index);
    if (!bh) {
        printk("Unable to read dentry block\n");
    }
    dentry_p = (struct sfs_dentry *) (bh->b_data);
    dentry_p += dentry_within_block_index;


    dir->i_blocks ++;
    printk("\t\tdir->i_blocks = %lu, D_SIZE(dir) = %lu\n",
           dir->i_blocks, D_SIZE(dir));
    dir->i_ctime = CURRENT_TIME;
    dir->i_mtime = CURRENT_TIME;
    mark_inode_dirty(dir);

    // update dentry
    dentry_p->ino = ino;
    for (i=0; i<SFS_NAMELEN; i++)
        dentry_p->name[i] = (i < namelen) ? name[i] : 0;

    printk("\t\tNew dentry to be flushed: ino = %u, name = %s\n", dentry_p->ino, dentry_p->name);

    // flush dentry tp disk
    mark_buffer_dirty(bh);
    brelse(bh);
    printk("\n");
    return 0;

    brelse(bh);
    printk("\n");
    return -ENOSPC;
}


static struct buffer_head * sfs_find_entry(struct inode *dir, const char *name,
                                           int namelen, struct sfs_dentry **res_dir, struct buffer_head **bh_de_map) {
    int block;
    struct buffer_head *bh_sb, *bh;
    struct sfs_dentry *dentry_p;
    char *dentry_map_p;
    int set_bit_pos = -1;
    int dentry_map_size, dentry_index, dentry_block_index, dentry_within_block_index;

    printk("sfs_find_entry()\n");
    printk("dir->ino = %lu, D_SIZE(dir) = %lu\n", dir->i_ino, D_SIZE(dir));

    // Read dentry map
    bh_sb = get_super_bh(dir->i_sb);
    if (!bh_sb) {
        printk("Unable to read dentry map\n");
        return  NULL;
    }
    dentry_map_p = get_dentrymap(bh_sb);
    printk("Dentry map is %04x\n", *((unsigned int*)dentry_map_p));

    block = DENTRY_BLOCKS_START;
    dentry_map_size = SFS_INO_NUM;
    _dentry_index = -1;
    while ((set_bit_pos = find_next_bit(dentry_map_p, dentry_map_size, set_bit_pos+1)) < dentry_map_size) {
        dentry_index = set_bit_pos;
        dentry_block_index = dentry_index / SFS_DENTRY_PER_BLOCK;
        dentry_within_block_index = dentry_index % SFS_DENTRY_PER_BLOCK;

        printk("dentry_index = %d, dentry_block_index = %d, dentry_within_block_index = %d\n",
               dentry_index, dentry_block_index, dentry_within_block_index);

        bh = sb_bread(dir->i_sb, block + dentry_block_index);
        if (!bh) {
            printk("Unable to read dentry block\n");
        }
        dentry_p = (struct sfs_dentry *) (bh->b_data);
        dentry_p += dentry_within_block_index;
        
        if (dentry_p->ino && namecmp(namelen, name, dentry_p->name)) {
            *res_dir = dentry_p;
            *bh_de_map = bh_sb;
            _dentry_index = dentry_index;
            printk("\n");
            return bh;
        }
        brelse(bh);
        bh = NULL;
    }

    brelse(bh_sb);
    printk("\n");
    return NULL;
}

static int sfs_readdir(struct file *f, void *dirent, filldir_t filldir) {
    struct inode * dir = f->f_dentry->d_inode;
    int size;
    int block;
    struct buffer_head *bh_sb, *bh;
    struct sfs_dentry *dentry_p;
    char *dentry_map_p;
    int set_bit_pos = f->f_pos;
    int  dentry_index, dentry_block_index, dentry_within_block_index;
    lock_kernel();
    
    printk("sfs_readdir()\n");
    printk("dir->ino = %lu, D_SIZE(dir) = %lu\n", dir->i_ino, D_SIZE(dir));

    if (f->f_pos >=SFS_INO_NUM) {
        printk("Bad f_pos=%08lx for %s:%08lx\n", f->f_pos, 
                dir->i_sb->s_id, dir->i_ino);
        unlock_kernel();
        return -EBADF;
    }

    // read dentry map 
    bh_sb = get_super_bh(dir->i_sb);
    if (!bh_sb) {
        printk("Unable to read super block\n");
    }
    dentry_map_p = get_dentrymap(bh_sb);
    printk("Dentry map is %04x\n", *((unsigned int*)dentry_map_p));
    
    block = DENTRY_BLOCKS_START;

    while ((set_bit_pos = find_next_bit(dentry_map_p, SFS_INO_NUM, f->f_pos)) < SFS_INO_NUM) {
        dentry_index = set_bit_pos;
        dentry_block_index = dentry_index / SFS_DENTRY_PER_BLOCK;
        dentry_within_block_index = dentry_index % SFS_DENTRY_PER_BLOCK;
        
        bh = sb_bread(dir->i_sb, block + dentry_block_index);
        
        if (!bh) { 
            printk("Unable to read dentry block\n");
        }
        dentry_p = (struct sfs_dentry *) (bh->b_data);
        dentry_p += dentry_within_block_index;
        
        size = strnlen(dentry_p->name, SFS_NAMELEN);
        if (dentry_p->ino != 0) {
            int type = (dentry_p->ino == SFS_ROOT_INO)? DT_DIR : DT_REG;
            printk("ino = %u, type = %d name=%s pos=%d\n", dentry_p->ino, type,dentry_p->name,set_bit_pos);
            if (filldir(dirent, dentry_p->name, size, f->f_pos, dentry_p->ino, type) < 0) {
                printk("ERROR: filldir() failed!\n");
                brelse(bh);
                break;
            }
        }
        //next search start
        f->f_pos =set_bit_pos+1;
       
        brelse(bh);
    }
    
    brelse(bh_sb);
    
    unlock_kernel();
    printk("\n");
    return 0;
}

struct file_operations sfs_dir_operations = {
    .read       = generic_read_dir,
    .readdir    = sfs_readdir,
    .fsync      = file_fsync,
};

static int sfs_create(struct inode *dir, struct dentry *dentry, int mode, struct nameidata *nd) {
    int err;
    struct inode * inode;
    struct super_block * s = dir->i_sb;
    unsigned long ino;
    int block;
    struct buffer_head *bh_sb;
    char  *inode_map_p;
    
    printk("sfs_create(), name = %s, len = %d\n", dentry->d_name.name, dentry->d_name.len);
   
    inode = new_inode(s);
    if (!inode)
        return -ENOSPC;
    lock_kernel();
    
    // read inode map from disk and set bit
    bh_sb = get_super_bh(dir->i_sb);
    if (!bh_sb) {
        printk("Unable to read inode map\n");
    }
    inode_map_p = get_inodemap(bh_sb);
    ino = find_first_zero_bit(inode_map_p, SFS_INO_NUM);
    printk("first zero bit index (new ino) = %lu\n", ino);
    if (ino >= SFS_INO_NUM) {
        unlock_kernel();
        printk("error: ino is beyond ino map limit\n");
        iput(inode);
        return -ENOSPC;
    }
    set_bit(ino, inode_map_p); 
    
    mark_buffer_dirty(bh_sb);
    brelse(bh_sb);
    printk("flush inodemap to disk\n");

    inode->i_uid = current->fsuid;
    inode->i_gid = (dir->i_mode & S_ISGID) ? dir->i_gid : current->fsgid;
    inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
    inode->i_blocks = inode->i_blksize = 0;
    inode->i_op = &sfs_file_inops;
    inode->i_fop = &sfs_file_operations;
    inode->i_mapping->a_ops = &sfs_aops;
    inode->i_mode = mode;
    inode->i_ino = ino;
    inode->i_blocks = 0;
    INODE_TO_SFSINODE(inode)->bi_dsk_ino = ino;
    insert_inode_hash(inode);
    mark_inode_dirty(inode);

    err = sfs_add_entry(dir, dentry->d_name.name, dentry->d_name.len, inode->i_ino);
    if (err) {
        printk("\t\terror when sfs_add_entry\n");
        inode->i_nlink--;
        mark_inode_dirty(inode);
        iput(inode);
        unlock_kernel();
        printk("\n");
        return err;
    }
    
    unlock_kernel();
    d_instantiate(dentry, inode);
    printk("\n");
    return 0;
}

static int sfs_unlink(struct inode *dir, struct dentry *dentry) {
    int error = -ENOENT;
    struct inode * inode;
    struct buffer_head *bh;
    struct sfs_dentry *de;
    struct buffer_head * bh_sb;
    char * dentry_map_p;

    int block;
    char  *inode_map_p;
    
    printk("sfs_unlink()\n");
    
    inode = dentry->d_inode;
    lock_kernel();
    bh = sfs_find_entry(dir, dentry->d_name.name, dentry->d_name.len, &de, &bh_sb);
    printk("dentry found: ino = %zu\n", de->ino);
    if (!bh || de->ino != inode->i_ino) {
        printk("error: !bh || de->ino != inode->i_ino\n");
        goto out_brelse;
    }

    if (!inode->i_nlink) {
        printk("unlinking non-existent file\n");
    }

    // clear dentry bitmap and flush to disk
    dentry_map_p = get_dentrymap(bh_sb);
    clear_bit(_dentry_index, (volatile unsigned long *)dentry_map_p);
    mark_buffer_dirty(bh_sb);
    printk("clear the %d bit in dentry bitmap and flush to disk\n", _dentry_index);
    
    // clear dentry and flush to disk
    memset(de, 0, sizeof(struct sfs_dentry));
    mark_buffer_dirty(bh);
    dir->i_ctime = dir->i_mtime = CURRENT_TIME;
    dir->i_blocks --;
    mark_inode_dirty(dir);
    inode->i_nlink--;
    inode->i_ctime = dir->i_ctime;
    mark_inode_dirty(inode);
    printk("after unlink, D_SIZE(dir) = %lu, dir->i_blocks = %lu\n",
            D_SIZE(dir), dir->i_blocks);
    error = 0;
    
out_brelse:
    brelse(bh);
    brelse(bh_sb);
    unlock_kernel();
    printk("\n");
    return error;
}

static int sfs_link(struct dentry *old, struct inode *dir, struct dentry *new) {
    struct inode * inode = old->d_inode;
    int err;

    printk("sfs_link(), new->d_name.name = %s, ino = %lu\n",
            new->d_name.name, inode->i_ino);
    
    lock_kernel();
    err = sfs_add_entry(dir, new->d_name.name, new->d_name.len, inode->i_ino);
    if (err) {
        unlock_kernel();
        return err;
    }
    inode->i_nlink++;
    inode->i_ctime = CURRENT_TIME;
    mark_inode_dirty(inode);
    atomic_inc(&inode->i_count);
    d_instantiate(new, inode);
    unlock_kernel();
    printk("\n");
    return 0;
}

static struct dentry * sfs_lookup(struct inode *dir, struct dentry *dentry, struct nameidata *nd) {
    struct inode * inode = NULL;
    struct buffer_head * bh;
    struct sfs_dentry * de;
    struct buffer_head * bh_de_map;
    
    printk("sfs_lookup(), dentry's name is %s\n", dentry->d_name.name);
    
    if (dentry->d_name.len > SFS_NAMELEN)
        return ERR_PTR(-ENAMETOOLONG);
    
    lock_kernel();
    bh = sfs_find_entry(dir, dentry->d_name.name, dentry->d_name.len, &de, &bh_de_map);
    printk("_dentry_index = %d\n", _dentry_index);
    
    if (bh) {
        unsigned long ino = le32_to_cpu(de->ino);
        printk("dentry found, ino = %lu\n", ino);
        brelse(bh);
        brelse(bh_de_map);
        inode = iget(dir->i_sb, ino);
        if (!inode) {
            printk("error: return -EACCES\n");
            unlock_kernel();
            return ERR_PTR(-EACCES);
        }
    }
    
    printk("dentry not found\n");
    unlock_kernel();
    d_add(dentry, inode);
    
    printk("\n");
    return NULL;
}
    
struct inode_operations sfs_dir_inops = {
    .create         = sfs_create,
    .lookup         = sfs_lookup,
    .unlink         = sfs_unlink,
    .link           = sfs_link,
};


static inline int namecmp(int len, const char *name, const char *buffer) {
    int ret = 0;
    if (len < SFS_NAMELEN && buffer[len])
        return ret;
    ret = !memcmp(name, buffer, len);
    printk("ret = %d\n", ret);
    return ret;
}

