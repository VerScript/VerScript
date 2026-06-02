#ifndef LEXER_H
#define LEXER_H

typedef enum {
    TOKEN_DISPLAY,
    TOKEN_STRING,
    TOKEN_EOF,
    TOKEN_ERROR
} TokenType;

typedef struct {
    TokenType type;
    char *value;
} Token;

Token getNextToken(const char **cursor);

#endif
