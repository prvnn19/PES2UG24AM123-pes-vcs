// tree.c — Tree object serialization and construction

#include "tree.h"
#include "index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

// Forward declaration to link with object.c
extern int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);

// ─── Mode Constants ─────────────────────────────────────────────────────────

#define MODE_FILE      0100644
#define MODE_EXEC      0100755
#define MODE_DIR       0040000

// ─── PROVIDED ───────────────────────────────────────────────────────────────

// Determine the object mode for a filesystem path.
uint32_t get_file_mode(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return 0;

    if (S_ISDIR(st.st_mode))  return MODE_DIR;
    if (st.st_mode & S_IXUSR) return MODE_EXEC;
    return MODE_FILE;
}

// Parse binary tree data into a Tree struct safely.
int tree_parse(const void *data, size_t len, Tree *tree_out) {
    tree_out->count = 0;
    const uint8_t *ptr = (const uint8_t *)data;
    const uint8_t *end = ptr + len;

    while (ptr < end && tree_out->count < MAX_TREE_ENTRIES) {
        TreeEntry *entry = &tree_out->entries[tree_out->count];

        const uint8_t *space = memchr(ptr, ' ', end - ptr);
        if (!space) return -1; 

        char mode_str[16] = {0};
        size_t mode_len = space - ptr;
        if (mode_len >= sizeof(mode_str)) return -1;
        memcpy(mode_str, ptr, mode_len);
        entry->mode = strtol(mode_str, NULL, 8);

        ptr = space + 1; 

        const uint8_t *null_byte = memchr(ptr, '\0', end - ptr);
        if (!null_byte) return -1; 

        size_t name_len = null_byte - ptr;
        if (name_len >= sizeof(entry->name)) return -1;
        memcpy(entry->name, ptr, name_len);
        entry->name[name_len] = '\0'; 

        ptr = null_byte + 1; 

        if (ptr + HASH_SIZE > end) return -1; 
        memcpy(entry->hash.hash, ptr, HASH_SIZE);
        ptr += HASH_SIZE;

        tree_out->count++;
    }
    return 0;
}

// Helper for qsort to ensure consistent tree hashing
static int compare_tree_entries(const void *a, const void *b) {
    return strcmp(((const TreeEntry *)a)->name, ((const TreeEntry *)b)->name);
}

// Serialize a Tree struct into binary format for storage.
int tree_serialize(const Tree *tree, void **data_out, size_t *len_out) {
    size_t max_size = tree->count * 296; 
    uint8_t *buffer = malloc(max_size);
    if (!buffer) return -1;

    Tree sorted_tree = *tree;
    qsort(sorted_tree.entries, sorted_tree.count, sizeof(TreeEntry), compare_tree_entries);

    size_t offset = 0;
    for (int i = 0; i < sorted_tree.count; i++) {
        const TreeEntry *entry = &sorted_tree.entries[i];
        
        int written = sprintf((char *)buffer + offset, "%o %s", entry->mode, entry->name);
        offset += written + 1; 
        
        memcpy(buffer + offset, entry->hash.hash, HASH_SIZE);
        offset += HASH_SIZE;
    }

    *data_out = buffer;
    *len_out = offset;
    return 0;
}

// ─── IMPLEMENTED ──────────────────────────────────────────────────

// Recursive helper to build subdirectories from a flat index
static int write_tree_level(IndexEntry *entries, int count, int depth, ObjectID *out_id) {
    Tree tree;
    tree.count = 0;
    
    int i = 0;
    while (i < count) {
        const char *path = entries[i].path;
        const char *curr_level = path;
        for (int s = 0; s < depth; s++) {
            curr_level = strchr(curr_level, '/');
            if (curr_level) curr_level++;
        }
        
        const char *next_slash = strchr(curr_level, '/');
        
        if (!next_slash) {
            // It's a file
            TreeEntry *te = &tree.entries[tree.count++];
            te->mode = entries[i].mode;
            te->hash = entries[i].hash;
            strcpy(te->name, curr_level);
            i++;
        } else {
            // It's a directory
            size_t dir_len = next_slash - curr_level;
            char dir_name[256];
            strncpy(dir_name, curr_level, dir_len);
            dir_name[dir_len] = '\0';
            
            // Group all entries under this directory
            int j = i;
            while (j < count) {
                const char *j_curr = entries[j].path;
                for (int s = 0; s < depth && j_curr; s++) {
                    j_curr = strchr(j_curr, '/');
                    if (j_curr) j_curr++;
                }
                if (!j_curr || strncmp(j_curr, dir_name, dir_len) != 0 || j_curr[dir_len] != '/') break;
                j++;
            }
            
            ObjectID sub_id;
            write_tree_level(&entries[i], j - i, depth + 1, &sub_id);
            
            TreeEntry *te = &tree.entries[tree.count++];
            te->mode = MODE_DIR;
            te->hash = sub_id;
            strcpy(te->name, dir_name);
            i = j;
        }
    }
    
    void *data;
    size_t len;
    tree_serialize(&tree, &data, &len);
    int rc = object_write(OBJ_TREE, data, len, out_id);
    free(data);
    return rc;
}

// Build a tree hierarchy from the current index
int tree_from_index(ObjectID *id_out) {
    // HEAP FIX: Allocate 5.6MB on the HEAP to prevent a Stack Overflow!
    Index *idx = malloc(sizeof(Index));
    if (!idx) return -1;
    
    if (index_load(idx) != 0 || idx->count == 0) {
        free(idx);
        return -1; // Empty index means no tree to build
    }
    
    int rc = write_tree_level(idx->entries, idx->count, 0, id_out);
    free(idx); // Give memory back
    return rc;
}