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
        exit(1);
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
            int rhs_sign = 1;

            if (rhs.type == TOKEN_MINUS) {
                rhs_sign = -1;
                freeToken(&rhs);
                rhs = getNextToken(cursor);
            }

            if (rhs.type == TOKEN_NUMBER) {
                rhs_val = atoi(rhs.value) * rhs_sign;
            } else if (rhs.type == TOKEN_TRUE) {
                if (rhs_sign == -1) {
                    printf("ERROR: Invalid operand for unary '-'\n");
                    exit(1);
                }
                rhs_val = 1;
            } else if (rhs.type == TOKEN_FALSE) {
                if (rhs_sign == -1) {
                    printf("ERROR: Invalid operand for unary '-'\n");
                    exit(1);
                }
                rhs_val = 0;
            } else if (rhs.type == TOKEN_STRING) {
                if (rhs_sign == -1) {
                    printf("ERROR: Invalid operand for unary '-'\n");
                    exit(1);
                }
                rhs_str = strdup(rhs.value);
            } else if (rhs.type == TOKEN_IDENTIFIER) {
                Variable *v = get_var(rhs.value);
                if (v) {
                    if (v->type == VAR_INT) rhs_val = v->int_val * rhs_sign;
                    else if (v->type == VAR_BOOL) {
                        if (rhs_sign == -1) {
                            printf("ERROR: Invalid operand for unary '-'\n");
                            exit(1);
                        }
                        rhs_val = v->int_val;
                    }
                    else {
                        if (rhs_sign == -1) {
                            printf("ERROR: Invalid operand for unary '-'\n");
                            exit(1);
                        }
                        rhs_str = strdup(v->string_val);
                    }
                } else {
                    printf("ERROR: Undefined variable '%s'\n", rhs.value);
                    exit(1);
                }
            } else {
                printf("ERROR: Expected value in expression\n");
                exit(1);
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

typedef struct {
    char *text;
    int indent;
    int line_num;
} Line;

Line *lines = NULL;
int line_count = 0;
int line_capacity = 0;

void parse_lines(const char *buffer) {
    const char *p = buffer;
    int line_num = 1;
    while (*p != '\0') {
        const char *eol = p;
        while (*eol != '\n' && *eol != '\0') eol++;
        
        int raw_len = eol - p;
        char *raw_line = malloc(raw_len + 1);
        memcpy(raw_line, p, raw_len);
        raw_line[raw_len] = '\0';
        
        int indent = 0;
        char *src = raw_line;
        while (*src == ' ' || *src == '\t') {
            if (*src == ' ') indent += 1;
            else indent += 4;
            src++;
        }
        
        char *code = strdup(src);
        int len = strlen(code);
        while (len > 0 && isspace((unsigned char)code[len - 1])) {
            code[len - 1] = '\0';
            len--;
        }
        
        if (line_count >= line_capacity) {
            line_capacity = (line_capacity == 0) ? 100 : line_capacity * 2;
            lines = realloc(lines, line_capacity * sizeof(Line));
        }
        lines[line_count].text = code;
        lines[line_count].indent = indent;
        lines[line_count].line_num = line_num;
        line_count++;
        
        free(raw_line);
        
        if (*eol == '\n') p = eol + 1;
        else p = eol;
        line_num++;
    }
}

void execute_line(const char *text, int line_num) {
    const char *cursor = text;
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
                    input[strcspn(input, "\r\n")] = 0;
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
                printf("ERROR: Expected variable name after prompt on line %d\n", line_num);
                exit(1);
            }
            if (var_tok.value) free(var_tok.value);
        }
        else if (t.type == TOKEN_IDENTIFIER) {
            Token next = peekToken(&cursor);
            if (next.type == TOKEN_COLON) {
                freeToken(&next);
                Token colon_consumed = getNextToken(&cursor);
                freeToken(&colon_consumed);
                char *out_str = NULL;
                int out_type = VAR_INT;
                int val = evaluate_expression(&cursor, &out_str, &out_type);
                Variable *v = set_var(t.value);
                if (out_str) {
                    v->type = VAR_STRING;
                    if (v->string_val) free(v->string_val);
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
                printf("ERROR: Unexpected identifier '%s' on line %d\n", t.value, line_num);
                exit(1);
            }
        }
        else if (t.type == TOKEN_ERROR) {
            printf("LEXER ERROR: Unexpected token '%s' on line %d\n", t.value ? t.value : "", line_num);
            exit(1);
        }
        freeToken(&t);
    }
}

void execute_block(int start, int end) {
    int expected_indent = -1;
    int i = start;
    while (i <= end) {
        Line *line = &lines[i];
        if (line->text[0] == '\0' || line->text[0] == '!') {
            i++;
            continue;
        }
        if (expected_indent == -1) {
            expected_indent = line->indent;
        } else if (line->indent != expected_indent) {
            printf("ERROR: Indentation error on line %d (expected %d, got %d)\n", line->line_num, expected_indent, line->indent);
            exit(1);
        }

        if (strncmp(line->text, "loop ", 5) == 0 || strcmp(line->text, "loop") == 0) {
            int block_start = i + 1;
            int block_end = i;
            while (block_end + 1 <= end) {
                Line *next = &lines[block_end + 1];
                if (next->text[0] == '\0' || next->text[0] == '!') {
                    block_end++;
                    continue;
                }
                if (next->indent > line->indent) {
                    block_end++;
                } else {
                    break;
                }
            }

            const char *cursor = line->text + 4;
            char *out_str = NULL;
            int out_type = VAR_INT;
            int iters = evaluate_expression(&cursor, &out_str, &out_type);
            if (out_str || out_type == VAR_STRING) {
                printf("ERROR: Loop iterations must be numeric on line %d\n", line->line_num);
                exit(1);
            }

            for (int k = 0; k < iters; k++) {
                execute_block(block_start, block_end);
            }
            i = block_end + 1;
        }
        else if (strncmp(line->text, "iterate ", 8) == 0) {
            int block_start = i + 1;
            int block_end = i;
            while (block_end + 1 <= end) {
                Line *next = &lines[block_end + 1];
                if (next->text[0] == '\0' || next->text[0] == '!') {
                    block_end++;
                    continue;
                }
                if (next->indent > line->indent) {
                    block_end++;
                } else {
                    break;
                }
            }

            const char *cursor = line->text + 8;
            while (isspace((unsigned char)*cursor)) cursor++;
            const char *id_start = cursor;
            while (isalnum((unsigned char)*cursor) || *cursor == '_') cursor++;
            int id_len = cursor - id_start;
            if (id_len == 0) {
                printf("ERROR: Expected identifier after iterate on line %d\n", line->line_num);
                exit(1);
            }
            char var_name[64];
            strncpy(var_name, id_start, id_len);
            var_name[id_len] = '\0';

            while (isspace((unsigned char)*cursor)) cursor++;
            int start_val = 0;
            if (strncmp(cursor, "from", 4) == 0 && isspace((unsigned char)cursor[4])) {
                cursor += 4;
                char *out_str = NULL;
                int out_type = VAR_INT;
                start_val = evaluate_expression(&cursor, &out_str, &out_type);
                if (out_str || out_type == VAR_STRING) {
                    printf("ERROR: Loop start index must be numeric on line %d\n", line->line_num);
                    exit(1);
                }
            } else {
                Variable *v = get_var(var_name);
                if (v) {
                    if (v->type != VAR_INT && v->type != VAR_BOOL) {
                        printf("ERROR: Existing loop variable '%s' is not numeric on line %d\n", var_name, line->line_num);
                        exit(1);
                    }
                    start_val = v->int_val;
                } else {
                    start_val = 0;
                }
            }

            while (isspace((unsigned char)*cursor)) cursor++;
            if (strncmp(cursor, "to", 2) != 0 || !isspace((unsigned char)cursor[2])) {
                printf("ERROR: Expected 'to' in iterate loop on line %d\n", line->line_num);
                exit(1);
            }
            cursor += 2;

            char *out_str = NULL;
            int out_type = VAR_INT;
            int end_val = evaluate_expression(&cursor, &out_str, &out_type);
            if (out_str || out_type == VAR_STRING) {
                printf("ERROR: Loop end index must be numeric on line %d\n", line->line_num);
                exit(1);
            }

            if (start_val > end_val) {
                printf("ERROR: start index greater than end index on line %d\n", line->line_num);
                exit(1);
            }

            Variable *v = set_var(var_name);
            for (int idx_val = start_val; idx_val <= end_val; idx_val++) {
                v->type = VAR_INT;
                v->int_val = idx_val;
                if (v->string_val) {
                    free(v->string_val);
                    v->string_val = NULL;
                }
                execute_block(block_start, block_end);
            }
            i = block_end + 1;
        }
        else {
            execute_line(line->text, line->line_num);
            i++;
        }
    }
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

    parse_lines(buffer);

    if (line_count > 0) {
        execute_block(0, line_count - 1);
    }

    free(buffer);
    for (int i = 0; i < line_count; i++) {
        free(lines[i].text);
    }
    if (lines) {
        free(lines);
    }

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
