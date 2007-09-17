#ifndef TYPE_H
#define TYPE_H

#include <stdio.h>
#include "symbol.h"

typedef struct type_t                   type_t;
typedef struct atomic_type_t            atomic_type_t;
typedef struct pointer_type_t           pointer_type_t;
typedef struct method_parameter_t       method_parameter_t;
typedef struct method_type_t            method_type_t;
typedef struct compound_type_t          compound_type_t;
typedef struct enum_type_t              enum_type_t;
typedef struct builtin_type_t           builtin_type_t;
typedef struct typeof_type_t            typeof_type_t;

void init_types(void);
void exit_types(void);

void print_type(const type_t *type);

/**
 * prints a human readable form of @p type. prints an abstract typename
 * if symbol is NULL
 */
void print_type_ext(const type_t *type, const symbol_t *symbol,
                    const context_t *context);

/**
 * set output stream for the type printer
 */
void type_set_output(FILE *out);

/**
 * returns 1 if type contains integer numbers
 */
int is_type_int(const type_t *type);

/**
 * returns 1 if the type is valid. A type is valid if it contains no unresolved
 * references anymore and is not of TYPE_INVALID.
 */
int type_valid(const type_t *type);

#endif
