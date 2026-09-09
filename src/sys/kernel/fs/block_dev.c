#include "block_dev.h"

#ifdef LINUX_BUILD
#include <stdio.h>
#include <stdlib.h>

static FILE *disk_img = NULL;

int block_dev_init() {
    disk_img = fopen("build/disk.img", "rb+");
    if (!disk_img) {
        printf("Error: Could not open build/disk.img\n");
        return 0;
    }
    return 1;
}

int block_dev_read(unsigned int start_sector, unsigned int count, void *buffer) {
    if (!disk_img) return 0;
    fseek(disk_img, start_sector * SECTOR_SIZE, SEEK_SET);
    size_t read = fread(buffer, SECTOR_SIZE, count, disk_img);
    return read == count;
}

int block_dev_write(unsigned int start_sector, unsigned int count, const void *buffer) {
    if (!disk_img) return 0;
    fseek(disk_img, start_sector * SECTOR_SIZE, SEEK_SET);
    size_t written = fwrite(buffer, SECTOR_SIZE, count, disk_img);
    fflush(disk_img);
    return written == count;
}
#else

int block_dev_init() {
    // Stub for ARM bare metal
    return 1;
}

int block_dev_read(unsigned int start_sector, unsigned int count, void *buffer) {
    // Stub for ARM bare metal
    return 0;
}

int block_dev_write(unsigned int start_sector, unsigned int count, const void *buffer) {
    // Stub for ARM bare metal
    return 0;
}
#endif
