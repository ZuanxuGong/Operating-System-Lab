#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/smp_lock.h>

#include <linux/namei.h>


#include "sfs.h"

struct file_operations sfs_file_operations = {
    .llseek     = generic_file_llseek,
    .read       = generic_file_read,
    .write      = generic_file_write,
    .mmap       = generic_file_mmap,
    .sendfile   = generic_file_sendfile,
};

static int sfs_get_block(struct inode *inode, sector_t block,
                         struct buffer_head *bh_result, int create) {
    long phys;
    int block_data_start = DATA_START; // 297
    struct super_block *sb = inode->i_sb;
    struct sfs_inode_info *bi = INODE_TO_SFSINODE(inode);

    int _block, index;
    struct buffer_head * bh_data_map;
    struct sfs_datamap * data_map_p;
    struct buffer_head *bh_sb;
    printk("sfs_get_block(), block = %llu\n", block);
    
    if (block < 0) {
        printk("error: block < 0\n");
        return -EIO;
    }

    phys = bi->bi_blocks[block];
    if (!create) {
        if (phys > 0) {
            printk("\tc=%d, b=%llu, phys=%ld (granted)\n", create, block, phys);
            map_bh(bh_result, sb, phys);
        }
        printk("\n");
        return 0;
    }
    
    if (bi->bi_blocks[0] > 0 && phys > 0) {
        printk("\tc=%d, b=%llu, phys=%ld (interim block granted)\n", 
            create, block, phys);
        map_bh(bh_result, sb, phys);
        printk("\n");
        return 0;
    }

    lock_kernel();
    bh_data_map = sb_bread(inode->i_sb, DATAMAP_START);
    if (!bh_data_map) {
        printk("Unable to read data map\n");
    }
    //todo:  should check data index here
    bh_sb=get_super_bh(inode->i_sb);
    struct sfs_sb *super_block=(struct sfs_sb *)(bh_sb->b_data);
    int max_data_block=super_block->s_end-super_block->s_start;
    brelse(bh_sb);
    data_map_p = (struct sfs_datamap *) (bh_data_map->b_data);
    //here should be the max data size
    index = find_first_zero_bit(data_map_p, max_data_block);
    //todo if all occupied, should return error
    printk("write block the first zero bit index is %d\n", index);
    if(index==max_data_block){
        printk("error, no more space");
        brelse(bh_data_map);
        unlock_kernel();
        return -EIO;
    }
    // flush data bitmap to disk
    set_bit(index,  data_map_p);
    mark_buffer_dirty(bh_data_map);
    brelse(bh_data_map);
    printk("flush data bitmap to disk\n");
    
    phys = block_data_start + index;
    printk("\tc=%d, b=%llu, phys=%ld (moved)\n", create, block, phys);
    
    bi->bi_blocks[block] = phys;
  
    mark_inode_dirty(inode);
    map_bh(bh_result, sb, phys);
    
    unlock_kernel();
    printk("\n");
    return 0;
}

static int sfs_writepage(struct page *page, struct writeback_control *wbc) {
    return block_write_full_page(page, sfs_get_block, wbc);
}

static int sfs_readpage(struct file *file, struct page *page) {
    return block_read_full_page(page, sfs_get_block);
}

static int sfs_prepare_write(struct file *file, struct page *page, unsigned from, unsigned to) {
    return block_prepare_write(page, from, to, sfs_get_block);
}

struct address_space_operations sfs_aops = {
    .readpage   = sfs_readpage,
    .writepage  = sfs_writepage,
    .sync_page  = block_sync_page,
    .prepare_write  = sfs_prepare_write,
    .commit_write   = generic_commit_write,
};

struct inode_operations sfs_file_inops;