// This is a grep program.
// It shows multiline matches well.

#include "rx.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>

rx_t *rx;
matcher_t *m;
int data_size;
int data_allocated;
char *data;
int line;
int line_byte;
int match_count;
int before;
int after;

#define eq(a, b) (strcmp(a, b) == 0)

void usage () {
    char str[] =
        "This program is like a recursive grep program.\n"
        "\n"
        "Usage: ./example5 [-h] regexp file...\n"
        "\n"
        "Options:\n"
        "    -h          help text\n"
        "    -A <n>      after context\n"
        "    -B <n>      before context\n"
        "    -C <n>      before and after context";
    puts(str);
    exit(0);
}

void read_file (int fd) {
    data_size = 0;
    int chunk_size = 4096;
    if (data_allocated == 0) {
        data_allocated = 2 * chunk_size;
        data = malloc(data_allocated);
    }
    int retval;
    while (1) {
        if (data_size + chunk_size > data_allocated) {
            data_allocated *= 2;
            data = realloc(data, data_allocated);
        }
        retval = read(fd, data + data_size, chunk_size);
        if (retval < 0) {
            fprintf(stderr, "Can't read file: %s\n", strerror(errno));
            exit(1);
        } else if (retval == 0) {
            break;
        } else {
            data_size += retval;
        }
    }
}

// The regexp doesn't track what line number it was found on, so we go back
// and find out after finding the match, we can speed things up for multiple
// matches by storing the last byte number the line was found on, and only
// start the search from there.
void find_line (int pos) {
    for (int i = line_byte; i < pos; i += 1) {
        if (data[i] == '\n') {
            line += 1;
        }
    }
    line_byte = pos;
}

// Show all text leading to the start of this match, the previous match's position
// is stored in end, if it occurs on the same line, show from end to start, otherwise
// it will show from the beginning of the line.
void show_pre_text (int end, int start) {
    int i;
    for (i = start; i >= 1; i -= 1) {
        if (data[i - 1] == '\n') {
            break;
        } else if (i == end) {
            break;
        }
    }
    if (i < start) {
        printf("%.*s", start - i, data + i);
    }
}

// Show the remaining line from the end of the last match. Returns the end position
// of the post text.
int show_post_text (int end) {
    int i;
    for (i = end; i < data_size; i += 1) {
        if (data[i] == '\n') {
            break;
        }
    }
    if (i > end) {
        printf("%.*s\n", i - end, data + end);
    }
    return i + 1;
}

// Show the context of the file for the lines before start (start doesn't necessarily
// start at a newline) and doesn't go past (backwards) the end of the previous text
// that was output.
void show_before_context (int start, int end, int old_line) {
    int i, j;
    for (i = start; i >= 1; i -= 1) {
        if (data[i - 1] == '\n') {
            break;
        }
    }

    // i is now at the beginning of the line
    int line_start = i;
    int context_start = line_start;
    for (j = 0; j < before; j += 1) {
        if (i == 0) {
            break;
        }
        for (i -= 1; i >= 1; i -= 1) {
            if (data[i - 1] == '\n') {
                if (i < end) {
                    goto out2;
                }
                context_start = i;
                goto out1;
            }
        }
        if (i < end) {
            break;
        }
        context_start = i;
        out1:
            continue;
    }
    out2:
    if (context_start == line_start) {
        return;
    }

    // The number of lines before is bounded by what was previously output,
    // actual_before is the number of lines we can actually show.
    int actual_before = j;
    if (line - before > old_line + after && old_line != 1) {
        printf("--\n");
    }
    for (j = 0; j < actual_before; j += 1) {
        printf("%d: ", line - actual_before + j);
        for (i = context_start;; i += 1) {
            if (data[i] == '\n') {
                break;
            }
        }
        printf("%.*s\n", i - context_start, data + context_start);
        context_start = i + 1;
    }
}

// Show the after context starting the line after the end of the last match, and
// not including the line of the start of the current match.
int show_after_context (int end, int start, int old_line) {
    int i, j = 0, line_start = end;
    for (i = end; i < start; i += 1) {
        if (data[i] == '\n') {
            printf("%d: ", old_line + j + 1);
            printf("%.*s\n", i - line_start, data + line_start);
            line_start = i + 1;
            j += 1;
            if (j == after) {
                break;
            }
        }
    }
    return line_start;
}

void process_file (int fd, char *file) {
    read_file(fd);
    line = 1;
    line_byte = 0;
    int old_line = 1;
    int pos = 0;
    int end = 0;
    int file_match_count = 0;
    while (1) {
        rx_match(rx, m, data_size, data, pos);
        if (!m->success) {
            break;
        }
        if (pos == m->cap_end[0]) {
            pos += 1;
        } else {
            pos = m->cap_end[0];
        }

        if (file_match_count == 0 && fd != 0) {
            if (match_count > 0) {
                printf("\n");
            }
            printf("\e[1;32m%s\e[0m\n", file);
        }
        int start = m->cap_start[0];
        find_line(start);
        if (line > old_line || file_match_count == 0) {
            if (file_match_count) {
                end = show_post_text(end);
                if (after) {
                    end = show_after_context(end, start, old_line);
                }
            }
            if (before) {
                show_before_context(start, end, old_line);
            }
            printf("\e[1;33m%d\e[0m: ", line);
        }
        show_pre_text(end, start);
        // The \e[0K makes it color nicely when the match contains a newline
        printf("\e[103m\e[1;30m%.*s\e[0m\e[0K", m->cap_size[0], m->cap_str[0]);
        end = m->cap_end[0];
        find_line(end);
        old_line = line;
        match_count += 1;
        file_match_count += 1;
    }
    if (file_match_count) {
        end = show_post_text(end);
        if (after) {
            end = show_after_context(end, data_size, old_line);
        }
    }
}

void find (int size, char *file) {
    int fd = open(file, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Can't open %s: %s\n", file, strerror(errno));
        return;
    }

    struct stat st;
    int retval = fstat(fd, &st);
    if (retval < 0) {
        fprintf(stderr, "Can't stat file: %s\n", strerror(errno));
        return;
    }

    if ((st.st_mode & S_IFMT) == S_IFDIR) {
        // recurse
        DIR *dp = fdopendir(fd);
        struct dirent *de;
        while ((de = readdir(dp))) {
            if (eq(de->d_name, ".") || eq(de->d_name, "..")) {
                continue;
            }
            int size2 = size + strlen(de->d_name) + 2;
            char *file2 = malloc(size2);
            sprintf(file2, "%s/%s", file, de->d_name);
            find(size2, file2);
        }
        closedir(dp);
    } else {
        process_file(fd, file);
    }

    close(fd);
}

int main (int argc, char **argv) {
    char *argv2[argc];
    int argc2 = 0;

    for (int i = 1; i < argc; i += 1) {
        if (eq(argv[i], "-h")) {
            usage();
        } else if (eq(argv[i], "-A")) {
            if (i + 1 == argc) {
                printf("Expected argument after -A.\n");
            }
            after = atoi(argv[i + 1]);
            i += 1;
        } else if (eq(argv[i], "-B")) {
            if (i + 1 == argc) {
                printf("Expected argument after -B.\n");
            }
            before = atoi(argv[i + 1]);
            i += 1;
        } else if (eq(argv[i], "-C")) {
            if (i + 1 == argc) {
                printf("Expected argument after -C.\n");
            }
            after = before = atoi(argv[i + 1]);
            i += 1;
        } else if (eq(argv[i], "--")) {
            for (i += 1; i < argc; i += 1) {
                argv2[argc2] = argv[i];
                argc2 += 1;
            }
        } else if (argv[i][0] == '-') {
            printf("Unrecognized option \"%s\".\n", argv[i]);
            return 1;
        } else {
            argv2[argc2] = argv[i];
            argc2 += 1;
        }
    }

    if (argc2 == 0) {
        printf("A regexp is required.\n");
        return 1;
    }
    char *regexp = argv2[0];
    rx = rx_alloc();
    rx_init(rx, strlen(regexp), regexp);
    if (rx->error) {
        puts(rx->errorstr);
        return 1;
    }
    m = rx_matcher_alloc();

    if (!isatty(0)) {
        process_file(0, "stdin");
    } else if (argc2 == 1) {
        argv2[argc2] = ".";
        argc2 += 1;
    }

    for (int i = 1; i < argc2; i += 1) {
        char *file = argv2[i];
        int size = strlen(file);
        find(size, file);
    }

    return 0;
}

