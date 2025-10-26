#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

int main() {
    int fd1 = open("/dev/scull_ring_buffer0", O_RDONLY | O_NONBLOCK);
    int fd2 = open("/dev/scull_ring_buffer1", O_RDONLY | O_NONBLOCK);

    if (fd1 < 0 || fd2 < 0) {
        perror("open");
        exit(1);
    }

    struct buffer_info {
        int data_size;
        int buffer_size;
        int read_index;
        int write_index;
    } info1, info2;

    while (1) {
        // Используем ioctl для получения размера данных
        if (ioctl(fd1, 1, &info1) == 0) {
            printf("scull1 buffer data size: %d bytes\n", info1.data_size);
            printf("scull1 buffer size: %d bytes\n", info1.buffer_size);
            printf("scull1 read_index: %d\n", info1.read_index);
            printf("scull1 write_index: %d\n\n", info1.write_index);

        }
        if (ioctl(fd2, 1, &info2) == 0) {
            printf("scull2 buffer data size: %d bytes\n", info2.data_size);
            printf("scull2 buffer size: %d bytes\n", info2.buffer_size);
            printf("scull2 read_index: %d\n", info2.read_index);
            printf("scull2 write_index: %d\n", info2.write_index);

        }
        printf("---\n");
        sleep(2);
    }

    close(fd1);
    close(fd2);
    return 0;
}