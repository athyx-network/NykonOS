#ifndef FS_H
#define FS_H

#define MAX_FILES 32

typedef struct {
    char name[100];
    unsigned int size;
    int is_dir;
} FileInfo;

int fs_init();
int fs_list_dir(const char *dir_path, FileInfo *files, int max_files);
int fs_create_file(const char *path, int is_dir);
int my_strncmp(const char *s1, const char *s2, int n);
char* fs_get_file_data(const char *path, unsigned int *out_size);

#endif
