#include "../fs.h"
#include "fat32.h"
#include "block_dev.h"

// Define a simple memset and memcmp here to avoid dependencies
static void my_memset(void *dst, int val, int len) {
    char *d = (char*)dst;
    while(len--) *d++ = val;
}

static FAT32_BPB bpb;
static uint32_t fat_start_sector;
static uint32_t data_start_sector;

int fat32_init() {
    if (!block_dev_init()) return 0;
    
    uint8_t boot_sector[512];
    if (!block_dev_read(0, 1, boot_sector)) return 0;
    
    // Copy to BPB struct (assuming little endian)
    for (int i = 0; i < sizeof(FAT32_BPB); i++) {
        ((uint8_t*)&bpb)[i] = boot_sector[i];
    }
    
    if (bpb.signature != 0x29 && bpb.signature != 0x28) {
        // Not a standard FAT32 signature, but some formatters omit it.
        // We'll proceed but this could be a point of failure.
    }
    
    fat_start_sector = bpb.reserved_sectors;
    uint32_t root_dir_sectors = ((bpb.dir_entries * 32) + 511) >> 9;
    
    uint32_t fat_size = bpb.sectors_per_fat_16 ? bpb.sectors_per_fat_16 : bpb.sectors_per_fat_32;
    data_start_sector = bpb.reserved_sectors + (bpb.fat_count * fat_size) + root_dir_sectors;
    
    return 1;
}

static uint32_t get_cluster_sector(uint32_t cluster) {
    return data_start_sector + ((cluster - 2) * bpb.sectors_per_cluster);
}

static void to_dos_name(const char *path, int len, char *dos_name) {
    int i = 0, j = 0;
    for (int k=0; k<11; k++) dos_name[k] = ' ';
    while (i < len && path[i] != '.' && j < 8) {
        dos_name[j++] = (path[i] >= 'a' && path[i] <= 'z') ? path[i] - 32 : path[i];
        i++;
    }
    while (i < len && path[i] != '.') i++; // skip remainder of name if > 8
    if (i < len && path[i] == '.') {
        i++;
        j = 8;
        while (i < len && j < 11) {
            dos_name[j++] = (path[i] >= 'a' && path[i] <= 'z') ? path[i] - 32 : path[i];
            i++;
        }
    }
}

uint32_t fat32_find_file(const char *path, FAT32_DirEntry *out_entry) {
    if (!path || !path[0]) return 0;
    if (path[0] == '/') path++; // Skip leading slash
    
    uint32_t current_cluster = bpb.root_cluster;
    
    while (*path) {
        // Extract the next path segment
        int seg_len = 0;
        while (path[seg_len] && path[seg_len] != '/') seg_len++;
        
        char dos_name[11];
        to_dos_name(path, seg_len, dos_name);
        
        int found = 0;
        uint32_t search_cluster = current_cluster;
        
        while (search_cluster < 0x0FFFFFF8 && !found) {
            uint32_t sector = get_cluster_sector(search_cluster);
            for (int i = 0; i < bpb.sectors_per_cluster && !found; i++) {
                uint8_t buf[512];
                block_dev_read(sector + i, 1, buf);
                
                FAT32_DirEntry *entries = (FAT32_DirEntry*)buf;
                for (int e = 0; e < 512 / sizeof(FAT32_DirEntry); e++) {
                    if (entries[e].name[0] == 0x00) break; // End of dir
                    if (entries[e].name[0] == 0xE5) continue; // Deleted
                    if (entries[e].attr & FAT_ATTR_LFN) continue; // Skip LFN
                    
                    int match = 1;
                    for (int k = 0; k < 11; k++) {
                        if (entries[e].name[k] != dos_name[k]) {
                            match = 0; break;
                        }
                    }
                    
                    if (match) {
                        *out_entry = entries[e];
                        found = 1;
                        break;
                    }
                }
            }
            if (!found) {
                // Next cluster in chain
                uint32_t fat_sec = fat_start_sector + ((search_cluster * 4) >> 9);
                uint32_t fat_ent_offset = (search_cluster * 4) & 511;
                uint8_t fbuf[512];
                block_dev_read(fat_sec, 1, fbuf);
                search_cluster = *(uint32_t*)(fbuf + fat_ent_offset) & 0x0FFFFFFF;
            }
        }
        
        if (!found) return 0; // Segment not found
        
        path += seg_len;
        if (*path == '/') path++;
        
        if (*path) {
            // There are more segments, so this MUST be a directory
            if (!(out_entry->attr & FAT_ATTR_DIRECTORY)) return 0;
            current_cluster = (out_entry->cluster_high << 16) | out_entry->cluster_low;
            if (current_cluster == 0) current_cluster = bpb.root_cluster;
        }
    }
    
    return 1;
}

static void from_dos_name(const char *dos_name, char *name) {
    int j = 0;
    for (int i = 0; i < 8; i++) {
        if (dos_name[i] != ' ') name[j++] = (dos_name[i] >= 'A' && dos_name[i] <= 'Z') ? dos_name[i] + 32 : dos_name[i];
    }
    if (dos_name[8] != ' ') {
        name[j++] = '.';
        for (int i = 8; i < 11; i++) {
            if (dos_name[i] != ' ') name[j++] = (dos_name[i] >= 'A' && dos_name[i] <= 'Z') ? dos_name[i] + 32 : dos_name[i];
        }
    }
    name[j] = '\0';
}

int fat32_opendir(const char *path, NYKON_DIR *dir) {
    if (!path) return 0;
    dir->is_fat32 = 1;
    dir->eof = 0;
    dir->sector_offset = 0;
    dir->entry_offset = 0;
    dir->lfn_len = 0;
    for (int i = 0; i < 256; i++) dir->lfn_buf[i] = '\0';
    
    // For now, only support root directory. 
    // Traversing subdirectories would require reading directory files and finding the cluster.
    // Given the constraints, we will map '/' to the root cluster.
    if (path[0] == '/' && path[1] == '\0') {
        dir->current_cluster = bpb.root_cluster;
        return 1;
    }
    
    // Check if it's a subdirectory in the root
    FAT32_DirEntry entry;
    if (fat32_find_file(path, &entry)) {
        if (entry.attr & FAT_ATTR_DIRECTORY) {
            dir->current_cluster = (entry.cluster_high << 16) | entry.cluster_low;
            if (dir->current_cluster == 0) dir->current_cluster = bpb.root_cluster;
            return 1;
        }
    }
    
    return 0;
}

int fat32_readdir(NYKON_DIR *dir) {
    if (dir->eof || dir->current_cluster >= 0x0FFFFFF8) return 0;
    
    while (dir->current_cluster < 0x0FFFFFF8) {
        uint32_t sector = get_cluster_sector(dir->current_cluster) + dir->sector_offset;
        uint8_t buf[512];
        block_dev_read(sector, 1, buf);
        
        FAT32_DirEntry *entries = (FAT32_DirEntry*)buf;
        
        while (dir->entry_offset < 512 / sizeof(FAT32_DirEntry)) {
            FAT32_DirEntry *e = &entries[dir->entry_offset];
            dir->entry_offset++;
            
            if (e->name[0] == 0x00) {
                dir->eof = 1;
                return 0; // End of directory
            }
            if (e->name[0] == 0xE5) {
                dir->lfn_len = 0; // Reset LFN on deleted file
                continue; 
            }
            
            if (e->attr == FAT_ATTR_LFN) {
                FAT32_LFNEntry *lfn = (FAT32_LFNEntry *)e;
                int idx = ((lfn->order & 0x3F) - 1) * 13;
                if (idx >= 0 && idx < 256 - 13) {
                    // Extract 13 UCS-2 chars to ASCII
                    char chars[13];
                    for (int i=0; i<5; i++) chars[i] = (char)(lfn->name1[i] & 0xFF);
                    for (int i=0; i<6; i++) chars[5+i] = (char)(lfn->name2[i] & 0xFF);
                    for (int i=0; i<2; i++) chars[11+i] = (char)(lfn->name3[i] & 0xFF);
                    
                    for (int i=0; i<13; i++) {
                        if (chars[i] == (char)0xFF || chars[i] == '\0') break;
                        dir->lfn_buf[idx + i] = chars[i];
                        if (idx + i >= dir->lfn_len) dir->lfn_len = idx + i + 1;
                    }
                    dir->lfn_buf[dir->lfn_len] = '\0';
                }
                continue;
            }
            
            if (e->attr & FAT_ATTR_VOLUME_ID) {
                dir->lfn_len = 0;
                continue; // Skip Volume ID
            }
            
            // Found a valid entry
            if (dir->lfn_len > 0) {
                // Use LFN buffer
                int k = 0;
                while (k < dir->lfn_len && k < 255) {
                    dir->current_ent.d_name[k] = dir->lfn_buf[k];
                    k++;
                }
                dir->current_ent.d_name[k] = '\0';
                dir->lfn_len = 0; // Reset for next file
            } else {
                // Fallback to 8.3 short name
                from_dos_name(e->name, dir->current_ent.d_name);
            }
            
            dir->current_ent.d_type = (e->attr & FAT_ATTR_DIRECTORY) ? DT_DIR : DT_REG;
            dir->current_ent.d_size = e->file_size;
            return 1;
        }
        
        // Move to next sector in cluster
        dir->sector_offset++;
        dir->entry_offset = 0;
        
        if (dir->sector_offset >= bpb.sectors_per_cluster) {
            // Next cluster
            uint32_t fat_sec = fat_start_sector + ((dir->current_cluster * 4) >> 9);
            uint32_t fat_ent_offset = (dir->current_cluster * 4) & 511;
            uint8_t fbuf[512];
            block_dev_read(fat_sec, 1, fbuf);
            dir->current_cluster = *(uint32_t*)(fbuf + fat_ent_offset) & 0x0FFFFFFF;
            dir->sector_offset = 0;
        }
    }
    
    dir->eof = 1;
    return 0;
}



int fat32_read_file(FAT32_DirEntry *entry, void *buffer, uint32_t max_len) {
    uint32_t cluster = (entry->cluster_high << 16) | entry->cluster_low;
    uint32_t bytes_read = 0;
    
    while (cluster < 0x0FFFFFF8 && bytes_read < entry->file_size && bytes_read < max_len) {
        uint32_t sector = get_cluster_sector(cluster);
        
        // Read sectors for this cluster
        for (int i = 0; i < bpb.sectors_per_cluster; i++) {
            if (bytes_read >= entry->file_size || bytes_read >= max_len) break;
            
            uint8_t sec_buf[512];
            block_dev_read(sector + i, 1, sec_buf);
            
            uint32_t to_copy = 512;
            if (entry->file_size - bytes_read < to_copy) to_copy = entry->file_size - bytes_read;
            if (max_len - bytes_read < to_copy) to_copy = max_len - bytes_read;
            
            for(uint32_t j=0; j<to_copy; j++) {
                ((uint8_t*)buffer)[bytes_read++] = sec_buf[j];
            }
        }
        
        // Read next cluster from FAT table (simplified, assuming FAT32)
        uint32_t fat_sec = fat_start_sector + ((cluster * 4) >> 9);
        uint32_t fat_ent_offset = (cluster * 4) & 511;
        uint8_t fbuf[512];
        block_dev_read(fat_sec, 1, fbuf);
        uint32_t next_cluster = *(uint32_t*)(fbuf + fat_ent_offset) & 0x0FFFFFFF;
        cluster = next_cluster;
    }
    
    return bytes_read;
}

static uint32_t fat32_read_fat_entry(uint32_t cluster) {
    uint32_t fat_sec = fat_start_sector + ((cluster * 4) >> 9);
    uint32_t fat_ent_offset = (cluster * 4) & 511;
    uint8_t fbuf[512];
    block_dev_read(fat_sec, 1, fbuf);
    return *(uint32_t*)(fbuf + fat_ent_offset) & 0x0FFFFFFF;
}

static void fat32_write_fat_entry(uint32_t cluster, uint32_t value) {
    uint32_t fat_sec = fat_start_sector + ((cluster * 4) >> 9);
    uint32_t fat_ent_offset = (cluster * 4) & 511;
    uint8_t fbuf[512];
    block_dev_read(fat_sec, 1, fbuf);
    uint32_t *entry = (uint32_t*)(fbuf + fat_ent_offset);
    *entry = (*entry & 0xF0000000) | (value & 0x0FFFFFFF);
    block_dev_write(fat_sec, 1, fbuf);
}

static uint32_t fat32_find_free_cluster() {
    // Basic linear scan starting from cluster 2
    for (uint32_t c = 2; c < 0x0FFFFFF0; c++) {
        if (fat32_read_fat_entry(c) == 0) {
            return c;
        }
    }
    return 0; // Disk full
}

static uint32_t fat32_allocate_chain(uint32_t num_clusters) {
    if (num_clusters == 0) return 0;
    uint32_t start_cluster = fat32_find_free_cluster();
    if (start_cluster == 0) return 0;
    
    uint32_t current = start_cluster;
    for (uint32_t i = 1; i < num_clusters; i++) {
        fat32_write_fat_entry(current, 0x0FFFFFFF); // Mark as EOF temporarily
        uint32_t next = fat32_find_free_cluster();
        if (next == 0) return 0; // Out of space
        fat32_write_fat_entry(current, next);
        current = next;
    }
    fat32_write_fat_entry(current, 0x0FFFFFF8); // Mark end of chain
    return start_cluster;
}

int fat32_write_file(const char *path, const void *buffer, uint32_t size) {
    
    // For now, only write to root directory
    if (path[0] == '/') path++;
    
    // Check if it already exists (we won't handle overwrite/truncate for now, just fail)
    FAT32_DirEntry temp;
    if (fat32_find_file(path, &temp)) return 0; // File already exists
    
    // Allocate clusters
    uint32_t num_clusters = (size + (bpb.sectors_per_cluster * 512) - 1) / (bpb.sectors_per_cluster * 512);
    if (num_clusters == 0) num_clusters = 1; // Need at least 1 cluster for a 0-byte file
    uint32_t start_cluster = fat32_allocate_chain(num_clusters);
    if (start_cluster == 0) return 0;
    
    // Write data
    uint32_t bytes_written = 0;
    uint32_t current_cluster = start_cluster;
    uint8_t *data_ptr = (uint8_t *)buffer;
    
    while (current_cluster < 0x0FFFFFF8 && bytes_written < size) {
        uint32_t sector = get_cluster_sector(current_cluster);
        for (uint32_t i = 0; i < bpb.sectors_per_cluster && bytes_written < size; i++) {
            uint8_t sec_buf[512] = {0};
            uint32_t chunk = size - bytes_written;
            if (chunk > 512) chunk = 512;
            
            for (uint32_t j = 0; j < chunk; j++) {
                sec_buf[j] = data_ptr[bytes_written + j];
            }
            
            block_dev_write(sector + i, 1, sec_buf);
            bytes_written += chunk;
        }
        current_cluster = fat32_read_fat_entry(current_cluster);
    }
    
    // Create directory entry in root
    uint32_t dir_cluster = bpb.root_cluster;
    while (dir_cluster < 0x0FFFFFF8) {
        uint32_t sector = get_cluster_sector(dir_cluster);
        for (uint32_t s = 0; s < bpb.sectors_per_cluster; s++) {
            uint8_t sec_buf[512];
            block_dev_read(sector + s, 1, sec_buf);
            FAT32_DirEntry *entries = (FAT32_DirEntry *)sec_buf;
            
            for (int e = 0; e < 512 / sizeof(FAT32_DirEntry); e++) {
                if (entries[e].name[0] == 0x00 || entries[e].name[0] == 0xE5) {
                    // Empty slot!
                    int name_len = 0;
                    while (path[name_len]) name_len++;
                    to_dos_name(path, name_len, entries[e].name);
                    entries[e].attr = 0;
                    entries[e].cluster_high = (start_cluster >> 16) & 0xFFFF;
                    entries[e].cluster_low = start_cluster & 0xFFFF;
                    entries[e].file_size = size;
                    // Zero out dates for simplicity
                    entries[e].creation_time = 0;
                    entries[e].creation_date = 0;
                    entries[e].write_time = 0;
                    entries[e].write_date = 0;
                    entries[e].last_access_date = 0;
                    
                    block_dev_write(sector + s, 1, sec_buf);
                    return 1;
                }
            }
        }
        dir_cluster = fat32_read_fat_entry(dir_cluster);
    }
    
    return 0; // Root dir full
}
