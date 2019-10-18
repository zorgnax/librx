librx
=====

This is a regular expression library. It can take a regular expression and match
a string against it. For example, the regular expression:

    (abc)+\d+

Could be used to match against a string like:

    abcabcabc123

It's mostly like perl regular expressions but I doctored it with some features I
think are useful from other regexp languages like `\c` (ignorecase), `\<` (left word
boundary), and `\>` (right word boundary), `^^` (start of line), and `$$` (end of line).

It backtracks using an array instead of recursing.

It handles utf8 nicely without losing the ability to handle strings of arbitrary bytes.

It can reuse memory each time you create or match against a regexp.

It supports greedy/nongreedy quantifiers.

It supports capture groups.

There are no flags (ignore case, multi line, single line, etc.). Instead there are
syntactical features inside of the regexp you can use. For example, instead of `/i`
flag, use the `\c` escape sequence.

It uses string size instead of looking for a zero byte to indicate end of string.
This is useful for binary strings that contain inner zero bytes, and for matching
inside larger strings that you might not want the regexp to go further than a
particular spot.

Regular expression features supported
=====================================

    abc            character
    αβγ            unicode character
    a|b|c          or
    (abc)          capture group
    (?:abc)        non-capture group
    a*             zero or more
    a+             one or more
    a?             zero or one
    a{3,5}         quantified
    a*?            zero or more nongreedy
    a+?            one or more nongreedy
    a??            zero or one nongreedy
    a{3,5}?        quantified nongreedy
    ^abc           start of string
    ^^abc          start of line
    abc$           end of string
    abc$$          end of line
    \Gabc          start of position
    \<abc\>        word boundaries
    \c             ignore case
    [abc]          character class
    [a-z]          ranges in character class
    [α-ω]          unicode character ranges too
    .              any character
    \n             newline
    \r             carriage return
    \t             tab
    \e             escape
    \N             not newline
    \d             digit character
    \D             not digit character
    \w             word character
    \W             not word character
    \s             space character
    \S             not space character
    \x2a           hex character
    \u2603         4 digit hex unicode codepoint
    \U00002603     8 digit hex unicode codepoint

Character oriented square brackets
==================================

The `[]` operator in this regexp library is character oriented, so if you a have a
string like `"☃"` and you apply regexp like this to it:

    [\W]

It would match the entire non-word character of `"☃"`. However if you didnt have
that in a character class and instead used a regexp like:

    \W

It would match the first non-word byte in the string, which for `"☃"`, would match
`"\xe2"`. `"☃"` is represented in utf8 as `"\xe2\x98\x83"`.

Normally, outside of a character class, it doesn't matter if you match byte by
byte since you will match all 3 of them in a row. For example, a regexp of:

    ☃

would match the string `"☃"` since those bytes are in order. In a character class,
the regexp of:

    [☃]

would be wrong since it would think you wanted to match 3 separate bytes no
matter what order they came in.

Testing
=======

The library is tested by running ./test in the project directory. It
reads testdata.txt and outputs test results in tap format. Read
test.c for a description of the format of testdata.txt. If trying to
run it on linux, make sure to set the environment variable LD_LIBRARY_PATH
to ".".

Installation
============

To install globally on your computer, run:

    make
    sudo make install

or you can copy rx.h and rx.c into your project and it should work the same.

Examples
========

There are some examples of how to use the library in example*.c in the project.
example1.c is a very simple example of how to use it. example5.c is a grep clone.

Synopsis
========

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

Captures
========

The match object stores capture information in several fields. A capture is a
region in the regexp surrounded by parentheses. The 0 capture is the entire regexp.

`cap_count (int)`, contains the number of captures found.

`cap_start (int [])`, contains the start position of the capture at a given index.

`cap_end (int [])`, contains the end position of the capture.

`cap_defined (char [])`, contains whether the capture was found.

`cap_str (char * [])`, contains a pointer to the start of the capture.

`cap_size (int [])`, contains the size, in bytes, of the capture.

Recovering Overwritten Captures
===============================

With a regular expression like this:

    (\w+\.?)*

Matched against a string like this:

    foo.bar.baz

The first capture group would be equal to "baz". But during the course of the match
it had equaled "foo.", then "bar.", before finally getting it's value of "baz".
If you wanted to get those values, you still can, by looking at the path array of
the matcher object. The code would look something similar to this:

    for (int i = 0; i < m->path_count; i += 1) {
        path_t *p = m->path + i;
        if (p->node->type == CAPTURE_START) {
            printf("capture %d start %d\n", p->node->value, p->pos);
        } else if (p->node->type == CAPTURE_END) {
            printf("capture %d end %d\n", p->node->value, p->pos);
        }
    }

Function Reference
==================

rx_alloc () -> rx_t *
---------------------

Allocates a new rx_t object.

rx_matcher_alloc () -> matcher_t *
----------------------------------

Allocates a new matcher_t object.

rx_init (rx_t *rx, int regexp_size, char *regexp) -> int
--------------------------------------------------------

Create a new regular expression. Parses the string given in the regexp argument,
which must be given it's size in regexp_size. Returns 0 on error. rx->error will
be true, and rx->errorstr will contain a message about what error occurred. Memory
gained during previous calls to rx_init can be reused, there's no need to call
rx_free() on an rx before creating another rx object.

rx_match (rx_t *rx, matcher_t *m, int str_size, char *str, int start_pos) -> int
--------------------------------------------------------------------------------

Match a regexp against a given string. The string's size must be provided in
str_size. You can start the match at a position other than 0 by using the start_pos
argument. This is useful for doing global matches. Memory gained during previous
calls to rx_match, can be reused, there's no need to call rx_matcher_free() between
using rx_match() again. The matcher object contains info about the match including
whether it was successful and its capture strings. Returns 1 if it matched, 0
otherwise.

rx_free (rx_t *rx)
------------------

Frees the memory in an rx object.

rx_matcher_free (matcher_t *m)
------------------------------

Frees the memory in a matcher object.

rx_match_print (matcher_t *m)
-----------------------------

Prints the result of a match.

rx_print (rx_t *rx)
-------------------

This function will display the regular expression as a graph using the graph-easy
program. If graph-easy is not installed, calling this will just print an error
message. The output for a regexp like:

    (\w+\.?)*

Would look like:

                    ┌─────┐       ┌────┐
      ┌───────────▶ │  7  │ ◀──── │ 0B │
      │             └─────┘       └────┘
      │               │             :
      │               │ (1          :
      │               ▼             ▼
      │             ┌─────┐       ┌────┐
      │             │ 1C  │ ─┐    │ 8E │
      │             └─────┘  │    └────┘
      │               ▲      │      ▲
      │               │      │ ⧹w   :
      │               │      │      :
      │             ┌─────┐  │      :
      │             │ 2B  │ ◀┘      :
      │             └─────┘         :
      │               :             :
      │               :             :
      │               ▼             :
      │  ┌───┐      ┌─────┐         :
      │  │ 5 │ ◀─── │ 3B  │         :
      │  └───┘      └─────┘         :
      │    │          :             :
      │    │          :             :
      │    │          ▼             :
      │    │   .    ┌─────┐         :
      │    └──────▶ │  4  │         :
      │             └─────┘         :
      │               │             :
      │               │ )1          :
      │               ▼             :
      │             ┌─────┐         :
      └──────────── │ 6B  │ ········┘
                    └─────┘

Installation
============

On MacOS and Linux

    make
    make check
    sudo make install

On Windows, you must first install GNU Make, and find vcvars32.bat usually distributed with visual studio and run it. The you can run this:

    make -f Makefile.win
    make -f Makefile.win check
    make -f Makefile.win install

If you want to use the rx_print() function, which outputs utf-8 characters, you need to set cmd.exe to display utf-8 by running:

    chcp 65001

