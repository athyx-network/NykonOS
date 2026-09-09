#ifndef FAT32_H
#define FAT32_H

#include <stdint.h>

#pragma pack(push, 1)

typedef struct {
    uint8_t  jmp[3];
    char     oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  fat_count;
    uint16_t dir_entries;
    uint16_t total_sectors_16;
    uint8_t  media_descriptor;
    uint16_t sectors_per_fat_16;
    uint16_t sectors_per_track;
    uint16_t heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;

    // FAT32 Extended BPB
    uint32_t sectors_per_fat_32;
    uint16_t flags;
    uint16_t fat_version;
    uint32_t root_cluster;
    uint16_t fs_info_sector;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  nt_flags;
    uint8_t  signature;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];
} FAT32_BPB;

typedef struct {
    char     name[11];
    uint8_t  attr;
    uint8_t  nt_reserved;
    uint8_t  creation_time_tenth;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t last_access_date;
    uint16_t cluster_high;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t cluster_low;
    uint32_t file_size;
} FAT32_DirEntry;

typedef struct {
    uint8_t  order;
    uint16_t name1[5];      // 5 chars
    uint8_t  attr;          // Always 0x0F for LFN
    uint8_t  type;
    uint8_t  checksum;
    uint16_t name2[6];      // 6 chars
    uint16_t zero;
    uint16_t name3[2];      // 2 chars
} FAT32_LFNEntry;

#pragma pack(pop)

#define FAT_ATTR_READ_ONLY 0x01
#define FAT_ATTR_HIDDEN    0x02
#define FAT_ATTR_SYSTEM    0x04
#define FAT_ATTR_VOLUME_ID 0x08
#define FAT_ATTR_DIRECTORY 0x10
#define FAT_ATTR_ARCHIVE   0x20
#define FAT_ATTR_LFN       (FAT_ATTR_READ_ONLY | FAT_ATTR_HIDDEN | FAT_ATTR_SYSTEM | FAT_ATTR_VOLUME_ID)

int fat32_init();
uint32_t fat32_find_file(const char *path, FAT32_DirEntry *out_entry);
int fat32_read_file(FAT32_DirEntry *entry, void *buffer, uint32_t max_len);

struct NYKON_DIR;
int fat32_opendir(const char *path, struct NYKON_DIR *dir);
int fat32_readdir(struct NYKON_DIR *dir);

int fat32_write_file(const char *path, const void *buffer, uint32_t size);

#endif
