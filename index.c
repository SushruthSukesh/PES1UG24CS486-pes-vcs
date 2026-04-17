#include "index.h"
#include "tree.h"
#include "pes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

// REQUIRED (no object.h exists)
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);

// ================= PROVIDED =================

IndexEntry* index_find(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0)
            return &index->entries[i];
    }
    return NULL;
}

int index_remove(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0) {
            memmove(&index->entries[i], &index->entries[i+1],
                    (index->count - i - 1) * sizeof(IndexEntry));
            index->count--;
            return index_save(index);
        }
    }
    return -1;
}

int index_status(const Index *index) {
    printf("Staged changes:\n");
    if (index->count == 0) printf("  (nothing to show)\n");
    else {
        for (int i = 0; i < index->count; i++)
            printf("  staged:     %s\n", index->entries[i].path);
    }
    printf("\n");

    printf("Unstaged changes:\n  (nothing to show)\n\n");

    printf("Untracked files:\n");

    DIR *dir = opendir(".");
    struct dirent *ent;
    int found = 0;

    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') continue;

        int tracked = 0;
        for (int i = 0; i < index->count; i++) {
            if (strcmp(index->entries[i].path, ent->d_name) == 0) {
                tracked = 1;
                break;
            }
        }

        if (!tracked) {
            printf("  untracked:  %s\n", ent->d_name);
            found = 1;
        }
    }

    if (!found) printf("  (nothing to show)\n");

    closedir(dir);
    printf("\n");
    return 0;
}

// ================= YOUR CODE =================

int index_load(Index *index) {
    index->count = 0;

    FILE *f = fopen(".pes/index", "r");
    if (!f) return 0;

    while (index->count < MAX_INDEX_ENTRIES) {
        IndexEntry *e = &index->entries[index->count];
        char hash_hex[65];

        if (fscanf(f, "%o %64s %lu %u %[^\n]\n",
                   &e->mode,
                   hash_hex,
                   &e->mtime_sec,
                   &e->size,
                   e->path) != 5) break;

        for (int i = 0; i < 32; i++) {
            sscanf(hash_hex + i*2, "%2hhx", &e->hash.hash[i]);
        }

        index->count++;
    }

    fclose(f);
    return 0;
}

static int cmp(const void *a, const void *b) {
    return strcmp(((IndexEntry*)a)->path, ((IndexEntry*)b)->path);
}

int index_save(const Index *index) {
    Index tmp = *index;
    qsort(tmp.entries, tmp.count, sizeof(IndexEntry), cmp);

    FILE *f = fopen(".pes/index.tmp", "w");
    if (!f) return -1;

    for (int i = 0; i < tmp.count; i++) {
        char hex[65];
        for (int j = 0; j < 32; j++)
            sprintf(hex + j*2, "%02x", tmp.entries[i].hash.hash[j]);
        hex[64] = '\0';

        fprintf(f, "%o %s %lu %u %s\n",
                tmp.entries[i].mode,
                hex,
                tmp.entries[i].mtime_sec,
                tmp.entries[i].size,
                tmp.entries[i].path);
    }

    fflush(f);
    fsync(fileno(f));
    fclose(f);

    rename(".pes/index.tmp", ".pes/index");
    return 0;
}

int index_add(Index *index, const char *path) {
    index_load(index);

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    struct stat st;
    stat(path, &st);

    size_t size = st.st_size;
    void *buf = malloc(size ? size : 1);

    fread(buf, 1, size, f);
    fclose(f);

    ObjectID id;
    object_write(OBJ_BLOB, buf, size, &id);
    free(buf);

    IndexEntry *e = index_find(index, path);
    if (!e) e = &index->entries[index->count++];

    strcpy(e->path, path);
    e->mode = get_file_mode(path);
    e->mtime_sec = st.st_mtime;
    e->size = st.st_size;
    e->hash = id;

    return index_save(index);
}
