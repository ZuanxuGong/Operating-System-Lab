#include <linux/errno.h>
#include <sys/syscall.h>
#include <linux/unistd.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define __NR_cp_range 285
_syscall2(long, cp_range, unsigned long *,arg1,unsigned long *,arg2);
#define __NR_inc_cp_range 286
_syscall2(long,inc_cp_range,unsigned long *,arg1,unsigned long *,arg2);
#define PAGENUM 100
int main(int argc, char *argv[]) {
    unsigned long start, end;
    size_t size = sizeof(char) * PAGENUM * 4096;
    char *p = malloc(size);
    int inc=0;
    int ret;
    if(strcmp(argv[1], "1") == 0){
       start = 0;
       end = 0x0BFFFFFFF;
	//end=0xC0000000;
    }else{
       start = (unsigned long) p;
       end = start + size ;
    }
    //use inc checkpoint
    if(argc>2){
       inc=1;
	}
    printf("cp_range : 0x%08x  to 0x%08x inc %d\n", start, end,inc);
    if(inc){
   memset(p,0,size);
    ret=inc_cp_range(&start,&end);
    }else{
    memset(p,0,size);
    ret=cp_range(&start, &end);
    }
    if(ret==-1){
      printf("can not copy kernel space\n");
      return -1;
	}
    printf("First cp_range finished: 0x%08x  to 0x%08x\n", start, end);
   if(inc){
	memset(p,'2',2*4096);
	ret=inc_cp_range(&start,&end);
    }else{ 
 	 memset(p,'2',2*4096);
    ret=cp_range(&start, &end);
    }
    printf("second cp_range : 0x%08x  to 0x%08x inc %d\n", start, end,inc);
    free(p);
    return 0;
}
