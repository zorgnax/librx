// This program will match a regexp against a string globally.

#include "rx.h"
#include <stdio.h>

int main () {
    char regexp[] = "\\w+";
    char string[] = "Ricochet pinecone riverside elderberry";

    rx_t *rx = rx_alloc();
    rx_init(rx, sizeof(regexp) - 1, regexp);
    if (rx->error) {
        puts(rx->errorstr);
        return 1;
    }

    matcher_t *m = rx_matcher_alloc();
    int pos = 0;
    while (1) {
        rx_match(rx, m, sizeof(string) - 1, string, pos);
        if (!m->success) {
            break;
        }
        printf("%.*s\n", m->cap_size[0], m->cap_str[0]);
        pos = m->cap_end[0];
    }

    rx_matcher_free(m);
    rx_free(rx);
    return 0;
}

