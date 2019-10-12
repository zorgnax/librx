// This program will match a regexp against a string.

#include "rx.h"
#include <stdio.h>

int main () {
    char regexp[] = "\\d+\\.\\d+\\.\\d+\\.\\d+";
    char string[] = "There's no place like 127.0.0.1.";

    rx_t *rx = rx_alloc();
    rx_init(rx, sizeof(regexp) - 1, regexp);
    if (rx->error) {
        puts(rx->errorstr);
        return 1;
    }

    matcher_t *m = rx_matcher_alloc();
    rx_match(rx, m, sizeof(string) - 1, string, 0);
    if (m->success) {
        printf("%.*s\n", m->cap_size[0], m->cap_str[0]);
    }

    rx_matcher_free(m);
    rx_free(rx);
    return 0;
}

