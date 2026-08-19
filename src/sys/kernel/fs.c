#include "fs.h"

unsigned int parse_octal(const char *str, int len) {
    unsigned int val = 0;
    for (int i = 0; i < len; i++) {
        if (str[i] >= '0' && str[i] <= '7') {
            val = (val * 8) + (str[i] - '0');
        } else if (str[i] == ' ' || str[i] == '\0') {
            continue;
        }
    }
    return val;
}

int my_strncmp(const char *s1, const char *s2, int n) {
    for (int i = 0; i < n; i++) {
        if (s1[i] != s2[i]) return 1;
        if (s1[i] == '\0') break;
    }
    return 0;
}

void my_strncpy(char *dest, const char *src, int n) {
    int i;
    for (i = 0; i < n - 1 && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

#ifdef LINUX_BUILD
#include <stdio.h>
#include <stdlib.h>

char *linux_fs_base = NULL;
char* get_fs_base() {
    if (linux_fs_base == NULL) {
        FILE *f = fopen("build/rootfs.tar", "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long fsize = ftell(f);
            fseek(f, 0, SEEK_SET);
            linux_fs_base = malloc(fsize + 1);
            int bytes = fread(linux_fs_base, 1, fsize, f);
            fclose(f);
        } else {
            printf("Error: Could not load build/rootfs.tar\n");
            exit(1);
        }
    }
    return linux_fs_base;
}
#else
char* get_fs_base() {
    return (char *)0x01000000;
}
#endif

int fs_init() {
    return 1;
}

#define MAX_OVERLAY_FILES 64
typedef struct {
    char name[100];
    int is_dir;
    char data[1024];
    unsigned int size;
} OverlayFile;

OverlayFile overlay_files[MAX_OVERLAY_FILES];
int overlay_count = 0;

int fs_create_file(const char *path, int is_dir) {
    if (overlay_count >= MAX_OVERLAY_FILES) return 0;
    my_strncpy(overlay_files[overlay_count].name, path, 100);
    overlay_files[overlay_count].is_dir = is_dir;
    overlay_files[overlay_count].size = 0;
    overlay_count++;
    return 1;
}

int fs_write_file(const char *path, const char *data, unsigned int size) {
    if (size > 1024) size = 1024;
    
    // Check if it already exists
    for (int i = 0; i < overlay_count; i++) {
        int match = 1;
        int k = 0;
        while (overlay_files[i].name[k] != '\0' || path[k] != '\0') {
            if (overlay_files[i].name[k] != path[k]) { match = 0; break; }
            k++;
        }
        if (match && !overlay_files[i].is_dir) {
            for (unsigned int j = 0; j < size; j++) {
                overlay_files[i].data[j] = data[j];
            }
            overlay_files[i].size = size;
            return 1;
        }
    }
    
    // Create new
    if (overlay_count >= MAX_OVERLAY_FILES) return 0;
    my_strncpy(overlay_files[overlay_count].name, path, 100);
    overlay_files[overlay_count].is_dir = 0;
    for (unsigned int j = 0; j < size; j++) {
        overlay_files[overlay_count].data[j] = data[j];
    }
    overlay_files[overlay_count].size = size;
    overlay_count++;
    return 1;
}

int fs_list_dir(const char *dir_path, FileInfo *files, int max_files) {
    char *ptr = get_fs_base();
    if (!ptr) return 0;
    int count = 0;
    int dir_len = 0;
    while (dir_path[dir_len] != '\0') dir_len++;
    
    // 1. Scan TAR archive
    while (count < max_files) {
        if (ptr[0] == '\0') break;
        
        unsigned int size = parse_octal(ptr + 124, 11);
        char *name = ptr;
        
        if (my_strncmp(name, dir_path, dir_len) == 0) {
            char *rem = name + dir_len;
            if (rem[0] != '\0') {
                int i = 0;
                while (rem[i] != '\0' && rem[i] != '/') i++;
                
                int is_dir = (rem[i] == '/');
                char comp_name[100];
                int comp_len = is_dir ? i + 1 : i;
                my_strncpy(comp_name, rem, comp_len + 1);
                
                int duplicate = 0;
                for (int j = 0; j < count; j++) {
                    int match = 1;
                    int k = 0;
                    while (files[j].name[k] != '\0' || comp_name[k] != '\0') {
                        if (files[j].name[k] != comp_name[k]) { match = 0; break; }
                        k++;
                    }
                    if (match) { duplicate = 1; break; }
                }
                
                if (!duplicate) {
                    my_strncpy(files[count].name, comp_name, comp_len + 1);
                    files[count].size = is_dir ? 0 : size;
                    files[count].is_dir = is_dir;
                    count++;
                }
            }
        }
        ptr += 512 + ((size + 511) / 512) * 512;
    }
    
    // 2. Scan overlay files
    for (int o = 0; o < overlay_count; o++) {
        char *name = overlay_files[o].name;
        if (my_strncmp(name, dir_path, dir_len) == 0) {
            char *rem = name + dir_len;
            if (rem[0] != '\0') {
                int i = 0;
                while (rem[i] != '\0' && rem[i] != '/') i++;
                
                int is_dir = (rem[i] == '/');
                char comp_name[100];
                int comp_len = is_dir ? i + 1 : i;
                my_strncpy(comp_name, rem, comp_len + 1);
                
                int duplicate = 0;
                for (int j = 0; j < count; j++) {
                    int match = 1;
                    int k = 0;
                    while (files[j].name[k] != '\0' || comp_name[k] != '\0') {
                        if (files[j].name[k] != comp_name[k]) { match = 0; break; }
                        k++;
                    }
                    if (match) { duplicate = 1; break; }
                }
                
                if (!duplicate && count < max_files) {
                    my_strncpy(files[count].name, comp_name, comp_len + 1);
                    files[count].size = 0;
                    files[count].is_dir = is_dir;
                    count++;
                }
            }
        }
    }
    return count;
}

char* fs_get_file_data(const char *path, unsigned int *out_size) {
    // Check overlays first
    for (int i = 0; i < overlay_count; i++) {
        int match = 1;
        int k = 0;
        while (overlay_files[i].name[k] != '\0' || path[k] != '\0') {
            if (overlay_files[i].name[k] != path[k]) { match = 0; break; }
            k++;
        }
        if (match && !overlay_files[i].is_dir) {
            if (out_size) *out_size = overlay_files[i].size;
            return overlay_files[i].data;
        }
    }

    char *ptr = get_fs_base();
    if (!ptr) return 0;
    int path_len = 0;
    while (path[path_len] != '\0') path_len++;
    
    while (1) {
        if (ptr[0] == '\0') break;
        unsigned int size = parse_octal(ptr + 124, 11);
        char *name = ptr;
        
        int match = 1;
        for (int k = 0; k < path_len + 1; k++) {
            if (name[k] != path[k]) { match = 0; break; }
        }
        
        if (match) {
            if (out_size) *out_size = size;
            return ptr + 512;
        }
        
        ptr += 512 + ((size + 511) / 512) * 512;
    }
    
    return 0;
}
