#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


int create_file(const char *path)
{
	int fd = 0;
	
	fd = open(path, O_CREATE|O_TRUNC|O_RDWR, 0644);
	if(fd < 0){
		printf("open %s is error!\n", argv[1]);
	}

	return fd;
}

int main(int argc, char **argv)
{
	int fd;

	fd = create_file(argv[1]);

	return 0;
}

