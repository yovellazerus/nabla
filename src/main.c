
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

/*==============================================================================
=============            Debug & Errors           ==============================
==============================================================================*/

// Fatal OOM (unrecoverable -> abort) 
#define OUT_OF_MEMORY(msg)                                                                       \
    do {                                                                                        \
        fprintf(stderr, "\033[1;31m[OOM]\033[0m [%s:%d:%s()]: %s\n",                            \
                __FILE__, __LINE__, __func__, (msg));                                           \
        exit(EXIT_FAILURE);                                                                     \
    } while (0)

/*==============================================================================
==================                Token           ==============================
==============================================================================*/

#define TOKEN_TYPES(X)      \
    X(TOKEN_EOF)            \
    X(TOKEN_NEWLINE)        \
    X(TOKEN_IDENT)          \
    X(TOKEN_NUMBER)         \
    X(TOKEN_STRING)         \
    X(TOKEN_PLUS)           \
    X(TOKEN_MINUS)          \
    X(TOKEN_STAR)           \
    X(TOKEN_SLASH)          \
    X(TOKEN_EQUAL)          \
    X(TOKEN_COLON_EQUAL)    \
    X(TOKEN_BANG)           \
    X(TOKEN_BANG_EQUAL)     \
    X(TOKEN_EQUAL_EQUAL)    \
    X(TOKEN_LESS)           \
    X(TOKEN_LESS_EQUAL)     \
    X(TOKEN_GREATER)        \
    X(TOKEN_GREATER_EQUAL)  \
    X(TOKEN_QUESTION)       \
    X(TOKEN_LPAREN)         \
    X(TOKEN_RPAREN)         \
    X(TOKEN_ERROR)

typedef enum {
#define X(name) name,
    TOKEN_TYPES(X)
#undef X
    TOKEN_COUNT
} TokenType;

typedef struct Token {
    TokenType type;
    const char *start;  // Zero-copy slice into source buffer
    size_t length;
    size_t line;
    size_t col;
    struct Token *next;
} Token;

static const char *token_type_str(TokenType type) {
    static const char *names[] = {
#define X(name) [name] = #name,
        TOKEN_TYPES(X)
#undef X
    };
    return (type >= 0 && type < TOKEN_COUNT) ? names[type] : "TOKEN_UNKNOWN";
}

static const char *token_op_symbol(TokenType op) {
    switch (op) {
        case TOKEN_PLUS:            return "+";
        case TOKEN_MINUS:           return "-";
        case TOKEN_STAR:            return "*";
        case TOKEN_SLASH:           return "/";
        case TOKEN_BANG:            return "!";
        case TOKEN_BANG_EQUAL:      return "!=";
        case TOKEN_EQUAL_EQUAL:     return "==";
        case TOKEN_LESS:            return "<";
        case TOKEN_LESS_EQUAL:      return "<=";
        case TOKEN_GREATER:         return ">";
        case TOKEN_GREATER_EQUAL:   return ">=";
        
        default:          return token_type_str(op);
    }
}

static Token *token_make(TokenType type, const char *start, size_t len, size_t line, size_t col) {
    Token *tok = malloc(sizeof(Token));
    if (!tok) OUT_OF_MEMORY("allocating Token");
    tok->type = type;
    tok->start = start;
    tok->length = len;
    tok->line = line;
    tok->col = col;
    tok->next = NULL;
    return tok;
}

static void token_print(const Token *tok) {
    if (tok->type == TOKEN_NEWLINE) {
        printf("[%zu:%zu] %-12s '\\n'\n", tok->line, tok->col, token_type_str(tok->type));
    } else if (tok->type == TOKEN_EOF) {
        printf("[%zu:%zu] %-12s <EOF>\n", tok->line, tok->col, token_type_str(tok->type));
    } else {
        printf("[%zu:%zu] %-12s '%.*s'\n",
               tok->line, tok->col, token_type_str(tok->type),
               (int)tok->length, tok->start);
    }
}

static void token_list_free(Token *head) {
    while (head) {
        Token *next = head->next;
        free(head);
        head = next;
    }
}

/*==============================================================================
==================                Lexer           ==============================
==============================================================================*/

typedef struct Lexer {
    const char *src;
    const char *current;
    size_t line;
    size_t col;
} Lexer;

static void lexer_init(Lexer *lex, const char *src) {
    lex->src = src;
    lex->current = src;
    lex->line = 1;
    lex->col = 1;
}

static inline bool lexer_is_at_end(const Lexer *lex) {
    return *lex->current == '\0';
}

static inline char lexer_peek(const Lexer *lex) {
    return *lex->current;
}

static inline char lexer_peek_next(const Lexer *lex) {
    return lexer_is_at_end(lex) ? '\0' : lex->current[1];
}

static char lexer_advance(Lexer *lex) {
    char c = *lex->current++;
    if (c == '\n') {
        lex->line++;
        lex->col = 1;
    } else {
        lex->col++;
    }
    return c;
}

static void lexer_skip_spaces_and_comments(Lexer *lex) {
    for (;;) {
        char c = lexer_peek(lex);
        if (c == ' ' || c == '\t' || c == '\r') {
            lexer_advance(lex);
        } else if (c == '#') {
            while (!lexer_is_at_end(lex) && lexer_peek(lex) != '\n') {
                lexer_advance(lex);
            }
        } else {
            break;
        }
    }
}

static Token *lexer_scan_ident(Lexer *lex, const char *start, size_t line, size_t col) {
    while (isalnum((unsigned char)lexer_peek(lex)) || lexer_peek(lex) == '_') {
        lexer_advance(lex);
    }
    return token_make(TOKEN_IDENT, start, (size_t)(lex->current - start), line, col);
}

static Token *lexer_scan_number(Lexer *lex, const char *start, size_t line, size_t col) {
    while (isdigit((unsigned char)lexer_peek(lex))) lexer_advance(lex);

    if (lexer_peek(lex) == '.' && isdigit((unsigned char)lexer_peek_next(lex))) {
        lexer_advance(lex);
        while (isdigit((unsigned char)lexer_peek(lex))) lexer_advance(lex);
    }

    if (lexer_peek(lex) == 'e' || lexer_peek(lex) == 'E') {
        lexer_advance(lex);
        if (lexer_peek(lex) == '+' || lexer_peek(lex) == '-') lexer_advance(lex);
        while (isdigit((unsigned char)lexer_peek(lex))) lexer_advance(lex);
    }

    return token_make(TOKEN_NUMBER, start, (size_t)(lex->current - start), line, col);
}

static Token *lexer_scan_string(Lexer *lex, const char *start, size_t line, size_t col) {
    const char *content_start = lex->current;
    while (!lexer_is_at_end(lex) && lexer_peek(lex) != '"' && lexer_peek(lex) != '\n') {
        lexer_advance(lex);
    }
    if (lexer_peek(lex) != '"') {
        return token_make(TOKEN_ERROR, start, (size_t)(lex->current - start), line, col);
    }
    size_t content_len = (size_t)(lex->current - content_start);
    lexer_advance(lex); // consume closing '"'
    return token_make(TOKEN_STRING, content_start, content_len, line, col);
}

static Token *lexer_next_token(Lexer *lex) {
    lexer_skip_spaces_and_comments(lex);

    size_t line = lex->line;
    size_t col = lex->col;
    const char *start = lex->current;

    if (lexer_is_at_end(lex)) {
        return token_make(TOKEN_EOF, start, 0, line, col);
    }

    char c = lexer_advance(lex);

    if (isalpha((unsigned char)c) || c == '_') {
        return lexer_scan_ident(lex, start, line, col);
    }
    if (isdigit((unsigned char)c)) {
        return lexer_scan_number(lex, start, line, col);
    }

    switch (c) {
        
        case '"':  return lexer_scan_string(lex, start, line, col);
        
        case ':':
            if (lexer_peek(lex) == '=') {
                lexer_advance(lex);
                return token_make(TOKEN_COLON_EQUAL, start, 2, line, col);
            }
            return token_make(TOKEN_ERROR, start, 1, line, col);
            
        case '\n': return token_make(TOKEN_NEWLINE, start, 1, line, col);
        case '+':  return token_make(TOKEN_PLUS,    start, 1, line, col);
        case '-':  return token_make(TOKEN_MINUS,   start, 1, line, col);
        case '*':  return token_make(TOKEN_STAR,    start, 1, line, col);
        case '/':  return token_make(TOKEN_SLASH,   start, 1, line, col);
        
        case '=':
            if (lexer_peek(lex) == '=') {
                lexer_advance(lex);
                return token_make(TOKEN_EQUAL_EQUAL, start, 2, line, col);
            }
            return token_make(TOKEN_EQUAL, start, 1, line, col);
        case '!':
            if (lexer_peek(lex) == '=') {
                lexer_advance(lex);
                return token_make(TOKEN_BANG_EQUAL, start, 2, line, col);
            }
            return token_make(TOKEN_BANG, start, 1, line, col);
        case '<':
            if (lexer_peek(lex) == '=') {
                lexer_advance(lex);
                return token_make(TOKEN_LESS_EQUAL, start, 2, line, col);
            }
            return token_make(TOKEN_LESS, start, 1, line, col);
        case '>':
            if (lexer_peek(lex) == '=') {
                lexer_advance(lex);
                return token_make(TOKEN_GREATER_EQUAL, start, 2, line, col);
            }
            return token_make(TOKEN_GREATER, start, 1, line, col);
            
        case '?':  return token_make(TOKEN_QUESTION,    start, 1, line, col);
        case '(':  return token_make(TOKEN_LPAREN,  start, 1, line, col);
        case ')':  return token_make(TOKEN_RPAREN,  start, 1, line, col);
        
        default:   return token_make(TOKEN_ERROR,   start, 1, line, col);
    }
}

static Token *lexer_tokenize(Lexer *lex) {
    Token dummy = {0};
    Token *tail = &dummy;

    for (;;) {
        Token *tok = lexer_next_token(lex);

        if (tok->type == TOKEN_NEWLINE &&
            (tail == &dummy || tail->type == TOKEN_NEWLINE)) {
            free(tok);
            continue;
        }

        tail->next = tok;
        tail = tok;

        if (tok->type == TOKEN_EOF) break;
    }

    return dummy.next;
}

/*==============================================================================
==================                Object           =============================
==============================================================================*/

#define OBJECT_TYPES(X) \
    X(OBJECT_NONE)      \
    X(OBJECT_NUMBER)    \
    X(OBJECT_STRING)    \
    X(OBJECT_BOOL)      \
    X(OBJECT_ERROR)

typedef enum {
#define X(name) name,
    OBJECT_TYPES(X)
#undef X
    OBJECT_COUNT
} ObjectType;

static const char *object_type_str(ObjectType type) {
    static const char *names[] = {
#define X(name) [name] = #name,
        OBJECT_TYPES(X)
#undef X
    };
    return (type >= 0 && type < OBJECT_COUNT) ? names[type] : "OBJECT_UNKNOWN";
}

/* OOP-style vtable dispatch:
 USEGE:
 *   _(lhs, add, rhs)  ->  (lhs)->ops->add((lhs), rhs)
 *   _(val, print)        ->  (val)->ops->print((val))
*/
#define _(object, method, ...) ((object)->ops->method((object), ##__VA_ARGS__))

typedef struct Object Object;

/* Vtable: every Object type provides its own implementation of these ops.
 * Unimplemented ops are NULL (meaning "unsupported operation for this type"). */
typedef struct ObjectOpertions {
    // Arithmetic
    Object *(*add)(Object *self, Object *other);
    Object *(*sub)(Object *self, Object *other);
    Object *(*mul)(Object *self, Object *other);
    Object *(*div)(Object *self, Object *other);
    // Unary
    Object *(*neg)(Object *self);
    Object *(*not)(Object *self);
    // Comparisons
    Object *(*eq)(Object *self, Object *other);
    Object *(*neq)(Object *self, Object *other);
    Object *(*lt)(Object *self, Object *other);
    Object *(*lte)(Object *self, Object *other);
    Object *(*gt)(Object *self, Object *other);
    Object *(*gte)(Object *self, Object *other);
    // Core
    bool    (*is_truthy)(const Object *self);
    void    (*print)(const Object *self);
    void    (*destroy)(Object *self);
} ObjectOpertions;

struct Object {
    ObjectType type;
    int refcount;
    const ObjectOpertions *ops;
};

static inline bool object_is_error(const Object *obj) {
    return obj != NULL && obj->type == OBJECT_ERROR;
}

#define OBJECT_IMMORTAL -1

static Object *object_retain(Object *obj) {
    if (!obj || obj->refcount == OBJECT_IMMORTAL) return obj;
    obj->refcount++;
    return obj;
}

static void object_release(Object *obj) {
    if (!obj || obj->refcount == OBJECT_IMMORTAL) return;
    if (--obj->refcount == 0) {
        if (obj->ops->destroy) obj->ops->destroy(obj);
        free(obj);
    }
}

static Object *bool_make(bool value);

/* Shared unary '!' using the object's own is_truthy hook */
static Object *object_not(Object *self) {
    bool truthy = self->ops->is_truthy ? _(self, is_truthy) : true;
    return bool_make(!truthy);
}

/* Shared equality for singletons (Bool, None) via pointer identity */
static Object *ptr_eq(Object *self, Object *other) {
    return bool_make(self == other);
}

static Object *ptr_neq(Object *self, Object *other) {
    return bool_make(self != other);
}

/* ****** Derived Objects (Struct Embedding / Intrusive Inheritance) ******** */

/* --- None (singleton) --- */

static bool none_truthy(const Object *self) {
    (void)self;
    return false;
}

static void none_print(const Object *self) {
    (void)self;
    printf("None");
}

static const ObjectOpertions NONE_OPS = {
    .not       = object_not,
    .eq        = ptr_eq,
    .neq       = ptr_neq,
    .is_truthy = none_truthy,
    .print     = none_print
};

static Object NONE_OBJ = {
    .type     = OBJECT_NONE,
    .refcount = OBJECT_IMMORTAL,
    .ops      = &NONE_OPS
};

static inline Object *none_make(void) {
    return &NONE_OBJ;
}

/* --- Error --- */

typedef struct {
    Object base;
    char *message; // Heap-owned, formatted message
    size_t line;   // 0 if raised in Object layer; patched by node_eval
} ErrorObject;

static void err_print(const Object *self) {
    const ErrorObject *e = (const ErrorObject *)self;
    if (e->line > 0) {
        printf("\033[1;31mRuntimeError\033[0m \033[36m[line %zu]\033[0m: %s",
               e->line, e->message);
    } else {
        printf("\033[1;31mRuntimeError\033[0m: %s", e->message);
    }
}

static void err_destroy(Object *self) {
    free(((ErrorObject *)self)->message);
}

static const ObjectOpertions ERROR_OPS = {
    .print     = err_print,
    .destroy   = err_destroy
};

static Object *error_make(size_t line, const char *fmt, ...) {
    va_list args, copy;
    va_start(args, fmt);
    va_copy(copy, args);

    int needed = vsnprintf(NULL, 0, fmt, args);
    va_end(args);

    char *msg = malloc((size_t)needed + 1);
    if (!msg) {
        va_end(copy);
        OUT_OF_MEMORY("allocating ErrorObject message");
    }
    vsnprintf(msg, (size_t)needed + 1, fmt, copy);
    va_end(copy);

    ErrorObject *o = malloc(sizeof(ErrorObject));
    if (!o) {
        free(msg);
        OUT_OF_MEMORY("allocating ErrorObject");
    }
    o->base.type = OBJECT_ERROR;
    o->base.refcount = 1;
    o->base.ops = &ERROR_OPS;
    o->message = msg;
    o->line = line;
    return (Object *)o;
}

/* --- Number --- */

typedef struct {
    Object base;
    double value;
} NumberObject;

static Object *number_make(double val);

static Object *num_neg(Object *self) {
    return number_make(-((NumberObject *)self)->value);
}

static bool num_truthy(const Object *self) {
    return ((const NumberObject *)self)->value != 0.0;
}

static Object *num_eq(Object *self, Object *other) {
    if (other->type != OBJECT_NUMBER) return bool_make(false);
    return bool_make(((NumberObject *)self)->value == ((NumberObject *)other)->value);
}
static Object *num_neq(Object *self, Object *other) {
    if (other->type != OBJECT_NUMBER) return bool_make(true);
    return bool_make(((NumberObject *)self)->value != ((NumberObject *)other)->value);
}
static Object *num_lt(Object *self, Object *other) {
    if (other->type != OBJECT_NUMBER) return NULL;
    return bool_make(((NumberObject *)self)->value < ((NumberObject *)other)->value);
}
static Object *num_lte(Object *self, Object *other) {
    if (other->type != OBJECT_NUMBER) return NULL;
    return bool_make(((NumberObject *)self)->value <= ((NumberObject *)other)->value);
}
static Object *num_gt(Object *self, Object *other) {
    if (other->type != OBJECT_NUMBER) return NULL;
    return bool_make(((NumberObject *)self)->value > ((NumberObject *)other)->value);
}
static Object *num_gte(Object *self, Object *other) {
    if (other->type != OBJECT_NUMBER) return NULL;
    return bool_make(((NumberObject *)self)->value >= ((NumberObject *)other)->value);
}

static Object *num_add(Object *self, Object *other) {
    if (other->type != OBJECT_NUMBER) return NULL;
    return number_make(((NumberObject *)self)->value + ((NumberObject *)other)->value);
}

static Object *num_sub(Object *self, Object *other) {
    if (other->type != OBJECT_NUMBER) return NULL;
    return number_make(((NumberObject *)self)->value - ((NumberObject *)other)->value);
}

static Object *num_mul(Object *self, Object *other) {
    if (other->type != OBJECT_NUMBER) return NULL;
    return number_make(((NumberObject *)self)->value * ((NumberObject *)other)->value);
}

static Object *num_div(Object *self, Object *other) {
    if (other->type != OBJECT_NUMBER) return NULL;
    double rhs = ((NumberObject *)other)->value;
    if (rhs == 0.0) {
        return error_make(0, "division by zero");
    }
    return number_make(((NumberObject *)self)->value / rhs);
}

static void num_print(const Object *self) {
    printf("%g", ((const NumberObject *)self)->value);
}

static const ObjectOpertions NUMBER_OPS = {
    .add       = num_add,
    .sub       = num_sub,
    .mul       = num_mul,
    .div       = num_div,
    .neg       = num_neg,
    .not       = object_not,
    .eq        = num_eq,
    .neq       = num_neq,
    .lt        = num_lt,
    .lte       = num_lte,
    .gt        = num_gt,
    .gte       = num_gte,
    .is_truthy = num_truthy,
    .print     = num_print
};

static Object *number_make(double val) {
    NumberObject *o = malloc(sizeof(NumberObject));
    if (!o) OUT_OF_MEMORY("allocating NumberObject");
    o->base.type = OBJECT_NUMBER;
    o->base.refcount = 1;
    o->base.ops = &NUMBER_OPS;
    o->value = val;
    return (Object *)o;
}

/* --- String --- */

typedef struct {
    Object base;
    char *data;    // Heap-owned, null-terminated
    size_t length;
} StringObject;

static Object *string_make(const char *src, size_t len);

/* --- String --- */

static bool str_truthy(const Object *self) {
    return ((const StringObject *)self)->length > 0;
}

static Object *str_eq(Object *self, Object *other) {
    if (other->type != OBJECT_STRING) return bool_make(false);
    StringObject *a = (StringObject *)self;
    StringObject *b = (StringObject *)other;
    return bool_make(a->length == b->length && memcmp(a->data, b->data, a->length) == 0);
}
static Object *str_neq(Object *self, Object *other) {
    if (other->type != OBJECT_STRING) return bool_make(true);
    StringObject *a = (StringObject *)self;
    StringObject *b = (StringObject *)other;
    return bool_make(a->length != b->length || memcmp(a->data, b->data, a->length) != 0);
}
static Object *str_lt(Object *self, Object *other) {
    if (other->type != OBJECT_STRING) return NULL;
    return bool_make(strcmp(((StringObject *)self)->data, ((StringObject *)other)->data) < 0);
}
static Object *str_lte(Object *self, Object *other) {
    if (other->type != OBJECT_STRING) return NULL;
    return bool_make(strcmp(((StringObject *)self)->data, ((StringObject *)other)->data) <= 0);
}
static Object *str_gt(Object *self, Object *other) {
    if (other->type != OBJECT_STRING) return NULL;
    return bool_make(strcmp(((StringObject *)self)->data, ((StringObject *)other)->data) > 0);
}
static Object *str_gte(Object *self, Object *other) {
    if (other->type != OBJECT_STRING) return NULL;
    return bool_make(strcmp(((StringObject *)self)->data, ((StringObject *)other)->data) >= 0);
}

static Object *str_add(Object *self, Object *other) {
    if (other->type != OBJECT_STRING) return NULL;
    StringObject *a = (StringObject *)self;
    StringObject *b = (StringObject *)other;

    size_t total = a->length + b->length;
    char *buf = malloc(total + 1);
    if (!buf) OUT_OF_MEMORY("concatenating StringObject");
    memcpy(buf, a->data, a->length);
    memcpy(buf + a->length, b->data, b->length);
    buf[total] = '\0';

    StringObject *res = malloc(sizeof(StringObject));
    if (!res) OUT_OF_MEMORY("allocating StringObject");
    res->base.type = OBJECT_STRING;
    res->base.refcount = 1;
    res->base.ops = a->base.ops;
    res->data = buf;
    res->length = total;
    return (Object *)res;
}

static void str_print(const Object *self) {
    const StringObject *s = (const StringObject *)self;
    printf("%.*s", (int)s->length, s->data);
}

static void str_destroy(Object *self) {
    free(((StringObject *)self)->data);
}

static const ObjectOpertions STRING_OPS = {
    .add       = str_add,
    .not       = object_not,
    .eq        = str_eq,
    .neq       = str_neq,
    .lt        = str_lt,
    .lte       = str_lte,
    .gt        = str_gt,
    .gte       = str_gte,
    .is_truthy = str_truthy,
    .print     = str_print,
    .destroy   = str_destroy
};

static Object *string_make(const char *src, size_t len) {
    StringObject *o = malloc(sizeof(StringObject));
    if (!o) OUT_OF_MEMORY("allocating StringObject");
    o->data = malloc(len + 1);
    if (!o->data) OUT_OF_MEMORY("allocating StringObject buffer");
    memcpy(o->data, src, len);
    o->data[len] = '\0';

    o->base.type = OBJECT_STRING;
    o->base.refcount = 1;
    o->base.ops = &STRING_OPS;
    o->length = len;
    return (Object *)o;
}

/* --- Bool (singletons) --- */

typedef struct {
    Object base;
    bool value;
} BoolObject;

static bool bool_truthy(const Object *self) {
    return ((const BoolObject *)self)->value;
}

static void bool_print(const Object *self) {
    printf("%s", ((const BoolObject *)self)->value ? "True" : "False");
}

static const ObjectOpertions BOOL_OPS = {
    .not       = object_not,
    .eq        = ptr_eq,
    .neq       = ptr_neq,
    .is_truthy = bool_truthy,
    .print     = bool_print
};

static BoolObject TRUE_OBJ = {
    .base  = { .type = OBJECT_BOOL, .refcount = OBJECT_IMMORTAL, .ops = &BOOL_OPS },
    .value = true
};

static BoolObject FALSE_OBJ = {
    .base  = { .type = OBJECT_BOOL, .refcount = OBJECT_IMMORTAL, .ops = &BOOL_OPS },
    .value = false
};

static Object *bool_make(bool value) {
    return value ? (Object *)&TRUE_OBJ : (Object *)&FALSE_OBJ;
}

/*==============================================================================
====================                Env           ==============================
==============================================================================*/

typedef struct EnvEntry {
    char *name;
    size_t name_len;
    Object *value;
    bool is_const;
    struct EnvEntry *next;
} EnvEntry;

typedef struct Env {
    EnvEntry *head;
    struct Env *parent; // Lexical outer scope (NULL for global)
} Env;

static Env *env_make(Env *parent) {
    Env *env = malloc(sizeof(Env));
    if (!env) OUT_OF_MEMORY("allocating Env");
    env->head = NULL;
    env->parent = parent;
    return env;
}

static void env_free(Env *env) {
    if (!env) return;
    EnvEntry *cur = env->head;
    while (cur) {
        EnvEntry *next = cur->next;
        free(cur->name);
        object_release(cur->value);
        free(cur);
        cur = next;
    }
    free(env);
}

/* Look up a variable, walking up the parent chain if not found locally. */
static Object *env_get(const Env *env, const char *name, size_t len) {
    for (const Env *scope = env; scope != NULL; scope = scope->parent) {
        for (const EnvEntry *e = scope->head; e != NULL; e = e->next) {
            if (e->name_len == len && memcmp(e->name, name, len) == 0) {
                return object_retain(e->value);
            }
        }
    }
    return NULL;
}

/* Bind or update a variable in the current scope (retains val). */
static bool env_set(Env *env, const char *name, size_t len, Object *val, bool is_const) {
    for (EnvEntry *e = env->head; e != NULL; e = e->next) {
        if (e->name_len == len && memcmp(e->name, name, len) == 0) {
            if (e->is_const) return false; // blocked!
            object_retain(val);
            object_release(e->value);
            e->value = val;
            e->is_const = is_const; // allows locking an existing var with :=
            return true;
        }
    }

    // not found, make a new one
    EnvEntry *entry = malloc(sizeof(EnvEntry));
    if (!entry) OUT_OF_MEMORY("allocating EnvEntry");
    entry->name = malloc(len + 1);
    if (!entry->name) OUT_OF_MEMORY("allocating EnvEntry name");
    memcpy(entry->name, name, len);
    entry->name[len] = '\0';
    entry->name_len = len;
    entry->value = object_retain(val);
    entry->is_const = is_const;
    entry->next = env->head;
    env->head = entry;
    return true;
}

/*==============================================================================
===================                Node           ==============================
==============================================================================*/

#define NODE_TYPES(X) \
    X(NODE_LITERAL)   \
    X(NODE_NAME)      \
    X(NODE_UNARY_OP)  \
    X(NODE_BINARY_OP) \
    X(NODE_ASSIGN)    \
    X(NODE_BLOCK)     \
    X(NODE_DUMP)

typedef enum {
#define X(name) name,
    NODE_TYPES(X)
#undef X
    NODE_COUNT
} NodeType;

static const char *node_type_str(NodeType type) {
    static const char *names[] = {
#define X(name) [name] = #name,
        NODE_TYPES(X)
#undef X
    };
    return (type >= 0 && type < NODE_COUNT) ? names[type] : "NODE_UNKNOWN";
}

typedef struct Node {
    NodeType type;
    size_t line;
} Node;

typedef struct {
    Node base;
    Object *value;
} LiteralNode;

typedef struct {
    Node base;
    const char *start;
    size_t length;
} NameNode;

typedef struct {
    Node base;
    TokenType op;
    Node *operand;
} UnaryOpNode;

typedef struct {
    Node base;
    TokenType op;
    Node *lhs;
    Node *rhs;
} BinaryOpNode;

typedef struct {
    Node base;
    Node *lhs;
    Node *rhs;
    bool is_const;
} AssignNode;

typedef struct {
    Node base;
    Node **stmts;
    size_t count;
    size_t capacity;
    bool new_scope;   // true = create child Env; false = run in caller's Env (e.g. Program)
} BlockNode;

typedef struct {
    Node base;
    Node *expr;
} DumpNode;

static Node *node_new_literal(Object *value, size_t line) {
    LiteralNode *n = malloc(sizeof(LiteralNode));
    if (!n) OUT_OF_MEMORY("allocating LiteralNode");
    n->base.type = NODE_LITERAL;
    n->base.line = line;
    n->value = value; // Takes ownership of caller's ref
    return (Node *)n;
}

static Node *node_new_name(const char *start, size_t length, size_t line) {
    NameNode *n = malloc(sizeof(NameNode));
    if (!n) OUT_OF_MEMORY("allocating NameNode");
    n->base.type = NODE_NAME;
    n->base.line = line;
    n->start = start;
    n->length = length;
    return (Node *)n;
}

static Node *node_new_unary(TokenType op, Node *operand, size_t line) {
    UnaryOpNode *n = malloc(sizeof(UnaryOpNode));
    if (!n) OUT_OF_MEMORY("allocating UnaryOpNode");
    n->base.type = NODE_UNARY_OP;
    n->base.line = line;
    n->op = op;
    n->operand = operand;
    return (Node *)n;
}

static Node *node_new_binary_op(TokenType op, Node *lhs, Node *rhs, size_t line) {
    BinaryOpNode *n = malloc(sizeof(BinaryOpNode));
    if (!n) OUT_OF_MEMORY("allocating BinaryOpNode");
    n->base.type = NODE_BINARY_OP;
    n->base.line = line;
    n->op = op;
    n->lhs = lhs;
    n->rhs = rhs;
    return (Node *)n;
}

static Node *node_new_assign(Node *lhs, Node *rhs, bool is_const, size_t line) {
    AssignNode *n = malloc(sizeof(AssignNode));
    if (!n) OUT_OF_MEMORY("allocating AssignNode");
    n->base.type = NODE_ASSIGN;
    n->base.line = line;
    n->lhs = lhs;
    n->rhs = rhs;
    n->is_const = is_const;
    return (Node *)n;
}

static Node *node_new_block(bool new_scope, size_t line) {
    BlockNode *b = malloc(sizeof(BlockNode));
    if (!b) OUT_OF_MEMORY("allocating BlockNode");
    b->base.type = NODE_BLOCK;
    b->base.line = line;
    b->stmts = NULL;
    b->count = 0;
    b->capacity = 0;
    b->new_scope = new_scope;
    return (Node *)b;
}

// BlockNode helper
static void block_add_stmt(Node *block_node, Node *stmt) {
    BlockNode *b = (BlockNode *)block_node;
    if (b->count >= b->capacity) {
        size_t new_cap = (b->capacity == 0) ? 4 : b->capacity * 2;
        Node **new_stmts = realloc(b->stmts, new_cap * sizeof(Node *));
        if (!new_stmts) OUT_OF_MEMORY("growing BlockNode stmts");
        b->stmts = new_stmts;
        b->capacity = new_cap;
    }
    b->stmts[b->count++] = stmt;
}

// BlockNode helper
static Node *block_add_list(bool new_scope, size_t line, Node **stmts, size_t count) {
    Node *b = node_new_block(new_scope, line);
    for (size_t i = 0; i < count; i++) {
        block_add_stmt(b, stmts[i]);
    }
    return b;
}

static Node *node_new_dump(Node *expr, size_t line) {
    DumpNode *n = malloc(sizeof(DumpNode));
    if (!n) OUT_OF_MEMORY("allocating DumpNode");
    n->base.type = NODE_DUMP;
    n->base.line = line;
    n->expr = expr;
    return (Node *)n;
}

static void node_free(Node *node) {
    if (!node) return;
    switch (node->type) {
        case NODE_LITERAL:
            object_release(((LiteralNode *)node)->value);
            break;
        case NODE_NAME:
            break;
        case NODE_UNARY_OP:
            node_free(((UnaryOpNode *)node)->operand);
            break;
        case NODE_BINARY_OP: {
            BinaryOpNode *b = (BinaryOpNode *)node;
            node_free(b->lhs);
            node_free(b->rhs);
            break;
        }
        case NODE_ASSIGN: {
            AssignNode *a = (AssignNode *)node;
            node_free(a->lhs);
            node_free(a->rhs);
            break;
        }
        case NODE_BLOCK: {
            BlockNode *b = (BlockNode *)node;
            for (size_t i = 0; i < b->count; i++) {
                node_free(b->stmts[i]);
            }
            free(b->stmts);
            break;
        }
        case NODE_DUMP:
            node_free(((DumpNode *)node)->expr);
            break;
        default:
            break;
    }
    free(node);
}

// Graphviz DOT engine
static void node_write_dot(const Node *node, FILE *out) {
    if (!node) return;

    // Label switch:
    fprintf(out, "  \"n%p\" [", (const void *)node);
    switch (node->type) {
        case NODE_LITERAL: {
            const Object *v = ((const LiteralNode *)node)->value;
            fprintf(out, "shape=ellipse, fillcolor=\"#E8F5E9\", label=\"");
            if (v->type == OBJECT_NUMBER) {
                fprintf(out, "%g\\n(num)", ((const NumberObject *)v)->value);
            } else if (v->type == OBJECT_STRING) {
                const StringObject *s = (const StringObject *)v;
                fprintf(out, "\\\"%.*s\\\"\\n(str)", (int)s->length, s->data);
            } else if (v->type == OBJECT_BOOL) {
                fprintf(out, "%s\\n(bool)", ((const BoolObject *)v)->value ? "True" : "False");
            } else if (v->type == OBJECT_NONE) fprintf(out, "None\\n(none)");
            fprintf(out, "\"");
            break;
        }
        case NODE_NAME: {
            const NameNode *n = (const NameNode *)node;
            fprintf(out, "shape=box, style=\"rounded,filled\", fillcolor=\"#E3F2FD\", label=\"%.*s\"",
                    (int)n->length, n->start);
            break;
        }
        case NODE_UNARY_OP: {
            const UnaryOpNode *u = (const UnaryOpNode *)node;
            fprintf(out, "shape=circle, fillcolor=\"#FFF3E0\", label=\"%s\"",
                    u->op == TOKEN_MINUS ? "-" : "!");
            break;
        }
        case NODE_BINARY_OP: {
            const BinaryOpNode *b = (const BinaryOpNode *)node;
            fprintf(out, "shape=circle, fillcolor=\"#FFF3E0\", label=\"%s\"",
                    token_op_symbol(b->op));
            break;
        }
        
        case NODE_ASSIGN: {
            const AssignNode *a = (const AssignNode *)node;
            fprintf(out, "shape=box, fillcolor=\"%s\", label=\"%s\"",
                    a->is_const ? "#E1BEE7" : "#F3E5F5",
                    a->is_const ? ":=" : "=");
            break;
        }
        
        case NODE_BLOCK: {
            const BlockNode *b = (const BlockNode *)node;
            fprintf(out, "shape=box, style=\"dashed,filled\", fillcolor=\"#ECEFF1\", label=\"{}\"");
            break;
        }
            
        case NODE_DUMP:
            fprintf(out, "shape=box, fillcolor=\"#FCE4EC\", label=\"?\\n\"");
            break;
        default:
            fprintf(out, "shape=box, fillcolor=\"#FFCDD2\", label=\"<Unknown>\"");
            break;
    }
    fprintf(out, "];\n");

    // Edge switch:
    switch (node->type) {
        case NODE_UNARY_OP: {
            const UnaryOpNode *u = (const UnaryOpNode *)node;
            fprintf(out, "  \"n%p\" -> \"n%p\";\n", (const void *)node, (const void *)u->operand);
            node_write_dot(u->operand, out);
            break;
        }
        case NODE_BINARY_OP: {
            const BinaryOpNode *b = (const BinaryOpNode *)node;
            fprintf(out, "  \"n%p\" -> \"n%p\";\n", (const void *)node, (const void *)b->lhs);
            fprintf(out, "  \"n%p\" -> \"n%p\";\n", (const void *)node, (const void *)b->rhs);
            node_write_dot(b->lhs, out);
            node_write_dot(b->rhs, out);
            break;
        }
        case NODE_ASSIGN: {
            const AssignNode *a = (const AssignNode *)node;
            fprintf(out, "  \"n%p\" -> \"n%p\";\n", (const void *)node, (const void *)a->lhs);
            fprintf(out, "  \"n%p\" -> \"n%p\";\n", (const void *)node, (const void *)a->rhs);
            node_write_dot(a->lhs, out);
            node_write_dot(a->rhs, out);
            break;
        }
        case NODE_BLOCK: {
            const BlockNode *b = (const BlockNode *)node;
            for (size_t i = 0; i < b->count; i++) {
                fprintf(out, "  \"n%p\" -> \"n%p\";\n", (const void *)node, (const void *)b->stmts[i]);
                node_write_dot(b->stmts[i], out);
            }
            break;
        }
        case NODE_DUMP: {
            const DumpNode *d = (const DumpNode *)node;
            fprintf(out, "  \"n%p\" -> \"n%p\";\n", (const void *)node, (const void *)d->expr);
            node_write_dot(d->expr, out);
            break;
        }
        default:
            break;
    }
}

// main interpreter function
static Object *node_eval(const Node *node, Env *env) {
    if (!node) return none_make();

    switch (node->type) {
        case NODE_LITERAL: {
            const LiteralNode *l = (const LiteralNode *)node;
            return object_retain(l->value);
        }

        case NODE_NAME: {
            const NameNode *n = (const NameNode *)node;
            Object *val = env_get(env, n->start, n->length);
            if (!val) {
                return error_make(node->line, "undefined variable '%.*s'",
                                  (int)n->length, n->start);
            }
            return val;
        }
        
        case NODE_UNARY_OP: {
            const UnaryOpNode *u = (const UnaryOpNode *)node;
            Object *val = node_eval(u->operand, env);
            if (object_is_error(val)) return val;

            Object *res = NULL;
            switch (u->op) {
                case TOKEN_MINUS: if (val->ops->neg) res = _(val, neg); break;
                case TOKEN_BANG:  if (val->ops->not) res = _(val, not); break;
                default: break;
            }

            if (!res) {
                res = error_make(node->line, "unsupported unary op %s for %s",
                                 token_type_str(u->op), object_type_str(val->type));
            }
            object_release(val);
            return res;
        }

        case NODE_BINARY_OP: {
            const BinaryOpNode *b = (const BinaryOpNode *)node;
            Object *lhs = node_eval(b->lhs, env);
            if (object_is_error(lhs)) return lhs;

            Object *rhs = node_eval(b->rhs, env);
            if (object_is_error(rhs)) {
                object_release(lhs);
                return rhs;
            }

            Object *res = NULL;
            switch (b->op) {
                // arithmetic
                case TOKEN_PLUS:          if (lhs->ops->add) res = _(lhs, add, rhs); break;
                case TOKEN_MINUS:         if (lhs->ops->sub) res = _(lhs, sub, rhs); break;
                case TOKEN_STAR:          if (lhs->ops->mul) res = _(lhs, mul, rhs); break;
                case TOKEN_SLASH:         if (lhs->ops->div) res = _(lhs, div, rhs); break;

                // comparisons
                case TOKEN_EQUAL_EQUAL:   if (lhs->ops->eq)  res = _(lhs, eq,  rhs); break;
                case TOKEN_BANG_EQUAL:    if (lhs->ops->neq) res = _(lhs, neq, rhs); break;
                case TOKEN_LESS:          if (lhs->ops->lt)  res = _(lhs, lt,  rhs); break;
                case TOKEN_LESS_EQUAL:    if (lhs->ops->lte) res = _(lhs, lte, rhs); break;
                case TOKEN_GREATER:       if (lhs->ops->gt)  res = _(lhs, gt,  rhs); break;
                case TOKEN_GREATER_EQUAL: if (lhs->ops->gte) res = _(lhs, gte, rhs); break;

                default: break;
            }

            if (!res) {
                res = error_make(node->line, "unsupported op %s for %s and %s",
                                 token_type_str(b->op),
                                 object_type_str(lhs->type),
                                 object_type_str(rhs->type));
            } else if (object_is_error(res) && ((ErrorObject *)res)->line == 0) {
                /* patch in the Program line number for Object-layer errors (e.g. div by zero) */
                ((ErrorObject *)res)->line = node->line;
            }

            object_release(lhs);
            object_release(rhs);
            return res;
        }

        case NODE_ASSIGN: {
            const AssignNode *a = (const AssignNode *)node;
            if (a->lhs->type != NODE_NAME) {
                return error_make(node->line, "invalid assignment target");
            }
            Object *val = node_eval(a->rhs, env);
            if (object_is_error(val)) return val;

            const NameNode *target = (const NameNode *)a->lhs;
            if (!env_set(env, target->start, target->length, val, a->is_const)) {
                object_release(val);
                return error_make(node->line, "cannot reassign constant '%.*s'",
                                  (int)target->length, target->start);
            }
            return val;
        }

        case NODE_DUMP: {
            const DumpNode *d = (const DumpNode *)node;
            Object *val = node_eval(d->expr, env);
            if (object_is_error(val)){
                return val;
            } 
            _(val, print);
            printf("\n");
            return val;
        }

        case NODE_BLOCK: {
            const BlockNode *b = (const BlockNode *)node;
            Env *scope = b->new_scope ? env_make(env) : env;
            Object *last = none_make();

            for (size_t i = 0; i < b->count; i++) {
                object_release(last);
                last = node_eval(b->stmts[i], scope);
                if (object_is_error(last)) {
                    _(last, print);
                    printf("\n");
                    object_release(last);
                    last = none_make();
                    // TODO: real exception unwinding
                    // break;
                }
            }

            if (b->new_scope) env_free(scope);
            return last;
        }

        default:
            return error_make(node->line, "internal error: unknown AST node %d", node->type);
            
    }
}

/*==============================================================================
==================                Parser           =============================
==============================================================================*/

// TODO: Recursive Descent Parser

/*==============================================================================
====================                Program           ==========================
==============================================================================*/

typedef struct {
    Node *root;
    Env  *global;
    // Later: char *source; const char *filename;
} Program;

static Program *program_make(Node *root) {
    Program *prog = malloc(sizeof(Program));
    if (!prog) OUT_OF_MEMORY("allocating Program");
    prog->root   = root;
    prog->global = env_make(NULL);
    return prog;
}

static Object *program_eval(Program *prog) {
    if (!prog || !prog->root) return none_make();
    return node_eval(prog->root, prog->global);
}

/* convenience runner: evaluates, prints any uncaught error, and releases result */
static void program_run(Program *prog) {
    Object *res = program_eval(prog);
    if (object_is_error(res)) {
        _(res, print);
        printf("\n");
    }
    object_release(res);
}

static void program_free(Program *prog) {
    if (!prog) return;
    node_free(prog->root);
    env_free(prog->global);
    free(prog);
}

static void program_dump_dot(const Program *prog, const char *filename) {
    FILE *out = fopen(filename, "w");
    if (!out) {
        fprintf(stderr, "Error: could not write DOT file '%s'\n", filename);
        return;
    }

    fprintf(out, "digraph program {\n");
    fprintf(out, "  ordering=out;\n");
    fprintf(out, "  node [fontname=\"Courier\", style=filled];\n");
    node_write_dot(prog->root, out);
    fprintf(out, "}\n");
    fclose(out);
}

/*==============================================================================
==================                Tests           ==============================
==============================================================================*/

/* Program Builder DSL Macros (for manual testing before Parser exists) */
#define NUM(v)          node_new_literal(number_make(v), __LINE__)
#define STR(s)          node_new_literal(string_make((s), strlen(s)), __LINE__)
#define BOOL(b)         node_new_literal(bool_make(b), __LINE__)
#define NAME(s)         node_new_name((s), strlen(s), __LINE__)
#define ADD(l, r)       node_new_binary_op(TOKEN_PLUS,  (l), (r), __LINE__)
#define SUB(l, r)       node_new_binary_op(TOKEN_MINUS, (l), (r), __LINE__)
#define MUL(l, r)       node_new_binary_op(TOKEN_STAR,  (l), (r), __LINE__)
#define DIV(l, r)       node_new_binary_op(TOKEN_SLASH, (l), (r), __LINE__)
#define ASSIGN(l, r)    node_new_assign((l), (r), false, __LINE__)
#define CONST(l, r)     node_new_assign((l), (r), true,  __LINE__)
#define DUMP(e)         node_new_dump((e), __LINE__)
#define NEG(x)          node_new_unary(TOKEN_MINUS, (x), __LINE__)
#define NOT(x)          node_new_unary(TOKEN_BANG,  (x), __LINE__)
#define EQ(a, b)        node_new_binary_op(TOKEN_EQUAL_EQUAL,   (a), (b), __LINE__)
#define NEQ(a, b)       node_new_binary_op(TOKEN_BANG_EQUAL,    (a), (b), __LINE__)
#define LT(a, b)        node_new_binary_op(TOKEN_LESS,          (a), (b), __LINE__)
#define LTE(a, b)       node_new_binary_op(TOKEN_LESS_EQUAL,    (a), (b), __LINE__)
#define GT(a, b)        node_new_binary_op(TOKEN_GREATER,       (a), (b), __LINE__)
#define GTE(a, b)       node_new_binary_op(TOKEN_GREATER_EQUAL, (a), (b), __LINE__)

#define BLOCK(...) \
    block_add_list(true, __LINE__, \
        (Node *[]){ __VA_ARGS__ }, \
        sizeof((Node *[]){ __VA_ARGS__ }) / sizeof(Node *))
        
#define PROGRAM(...) \
    program_make(block_add_list(false, __LINE__, \
        (Node *[]){ __VA_ARGS__ }, \
        sizeof((Node *[]){ __VA_ARGS__ }) / sizeof(Node *)))

static char *test_read_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "Error: could not open '%s'\n", path);
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }

    long size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);

    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(fp);
        OUT_OF_MEMORY("reading source file");
    }

    size_t read = fread(buf, 1, (size_t)size, fp);
    buf[read] = '\0';
    fclose(fp);
    return buf;
}

static void test_eval_and_dot(void) {
    
    // the "program"
    Program* prog = 
        PROGRAM(
            ASSIGN(NAME("x"), NUM(10.0)),
            DUMP(NAME("x")),
            ASSIGN(NAME("res"), BLOCK(
                ASSIGN(NAME("y"), MUL(NAME("x"), NUM(5.0))),
                ASSIGN(NAME("x"), NUM(99.0)),
                DUMP(NAME("x")),
                NAME("y")
            )),
            DUMP(NAME("res")),
            DUMP(NAME("x")),
            DUMP(NAME("y")), // TEST: out of scope
            
            DUMP(ADD(STR("Hello"), STR(" World!"))),
            
            DUMP(DIV(NUM(36), NUM(0))), // TEST: division by zero
            
            DUMP(ASSIGN(NAME("_flag12_t"), BOOL(true))),
            
            CONST(NAME("pi"), NUM(3.14159265)),
            ASSIGN(NAME("pi"), NUM(3)), // TEST: reassigning
            DUMP(NAME("pi")),
            
            ADD(STR("bar"), NUM(7)),
    
            /* Unary ops tests */
            DUMP(NEG(NAME("x"))),               // -10
            DUMP(NOT(NAME("_flag12_t"))),       // False
            DUMP(NOT(NUM(0))),                  // True (0 is falsy)
            DUMP(NEG(STR("nope"))),             // TEST: unsupported unary op
    
            /* Comparison ops tests */
            DUMP(EQ(NAME("x"), NUM(10.0))),     // True
            DUMP(NEQ(NAME("x"), NUM(10.0))),    // False
            DUMP(LT(NUM(3), NUM(5))),           // True
            DUMP(GTE(NAME("x"), NUM(10.0))),    // True
            DUMP(EQ(STR("hi"), STR("hi"))),     // True
            DUMP(LT(STR("apple"), STR("banana"))), // True
            DUMP(LT(STR("hi"), NUM(5))),         // TEST: unsupported comparison
        );
    
    program_dump_dot(prog, "program.dot");
    program_run(prog);
    program_free(prog);
}

int main(int argc, char **argv) {
    test_eval_and_dot();
    return EXIT_SUCCESS;
}