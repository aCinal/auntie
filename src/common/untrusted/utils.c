#include "utils.h"
#include <stdio.h>
#include <stdlib.h>

uint8_t *read_entire_file(size_t *size, const char *filename)
{
    FILE *file;
    uint8_t *buf;
    size_t bytes_read;

    file = fopen(filename, "r");
    if (!file) {
        perror(filename);
        return NULL;
    }

    if (fseek(file, -1, SEEK_END)) {
        perror("fseek");
        (void) fclose(file);
        return NULL;
    }

    *size = ftell(file);

    if (fseek(file, 0, SEEK_SET)) {
        perror("fseek");
        (void) fclose(file);
        return NULL;
    }

    buf = malloc(*size);
    if (!buf) {
        perror("malloc");
        (void) fclose(file);
        return NULL;
    }

    bytes_read = fread(buf, sizeof(uint8_t), *size, file);
    (void) fclose(file);
    if (bytes_read != *size) {
        printf("%s: fread failed to read %lu byte(s) and returned %lu\n", \
            __func__, *size, bytes_read);
        return NULL;
    }

    return buf;
}
