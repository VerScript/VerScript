#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <setjmp.h>
#include "../include/opcodes.h"
#include "../include/lexer.h"

typedef enum { VAR_INT, VAR_STRING, VAR_BOOL, VAR_ARRAY, VAR_ENTITY } VarType;

struct Variable;
struct Routine;
struct Entity;
struct LibraryDef;

typedef struct Array {
    struct Variable *items;
    int count;
    int capacity;
} Array;

typedef struct Entity {
    char class_name[64];
    struct Variable *static_vars;
    int static_count;
    int static_capacity;
    int *static_is_public;
    struct Variable *dynamic_vars;
    int dynamic_count;
    int dynamic_capacity;
    struct Routine *methods;
    int method_count;
    int method_capacity;
} Entity;

typedef struct Variable {
    char *name;
    VarType type;
    int int_val;
    char *string_val;
    Array *array_val;
    Entity *entity_val;
    int scope_level;
} Variable;

typedef struct ClassDef {
    char name[64];
    char params[16][64];
    int param_count;
    int static_start_line;
    int static_end_line;
    int dynamic_start_line;
    int dynamic_end_line;
} ClassDef;

#define MAX_CLASSES 64
ClassDef classes[MAX_CLASSES];
int class_count = 0;

typedef struct LibraryDef {
    char name[64];
    Variable *const_vars;
    int const_count;
    int const_capacity;
    Variable *dynamic_vars;
    int dynamic_count;
    int dynamic_capacity;
    struct Routine *routines;
    int routine_count;
    int routine_capacity;
} LibraryDef;

#define MAX_LIBRARIES 64
LibraryDef libraries[MAX_LIBRARIES];
int library_count = 0;

Entity *current_entity = NULL;
LibraryDef *current_library = NULL;

ClassDef* find_class(const char *name) {
    for (int i = 0; i < class_count; i++) {
        if (strcmp(classes[i].name, name) == 0) return &classes[i];
    }
    return NULL;
}

LibraryDef* find_library(const char *name) {
    for (int i = 0; i < library_count; i++) {
        if (strcmp(libraries[i].name, name) == 0) return &libraries[i];
    }
    return NULL;
}

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

void apply_color(const char *col) {
    if (!col || col[0] == '\0') return;
    if (strcmp(col, "green") == 0) printf("\033[32m");
    else if (strcmp(col, "red") == 0) printf("\033[31m");
    else if (strcmp(col, "yellow") == 0) printf("\033[33m");
    else if (strcmp(col, "blue") == 0) printf("\033[34m");
    else if (strcmp(col, "purple") == 0) printf("\033[35m");
    else if (strcmp(col, "cyan") == 0) printf("\033[36m");
    else if (strcmp(col, "white") == 0) printf("\033[37m");
    else if (col[0] == '#') {
        unsigned int r = 0, g = 0, b = 0;
        int len = strlen(col);
        if (len == 7) {
            if (sscanf(col + 1, "%02x%02x%02x", &r, &g, &b) == 3) {
                printf("\033[38;2;%u;%u;%um", r, g, b);
            }
        } else if (len == 4) {
            if (sscanf(col + 1, "%1x%1x%1x", &r, &g, &b) == 3) {
                printf("\033[38;2;%u;%u;%um", r * 17, g * 17, b * 17);
            }
        }
    }
}

typedef struct {
    char from_arg[64]; // aliased attribute/arg name
    char to_arg[64];   // canonical attribute/arg name
} ArgMapping;

typedef struct {
    char target_cmd[64]; // original target command (e.g. "display")
    char alias_name[64]; // new alias name (e.g. "print")
    ArgMapping mappings[16];
    int mapping_count;
} CommandAlias;

#define MAX_ALIASES 128
CommandAlias aliases[MAX_ALIASES];
int alias_count = 0;

void add_command_alias(const char *target_cmd, const char *alias_name, ArgMapping *mappings, int mapping_count) {
    if (!target_cmd || !alias_name || !*target_cmd || !*alias_name) return;
    for (int i = 0; i < alias_count; i++) {
        if (strcmp(aliases[i].alias_name, alias_name) == 0) {
            strncpy(aliases[i].target_cmd, target_cmd, sizeof(aliases[i].target_cmd) - 1);
            aliases[i].target_cmd[sizeof(aliases[i].target_cmd) - 1] = '\0';
            aliases[i].mapping_count = mapping_count;
            for (int j = 0; j < mapping_count && j < 16; j++) {
                aliases[i].mappings[j] = mappings[j];
            }
            return;
        }
    }
    if (alias_count < MAX_ALIASES) {
        CommandAlias *a = &aliases[alias_count++];
        strncpy(a->target_cmd, target_cmd, sizeof(a->target_cmd) - 1);
        a->target_cmd[sizeof(a->target_cmd) - 1] = '\0';
        strncpy(a->alias_name, alias_name, sizeof(a->alias_name) - 1);
        a->alias_name[sizeof(a->alias_name) - 1] = '\0';
        a->mapping_count = mapping_count;
        for (int j = 0; j < mapping_count && j < 16; j++) {
            a->mappings[j] = mappings[j];
        }
    }
}

CommandAlias* find_alias(const char *alias_name) {
    for (int i = 0; i < alias_count; i++) {
        if (strcmp(aliases[i].alias_name, alias_name) == 0) return &aliases[i];
    }
    return NULL;
}

void parse_and_register_alias(const char *spec) {
    if (!spec) return;
    while (isspace((unsigned char)*spec)) spec++;
    if (strncmp(spec, "alias", 5) == 0 && (isspace((unsigned char)spec[5]) || spec[5] == ':')) {
        spec += 5;
        while (isspace((unsigned char)*spec)) spec++;
    }
    if (*spec == '\0' || *spec == ':') return;

    const char *p = spec;
    while (*p && (isalnum((unsigned char)*p) || *p == '_')) p++;
    int cmd1_len = p - spec;
    if (cmd1_len <= 0) return;
    char cmd1[64];
    if (cmd1_len >= 64) cmd1_len = 63;
    strncpy(cmd1, spec, cmd1_len);
    cmd1[cmd1_len] = '\0';

    while (isspace((unsigned char)*p)) p++;
    if (*p == ':') p++;
    while (isspace((unsigned char)*p)) p++;

    const char *p2 = p;
    while (*p2 && (isalnum((unsigned char)*p2) || *p2 == '_')) p2++;
    int cmd2_len = p2 - p;
    if (cmd2_len <= 0) return;
    char cmd2[64];
    if (cmd2_len >= 64) cmd2_len = 63;
    strncpy(cmd2, p, cmd2_len);
    cmd2[cmd2_len] = '\0';
    p = p2;

    ArgMapping mappings[16];
    int mapping_count = 0;

    while (isspace((unsigned char)*p)) p++;
    if (*p == '?') {
        p++;
        while (*p != '\0' && *p != '\n' && mapping_count < 16) {
            while (isspace((unsigned char)*p) || *p == ',') p++;
            if (*p == '\0' || *p == '\n') break;
            const char *arg_start = p;
            while (*p && (isalnum((unsigned char)*p) || *p == '_')) p++;
            int a1_len = p - arg_start;
            if (a1_len <= 0) break;
            char a1[64];
            if (a1_len >= 64) a1_len = 63;
            strncpy(a1, arg_start, a1_len);
            a1[a1_len] = '\0';

            while (isspace((unsigned char)*p)) p++;
            if (*p == '=') {
                p++;
                while (isspace((unsigned char)*p)) p++;
                const char *a2_start = p;
                while (*p && (isalnum((unsigned char)*p) || *p == '_')) p++;
                int a2_len = p - a2_start;
                if (a2_len > 0) {
                    char a2[64];
                    if (a2_len >= 64) a2_len = 63;
                    strncpy(a2, a2_start, a2_len);
                    a2[a2_len] = '\0';
                    strncpy(mappings[mapping_count].to_arg, a1, 63);
                    mappings[mapping_count].to_arg[63] = '\0';
                    strncpy(mappings[mapping_count].from_arg, a2, 63);
                    mappings[mapping_count].from_arg[63] = '\0';
                    mapping_count++;
                }
            } else {
                strncpy(mappings[mapping_count].from_arg, a1, 63);
                mappings[mapping_count].from_arg[63] = '\0';
                strncpy(mappings[mapping_count].to_arg, a1, 63);
                mappings[mapping_count].to_arg[63] = '\0';
                mapping_count++;
            }
        }
    }

    add_command_alias(cmd1, cmd2, mappings, mapping_count);
}

char* resolve_alias_line(const char *line_text, char *out_buf, size_t out_buf_size) {
    if (!line_text || !out_buf || out_buf_size == 0) return (char*)line_text;
    const char *p = line_text;
    while (isspace((unsigned char)*p)) p++;
    if (*p == '\0' || *p == '!') {
        strncpy(out_buf, line_text, out_buf_size - 1);
        out_buf[out_buf_size - 1] = '\0';
        return out_buf;
    }

    const char *cmd_start = p;
    while (*p && (isalnum((unsigned char)*p) || *p == '_')) p++;
    int cmd_len = p - cmd_start;
    if (cmd_len <= 0) {
        strncpy(out_buf, line_text, out_buf_size - 1);
        out_buf[out_buf_size - 1] = '\0';
        return out_buf;
    }

    char cmd[64];
    if (cmd_len >= 64) cmd_len = 63;
    strncpy(cmd, cmd_start, cmd_len);
    cmd[cmd_len] = '\0';

    CommandAlias *a = find_alias(cmd);
    if (!a) {
        strncpy(out_buf, line_text, out_buf_size - 1);
        out_buf[out_buf_size - 1] = '\0';
        return out_buf;
    }

    int prefix_len = cmd_start - line_text;
    int pos = 0;
    if (prefix_len > 0 && pos + prefix_len < (int)out_buf_size - 1) {
        strncpy(out_buf + pos, line_text, prefix_len);
        pos += prefix_len;
    }

    int tgt_len = strlen(a->target_cmd);
    if (pos + tgt_len < (int)out_buf_size - 1) {
        memmove(out_buf + pos, a->target_cmd, tgt_len + 1);
        pos += tgt_len;
    }

    const char *rem = p;
    while (*rem != '\0' && pos < (int)out_buf_size - 1) {
        if (*rem == '?') {
            const char *q_start = rem;
            rem++;
            const char *arg_start = rem;
            while (*rem && (isalnum((unsigned char)*rem) || *rem == '_')) rem++;
            int arg_len = rem - arg_start;
            char cur_arg[64] = "";
            if (arg_len > 0 && arg_len < 64) {
                strncpy(cur_arg, arg_start, arg_len);
                cur_arg[arg_len] = '\0';
            }

            const char *mapped_to = NULL;
            for (int m = 0; m < a->mapping_count; m++) {
                if (strcmp(a->mappings[m].from_arg, cur_arg) == 0) {
                    mapped_to = a->mappings[m].to_arg;
                    break;
                }
            }

            if (mapped_to) {
                out_buf[pos++] = '?';
                int m_len = strlen(mapped_to);
                if (pos + m_len < (int)out_buf_size - 1) {
                    memmove(out_buf + pos, mapped_to, m_len + 1);
                    pos += m_len;
                }
            } else {
                int orig_len = rem - q_start;
                if (pos + orig_len < (int)out_buf_size - 1) {
                    strncpy(out_buf + pos, q_start, orig_len);
                    pos += orig_len;
                }
            }
        } else {
            out_buf[pos++] = *rem++;
        }
    }
    out_buf[pos] = '\0';
    return out_buf;
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
int current_executing_line = 1;

char *source_buffer = NULL;

typedef struct {
    char *text;
    int indent;
    int line_num;
} Line;

Line *lines = NULL;
int line_count = 0;
int line_capacity = 0;

typedef struct {
    int has_replied;
    int int_val;
    char *string_val;
    Array *array_val;
    Entity *entity_val;
    VarType type;
} ReplyResult;

ReplyResult current_reply = {0, 0, NULL, NULL, NULL, VAR_INT};
int call_stack_ptr = 0;
int current_scope_depth = 0;

void free_globals(void) {
    if (source_buffer) {
        free(source_buffer);
        source_buffer = NULL;
    }
    if (current_reply.string_val) {
        free(current_reply.string_val);
        current_reply.string_val = NULL;
    }
    call_stack_ptr = 0;
    current_scope_depth = 0;
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
    cleanup_lexer();
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
        int len = strlen(name);
        if (len > 127) len = 127;
        memmove(current_error_name, name, len);
        current_error_name[len] = '\0';
    }

    char temp_msg[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(temp_msg, sizeof(temp_msg), fmt, args);
    va_end(args);

    memmove(current_error_msg, temp_msg, strlen(temp_msg) + 1);

    if (error_mode == ERR_MODE_FORCE) {
        free_all_tracked();
        printf("ERROR: %s: %s\n", current_error_name, current_error_msg);
        fflush(stdout);
        fflush(stderr);
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
        if (suppress_jmp_active) {
            longjmp(suppress_jmp_env, 1);
        }
    }

    if (jmp_stack_ptr > 0) {
        longjmp(jmp_env_stack[jmp_stack_ptr - 1], 1);
    } else {
        free_all_tracked();
        printf("ERROR: %s: %s\n", current_error_name, current_error_msg);
        fflush(stdout);
        fflush(stderr);
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
    if (strcmp(name, "LoopStepError") == 0) return 1;
    if (strcmp(name, "ScopeViolationError") == 0) return 1;
    if (strcmp(name, "SyntaxError") == 0) return 1;
    if (strcmp(name, "RuntimeError") == 0) return 1;
    if (strcmp(name, "InvalidErrorNameError") == 0) return 1;
    if (strcmp(name, "SystemError") == 0) return 1;
    if (strcmp(name, "VisibilityError") == 0) return 1;
    if (strcmp(name, "ImmutableError") == 0) return 1;
    if (strcmp(name, "IndexOutOfBoundsError") == 0) return 1;
    if (strcmp(name, "EntityError") == 0) return 1;
    return 0;
}

typedef enum {
    ROUTINE_FUNC,
    ROUTINE_METHOD
} RoutineKind;

typedef enum {
    PURITY_INBOUND,
    PURITY_OUTBOUND
} PurityKind;

typedef struct Routine {
    char name[64];
    RoutineKind kind;
    PurityKind purity;
    int is_private;
    char params[16][64];
    int param_count;
    int body_start_line;
    int body_end_line;
    struct Entity *bound_entity;
    struct LibraryDef *bound_library;
} Routine;

void copy_variable(Variable *dst, const Variable *src);
char* array_to_string(const Array *arr);
char* entity_to_string(const Entity *ent);

Array* create_array(void) {
    Array *arr = malloc(sizeof(Array));
    if (!arr) { printf("ERROR: MemoryAllocationError\n"); exit(1); }
    track_alloc((char*)arr);
    arr->items = NULL;
    arr->count = 0;
    arr->capacity = 0;
    return arr;
}

void array_append(Array *arr, const Variable *val) {
    if (!arr) return;
    if (arr->count >= arr->capacity) {
        int new_cap = (arr->capacity == 0) ? 8 : arr->capacity * 2;
        if (arr->items) untrack_alloc((char*)arr->items);
        Variable *tmp = realloc(arr->items, new_cap * sizeof(Variable));
        if (!tmp) { printf("ERROR: MemoryAllocationError\n"); exit(1); }
        arr->items = tmp;
        track_alloc((char*)arr->items);
        arr->capacity = new_cap;
    }
    copy_variable(&arr->items[arr->count++], val);
}

void copy_variable(Variable *dst, const Variable *src) {
    if (!dst || !src) return;
    dst->name = src->name ? strdup(src->name) : NULL;
    if (dst->name) track_alloc(dst->name);
    dst->type = src->type;
    dst->int_val = src->int_val;
    dst->string_val = src->string_val ? strdup(src->string_val) : NULL;
    if (dst->string_val) track_alloc(dst->string_val);
    dst->array_val = src->array_val;
    dst->entity_val = src->entity_val;
    dst->scope_level = src->scope_level;
}

char* array_to_string(const Array *arr) {
    if (!arr || arr->count == 0) {
        char *res = strdup("[]");
        track_alloc(res);
        return res;
    }
    int cap = 256;
    char *buf = malloc(cap);
    if (!buf) { printf("ERROR: MemoryAllocationError\n"); exit(1); }
    track_alloc(buf);
    buf[0] = '[';
    buf[1] = '\0';
    int len = 1;

    for (int i = 0; i < arr->count; i++) {
        char item_buf[512] = "";
        Variable *it = &arr->items[i];
        if (it->type == VAR_INT) {
            snprintf(item_buf, sizeof(item_buf), "%d", it->int_val);
        } else if (it->type == VAR_BOOL) {
            snprintf(item_buf, sizeof(item_buf), "%s", it->int_val ? "true" : "false");
        } else if (it->type == VAR_STRING) {
            snprintf(item_buf, sizeof(item_buf), "\"%s\"", it->string_val ? it->string_val : "");
        } else if (it->type == VAR_ARRAY) {
            char *sub = array_to_string(it->array_val);
            strncpy(item_buf, sub, sizeof(item_buf) - 1);
        } else if (it->type == VAR_ENTITY) {
            snprintf(item_buf, sizeof(item_buf), "<Entity:%s>", it->entity_val ? it->entity_val->class_name : "unknown");
        }

        int item_len = strlen(item_buf);
        if (len + item_len + 4 >= cap) {
            cap = (len + item_len + 4) * 2;
            untrack_alloc(buf);
            buf = realloc(buf, cap);
            if (!buf) { printf("ERROR: MemoryAllocationError\n"); exit(1); }
            track_alloc(buf);
        }
        strcat(buf, item_buf);
        len += item_len;
        if (i < arr->count - 1) {
            strcat(buf, ", ");
            len += 2;
        }
    }
    strcat(buf, "]");
    return buf;
}

char* entity_to_string(const Entity *ent) {
    char buf[128];
    snprintf(buf, sizeof(buf), "<Entity:%s>", ent ? ent->class_name : "unknown");
    char *res = strdup(buf);
    track_alloc(res);
    return res;
}

#define MAX_ROUTINES 128
Routine routines[MAX_ROUTINES];
int routine_count = 0;

typedef struct {
    Routine *routine;
    int caller_scope_level;
} CallFrame;

#define MAX_CALL_STACK 64
CallFrame call_stack[MAX_CALL_STACK];

Routine* find_routine(const char *name) {
    if (current_entity) {
        for (int i = 0; i < current_entity->method_count; i++) {
            if (strcmp(current_entity->methods[i].name, name) == 0) {
                return &current_entity->methods[i];
            }
        }
    }
    for (int i = 0; i < routine_count; i++) {
        if (strcmp(routines[i].name, name) == 0) return &routines[i];
    }
    for (int lib_idx = 0; lib_idx < library_count; lib_idx++) {
        LibraryDef *lib = &libraries[lib_idx];
        for (int i = 0; i < lib->routine_count; i++) {
            if (strcmp(lib->routines[i].name, name) == 0) {
                return &lib->routines[i];
            }
        }
    }
    return NULL;
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
            v->scope_level = 0;
        } else {
            v->type = VAR_STRING;
            if (v->string_val) free(v->string_val);
            v->string_val = strdup(current_error_name);
        }
        return v;
    }
    for (int i = var_count - 1; i >= 0; i--) {
        if (strcmp(symtable[i].name, name) == 0) return &symtable[i];
    }
    if (current_entity) {
        for (int i = 0; i < current_entity->dynamic_count; i++) {
            if (strcmp(current_entity->dynamic_vars[i].name, name) == 0) {
                return &current_entity->dynamic_vars[i];
            }
        }
        for (int i = 0; i < current_entity->static_count; i++) {
            if (strcmp(current_entity->static_vars[i].name, name) == 0) {
                return &current_entity->static_vars[i];
            }
        }
    }
    for (int lib_idx = 0; lib_idx < library_count; lib_idx++) {
        LibraryDef *lib = &libraries[lib_idx];
        for (int i = 0; i < lib->const_count; i++) {
            if (strcmp(lib->const_vars[i].name, name) == 0) {
                return &lib->const_vars[i];
            }
        }
        for (int i = 0; i < lib->dynamic_count; i++) {
            if (strcmp(lib->dynamic_vars[i].name, name) == 0) {
                return &lib->dynamic_vars[i];
            }
        }
    }
    return NULL;
}

void pop_scope(int target_depth) {
    while (var_count > 0 && symtable[var_count - 1].scope_level > target_depth) {
        var_count--;
        if (symtable[var_count].name) {
            free(symtable[var_count].name);
            symtable[var_count].name = NULL;
        }
        if (symtable[var_count].string_val) {
            free(symtable[var_count].string_val);
            symtable[var_count].string_val = NULL;
        }
    }
}

int is_outbound_assign = 0;
int is_outscope_assign = 0;
int in_entity_instantiation = 0;

Variable* set_var_scoped(const char *name, int line_num) {
    if (strcmp(name, "error") == 0) {
        return get_var("error");
    }

    // Check if modifying a library constant
    for (int lib_idx = 0; lib_idx < library_count; lib_idx++) {
        LibraryDef *lib = &libraries[lib_idx];
        for (int i = 0; i < lib->const_count; i++) {
            if (strcmp(lib->const_vars[i].name, name) == 0) {
                throw_error("ImmutableError", "Cannot modify immutable library constant '%s' on line %d", name, line_num);
            }
        }
    }

    if (current_entity && (is_outbound_assign || in_entity_instantiation)) {
        for (int i = 0; i < current_entity->dynamic_count; i++) {
            if (strcmp(current_entity->dynamic_vars[i].name, name) == 0) {
                return &current_entity->dynamic_vars[i];
            }
        }
        if (current_entity->dynamic_count >= current_entity->dynamic_capacity) {
            int new_cap = (current_entity->dynamic_capacity == 0) ? 16 : current_entity->dynamic_capacity * 2;
            if (current_entity->dynamic_vars) untrack_alloc((char*)current_entity->dynamic_vars);
            Variable *tmp = realloc(current_entity->dynamic_vars, new_cap * sizeof(Variable));
            if (!tmp) throw_error("MemoryAllocationError", "Memory allocation failed for entity dynamic vars");
            current_entity->dynamic_vars = tmp;
            track_alloc((char*)current_entity->dynamic_vars);
            current_entity->dynamic_capacity = new_cap;
        }
        Variable *v = &current_entity->dynamic_vars[current_entity->dynamic_count++];
        memset(v, 0, sizeof(Variable));
        v->name = strdup(name);
        track_alloc(v->name);
        v->type = VAR_INT;
        return v;
    }

    if (is_outscope_assign) {
        for (int i = 0; i < var_count; i++) {
            if (symtable[i].scope_level == 0 && strcmp(symtable[i].name, name) == 0) {
                return &symtable[i];
            }
        }
        if (var_count >= var_capacity) {
            int new_capacity = (var_capacity == 0) ? 100 : var_capacity * 2;
            Variable *tmp = realloc(symtable, new_capacity * sizeof(Variable));
            if (!tmp) throw_error("MemoryAllocationError", "Memory allocation failed");
            symtable = tmp;
            var_capacity = new_capacity;
        }
        Variable *v = &symtable[var_count++];
        v->name = strdup(name);
        v->type = VAR_INT;
        v->int_val = 0;
        v->string_val = NULL;
        v->array_val = NULL;
        v->entity_val = NULL;
        v->scope_level = 0;
        return v;
    }

    Variable *existing = NULL;
    for (int i = var_count - 1; i >= 0; i--) {
        if (strcmp(symtable[i].name, name) == 0) {
            existing = &symtable[i];
            break;
        }
    }

    if (existing) {
        if (existing->scope_level < current_scope_depth) {
            PurityKind cur_purity = PURITY_OUTBOUND;
            const char *rname = "routine";
            if (call_stack_ptr > 0) {
                cur_purity = call_stack[call_stack_ptr - 1].routine->purity;
                rname = call_stack[call_stack_ptr - 1].routine->name;
            }
            if (cur_purity == PURITY_INBOUND) {
                throw_error("ScopeViolationError", "Inbound routine '%s' cannot modify outer variable '%s' on line %d", rname, name, line_num);
            }
            return existing;
        }
        return existing;
    }

    if (var_count >= var_capacity) {
        int new_capacity = (var_capacity == 0) ? 100 : var_capacity * 2;
        Variable *tmp = realloc(symtable, new_capacity * sizeof(Variable));
        if (!tmp) {
            throw_error("MemoryAllocationError", "Memory allocation failed");
        }
        symtable = tmp;
        var_capacity = new_capacity;
    }

    Variable *v = &symtable[var_count++];
    v->name = strdup(name);
    v->type = VAR_INT;
    v->int_val = 0;
    v->string_val = NULL;
    v->scope_level = current_scope_depth;
    return v;
}

Variable* set_var(const char *name) {
    return set_var_scoped(name, current_executing_line);
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

// Forward declarations
void execute_line(const char *text, int line_num);
void execute_block(int start, int end);
int evaluate_expression(const char **cursor, char **out_str, int *out_type);
int evaluate_operand(const char **cursor, char **out_str, int *out_type);
void call_routine(Routine *r, const char **cursor, int line_num);
void call_routine_val(Routine *r, const char **cursor, char **out_str, int *out_type, int *out_int, int line_num);


Entity* instantiate_entity(ClassDef *cd, const char **cursor, int line_num);
int evaluate_operand_val(const char **cursor, char **out_str, int *out_type, Array **out_arr, Entity **out_ent);
int evaluate_expression_val(const char **cursor, char **out_str, int *out_type, Array **out_arr, Entity **out_ent);
int evaluate_argument(const char **cursor, char **out_str, int *out_type, int *out_int) {
    Token peek = peekToken(cursor);
    if (peek.type == TOKEN_LPAREN) {
        freeToken(&peek);
        Token lp = getNextToken(cursor);
        freeToken(&lp);
        Token inner_peek = peekToken(cursor);
        Routine *r = NULL;
        if (inner_peek.type == TOKEN_IDENTIFIER) {
            r = find_routine(inner_peek.value);
        }
        if (r && r->kind == ROUTINE_FUNC) {
            Token fn_tok = getNextToken(cursor);
            freeToken(&fn_tok);
            call_routine_val(r, cursor, out_str, out_type, out_int, current_executing_line);
        } else {
            Array *arr = NULL;
            Entity *ent = NULL;
            *out_int = evaluate_expression_val(cursor, out_str, out_type, &arr, &ent);
        }
        freeToken(&inner_peek);
        Token rp = getNextToken(cursor);
        if (rp.type != TOKEN_RPAREN) {
            freeToken(&rp);
            throw_error("SyntaxError", "Expected ')' after argument expression on line %d", current_executing_line);
        }
        freeToken(&rp);
        return 1;
    }
    freeToken(&peek);
    Array *arr = NULL;
    Entity *ent = NULL;
    *out_int = evaluate_operand_val(cursor, out_str, out_type, &arr, &ent);
    return 1;
}

void call_routine(Routine *r, const char **cursor, int line_num) {
    if (r->is_private) {
        if (r->bound_entity && current_entity != r->bound_entity) {
            throw_error("VisibilityError", "Cannot call private method '%s' outside entity on line %d", r->name, line_num);
        }
        if (r->bound_library && current_library != r->bound_library) {
            throw_error("VisibilityError", "Cannot call private routine '%s' outside library on line %d", r->name, line_num);
        }
    }
    if (call_stack_ptr >= MAX_CALL_STACK) {
        throw_error("SystemError", "Maximum call stack depth exceeded on line %d", line_num);
    }

    typedef struct {
        int int_val;
        char *str_val;
        int type;
    } EvaluatedArg;

    EvaluatedArg evaluated_args[16];
    for (int p = 0; p < r->param_count; p++) {
        char *arg_str = NULL;
        int arg_type = VAR_INT;
        int arg_int = 0;
        evaluate_argument(cursor, &arg_str, &arg_type, &arg_int);
        evaluated_args[p].int_val = arg_int;
        evaluated_args[p].str_val = arg_str;
        evaluated_args[p].type = arg_type;
    }

    int prev_scope = current_scope_depth;
    current_scope_depth++;

    CallFrame *frame = &call_stack[call_stack_ptr++];
    frame->routine = r;
    frame->caller_scope_level = prev_scope;

    for (int p = 0; p < r->param_count; p++) {
        if (var_count >= var_capacity) {
            int new_capacity = (var_capacity == 0) ? 100 : var_capacity * 2;
            Variable *tmp = realloc(symtable, new_capacity * sizeof(Variable));
            if (!tmp) {
                throw_error("MemoryAllocationError", "Memory allocation failed");
            }
            symtable = tmp;
            var_capacity = new_capacity;
        }
        Variable *param_var = &symtable[var_count++];
        param_var->name = strdup(r->params[p]);
        param_var->scope_level = current_scope_depth;
        param_var->type = evaluated_args[p].type;
        param_var->int_val = evaluated_args[p].int_val;
        if (evaluated_args[p].str_val) {
            untrack_alloc(evaluated_args[p].str_val);
            param_var->string_val = evaluated_args[p].str_val;
        } else {
            param_var->string_val = NULL;
        }
    }

    ReplyResult prev_reply = current_reply;
    current_reply.has_replied = 0;
    current_reply.int_val = 0;
    current_reply.string_val = NULL;
    current_reply.type = VAR_INT;

    Entity *saved_ent = current_entity;
    LibraryDef *saved_lib = current_library;
    if (r->bound_entity) current_entity = r->bound_entity;
    if (r->bound_library) current_library = r->bound_library;

    if (r->body_start_line <= r->body_end_line) {
        execute_block(r->body_start_line, r->body_end_line);
    }

    current_entity = saved_ent;
    current_library = saved_lib;

    ReplyResult routine_reply = current_reply;

    pop_scope(prev_scope);
    current_scope_depth = prev_scope;
    call_stack_ptr--;

    if (routine_reply.string_val) {
        free(routine_reply.string_val);
    }
    current_reply = prev_reply;
}

void call_routine_val(Routine *r, const char **cursor, char **out_str, int *out_type, int *out_int, int line_num) {
    if (r->is_private) {
        if (r->bound_entity && current_entity != r->bound_entity) {
            throw_error("VisibilityError", "Cannot call private method '%s' outside entity on line %d", r->name, line_num);
        }
        if (r->bound_library && current_library != r->bound_library) {
            throw_error("VisibilityError", "Cannot call private routine '%s' outside library on line %d", r->name, line_num);
        }
    }
    if (r->kind == ROUTINE_METHOD) {
        throw_error("SyntaxError", "Cannot use method '%s' in an expression on line %d", r->name, line_num);
    }
    if (call_stack_ptr >= MAX_CALL_STACK) {
        throw_error("SystemError", "Maximum call stack depth exceeded on line %d", line_num);
    }

    typedef struct {
        int int_val;
        char *str_val;
        int type;
    } EvaluatedArg;

    EvaluatedArg evaluated_args[16];
    for (int p = 0; p < r->param_count; p++) {
        char *arg_str = NULL;
        int arg_type = VAR_INT;
        int arg_int = 0;
        evaluate_argument(cursor, &arg_str, &arg_type, &arg_int);
        evaluated_args[p].int_val = arg_int;
        evaluated_args[p].str_val = arg_str;
        evaluated_args[p].type = arg_type;
    }

    int prev_scope = current_scope_depth;
    current_scope_depth++;

    CallFrame *frame = &call_stack[call_stack_ptr++];
    frame->routine = r;
    frame->caller_scope_level = prev_scope;

    for (int p = 0; p < r->param_count; p++) {
        if (var_count >= var_capacity) {
            int new_capacity = (var_capacity == 0) ? 100 : var_capacity * 2;
            Variable *tmp = realloc(symtable, new_capacity * sizeof(Variable));
            if (!tmp) {
                throw_error("MemoryAllocationError", "Memory allocation failed");
            }
            symtable = tmp;
            var_capacity = new_capacity;
        }
        Variable *param_var = &symtable[var_count++];
        param_var->name = strdup(r->params[p]);
        param_var->scope_level = current_scope_depth;
        param_var->type = evaluated_args[p].type;
        param_var->int_val = evaluated_args[p].int_val;
        if (evaluated_args[p].str_val) {
            untrack_alloc(evaluated_args[p].str_val);
            param_var->string_val = evaluated_args[p].str_val;
        } else {
            param_var->string_val = NULL;
        }
    }

    ReplyResult prev_reply = current_reply;
    current_reply.has_replied = 0;
    current_reply.int_val = 0;
    current_reply.string_val = NULL;
    current_reply.type = VAR_INT;

    Entity *saved_ent = current_entity;
    LibraryDef *saved_lib = current_library;
    if (r->bound_entity) current_entity = r->bound_entity;
    if (r->bound_library) current_library = r->bound_library;

    if (r->body_start_line <= r->body_end_line) {
        execute_block(r->body_start_line, r->body_end_line);
    }

    current_entity = saved_ent;
    current_library = saved_lib;

    ReplyResult routine_reply = current_reply;

    pop_scope(prev_scope);
    current_scope_depth = prev_scope;
    call_stack_ptr--;

    *out_int = routine_reply.int_val;
    *out_type = routine_reply.type;
    if (routine_reply.string_val) {
        *out_str = routine_reply.string_val;
        track_alloc(*out_str);
    } else {
        *out_str = NULL;
    }
    current_reply = prev_reply;
}

Entity* instantiate_entity(ClassDef *cd, const char **cursor, int line_num) {
    (void)line_num;
    Entity *ent = calloc(1, sizeof(Entity));
    if (!ent) throw_error("MemoryAllocationError", "Memory allocation failed for entity");
    track_alloc((char*)ent);
    strncpy(ent->class_name, cd->name, sizeof(ent->class_name) - 1);

    typedef struct {
        int int_val;
        char *str_val;
        int type;
        Array *arr_val;
        Entity *ent_val;
    } EvalParam;

    EvalParam evaluated_args[16];
    for (int p = 0; p < cd->param_count; p++) {
        char *arg_str = NULL;
        int arg_type = VAR_INT;
        int arg_int = 0;
        evaluate_argument(cursor, &arg_str, &arg_type, &arg_int);
        evaluated_args[p].int_val = arg_int;
        evaluated_args[p].str_val = arg_str;
        evaluated_args[p].type = arg_type;
        evaluated_args[p].arr_val = current_reply.array_val;
        evaluated_args[p].ent_val = current_reply.entity_val;
        if (p < cd->param_count - 1) {
            Token comma = peekToken(cursor);
            if (comma.type == TOKEN_COMMA) {
                freeToken(&comma);
                Token c = getNextToken(cursor); freeToken(&c);
            } else {
                freeToken(&comma);
            }
        }
    }

    int prev_scope = current_scope_depth;
    current_scope_depth++;

    for (int p = 0; p < cd->param_count; p++) {
        if (var_count >= var_capacity) {
            int new_capacity = (var_capacity == 0) ? 100 : var_capacity * 2;
            Variable *tmp = realloc(symtable, new_capacity * sizeof(Variable));
            if (!tmp) throw_error("MemoryAllocationError", "Memory allocation failed");
            symtable = tmp;
            var_capacity = new_capacity;
        }
        Variable *param_var = &symtable[var_count++];
        param_var->name = strdup(cd->params[p]);
        param_var->scope_level = current_scope_depth;
        param_var->type = evaluated_args[p].type;
        param_var->int_val = evaluated_args[p].int_val;
        if (evaluated_args[p].str_val) {
            untrack_alloc(evaluated_args[p].str_val);
            param_var->string_val = evaluated_args[p].str_val;
        } else {
            param_var->string_val = NULL;
        }
        param_var->array_val = evaluated_args[p].arr_val;
        param_var->entity_val = evaluated_args[p].ent_val;
    }

    Entity *saved_ent = current_entity;
    current_entity = ent;

    // Execute static block if present
    if (cd->static_start_line <= cd->static_end_line) {
        for (int line_idx = cd->static_start_line; line_idx <= cd->static_end_line; line_idx++) {
            Line *ln = &lines[line_idx];
            if (ln->text[0] == '\0' || ln->text[0] == '!') continue;
            const char *c = ln->text;
            int is_pub = 0;
            Token t = peekToken(&c);
            if (t.type == TOKEN_PUBLIC) {
                freeToken(&t);
                Token pub = getNextToken(&c); freeToken(&pub);
                is_pub = 1;
            } else {
                freeToken(&t);
            }
            Token name_tok = getNextToken(&c);
            if (name_tok.type == TOKEN_ARR) {
                freeToken(&name_tok);
                name_tok = getNextToken(&c);
            }
            if (name_tok.type == TOKEN_IDENTIFIER) {
                Token col = getNextToken(&c);
                if (col.type == TOKEN_COLON) {
                    freeToken(&col);
                    char *out_str = NULL;
                    int out_type = VAR_INT;
                    Array *out_arr = NULL;
                    Entity *out_sub_ent = NULL;
                    int val = evaluate_expression_val(&c, &out_str, &out_type, &out_arr, &out_sub_ent);

                    if (ent->static_count >= ent->static_capacity) {
                        int new_cap = (ent->static_capacity == 0) ? 8 : ent->static_capacity * 2;
                        if (ent->static_vars) untrack_alloc((char*)ent->static_vars);
                        if (ent->static_is_public) untrack_alloc((char*)ent->static_is_public);
                        Variable *tmp = realloc(ent->static_vars, new_cap * sizeof(Variable));
                        int *ptmp = realloc(ent->static_is_public, new_cap * sizeof(int));
                        if (!tmp || !ptmp) throw_error("MemoryAllocationError", "Memory allocation failed");
                        ent->static_vars = tmp;
                        ent->static_is_public = ptmp;
                        track_alloc((char*)ent->static_vars);
                        track_alloc((char*)ent->static_is_public);
                        ent->static_capacity = new_cap;
                    }
                    Variable *sv = &ent->static_vars[ent->static_count];
                    memset(sv, 0, sizeof(Variable));
                    sv->name = strdup(name_tok.value);
                    track_alloc(sv->name);
                    sv->type = out_type;
                    sv->int_val = val;
                    sv->string_val = out_str;
                    sv->array_val = out_arr ? out_arr : current_reply.array_val;
                    sv->entity_val = out_sub_ent ? out_sub_ent : current_reply.entity_val;
                    ent->static_is_public[ent->static_count] = is_pub;
                    ent->static_count++;
                } else {
                    freeToken(&col);
                }
            }
            freeToken(&name_tok);
        }
    }

    // Execute dynamic block if present
    if (cd->dynamic_start_line <= cd->dynamic_end_line) {
        // Pass 1: register all def routines in entity methods (non-procedural!)
        for (int line_idx = cd->dynamic_start_line; line_idx <= cd->dynamic_end_line; line_idx++) {
            Line *ln = &lines[line_idx];
            if (ln->text[0] == '\0' || ln->text[0] == '!') continue;
            const char *c = ln->text;
            int is_priv = 0;
            Token t = peekToken(&c);
            if (t.type == TOKEN_PRIVATE) {
                freeToken(&t);
                Token priv = getNextToken(&c); freeToken(&priv);
                is_priv = 1;
            } else {
                freeToken(&t);
            }
            Token def_tok = peekToken(&c);
            if (def_tok.type == TOKEN_DEF) {
                freeToken(&def_tok);
                int meth_start = line_idx + 1;
                int meth_end = line_idx;
                while (meth_end + 1 <= cd->dynamic_end_line) {
                    Line *next = &lines[meth_end + 1];
                    if (next->text[0] == '\0' || next->text[0] == '!') { meth_end++; continue; }
                    if (next->indent > ln->indent) meth_end++;
                    else break;
                }
                // Parse method into ent->methods
                if (ent->method_count >= ent->method_capacity) {
                    int new_cap = (ent->method_capacity == 0) ? 8 : ent->method_capacity * 2;
                    if (ent->methods) untrack_alloc((char*)ent->methods);
                    Routine *tmp = realloc(ent->methods, new_cap * sizeof(Routine));
                    if (!tmp) throw_error("MemoryAllocationError", "Memory allocation failed");
                    ent->methods = tmp;
                    track_alloc((char*)ent->methods);
                    ent->method_capacity = new_cap;
                }
                Routine *mr = &ent->methods[ent->method_count++];
                memset(mr, 0, sizeof(Routine));
                mr->is_private = is_priv;
                mr->bound_entity = ent;
                mr->body_start_line = meth_start;
                mr->body_end_line = meth_end;

                Token d = getNextToken(&c); freeToken(&d); // def
                Token kind_tok = getNextToken(&c);
                if (kind_tok.type == TOKEN_INBOUND || kind_tok.type == TOKEN_OUTBOUND) {
                    mr->purity = (kind_tok.type == TOKEN_INBOUND) ? PURITY_INBOUND : PURITY_OUTBOUND;
                    freeToken(&kind_tok);
                    kind_tok = getNextToken(&c);
                } else {
                    mr->purity = PURITY_OUTBOUND;
                }
                mr->kind = (kind_tok.type == TOKEN_FUNC) ? ROUTINE_FUNC : ROUTINE_METHOD;
                freeToken(&kind_tok);

                Token mname = getNextToken(&c);
                if (mname.type == TOKEN_IDENTIFIER) {
                    strncpy(mr->name, mname.value, sizeof(mr->name) - 1);
                }
                freeToken(&mname);

                Token lp = getNextToken(&c);
                if (lp.type == TOKEN_LPAREN) {
                    freeToken(&lp);
                    while (1) {
                        Token param = getNextToken(&c);
                        if (param.type == TOKEN_IDENTIFIER && mr->param_count < 16) {
                            strncpy(mr->params[mr->param_count++], param.value, 63);
                        }
                        if (param.type == TOKEN_RPAREN || param.type == TOKEN_EOF) {
                            freeToken(&param);
                            break;
                        }
                        freeToken(&param);
                    }
                } else {
                    freeToken(&lp);
                }
                line_idx = meth_end;
            } else {
                freeToken(&def_tok);
            }
        }

        // Pass 2: evaluate dynamic properties
        in_entity_instantiation = 1;
        for (int line_idx = cd->dynamic_start_line; line_idx <= cd->dynamic_end_line; line_idx++) {
            Line *ln = &lines[line_idx];
            if (ln->text[0] == '\0' || ln->text[0] == '!') continue;
            if (strstr(ln->text, "def ") != NULL) {
                // Skip routine definitions
                int meth_end = line_idx;
                while (meth_end + 1 <= cd->dynamic_end_line) {
                    Line *next = &lines[meth_end + 1];
                    if (next->text[0] == '\0' || next->text[0] == '!') { meth_end++; continue; }
                    if (next->indent > ln->indent) meth_end++;
                    else break;
                }
                line_idx = meth_end;
                continue;
            }
            execute_line(ln->text, ln->line_num);
        }
        in_entity_instantiation = 0;
    }

    current_entity = saved_ent;
    pop_scope(prev_scope);
    current_scope_depth = prev_scope;

    return ent;
}

int evaluate_operand_val(const char **cursor, char **out_str, int *out_type, Array **out_arr, Entity **out_ent) {
    *out_str = NULL;
    *out_type = VAR_INT;
    if (out_arr) *out_arr = NULL;
    if (out_ent) *out_ent = NULL;

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
            throw_error("InvalidOperandError", "Invalid operand for unary '-' on line %d", current_executing_line);
        }
        acc = 1;
        *out_type = VAR_BOOL;
    } else if (t.type == TOKEN_FALSE) {
        if (sign == -1) {
            throw_error("InvalidOperandError", "Invalid operand for unary '-' on line %d", current_executing_line);
        }
        acc = 0;
        *out_type = VAR_BOOL;
    } else if (t.type == TOKEN_STRING) {
        if (sign == -1) {
            throw_error("InvalidOperandError", "Invalid operand for unary '-' on line %d", current_executing_line);
        }
        *out_str = strdup(t.value);
        track_alloc(*out_str);
        *out_type = VAR_STRING;
    } else if (t.type == TOKEN_LBRACKET) {
        // Array literal [item1, item2, ...]
        Array *arr = create_array();
        Token peek = peekToken(cursor);
        if (peek.type == TOKEN_RBRACKET) {
            freeToken(&peek);
            Token rb = getNextToken(cursor); freeToken(&rb);
        } else {
            freeToken(&peek);
            while (1) {
                char *elem_str = NULL;
                int elem_type = VAR_INT;
                int elem_int = 0;
                evaluate_argument(cursor, &elem_str, &elem_type, &elem_int);
                Variable elem_var;
                memset(&elem_var, 0, sizeof(elem_var));
                elem_var.type = elem_type;
                elem_var.int_val = elem_int;
                elem_var.string_val = elem_str;
                elem_var.array_val = current_reply.array_val;
                elem_var.entity_val = current_reply.entity_val;
                array_append(arr, &elem_var);

                Token sep = getNextToken(cursor);
                if (sep.type == TOKEN_COMMA) {
                    freeToken(&sep);
                    continue;
                } else if (sep.type == TOKEN_RBRACKET) {
                    freeToken(&sep);
                    break;
                } else {
                    freeToken(&sep);
                    throw_error("SyntaxError", "Expected ',' or ']' in array literal on line %d", current_executing_line);
                }
            }
        }
        *out_type = VAR_ARRAY;
        if (out_arr) *out_arr = arr;
        current_reply.array_val = arr;
        *out_str = array_to_string(arr);
        acc = 0;
    } else if (t.type == TOKEN_LPAREN) {
        Token peek = peekToken(cursor);
        Routine *r = NULL;
        if (peek.type == TOKEN_IDENTIFIER) {
            r = find_routine(peek.value);
        }
        if (r && r->kind == ROUTINE_FUNC) {
            Token fn_tok = getNextToken(cursor);
            freeToken(&fn_tok);
            call_routine_val(r, cursor, out_str, out_type, &acc, current_executing_line);
        } else {
            acc = evaluate_expression_val(cursor, out_str, out_type, out_arr, out_ent);
        }
        freeToken(&peek);
        Token rp = getNextToken(cursor);
        if (rp.type != TOKEN_RPAREN) {
            freeToken(&rp);
            throw_error("SyntaxError", "Expected ')' on line %d", current_executing_line);
        }
        freeToken(&rp);
        if (sign == -1) {
            if (*out_type != VAR_INT && *out_type != VAR_BOOL) {
                throw_error("InvalidOperandError", "Invalid operand for unary '-' on line %d", current_executing_line);
            }
            acc *= -1;
        }
    } else if (t.type == TOKEN_IDENTIFIER) {
        // 1. Check if class instantiation: ClassName(...)
        ClassDef *cd = find_class(t.value);
        Token peek = peekToken(cursor);
        if (cd && peek.type == TOKEN_LPAREN) {
            freeToken(&peek);
            Token lp = getNextToken(cursor); freeToken(&lp);
            Entity *ent = instantiate_entity(cd, cursor, current_executing_line);
            Token rp = getNextToken(cursor);
            if (rp.type != TOKEN_RPAREN) {
                freeToken(&rp);
                throw_error("SyntaxError", "Expected ')' after class instantiation on line %d", current_executing_line);
            }
            freeToken(&rp);
            *out_type = VAR_ENTITY;
            if (out_ent) *out_ent = ent;
            current_reply.entity_val = ent;
            *out_str = entity_to_string(ent);
            freeToken(&t);
            return 0;
        }

        // 2. Check if array indexing: arr[index]
        if (peek.type == TOKEN_LBRACKET) {
            freeToken(&peek);
            Token lb = getNextToken(cursor); freeToken(&lb);
            char *idx_str = NULL;
            int idx_type = VAR_INT;
            int idx_val = evaluate_expression(cursor, &idx_str, &idx_type);
            Token rb = getNextToken(cursor);
            if (rb.type != TOKEN_RBRACKET) {
                freeToken(&rb);
                throw_error("SyntaxError", "Expected ']' after array index on line %d", current_executing_line);
            }
            freeToken(&rb);

            Variable *v = get_var(t.value);
            if (!v || v->type != VAR_ARRAY || !v->array_val) {
                throw_error("UndefinedVariableError", "Variable '%s' is not an array on line %d", t.value, current_executing_line);
            }
            if (idx_val < 0 || idx_val >= v->array_val->count) {
                throw_error("IndexOutOfBoundsError", "Array index %d out of bounds (length %d) on line %d", idx_val, v->array_val->count, current_executing_line);
            }
            Variable *elem = &v->array_val->items[idx_val];
            *out_type = elem->type;
            if (elem->type == VAR_INT) acc = elem->int_val * sign;
            else if (elem->type == VAR_BOOL) { acc = elem->int_val; *out_type = VAR_BOOL; }
            else if (elem->type == VAR_STRING) {
                *out_str = elem->string_val ? strdup(elem->string_val) : strdup("");
                track_alloc(*out_str);
            } else if (elem->type == VAR_ARRAY) {
                if (out_arr) *out_arr = elem->array_val;
                current_reply.array_val = elem->array_val;
                *out_str = array_to_string(elem->array_val);
            } else if (elem->type == VAR_ENTITY) {
                if (out_ent) *out_ent = elem->entity_val;
                current_reply.entity_val = elem->entity_val;
                *out_str = entity_to_string(elem->entity_val);
            }
            freeToken(&t);
            return acc;
        }

        // 3. Check if member access or method call: ident.member
        if (peek.type == TOKEN_DOT) {
            freeToken(&peek);
            Token dot = getNextToken(cursor); freeToken(&dot);
            Token mem = getNextToken(cursor);
            if (mem.type != TOKEN_IDENTIFIER) {
                freeToken(&mem);
                throw_error("SyntaxError", "Expected member name after '.' on line %d", current_executing_line);
            }

            // Check if t.value is a Library
            LibraryDef *lib = find_library(t.value);
            if (lib) {
                Token call_peek = peekToken(cursor);
                if (call_peek.type == TOKEN_LPAREN) {
                    freeToken(&call_peek);
                    Token lp = getNextToken(cursor); freeToken(&lp);
                    Routine *r = NULL;
                    for (int i = 0; i < lib->routine_count; i++) {
                        if (strcmp(lib->routines[i].name, mem.value) == 0) { r = &lib->routines[i]; break; }
                    }
                    if (!r) throw_error("SyntaxError", "Library '%s' has no routine '%s' on line %d", lib->name, mem.value, current_executing_line);
                    call_routine_val(r, cursor, out_str, out_type, &acc, current_executing_line);
                    Token rp = getNextToken(cursor);
                    if (rp.type != TOKEN_RPAREN) { freeToken(&rp); throw_error("SyntaxError", "Expected ')' on line %d", current_executing_line); }
                    freeToken(&rp);
                    freeToken(&mem);
                    freeToken(&t);
                    return acc;
                } else {
                    freeToken(&call_peek);
                    // Library const or dynamic variable
                    for (int i = 0; i < lib->const_count; i++) {
                        if (strcmp(lib->const_vars[i].name, mem.value) == 0) {
                            Variable *cv = &lib->const_vars[i];
                            *out_type = cv->type;
                            if (cv->type == VAR_INT) acc = cv->int_val * sign;
                            else if (cv->type == VAR_BOOL) { acc = cv->int_val; *out_type = VAR_BOOL; }
                            else if (cv->type == VAR_STRING) { *out_str = strdup(cv->string_val); track_alloc(*out_str); }
                            freeToken(&mem); freeToken(&t);
                            return acc;
                        }
                    }
                    for (int i = 0; i < lib->dynamic_count; i++) {
                        if (strcmp(lib->dynamic_vars[i].name, mem.value) == 0) {
                            Variable *dv = &lib->dynamic_vars[i];
                            *out_type = dv->type;
                            if (dv->type == VAR_INT) acc = dv->int_val * sign;
                            else if (dv->type == VAR_BOOL) { acc = dv->int_val; *out_type = VAR_BOOL; }
                            else if (dv->type == VAR_STRING) { *out_str = strdup(dv->string_val); track_alloc(*out_str); }
                            freeToken(&mem); freeToken(&t);
                            return acc;
                        }
                    }
                    throw_error("UndefinedVariableError", "Library '%s' has no property '%s' on line %d", lib->name, mem.value, current_executing_line);
                }
            }

            // Entity instance variable lookup
            Variable *ev = get_var(t.value);
            if (!ev || ev->type != VAR_ENTITY || !ev->entity_val) {
                throw_error("UndefinedVariableError", "Variable '%s' is not an entity on line %d", t.value, current_executing_line);
            }
            Entity *ent = ev->entity_val;
            Token call_peek = peekToken(cursor);
            if (call_peek.type == TOKEN_LPAREN) {
                freeToken(&call_peek);
                Token lp = getNextToken(cursor); freeToken(&lp);
                Routine *m = NULL;
                for (int i = 0; i < ent->method_count; i++) {
                    if (strcmp(ent->methods[i].name, mem.value) == 0) { m = &ent->methods[i]; break; }
                }
                if (!m) throw_error("EntityError", "Entity '%s' has no method '%s' on line %d", ent->class_name, mem.value, current_executing_line);
                call_routine_val(m, cursor, out_str, out_type, &acc, current_executing_line);
                Token rp = getNextToken(cursor);
                if (rp.type != TOKEN_RPAREN) { freeToken(&rp); throw_error("SyntaxError", "Expected ')' on line %d", current_executing_line); }
                freeToken(&rp);
                freeToken(&mem);
                freeToken(&t);
                return acc;
            } else {
                freeToken(&call_peek);
                // Check dynamic properties
                for (int i = 0; i < ent->dynamic_count; i++) {
                    if (strcmp(ent->dynamic_vars[i].name, mem.value) == 0) {
                        Variable *prop = &ent->dynamic_vars[i];
                        *out_type = prop->type;
                        if (prop->type == VAR_INT) acc = prop->int_val * sign;
                        else if (prop->type == VAR_BOOL) { acc = prop->int_val; *out_type = VAR_BOOL; }
                        else if (prop->type == VAR_STRING) { *out_str = strdup(prop->string_val); track_alloc(*out_str); }
                        else if (prop->type == VAR_ARRAY) { if (out_arr) *out_arr = prop->array_val; *out_str = array_to_string(prop->array_val); }
                        else if (prop->type == VAR_ENTITY) { if (out_ent) *out_ent = prop->entity_val; *out_str = entity_to_string(prop->entity_val); }
                        freeToken(&mem); freeToken(&t);
                        return acc;
                    }
                }
                // Check static properties (check visibility if outside entity)
                for (int i = 0; i < ent->static_count; i++) {
                    if (strcmp(ent->static_vars[i].name, mem.value) == 0) {
                        if (current_entity != ent && !ent->static_is_public[i]) {
                            throw_error("VisibilityError", "Cannot access private static member '%s' outside entity on line %d", mem.value, current_executing_line);
                        }
                        Variable *prop = &ent->static_vars[i];
                        *out_type = prop->type;
                        if (prop->type == VAR_INT) acc = prop->int_val * sign;
                        else if (prop->type == VAR_BOOL) { acc = prop->int_val; *out_type = VAR_BOOL; }
                        else if (prop->type == VAR_STRING) { *out_str = strdup(prop->string_val); track_alloc(*out_str); }
                        else if (prop->type == VAR_ARRAY) { if (out_arr) *out_arr = prop->array_val; *out_str = array_to_string(prop->array_val); }
                        else if (prop->type == VAR_ENTITY) { if (out_ent) *out_ent = prop->entity_val; *out_str = entity_to_string(prop->entity_val); }
                        freeToken(&mem); freeToken(&t);
                        return acc;
                    }
                }
                throw_error("EntityError", "Entity '%s' has no property '%s' on line %d", ent->class_name, mem.value, current_executing_line);
            }
        }
        freeToken(&peek);

        // 4. Routine lookup (user routines or library routines)
        Routine *r = find_routine(t.value);
        if (r) {
            if (r->kind == ROUTINE_METHOD) {
                throw_error("SyntaxError", "Cannot use method '%s' in an expression on line %d", r->name, current_executing_line);
            }
            call_routine_val(r, cursor, out_str, out_type, &acc, current_executing_line);
            if (sign == -1) {
                if (*out_type != VAR_INT && *out_type != VAR_BOOL) {
                    throw_error("InvalidOperandError", "Invalid operand for unary '-' on line %d", current_executing_line);
                }
                acc *= -1;
            }
        } else {
            Variable *v = get_var(t.value);
            if (v) {
                if (v->type == VAR_INT) acc = v->int_val * sign;
                else if (v->type == VAR_BOOL) {
                    if (sign == -1) throw_error("InvalidOperandError", "Invalid operand for unary '-' on line %d", current_executing_line);
                    acc = v->int_val;
                    *out_type = VAR_BOOL;
                } else if (v->type == VAR_STRING) {
                    if (sign == -1) throw_error("InvalidOperandError", "Invalid operand for unary '-' on line %d", current_executing_line);
                    *out_str = strdup(v->string_val ? v->string_val : "");
                    track_alloc(*out_str);
                    *out_type = VAR_STRING;
                } else if (v->type == VAR_ARRAY) {
                    *out_type = VAR_ARRAY;
                    if (out_arr) *out_arr = v->array_val;
                    current_reply.array_val = v->array_val;
                    *out_str = array_to_string(v->array_val);
                } else if (v->type == VAR_ENTITY) {
                    *out_type = VAR_ENTITY;
                    if (out_ent) *out_ent = v->entity_val;
                    current_reply.entity_val = v->entity_val;
                    *out_str = entity_to_string(v->entity_val);
                }
            } else {
                throw_error("UndefinedVariableError", "Undefined variable '%s' on line %d", t.value, current_executing_line);
            }
        }
    } else {
        throw_error("SyntaxError", "Expected value in expression on line %d", current_executing_line);
    }
    freeToken(&t);
    return acc;
}

int evaluate_operand(const char **cursor, char **out_str, int *out_type) {
    return evaluate_operand_val(cursor, out_str, out_type, NULL, NULL);
}

int evaluate_expression_val(const char **cursor, char **out_str, int *out_type, Array **out_arr, Entity **out_ent) {
    int acc = evaluate_operand_val(cursor, out_str, out_type, out_arr, out_ent);

    while (1) {
        Token op = peekToken(cursor);
        if (op.type == TOKEN_PLUS || op.type == TOKEN_MINUS || op.type == TOKEN_STAR || op.type == TOKEN_SLASH) {
            freeToken(&op);
            Token op_consumed = getNextToken(cursor);
            freeToken(&op_consumed);

            char *rhs_str = NULL;
            int rhs_type = VAR_INT;
            Array *rhs_arr = NULL;
            Entity *rhs_ent = NULL;
            int rhs_val = evaluate_operand_val(cursor, &rhs_str, &rhs_type, &rhs_arr, &rhs_ent);

            if (op.type == TOKEN_PLUS) {
                if (*out_str != NULL && rhs_str != NULL) {
                    size_t len1 = strlen(*out_str);
                    size_t len2 = strlen(rhs_str);
                    char *new_str = malloc(len1 + len2 + 1);
                    if (!new_str) throw_error("MemoryAllocationError", "Memory allocation failed");
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
                    if (!new_str) throw_error("MemoryAllocationError", "Memory allocation failed");
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
                    if (!new_str) throw_error("MemoryAllocationError", "Memory allocation failed");
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
                    throw_error("InvalidOperandError", "Invalid operands for operator '-' on line %d", current_executing_line);
                }
                acc -= rhs_val;
                *out_type = VAR_INT;
            } else if (op.type == TOKEN_STAR) {
                if (rhs_str || (*out_type != VAR_INT && *out_type != VAR_BOOL)) {
                    throw_error("InvalidOperandError", "Invalid operands for operator '*' on line %d", current_executing_line);
                }
                acc *= rhs_val;
                *out_type = VAR_INT;
            } else if (op.type == TOKEN_SLASH) {
                if (rhs_str || (*out_type != VAR_INT && *out_type != VAR_BOOL)) {
                    throw_error("InvalidOperandError", "Invalid operands for operator '/' on line %d", current_executing_line);
                }
                if (rhs_val != 0) {
                    acc /= rhs_val;
                    *out_type = VAR_INT;
                } else {
                    throw_error("DivisionByZeroError", "Division by zero on line %d", current_executing_line);
                }
            }
            if (rhs_str) { untrack_alloc(rhs_str); free(rhs_str); }
        }
        else if (op.type == TOKEN_EQUAL || op.type == TOKEN_GREATER || op.type == TOKEN_LESS ||
                 op.type == TOKEN_GREATER_EQUAL || op.type == TOKEN_LESS_EQUAL || op.type == TOKEN_NOT_EQUAL) {
            TokenType cmp_type = op.type;
            freeToken(&op);
            Token op_consumed = getNextToken(cursor);
            freeToken(&op_consumed);

            char *rhs_str = NULL;
            int rhs_type = VAR_INT;
            int rhs_val = evaluate_expression(cursor, &rhs_str, &rhs_type);

            int cmp_res = 0;
            if (*out_str != NULL && rhs_str != NULL) {
                int cmp = strcmp(*out_str, rhs_str);
                if (cmp_type == TOKEN_EQUAL) cmp_res = (cmp == 0);
                else if (cmp_type == TOKEN_NOT_EQUAL) cmp_res = (cmp != 0);
                else if (cmp_type == TOKEN_GREATER) cmp_res = (cmp > 0);
                else if (cmp_type == TOKEN_LESS) cmp_res = (cmp < 0);
                else if (cmp_type == TOKEN_GREATER_EQUAL) cmp_res = (cmp >= 0);
                else if (cmp_type == TOKEN_LESS_EQUAL) cmp_res = (cmp <= 0);
            } else if (*out_str == NULL && rhs_str == NULL) {
                if (cmp_type == TOKEN_EQUAL) cmp_res = (acc == rhs_val);
                else if (cmp_type == TOKEN_NOT_EQUAL) cmp_res = (acc != rhs_val);
                else if (cmp_type == TOKEN_GREATER) cmp_res = (acc > rhs_val);
                else if (cmp_type == TOKEN_LESS) cmp_res = (acc < rhs_val);
                else if (cmp_type == TOKEN_GREATER_EQUAL) cmp_res = (acc >= rhs_val);
                else if (cmp_type == TOKEN_LESS_EQUAL) cmp_res = (acc <= rhs_val);
            } else {
                if (cmp_type == TOKEN_EQUAL) cmp_res = 0;
                else if (cmp_type == TOKEN_NOT_EQUAL) cmp_res = 1;
                else {
                    throw_error("InvalidOperandError", "Invalid comparison between string and non-string on line %d", current_executing_line);
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
        } else {
            freeToken(&op);
            break;
        }
    }

    return acc;
}

int evaluate_expression(const char **cursor, char **out_str, int *out_type) {
    return evaluate_expression_val(cursor, out_str, out_type, NULL, NULL);
}

void parse_and_register_routine(const char *def_line, int body_start, int body_end, int line_num) {
    const char *cursor = def_line;
    Token t = getNextToken(&cursor); // "def"
    freeToken(&t);

    int purity_specified = 0;
    PurityKind purity = PURITY_INBOUND;
    RoutineKind kind = ROUTINE_FUNC;

    Token tok = getNextToken(&cursor);
    if (tok.type == TOKEN_INBOUND) {
        purity_specified = 1;
        purity = PURITY_INBOUND;
        freeToken(&tok);
        tok = getNextToken(&cursor);
    } else if (tok.type == TOKEN_OUTBOUND) {
        purity_specified = 1;
        purity = PURITY_OUTBOUND;
        freeToken(&tok);
        tok = getNextToken(&cursor);
    }

    if (tok.type == TOKEN_FUNC) {
        kind = ROUTINE_FUNC;
        if (!purity_specified) purity = PURITY_INBOUND;
    } else if (tok.type == TOKEN_METHOD) {
        kind = ROUTINE_METHOD;
        if (!purity_specified) purity = PURITY_OUTBOUND;
    } else {
        freeToken(&tok);
        throw_error("SyntaxError", "Expected 'func' or 'method' in routine definition on line %d", line_num);
    }
    freeToken(&tok);

    Token name_tok = getNextToken(&cursor);
    if (name_tok.type != TOKEN_IDENTIFIER) {
        freeToken(&name_tok);
        throw_error("SyntaxError", "Expected routine name in definition on line %d", line_num);
    }

    char rname[64];
    strncpy(rname, name_tok.value, sizeof(rname) - 1);
    rname[sizeof(rname) - 1] = '\0';
    freeToken(&name_tok);

    Routine *r = NULL;
    for (int i = 0; i < routine_count; i++) {
        if (strcmp(routines[i].name, rname) == 0) {
            r = &routines[i];
            break;
        }
    }
    if (!r) {
        if (routine_count >= MAX_ROUTINES) {
            throw_error("SystemError", "Maximum routine count exceeded on line %d", line_num);
        }
        r = &routines[routine_count++];
    }

    strncpy(r->name, rname, sizeof(r->name) - 1);
    r->name[sizeof(r->name) - 1] = '\0';
    r->kind = kind;
    r->purity = purity;
    r->body_start_line = body_start;
    r->body_end_line = body_end;
    r->param_count = 0;

    while (1) {
        Token param_tok = getNextToken(&cursor);
        if (param_tok.type == TOKEN_EOF) {
            freeToken(&param_tok);
            break;
        }
        if (param_tok.type == TOKEN_IDENTIFIER) {
            if (r->param_count < 16) {
                strncpy(r->params[r->param_count], param_tok.value, sizeof(r->params[0]) - 1);
                r->params[r->param_count][sizeof(r->params[0]) - 1] = '\0';
                r->param_count++;
            }
        }
        freeToken(&param_tok);
    }
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
        if (!raw_line) {
            throw_error("MemoryAllocationError", "Memory allocation failed");
        }
        memcpy(raw_line, p, raw_len);
        raw_line[raw_len] = '\0';

        char *clean_line = calloc(1, raw_len + 1);
        if (!clean_line) {
            free(raw_line);
            throw_error("MemoryAllocationError", "Memory allocation failed");
        }
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
        free(clean_line);
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
    char resolved_buf[2048];
    const char *resolved_text = resolve_alias_line(text, resolved_buf, sizeof(resolved_buf));
    const char *cursor = resolved_text;
    Token t;
    while ((t = getNextToken(&cursor)).type != TOKEN_EOF) {
        if (t.type == TOKEN_ALIAS) {
            parse_and_register_alias(resolved_text);
            while (t.type != TOKEN_EOF) {
                freeToken(&t);
                t = getNextToken(&cursor);
            }
            continue;
        }
        else if (t.type == TOKEN_DISPLAY) {
            char col[64] = "";
            get_attribute_str(resolved_text, "color", col, sizeof(col));
            int newline = 1;
            char nl[32] = "";
            if (get_attribute_str(resolved_text, "newline", nl, sizeof(nl))) {
                if (strcmp(nl, "false") == 0 || strcmp(nl, "0") == 0) newline = 0;
            }
            if (strstr(resolved_text, "?inline") != NULL) newline = 0;

            if (col[0] != '\0') {
                apply_color(col);
            }

            char *out_str = NULL;
            int out_type = VAR_INT;
            Array *out_arr = NULL;
            Entity *out_ent = NULL;
            int val = evaluate_expression_val(&cursor, &out_str, &out_type, &out_arr, &out_ent);
            if (out_type == VAR_ARRAY) {
                Array *target = out_arr ? out_arr : current_reply.array_val;
                char *s = array_to_string(target);
                printf("%s", s ? s : "[]");
            } else if (out_type == VAR_ENTITY) {
                Entity *target = out_ent ? out_ent : current_reply.entity_val;
                char *s = entity_to_string(target);
                printf("%s", s ? s : "<Entity>");
            } else if (out_str) {
                printf("%s", out_str);
                untrack_alloc(out_str);
                free(out_str);
            } else if (out_type == VAR_BOOL) {
                printf("%s", val ? "true" : "false");
            } else {
                printf("%d", val);
            }
            if (col[0] != '\0') printf("\033[0m");
            if (newline) printf("\n");
        }
        else if (t.type == TOKEN_PROMPT) {
            Token var_tok = getNextToken(&cursor);
            if (var_tok.type == TOKEN_IDENTIFIER) {
                Variable *v = set_var(var_tok.value);
                char def_val[128] = "";
                get_attribute_str(resolved_text, "default", def_val, sizeof(def_val));
                char input[256];
                if (fgets(input, sizeof(input), stdin)) {
                    input[strcspn(input, "\r\n")] = 0;
                    if (input[0] == '\0' && def_val[0] != '\0') {
                        strncpy(input, def_val, sizeof(input) - 1);
                        input[sizeof(input) - 1] = '\0';
                    }
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
            freeToken(&var_tok);
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
                    char custom_msg[256] = "";
                    get_attribute_str(resolved_text, "msg", custom_msg, sizeof(custom_msg));
                    if (custom_msg[0] != '\0') {
                        throw_error(err_tok.value, "%s", custom_msg);
                    } else {
                        throw_error(err_tok.value, "User thrown error");
                    }
                } else {
                    throw_error("SyntaxError", "Invalid error: %s error on line %d", err_tok.value, line_num);
                }
            } else {
                throw_error("SyntaxError", "Expected error name after throw on line %d", line_num);
            }
            freeToken(&err_tok);
        }
        else if (t.type == TOKEN_INJECT) {
            // Single-line inject token handler
            Token lang_tok = getNextToken(&cursor);
            freeToken(&lang_tok);
            while (t.type != TOKEN_EOF) {
                freeToken(&t);
                t = getNextToken(&cursor);
            }
            continue;
        }
        else if (t.type == TOKEN_SET) {
            Token var_tok = getNextToken(&cursor);
            if (var_tok.type != TOKEN_IDENTIFIER) {
                freeToken(&var_tok);
                throw_error("SyntaxError", "Expected variable name after 'set' on line %d", line_num);
            }
            Token colon = getNextToken(&cursor);
            if (colon.type != TOKEN_COLON) {
                freeToken(&colon);
                freeToken(&var_tok);
                throw_error("SyntaxError", "Expected ':' after variable name in 'set' on line %d", line_num);
            }
            freeToken(&colon);
            char *out_str = NULL;
            int out_type = VAR_INT;
            int val = evaluate_expression(&cursor, &out_str, &out_type);
            Variable *v = set_var_scoped(var_tok.value, line_num);
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
            freeToken(&var_tok);
        }
        else if (t.type == TOKEN_REPLY) {
            if (call_stack_ptr == 0) {
                throw_error("SyntaxError", "'reply' statement outside of routine on line %d", line_num);
            }
            Routine *cur_routine = call_stack[call_stack_ptr - 1].routine;
            Token peek = peekToken(&cursor);
            if (peek.type != TOKEN_EOF) {
                freeToken(&peek);
                if (cur_routine->kind == ROUTINE_METHOD) {
                    throw_error("SyntaxError", "Methods cannot return values with reply on line %d", line_num);
                }
                char *out_str = NULL;
                int out_type = VAR_INT;
                int val = evaluate_expression(&cursor, &out_str, &out_type);
                current_reply.has_replied = 1;
                current_reply.type = out_type;
                current_reply.int_val = val;
                if (current_reply.string_val) {
                    free(current_reply.string_val);
                    current_reply.string_val = NULL;
                }
                if (out_str) {
                    untrack_alloc(out_str);
                    current_reply.string_val = out_str;
                }
            } else {
                freeToken(&peek);
                current_reply.has_replied = 1;
                current_reply.type = VAR_INT;
                current_reply.int_val = 0;
                if (current_reply.string_val) {
                    free(current_reply.string_val);
                    current_reply.string_val = NULL;
                }
            }
        }
        else if (t.type == TOKEN_ARR) {
            Token var_tok = getNextToken(&cursor);
            if (var_tok.type != TOKEN_IDENTIFIER) {
                throw_error("SyntaxError", "Expected variable name after 'arr' on line %d", line_num);
            }
            Token peek = peekToken(&cursor);
            Variable *v = set_var_scoped(var_tok.value, line_num);
            v->type = VAR_ARRAY;
            if (peek.type == TOKEN_COLON) {
                Token col = getNextToken(&cursor); freeToken(&col);
                char *out_str = NULL;
                int out_type = VAR_INT;
                Array *out_arr = NULL;
                Entity *out_ent = NULL;
                evaluate_expression_val(&cursor, &out_str, &out_type, &out_arr, &out_ent);
                if (out_type == VAR_ARRAY) {
                    v->array_val = out_arr ? out_arr : current_reply.array_val;
                } else {
                    Array *single = create_array();
                    Variable item;
                    memset(&item, 0, sizeof(item));
                    item.type = out_type;
                    item.string_val = out_str;
                    array_append(single, &item);
                    v->array_val = single;
                }
            } else {
                v->array_val = create_array();
            }
            freeToken(&peek);
            freeToken(&var_tok);
        }
        else if (t.type == TOKEN_OUTSCOPE) {
            is_outscope_assign = 1;
            continue;
        }
        else if (t.type == TOKEN_OUTBOUND) {
            is_outbound_assign = 1;
            continue;
        }
        else if (t.type == TOKEN_PUBLIC) {
            continue;
        }
        else if (t.type == TOKEN_IDENTIFIER) {
            Token next = peekToken(&cursor);
            // Array element assignment: arr[idx]: val
            if (next.type == TOKEN_LBRACKET) {
                freeToken(&next);
                Token lb = getNextToken(&cursor); freeToken(&lb);
                char *idx_str = NULL;
                int idx_type = VAR_INT;
                int idx_val = evaluate_expression(&cursor, &idx_str, &idx_type);
                Token rb = getNextToken(&cursor);
                if (rb.type != TOKEN_RBRACKET) {
                    freeToken(&rb);
                    throw_error("SyntaxError", "Expected ']' after array index on line %d", line_num);
                }
                freeToken(&rb);
                Token colon = getNextToken(&cursor);
                if (colon.type != TOKEN_COLON) {
                    freeToken(&colon);
                    throw_error("SyntaxError", "Expected ':' after array index assignment on line %d", line_num);
                }
                freeToken(&colon);
                char *out_str = NULL;
                int out_type = VAR_INT;
                Array *out_arr = NULL;
                Entity *out_ent = NULL;
                int val = evaluate_expression_val(&cursor, &out_str, &out_type, &out_arr, &out_ent);

                Variable *v = get_var(t.value);
                if (!v || v->type != VAR_ARRAY || !v->array_val) {
                    throw_error("UndefinedVariableError", "Variable '%s' is not an array on line %d", t.value, line_num);
                }
                if (idx_val < 0 || idx_val >= v->array_val->count) {
                    throw_error("IndexOutOfBoundsError", "Array index %d out of bounds (length %d) on line %d", idx_val, v->array_val->count, line_num);
                }
                Variable *elem = &v->array_val->items[idx_val];
                elem->type = out_type;
                elem->int_val = val;
                elem->string_val = out_str;
                elem->array_val = out_arr ? out_arr : current_reply.array_val;
                elem->entity_val = out_ent ? out_ent : current_reply.entity_val;
            }
            // Dot notation: ident.prop: val or ident.method(...)
            else if (next.type == TOKEN_DOT) {
                freeToken(&next);
                Token dot = getNextToken(&cursor); freeToken(&dot);
                Token mem = getNextToken(&cursor);
                if (mem.type != TOKEN_IDENTIFIER) {
                    freeToken(&mem);
                    throw_error("SyntaxError", "Expected member name after '.' on line %d", line_num);
                }
                Token after_mem = peekToken(&cursor);
                if (after_mem.type == TOKEN_COLON) {
                    freeToken(&after_mem);
                    Token colon = getNextToken(&cursor); freeToken(&colon);
                    char *out_str = NULL;
                    int out_type = VAR_INT;
                    Array *out_arr = NULL;
                    Entity *out_ent = NULL;
                    int val = evaluate_expression_val(&cursor, &out_str, &out_type, &out_arr, &out_ent);

                    LibraryDef *lib = find_library(t.value);
                    if (lib) {
                        for (int k = 0; k < lib->const_count; k++) {
                            if (strcmp(lib->const_vars[k].name, mem.value) == 0) {
                                throw_error("ImmutableError", "Cannot modify immutable library constant '%s' on line %d", mem.value, line_num);
                            }
                        }
                        int found = 0;
                        for (int k = 0; k < lib->dynamic_count; k++) {
                            if (strcmp(lib->dynamic_vars[k].name, mem.value) == 0) {
                                lib->dynamic_vars[k].type = out_type;
                                lib->dynamic_vars[k].int_val = val;
                                lib->dynamic_vars[k].string_val = out_str;
                                found = 1;
                                break;
                            }
                        }
                        if (!found) {
                            int new_cap = (lib->dynamic_capacity == 0) ? 8 : lib->dynamic_capacity * 2;
                            if (lib->dynamic_vars) untrack_alloc((char*)lib->dynamic_vars);
                            Variable *tmp = realloc(lib->dynamic_vars, new_cap * sizeof(Variable));
                            if (!tmp) throw_error("MemoryAllocationError", "Memory allocation failed");
                            lib->dynamic_vars = tmp;
                            track_alloc((char*)lib->dynamic_vars);
                            lib->dynamic_capacity = new_cap;
                            Variable *nv = &lib->dynamic_vars[lib->dynamic_count++];
                            memset(nv, 0, sizeof(Variable));
                            nv->name = strdup(mem.value);
                            track_alloc(nv->name);
                            nv->type = out_type;
                            nv->int_val = val;
                            nv->string_val = out_str;
                        }
                    } else {
                        Variable *ev = get_var(t.value);
                        if (!ev || ev->type != VAR_ENTITY || !ev->entity_val) {
                            throw_error("UndefinedVariableError", "Variable '%s' is not an entity on line %d", t.value, line_num);
                        }
                        Entity *ent = ev->entity_val;
                        for (int k = 0; k < ent->static_count; k++) {
                            if (strcmp(ent->static_vars[k].name, mem.value) == 0) {
                                throw_error("ImmutableError", "Cannot modify immutable static property '%s' on line %d", mem.value, line_num);
                            }
                        }
                        int found = 0;
                        for (int k = 0; k < ent->dynamic_count; k++) {
                            if (strcmp(ent->dynamic_vars[k].name, mem.value) == 0) {
                                ent->dynamic_vars[k].type = out_type;
                                ent->dynamic_vars[k].int_val = val;
                                ent->dynamic_vars[k].string_val = out_str;
                                ent->dynamic_vars[k].array_val = out_arr ? out_arr : current_reply.array_val;
                                ent->dynamic_vars[k].entity_val = out_ent ? out_ent : current_reply.entity_val;
                                found = 1;
                                break;
                            }
                        }
                        if (!found) {
                            int new_cap = (ent->dynamic_capacity == 0) ? 8 : ent->dynamic_capacity * 2;
                            if (ent->dynamic_vars) untrack_alloc((char*)ent->dynamic_vars);
                            Variable *tmp = realloc(ent->dynamic_vars, new_cap * sizeof(Variable));
                            if (!tmp) throw_error("MemoryAllocationError", "Memory allocation failed");
                            ent->dynamic_vars = tmp;
                            track_alloc((char*)ent->dynamic_vars);
                            ent->dynamic_capacity = new_cap;
                            Variable *nv = &ent->dynamic_vars[ent->dynamic_count++];
                            memset(nv, 0, sizeof(Variable));
                            nv->name = strdup(mem.value);
                            track_alloc(nv->name);
                            nv->type = out_type;
                            nv->int_val = val;
                            nv->string_val = out_str;
                            nv->array_val = out_arr ? out_arr : current_reply.array_val;
                            nv->entity_val = out_ent ? out_ent : current_reply.entity_val;
                        }
                    }
                } else if (after_mem.type == TOKEN_LPAREN) {
                    freeToken(&after_mem);
                    Token lp = getNextToken(&cursor); freeToken(&lp);
                    LibraryDef *lib = find_library(t.value);
                    if (lib) {
                        Routine *r = NULL;
                        for (int k = 0; k < lib->routine_count; k++) {
                            if (strcmp(lib->routines[k].name, mem.value) == 0) { r = &lib->routines[k]; break; }
                        }
                        if (!r) throw_error("SyntaxError", "Library '%s' has no routine '%s' on line %d", lib->name, mem.value, line_num);
                        call_routine(r, &cursor, line_num);
                        Token rp = getNextToken(&cursor);
                        if (rp.type != TOKEN_RPAREN) { freeToken(&rp); throw_error("SyntaxError", "Expected ')' on line %d", line_num); }
                        freeToken(&rp);
                    } else {
                        Variable *ev = get_var(t.value);
                        if (!ev || ev->type != VAR_ENTITY || !ev->entity_val) {
                            throw_error("UndefinedVariableError", "Variable '%s' is not an entity on line %d", t.value, line_num);
                        }
                        Entity *ent = ev->entity_val;
                        Routine *m = NULL;
                        for (int k = 0; k < ent->method_count; k++) {
                            if (strcmp(ent->methods[k].name, mem.value) == 0) { m = &ent->methods[k]; break; }
                        }
                        if (!m) throw_error("EntityError", "Entity '%s' has no method '%s' on line %d", ent->class_name, mem.value, line_num);
                        call_routine(m, &cursor, line_num);
                        Token rp = getNextToken(&cursor);
                        if (rp.type != TOKEN_RPAREN) { freeToken(&rp); throw_error("SyntaxError", "Expected ')' on line %d", line_num); }
                        freeToken(&rp);
                    }
                } else {
                    freeToken(&after_mem);
                    throw_error("SyntaxError", "Unexpected token after member on line %d", line_num);
                }
                freeToken(&mem);
            }
            // Normal assignment: ident: expr
            else if (next.type == TOKEN_COLON) {
                freeToken(&next);
                Token colon_consumed = getNextToken(&cursor);
                freeToken(&colon_consumed);
                char *out_str = NULL;
                int out_type = VAR_INT;
                Array *out_arr = NULL;
                Entity *out_ent = NULL;
                int val = evaluate_expression_val(&cursor, &out_str, &out_type, &out_arr, &out_ent);
                Variable *v = set_var_scoped(t.value, line_num);
                is_outscope_assign = 0;
                is_outbound_assign = 0;
                v->type = out_type;
                if (out_type == VAR_ARRAY) {
                    v->array_val = out_arr ? out_arr : current_reply.array_val;
                } else if (out_type == VAR_ENTITY) {
                    v->entity_val = out_ent ? out_ent : current_reply.entity_val;
                } else if (out_str) {
                    if (symtable && v >= symtable && v < symtable + var_capacity) {
                        if (v->string_val) free(v->string_val);
                        untrack_alloc(out_str);
                        v->string_val = out_str;
                    } else {
                        v->string_val = out_str;
                    }
                } else if (out_type == VAR_BOOL) {
                    v->int_val = val;
                    if (v->string_val) { free(v->string_val); v->string_val = NULL; }
                } else {
                    v->int_val = val;
                    if (v->string_val) { free(v->string_val); v->string_val = NULL; }
                }
            } else {
                freeToken(&next);
                Routine *r = find_routine(t.value);
                if (r) {
                    call_routine(r, &cursor, line_num);
                } else {
                    throw_error("SyntaxError", "Unexpected identifier '%s' on line %d", t.value, line_num);
                }
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
        if (current_reply.has_replied) {
            break;
        }

        Line *line = &lines[i];
        current_executing_line = line->line_num;
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

        char resolved_line_buf[2048];
        const char *effective_text = resolve_alias_line(line->text, resolved_line_buf, sizeof(resolved_line_buf));

        suppress_jmp_active = 1;
        if (setjmp(suppress_jmp_env) == 0) {
            if (strncmp(effective_text, "class ", 6) == 0 || strncmp(effective_text, "class(", 6) == 0) {
                int block_start = i + 1;
                int block_end = i;
                while (block_end + 1 <= end) {
                    Line *next = &lines[block_end + 1];
                    if (next->text[0] == '\0' || next->text[0] == '!') { block_end++; continue; }
                    if (next->indent > line->indent) block_end++;
                    else break;
                }

                if (class_count >= MAX_CLASSES) throw_error("SystemError", "Max classes exceeded on line %d", line->line_num);
                ClassDef *cd = &classes[class_count++];
                memset(cd, 0, sizeof(ClassDef));
                cd->static_start_line = 1;
                cd->static_end_line = 0;
                cd->dynamic_start_line = 1;
                cd->dynamic_end_line = 0;

                const char *cc = effective_text + 5;
                while (isspace((unsigned char)*cc)) cc++;
                const char *name_start = cc;
                while (isalnum((unsigned char)*cc) || *cc == '_') cc++;
                int nlen = cc - name_start;
                if (nlen > 63) nlen = 63;
                strncpy(cd->name, name_start, nlen);
                while (isspace((unsigned char)*cc)) cc++;
                if (*cc == '(') {
                    cc++;
                    while (*cc != '\0' && *cc != ')' && cd->param_count < 16) {
                        while (isspace((unsigned char)*cc) || *cc == ',') cc++;
                        if (*cc == ')' || *cc == '\0') break;
                        const char *p_start = cc;
                        while (isalnum((unsigned char)*cc) || *cc == '_') cc++;
                        int plen = cc - p_start;
                        if (plen > 63) plen = 63;
                        strncpy(cd->params[cd->param_count++], p_start, plen);
                    }
                }

                int cur_section = 0; // 1 = static, 2 = dynamic
                for (int idx = block_start; idx <= block_end; idx++) {
                    Line *ln = &lines[idx];
                    if (ln->text[0] == '\0' || ln->text[0] == '!') continue;
                    if (strcmp(ln->text, "static:") == 0 || strncmp(ln->text, "static:", 7) == 0) {
                        cur_section = 1;
                        cd->static_start_line = idx + 1;
                        cd->static_end_line = idx;
                    } else if (strcmp(ln->text, "dynamic:") == 0 || strncmp(ln->text, "dynamic:", 8) == 0) {
                        cur_section = 2;
                        cd->dynamic_start_line = idx + 1;
                        cd->dynamic_end_line = idx;
                    } else {
                        if (cur_section == 1) cd->static_end_line = idx;
                        else if (cur_section == 2) cd->dynamic_end_line = idx;
                    }
                }
                i = block_end + 1;
            }
            else if (strncmp(effective_text, "lib ", 4) == 0 || strncmp(effective_text, "library ", 8) == 0) {
                int block_start = i + 1;
                int block_end = i;
                while (block_end + 1 <= end) {
                    Line *next = &lines[block_end + 1];
                    if (next->text[0] == '\0' || next->text[0] == '!') { block_end++; continue; }
                    if (next->indent > line->indent) block_end++;
                    else break;
                }

                if (library_count >= MAX_LIBRARIES) throw_error("SystemError", "Max libraries exceeded on line %d", line->line_num);
                LibraryDef *lib = &libraries[library_count++];
                memset(lib, 0, sizeof(LibraryDef));

                const char *cc = effective_text;
                while (*cc && !isspace((unsigned char)*cc)) cc++;
                while (isspace((unsigned char)*cc)) cc++;
                const char *name_start = cc;
                while (isalnum((unsigned char)*cc) || *cc == '_') cc++;
                int nlen = cc - name_start;
                if (nlen > 63) nlen = 63;
                strncpy(lib->name, name_start, nlen);

                int cur_sec = 0; // 1 = const, 2 = dynamic
                LibraryDef *prev_lib_ctx = current_library;
                current_library = lib;

                for (int idx = block_start; idx <= block_end; idx++) {
                    Line *ln = &lines[idx];
                    if (ln->text[0] == '\0' || ln->text[0] == '!') continue;
                    if (strcmp(ln->text, "const:") == 0 || strncmp(ln->text, "const:", 6) == 0) {
                        cur_sec = 1;
                    } else if (strcmp(ln->text, "dynamic:") == 0 || strncmp(ln->text, "dynamic:", 8) == 0) {
                        cur_sec = 2;
                    } else {
                        const char *c = ln->text;
                        int is_priv = 0;
                        Token t = peekToken(&c);
                        if (t.type == TOKEN_PRIVATE) {
                            freeToken(&t);
                            Token priv = getNextToken(&c); freeToken(&priv);
                            is_priv = 1;
                        } else { freeToken(&t); }

                        Token def_tok = peekToken(&c);
                        if (def_tok.type == TOKEN_DEF) {
                            freeToken(&def_tok);
                            int meth_start = idx + 1;
                            int meth_end = idx;
                            while (meth_end + 1 <= block_end) {
                                Line *next = &lines[meth_end + 1];
                                if (next->text[0] == '\0' || next->text[0] == '!') { meth_end++; continue; }
                                if (next->indent > ln->indent) meth_end++;
                                else break;
                            }
                            if (lib->routine_count >= lib->routine_capacity) {
                                int new_cap = (lib->routine_capacity == 0) ? 8 : lib->routine_capacity * 2;
                                if (lib->routines) untrack_alloc((char*)lib->routines);
                                Routine *tmp = realloc(lib->routines, new_cap * sizeof(Routine));
                                if (!tmp) throw_error("MemoryAllocationError", "Memory allocation failed");
                                lib->routines = tmp;
                                track_alloc((char*)lib->routines);
                                lib->routine_capacity = new_cap;
                            }
                            Routine *lr = &lib->routines[lib->routine_count++];
                            memset(lr, 0, sizeof(Routine));
                            lr->is_private = is_priv;
                            lr->bound_library = lib;
                            lr->body_start_line = meth_start;
                            lr->body_end_line = meth_end;

                            Token d = getNextToken(&c); freeToken(&d);
                            Token kind_tok = getNextToken(&c);
                            lr->kind = (kind_tok.type == TOKEN_FUNC) ? ROUTINE_FUNC : ROUTINE_METHOD;
                            freeToken(&kind_tok);
                            Token rname = getNextToken(&c);
                            if (rname.type == TOKEN_IDENTIFIER) strncpy(lr->name, rname.value, 63);
                            freeToken(&rname);
                            Token lp = getNextToken(&c);
                            if (lp.type == TOKEN_LPAREN) {
                                freeToken(&lp);
                                while (1) {
                                    Token ptok = getNextToken(&c);
                                    if (ptok.type == TOKEN_IDENTIFIER && lr->param_count < 16) {
                                        strncpy(lr->params[lr->param_count++], ptok.value, 63);
                                    }
                                    if (ptok.type == TOKEN_RPAREN || ptok.type == TOKEN_EOF) { freeToken(&ptok); break; }
                                    freeToken(&ptok);
                                }
                            } else { freeToken(&lp); }
                            idx = meth_end;
                        } else {
                            freeToken(&def_tok);
                            Token vname = getNextToken(&c);
                            if (vname.type == TOKEN_IDENTIFIER) {
                                Token col = getNextToken(&c);
                                if (col.type == TOKEN_COLON) {
                                    freeToken(&col);
                                    char *out_str = NULL;
                                    int out_type = VAR_INT;
                                    Array *out_arr = NULL;
                                    Entity *out_ent = NULL;
                                    int val = evaluate_expression_val(&c, &out_str, &out_type, &out_arr, &out_ent);
                                    if (cur_sec == 1) {
                                        if (lib->const_count >= lib->const_capacity) {
                                            int new_cap = (lib->const_capacity == 0) ? 8 : lib->const_capacity * 2;
                                            if (lib->const_vars) untrack_alloc((char*)lib->const_vars);
                                            Variable *tmp = realloc(lib->const_vars, new_cap * sizeof(Variable));
                                            if (!tmp) throw_error("MemoryAllocationError", "Memory allocation failed");
                                            lib->const_vars = tmp;
                                            track_alloc((char*)lib->const_vars);
                                            lib->const_capacity = new_cap;
                                        }
                                        Variable *cv = &lib->const_vars[lib->const_count++];
                                        memset(cv, 0, sizeof(Variable));
                                        cv->name = strdup(vname.value);
                                        track_alloc(cv->name);
                                        cv->type = out_type;
                                        cv->int_val = val;
                                        cv->string_val = out_str;
                                    } else {
                                        if (lib->dynamic_count >= lib->dynamic_capacity) {
                                            int new_cap = (lib->dynamic_capacity == 0) ? 8 : lib->dynamic_capacity * 2;
                                            if (lib->dynamic_vars) untrack_alloc((char*)lib->dynamic_vars);
                                            Variable *tmp = realloc(lib->dynamic_vars, new_cap * sizeof(Variable));
                                            if (!tmp) throw_error("MemoryAllocationError", "Memory allocation failed");
                                            lib->dynamic_vars = tmp;
                                            track_alloc((char*)lib->dynamic_vars);
                                            lib->dynamic_capacity = new_cap;
                                        }
                                        Variable *dv = &lib->dynamic_vars[lib->dynamic_count++];
                                        memset(dv, 0, sizeof(Variable));
                                        dv->name = strdup(vname.value);
                                        track_alloc(dv->name);
                                        dv->type = out_type;
                                        dv->int_val = val;
                                        dv->string_val = out_str;
                                    }
                                } else { freeToken(&col); }
                            }
                            freeToken(&vname);
                        }
                    }
                }
                current_library = prev_lib_ctx;
                i = block_end + 1;
            }
            else if (strncmp(effective_text, "def ", 4) == 0) {
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
                parse_and_register_routine(effective_text, block_start, block_end, line->line_num);
                i = block_end + 1;
            }
            else if (strncmp(line->text, "alias:", 6) == 0 || strcmp(line->text, "alias:") == 0 || strcmp(line->text, "alias") == 0) {
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
                for (int k = block_start; k <= block_end; k++) {
                    if (lines[k].text[0] != '\0' && lines[k].text[0] != '!') {
                        parse_and_register_alias(lines[k].text);
                    }
                }
                i = block_end + 1;
            }
            else if (strncmp(line->text, "alias ", 6) == 0) {
                parse_and_register_alias(line->text);
                i++;
            }
            else if (strncmp(effective_text, "loop ", 5) == 0 || strcmp(effective_text, "loop") == 0) {
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

            const char *cursor = effective_text + 4;
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
                if (current_reply.has_replied) break;
                execute_block(block_start, block_end);
                if (current_reply.has_replied) break;
                if (active_watch.active && active_watch.triggered) break;
            }
            i = block_end + 1;
        }
            else if (strncmp(effective_text, "iterate ", 8) == 0) {
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

            const char *cursor = effective_text + 8;
            while (isspace((unsigned char)*cursor)) cursor++;
            const char *id_start = cursor;
            while (isalnum((unsigned char)*cursor) || *cursor == '_') cursor++;
            int id_len = cursor - id_start;
            if (id_len == 0) {
                throw_error("SyntaxError", "Expected identifier after iterate on line %d", line->line_num);
            }
            if (id_len >= 64) {
                id_len = 63;
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
                    throw_error("LoopIterationError", "Iterate start must be numeric on line %d", line->line_num);
                }
            } else {
                throw_error("SyntaxError", "Expected 'from' after identifier in iterate on line %d", line->line_num);
            }

            while (isspace((unsigned char)*cursor)) cursor++;
            int end_val = 0;
            if (strncmp(cursor, "to", 2) == 0 && isspace((unsigned char)cursor[2])) {
                cursor += 2;
                char *out_str = NULL;
                int out_type = VAR_INT;
                end_val = evaluate_expression(&cursor, &out_str, &out_type);
                if (out_str) {
                    untrack_alloc(out_str);
                    free(out_str);
                }
                if (out_type == VAR_STRING) {
                    throw_error("LoopIterationError", "Iterate end must be numeric on line %d", line->line_num);
                }
            } else {
                throw_error("SyntaxError", "Expected 'to' after from-value in iterate on line %d", line->line_num);
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
                if (current_reply.has_replied) break;
                v->type = VAR_INT;
                v->int_val = idx_val;
                if (v->string_val) {
                    free(v->string_val);
                    v->string_val = NULL;
                }
                execute_block(block_start, block_end);
                if (current_reply.has_replied) break;
                if (active_watch.active && active_watch.triggered) break;
            }
            i = block_end + 1;
        }
        else if (strncmp(effective_text, "if ", 3) == 0) {
            const char *cursor = effective_text + 3;
            const char *then_ptr = strstr(cursor, " then");
            if (!then_ptr) {
                throw_error("SyntaxError", "Expected 'then' after if condition on line %d", line->line_num);
            }
            int cond_len = then_ptr - cursor;
            char *cond_str = malloc(cond_len + 1);
            if (!cond_str) throw_error("MemoryAllocationError", "Memory allocation failed");
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

                char resolved_lookahead_buf[2048];
                const char *eff_lookahead = resolve_alias_line(lookahead->text, resolved_lookahead_buf, sizeof(resolved_lookahead_buf));

                if (strncmp(eff_lookahead, "else if ", 8) == 0) {
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
                        const char *elif_cursor = eff_lookahead + 8;
                        const char *elif_then_ptr = strstr(elif_cursor, " then");
                        if (!elif_then_ptr) {
                            throw_error("SyntaxError", "Expected 'then' after else if condition on line %d", lookahead->line_num);
                        }
                        int elif_cond_len = elif_then_ptr - elif_cursor;
                        char *elif_cond_str = malloc(elif_cond_len + 1);
                        if (!elif_cond_str) throw_error("MemoryAllocationError", "Memory allocation failed");
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
                else if (strncmp(eff_lookahead, "else ", 5) == 0 || strcmp(eff_lookahead, "else") == 0) {
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
        else if (strncmp(effective_text, "while ", 6) == 0) {
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
            const char *cursor = effective_text + 6;
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
            if (!cond_buf) throw_error("MemoryAllocationError", "Memory allocation failed");
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
                    if (current_reply.has_replied) break;
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
        else if (strncmp(effective_text, "until ", 6) == 0) {
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
            const char *cursor = effective_text + 6;
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
            if (!cond_buf) throw_error("MemoryAllocationError", "Memory allocation failed");
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
                    if (current_reply.has_replied) break;
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
        else if (strcmp(effective_text, "do") == 0) {
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

                int saved_scope = current_scope_depth;
                int saved_call_ptr = call_stack_ptr;

                if (setjmp(jmp_env_stack[jmp_stack_ptr++]) == 0) {
                    execute_block(do_start, do_end);
                    jmp_stack_ptr--;
                } else {
                    jmp_stack_ptr--;
                    pop_scope(saved_scope);
                    current_scope_depth = saved_scope;
                    call_stack_ptr = saved_call_ptr;

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
        else if (strcmp(effective_text, "ForceErrors") == 0) {
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
        else if (strcmp(effective_text, "CriticalErrors") == 0) {
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
        else if (strncmp(effective_text, "inject", 6) == 0 && (effective_text[6] == ' ' || effective_text[6] == '\0')) {
            const char *lang_ptr = effective_text + 6;
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
                char col[64] = "";
                get_attribute_str(effective_text, "color", col, sizeof(col));
                if (col[0] != '\0') {
                    apply_color(col);
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
        else if (strcmp(effective_text, "SuppressErrors") == 0) {
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
            execute_line(effective_text, line->line_num);
            if (current_reply.has_replied) {
                break;
            }
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
        if (strncmp(effective_text, "def ", 4) == 0 ||
            strncmp(effective_text, "loop ", 5) == 0 || strcmp(effective_text, "loop") == 0 ||
            strncmp(effective_text, "iterate ", 8) == 0 ||
            strncmp(effective_text, "if ", 3) == 0 ||
            strncmp(effective_text, "while ", 6) == 0 ||
            strncmp(effective_text, "until ", 6) == 0 ||
            strcmp(effective_text, "do") == 0 ||
            strcmp(effective_text, "ForceErrors") == 0 ||
            strcmp(effective_text, "CriticalErrors") == 0 ||
            strcmp(effective_text, "SuppressErrors") == 0) {

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
