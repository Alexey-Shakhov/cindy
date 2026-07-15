#include <stdio.h>
#include <stdlib.h>

void fatal(const char* message) {
    fprintf(stderr, "%s\n", message);
    exit(EXIT_FAILURE);
}

// TODO add error checking
int read_binary_file(const char *filename, char* *const o_dest, size_t *o_size, Arena* arena) {   
    FILE *file = fopen(filename, "rb");
    if (!file)
        return 1;
    
    fseek(file, 0, SEEK_END);
    *o_size = ftell(file);
    rewind(file);
    
    *o_dest = arena_alloc(arena, *o_size);
    fread(*o_dest, *o_size, 1, file);

    fclose(file);

    return 0;
}   
