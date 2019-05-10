#define _LARGEFILE64_SOURCE
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
	int fd;
	off64_t off;

	if(argc != 3 || strcmp(argv[1], "--help") == 0){
		printf("%s pathname offset\n", argv[0]);
	}

	fd = open64(argv[1], O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	if(fd == -1){
		perror("open64");
		exit(1);
	}

	off = atoll(argv[2]);
	if(lseek(fd, off, SEEK_SET) == -1){
		perror("lseek");
		exit(1);
	}

	if(write(fd, "test", 4) == -1){
		perror("write");
	}

	exit(0);
}
