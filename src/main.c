#include <stdio.h>
#include <stdlib.h>
#include "../include/opcodes.h"
#include "../include/lexer.h"

int main(int argc, char *argv[]) {
    if (argc < 2) return 1;

    FILE *file = fopen(argv[1], "r");
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *buffer = malloc(length + 1);
    fread(buffer, 1, length, file);
    buffer[length] = '\0';
    fclose(file);

    const char *cursor = buffer;
    Token t;
    while ((t = getNextToken(&cursor)).type != TOKEN_EOF) {
        if (t.type == TOKEN_DISPLAY) {
            Token data = getNextToken(&cursor);
            if (data.type == TOKEN_STRING) {
                printf("RUNTIME: %s\n", data.value);
                free(data.value);
            }
        }
    }

    free(buffer);
    return 0;
}
