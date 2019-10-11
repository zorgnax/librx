// This is a program runs a testsuite on the librx library. It reads testdata.txt
// for regular expressions and strings to run those regular expressions against and
// check if the result is the expected result. The output is in tap format. The input
// format is something like this:
// 
// # comment
// regexp
//     string
//     0: expected
//     string
//     0: expected
//     1: expected
// 
// Comments must appear on a line by themselves.
// 
// Regexps must start on the leftmost part of the line. Strings and expected strings
// must be indented. If the regexp is supposed to start with whitespece, backslash
// the whitespace, it will mean the same thing. If the regexp starts with a #,
// backslash the #.
//
// Strings and expected values need to be on only one line. So if they contain newlines,
// you have to use \n. The other backslash escapes are available too, including \x12,
// \u1234, \U12341234, \r, \t, and \e. Anything else that is backslashed will convert
// to itself.
// 
// If an expected string is ~ that means it is not a match. If your expected string
// is actually just a ~, you need to backslash the ~.
// 
// If the expected string starts with a digit colon, like "1: expected", it will match
// capture 1. Most of the time you would use capture 0, which is the whole match. If
// you want to match an empty string for the entire match, use "0: ", empty is different
// from a non-match ~. 
// 

#include "rx.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

int test_count = 0;
int failed_tests = 0;
rx_t *test_rx;
matcher_t *test_m;

typedef struct {
    int size;
    int allocated;
    char *str;
    int usize;
    int uallocated;
    char *ustr;
} urstr_t;

urstr_t *test_string;
int expected_count;
int expected_allocated;
urstr_t *expected;

int read_file (char *file, char **data2) {
    int fd = open(file, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Can't open %s: %s\n", file, strerror(errno));
        exit(1);
    }

    struct stat stat;
    int retval = fstat(fd, &stat);
    if (retval < 0) {
        fprintf(stderr, "Can't stat file: %s\n", strerror(errno));
        exit(1);
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

    *data2 = data;
    return data_size;
}

void unescape (urstr_t *s) {
    // unescaped versions of strings are always less than or equal to the src string
    // for example \n goes from 2 characters to 1 character
    if (s->uallocated < s->size) {
        s->uallocated = s->size;
        s->ustr = realloc(s->ustr, s->uallocated);
    }
    int j = 0;
    for (int i = 0; i < s->size; i += 1) {
        char c = s->str[i];
        if (c == '\\') {
            if (i + 1 >= s->size) {
                break; // Expected a character after \.
            }
            i += 1;
            char c2 = s->str[i];
            if (c2 == 'n') {
                s->ustr[j] = '\n';
                j += 1;
            } else if (c2 == 'r') {
                s->ustr[j] = '\r';
                j += 1;
            } else if (c2 == 't') {
                s->ustr[j] = '\t';
                j += 1;
            } else if (c2 == 'e') {
                s->ustr[j] = '\x1b';
                j += 1;
            } else if (c2 == 'x') {
                if (i + 2 >= s->size) {
                    break; // Expected 2 characters after \x.
                }
                unsigned int value;
                if (!rx_hex_to_int(s->str + i + 1, 2, &value)) {
                    break; // Expected 2 hex digits after \x.
                }
                s->ustr[j] = value;
                j += 1;
                i += 2;
            } else if (c2 == 'u') {
                if (i + 4 >= s->size) {
                    break; // Expected 4 characters after \u.
                }
                unsigned int value;
                if (!rx_hex_to_int(s->str + i + 1, 4, &value)) {
                    break; // Expected 4 hex digits after \u.
                }
                int size = rx_int_to_utf8(value, s->ustr + j);
                if (!size) {
                    break; // Invalid \u sequence.
                }
                j += size;
                i += 4;
            } else if (c2 == 'U') {
                if (i + 8 >= s->size) {
                    break; // Expected 8 characters after \U.
                }
                unsigned int value;
                if (!rx_hex_to_int(s->str + i + 1, 8, &value)) {
                    break; // Expected 8 hex digits after \U.
                }
                int size = rx_int_to_utf8(value, s->ustr + j);
                if (!size) {
                    break; // Invalid \U sequence.
                }
                j += size;
                i += 8;
            } else {
                s->ustr[j] = c2;
                j += 1;
            }
        } else {
            s->ustr[j] = c;
            j += 1;
        }
    }
    s->usize = j;
}

void fill_expected_arrays (matcher_t *m) {
    expected_count = 0;

    int en_start = 0, en_end = 0, es_start = 0, es_end = 0;
    for (int i = 0; i < m->path_count; i += 1) {
        path_t *p = m->path + i;
        if (p->node->type == CAPTURE_START && p->node->value == 5) {
            en_start = p->pos;
        } else if (p->node->type == CAPTURE_END && p->node->value == 5) {
            en_end = p->pos;
            char *en = m->str + en_start;
            int en_size = en_end - en_start;
            int en_value = atoi(en);
            if (en_value + 1 > expected_count) {
                expected_count = en_value + 1;
            }
        }
    }

    if (expected_count > expected_allocated) {
        if (expected_allocated == 0) {
            expected_allocated = 10;
        } else {
            expected_allocated *= 2;
        }
        if (expected_count > expected_allocated) {
            expected_allocated += expected_count;
        }
        expected = realloc(expected, expected_allocated * sizeof(urstr_t));
    }

    for (int i = 0; i < expected_count; i++) {
        urstr_t *s = expected + i;
        s->size = 0;
        s->str = NULL;
    }

    en_start = 0, en_end = 0, es_start = 0, es_end = 0;
    for (int i = 0; i < m->path_count; i += 1) {
        path_t *p = m->path + i;
        if (p->node->type == CAPTURE_START && p->node->value == 5) {
            en_start = p->pos;
        } else if (p->node->type == CAPTURE_END && p->node->value == 5) {
            en_end = p->pos;
        } else if (p->node->type == CAPTURE_START && p->node->value == 6) {
            es_start = p->pos;
        } else if (p->node->type == CAPTURE_END && p->node->value == 6) {
            es_end = p->pos;
            char *en = m->str + en_start;
            int en_size = en_end - en_start;
            char *es = m->str + es_start;
            int es_size = es_end - es_start;
            int en_value = atoi(en);
            urstr_t *s = expected + en_value;
            s->size = es_size;
            s->str = es;
        }
    }

    for (int i = 0; i < expected_count; i += 1) {
        urstr_t *s = expected + i;
        unescape(s);
    }
}

void run_test () {
    int fail = 0;
    char *errorstr = NULL;
    test_count += 1;

    rx_match(test_rx, test_m, test_string->usize, test_string->ustr, 0);
    
    if (expected_count == 0) {
        fail = test_m->success ? 0 : 1;
        goto show_result;
    }

    urstr_t *s = expected + 0;
    if (s->str && s->size == 1 && s->str[0] == '~') {
        if (expected_count != 1) {
            fail = 1;
            errorstr = "Can't specify other expected values if 0 is ~\n";
            goto show_result;
        }
        fail = test_m->success ? 1 : 0;
        goto show_result;
    }
    
    if (!test_m->success) {
        fail = 1;
        goto show_result;
    }

    for (int i = 0; i < expected_count; i += 1) {
        urstr_t *s = expected + i;
        if (s->str) {
            if (!test_m->cap_defined[i]) {
                if (s->size == 1 && s->str[0] == '~') {
                    continue;
                }
                fail = 1;
                goto show_result;
            }
            if (s->usize != test_m->cap_size[i]) {
                fail = 1;
                goto show_result;
            }
            if (strncmp(s->ustr, test_m->cap_str[i], s->usize) != 0) {
                fail = 1;
                goto show_result;
            }
        }
    }

    show_result:
    if (fail) {
        failed_tests += 1;
        printf("not ");
    }
    printf("ok %d - %.*s\n", test_count, test_rx->regexp_size, test_rx->regexp);
    if (errorstr) {
        printf("    %s\n", errorstr);
        return;
    }
    printf("    %.*s\n", test_string->size, test_string->str);
    for (int i = 0; i < expected_count; i += 1) {
        urstr_t *s = expected + i;
        if (s->str) {
            printf("    %d: %.*s\n", i, s->size, s->str);
        }
    }
    if (fail && test_m->success) {
        printf("    got:\n");
        for (int i = 0; i < test_m->cap_count; i += 1) {
            if (test_m->cap_defined[i]) {
                printf("    %d: %.*s\n", i, test_m->cap_size[i], test_m->cap_str[i]);
            }
        }
    }
    if (fail && !test_m->success) {
        printf("    got:\n");
        printf("    0: ~\n");
    }
    printf("\n");
}

int main (int argc, char **argv) {
    char *data;
    int data_size = read_file("testdata.txt", &data);

    test_string = malloc(sizeof(urstr_t));
    expected_count = 0;
    expected_allocated = 10;
    expected = malloc(expected_allocated * sizeof(urstr_t));

    test_rx = rx_alloc();
    test_m = rx_matcher_alloc();

    // Regexp for the first line, the regexp to test against
    char regexp1[] =
        "^^([^#\\s]\\N*)\\n"
        "(.*?)(^^[^#\\s]|$)";
    rx_t *rx1 = rx_alloc();
    rx_init(rx1, sizeof(regexp1) - 1, regexp1);

    // Regexp for the string and the expected values
    char regexp2[] =
        "^^[ ]+([^#\\s]\\N*)\\n"
        "("
        "([ ]*(#\\N*)?\\n)*"
        "[ ]*(\\d+):[ ]*(\\N*)"
        ")*";

    rx_t *rx2 = rx_alloc();
    rx_init(rx2, sizeof(regexp2) - 1, regexp2);

    int pos = 0;
    matcher_t *m = rx_matcher_alloc();

    while (1) {
        rx_match(rx1, m, data_size, data, pos);
        if (!m->success) {
            break;
        }

        char *test_regexp = m->cap_str[1];
        int test_regexp_size = m->cap_size[1];
        char *content = m->cap_str[2];
        int content_size = m->cap_size[2];
        pos = m->cap_end[2];

        // printf("test regexp is %.*s\n", test_regexp_size, test_regexp);
        // printf("test content is [%.*s]\n", content_size, content);
        
        rx_init(test_rx, test_regexp_size, test_regexp);
        if (test_rx->error) {
            test_count += 1;
            printf("not ok %d - %.*s\n", test_count, test_regexp_size, test_regexp);
            printf("    Can't compile regexp: %s\n", test_rx->errorstr);
            continue;
        }

        int pos2 = 0;
        while (1) {
            rx_match(rx2, m, content_size, content, pos2);
            if (!m->success) {
                break;
            }
            pos2 = m->cap_end[0];

            test_string->str = m->cap_str[1];
            test_string->size = m->cap_size[1];
            unescape(test_string);

            fill_expected_arrays(m);

            run_test();
        }
    }

    printf("1..%d\n", test_count);
    if (failed_tests) {
        printf("# Looks like you failed %d test%s of %d run.\n", failed_tests, failed_tests == 1 ? "" : "s", test_count);
        return 1;
    }
    return 0;
}

// TODO add a -h option

