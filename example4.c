// This program will count the lines in each top level block of a file.

#include "rx.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

#define eq(a, b) (strcmp(a, b) == 0)

void usage () {
    char str[] =
        "This program counts the number of lines in each top level block of a file.\n"
        "\n"
        "Usage: ./example4 [-h] file...\n"
        "\n"
        "Options:\n"
        "    -h          help text";
    puts(str);
    exit(0);
}

int read_file (char *file, char **data2) {
    int fd = open(file, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Can't open %s: %s\n", file, strerror(errno));
        return 0;
    }

    struct stat stat;
    int retval = fstat(fd, &stat);
    if (retval < 0) {
        fprintf(stderr, "Can't stat file: %s\n", strerror(errno));
        return 0;
    }

    int file_size = stat.st_size;
    int data_size = 0;
    char *data = malloc(file_size);
    while (1) {
        retval = read(fd, data + data_size, file_size - data_size);
        if (retval < 0) {
            fprintf(stderr, "Can't read file: %s\n", strerror(errno));
            exit(1);
        } else if (retval == 0) {
            break;
        } else {
            data_size += retval;
        }
    }

    close(fd);

    *data2 = data;
    return data_size;
}

void process_file (char *file) {
    char *data;
    int data_size = read_file(file, &data);

    char regexp[] =
        "^^(\\w[^{\\n]*?)(\\([^{\\n]*\\))?( *)\\{\\N*\n"
        "(.*?)"
        "^^\\} *([^;\\n]*)";
    rx_t *rx = rx_alloc();
    rx_init(rx, sizeof(regexp) - 1, regexp);

    matcher_t *m = rx_matcher_alloc();
    int pos = 0;

    while (1) {
        rx_match(rx, m, data_size, data, pos);
        if (!m->success) {
            break;
        }

        // match captures are:
        // 1: type name
        // 2: function arguments
        // 3: whitespace before {
        // 4: contents
        // 5: afterwards type name, like in "typedef struct {} foo_t",
        //    this would be foo_t

        int lines = 0;
        for (int i = 0; i < m->cap_size[4]; i += 1) {
            char c = m->cap_str[4][i];
            if (c == '\n') {
                lines += 1;
            }
        }
        printf("%5d ", lines);
        printf("%.*s", m->cap_size[1], m->cap_str[1]);
        if (m->cap_size[5]) {
            printf(" %.*s", m->cap_size[5], m->cap_str[5]);
        }
        printf("\n");
        pos = m->cap_end[0];
        // rx_match_print(m);
    }
}

int main (int argc, char **argv) {
    int i, j;
    for (i = 1, j = 1; i < argc; i += 1) {
        if (eq(argv[i], "-h")) {
            usage();
        } else if (eq(argv[i], "--")) {
            for (i += 1; i < argc; i += 1) {
                argv[j] = argv[i];
                j += 1;
            }
        } else if (argv[i][0] == '-') {
            printf("Unrecognized option \"%s\".\n", argv[i]);
            return 1;
        } else {
            argv[j] = argv[i];
            j += 1;
        }
    }
    argc = j;

    if (argc < 2) {
        usage();
    }
    for (i = 1; i < argc; i += 1) {
        process_file(argv[i]);
    }

    return 0;
}

