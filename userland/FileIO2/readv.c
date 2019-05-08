#include <sys/stat.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int main(int argc, char **argv)
{
	int fd;
	struct iovec iov[3];
	struct stat myStruct;
	int x;
#define STR_SIZE 100
	char str[STR_SIZE];
	size_t numRead, totRequired;

	if(argc != 2 || strcmp(argv[1], "--help") == 0){
		printf("%s file \n", argv[0]);
	}
	
	fd = open(argv[1], O_RDONLY);
	if(fd == -1){
		perror("open");
		exit(1);
	}

	totRequired = 0;

	iov[0].iov_base = &myStruct;
	iov[0].iov_len = sizeof(struct stat);
	totRequired += iov[0].iov_len;

	iov[1].iov_base = &x;
	iov[1].iov_len = sizeof(x);
	totRequired += iov[1].iov_len;

	iov[2].iov_base = str;
	iov[2].iov_len = STR_SIZE;
	totRequired += iov[2].iov_len;

	numRead = readv(fd, iov, 3);
	if(numRead == -1){
		perror("readv");
		exit(1);
	}

	if(numRead < totRequired){
		printf("Read fewer bytes than requested\n");
	}

	printf("total byte requested: %ld; byte read: %ld \n", (long)totRequired, (long)numRead);

	exit(EXIT_SUCCESS);

}
