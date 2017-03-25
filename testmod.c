#include <stdio.h>
#include <linux/ioctl.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#define CDRV_IOC_MAGIC 'Z'
#define ASP_CHGACCDIR _IOW(CDRV_IOC_MAGIC, 1, int)

#define BUFSIZE 128

int main(int argc, char *argv[]) {
	int fd, rv, lop, pos;
	char buf[128] = {0};
	char op;

	fd = open(argv[1], O_RDWR);
	if(fd < 0) {
		perror("open");
		return -1;
	}

	printf("Enter operation : ");
	while(op = getchar()) {
		//scanf("%c", &op);
		switch(op) {
			case 'w':
				printf("Enter data :\n");
				scanf(" %[^\n]", buf);
				rv = write(fd, buf, strlen(buf));
				memset(buf, 0, sizeof(buf));
				printf("Wrote %d bytes\n", rv);
			break;

			case 'r':
				printf("Read data :\n");
				do {
				rv = read(fd, buf, sizeof(buf));
				printf("%s", buf);
				memset(buf, 0, sizeof(buf));
				} while(rv > 0);
			break;

			case 's':
				printf("Enter SEEK_SET, SEEK_CUR, SEEK_END : ");
				scanf("%d", &lop);
				printf("Enter pos : ");
				scanf("%d", &pos);
				rv = lseek(fd, pos, lop);
				printf("Returned %d\n", rv);
			break;

			case 'i':
				printf("Enter new direction : ");
				scanf("%d", &lop);
				rv = ioctl(fd, ASP_CHGACCDIR, &lop);
				printf("Returned %d\n", rv);
			break;

			case 'e':
				goto done;
			break;

			case '\n':
			continue;

			default:
				printf("invalid operation\n");
		}
		printf("\n\nEnter operation : ");
	}
done:
	close(fd);
}
