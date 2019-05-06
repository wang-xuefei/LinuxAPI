#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

int main(int argc, char **argv)
{
	int fd = 0;

	fd = open(argv[1], O_CREATE|O_TRUNC|O_RDWR, 0644);
	if(fd < 0){
		printf("open %s is error!\n", argv[1]);
	}
	
	return 0;
}
