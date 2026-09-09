#include "fs.h"
#include "fs/fat32.h"

// Keep original TAR functions
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
        }
    }
    return linux_fs_base;
}
#elif defined(TARGET_BPI)
char* get_fs_base() {
    return (char *)0x46000000;
}
#else
char* get_fs_base() {
    return (char *)0x01000000;
}
#endif

#include "mm.h"
static int fat32_ready = 0;

#define MAX_RAM_FILES 64

typedef struct {
    char path[128];
    char *data;
    unsigned int size;
    int is_dir;
    int in_use;
} RamFile;

static RamFile ram_files[MAX_RAM_FILES];

static void normalize_path(const char *in, char *out, int max_len) {
    out[0] = '\0';
    if (!in) return;
    
    // Skip leading slashes
    while (*in == '/') in++;
    
    int o = 0;
    while (*in != '\0' && o < max_len - 1) {
        if (*in == '/') {
            if (o > 0 && out[o - 1] != '/') {
                out[o++] = '/';
            }
            while (*in == '/') in++;
        } else {
            out[o++] = *in++;
        }
    }
    // Remove trailing slashes
    while (o > 0 && out[o - 1] == '/') o--;
    out[o] = '\0';
}

int fs_init() {
    for (int i = 0; i < MAX_RAM_FILES; i++) {
        ram_files[i].in_use = 0;
        ram_files[i].data = 0;
        ram_files[i].size = 0;
        ram_files[i].is_dir = 0;
    }
    fat32_ready = fat32_init();
    return 1;
}

int fs_create_file(const char *path, int is_dir) {
    if (!path || path[0] == '\0') return 0;
    
    char norm[128];
    normalize_path(path, norm, 128);
    if (norm[0] == '\0') return 0;
    
    // Check if file already exists in RAM
    for (int i = 0; i < MAX_RAM_FILES; i++) {
        if (ram_files[i].in_use) {
            char r_norm[128];
            normalize_path(ram_files[i].path, r_norm, 128);
            if (my_strncmp(r_norm, norm, 128) == 0) {
                ram_files[i].is_dir = is_dir;
                return 1;
            }
        }
    }
    
    // Find empty slot
    for (int i = 0; i < MAX_RAM_FILES; i++) {
        if (!ram_files[i].in_use) {
            my_strncpy(ram_files[i].path, norm, 127);
            ram_files[i].is_dir = is_dir;
            ram_files[i].size = 0;
            ram_files[i].data = 0;
            ram_files[i].in_use = 1;
            return 1;
        }
    }
    return 0;
}

int fs_write_file(const char *path, const char *data, unsigned int size) {
    if (!path) return 0;
    
    char norm[128];
    normalize_path(path, norm, 128);
    if (norm[0] == '\0') return 0;
    
    // Find or create in RAM
    int slot = -1;
    for (int i = 0; i < MAX_RAM_FILES; i++) {
        if (ram_files[i].in_use) {
            char r_norm[128];
            normalize_path(ram_files[i].path, r_norm, 128);
            if (my_strncmp(r_norm, norm, 128) == 0) {
                slot = i;
                break;
            }
        }
    }
    
    if (slot == -1) {
        fs_create_file(norm, 0);
        for (int i = 0; i < MAX_RAM_FILES; i++) {
            if (ram_files[i].in_use) {
                char r_norm[128];
                normalize_path(ram_files[i].path, r_norm, 128);
                if (my_strncmp(r_norm, norm, 128) == 0) {
                    slot = i;
                    break;
                }
            }
        }
    }
    
    if (slot == -1) return 0;
    
    if (ram_files[slot].data) {
        nykon_free(ram_files[slot].data);
        ram_files[slot].data = 0;
    }
    
    if (size > 0 && data) {
        ram_files[slot].data = (char *)nykon_malloc(size + 1);
        if (ram_files[slot].data) {
            for (unsigned int k = 0; k < size; k++) {
                ram_files[slot].data[k] = data[k];
            }
            ram_files[slot].data[size] = '\0';
            ram_files[slot].size = size;
        } else {
            ram_files[slot].size = 0;
            return 0;
        }
    } else {
        ram_files[slot].size = 0;
    }
    
    return 1;
}

NYKON_DIR *fs_opendir(const char *path) {
    static NYKON_DIR dir; 
    dir.is_fat32 = 0;
    
    if (fat32_ready && fat32_opendir(path, &dir)) {
        return &dir;
    }
    
    char *ptr = get_fs_base();
    if (!ptr) return 0;
    
    dir.is_fat32 = 0;
    dir.tar_ptr = ptr;
    dir.eof = 0;
    dir.ram_idx = 0;
    dir.returned_count = 0;
    
    my_strncpy(dir.tar_path, path, 100);
    dir.path_len = 0;
    while (path[dir.path_len] != '\0') dir.path_len++;
    
    return &dir;
}

nykon_dirent *fs_readdir(NYKON_DIR *dirp) {
    if (!dirp || dirp->eof) return 0;
    
    if (dirp->is_fat32) {
        if (fat32_readdir(dirp)) {
            return &dirp->current_ent;
        }
        return 0;
    }
    
    char norm_dir[128];
    normalize_path(dirp->tar_path, norm_dir, 128);
    int dir_len = 0;
    while (norm_dir[dir_len] != '\0') dir_len++;
    
    // 1. Check RAM files
    while (dirp->ram_idx < MAX_RAM_FILES) {
        int idx = dirp->ram_idx++;
        if (!ram_files[idx].in_use) continue;
        
        char norm_item[128];
        normalize_path(ram_files[idx].path, norm_item, 128);
        if (norm_item[0] == '\0') continue;
        
        const char *rem = 0;
        if (dir_len == 0) {
            rem = norm_item;
        } else {
            if (my_strncmp(norm_item, norm_dir, dir_len) == 0 && norm_item[dir_len] == '/') {
                rem = norm_item + dir_len + 1;
            }
        }
        if (!rem || rem[0] == '\0') continue;
        
        int comp_len = 0;
        while (rem[comp_len] != '\0' && rem[comp_len] != '/') comp_len++;
        if (comp_len == 0) continue;
        
        int is_dir = (rem[comp_len] == '/') || ram_files[idx].is_dir;
        
        char comp_name[64];
        if (comp_len > 63) comp_len = 63;
        for (int c = 0; c < comp_len; c++) comp_name[c] = rem[c];
        comp_name[comp_len] = '\0';
        
        if (comp_name[0] == '\0' || (comp_name[0] == '.' && (comp_name[1] == '\0' || (comp_name[1] == '.' && comp_name[2] == '\0')))) {
            continue;
        }
        
        int already = 0;
        for (int k = 0; k < dirp->returned_count; k++) {
            if (my_strncmp(dirp->returned_names[k], comp_name, 64) == 0) {
                already = 1;
                break;
            }
        }
        if (already) continue;
        
        if (dirp->returned_count < 32) {
            my_strncpy(dirp->returned_names[dirp->returned_count++], comp_name, 63);
        }
        
        my_strncpy(dirp->current_ent.d_name, comp_name, 255);
        dirp->current_ent.d_type = is_dir ? DT_DIR : DT_REG;
        dirp->current_ent.d_size = is_dir ? 0 : ram_files[idx].size;
        return &dirp->current_ent;
    }
    
    // 2. Check TAR archive
    while (1) {
        if (dirp->tar_ptr[0] == '\0') {
            dirp->eof = 1;
            return 0;
        }
        unsigned int size = parse_octal(dirp->tar_ptr + 124, 11);
        char *raw_name = dirp->tar_ptr;
        
        char norm_item[128];
        normalize_path(raw_name, norm_item, 128);
        
        if (norm_item[0] != '\0') {
            const char *rem = 0;
            if (dir_len == 0) {
                rem = norm_item;
            } else {
                if (my_strncmp(norm_item, norm_dir, dir_len) == 0 && norm_item[dir_len] == '/') {
                    rem = norm_item + dir_len + 1;
                }
            }
            
            if (rem && rem[0] != '\0') {
                int comp_len = 0;
                while (rem[comp_len] != '\0' && rem[comp_len] != '/') comp_len++;
                
                if (comp_len > 0) {
                    int is_dir = (rem[comp_len] == '/') || (dirp->tar_ptr[156] == '5');
                    
                    char comp_name[64];
                    if (comp_len > 63) comp_len = 63;
                    for (int c = 0; c < comp_len; c++) comp_name[c] = rem[c];
                    comp_name[comp_len] = '\0';
                    
                    if (comp_name[0] != '\0' && !(comp_name[0] == '.' && (comp_name[1] == '\0' || (comp_name[1] == '.' && comp_name[2] == '\0')))) {
                        int already = 0;
                        for (int k = 0; k < dirp->returned_count; k++) {
                            if (my_strncmp(dirp->returned_names[k], comp_name, 64) == 0) {
                                already = 1;
                                break;
                            }
                        }
                        
                        dirp->tar_ptr += 512 + ((size + 511) / 512) * 512;
                        
                        if (!already) {
                            if (dirp->returned_count < 32) {
                                my_strncpy(dirp->returned_names[dirp->returned_count++], comp_name, 63);
                            }
                            my_strncpy(dirp->current_ent.d_name, comp_name, 255);
                            dirp->current_ent.d_type = is_dir ? DT_DIR : DT_REG;
                            dirp->current_ent.d_size = is_dir ? 0 : size;
                            return &dirp->current_ent;
                        }
                        continue;
                    }
                }
            }
        }
        
        dirp->tar_ptr += 512 + ((size + 511) / 512) * 512;
    }
}

int fs_closedir(NYKON_DIR *dirp) {
    if (dirp) dirp->eof = 1;
    return 0;
}

char* fs_get_file_data(const char *path, unsigned int *out_size) {
    if (!path || path[0] == '\0') return 0;
    
    char norm_path[128];
    normalize_path(path, norm_path, 128);
    int norm_len = 0;
    while (norm_path[norm_len] != '\0') norm_len++;
    
    // 1. Check RAM files first
    for (int i = 0; i < MAX_RAM_FILES; i++) {
        if (ram_files[i].in_use && !ram_files[i].is_dir) {
            char r_norm[128];
            normalize_path(ram_files[i].path, r_norm, 128);
            if (my_strncmp(r_norm, norm_path, 128) == 0) {
                if (out_size) *out_size = ram_files[i].size;
                return ram_files[i].data;
            }
        }
    }
    
    // 2. Check FAT32 Disk first!
    if (fat32_ready) {
        FAT32_DirEntry entry;
        if (fat32_find_file(path, &entry)) {
            char *buf = (char *)nykon_malloc(entry.file_size);
            if (buf) {
                fat32_read_file(&entry, buf, entry.file_size);
                if (out_size) *out_size = entry.file_size;
                return buf;
            }
        }
    }

    // 3. Fallback to TAR initrd
    char *ptr = get_fs_base();
    if (!ptr) return 0;
    
    while (1) {
        if (ptr[0] == '\0') break;
        unsigned int size = parse_octal(ptr + 124, 11);
        char *raw_name = ptr;
        
        char t_norm[128];
        normalize_path(raw_name, t_norm, 128);
        
        if (my_strncmp(t_norm, norm_path, 128) == 0) {
            if (out_size) *out_size = size;
            return ptr + 512;
        }
        
        ptr += 512 + ((size + 511) / 512) * 512;
    }
    return 0;
}
