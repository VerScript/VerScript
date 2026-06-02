#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../../include/lexer.h"

Token getNextToken(const char **cursor) {
    Token token;
    while (isspace(**cursor)) (*cursor)++; // Skip whitespace

    if (**cursor == '\0') {
        token.type = TOKEN_EOF;
        return token;
    }

    if (strncmp(*cursor, "display", 7) == 0 && isspace((*cursor)[7])) {
        token.type = TOKEN_DISPLAY;
        *cursor += 7;
        return token;
    }

    if (**cursor == '"') {
        (*cursor)++;
        const char *start = *cursor;
        while (**cursor != '"' && **cursor != '\0') (*cursor)++;
        size_t len = *cursor - start;
        token.value = malloc(len + 1);
        strncpy(token.value, start, len);
        token.value[len] = '\0';
        token.type = TOKEN_STRING;
        if (**cursor == '"') (*cursor)++;
        return token;
    }

    token.type = TOKEN_ERROR;
    return token;
}
