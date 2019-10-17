#ifndef __RX_H__
#define __RX_H__

enum {
    EMPTY,
    TAKE,
    BRANCH,
    CAPTURE_START,
    CAPTURE_END,
    SUBGRAPH_END,
    MATCH_END,
    QUANTIFIER,
    ASSERTION,
    CHAR_CLASS,
    CHAR_SET,
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
    int regexp_size;
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

rx_t *rx_alloc ();
matcher_t *rx_matcher_alloc ();
int rx_init (rx_t *rx, int regexp_size, char *regexp);
void rx_print (rx_t *rx);
void rx_match_print (matcher_t *m);
int rx_match (rx_t *rx, matcher_t *m, int str_size, char *str, int start_pos);
int rx_hex_to_int (char *str, int size, unsigned int *dest);
int rx_int_to_utf8 (unsigned int value, char *str);
int rx_utf8_char_size (int str_size, char *str, int pos);
void rx_matcher_free (matcher_t *m);
void rx_free (rx_t *rx);

#endif

