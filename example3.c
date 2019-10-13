// This program will take a regexp and a string on the command line and run them.

#include "rx.h"
#include <stdio.h>
#include <string.h>

int main (int argc, char **argv) {
    if (argc != 3) {
        printf("Usage: example3 <regexp> <string>\n");
        return 1;
    }

    char *regexp = argv[1];
    char *string = argv[2];

    rx_t *rx = rx_alloc();
    rx_init(rx, strlen(regexp), regexp);
    if (rx->error) {
        puts(rx->errorstr);
        return 1;
    }
    rx_print(rx);

    matcher_t *m = rx_matcher_alloc();
    rx_match(rx, m, strlen(string), string, 0);
    rx_match_print(m);

    rx_matcher_free(m);
    rx_free(rx);
    return 0;
}

