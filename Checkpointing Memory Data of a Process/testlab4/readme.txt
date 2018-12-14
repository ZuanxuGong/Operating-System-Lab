list
   build.sh  build executable
   cp_range_test.c    test program
usage:
	./build.sh
    ./cp_range_test 0   (test normal checkpoint)
    ./cp_range_test 1    (test large address 0-0xBFFFFFFF)
    normal checkpoint algorithm will generate file as cp_pid_no 

    ./cp_range_test 0 inc   (test incremental checkpoing algo)
    ./cp_range_test 1 inc   (test large address under incremental) 
    incremental algorithm generates file name  inc_pid_no
