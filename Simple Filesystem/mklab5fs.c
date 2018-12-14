#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <asm/bitops.h>

#include "sfs.h"

int main(int argc, char **argv){
    char *fsname, *device;
    int total_bytes,ino_nums,ino_bytes,total_blocks,ino_blocks,data_blocks,dentry_blocks,dentrymap_blocks,datamap_blocks,inomap_blocks;
    int fd;
    uint32_t first_block;
    struct sfs_sb sb;
    struct sfs_ino ri;
    struct sfs_dentry de;
    struct sfs_datamap datamap;
    time_t now;
    int c,i,len;
    struct stat buf;
    int _i;
    int pos;
    if (argc != 2) {
        printf("format error: ./mklab5fs image!\n");
        return -1;
    }

    device = argv[1];
    printf("device name: %s\n", device);
        
    fd = open(device,O_RDWR);
    if (fd < 0) {
        printf("OPEN ERROR!\n");
        return -1;
    } else {
        printf("OPEN successfully\n");
        ino_nums = SFS_INO_NUM; //1024
        ino_bytes = ino_nums * sizeof(struct sfs_ino);
        fstat(fd, &buf);
        total_bytes = buf.st_size;
        if (total_bytes > SFS_MAX_IMAGESIZE){
            printf("IMAGE TOO LARGE! max file system size: 16M!\n");
            return -1;
        }
        if (total_bytes < SFS_MIN_IMAGESIZE){
            printf("IMAGE TOO SMALL! min file system size: 1M!\n");
            return -1;
        }
        
        total_blocks = total_bytes/SFS_BLOCKSIZE;

        // super block info
        memset(&sb, 0,sizeof(sb));
        snprintf(sb.s_fsname,SFS_NAMELEN,device);
        sb.s_magic = (MAGIC);
        sb.s_start=DATA_START;
        sb.s_end=total_blocks;
        printf("s_magic: %lx, s_start: %d, s_end: %d, s_fsname: %s\n",\
                sb.s_magic, sb.s_start, sb.s_end, sb.s_fsname); 
        
        //maps info
        memset(&datamap, 0, sizeof(datamap));

        for (i=0; i<=1; i++){ 
            set_bit(i, sb.dentry_map);
        }
        //first three inode is reserved
        set_bit(0, sb.i_nodemap);
        set_bit(1, sb.i_nodemap);
        set_bit(2, sb.i_nodemap);
        printf("dentrymap[0]: %x, inomap[0]: %x\n", sb.dentry_map[0], sb.i_nodemap[0]);
        
        //write to file
        if (write(fd, &sb, sizeof(sb)) != sizeof(sb)){
            printf("error writing superblock\n");
            return -1;
        }
        printf("write superblock successfully, %d\n", sizeof(sb));
        
        //write maps
        if (write(fd, &datamap, sizeof(datamap)) != sizeof(datamap)){
            printf("error writing datamap\n");
            return -1;
        }
        printf("write datamap %d bytes successfully\n", sizeof(datamap));
        
        memset(&ri, 0, sizeof(ri));
        
        //write root inode, first two inode is no use
        if (write(fd, &ri, sizeof(ri)) != sizeof(ri)){
            printf("error writing root inode\n");
            return -1;
        }
        if (write(fd, &ri, sizeof(ri)) != sizeof(ri)){
            printf("error writing root inode\n");
            return -1;
        }
        // root inode info
        ri.di_ino = SFS_ROOT_INO;
        ri.di_endsize = 0;
        ri.is_dir = 1;
        ri.di_mode = S_IFDIR | 0755;
        ri.di_nlink = 2;
        ri.di_blocknum = 2;
        ri.di_uid = 0;
        ri.di_gid = 0;
        time(&now);
        ri.di_atime = ri.di_mtime = ri.di_ctime = now;
        
        if (write(fd, &ri, sizeof(ri)) != sizeof(ri)){
            printf("error writing root inode\n");
            return -1;
        }
        printf("write root inode successfully\n");
        //lseek to start of dentry blocks
        if ((pos=lseek(fd, (DENTRY_BLOCKS_START) * SFS_BLOCKSIZE, SEEK_SET)) == -1){
            printf("lseek error\n");
            return -1;
        }
        printf("lseek successfully, %d\n",pos);
        
        memset(&de, 0, sizeof(de));
        de.ino = SFS_ROOT_INO;
        memcpy(de.name,".",1);
        if (write(fd, &de, sizeof(de)) != sizeof(de)){
            printf("error writing dentry \".\" \n");
            return -1;
        }
        printf("write dentry: \".\" successfully\n");
        
        memcpy(de.name,"..",2);
        if (write(fd, &de, sizeof(de)) != sizeof(de)){
            printf("error writing dentry \"..\" \n");
            return -1;
        }
        printf("write dentry: \"..\" successfully\n");       
        close(fd);
    }
}