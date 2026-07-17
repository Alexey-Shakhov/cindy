#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

i64 get_file_timestamp(const char* filename) {
    struct stat file_info;
    if (stat(filename, &file_info) != 0) {
        return -1;
    }
    return (i64) file_info.st_mtime;
}
