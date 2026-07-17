#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

void fatal(const char* message) {
    fprintf(stderr, "%s\n", message);
    exit(EXIT_FAILURE);
}

// TODO This is POSIX-specific. Make cross-platform
time_t get_file_timestamp(const char* filename) {
    struct stat file_info;
    if (stat(filename, &file_info) != 0) {
        return 0;
    }
    return file_info.st_mtime;
}

// TODO add error checking
// If null_term is true, *o_size is going to be without the null terminator
int read_file(const char *filename, bool null_term, char* *const o_dest, size_t *o_size, Arena* arena) {   
    FILE* file = fopen(filename, "rb");
    if (!file)
        return 1;

    fseek(file, 0, SEEK_END);
    size_t file_size = ftell(file);
    rewind(file);

    size_t output_size = file_size;
    if (null_term) {
        output_size++;
    }
    
    char* dest = arena_alloc(arena, output_size);
    fread(dest, file_size, 1, file);
    fclose(file);

    if (null_term) {
        dest[output_size - 1] = '\0';
    }

    *o_dest = dest;
    *o_size = null_term ? output_size - 1 : output_size;

    return 0;
}   
