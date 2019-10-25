// This program is like flex, it will take a file with a .lx extension, and convert
// it into a .c file with a lex() function, that executes code each time a regexp is
// matched. The format is like this:
//
// pre code
// %%
// regexp1
//      code when regexp1 matches
//
// regexp2
//      code when regexp2 matches
//
// @START1 regexp3
//      code when regexp3 matches
//
// @START2 regexp4
//      code when regexp4 matches
//
// %%
// post code
//
// The scanner should be a bit slower than actual flex since this works with an
// NFA not a DFA, and also since this library requires initialization (lex_init())
// to get the exported tables into the form required. It also would be slower since
// it tracks line and column number by default. It also stops after the first match
// found, it doesn't try to find the longest match.
//
// The start conditions can be used to enter regexes that match only when the state
// is active. Use BEGIN(STATE), to enter a state and BEGIN(INITIAL) to return to the
// original (unadorned) start state. These are exclusive start states.
//
// The following special rules are available to insert code at different places
// in the generated lex() function.
//
//     %LEXPRE
//     %LEXPOST
//     %ALLRULESPRE
//     %ALLRULESPOST
//
// These get inserted into the lex() function like this:
//
// int lex () {
//     %LEXPRE
//     while (match) {
//         %ALLRULESPRE
//         switch (rule) {
//             case 0:
//                 ...
//             case 1:
//                 ...
//             case 2:
//                 ...
//         }
//         %ALLRULESPOST
//     }
//     %LEXPOST
// }
//
// Within each rule, you have access to the following variables:
//
// m            the match object, for fine grained access to all captures
// text_size    the size of the matched text
// text         the matched text
// rule         the number of the rule that matched
// pos          the byte position of the start of the token
// line         the line of the start of the token
// column       the column of the start of the token
// pos2         the byte position of the end of the token
// line2        the line of the end of the token
// column2      the column of the end of the token
//

#include "rx.h"
#include "hash.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#ifndef _WIN32
    #include <unistd.h>
#endif

int pre_code_size;
char *pre_code;
int mid_size;
char *mid_str;
int post_code_size;
char *post_code;

int rules_count;
int rules_allocated;
char **rules_str;
int *rules_size;

int start_nodes_count;
int start_nodes_allocated;
node_t **start_nodes;
node_t **start_nodes2;
char **start_nodes_names;

enum special_rule_t {
    LEXPRE,
    LEXPOST,
    ALLRULESPRE,
    ALLRULESPOST,
};

char *special_rules_str[4];
int special_rules_size[4];

rx_t *lex_rx;

char *node_types[] = {
    "EMPTY",
    "TAKE",
    "BRANCH",
    "CAPTURE_START",
    "CAPTURE_END",
    "MATCH_END",
    "ASSERTION",
    "CHAR_CLASS",
    "CHAR_SET",
    "GROUP_START",
    "GROUP_END",
};

char *char_set_types[] = {
    "CS_ANY",
    "CS_NOTNL",
    "CS_DIGIT",
    "CS_NOTDIGIT",
    "CS_WORD",
    "CS_NOTWORD",
    "CS_SPACE",
    "CS_NOTSPACE",
};

char *assert_types[] = {
    "ASSERT_SOS",
    "ASSERT_SOL",
    "ASSERT_EOS",
    "ASSERT_EOL",
    "ASSERT_SOP",
    "ASSERT_SOW",
    "ASSERT_EOW",
};

#ifdef _WIN32
char *strndup (char *str, int size) {
    char *dup = malloc(size + 1);
    memcpy(dup, str, size);
    dup[size] = 0;
    return dup;
}
#endif

void usage () {
    char str[] =
        "This program converts an .lx file to .c.\n"
        "\n"
        "Usage: ./example6 [-h] file...\n"
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

    close(fd);
    *data2 = data;
    return data_size;
}

// Start nodes into the rx object begin with a \G position assertion followed by
// the actual start node, each subsequent regexp that gets added is branched off the
// second start node.
node_t *create_start_node (rx_t *rx, int size, char *name) {
    int i;
    for (i = 0; i < start_nodes_count; i += 1) {
        char *existing_name = start_nodes_names[i];
        if (strncmp(existing_name, name, size) == 0 && existing_name[size] == '\0') {
            break;
        }
    }
    if (i == start_nodes_count) {
        // New start node
        char *name2 = strndup(name, size);
        node_t *node = rx_node_create(rx);
        node_t *node2 = rx_node_create(rx);
        node->type = ASSERTION;
        node->value = ASSERT_SOP;
        node->next = node2;
        start_nodes_names[start_nodes_count] = name2;
        start_nodes[start_nodes_count] = node;
        start_nodes2[start_nodes_count] = node2;
        start_nodes_count += 1;
        return node2;
    } else {
        // Branch off an existing start node
        node_t *node = start_nodes2[i];
        node_t *node2 = rx_node_create(rx);
        node_t *node3 = rx_node_create(rx);
        *node2 = *node;
        node->type = BRANCH;
        node->next = node2;
        node->next2 = node3;
        return node3;
    }
}

void print_string_escaped (FILE *fp, int size, char *str) {
    if (!str) {
        fprintf(fp, "NULL");
        return;
    }
    fprintf(fp, "\"");
    for (int i = 0; i < size; i += 1) {
        char c = str[i];
        if (c == '\n') {
            fprintf(fp, "\\n");
        } else if (c == '\r') {
            fprintf(fp, "\\r");
        } else if (c == '\t') {
            fprintf(fp, "\\t");
        } else if (c >= 0x20 && c < 0x7f) {
            fprintf(fp, "%c", c);
        } else {
            fprintf(fp, "\\x%02x", c);
        }
    }
    fprintf(fp, "\"");
}

void output_file (char *file, char *file2) {
    int i;
    printf("outputting to %s\n", file2);
    printf("%d rules\n", rules_count);
    printf("%d starting states\n", start_nodes_count);
    printf("%d nodes\n", lex_rx->nodes_count);
    FILE *fp = fopen(file2, "w");

    fprintf(fp, "// This file was generated from %s\n\n", file);

    fprintf(fp, "#include \"rx.h\"\n");
    fprintf(fp, "#include <stdio.h>\n");
    fprintf(fp, "#include <stdlib.h>\n");
    fprintf(fp, "#include <string.h>\n");
    fprintf(fp, "#include <errno.h>\n");
    fprintf(fp, "#include <fcntl.h>\n");
    fprintf(fp, "#ifndef _WIN32\n");
    fprintf(fp, "    #include <unistd.h>\n");
    fprintf(fp, "#endif\n\n");

    fprintf(fp, "%.*s\n", pre_code_size, pre_code);

    hash_t *node_index = hash_init(hash_direct_hash, hash_direct_equal);
    for (i = 0; i < lex_rx->nodes_count; i += 1) {
        node_t *n = lex_rx->nodes[i];
        hash_insert(node_index, n, (void *) (long) i);
    }

    fprintf(fp, "char_class_t char_classes[] = {\n");
    for (i = 0; i < lex_rx->char_classes_count; i += 1) {
        char_class_t *c = lex_rx->char_classes[i];
        hash_insert(node_index, c, (void *) (long) i);
        fprintf(fp, "    {%3d, %3d, ", c->negated, c->values_count);
        print_string_escaped(fp, c->values_count, c->values);
        fprintf(fp, ", %3d, ", c->ranges_count);
        print_string_escaped(fp, c->ranges_count, c->ranges);
        fprintf(fp, ", %3d, ", c->char_sets_count);
        print_string_escaped(fp, c->char_sets_count, c->char_sets);
        fprintf(fp, "},\n");
    }
    if (i == 0) {
        fprintf(fp, "    {0, 0, NULL, 0, NULL, 0, NULL},\n");
    }
    fprintf(fp, "};\n\n");

    fprintf(fp, "node_t nodes[] = {\n");
    for (i = 0; i < lex_rx->nodes_count; i += 1) {
        node_t *n = lex_rx->nodes[i];
        int next = (int) hash_lookup(node_index, n->next);
        fprintf(fp, "    {%12s, (node_t *) %5d, ", node_types[n->type], next);
        if (n->type == TAKE) {
            if (n->value == '\n') {
                fprintf(fp, "      '\\n'");
            } else if (n->value == '\r') {
                fprintf(fp, "      '\\r'");
            } else if (n->value == '\t') {
                fprintf(fp, "      '\\t'");
            } else if (n->value == '\'') {
                fprintf(fp, "       '\\''");
            } else if (n->value == '\\') {
                fprintf(fp, "       '\\\\'");
            } else if (n->value >= 0x20 && n->value < 0x7f) {
                fprintf(fp, "       '%c'", n->value);
            } else {
                fprintf(fp, "    '\\x%02x'", n->value);
            }
        } else if (n->type == BRANCH) {
            int next2 = (int) hash_lookup(node_index, n->next2);
            fprintf(fp, "%10d", next2);
        } else if (n->type == CHAR_SET) {
            fprintf(fp, "%10s", char_set_types[n->value]);
        } else if (n->type == ASSERTION) {
            fprintf(fp, "%10s", assert_types[n->value]);
        } else if (n->type == CHAR_CLASS) {
            char_class_t *c = n->ccval;
            int index = (int) hash_lookup(node_index, c);
            fprintf(fp, "%10d", index);
        } else {
            fprintf(fp, "%10d", n->value);
        }
        fprintf(fp, "},\n");
    }
    fprintf(fp, "};\n\n");

    fprintf(fp, "enum start_node_t {\n");
    for (i = 0; i < start_nodes_count; i += 1) {
        char *name = start_nodes_names[i];
        fprintf(fp, "    %s,\n", name);
    }
    fprintf(fp, "};\n\n");

    fprintf(fp, "node_t *start_nodes[] = {\n");
    for (i = 0; i < start_nodes_count; i += 1) {
        node_t *n = start_nodes[i];
        int index = (int) hash_lookup(node_index, n);
        fprintf(fp, "    nodes + %d,\n", index);
    }
    fprintf(fp, "};\n\n");

    fprintf(fp, "rx_t *rx;\n");
    fprintf(fp, "matcher_t *m;\n");
    fprintf(fp, "int str_allocated;\n");
    fprintf(fp, "int str_size;\n");
    fprintf(fp, "char *str;\n");
    fprintf(fp, "int pos = 0;\n");
    fprintf(fp, "int line = 1;\n");
    fprintf(fp, "int column = 1;\n");
    fprintf(fp, "int pos2 = 0;\n");
    fprintf(fp, "int line2 = 1;\n");
    fprintf(fp, "int column2 = 1;\n");
    fprintf(fp, "int text_size;\n");
    fprintf(fp, "char *text;\n");
    fprintf(fp, "int rule;\n");
    fprintf(fp, "#define BEGIN(n) rx->start = start_nodes[n]\n\n");

    fprintf(fp, "int read_file (int fd, char *file) {\n");
    fprintf(fp, "    str_size = 0;\n");
    fprintf(fp, "    int opened = 0;\n");
    fprintf(fp, "    if (fd == -1) {\n");
    fprintf(fp, "        fd = open(file, O_RDONLY);\n");
    fprintf(fp, "        if (fd < 0) {\n");
    fprintf(fp, "            fprintf(stderr, \"Can't open %%s: %%s\\n\", file, strerror(errno));\n");
    fprintf(fp, "            return 0;\n");
    fprintf(fp, "        }\n");
    fprintf(fp, "        opened = 1;\n");
    fprintf(fp, "    }\n");
    fprintf(fp, "    int chunk_size = 16384;\n");
    fprintf(fp, "    if (str_allocated == 0) {\n");
    fprintf(fp, "        str_allocated = 2 * chunk_size;\n");
    fprintf(fp, "        str = malloc(str_allocated);\n");
    fprintf(fp, "    }\n");
    fprintf(fp, "    int retval;\n");
    fprintf(fp, "    while (1) {\n");
    fprintf(fp, "        if (str_size + chunk_size > str_allocated) {\n");
    fprintf(fp, "            str_allocated *= 2;\n");
    fprintf(fp, "            str = realloc(str, str_allocated);\n");
    fprintf(fp, "        }\n");
    fprintf(fp, "        retval = read(fd, str + str_size, chunk_size);\n");
    fprintf(fp, "        if (retval < 0) {\n");
    fprintf(fp, "            fprintf(stderr, \"Can't read file: %%s\\n\", strerror(errno));\n");
    fprintf(fp, "            return 0;\n");
    fprintf(fp, "        } else if (retval == 0) {\n");
    fprintf(fp, "            break;\n");
    fprintf(fp, "        } else {\n");
    fprintf(fp, "            str_size += retval;\n");
    fprintf(fp, "        }\n");
    fprintf(fp, "    }\n");
    fprintf(fp, "    if (opened) {\n");
    fprintf(fp, "        close(fd);\n");
    fprintf(fp, "    }\n");
    fprintf(fp, "    return 1;\n");
    fprintf(fp, "}\n\n");

    fprintf(fp, "int lex_init () {\n");
    fprintf(fp, "    rx = calloc(1, sizeof(rx_t));\n");
    fprintf(fp, "    m = rx_matcher_alloc();\n");
    fprintf(fp, "    int nodes_count = sizeof(nodes) / sizeof(nodes[0]);\n");
    fprintf(fp, "    rx->nodes_count = nodes_count;\n");
    fprintf(fp, "    rx->nodes = malloc(nodes_count * sizeof(node_t *));\n");
    fprintf(fp, "    int i;\n");
    fprintf(fp, "    for (i = 0; i < nodes_count; i += 1) {\n");
    fprintf(fp, "        node_t *n = nodes + i;\n");
    fprintf(fp, "        rx->nodes[i] = n;\n");
    fprintf(fp, "        if (n->type != MATCH_END) {\n");
    fprintf(fp, "            n->next = nodes + (int) n->next;\n");
    fprintf(fp, "        }\n");
    fprintf(fp, "        if (n->type == BRANCH) {\n");
    fprintf(fp, "            n->next2 = nodes + (int) n->next2;\n");
    fprintf(fp, "        } else if (n->type == CHAR_CLASS) {\n");
    fprintf(fp, "            n->ccval = char_classes + n->value;\n");
    fprintf(fp, "        }\n");
    fprintf(fp, "    }\n");
    fprintf(fp, "    BEGIN(INITIAL);\n");
    fprintf(fp, "    return 1;\n");
    fprintf(fp, "}\n\n");

    fprintf(fp, "int lex () {\n");
    if (special_rules_size[LEXPRE]) {
        fprintf(fp, "%.*s\n", special_rules_size[LEXPRE], special_rules_str[LEXPRE]);
    }
    fprintf(fp, "    while (1) {\n");
    fprintf(fp, "        pos = pos2;\n");
    fprintf(fp, "        line = line2;\n");
    fprintf(fp, "        column = column2;\n");
    fprintf(fp, "        rx_match(rx, m, str_size, str, pos);\n");
    fprintf(fp, "        if (!m->success) {\n");
    fprintf(fp, "            if (pos == str_size) {\n");
    fprintf(fp, "                return 0;\n");
    fprintf(fp, "            } else {\n");
    fprintf(fp, "                fprintf(stderr, \"token not found.\\n\");\n");
    fprintf(fp, "                return 0;\n");
    fprintf(fp, "            }\n");
    fprintf(fp, "        }\n");
    fprintf(fp, "        pos2 += m->cap_size[0];\n");
    fprintf(fp, "        for (int i = pos; i < pos2; i += 1) {\n");
    fprintf(fp, "            if (str[i] == '\\n') {\n");
    fprintf(fp, "                line2 += 1;\n");
    fprintf(fp, "                column2 = 1;\n");
    fprintf(fp, "            } else {\n");
    fprintf(fp, "                column2 += 1;\n");
    fprintf(fp, "            }\n");
    fprintf(fp, "        }\n");
    fprintf(fp, "        rule = m->value;\n");
    fprintf(fp, "        text_size = m->cap_size[0];\n");
    fprintf(fp, "        text = m->cap_str[0];\n");
    if (special_rules_size[ALLRULESPRE]) {
        fprintf(fp, "%.*s\n", special_rules_size[ALLRULESPRE], special_rules_str[ALLRULESPRE]);
    }
    fprintf(fp, "        switch (rule) {\n");
    for (i = 0; i < rules_count; i += 1) {
        fprintf(fp, "        case %d:\n", i);
        fprintf(fp, "%.*s\n", rules_size[i], rules_str[i]);
        fprintf(fp, "        break;\n\n");
    }
    fprintf(fp, "        }\n");
    if (special_rules_size[ALLRULESPOST]) {
        fprintf(fp, "%.*s\n", special_rules_size[ALLRULESPOST], special_rules_str[ALLRULESPOST]);
    }
    fprintf(fp, "    }\n");
    if (special_rules_size[LEXPOST]) {
        fprintf(fp, "%.*s\n", special_rules_size[LEXPOST], special_rules_str[LEXPOST]);
    }
    fprintf(fp, "    return 0;\n");
    fprintf(fp, "}\n\n");

    fprintf(fp, "%.*s\n", post_code_size, post_code);

    fclose(fp);
    hash_free(node_index);
}

void process_file (char *file) {
    char *data;
    int data_size = read_file(file, &data);

    // Regexp for the main sections of the .lx file
    char regexp1[] =
        "^(.*?)"
        "^^%%\\n"
        "(.*?)"
        "^^%%\\n"
        "(.*?)$";
    rx_t *rx = rx_alloc();
    rx_init(rx, sizeof(regexp1) - 1, regexp1);
    matcher_t *m = rx_matcher_alloc();
    rx_match(rx, m, data_size, data, 0);
    if (!m->success) {
        printf("Expected 3 sections separated by %%%%\n");
        exit(1);
    }

    pre_code_size = m->cap_size[1];
    pre_code = m->cap_str[1];
    mid_size = m->cap_size[2];
    mid_str = m->cap_str[2];
    post_code_size = m->cap_size[3];
    post_code = m->cap_str[3];

    rules_count = 0;
    rules_allocated = 10;
    rules_str = malloc(rules_allocated * sizeof(char *));
    rules_size = malloc(rules_allocated * sizeof(int));

    start_nodes_count = 0;
    start_nodes_allocated = 10;
    start_nodes = malloc(start_nodes_allocated * sizeof(node_t *));
    start_nodes2 = malloc(start_nodes_allocated * sizeof(node_t *));
    start_nodes_names = malloc(start_nodes_allocated * sizeof(char *));

    // Regexp for each individual rule in the .lx file
    char regexp2[] =
        "^^(?:@(?:([\\w]+),?)+[ \t]+)?(\\S\\N*)\\n"
        "((?:[ \\t]*\\n|"
        "[ \\t]+\\N*\\n)*)";
    rx_init(rx, sizeof(regexp2) - 1, regexp2);
    int pos = 0;

    lex_rx = rx_alloc();

    while (1) {
        rx_match(rx, m, mid_size, mid_str, pos);
        if (!m->success) {
            break;
        }
        int lex_regexp_size = m->cap_size[2];
        char *lex_regexp = m->cap_str[2];
        if (lex_regexp_size >= 2 && strncmp(lex_regexp, "//", 2) == 0) {
            pos = m->cap_end[0];
            continue;
        }

        // printf("lex regexp is %.*s\n", lex_regexp_size, lex_regexp);

        if (lex_regexp_size >= 1 && lex_regexp[0] == '%') {
            if (strncmp("%ALLRULESPRE", lex_regexp, lex_regexp_size) == 0) {
                special_rules_size[ALLRULESPRE] = m->cap_size[3];
                special_rules_str[ALLRULESPRE] = m->cap_str[3];
            } else if (strncmp("%ALLRULESPOST", lex_regexp, lex_regexp_size) == 0) {
                special_rules_size[ALLRULESPOST] = m->cap_size[3];
                special_rules_str[ALLRULESPOST] = m->cap_str[3];
            } else if (strncmp("%LEXPRE", lex_regexp, lex_regexp_size) == 0) {
                special_rules_size[LEXPRE] = m->cap_size[3];
                special_rules_str[LEXPRE] = m->cap_str[3];
            } else if (strncmp("%LEXPOST", lex_regexp, lex_regexp_size) == 0) {
                special_rules_size[LEXPOST] = m->cap_size[3];
                special_rules_str[LEXPOST] = m->cap_str[3];
            } else {
                printf("Unrecognized special rule \"%.*s\".\n", lex_regexp_size, lex_regexp);
                exit(1);
            }
            pos = m->cap_end[0];
            continue;
        }

        if (rules_count >= rules_allocated) {
            rules_allocated *= 2;
            rules_str = realloc(rules_str, rules_allocated * sizeof(char *));
            rules_size = realloc(rules_size, rules_allocated * sizeof(int));
        }
        rules_size[rules_count] = m->cap_size[3];
        rules_str[rules_count] = m->cap_str[3];

        // Extract the start position names
        if (!m->cap_defined[1]) {
            node_t *start_node = create_start_node(lex_rx, 7, "INITIAL");
            if (!rx_init_start(lex_rx, lex_regexp_size, lex_regexp, start_node, rules_count)) {
                printf("%s\n", lex_rx->errorstr);
                exit(1);
            }
        } else {
            char *name;
            int size;
            for (int i = 0; i < m->path_count; i += 1) {
                path_t *p = m->path + i;
                if (p->node->type == CAPTURE_START && p->node->value == 1) {
                    name = mid_str + p->pos;
                } else if (p->node->type == CAPTURE_END && p->node->value == 1) {
                    size = mid_str + p->pos - name;
                    node_t *start_node = create_start_node(lex_rx, size, name);
                    if (!rx_init_start(lex_rx, lex_regexp_size, lex_regexp, start_node, rules_count)) {
                        printf("%s\n", lex_rx->errorstr);
                        exit(1);
                    }
                }
            }
        }
        rules_count += 1;
        pos = m->cap_end[0];
    }

    char *file2;
    int file_size = strlen(file);
    if (file_size > 3 && strcmp(file + file_size - 3, ".lx") == 0) {
        file2 = malloc(file_size);
        sprintf(file2, "%.*s.c", file_size - 3, file);
    } else {
        file2 = strdup("scanner.c");
    }
    output_file(file, file2);
    free(file2);
}

#define eq(a, b) (strcmp(a, b) == 0)

int main (int argc, char **argv) {
    int argc2 = 1;

    for (int i = 1; i < argc; i += 1) {
        if (eq(argv[i], "-h") || eq(argv[i], "--help") || eq(argv[i], "-help") || eq(argv[i], "-?")) {
            usage();
        } else if (eq(argv[i], "--")) {
            for (i += 1; i < argc; i += 1) {
                argv[argc2] = argv[i];
                argc2 += 1;
            }
        } else if (argv[i][0] == '-') {
            printf("Unrecognized option \"%s\"\n", argv[i]);
            return 1;
        } else {
            argv[argc2] = argv[i];
            argc2 += 1;
        }
    }

    if (argc2 > 2) {
        printf("Too may file arguments.\n");
        return 1;
    }
    if (argc2 == 1) {
        argv[argc2] = "example6b.lx";
        argc2 += 1;
    }

    process_file(argv[1]);
    return 0;
}

