#ifndef FS_H
#define FS_H

#define MAX_FILES 32

typedef struct {
    char name[100];
    unsigned int size;
    int is_dir;
} FileInfo; // Keep this for now if any app relies on it heavily, but we'll introduce dirent

#define DT_DIR 4
#define DT_REG 8

typedef struct {
    char d_name[256];
    int d_type;
    unsigned int d_size;
} nykon_dirent;

typedef struct NYKON_DIR {
    unsigned int current_cluster;
    unsigned int sector_offset;
    unsigned int entry_offset;
    nykon_dirent current_ent;
    int is_fat32;
    int eof;
    // For FAT32 LFN support
    char lfn_buf[256];
    int lfn_len;
    // For TAR fallback and RAM overlay
    char *tar_ptr;
    int path_len;
    char tar_path[100];
    int ram_idx;
    char returned_names[32][64];
    int returned_count;
} NYKON_DIR;

int fs_init();
int fs_create_file(const char *path, int is_dir);
int fs_write_file(const char *path, const char *data, unsigned int size);
int my_strncmp(const char *s1, const char *s2, int n);
void my_strncpy(char *dest, const char *src, int n);
char* fs_get_file_data(const char *path, unsigned int *out_size);

NYKON_DIR *fs_opendir(const char *path);
nykon_dirent *fs_readdir(NYKON_DIR *dirp);
int fs_closedir(NYKON_DIR *dirp);

#endif
