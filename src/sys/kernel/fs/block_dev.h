#ifndef BLOCK_DEV_H
#define BLOCK_DEV_H

#define SECTOR_SIZE 512

int block_dev_init();
int block_dev_read(unsigned int start_sector, unsigned int count, void *buffer);
int block_dev_write(unsigned int start_sector, unsigned int count, const void *buffer);

#endif
