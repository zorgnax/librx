#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

// This library implements regular expressions using an NFA graph. It backtracks using
// an array instead of recursing. It handles utf8 nicely without losing the ability
// to handle strings of arbitrary bytes (as long as they don't contain zero bytes).
// It's mostly like perl regular expressions but is missing some of the lesser used
// features. From vim regexp, I took \c (ignorecase), \< (left word boundary), and
// \> (right word boundary). It supports greedy/nongreedy versions of all its quantifiers
// (*?, +?, ??, {3,5}?). And it supports capture groups. There are no flags in
// this regexp language, instead everything must be inside the regexp itself. So to
// ignore case, you must supply the \c rule somewhere in the regexp string. To match
// the start of a string you use ^, but to match the start of a line you use ^^.

enum {
    EMPTY,
    CHAR,
    BRANCH,
    CAPTURE_START,
    CAPTURE_END,
    MATCH_END,
    QUANTIFIER,
    SUBGRAPH_END,
    ASSERTION,
    CHAR_CLASS, // [...]
    CHAR_SET, // \d etc.
    GROUP_START,
    GROUP_END,
};

enum {
    ASSERT_SOS, // start of string
    ASSERT_SOL, // start of line
    ASSERT_EOS, // end of string
    ASSERT_EOL, // end of line
    ASSERT_SOP, // start of position
    ASSERT_SOW, // start of word
    ASSERT_EOW, // end of word
};

enum {
    CS_ANY,
    CS_NOTNL,
    CS_DIGIT,
    CS_NOTDIGIT,
    CS_WORD,
    CS_NOTWORD,
    CS_SPACE,
    CS_NOTSPACE,
};

typedef struct node_t node_t;

typedef struct {
    int min;
    int max;
    int greedy;
    node_t *next;
} quantifier_t;

typedef struct {
    char negated;
    int value_count;
    int value_offset;
    int range_count;
    int range_offset;
    int char_set_count;
    int char_set_offset;
    int str_size;
    char *str;
} char_class_t;

struct node_t {
    char type;
    node_t *next;
    union {
        unsigned char value;
        node_t *next2;
        int qval_offset;
        int ccval_offset;
    };
};

typedef struct {
    node_t *start;
    char *regexp;
    node_t *nodes;
    int nodes_count;
    int nodes_allocated;
    int cap_count;
    int max_cap_depth;
    node_t **cap_start;
    node_t **or_end;
    int error;
    char *errorstr;
    int ignorecase;
    char *data;
    int data_count;
    int data_allocated;
} rx_t;

typedef struct {
    node_t *node;
    int pos;
    int visit;
} path_t;

// The matcher maintains a list of positions that are important for backtracking
// and for remembering captures.
typedef struct {
    char *str;
    rx_t *rx;
    int path_count;
    int path_allocated;
    path_t *path;
    int cap_count;
    int cap_allocated;
    int *cap_start;
    int *cap_end;
    char *cap_defined;
    char **cap_str;
    int *cap_size;
    int success;
} matcher_t;

static rx_t *rx;
static matcher_t *m;

// Print with no new line, except if it appears as the last character.
static int printnnl (char *fmt, ...) {
    static char str[256];
    static char str2[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(str, 256, fmt, args);
    va_end(args);
    int j = 0;
    for (int i = 0; str[i]; i += 1) {
        if (str[i] == '\n' && str[i + 1] != '\0') {
            strcpy(str2 + j, " \u2424 ");
            j += 5;
        }
        else {
            str2[j] = str[i];
            j += 1;
        }
    }
    str2[j] = '\0';
    return printf("%s", str2);
}

// Reads a utf8 character from src and determines how many bytes it is. If the src
// doesn't contain a proper utf8 character, it returns 1. src should be NULL terminated.
static int utf8_char_size (char *src, int pos) {
    char c = src[pos];
    int size = 0;
    if ((c & 0x80) == 0x00) {
        size = 1;
    } else if ((c & 0xe0) == 0xc0) {
        size = 2;
    } else if ((c & 0xf0) == 0xe0) {
        size = 3;
    } else if ((c & 0xf8) == 0xf0) {
        size = 4;
    } else {
        // Invalid utf8 starting byte
        size = 1;
    }
    for (int i = 1; i < size; i += 1) {
        c = src[pos + i];
        if ((c & 0xc0) != 0x80) {
            // Didn't find a proper utf8 continuation byte 10xx_xxxx
            return 1;
        }
    }
    return size;
}

static int hex_to_int (char *str, int size, unsigned int *dest) {
    unsigned int value = 0;
    for (int i = 0; i < size; i += 1) {
        char c = str[i];
        unsigned char b = 0;
        if (c >= '0' && c <= '9') {
            b = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            b = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            b = c - 'A' + 10;
        } else {
            return 0;
        }
        value = (value << 4) | b;
    }
    *dest = value;
    return 1;
}

static int int_to_utf8 (unsigned int value, char *str) {
    if (value < 0x80) {
        str[0] = value;
        str[1] = '\0';
        return 1;
    } else if (value < 0x800) {
        str[0] = 0x80 | ((value & 0x7c) >> 6);
        str[1] = 0x80 | ((value & 0x3f) >> 0);
        str[2] = '\0';
        return 2;
    } else if (value < 0x10000) {
        str[0] = 0xe0 | ((value & 0xf000) >> 12);
        str[1] = 0x80 | ((value & 0x0fc0) >> 6);
        str[2] = 0x80 | ((value & 0x003f) >> 0);
        str[3] = '\0';
        return 3;
    } else if (value < 0x200000) {
        str[0] = 0xf0 | ((value & 0x1c0000) >> 18);
        str[1] = 0x80 | ((value & 0x03f000) >> 12);
        str[2] = 0x80 | ((value & 0x000fc0) >> 6);
        str[3] = 0x80 | ((value & 0x00003f) >> 0);
        str[4] = '\0';
        return 4;
    } else {
        return 0;
    }
}

node_t *rx_node_create (rx_t *rx) {
    node_t *n = &rx->nodes[rx->nodes_count];
    rx->nodes_count += 1;
    n->type = EMPTY;
    n->next = NULL;
    return n;
}

int rx_node_index (rx_t *rx, node_t *n) {
    int index = n - rx->nodes;
    return index;
}

void rx_print (rx_t *rx) {
    FILE *fp = fopen("/tmp/nfa.txt", "w");
    fprintf(fp, "graph g {\n");
    for (int i = 0; i < rx->nodes_count; i += 1) {
        node_t *n = &rx->nodes[i];
        int i1 = rx_node_index(rx, n);
        int i2 = rx_node_index(rx, n->next);

        if (n->type == CHAR) {
            char label[6];
            if (n->value == '\x1b') {
                strcpy(label, "\u29f9e");
            } else if (n->value == '\r') {
                strcpy(label, "\u29f9r");
            } else if (n->value == '\n') {
                strcpy(label, "\u29f9n");
            } else if (n->value == '\t') {
                strcpy(label, "\u29f9t");
            } else {
                label[0] = n->value;
                label[1] = '\0';
            }
            fprintf(fp, "    %d -> %d [label=\"%s\",style=solid]\n", i1, i2, label);
        } else if (n->type == CAPTURE_START) {
            fprintf(fp, "    %d -> %d [label=\"(%d\",style=solid]\n", i1, i2, n->value);
        } else if (n->type == CAPTURE_END) {
            fprintf(fp, "    %d -> %d [label=\")%d\",style=solid]\n", i1, i2, n->value);
        } else if (n->type == GROUP_START) {
            fprintf(fp, "    %d -> %d [label=\"(?\",style=solid]\n", i1, i2);
        } else if (n->type == GROUP_END) {
            fprintf(fp, "    %d -> %d [label=\")?\",style=solid]\n", i1, i2);
        } else if (n->type == BRANCH) {
            int i3 = rx_node_index(rx, n->next2);
            fprintf(fp, "    %d [label=\"%dB\"]\n", i1, i1);
            fprintf(fp, "    %d -> %d [style=solid]\n", i1, i2);
            fprintf(fp, "    %d -> %d [style=dotted]\n", i1, i3);
        } else if (n->type == ASSERTION) {
            fprintf(fp, "    %d [label=\"%dA\"]\n", i1, i1);
            char *labels[] = {
                "^",         // ASSERT_SOS
                "^^",        // ASSERT_SOL
                "$",         // ASSERT_EOS
                "$$",        // ASSERT_EOL
                "\u29f9G",   // ASSERT_SOP
                "\\<",       // ASSERT_SOW
                "\\>",       // ASSERT_EOW
            };
            char *label = labels[n->value];
            fprintf(fp, "    %d -> %d [label=\"%s\"]\n", i1, i2, label);
        } else if (n->type == CHAR_CLASS) {
            char_class_t *ccval = (char_class_t *) (rx->data + n->ccval_offset);
            fprintf(fp, "    %d [label=\"%dC\"]\n", i1, i1);
            fprintf(fp, "    %d -> %d [label=\"%.*s\"]\n", i1, i2, ccval->str_size, ccval->str);
        } else if (n->type == CHAR_SET) {
            char *labels[] = {
                ".",         // CS_ANY
                "\u29f9N",   // CS_NOTNL
                "\u29f9d",   // CS_DIGIT
                "\u29f9D",   // CS_NOTDIGIT
                "\u29f9w",   // CS_WORD
                "\u29f9W",   // CS_NOTWORD
                "\u29f9s",   // CS_SPACE
                "\u29f9S",   // CS_NOTSPACE
            };
            char *label = labels[n->value];
            fprintf(fp, "    %d [label=\"%dC\"]\n", i1, i1);
            fprintf(fp, "    %d -> %d [label=\"%s\"]\n", i1, i2, label);
        } else if (n->type == QUANTIFIER) {
            quantifier_t *qval = (quantifier_t *) (rx->data + n->qval_offset);
            if (qval->greedy) {
                int i3 = rx_node_index(rx, qval->next);
                fprintf(fp, "    %d [label=\"%dQ\"]\n", i1, i1);
                fprintf(fp, "    %d -> %d [style=dotted]\n", i1, i2);
                fprintf(fp, "    %d -> %d [style=solid,label=\"{%d", i1, i3, qval->min);
                if (qval->min == qval->max) {
                    fprintf(fp, "}\"]\n");
                } else if (qval->max == -1) {
                    fprintf(fp, ",}\"]\n");
                } else {
                    fprintf(fp, ",%d}\"]\n", qval->max);
                }
            } else {
                int i3 = rx_node_index(rx, qval->next);
                fprintf(fp, "    %d [label=\"%dQ\"]\n", i1, i1);
                fprintf(fp, "    %d -> %d [style=solid]\n", i1, i2);
                fprintf(fp, "    %d -> %d [style=dotted,label=\"{%d", i1, i3, qval->min);
                if (qval->min == qval->max) {
                    fprintf(fp, "}?\"]\n");
                } else if (qval->max == -1) {
                    fprintf(fp, ",}?\"]\n");
                } else {
                    fprintf(fp, ",%d}?\"]\n", qval->max);
                }
            }
        } else if (n->type == MATCH_END) {
            fprintf(fp, "    %d [label=\"%dE\"]\n", i1, i1);
        } else if (n->next) {
            fprintf(fp, "    %d -> %d [style=solid]\n", i1, i2);
        }
    }
    fprintf(fp, "}\n");
    fclose(fp);
    system("graph-easy -as=boxart /tmp/nfa.txt");
}

int rx_error (rx_t *rx, char *fmt, ...) {
    rx->error = 1;
    if (!rx->errorstr) {
        rx->errorstr = malloc(64);
    }
    va_list args;
    va_start(args, fmt);
    vsnprintf(rx->errorstr, 64, fmt, args);
    va_end(args);
    return 0;
}

int rx_char_class_parse (rx_t *rx, int pos, int *pos2, int save, char_class_t *ccval) {
    int value_count = 0, range_count = 0, char_set_count = 0;
    char *regexp = rx->regexp;
    char c1, c2;
    char char1[5], char2[5];
    int char1_size = 0, char2_size = 0;
    char seen_dash = 0;
    char seen_special = '\0';

    while (regexp[pos]) {
        c1 = regexp[pos];
        if (c1 == ']') {
            break;
        } else if (c1 == '-' && !seen_dash) {
            seen_dash = 1;
            pos += 1;
            continue;
        } else if (c1 == '\\') {
            c2 = regexp[pos + 1];
            if (c2 == '\0') {
                return rx_error(rx, "Expected character after \\.");

            } else if (c2 == 'd' || c2 == 'D' || c2 == 'w' || c2 == 'W' || c2 == 's' || c2 == 'S' || c2 == 'N') {
                // Something like \d or \D
                if (seen_dash) {
                    return rx_error(rx, "Can't have \\%c after -.", c2);
                }
                if (save) {
                    rx->data[ccval->char_set_offset + char_set_count]
                                = c2 == 'N' ? CS_NOTNL
                                : c2 == 'd' ? CS_DIGIT
                                : c2 == 'D' ? CS_NOTDIGIT
                                : c2 == 'w' ? CS_WORD
                                : c2 == 'W' ? CS_NOTWORD
                                : c2 == 's' ? CS_SPACE
                                : c2 == 'S' ? CS_NOTSPACE : 0;
                }
                char_set_count += 1;
                seen_special = c2;
                pos += 2;
                continue;

            } else if (c2 == 'e' || c2 == 'r' || c2 == 'n' || c2 == 't') {
                // Something like \n or \t
                char2[0] = c2 == 'e' ? '\x1b'
                         : c2 == 'r' ? '\r'
                         : c2 == 'n' ? '\n'
                         : c2 == 't' ? '\t' : '\0';
                char2_size = 1;
                pos += 2;

            } else if (c2 == 'x') {
                // \x3f
                unsigned int i;
                if (!hex_to_int(regexp + pos + 2, 2, &i)) {
                    return rx_error(rx, "Expected 2 hex digits after \\x.");
                }
                char2_size = 1;
                char2[0] = i;
                pos += 4;

            } else if (c2 == 'u' || c2 == 'U') {
                // \u2603 or \U00002603
                int size = c2 == 'u' ? 4 : 8;
                unsigned int i;
                if (!hex_to_int(regexp + pos + 2, size, &i)) {
                    return rx_error(rx, "Expected %d hex digits after \\%c.", size, c2);
                }
                char2_size = int_to_utf8(i, char2);
                if (!char2_size) {
                    return rx_error(rx, "Invalid \\%c sequence.", c2);
                }
                pos += 2 + size;

            } else {
                // Any unrecognized backslash escape, for example \]
                pos += 1;
                char2_size = utf8_char_size(regexp, pos);
                memcpy(char2, regexp + pos, char2_size);
                pos += char2_size;
            }

        } else {
            // An unspecial character, for example a or ☃
            char2_size = utf8_char_size(regexp, pos);
            memcpy(char2, regexp + pos, char2_size);
            pos += char2_size;
        }

        // At this point we can assume we have a character and pos was incremented.
        if (char1_size && seen_dash) {
            // Range
            if (save) {
                memcpy(rx->data + ccval->range_offset + range_count, char1, char1_size);
                range_count += char1_size;
                memcpy(rx->data + ccval->range_offset + range_count, char2, char2_size);
                range_count += char2_size;
            }
            else {
                if (seen_special) {
                    return rx_error(rx, "Can't have - after \\%c.", seen_special);
                }
                if (char1_size > char2_size || strncmp(char1, char2, char1_size) >= 0) {
                    return rx_error(rx, "End of range must be higher than start.");
                }
                range_count += char1_size + char2_size;
            }
            seen_dash = 0;
            char1_size = 0;
        } else if (seen_dash) {
            return rx_error(rx, "Unexpected -.");
        } else {
            // Value
            if (char1_size) {
                if (save) {
                    memcpy(rx->data + ccval->value_offset + value_count, char1, char1_size);
                }
                value_count += char1_size;
            }
            memcpy(char1, char2, char2_size);
            char1_size = char2_size;
        }
        seen_special = '\0';
    }
    if (char1_size) {
        if (save) {
            memcpy(rx->data + ccval->value_offset + value_count, char1, char1_size);
        }
        value_count += char1_size;
    }
    if (seen_dash) {
        if (save) {
            rx->data[ccval->value_offset + value_count] = '-';
        }
        value_count += 1;
    }
    if (regexp[pos] != ']') {
        return rx_error(rx, "Expected ].");
    }
    if (save) {
        *pos2 = pos;
    }
    ccval->value_count = value_count;
    ccval->range_count = range_count;
    ccval->char_set_count = char_set_count;
    return 1;
}

// Stores a block of data inside the rx object so the data can be reused and freed
// as one. users must only store offsets into it, since it may be realloced.
int rx_internal_alloc (rx_t *rx, int size) {
    if (rx->data_allocated == 0) {
        rx->data_allocated = (size > 100) ? (size + 100) : 100;
        rx->data = malloc(rx->data_allocated);
    } else if (rx->data_allocated - rx->data_count < size) {
        rx->data_allocated *= 2;
        if (rx->data_allocated - rx->data_count < size) {
            rx->data_allocated += size;
        }
        rx->data = realloc(rx->data, rx->data_allocated);
    }
    int offset = rx->data_count;
    rx->data_count += size;
    return offset;
}

// This construct is character oriented. If you write [☃], it will match the 3
// byte sequence \xe2\e98\x83, and not individual bytes in that sequnce. Similarly,
// if you specify a range like [Α-Ω], Greek alpha to omega, it will match only
// characters in that range, and not invalid bytes in that sequence. You can also
// enter invalid bytes, and they will only match 1 byte at a time.
int rx_char_class_init (rx_t *rx, int pos, int *pos2, int *ccval_offset) {
    char *regexp = rx->regexp;
    char_class_t ccval = {};
    ccval.str = regexp + pos;

    pos += 1;
    char c1 = regexp[pos];
    if (c1 == '^') {
        ccval.negated = 1;
        pos += 1;
    }

    // Count the number of values and ranges needed before allocating them
    if(!rx_char_class_parse(rx, pos, &pos, 0, &ccval)) {
        return 0;
    }

    // Allocate the arrays
    if (ccval.value_count) {
        ccval.value_offset = rx_internal_alloc(rx, ccval.value_count + 1);
        rx->data[ccval.value_offset + ccval.value_count] = '\0';
    }
    if (ccval.range_count) {
        ccval.range_offset = rx_internal_alloc(rx, ccval.range_count + 1);
        rx->data[ccval.range_offset + ccval.range_count] = '\0';
    }
    if (ccval.char_set_count) {
        ccval.char_set_offset = rx_internal_alloc(rx, ccval.char_set_count);
    }

    // Fill in the arrays
    rx_char_class_parse(rx, pos, &pos, 1, &ccval);
    ccval.str_size = regexp + pos - ccval.str + 1;

    *pos2 = pos;
    *ccval_offset = rx_internal_alloc(rx, sizeof(char_class_t));
    char_class_t *ccval2 = (char_class_t *) (rx->data + *ccval_offset);
    *ccval2 = ccval;
    return 1;
}

int rx_quantifier_init (rx_t *rx, int pos, int *pos2, int *qval_offset) {
    char *regexp = rx->regexp;
    char c;
    int min = 0, minp = 1, max = 0, maxp = 1, greedy = 1;
    pos += 1;
    for (; regexp[pos]; pos += 1) {
        c = regexp[pos];
        if (c >= '0' && c <= '9') {
            min = minp * min + (c - '0');
            minp *= 10;
        } else if (c == ',') {
            if (minp == 1) {
                return rx_error(rx, "Expected a number before ,.");
            }
            pos += 1;
            break;
        } else if (c == '}') {
            if (minp == 1) {
                return rx_error(rx, "Expected a number before }.");
            }
            max = min;
            goto check_greedy;
        } else {
            return rx_error(rx, "Unexpected character in quantifier.");
        }
    }
    for (; regexp[pos]; pos += 1) {
        c = regexp[pos];
        if (c >= '0' && c <= '9') {
            max = maxp * max + (c - '0');
            maxp *= 10;
        } else if (c == '}') {
            if (maxp == 1) {
                max = -1; // -1 means infinite
            }
            goto check_greedy;
        } else {
            return rx_error(rx, "Unexpected character in quantifier.");
        }
    }
    return rx_error(rx, "Quantifier not closed.");

    check_greedy:
    c = regexp[pos + 1];
    if (c == '?') {
        // non greedy
        pos += 1;
        greedy = 0;
    }
    else {
        // greedy
        greedy = 1;
    }
    *pos2 = pos;
    (*qval_offset) = rx_internal_alloc(rx, sizeof(quantifier_t));
    quantifier_t *qval = (quantifier_t *) (rx->data + *qval_offset);
    qval->min = min;
    qval->max = max;
    qval->greedy = greedy;
    return 1;
}

rx_t *rx_alloc () {
    rx_t *rx = calloc(1, sizeof(rx_t));
    return rx;
}

matcher_t *rx_matcher_alloc () {
    matcher_t *m = calloc(1, sizeof(matcher_t));
    return m;
}

// Returns 1 on success
int rx_init (rx_t *rx, char *regexp) {
    rx->error = 0;

    // Preallocate the space needed for nodes. Since each character can
    // add at most 2 nodes, the count of nodes needed shouldn't be
    // more than 2 * (x + 1). Also count the max capture nest depth, which can't
    // take into account ) since it could be \) or [)] and there's no way to tell
    // what kind of ) it is without a full parse.
    //
    // All allocations that occur in rx_init are reuseable in subsequent calls to it.
    // Nothing is freed until rx_free() is called, and that only needs to happen
    // once when the user is entirely done with regexps.
    int cap_depth = 0;
    int max_cap_depth = 0;
    int max_node_count = 0;
    for (int pos = 0; regexp[pos]; pos += 1) {
        char c = regexp[pos];
        max_node_count += 1;
        if (c == '(') {
            max_cap_depth += 1;
        }
    }
    max_node_count = (max_node_count + 1) * 2;

    if (max_node_count > rx->nodes_allocated) {
        rx->nodes_allocated = max_node_count;
        rx->nodes = realloc(rx->nodes, rx->nodes_allocated * sizeof(node_t));
    }

    if (max_cap_depth > rx->max_cap_depth) {
        rx->max_cap_depth = max_cap_depth;
        rx->cap_start = realloc(rx->cap_start, rx->max_cap_depth * sizeof(node_t *));
        rx->or_end = realloc(rx->or_end, rx->max_cap_depth * sizeof(node_t *));
    }

    rx->data_count = 0;
    rx->nodes_count = 0;
    rx->regexp = regexp;
    rx->start = rx_node_create(rx);
    node_t *node = rx->start;
    node_t *atom_start = NULL;
    node_t *or_end = NULL;
    rx->cap_count = 0;
    rx->ignorecase = 0;

    for (int pos = 0; regexp[pos]; pos += 1) {
        unsigned char c = regexp[pos];
        if (c == '(') {
            if (regexp[pos + 1] == '?' && regexp[pos + 2] == ':') {
                pos += 2;
                node->type = GROUP_START;
            }
            else {
                rx->cap_count += 1;
                node->value = rx->cap_count;
                node->type = CAPTURE_START;
            }
            node_t *node2 = rx_node_create(rx);
            node->next = node2;
            rx->cap_start[cap_depth] = node;
            rx->or_end[cap_depth] = or_end;
            or_end = NULL;
            cap_depth += 1;
            atom_start = NULL;
            node = node2;

        } else if (c == ')') {
            if (!cap_depth) {
                return rx_error(rx, ") was unexpected.");
            }
            if (or_end) {
                node->next = or_end;
                node = or_end;
            }
            cap_depth -= 1;
            or_end = rx->or_end[cap_depth];
            atom_start = rx->cap_start[cap_depth];
            node_t *node2 = rx_node_create(rx);
            if (atom_start->type == CAPTURE_START) {
                node->type = CAPTURE_END;
            } else {
                node->type = GROUP_END;
            }
            node->value = atom_start->value;
            node->next = node2;
            node = node2;

        } else if (c == '|') {
            node_t *node2 = rx_node_create(rx);
            node_t *node3 = rx_node_create(rx);
            node_t *or_start;
            if (cap_depth) {
                or_start = rx->cap_start[cap_depth - 1]->next;
            } else {
                or_start = rx->start;
            }
            *node2 = *or_start;
            or_start->type = BRANCH;
            or_start->next = node2;
            or_start->next2 = node3;
            if (or_end) {
                node->next = or_end;
            } else {
                or_end = node;
            }
            node = node3;

        } else if (c == '*') {
            if (!atom_start) {
                return rx_error(rx, "Expected something to apply the *.");
            }
            node_t *node2 = rx_node_create(rx);
            node_t *node3 = rx_node_create(rx);
            *node2 = *atom_start;
            atom_start->type = BRANCH;
            node->type = BRANCH;
            char c2 = regexp[pos + 1];
            if (c2 == '?') {
                // non greedy
                pos += 1;
                atom_start->next = node3;
                atom_start->next2 = node2;
                node->next = node3;
                node->next2 = node2;
            } else {
                // greedy
                atom_start->next = node2;
                atom_start->next2 = node3;
                node->next = node2;
                node->next2 = node3;
            }
            node = node3;

        } else if (c == '+') {
            if (!atom_start) {
                return rx_error(rx, "Expected something to apply the +.");
            }
            node_t *node2 = rx_node_create(rx);
            node->type = BRANCH;
            char c2 = regexp[pos + 1];
            if (c2 == '?') {
                // non greedy
                pos += 1;
                node->next = node2;
                node->next2 = atom_start;
            } else {
                // greedy
                node->next = atom_start;
                node->next2 = node2;
            }
            node = node2;

        } else if (c == '?') {
            if (!atom_start) {
                return rx_error(rx, "Expected something to apply the ?.");
            }
            node_t *node2 = rx_node_create(rx);
            *node2 = *atom_start;
            atom_start->type = BRANCH;
            char c2 = regexp[pos + 1];
            if (c2 == '?') {
                // non greedy
                pos += 1;
                atom_start->next = node;
                atom_start->next2 = node2;
            } else {
                // greedy
                atom_start->next = node2;
                atom_start->next2 = node;
            }

        } else if (c == '{') {
            if (!atom_start) {
                return rx_error(rx, "Expected something to apply the {.");
            }
            int qval_offset;
            if (!rx_quantifier_init(rx, pos, &pos, &qval_offset)) {
                return 0;
            }
            node_t *node2 = rx_node_create(rx);
            node_t *node3 = rx_node_create(rx);
            *node2 = *atom_start;
            atom_start->type = QUANTIFIER;
            atom_start->qval_offset = qval_offset;
            quantifier_t *qval = (quantifier_t *) (rx->data + qval_offset);
            // For QUANTIFIER nodes, qval->next points into the subgraph and next
            // points out of it.
            atom_start->next = node3;
            qval->next = node2;
            node->type = SUBGRAPH_END;
            node->next2 = atom_start;
            node = node3;

        } else if (c == '\\') {
            pos += 1;
            char c2 = regexp[pos];
            if (c2 == 'G') {
                node_t *node2 = rx_node_create(rx);
                node->type = ASSERTION;
                node->next = node2;
                node->value = ASSERT_SOP;
                node = node2;
            } else if (c2 == '<') {
                node_t *node2 = rx_node_create(rx);
                node->type = ASSERTION;
                node->next = node2;
                node->value = ASSERT_SOW;
                node = node2;
            } else if (c2 == '>') {
                node_t *node2 = rx_node_create(rx);
                node->type = ASSERTION;
                node->next = node2;
                node->value = ASSERT_EOW;
                node = node2;
            } else if (c2 == 'c') {
                rx->ignorecase = 1;
            } else if (c2 == 'e' || c2 == 'r' || c2 == 'n' || c2 == 't') {
                node_t *node2 = rx_node_create(rx);
                node->type = CHAR;
                node->next = node2;
                node->value = c2 == 'e' ? '\x1b'
                            : c2 == 'r' ? '\r'
                            : c2 == 'n' ? '\n'
                            : c2 == 't' ? '\t' : '\0';
                atom_start = node;
                node = node2;
            } else if (c2 == 'N' || c2 == 'd' || c2 == 'D' || c2 == 'w' || c2 == 'W' || c2 == 's' || c2 == 'S') {
                node_t *node2 = rx_node_create(rx);
                node->type = CHAR_SET;
                node->next = node2;
                node->value = c2 == 'N' ? CS_NOTNL
                            : c2 == 'd' ? CS_DIGIT
                            : c2 == 'D' ? CS_NOTDIGIT
                            : c2 == 'w' ? CS_WORD
                            : c2 == 'W' ? CS_NOTWORD
                            : c2 == 's' ? CS_SPACE
                            : c2 == 'S' ? CS_NOTSPACE : 0;

                atom_start = node;
                node = node2;
            } else if (c2 == 'x') {
                unsigned int i;
                if (!hex_to_int(regexp + pos + 1, 2, &i)) {
                    return rx_error(rx, "Expected 2 hex digits after \\x.");
                }
                pos += 2;
                node_t *node2 = rx_node_create(rx);
                node->type = CHAR;
                node->next = node2;
                node->value = i;
                atom_start = node;
                node = node2;
            } else if (c2 == 'u' || c2 == 'U') {
                int count = c2 == 'u' ? 4 : 8;
                unsigned int i;
                if (!hex_to_int(regexp + pos + 1, count, &i)) {
                    return rx_error(rx, "Expected %d hex digits after \\%c.", count, c2);
                }
                pos += count;
                char str[5];
                if (!int_to_utf8(i, str)) {
                    return rx_error(rx, "Invalid \\%c sequence.", c2);
                }
                atom_start = node;
                for (i = 0; str[i]; i += 1) {
                    node_t *node2 = rx_node_create(rx);
                    node->type = CHAR;
                    node->next = node2;
                    node->value = str[i];
                    node = node2;
                }

            } else {
                // Unrecognized backslash escape will match itself, for example \\ \* \+ \?
                node_t *node2 = rx_node_create(rx);
                node->type = CHAR;
                node->next = node2;
                node->value = c2;
                atom_start = node;
                node = node2;
            }

        } else if (c == '^') {
            node_t *node2 = rx_node_create(rx);
            node->type = ASSERTION;
            node->next = node2;
            char c2 = regexp[pos + 1];
            if (c2 == '^') {
                pos += 1;
                node->value = ASSERT_SOL;
            } else {
                node->value = ASSERT_SOS;
            }
            node = node2;

        } else if (c == '$') {
            node_t *node2 = rx_node_create(rx);
            node->type = ASSERTION;
            node->next = node2;
            char c2 = regexp[pos + 1];
            if (c2 == '$') {
                pos += 1;
                node->value = ASSERT_EOL;
            } else {
                node->value = ASSERT_EOS;
            }
            node = node2;
        } else if (c == '[') {
            int ccval_offset;
            if (!rx_char_class_init(rx, pos, &pos, &ccval_offset)) {
                return 0;
            }
            node_t *node2 = rx_node_create(rx);
            node->type = CHAR_CLASS;
            node->next = node2;
            node->ccval_offset = ccval_offset;
            atom_start = node;
            node = node2;

        } else if (c == '.') {
            node_t *node2 = rx_node_create(rx);
            node->type = CHAR_SET;
            node->value = CS_ANY;
            node->next = node2;
            atom_start = node;
            node = node2;

        } else {
            node_t *node2 = rx_node_create(rx);
            node->type = CHAR;
            node->next = node2;
            node->value = c;
            atom_start = node;
            node = node2;
        }

    }
    if (cap_depth) {
        return rx_error(rx, "Expected closing ).");
    }
    if (or_end) {
        node->next = or_end;
        node = or_end;
    }
    node->type = MATCH_END;
    return 1;
}

// rx_match() will match a regexp against a given string. The strings it finds
// will be stored in the matcher argument. The start position can be given, but usually
// it would be 0 for the start of the string. Returns 1 on success and 0 on failure.
//
// The same matcher object can be used multiple times which will reuse the
// memory allocated for previous matches.
int rx_match (rx_t *rx, matcher_t *m, char *str, int start_pos) {
    m->str = str;
    m->rx = rx;
    m->success = 0;
    m->path_count = 0;
    if (rx->error) {
        return 0;
    }
    node_t *node = rx->start;
    int pos = start_pos;
    if (m->path_allocated == 0) {
        m->path_allocated = 10;
        m->path = malloc(m->path_allocated * sizeof(path_t));
    }
    unsigned char c;
    unsigned char retry_buf[4];
    int retry_ignorecase;
    while (1) {
        retry_ignorecase = 0;
        retry:

        c = str[pos];
        if (retry_ignorecase) {
            if (c >= 'a' && c <= 'z') {
                c -= 'a' - 'A';
            } else if (c >= 'A' && c <= 'Z') {
                c += 'a' - 'A';
            } else {
                goto try_alternative;
            }
        }

        if (node->type == MATCH_END) {
            // End node found!
            // Match cap count is one more than rx cap count since it counts the
            // entire match as the 0 capture.
            m->cap_count = rx->cap_count + 1;
            if (m->cap_allocated < m->cap_count) {
                m->cap_allocated = m->cap_count;
                m->cap_start = realloc(m->cap_start, m->cap_allocated * sizeof(int));
                m->cap_end = realloc(m->cap_end, m->cap_allocated * sizeof(int));
                m->cap_defined = realloc(m->cap_defined, m->cap_allocated * sizeof(char));
                m->cap_str = realloc(m->cap_str, m->cap_allocated * sizeof(char *));
                m->cap_size = realloc(m->cap_size, m->cap_allocated * sizeof(int));
            }
            m->cap_defined[0] = 1;
            m->cap_start[0] = start_pos;
            m->cap_end[0] = pos;
            m->cap_str[0] = str + start_pos;
            m->cap_size[0] = pos - start_pos;
            for (int i = 1; i < m->cap_count; i++) {
                m->cap_defined[i] = 0;
                m->cap_start[i] = 0;
                m->cap_end[i] = 0;
                m->cap_str[i] = NULL;
                m->cap_size[i] = 0;
            }
            for (int i = 0; i < m->path_count; i += 1) {
                path_t *p = &m->path[i];
                if (p->node->type == CAPTURE_START) {
                    int j = p->node->value;
                    m->cap_defined[j] = 1;
                    m->cap_start[j] = p->pos;
                    m->cap_str[j] = str + p->pos;
                } else if (p->node->type == CAPTURE_END) {
                    int j = p->node->value;
                    m->cap_end[j] = p->pos;
                    m->cap_size[j] = p->pos - m->cap_start[j];
                }
            }
            m->success = 1;
            return 1;

        } else if (node->type == CHAR) {
            if (c == '\0') {
                goto try_alternative;
            }
            if (c == node->value) {
                node = node->next;
                pos += 1;
                continue;
            }

        } else if (node->type == BRANCH || node->type == CAPTURE_START || node->type == CAPTURE_END) {
            if (m->path_count == m->path_allocated) {
                m->path_allocated *= 2;
                m->path = realloc(m->path, m->path_allocated * sizeof(path_t));
            }
            path_t *p = &m->path[m->path_count];
            p->pos = pos;
            p->node = node;
            m->path_count += 1;
            node = node->next;
            continue;
        } else if (node->type == GROUP_START || node->type == GROUP_END) {
            node = node->next;
            continue;

        } else if (node->type == QUANTIFIER) {
            if (m->path_count == m->path_allocated) {
                m->path_allocated *= 2;
                m->path = realloc(m->path, m->path_allocated * sizeof(path_t));
            }
            path_t *p = &m->path[m->path_count];
            p->pos = pos;
            p->node = node;
            p->visit = 0;
            m->path_count += 1;
            quantifier_t *qval = (quantifier_t *) (rx->data + node->qval_offset);
            if (qval->greedy) {
                node = qval->next;
                p->visit = 1;
            } else if (qval->min) {
                node = qval->next;
                p->visit = 1;
            }
            else {
                node = node->next;
            }
            continue;

        } else if (node->type == SUBGRAPH_END) {
            // End of a quantified branch
            path_t *p = NULL;
            for (int i = m->path_count - 1; i >= 0; i--) {
                path_t *p2 = &m->path[i];
                if (p2->node == node->next2) {
                    p = p2;
                    break;
                }
            }
            if (!p) {
                goto try_alternative; // This should never happen
            }

            // I *think* I got this correct, it's hard to understand the state of
            // the graph when returning from subgraph under greedy/nongreedy, and to
            // be sure to save captures, etc.
            quantifier_t *qval = (quantifier_t *) (rx->data + p->node->qval_offset);
            if (qval->greedy) {
                if (p->visit == qval->max) {
                    node = p->node->next;
                } else if (p->visit < qval->min) {
                    node = qval->next;
                    p->visit += 1;
                } else {
                    if (m->path_count == m->path_allocated) {
                        m->path_allocated *= 2;
                        m->path = realloc(m->path, m->path_allocated * sizeof(path_t));
                    }
                    path_t *p2 = &m->path[m->path_count];
                    p2->pos = pos;
                    p2->node = p->node;
                    p2->visit = p->visit + 1;
                    m->path_count += 1;
                    node = qval->next;
                }
            }
            else {
                if (p->visit < qval->min) {
                    node = qval->next;
                    p->visit += 1;
                } else {
                    if (m->path_count == m->path_allocated) {
                        m->path_allocated *= 2;
                        m->path = realloc(m->path, m->path_allocated * sizeof(path_t));
                    }
                    path_t *p2 = &m->path[m->path_count];
                    p2->pos = pos;
                    p2->node = p->node;
                    p2->visit = p->visit;
                    m->path_count += 1;
                    node = p->node->next;
                }
            }
            continue;

        } else if (node->type == ASSERTION) {
            if (node->value == ASSERT_SOS) {
                if (pos == 0) {
                    node = node->next;
                    continue;
                }
            } else if (node->value == ASSERT_SOL) {
                if (pos == 0 || str[pos - 1] == '\n') {
                    node = node->next;
                    continue;
                }
            } else if (node->value == ASSERT_EOS) {
                if (c == '\0') {
                    node = node->next;
                    continue;
                }
            } else if (node->value == ASSERT_EOL) {
                if (c == '\0' || c == '\n' || c == '\r') {
                    node = node->next;
                    continue;
                }
            } else if (node->value == ASSERT_SOP) {
                if (pos == start_pos) {
                    node = node->next;
                    continue;
                }
            } else if (node->value == ASSERT_SOW) {
                char c0, w0, w;
                if (pos == 0) {
                    w0 = 0;
                } else {
                    c0 = str[pos - 1];
                    w0 = (c0 >= '0' && c0 <= '9') || (c0 >= 'A' && c0 <= 'Z') || (c0 >= 'a' && c0 <= 'z') || (c0 == '_');
                }
                w = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c == '_');
                if (!w0 && w) {
                    node = node->next;
                    continue;
                }
            } else if (node->value == ASSERT_EOW) {
                char c0, w0, w;
                if (pos == 0) {
                    w0 = 0;
                } else {
                    c0 = str[pos - 1];
                    w0 = (c0 >= '0' && c0 <= '9') || (c0 >= 'A' && c0 <= 'Z') || (c0 >= 'a' && c0 <= 'z') || (c0 == '_');
                }
                w = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c == '_');
                if (w0 && !w) {
                    node = node->next;
                    continue;
                }
            }

        } else if (node->type == CHAR_CLASS) {
            int matched = 0;
            if (c == '\0') {
                goto try_alternative;
            }

            int test_size = utf8_char_size(str, pos);
            char *test = str + pos;

            // If retrying because of ignorecase, copy the char to a buffer to
            // change the case of what's being tested.
            if (retry_ignorecase) {
                memcpy(retry_buf, test, test_size);
                retry_buf[0] = c;
                test = (char *) retry_buf;
            }
            char_class_t *ccval = (char_class_t *) (rx->data + node->ccval_offset);

            // Check the individual values
            char *value = rx->data + ccval->value_offset;
            for (int i = 0; i < ccval->value_count;) {
                int char1_size = utf8_char_size(value, i);
                char *char1 = value + i;
                if (test_size == char1_size && strncmp(test, char1, test_size) == 0) {
                    matched = 1;
                    goto char_class_done;
                }
                i += char1_size;
            }

            // Check the character ranges
            char *range = rx->data + ccval->range_offset;
            for (int i = 0; i < ccval->range_count;) {
                int char1_size = utf8_char_size(range, i);
                char *char1 = range + i;
                i += char1_size;
                int char2_size = utf8_char_size(range, i);
                char *char2 = range + i;
                i += char2_size;

                int ge = (test_size > char1_size) ||
                         (test_size == char1_size && strncmp(test, char1, test_size) >= 0);

                int le = (test_size < char2_size) ||
                         (test_size == char2_size && strncmp(test, char2, test_size) <= 0);

                if (ge && le) {
                    matched = 1;
                    goto char_class_done;
                }
            }

            // Check the character sets
            for (int i = 0; i < ccval->char_set_count; i += 1) {
                char cs = rx->data[ccval->char_set_offset + i];
                if (cs == CS_NOTNL) {
                    if (c != '\n') {
                        matched = 1;
                        goto char_class_done;
                    }
                } else if (cs == CS_DIGIT) {
                    if (c >= '0' && c <= '9') {
                        matched = 1;
                        goto char_class_done;
                    }
                } else if (cs == CS_NOTDIGIT) {
                    if (!(c >= '0' && c <= '9')) {
                        matched = 1;
                        goto char_class_done;
                    }
                } else if (cs == CS_WORD) {
                    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c == '_')) {
                        matched = 1;
                        goto char_class_done;
                    }
                } else if (cs == CS_NOTWORD) {
                    if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c == '_'))) {
                        matched = 1;
                        goto char_class_done;
                    }
                } else if (cs == CS_SPACE) {
                    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                        matched = 1;
                        goto char_class_done;
                    }
                } else if (cs == CS_NOTSPACE) {
                    if (!(c == ' ' || c == '\t' || c == '\n' || c == '\r')) {
                        matched = 1;
                        goto char_class_done;
                    }
                }
            }

            char_class_done:
            if ((matched && !ccval->negated) || (!matched && ccval->negated)) {
                pos += test_size;
                node = node->next;
                continue;
            }

        } else if (node->type == CHAR_SET) {
            if (c == '\0') {
                goto try_alternative;
            }
            if (node->value == CS_ANY) {
                pos += 1;
                node = node->next;
                continue;
            } else if (node->value == CS_NOTNL) {
                if (c != '\n') {
                    pos += 1;
                    node = node->next;
                    continue;
                }
            } else if (node->value == CS_DIGIT) {
                if (c >= '0' && c <= '9') {
                    pos += 1;
                    node = node->next;
                    continue;
                }
            } else if (node->value == CS_NOTDIGIT) {
                if (!(c >= '0' && c <= '9')) {
                    pos += 1;
                    node = node->next;
                    continue;
                }
            } else if (node->value == CS_WORD) {
                if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c == '_')) {
                    pos += 1;
                    node = node->next;
                    continue;
                }
            } else if (node->value == CS_NOTWORD) {
                if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c == '_'))) {
                    pos += 1;
                    node = node->next;
                    continue;
                }
            } else if (node->value == CS_SPACE) {
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                    pos += 1;
                    node = node->next;
                    continue;
                }
            } else if (node->value == CS_NOTSPACE) {
                if (!(c == ' ' || c == '\t' || c == '\n' || c == '\r')) {
                    pos += 1;
                    node = node->next;
                    continue;
                }
            }

        } else if (node->type == EMPTY) {
            node = node->next;
            continue;
        }

        try_alternative:
        if (rx->ignorecase) {
            if (retry_ignorecase) {
                retry_ignorecase = 0;
            }
            else {
                retry_ignorecase = 1;
                goto retry;
            }
        }

        for (int i = m->path_count - 1; i >= 0; i--) {
            path_t *p = &m->path[i];
            if (p->node->type == BRANCH) {
                node = p->node->next2;
                pos = p->pos;
                m->path_count = i;
                goto retry;
            } else if (p->node->type == QUANTIFIER) {
                quantifier_t *qval = (quantifier_t *) (rx->data + p->node->qval_offset);
                if (qval->greedy) {
                    if (p->visit > qval->min) {
                        node = p->node->next;
                        pos = p->pos;
                        m->path_count = i;
                        goto retry;
                    }
                } else {
                    if (p->visit != qval->max) {
                        p->visit += 1;
                        node = qval->next;
                        pos = p->pos;
                        goto retry;
                    }
                }
            }
        }

        // Try another start position
        if (rx->start->type == ASSERTION && (rx->start->value == ASSERT_SOS || rx->start->value == ASSERT_SOP)) {
            break;
        }
        if (!str[start_pos]) {
            break;
        }
        m->path_count = 0;
        start_pos += 1;
        pos = start_pos;
        node = rx->start;
    }
    return 0;
}

void rx_try (char *regexp, char *str) {
    rx_init(rx, regexp);
    if (rx->error) {
        printf("Matching \"%s\" against /%s/.\n", str, regexp);
        printf("%s\n", rx->errorstr);
    }
    else {
        rx_print(rx);
        printf("Matching \"%s\" against /%s/.\n", str, regexp);
        rx_match(rx, m, str, 0);
        if (m->success) {
            printf("Match:");
            for (int i = 0; i < m->cap_count; i++) {
                if (m->cap_defined[i]) {
                    printf(" [%.*s]", m->cap_size[i], m->cap_str[i]);
                }
            }
            printf("\n");
        } else {
            printf("No match\n");
        }
    }
}

void rx_try_global (char *regexp, char *str) {
    rx_init(rx, regexp);
    if (rx->error) {
        printf("Matching \"%s\" against /%s/.\n", str, regexp);
        printf("%s\n", rx->errorstr);
    }
    else {
        rx_print(rx);
        printf("Matching \"%s\" against /%s/ globally.\n", str, regexp);
        int i = 0, j = 0, start_pos = 0;
        for (i = 0;; i += 1) {
            rx_match(rx, m, str, start_pos);
            if (!m->success) {
                break;
            }
            if (j == 0) {
                printf("Match:");
            }
            printf(" [%.*s]", m->cap_size[0], m->cap_str[0]);
            j += 1;
            start_pos = m->cap_end[0];
        }
        if (j == 0) {
            printf("No match\n");
        } else {
            printf("\n");
        }
    }
}

void rx_test (char *regexp, char *str, char *expected) {
    static int test_count = 0;
    test_count += 1;
    rx_init(rx, regexp);
    if (rx->error) {
        printf("%s\n", rx->errorstr);
        exit(1);
    }
    else {
        rx_match(rx, m, str, 0);
        if (m->success) {
            char *got = m->cap_str[0];
            int size = m->cap_size[0];
            int expected_size = strlen(expected);
            if (expected_size != size || strncmp(expected, got, size) != 0) {
                printf("not ");
            }
            printf("ok %d - %s\n", test_count, regexp);
            printnnl("    %s\n", str);
            printnnl("    %s\n", expected);
            if (expected_size != size || strncmp(expected, got, size) != 0) {
                printnnl("    %.*s\n", size, got);
            }
        } else {
            printf("not ok %d - %s\n", test_count, regexp);
            printnnl("    %s\n", str);
            printnnl("    %s\n", expected);
        }
    }
}

void rx_free (rx_t *rx) {
    free(rx->nodes);
    free(rx->cap_start);
    free(rx->or_end);
    free(rx->data);
    free(rx->errorstr);
    free(rx);
}

void rx_matcher_free (matcher_t *m) {
    free(m->path);
    free(m->cap_start);
    free(m->cap_end);
    free(m->cap_defined);
    free(m->cap_str);
    free(m->cap_size);
    free(m);
}

// A test suite of the features.
void run_tests () {
    rx_test("[a]+\\c", "A", "A");
    rx_test("[\\w-]+", "foo-bar", "foo-bar");
    rx_test("[\\-\\w]+", "foo-bar", "foo-bar");
    rx_test("(☃)+", "[☃☃☃]", "☃☃☃");
    rx_test("[\\U00010083]", "a𐂃bc☃☃def", "𐂃");
    rx_test("[\\u2603]{2}", "abc☃☃def", "☃☃");
    rx_test("\\(def\\)", "abc(def)ghi", "(def)");
    rx_test("[\\[]", "abc[def]ghi", "[");
    rx_test("[\\]]", "abc[def]ghi", "]");
    rx_test("[[☁-★]+", "abcdef☃", "☃");
    rx_test("[\\x0e-★]+", "abcdef☃", "abcdef☃");
    rx_test("[\\x0e]", "as\nd\x0e f☃", "\x0e");
    rx_test("[\\n]", "as\ndf☃", "\n");
    rx_test("[α-ω]+", "It's all Ελληνικά to me", "λληνικ");
    rx_test("\\W", "3abc☃", "\xe2");
    rx_test("[\\W]", "3abc☃", "☃");
    rx_test("[\\d\\w]*", "3abc", "3abc");
    rx_test("[a\\dfc-g]*", "3abc", "3a");
    rx_test("\\U00002603", "☃", "☃");
    rx_test("\\u2603", "☃", "☃");
    rx_test("\\xe2\\x98\\x83", "☃", "☃");
    rx_test("\\S+", "abc 2345 def", "abc");
    rx_test("\\s+", "abc 2345 def", " ");
    rx_test("\\W+", "abc 2345 def", " ");
    rx_test("\\w+", "abc 2345 def", "abc");
    rx_test("\\D+", "abc 2345 def", "abc ");
    rx_test("\\d+", "abc 2345 def", "2345");
    rx_test("\\N+", "abc\ndef", "abc");
    rx_test("\\n", "abc\ndef", "\n");
    rx_test("(?:abc)", "abcdef", "abc");
    rx_test("abc\\c", "ABC", "ABC");
    rx_test(".+d", "abcdef", "abcd");
    rx_test("[tea-d]{2,}", "The date is 2019-10-03", "date");
    rx_test("[0-9]+-[0-9]+-[0-9]+", "The date is 2019-10-03", "2019-10-03");
    rx_test("[fed]+", "abc def ghi", "def");
    rx_test("\\Gabc", "abcdefghi", "abc");
    rx_test("\\<def\\>", "abc def ghi", "def");
    rx_test("def$$", "abc\ndef\nghi", "def");
    rx_test("ghi$", "abc\ndef\nghi", "ghi");
    rx_test("^^def", "abc\ndef", "def");
    rx_test("^abc", "abc\ndef", "abc");
    rx_test("ab|a(b|c)*", "abc", "ab");
    rx_test("/(f|o|b|a|r|/){1,10}/", "/foo/o/bar/", "/foo/o/bar/");
    rx_test("/(f|o|b|a|r|/){1,10}?/", "/foo/o/bar/", "/foo/");
    rx_test("a(a|b)*?a", "abababababa", "aba");
    rx_test("a(a|b)*a", "abababababa", "abababababa");
    rx_test("a(a|b){0,}a", "abababababa", "abababababa");
    rx_test("a(a|b){0,}?a", "abababababa", "aba");
    rx_test("ra{2,4}", "jtraaabke", "raaa");
    rx_test("ra{2,4}?", "jtraaabke", "raa");
    rx_test("a{2,4}?b", "jtraaabke", "aaab");
    rx_test("a{2,4}b", "jtraaabke", "aaab");
    rx_test("(0|1|2|3|4|5|6|7|8|9){4}-(0|1|2|3|4|5|6|7|8|9){1,2}-(0|1|2|3|4|5|6|7|8|9){1,2}", "The date is 2019-10-01", "2019-10-01");
    rx_test("(abc|bcd|jt|ghi)+aab", "jtrjtjtaabke", "jtjtaab");
    rx_test("(abc|bcd|jtr|ghi)aab", "jtraabke", "jtraab");
    rx_test("(a|b)*ab", "jtraabke", "aab");
    rx_test("b((an)+)(an)", "bananana", "bananan");
    rx_test("ab|ra|ke", "jtraabke", "ra");
    rx_test("(a{2})*b", "jtraaabke", "aab");
    rx_test("b((an){2}){1,3}", "bananananana", "banananan");
    rx_test("a{2,1}b", "jtraaabke", "ab");
    rx_test("a{2,}b", "jtraaabke", "aaab");
    rx_test("a{2,4}b", "jtraaaaaaaaaabke", "aaaab");
    rx_test("a{2}b", "jtraaabke", "aab");
    rx_test("a*b", "jtraabke", "aab");
    rx_test("(ab|ra|ke)", "jtraabke", "ra");
    rx_test("ra?", "jtraabke", "ra");
    rx_test("ra??", "jtraabke", "r");
    rx_test("ra+?", "jtraabke", "ra");
    rx_test("ra+", "jtraabke", "raa");
    rx_test("a*?b", "jtraabke", "aab");
    rx_test("aab", "jtraabke", "aab");
    rx_test("ra*b", "jtraabke", "raab");
    rx_test("ra*?", "jtraabke", "r");
    //rx_test("a?a?a?a?a?a?a?a?a?a?a?a?a?a?a?aaaaaaaaaaaaaaa", "aaaaaaaaaaaaaaa", "aaaaaaaaaaaaaaa");
}

int main () {
    rx = rx_alloc();
    m = rx_matcher_alloc();
    //run_tests();
    //return 0;
    rx_try("[a]+\\c", "AaAaBbBbaaAaBbBb");
    rx_matcher_free(m);
    rx_free(rx);
    return 0;
}

