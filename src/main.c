#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <setjmp.h>
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

int get_attribute_str(const char *full_text, const char *key, char *out_val, int max_len) {
    if (!full_text || !key || !out_val || max_len <= 0) return 0;
    out_val[0] = '\0';
    char search_pattern[64];
    snprintf(search_pattern, sizeof(search_pattern), "?%s=", key);
    const char *ptr = strstr(full_text, search_pattern);
    if (!ptr) {
        snprintf(search_pattern, sizeof(search_pattern), "%s=", key);
        ptr = strstr(full_text, search_pattern);
    }
    if (ptr) {
        ptr = strchr(ptr, '=');
        if (ptr) {
            ptr++;
            while (isspace((unsigned char)*ptr)) ptr++;
            int quote = (*ptr == '"' || *ptr == '\'') ? *ptr++ : 0;
            int idx = 0;
            while (*ptr != '\0' && idx < max_len - 1) {
                if (quote && *ptr == quote) break;
                if (!quote && (isspace((unsigned char)*ptr) || *ptr == '?')) break;
                out_val[idx++] = *ptr++;
            }
            out_val[idx] = '\0';
            return 1;
        }
    }
    return 0;
}

#define MAX_JMP_STACK 64
jmp_buf jmp_env_stack[MAX_JMP_STACK];
int jmp_stack_ptr = 0;

#define MODE_DEFAULT 0
#define MODE_INTERNAL 1
#define MODE_EXTERNAL 2

char current_error_name[128] = "";
char current_error_msg[256] = "";

#define ERR_MODE_NORMAL 0
#define ERR_MODE_FORCE 1
#define ERR_MODE_CRITICAL 2
#define ERR_MODE_SUPPRESS 3

int error_mode = ERR_MODE_NORMAL;
jmp_buf suppress_jmp_env;
int suppress_jmp_active = 0;

char *source_buffer = NULL;

typedef struct {
    char *text;
    int indent;
    int line_num;
} Line;

Line *lines = NULL;
int line_count = 0;
int line_capacity = 0;

void free_globals(void) {
    if (source_buffer) {
        free(source_buffer);
        source_buffer = NULL;
    }
    if (lines) {
        for (int i = 0; i < line_count; i++) {
            if (lines[i].text) {
                free(lines[i].text);
                lines[i].text = NULL;
            }
        }
        free(lines);
        lines = NULL;
    }
    if (symtable) {
        for (int i = 0; i < var_count; i++) {
            if (symtable[i].name) {
                free(symtable[i].name);
                symtable[i].name = NULL;
            }
            if (symtable[i].string_val) {
                free(symtable[i].string_val);
                symtable[i].string_val = NULL;
            }
        }
        free(symtable);
        symtable = NULL;
    }
}

typedef struct {
    int active;
    const char *expr;
    int triggered;
} WatchCondition;

WatchCondition active_watch = {0, NULL, 0};

int is_critical_error(const char *name) {
    if (strcmp(name, "MemoryAllocationError") == 0) return 1;
    if (strcmp(name, "SystemError") == 0) return 1;
    if (strcmp(name, "SyntaxError") == 0) return 1;
    if (strcmp(name, "IndentationError") == 0) return 1;
    return 0;
}

void throw_error(const char *name, const char *fmt, ...) {
    if (name != current_error_name) {
        memmove(current_error_name, name, strlen(name) + 1);
    }

    char temp_msg[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(temp_msg, sizeof(temp_msg), fmt, args);
    va_end(args);

    memmove(current_error_msg, temp_msg, strlen(temp_msg) + 1);

    if (error_mode == ERR_MODE_FORCE) {
        free_all_tracked();
        printf("ERROR: %s: %s\n", name, current_error_msg);
        free_globals();
        exit(1);
    }

    int is_crit = is_critical_error(name);
    int suppress = 0;
    if (error_mode == ERR_MODE_SUPPRESS) {
        suppress = 1;
    } else if (error_mode == ERR_MODE_CRITICAL && !is_crit) {
        suppress = 1;
    }

    if (suppress) {
        free_all_tracked();
        if (suppress_jmp_active) {
            longjmp(suppress_jmp_env, 1);
        }
    }

    free_all_tracked();
    if (jmp_stack_ptr > 0) {
        longjmp(jmp_env_stack[jmp_stack_ptr - 1], 1);
    } else {
        printf("ERROR: %s: %s\n", name, current_error_msg);
        free_globals();
        exit(1);
    }
}

int is_error_name(const char *name) {
    if (strcmp(name, "error") == 0) return 1;
    if (strcmp(name, "MemoryAllocationError") == 0) return 1;
    if (strcmp(name, "UndefinedVariableError") == 0) return 1;
    if (strcmp(name, "InvalidOperandError") == 0) return 1;
    if (strcmp(name, "DivisionByZeroError") == 0) return 1;
    if (strcmp(name, "IndentationError") == 0) return 1;
    if (strcmp(name, "LoopIterationError") == 0) return 1;
    if (strcmp(name, "LoopLimitError") == 0) return 1;
    if (strcmp(name, "LoopDirectionError") == 0) return 1;
    if (strcmp(name, "SyntaxError") == 0) return 1;
    if (strcmp(name, "RuntimeError") == 0) return 1;
    if (strcmp(name, "InvalidErrorNameError") == 0) return 1;
    if (strcmp(name, "SystemError") == 0) return 1;
    return 0;
}

Variable* get_var(const char *name) {
    if (strcmp(name, "error") == 0) {
        Variable *v = NULL;
        for (int i = 0; i < var_count; i++) {
            if (strcmp(symtable[i].name, "error") == 0) {
                v = &symtable[i];
                break;
            }
        }
        if (!v) {
            if (var_count >= var_capacity) {
                int new_capacity = (var_capacity == 0) ? 100 : var_capacity * 2;
                Variable *tmp = realloc(symtable, new_capacity * sizeof(Variable));
                if (!tmp) {
                    printf("ERROR: MemoryAllocationError: Memory allocation failed\n");
                    free_globals();
                    exit(1);
                }
                symtable = tmp;
                var_capacity = new_capacity;
            }
            v = &symtable[var_count++];
            v->name = strdup("error");
            v->type = VAR_STRING;
            v->int_val = 0;
            v->string_val = strdup(current_error_name);
        } else {
            v->type = VAR_STRING;
            if (v->string_val) free(v->string_val);
            v->string_val = strdup(current_error_name);
        }
        return v;
    }
    for (int i = 0; i < var_count; i++) {
        if (strcmp(symtable[i].name, name) == 0) return &symtable[i];
    }
    return NULL;
}

Variable* set_var(const char *name) {
    Variable* v = get_var(name);
    if (v) return v;

    if (var_count >= var_capacity) {
        int new_capacity = (var_capacity == 0) ? 100 : var_capacity * 2;
        Variable *tmp = realloc(symtable, new_capacity * sizeof(Variable));
        if (!tmp) {
            throw_error("MemoryAllocationError", "Memory allocation failed");
        }
        symtable = tmp;
        var_capacity = new_capacity;
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
        untrack_alloc(t->value);
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
            throw_error("InvalidOperandError", "Invalid operand for unary '-'");
        }
        acc = 1;
        *out_type = VAR_BOOL;
    } else if (t.type == TOKEN_FALSE) {
        if (sign == -1) {
            throw_error("InvalidOperandError", "Invalid operand for unary '-'");
        }
        acc = 0;
        *out_type = VAR_BOOL;
    } else if (t.type == TOKEN_STRING) {
        if (sign == -1) {
            throw_error("InvalidOperandError", "Invalid operand for unary '-'");
        }
        *out_str = strdup(t.value);
        track_alloc(*out_str);
        *out_type = VAR_STRING;
    } else if (t.type == TOKEN_IDENTIFIER) {
        Variable *v = get_var(t.value);
        if (v) {
            if (v->type == VAR_INT) acc = v->int_val * sign;
            else if (v->type == VAR_BOOL) {
                if (sign == -1) {
                    throw_error("InvalidOperandError", "Invalid operand for unary '-'");
                }
                acc = v->int_val;
                *out_type = VAR_BOOL;
            }
            else {
                if (sign == -1) {
                    throw_error("InvalidOperandError", "Invalid operand for unary '-'");
                }
                *out_str = strdup(v->string_val);
                track_alloc(*out_str);
                *out_type = VAR_STRING;
            }
        } else {
            throw_error("UndefinedVariableError", "Undefined variable '%s'", t.value);
        }
    } else {
        throw_error("SyntaxError", "Expected value in expression");
    }
    freeToken(&t);

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
                    throw_error("InvalidOperandError", "Invalid operand for unary '-'");
                }
                rhs_val = 1;
            } else if (rhs.type == TOKEN_FALSE) {
                if (rhs_sign == -1) {
                    throw_error("InvalidOperandError", "Invalid operand for unary '-'");
                }
                rhs_val = 0;
            } else if (rhs.type == TOKEN_STRING) {
                if (rhs_sign == -1) {
                    throw_error("InvalidOperandError", "Invalid operand for unary '-'");
                }
                rhs_str = strdup(rhs.value);
                track_alloc(rhs_str);
            } else if (rhs.type == TOKEN_IDENTIFIER) {
                Variable *v = get_var(rhs.value);
                if (v) {
                    if (v->type == VAR_INT) rhs_val = v->int_val * rhs_sign;
                    else if (v->type == VAR_BOOL) {
                        if (rhs_sign == -1) {
                            throw_error("InvalidOperandError", "Invalid operand for unary '-'");
                        }
                        rhs_val = v->int_val;
                    }
                    else {
                        if (rhs_sign == -1) {
                            throw_error("InvalidOperandError", "Invalid operand for unary '-'");
                        }
                        rhs_str = strdup(v->string_val);
                        track_alloc(rhs_str);
                    }
                } else {
                    throw_error("UndefinedVariableError", "Undefined variable '%s'", rhs.value);
                }
            } else {
                throw_error("SyntaxError", "Expected value in expression");
            }

            if (op.type == TOKEN_PLUS) {
                if (*out_str != NULL && rhs_str != NULL) {
                    size_t len1 = strlen(*out_str);
                    size_t len2 = strlen(rhs_str);
                    char *new_str = malloc(len1 + len2 + 1);
                    track_alloc(new_str);
                    memcpy(new_str, *out_str, len1);
                    memcpy(new_str + len1, rhs_str, len2 + 1);
                    untrack_alloc(*out_str);
                    free(*out_str);
                    *out_str = new_str;
                } else if (*out_str != NULL && rhs_str == NULL) {
                    char num_str[32];
                    snprintf(num_str, sizeof(num_str), "%d", rhs_val);
                    size_t len1 = strlen(*out_str);
                    size_t len2 = strlen(num_str);
                    char *new_str = malloc(len1 + len2 + 1);
                    track_alloc(new_str);
                    memcpy(new_str, *out_str, len1);
                    memcpy(new_str + len1, num_str, len2 + 1);
                    untrack_alloc(*out_str);
                    free(*out_str);
                    *out_str = new_str;
                } else if (*out_str == NULL && rhs_str != NULL) {
                    char num_str[32];
                    snprintf(num_str, sizeof(num_str), "%d", acc);
                    size_t len1 = strlen(num_str);
                    size_t len2 = strlen(rhs_str);
                    char *new_str = malloc(len1 + len2 + 1);
                    track_alloc(new_str);
                    memcpy(new_str, num_str, len1);
                    memcpy(new_str + len1, rhs_str, len2 + 1);
                    *out_str = new_str;
                    *out_type = VAR_STRING;
                } else {
                    acc += rhs_val;
                    *out_type = VAR_INT;
                }
            } else if (op.type == TOKEN_MINUS) {
                if (rhs_str || (*out_type != VAR_INT && *out_type != VAR_BOOL)) {
                    throw_error("InvalidOperandError", "Invalid operands for operator '-'");
                }
                acc -= rhs_val;
                *out_type = VAR_INT;
            }
            else if (op.type == TOKEN_STAR) {
                if (rhs_str || (*out_type != VAR_INT && *out_type != VAR_BOOL)) {
                    throw_error("InvalidOperandError", "Invalid operands for operator '*'");
                }
                acc *= rhs_val;
                *out_type = VAR_INT;
            }
            else if (op.type == TOKEN_SLASH) {
                if (rhs_str || (*out_type != VAR_INT && *out_type != VAR_BOOL)) {
                    throw_error("InvalidOperandError", "Invalid operands for operator '/'");
                }
                if (rhs_val != 0) {
                    acc /= rhs_val;
                    *out_type = VAR_INT;
                } else {
                    throw_error("DivisionByZeroError", "Division by zero");
                }
            }
            if (rhs_str) { untrack_alloc(rhs_str); free(rhs_str); }
            freeToken(&rhs);
        }
        else if (op.type == TOKEN_EQUAL || op.type == TOKEN_GREATER || op.type == TOKEN_LESS ||
                 op.type == TOKEN_GREATER_EQUAL || op.type == TOKEN_LESS_EQUAL || op.type == TOKEN_NOT_EQUAL) {
            freeToken(&op);
            Token op_consumed = getNextToken(cursor);
            freeToken(&op_consumed);

            char *rhs_str = NULL;
            int rhs_type = VAR_INT;
            int rhs_val = evaluate_expression(cursor, &rhs_str, &rhs_type);

            int cmp_res = 0;
            if (*out_str != NULL && rhs_str != NULL) {
                int cmp = strcmp(*out_str, rhs_str);
                if (op.type == TOKEN_EQUAL) cmp_res = (cmp == 0);
                else if (op.type == TOKEN_NOT_EQUAL) cmp_res = (cmp != 0);
                else if (op.type == TOKEN_GREATER) cmp_res = (cmp > 0);
                else if (op.type == TOKEN_LESS) cmp_res = (cmp < 0);
                else if (op.type == TOKEN_GREATER_EQUAL) cmp_res = (cmp >= 0);
                else if (op.type == TOKEN_LESS_EQUAL) cmp_res = (cmp <= 0);
            } else if (*out_str == NULL && rhs_str == NULL) {
                if (op.type == TOKEN_EQUAL) cmp_res = (acc == rhs_val);
                else if (op.type == TOKEN_NOT_EQUAL) cmp_res = (acc != rhs_val);
                else if (op.type == TOKEN_GREATER) cmp_res = (acc > rhs_val);
                else if (op.type == TOKEN_LESS) cmp_res = (acc < rhs_val);
                else if (op.type == TOKEN_GREATER_EQUAL) cmp_res = (acc >= rhs_val);
                else if (op.type == TOKEN_LESS_EQUAL) cmp_res = (acc <= rhs_val);
            } else {
                if (op.type == TOKEN_EQUAL) cmp_res = 0;
                else if (op.type == TOKEN_NOT_EQUAL) cmp_res = 1;
                else {
                    throw_error("InvalidOperandError", "Invalid comparison between string and non-string");
                }
            }

            if (*out_str) {
                untrack_alloc(*out_str);
                    free(*out_str);
                *out_str = NULL;
            }
            if (rhs_str) {
                untrack_alloc(rhs_str);
                free(rhs_str);
            }

            acc = cmp_res;
            *out_type = VAR_BOOL;
        }
        else {
            freeToken(&op);
            break;
        }
    }

    return acc;
}

void parse_lines(const char *buffer) {
    const char *p = buffer;
    int line_num = 1;
    int in_multiline_comment = 0;

    while (*p != '\0') {
        const char *eol = p;
        while (*eol != '\n' && *eol != '\0') eol++;

        int raw_len = eol - p;
        char *raw_line = malloc(raw_len + 1);
        memcpy(raw_line, p, raw_len);
        raw_line[raw_len] = '\0';

        char clean_line[1024] = "";
        int clean_pos = 0;
        char *src_ptr = raw_line;
        int in_string = 0;

        while (*src_ptr != '\0') {
            if (!in_multiline_comment && *src_ptr == '"') {
                in_string = !in_string;
                clean_line[clean_pos++] = *src_ptr++;
            } else {
                if (in_multiline_comment) {
                    if (src_ptr[0] == '!' && src_ptr[1] == '!') {
                        in_multiline_comment = 0;
                        src_ptr += 2;
                    } else {
                        src_ptr++;
                    }
                } else {
                    if (!in_string && src_ptr[0] == '!' && src_ptr[1] == '!') {
                        in_multiline_comment = 1;
                        src_ptr += 2;
                    } else if (!in_string && src_ptr[0] == '!' && src_ptr[1] != '!') {
                        // single line comment
                        break;
                    } else {
                        clean_line[clean_pos++] = *src_ptr++;
                    }
                }
            }
        }
        clean_line[clean_pos] = '\0';

        int indent = 0;
        char *src_indent = raw_line;
        while (*src_indent == ' ' || *src_indent == '\t') {
            if (*src_indent == ' ') indent += 1;
            else indent += 4;
            src_indent++;
        }

        char *src = clean_line;
        while (*src == ' ' || *src == '\t') {
            src++;
        }

        char *code = strdup(src);
        int len = strlen(code);
        while (len > 0 && isspace((unsigned char)code[len - 1])) {
            code[len - 1] = '\0';
            len--;
        }

        if (line_count >= line_capacity) {
            int new_capacity = (line_capacity == 0) ? 100 : line_capacity * 2;
            Line *tmp = realloc(lines, new_capacity * sizeof(Line));
            if (!tmp) {
                free(code);
                free(raw_line);
                throw_error("MemoryAllocationError", "Memory allocation failed");
            }
            lines = tmp;
            line_capacity = new_capacity;
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
                untrack_alloc(out_str);
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
                throw_error("SyntaxError", "Expected variable name after prompt on line %d", line_num);
            }
            if (var_tok.value) free(var_tok.value);
        }
        else if (t.type == TOKEN_THROW) {
            Token err_tok = getNextToken(&cursor);
            if (err_tok.type == TOKEN_IDENTIFIER) {
                if (strcmp(err_tok.value, "error") == 0) {
                    if (current_error_name[0] == '\0') {
                        throw_error("RuntimeError", "No error is currently active to rethrow on line %d", line_num);
                    } else {
                        throw_error(current_error_name, "%s", current_error_msg);
                    }
                } else if (is_error_name(err_tok.value)) {
                    throw_error(err_tok.value, "User thrown error");
                } else {
                    throw_error("SyntaxError", "Invalid error: %s error on line %d", err_tok.value, line_num);
                }
            } else {
                throw_error("SyntaxError", "Expected error name after throw on line %d", line_num);
            }
            if (err_tok.value) free(err_tok.value);
        }
        else if (t.type == TOKEN_INJECT) {
            // Single-line inject token handler
            Token lang_tok = getNextToken(&cursor);
            if (lang_tok.value) free(lang_tok.value);
            while (t.type != TOKEN_EOF) {
                freeToken(&t);
                t = getNextToken(&cursor);
            }
            continue;
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
                    untrack_alloc(out_str);
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
                throw_error("SyntaxError", "Unexpected identifier '%s' on line %d", t.value, line_num);
            }
        }
        else if (t.type == TOKEN_QUESTION) {
            Token name_tok = getNextToken(&cursor);
            if (name_tok.type == TOKEN_IDENTIFIER) {
                Token peek = peekToken(&cursor);
                if (peek.type == TOKEN_EQUAL) {
                    freeToken(&peek);
                    Token eq = getNextToken(&cursor); freeToken(&eq);
                    Token val_tok = getNextToken(&cursor); freeToken(&val_tok);
                } else {
                    freeToken(&peek);
                }
            }
            freeToken(&name_tok);
        }
        else if (t.type == TOKEN_ERROR) {
            throw_error("SyntaxError", "Unexpected token '%s' on line %d", t.value ? t.value : "", line_num);
        }
        freeToken(&t);
    }
}

void execute_block(int start, int end) {
    int expected_indent = -1;
    int i = start;
    while (i <= end) {
        if (active_watch.active && active_watch.triggered) {
            break;
        }

        Line *line = &lines[i];
        if (line->text[0] == '\0' || line->text[0] == '!') {
            i++;
            continue;
        }
        if (expected_indent == -1) {
            expected_indent = line->indent;
        } else if (line->indent != expected_indent) {
            throw_error("IndentationError", "Indentation error on line %d (expected %d, got %d)", line->line_num, expected_indent, line->indent);
        }

        int prev_suppress_active = suppress_jmp_active;
        jmp_buf prev_suppress_env;
        memcpy(prev_suppress_env, suppress_jmp_env, sizeof(jmp_buf));

        suppress_jmp_active = 1;
        if (setjmp(suppress_jmp_env) == 0) {
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
            if (out_str) {
                untrack_alloc(out_str);
                free(out_str);
            }
            if (out_type == VAR_STRING) {
                throw_error("LoopIterationError", "Loop iterations must be numeric on line %d", line->line_num);
            }

            int step_val = 1;
            while (isspace((unsigned char)*cursor)) cursor++;
            if (strncmp(cursor, "step", 4) == 0 && (isspace((unsigned char)cursor[4]) || cursor[4] == '\0')) {
                cursor += 4;
                char *step_out_str = NULL;
                int step_out_type = VAR_INT;
                step_val = evaluate_expression(&cursor, &step_out_str, &step_out_type);
                if (step_out_str) {
                    untrack_alloc(step_out_str);
                    free(step_out_str);
                }
                if (step_out_type == VAR_STRING) {
                    throw_error("LoopStepError", "Loop step must be numeric on line %d", line->line_num);
                }
            }

            if (iters > 0 && step_val > iters) {
                throw_error("LoopStepError", "Loop step larger than loop size on line %d", line->line_num);
            }
            if (step_val <= 0) {
                throw_error("LoopStepError", "Loop step must be greater than 0 on line %d", line->line_num);
            }

            for (int k = 0; k < iters; k += step_val) {
                execute_block(block_start, block_end);
                if (active_watch.active && active_watch.triggered) break;
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
                throw_error("SyntaxError", "Expected identifier after iterate on line %d", line->line_num);
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
                if (out_str) {
                    untrack_alloc(out_str);
                    free(out_str);
                }
                if (out_type == VAR_STRING) {
                    throw_error("LoopLimitError", "Loop start index must be numeric on line %d", line->line_num);
                }
            } else {
                Variable *v = get_var(var_name);
                if (v) {
                    if (v->type != VAR_INT && v->type != VAR_BOOL) {
                        throw_error("LoopLimitError", "Existing loop variable '%s' is not numeric on line %d", var_name, line->line_num);
                    }
                    start_val = v->int_val;
                } else {
                    start_val = 0;
                }
            }

            while (isspace((unsigned char)*cursor)) cursor++;
            if (strncmp(cursor, "to", 2) != 0 || !isspace((unsigned char)cursor[2])) {
                throw_error("SyntaxError", "Expected 'to' in iterate loop on line %d", line->line_num);
            }
            cursor += 2;

            char *out_str = NULL;
            int out_type = VAR_INT;
            int end_val = evaluate_expression(&cursor, &out_str, &out_type);
            if (out_str) {
                untrack_alloc(out_str);
                free(out_str);
            }
            if (out_type == VAR_STRING) {
                throw_error("LoopLimitError", "Loop end index must be numeric on line %d", line->line_num);
            }

            int step_val = 1;
            while (isspace((unsigned char)*cursor)) cursor++;
            if (strncmp(cursor, "step", 4) == 0 && (isspace((unsigned char)cursor[4]) || cursor[4] == '\0')) {
                cursor += 4;
                char *step_out_str = NULL;
                int step_out_type = VAR_INT;
                step_val = evaluate_expression(&cursor, &step_out_str, &step_out_type);
                if (step_out_str) {
                    untrack_alloc(step_out_str);
                    free(step_out_str);
                }
                if (step_out_type == VAR_STRING) {
                    throw_error("LoopStepError", "Loop step must be numeric on line %d", line->line_num);
                }
            }

            if (start_val > end_val) {
                throw_error("LoopDirectionError", "start index greater than end index on line %d", line->line_num);
            }

            if ((end_val - start_val + 1) > 0 && step_val > (end_val - start_val + 1)) {
                throw_error("LoopStepError", "Loop step larger than loop size on line %d", line->line_num);
            }
            if (step_val <= 0) {
                throw_error("LoopStepError", "Loop step must be greater than 0 on line %d", line->line_num);
            }

            Variable *v = set_var(var_name);
            for (int idx_val = start_val; idx_val <= end_val; idx_val += step_val) {
                v->type = VAR_INT;
                v->int_val = idx_val;
                if (v->string_val) {
                    free(v->string_val);
                    v->string_val = NULL;
                }
                execute_block(block_start, block_end);
                if (active_watch.active && active_watch.triggered) break;
            }
            i = block_end + 1;
        }
        else if (strncmp(line->text, "if ", 3) == 0) {
            const char *cursor = line->text + 3;
            const char *then_ptr = strstr(cursor, " then");
            if (!then_ptr) {
                throw_error("SyntaxError", "Expected 'then' after if condition on line %d", line->line_num);
            }
            int cond_len = then_ptr - cursor;
            char *cond_str = malloc(cond_len + 1);
            track_alloc(cond_str);
            strncpy(cond_str, cursor, cond_len);
            cond_str[cond_len] = '\0';

            const char *cond_cursor = cond_str;
            char *out_str = NULL;
            int out_type = VAR_INT;
            int cond_val = evaluate_expression(&cond_cursor, &out_str, &out_type);
            if (out_str) {
                untrack_alloc(out_str);
                free(out_str);
                throw_error("InvalidOperandError", "Condition must evaluate to a boolean or number on line %d", line->line_num);
            }
            untrack_alloc(cond_str);
            free(cond_str);

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

            int executed = 0;
            if (cond_val) {
                execute_block(block_start, block_end);
                executed = 1;
            }

            int current_idx = block_end + 1;
            while (current_idx <= end) {
                Line *lookahead = &lines[current_idx];
                if (lookahead->text[0] == '\0' || lookahead->text[0] == '!') {
                    current_idx++;
                    continue;
                }
                if (lookahead->indent != line->indent) {
                    break;
                }

                if (strncmp(lookahead->text, "else if ", 8) == 0) {
                    int elif_start = current_idx + 1;
                    int elif_end = current_idx;
                    while (elif_end + 1 <= end) {
                        Line *next = &lines[elif_end + 1];
                        if (next->text[0] == '\0' || next->text[0] == '!') {
                            elif_end++;
                            continue;
                        }
                        if (next->indent > lookahead->indent) {
                            elif_end++;
                        } else {
                            break;
                        }
                    }

                    if (!executed) {
                        const char *elif_cursor = lookahead->text + 8;
                        const char *elif_then_ptr = strstr(elif_cursor, " then");
                        if (!elif_then_ptr) {
                            throw_error("SyntaxError", "Expected 'then' after else if condition on line %d", lookahead->line_num);
                        }
                        int elif_cond_len = elif_then_ptr - elif_cursor;
                        char *elif_cond_str = malloc(elif_cond_len + 1);
                        track_alloc(elif_cond_str);
                        strncpy(elif_cond_str, elif_cursor, elif_cond_len);
                        elif_cond_str[elif_cond_len] = '\0';

                        const char *elif_cond_cursor = elif_cond_str;
                        char *elif_out_str = NULL;
                        int elif_out_type = VAR_INT;
                        int elif_cond_val = evaluate_expression(&elif_cond_cursor, &elif_out_str, &elif_out_type);
                        if (elif_out_str) {
                            untrack_alloc(elif_out_str);
                            free(elif_out_str);
                            throw_error("InvalidOperandError", "Condition must evaluate to a boolean or number on line %d", lookahead->line_num);
                        }
                        untrack_alloc(elif_cond_str);
                        free(elif_cond_str);

                        if (elif_cond_val) {
                            execute_block(elif_start, elif_end);
                            executed = 1;
                        }
                    }
                    current_idx = elif_end + 1;
                }
                else if (strncmp(lookahead->text, "else ", 5) == 0 || strcmp(lookahead->text, "else") == 0) {
                    int else_start = current_idx + 1;
                    int else_end = current_idx;
                    while (else_end + 1 <= end) {
                        Line *next = &lines[else_end + 1];
                        if (next->text[0] == '\0' || next->text[0] == '!') {
                            else_end++;
                            continue;
                        }
                        if (next->indent > lookahead->indent) {
                            else_end++;
                        } else {
                            break;
                        }
                    }

                    if (!executed) {
                        execute_block(else_start, else_end);
                        executed = 1;
                    }
                    current_idx = else_end + 1;
                }
                else {
                    break;
                }
            }
            if (active_watch.active && active_watch.triggered) {
                i = current_idx;
                break;
            }
            i = current_idx;
        }
        else if (strncmp(line->text, "while ", 6) == 0) {
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

            int step_val = 1;
            const char *cursor = line->text + 6;
            const char *step_ptr = strstr(cursor, " step");
            if (step_ptr) {
                if (isspace(*(step_ptr - 1)) && (isspace(*(step_ptr + 5)) || *(step_ptr + 5) == '\0')) {
                    const char *step_cursor = step_ptr + 6;
                    char *step_out_str = NULL;
                    int step_out_type = VAR_INT;
                    step_val = evaluate_expression(&step_cursor, &step_out_str, &step_out_type);
                    if (step_out_str || step_out_type == VAR_STRING) {
                        throw_error("LoopStepError", "Loop step must be numeric on line %d", line->line_num);
                    }
                    if (step_val <= 0) {
                        throw_error("LoopStepError", "Loop step must be greater than 0 on line %d", line->line_num);
                    }
                } else {
                    step_ptr = NULL;
                }
            }

            int cond_len = step_ptr ? (int)(step_ptr - cursor) : (int)strlen(cursor);
            char *cond_buf = malloc(cond_len + 1);
            track_alloc(cond_buf);
            strncpy(cond_buf, cursor, cond_len);
            cond_buf[cond_len] = '\0';

            int iter_count = 0;
            while (1) {
                const char *eval_cursor = cond_buf;
                char *out_str = NULL;
                int out_type = VAR_INT;
                int cond_val = evaluate_expression(&eval_cursor, &out_str, &out_type);
                if (out_str) {
                    untrack_alloc(out_str);
                    free(out_str);
                    untrack_alloc(cond_buf);
            free(cond_buf);
                    throw_error("InvalidOperandError", "While condition must evaluate to a boolean or number on line %d", line->line_num);
                }
                if (!cond_val) break;

                if (iter_count % step_val == 0) {
                    execute_block(block_start, block_end);
                }
                iter_count++;
                if (active_watch.active && active_watch.triggered) break;
            }
            untrack_alloc(cond_buf);
            free(cond_buf);
            if (step_val > iter_count && iter_count > 0) {
                throw_error("LoopStepError", "Loop step larger than loop size on line %d", line->line_num);
            }
            i = block_end + 1;
        }
        else if (strncmp(line->text, "until ", 6) == 0) {
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

            int step_val = 1;
            const char *cursor = line->text + 6;
            const char *step_ptr = strstr(cursor, " step");
            if (step_ptr) {
                if (isspace(*(step_ptr - 1)) && (isspace(*(step_ptr + 5)) || *(step_ptr + 5) == '\0')) {
                    const char *step_cursor = step_ptr + 6;
                    char *step_out_str = NULL;
                    int step_out_type = VAR_INT;
                    step_val = evaluate_expression(&step_cursor, &step_out_str, &step_out_type);
                    if (step_out_str || step_out_type == VAR_STRING) {
                        throw_error("LoopStepError", "Loop step must be numeric on line %d", line->line_num);
                    }
                    if (step_val <= 0) {
                        throw_error("LoopStepError", "Loop step must be greater than 0 on line %d", line->line_num);
                    }
                } else {
                    step_ptr = NULL;
                }
            }

            int cond_len = step_ptr ? (int)(step_ptr - cursor) : (int)strlen(cursor);
            char *cond_buf = malloc(cond_len + 1);
            track_alloc(cond_buf);
            strncpy(cond_buf, cursor, cond_len);
            cond_buf[cond_len] = '\0';

            int iter_count = 0;
            while (1) {
                const char *eval_cursor = cond_buf;
                char *out_str = NULL;
                int out_type = VAR_INT;
                int cond_val = evaluate_expression(&eval_cursor, &out_str, &out_type);
                if (out_str) {
                    untrack_alloc(out_str);
                    free(out_str);
                    untrack_alloc(cond_buf);
            free(cond_buf);
                    throw_error("InvalidOperandError", "Until condition must evaluate to a boolean or number on line %d", line->line_num);
                }
                if (cond_val) break;

                if (iter_count % step_val == 0) {
                    execute_block(block_start, block_end);
                }
                iter_count++;
                if (active_watch.active && active_watch.triggered) break;
            }
            untrack_alloc(cond_buf);
            free(cond_buf);
            if (step_val > iter_count && iter_count > 0) {
                throw_error("LoopStepError", "Loop step larger than loop size on line %d", line->line_num);
            }
            i = block_end + 1;
        }
        else if (strcmp(line->text, "do") == 0) {
            int do_start = i + 1;
            int do_end = i;
            while (do_end + 1 <= end) {
                Line *next = &lines[do_end + 1];
                if (next->text[0] == '\0' || next->text[0] == '!') {
                    do_end++;
                    continue;
                }
                if (next->indent > line->indent) {
                    do_end++;
                } else {
                    break;
                }
            }

            int unless_idx = do_end + 1;
            while (unless_idx <= end) {
                Line *next = &lines[unless_idx];
                if (next->text[0] == '\0' || next->text[0] == '!') {
                    unless_idx++;
                    continue;
                }
                break;
            }

            if (unless_idx > end || lines[unless_idx].indent != line->indent || strncmp(lines[unless_idx].text, "unless ", 7) != 0) {
                throw_error("SyntaxError", "Expected 'unless' block matching 'do' on line %d", line->line_num);
            }

            Line *unless_line = &lines[unless_idx];
            const char *unless_cursor = unless_line->text + 7;

            // Parse mode
            int mode = MODE_DEFAULT;
            if (strncmp(unless_cursor, "internal ", 9) == 0) {
                mode = MODE_INTERNAL;
                unless_cursor += 9;
            } else if (strncmp(unless_cursor, "external ", 9) == 0) {
                mode = MODE_EXTERNAL;
                unless_cursor += 9;
            }

            const char *unless_expr = unless_cursor;

            int unless_start = unless_idx + 1;
            int unless_end = unless_idx;
            while (unless_end + 1 <= end) {
                Line *next = &lines[unless_end + 1];
                if (next->text[0] == '\0' || next->text[0] == '!') {
                    unless_end++;
                    continue;
                }
                if (next->indent > unless_line->indent) {
                    unless_end++;
                } else {
                    break;
                }
            }

            if (is_error_name(unless_expr)) {
                // If mode is default, errors default to internal
                if (mode == MODE_DEFAULT) {
                    mode = MODE_INTERNAL;
                }

                if (jmp_stack_ptr >= MAX_JMP_STACK) {
                    throw_error("SystemError", "Jump stack overflow on line %d", line->line_num);
                }

                if (setjmp(jmp_env_stack[jmp_stack_ptr++]) == 0) {
                    execute_block(do_start, do_end);
                    jmp_stack_ptr--;
                } else {
                    jmp_stack_ptr--;
                    int catch_all = (strcmp(unless_expr, "error") == 0);
                    int catch_spec = (strcmp(unless_expr, current_error_name) == 0);
                    if (catch_all || catch_spec) {
                        Variable *err_var = set_var("error");
                        err_var->type = VAR_STRING;
                        if (err_var->string_val) free(err_var->string_val);
                        err_var->string_val = strdup(current_error_name);

                        execute_block(unless_start, unless_end);
                    } else {
                        throw_error(current_error_name, "%s", current_error_msg);
                    }
                }
            } else {
                // Bools default to external
                if (mode == MODE_DEFAULT) {
                    mode = MODE_EXTERNAL;
                }

                if (mode == MODE_EXTERNAL) {
                    // Evaluate condition beforehand
                    const char *expr_cursor = unless_expr;
                    char *out_str = NULL;
                    int out_type = VAR_INT;
                    int cond_val = evaluate_expression(&expr_cursor, &out_str, &out_type);
                    if (out_str) {
                        untrack_alloc(out_str);
                        free(out_str);
                        throw_error("InvalidOperandError", "Condition must evaluate to a boolean or number on line %d", unless_line->line_num);
                    }
                    if (cond_val) {
                        execute_block(unless_start, unless_end);
                    } else {
                        execute_block(do_start, do_end);
                    }
                } else {
                    // Mode is internal: checks before executing, and then after every line
                    const char *expr_cursor = unless_expr;
                    char *out_str = NULL;
                    int out_type = VAR_INT;
                    int start_val = evaluate_expression(&expr_cursor, &out_str, &out_type);
                    if (out_str) {
                        untrack_alloc(out_str);
                        free(out_str);
                        throw_error("InvalidOperandError", "Condition must evaluate to a boolean or number on line %d", unless_line->line_num);
                    }

                    if (start_val) {
                        execute_block(unless_start, unless_end);
                    } else {
                        WatchCondition prev_watch = active_watch;
                        active_watch.active = 1;
                        active_watch.expr = unless_expr;
                        active_watch.triggered = 0;

                        execute_block(do_start, do_end);

                        int triggered = active_watch.triggered;
                        active_watch = prev_watch;

                        if (triggered) {
                            execute_block(unless_start, unless_end);
                        }
                    }
                }
            }

            i = unless_end + 1;
        }
        else if (strcmp(line->text, "ForceErrors") == 0) {
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
            int prev_mode = error_mode;
            error_mode = ERR_MODE_FORCE;
            execute_block(block_start, block_end);
            error_mode = prev_mode;
            i = block_end + 1;
        }
        else if (strcmp(line->text, "CriticalErrors") == 0) {
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
            int prev_mode = error_mode;
            error_mode = ERR_MODE_CRITICAL;
            execute_block(block_start, block_end);
            error_mode = prev_mode;
            i = block_end + 1;
        }
        else if (strncmp(line->text, "inject", 6) == 0 && (line->text[6] == ' ' || line->text[6] == '\0')) {
            const char *lang_ptr = line->text + 6;
            while (*lang_ptr == ' ') lang_ptr++;
            char lang[64] = "verscript";
            if (*lang_ptr != '\0') {
                int l_idx = 0;
                while (*lang_ptr != '\0' && *lang_ptr != ' ' && *lang_ptr != '?' && l_idx < 63) {
                    lang[l_idx++] = tolower((unsigned char)*lang_ptr++);
                }
                lang[l_idx] = '\0';
            }

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

            char injected_code[4096] = "";
            int pos = 0;
            int base_indent = (block_start <= block_end) ? lines[block_start].indent : 0;
            for (int k = block_start; k <= block_end; k++) {
                if (lines[k].text[0] == '\0' || lines[k].text[0] == '!') continue;
                int spaces_to_keep = lines[k].indent - base_indent;
                for (int s = 0; s < spaces_to_keep && pos < 4090; s++) injected_code[pos++] = ' ';
                int line_len = strlen(lines[k].text);
                if (pos + line_len + 1 < 4090) {
                    memmove(injected_code + pos, lines[k].text, line_len + 1);
                    pos += line_len;
                    injected_code[pos++] = '\n';
                }
            }
            injected_code[pos] = '\0';

            if (strcmp(lang, "verscript") == 0 || strcmp(lang, "vrs") == 0 || strcmp(lang, "eval") == 0) {
                int prev_line_count = line_count;
                parse_lines(injected_code);
                if (line_count > prev_line_count) {
                    execute_block(prev_line_count, line_count - 1);
                    for (int idx = prev_line_count; idx < line_count; idx++) {
                        if (lines[idx].text) free(lines[idx].text);
                    }
                    line_count = prev_line_count;
                }
            } else {
                char col[32] = "";
                get_attribute_str(line->text, "color", col, sizeof(col));
                if (col[0] != '\0') {
                    if (strcmp(col, "green") == 0) printf("\033[32m");
                    else if (strcmp(col, "red") == 0) printf("\033[31m");
                    else if (strcmp(col, "yellow") == 0) printf("\033[33m");
                    else if (strcmp(col, "blue") == 0) printf("\033[34m");
                    else if (strcmp(col, "purple") == 0) printf("\033[35m");
                    else if (strcmp(col, "cyan") == 0) printf("\033[36m");
                }
                int num_lines = 0;
                for (int k = block_start; k <= block_end; k++) {
                    if (lines[k].text[0] != '\0' && lines[k].text[0] != '!') num_lines++;
                }
                printf("[Inject:%s] Evaluated %d code line(s) cleanly.\n", lang, num_lines);
                if (col[0] != '\0') printf("\033[0m");
            }

            i = block_end + 1;
        }
        else if (strcmp(line->text, "SuppressErrors") == 0) {
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
            int prev_mode = error_mode;
            error_mode = ERR_MODE_SUPPRESS;
            execute_block(block_start, block_end);
            error_mode = prev_mode;
            i = block_end + 1;
        }
        else {
            execute_line(line->text, line->line_num);
            if (active_watch.active) {
                const char *cursor = active_watch.expr;
                char *out_str = NULL;
                int out_type = VAR_INT;
                int val = evaluate_expression(&cursor, &out_str, &out_type);
                if (out_str) {
                    untrack_alloc(out_str);
                    free(out_str);
                    throw_error("InvalidOperandError", "Watch condition must evaluate to a boolean or number on line %d", line->line_num);
                }
                if (val) {
                    active_watch.triggered = 1;
                    break;
                }
            }
            i++;
        }
    } else {
        // Suppressed error occurred. Skip statement or block.
        int block_end = i;
        if (strncmp(line->text, "loop ", 5) == 0 || strcmp(line->text, "loop") == 0 ||
            strncmp(line->text, "iterate ", 8) == 0 ||
            strncmp(line->text, "if ", 3) == 0 ||
            strncmp(line->text, "while ", 6) == 0 ||
            strncmp(line->text, "until ", 6) == 0 ||
            strcmp(line->text, "do") == 0 ||
            strcmp(line->text, "ForceErrors") == 0 ||
            strcmp(line->text, "CriticalErrors") == 0 ||
            strcmp(line->text, "SuppressErrors") == 0) {

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

            if (strcmp(line->text, "do") == 0) {
                int unless_idx = block_end + 1;
                while (unless_idx <= end) {
                    Line *next = &lines[unless_idx];
                    if (next->text[0] == '\0' || next->text[0] == '!') {
                        unless_idx++;
                        continue;
                    }
                    break;
                }
                if (unless_idx <= end && lines[unless_idx].indent == line->indent && strncmp(lines[unless_idx].text, "unless ", 7) == 0) {
                    int unless_end = unless_idx;
                    while (unless_end + 1 <= end) {
                        Line *next = &lines[unless_end + 1];
                        if (next->text[0] == '\0' || next->text[0] == '!') {
                            unless_end++;
                            continue;
                        }
                        if (next->indent > lines[unless_idx].indent) {
                            unless_end++;
                        } else {
                            break;
                        }
                    }
                    block_end = unless_end;
                }
            }
            i = block_end + 1;
        } else {
            i++;
        }
    }

    suppress_jmp_active = prev_suppress_active;
    memcpy(suppress_jmp_env, prev_suppress_env, sizeof(jmp_buf));
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
    source_buffer = malloc(length + 1);
    if (!source_buffer) {
        fclose(file);
        return 1;
    }
    size_t read_bytes = fread(source_buffer, 1, length, file);
    source_buffer[read_bytes] = '\0';
    fclose(file);

    parse_lines(source_buffer);

    if (line_count > 0) {
        execute_block(0, line_count - 1);
    }

    free_globals();

    return 0;
}
