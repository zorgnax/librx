//
// librx
//

#include "rx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

static rx_t *rx;
static matcher_t *m;

// Reads a utf8 character from str and determines how many bytes it is. If the str
// doesn't contain a proper utf8 character, it returns 1. str needs to have at
// least one byte in it, but can end right after that, even if the byte sequence is
// invalid.
int rx_utf8_char_size (int str_size, char *str, int pos) {
    char c = str[pos];
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
    if (pos + size > str_size) {
        return 1;
    }
    for (int i = 1; i < size; i += 1) {
        c = str[pos + i];
        if ((c & 0xc0) != 0x80) {
            // Didn't find a proper utf8 continuation byte 10xx_xxxx
            return 1;
        }
    }
    return size;
}

int rx_int_to_utf8 (unsigned int value, char *str) {
    if (value < 0x80) {
        str[0] = value;
        return 1;
    } else if (value < 0x800) {
        str[0] = 0x80 | ((value & 0x7c) >> 6);
        str[1] = 0x80 | ((value & 0x3f) >> 0);
        return 2;
    } else if (value < 0x10000) {
        str[0] = 0xe0 | ((value & 0xf000) >> 12);
        str[1] = 0x80 | ((value & 0x0fc0) >> 6);
        str[2] = 0x80 | ((value & 0x003f) >> 0);
        return 3;
    } else if (value < 0x200000) {
        str[0] = 0xf0 | ((value & 0x1c0000) >> 18);
        str[1] = 0x80 | ((value & 0x03f000) >> 12);
        str[2] = 0x80 | ((value & 0x000fc0) >> 6);
        str[3] = 0x80 | ((value & 0x00003f) >> 0);
        return 4;
    } else {
        return 0;
    }
}

int rx_hex_to_int (char *str, int size, unsigned int *dest) {
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

void rx_match_print (matcher_t *m) {
    if (m->success) {
        printf("matched\n");
    } else {
        printf("it didn't match\n");
        return;
    }

    for (int i = 0; i < m->cap_count; i += 1) {
        if (m->cap_defined[i]) {
            printf("%d: %.*s\n", i, m->cap_size[i], m->cap_str[i]);
        } else {
            printf("%d: ~\n", i);
        }
    }

    for (int i = 0; i < m->path_count; i += 1) {
        path_t *p = &(m->path[i]);
        if (p->node->type == CAPTURE_START) {
            printf("capture %d start %d\n", p->node->value, p->pos);
        } else if (p->node->type == CAPTURE_END) {
            printf("capture %d end %d\n", p->node->value, p->pos);
        }
    }
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
    int regexp_size = rx->regexp_size;
    char c1, c2;
    char char1[4], char2[4];
    int char1_size = 0, char2_size = 0;
    char seen_dash = 0;
    char seen_special = '\0';

    while (pos < regexp_size) {
        c1 = regexp[pos];
        if (c1 == ']') {
            break;
        } else if (c1 == '-' && !seen_dash) {
            seen_dash = 1;
            pos += 1;
            continue;
        } else if (c1 == '\\') {
            if (pos + 1 >= regexp_size) {
                return rx_error(rx, "Expected character after \\.");
            }
            c2 = regexp[pos + 1];
            if (c2 == 'd' || c2 == 'D' || c2 == 'w' || c2 == 'W' || c2 == 's' || c2 == 'S' || c2 == 'N') {
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
                if (pos + 3 >= regexp_size) {
                    return rx_error(rx, "Expected 2 characters after \\x.");
                }
                unsigned int i;
                if (!rx_hex_to_int(regexp + pos + 2, 2, &i)) {
                    return rx_error(rx, "Expected 2 hex digits after \\x.");
                }
                char2_size = 1;
                char2[0] = i;
                pos += 4;

            } else if (c2 == 'u' || c2 == 'U') {
                // \u2603 or \U00002603
                int count = c2 == 'u' ? 4 : 8;
                if (pos + 1 + count >= regexp_size) {
                    return rx_error(rx, "Expected %d characters after \\%c.", count, c2);
                }
                unsigned int i;
                if (!rx_hex_to_int(regexp + pos + 2, count, &i)) {
                    return rx_error(rx, "Expected %d hex digits after \\%c.", count, c2);
                }
                char2_size = rx_int_to_utf8(i, char2);
                if (!char2_size) {
                    return rx_error(rx, "Invalid \\%c sequence.", c2);
                }
                pos += 2 + count;

            } else {
                // Any unrecognized backslash escape, for example \]
                pos += 1;
                char2_size = rx_utf8_char_size(regexp_size, regexp, pos);
                memcpy(char2, regexp + pos, char2_size);
                pos += char2_size;
            }

        } else {
            // An unspecial character, for example a or ☃
            char2_size = rx_utf8_char_size(regexp_size, regexp, pos);
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
    if (pos >= regexp_size || regexp[pos] != ']') {
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
    int regexp_size = rx->regexp_size;
    char_class_t ccval = {};
    ccval.str = regexp + pos;

    if (pos + 1 >= regexp_size) {
        return rx_error(rx, "Expected a character after [.");
    }
    pos += 1;
    char c1 = regexp[pos];
    if (c1 == '^') {
        ccval.negated = 1;
        if (pos + 1 >= regexp_size) {
            return rx_error(rx, "Expected a character in [.");
        }
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
    int regexp_size = rx->regexp_size;
    char *regexp = rx->regexp;
    char c;
    int min = 0, minp = 1, max = 0, maxp = 1, greedy = 1;
    pos += 1;
    for (; pos < regexp_size; pos += 1) {
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
    for (; pos < regexp_size; pos += 1) {
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
    c = (pos + 1 < regexp_size) ? regexp[pos + 1] : '\0';
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
int rx_init (rx_t *rx, int regexp_size, char *regexp) {
    rx->error = 0;

    // Preallocate the space needed for nodes. Since each character can
    // add at most 2 nodes, the count of nodes needed shouldn't be
    // more than 2 * (x + 1). Also count the max capture nest depth, which can't
    // take into account ) since it could be \) or [)] without a full parse, so
    // set it to the number of times ( appears in the regexp.
    //
    // All allocations that occur in rx_init are reuseable in subsequent calls to it.
    // Nothing is freed until rx_free() is called, and that only needs to happen
    // once when the user is entirely done with regexps.
    int cap_depth = 0;
    int max_cap_depth = 0;
    int max_node_count = (regexp_size + 1) * 2;
    for (int pos = 0; pos <= regexp_size; pos += 1) {
        char c = regexp[pos];
        if (c == '(') {
            max_cap_depth += 1;
        }
    }

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
    rx->regexp_size = regexp_size;
    rx->regexp = regexp;
    rx->start = rx_node_create(rx);
    node_t *node = rx->start;
    node_t *atom_start = NULL;
    node_t *or_end = NULL;
    rx->cap_count = 0;
    rx->ignorecase = 0;

    for (int pos = 0; pos < regexp_size; pos += 1) {
        unsigned char c = regexp[pos];
        if (c == '(') {
            if (pos + 2 < regexp_size && regexp[pos + 1] == '?' && regexp[pos + 2] == ':') {
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
            char c2 = (pos + 1 < regexp_size) ? regexp[pos + 1] : '\0';
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
            char c2 = (pos + 1 < regexp_size) ? regexp[pos + 1] : '\0';
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
            char c2 = (pos + 1 < regexp_size) ? regexp[pos + 1] : '\0';
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
            if (pos + 1 == regexp_size) {
                return rx_error(rx, "Expected character after \\.");
            }
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
                if (pos + 2 >= regexp_size) {
                    return rx_error(rx, "Expected 2 characters after \\x.");
                }
                unsigned int i;
                if (!rx_hex_to_int(regexp + pos + 1, 2, &i)) {
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
                if (pos + count >= regexp_size) {
                    return rx_error(rx, "Expected %d characters after \\%c.", count, c2);
                }
                unsigned int i;
                if (!rx_hex_to_int(regexp + pos + 1, count, &i)) {
                    return rx_error(rx, "Expected %d hex digits after \\%c.", count, c2);
                }
                pos += count;
                char str[4];
                int str_size = rx_int_to_utf8(i, str);
                if (!str_size) {
                    return rx_error(rx, "Invalid \\%c sequence.", c2);
                }
                atom_start = node;
                for (i = 0; i < str_size; i += 1) {
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
            char c2 = (pos + 1 < regexp_size) ? regexp[pos + 1] : '\0';
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
            char c2 = (pos + 1 < regexp_size) ? regexp[pos + 1] : '\0';
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
// memory allocated for previous matches. All the captures are references into the
// original string.
int rx_match (rx_t *rx, matcher_t *m, int str_size, char *str, int start_pos) {
    m->str_size = str_size;
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

        c = (pos < str_size) ? str[pos] : '\0';
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
            if (pos >= str_size) {
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
                if (pos == str_size) {
                    node = node->next;
                    continue;
                }
            } else if (node->value == ASSERT_EOL) {
                if (pos == str_size || c == '\n' || c == '\r') {
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
            if (pos >= str_size) {
                goto try_alternative;
            }

            int test_size = rx_utf8_char_size(str_size, str, pos);
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
                int char1_size = rx_utf8_char_size(ccval->value_count, value, i);
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
                int char1_size = rx_utf8_char_size(ccval->range_count, range, i);
                char *char1 = range + i;
                i += char1_size;
                int char2_size = rx_utf8_char_size(ccval->range_count, range, i);
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
            if (pos >= str_size) {
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
                    // TODO might make more sense to increment by an entire character
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

// TODO a recursive grep program
// TODO synopsis and code examples in README
// TODO output from rx_print in README
// TODO output from rx_match_print in README
// TODO port to linux

