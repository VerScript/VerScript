#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/opcodes.h"
#include "../include/lexer.h"

typedef enum { VAR_INT, VAR_STRING, VAR_BOOL } VarType;

typedef struct {
    char *name;
    VarType type;
    int int_val;
    char *string_val;
} Variable;

Variable *symtable = NULL;
int var_count = 0;
int var_capacity = 0;

Variable* get_var(const char *name) {
    for (int i = 0; i < var_count; i++) {
        if (strcmp(symtable[i].name, name) == 0) return &symtable[i];
    }
    return NULL;
}

Variable* set_var(const char *name) {
    Variable* v = get_var(name);
    if (v) return v;

    if (var_count >= var_capacity) {
        var_capacity = (var_capacity == 0) ? 100 : var_capacity * 2;
        symtable = realloc(symtable, var_capacity * sizeof(Variable));
        if (!symtable) {
            printf("ERROR: Memory allocation failed\n");
            exit(1);
        }
    }

    v = &symtable[var_count++];
    v->name = strdup(name);
    v->type = VAR_INT;
    v->int_val = 0;
    v->string_val = NULL;
    return v;
}

Token peekToken(const char **cursor) {
    const char *temp = *cursor;
    return getNextToken(&temp);
}

void freeToken(Token *t) {
    if (t->value) {
        free(t->value);
        t->value = NULL;
    }
}

// Simple evaluator for left-to-right math
int evaluate_expression(const char **cursor, char **out_str, int *out_type) {
    *out_str = NULL;
    *out_type = VAR_INT;
    Token t = getNextToken(cursor);
    int acc = 0;
    int sign = 1;

    if (t.type == TOKEN_MINUS) {
        sign = -1;
        freeToken(&t);
        t = getNextToken(cursor);
    }
    
    if (t.type == TOKEN_NUMBER) {
        acc = atoi(t.value) * sign;
    } else if (t.type == TOKEN_TRUE) {
        if (sign == -1) {
            printf("ERROR: Invalid operand for unary '-'\n");
            exit(1);
        }
        acc = 1;
        *out_type = VAR_BOOL;
    } else if (t.type == TOKEN_FALSE) {
        if (sign == -1) {
            printf("ERROR: Invalid operand for unary '-'\n");
            exit(1);
        }
        acc = 0;
        *out_type = VAR_BOOL;
    } else if (t.type == TOKEN_STRING) {
        if (sign == -1) {
            printf("ERROR: Invalid operand for unary '-'\n");
            exit(1);
        }
        *out_str = strdup(t.value);
        *out_type = VAR_STRING;
    } else if (t.type == TOKEN_IDENTIFIER) {
        Variable *v = get_var(t.value);
        if (v) {
            if (v->type == VAR_INT) acc = v->int_val * sign;
            else if (v->type == VAR_BOOL) {
                if (sign == -1) {
                    printf("ERROR: Invalid operand for unary '-'\n");
                    exit(1);
                }
                acc = v->int_val;
                *out_type = VAR_BOOL;
            }
            else {
                if (sign == -1) {
                    printf("ERROR: Invalid operand for unary '-'\n");
                    exit(1);
                }
                *out_str = strdup(v->string_val);
                *out_type = VAR_STRING;
            }
        } else {
            printf("ERROR: Undefined variable '%s'\n", t.value);
            exit(1);
        }
    } else {
        printf("ERROR: Expected value in expression\n");
    }
    freeToken(&t);
    
    // Check for operators
    while (1) {
        Token op = peekToken(cursor);
        if (op.type == TOKEN_PLUS || op.type == TOKEN_MINUS || op.type == TOKEN_STAR || op.type == TOKEN_SLASH) {
            freeToken(&op);
            Token op_consumed = getNextToken(cursor); // consume op
            freeToken(&op_consumed);
            Token rhs = getNextToken(cursor);
            int rhs_val = 0;
            char *rhs_str = NULL;

            if (rhs.type == TOKEN_NUMBER) {
                rhs_val = atoi(rhs.value);
            } else if (rhs.type == TOKEN_TRUE) {
                rhs_val = 1;
            } else if (rhs.type == TOKEN_FALSE) {
                rhs_val = 0;
            } else if (rhs.type == TOKEN_STRING) {
                rhs_str = strdup(rhs.value);
            } else if (rhs.type == TOKEN_IDENTIFIER) {
                Variable *v = get_var(rhs.value);
                if (v) {
                    if (v->type == VAR_INT || v->type == VAR_BOOL) rhs_val = v->int_val;
                    else rhs_str = strdup(v->string_val);
                } else {
                    printf("ERROR: Undefined variable '%s'\n", rhs.value);
                    exit(1);
                }
            }
            
            if (op.type == TOKEN_PLUS) {
                if (*out_str != NULL && rhs_str != NULL) {
                    size_t len1 = strlen(*out_str);
                    size_t len2 = strlen(rhs_str);
                    char *new_str = malloc(len1 + len2 + 1);
                    memcpy(new_str, *out_str, len1);
                    memcpy(new_str + len1, rhs_str, len2 + 1);
                    free(*out_str);
                    *out_str = new_str;
                } else if (*out_str != NULL && rhs_str == NULL) {
                    char num_str[32];
                    snprintf(num_str, sizeof(num_str), "%d", rhs_val);
                    size_t len1 = strlen(*out_str);
                    size_t len2 = strlen(num_str);
                    char *new_str = malloc(len1 + len2 + 1);
                    memcpy(new_str, *out_str, len1);
                    memcpy(new_str + len1, num_str, len2 + 1);
                    free(*out_str);
                    *out_str = new_str;
                } else if (*out_str == NULL && rhs_str != NULL) {
                    char num_str[32];
                    snprintf(num_str, sizeof(num_str), "%d", acc);
                    size_t len1 = strlen(num_str);
                    size_t len2 = strlen(rhs_str);
                    char *new_str = malloc(len1 + len2 + 1);
                    memcpy(new_str, num_str, len1);
                    memcpy(new_str + len1, rhs_str, len2 + 1);
                    *out_str = new_str;
                    *out_type = VAR_STRING; // Ensures *out_type updates properly if an integer was converted
                } else {
                    acc += rhs_val;
                    *out_type = VAR_INT;
                }
            } else if (op.type == TOKEN_MINUS) {
                if (rhs_str || (*out_type != VAR_INT && *out_type != VAR_BOOL)) {
                    printf("ERROR: Invalid operands for operator '-'\n");
                    exit(1);
                }
                acc -= rhs_val;
                *out_type = VAR_INT;
            }
            else if (op.type == TOKEN_STAR) {
                if (rhs_str || (*out_type != VAR_INT && *out_type != VAR_BOOL)) {
                    printf("ERROR: Invalid operands for operator '*'\n");
                    exit(1);
                }
                acc *= rhs_val;
                *out_type = VAR_INT;
            }
            else if (op.type == TOKEN_SLASH) {
                if (rhs_str || (*out_type != VAR_INT && *out_type != VAR_BOOL)) {
                    printf("ERROR: Invalid operands for operator '/'\n");
                    exit(1);
                }
                if (rhs_val != 0) {
                    acc /= rhs_val;
                    *out_type = VAR_INT;
                } else {
                    printf("ERROR: Division by zero\n");
                    exit(1);
                }
            }
            if (rhs_str) free(rhs_str);
            freeToken(&rhs);
        } else {
            freeToken(&op);
            break;
        }
    }
    
    return acc;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <filename>\n", argv[0]);
        return 1;
    }

    FILE *file = fopen(argv[1], "rb");
    if (!file) {
        printf("ERROR: Could not open file %s\n", argv[1]);
        return 1;
    }
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    if (length < 0) {
        printf("ERROR: Could not determine file size\n");
        fclose(file);
        return 1;
    }
    fseek(file, 0, SEEK_SET);
    char *buffer = malloc(length + 1);
    if (!buffer) {
        fclose(file);
        return 1;
    }
    size_t read_bytes = fread(buffer, 1, length, file);
    buffer[read_bytes] = '\0';
    fclose(file);

    const char *cursor = buffer;
    Token t;
    
    while ((t = getNextToken(&cursor)).type != TOKEN_EOF) {
        if (t.type == TOKEN_DISPLAY) {
            char *out_str = NULL;
            int out_type = VAR_INT;
            int val = evaluate_expression(&cursor, &out_str, &out_type);
            if (out_str) {
                printf("%s\n", out_str);
                free(out_str);
            } else if (out_type == VAR_BOOL) {
                printf("%s\n", val ? "true" : "false");
            } else {
                printf("%d\n", val);
            }
        } 
        else if (t.type == TOKEN_PROMPT) {
            Token var_tok = getNextToken(&cursor);
            if (var_tok.type == TOKEN_IDENTIFIER) {
                Variable *v = set_var(var_tok.value);
                char input[256];
                if (fgets(input, sizeof(input), stdin)) {
                    input[strcspn(input, "\r\n")] = 0; // Remove newline
                    char *endptr;
                    long lval = strtol(input, &endptr, 10);
                    if (*endptr == '\0' && input[0] != '\0') {
                        v->type = VAR_INT;
                        v->int_val = (int)lval;
                        if (v->string_val) {
                            free(v->string_val);
                            v->string_val = NULL;
                        }
                    } else {
                        v->type = VAR_STRING;
                        if (v->string_val) free(v->string_val);
                        v->string_val = strdup(input);
                    }
                }
            } else {
                printf("ERROR: Expected variable name after prompt\n");
            }
            if (var_tok.value) free(var_tok.value);
        }
        else if (t.type == TOKEN_IDENTIFIER) {
            Token next = peekToken(&cursor);
            if (next.type == TOKEN_COLON) {
                freeToken(&next);
                Token colon_consumed = getNextToken(&cursor); // Consume COLON
                freeToken(&colon_consumed);
                char *out_str = NULL;
                int out_type = VAR_INT;
                int val = evaluate_expression(&cursor, &out_str, &out_type);
                Variable *v = set_var(t.value);
                if (out_str) {
                    v->type = VAR_STRING;
                    if (v->string_val) {
                        free(v->string_val);
                    }
                    v->string_val = out_str;
                } else if (out_type == VAR_BOOL) {
                    v->type = VAR_BOOL;
                    v->int_val = val;
                    if (v->string_val) {
                        free(v->string_val);
                        v->string_val = NULL;
                    }
                } else {
                    v->type = VAR_INT;
                    v->int_val = val;
                    if (v->string_val) {
                        free(v->string_val);
                        v->string_val = NULL;
                    }
                }
            } else {
                freeToken(&next);
                printf("ERROR: Unexpected identifier '%s'\n", t.value);
            }
        }
        else if (t.type == TOKEN_ERROR) {
            printf("LEXER ERROR: Unexpected token '%s'\n", t.value ? t.value : "");
        }

        freeToken(&t);
    }

    free(buffer);

    for (int i = 0; i < var_count; i++) {
        free(symtable[i].name);
        if (symtable[i].string_val) {
            free(symtable[i].string_val);
        }
    }
    if (symtable) {
        free(symtable);
    }

    return 0;
}
