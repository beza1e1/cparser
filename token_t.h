/*
 * This file is part of cparser.
 * Copyright (C) 2007-2009 Matthias Braun <matze@braunis.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */
#ifndef TOKEN_T_H
#define TOKEN_T_H

#include <stdio.h>
#include "string_rep.h"
#include "symbol.h"
#include "symbol_table.h"
#include "type.h"

typedef enum token_type_t {
	T_ERROR = -1,
	T_NULL  =  0,
	T_EOF   = '\x04', // EOT
#define T(mode,x,str,val) T_##x val,
#define TS(x,str,val) T_##x val,
#include "tokens.inc"
#undef TS
#undef T
	T_LAST_TOKEN
} token_type_t;

typedef enum preprocessor_token_type_t {
	TP_NULL  = T_NULL,
	TP_EOF   = T_EOF,
	TP_ERROR = T_ERROR,
#define T(mode,x,str,val) TP_##x val,
#define TS(x,str,val) TP_##x val,
#include "tokens_preprocessor.inc"
#undef TS
#undef T
	TP_LAST_TOKEN
} preprocessor_token_type_t;

typedef struct source_position_t source_position_t;
struct source_position_t {
	const char *input_name;
	unsigned    linenr;
};

/* position used for "builtin" declarations/types */
extern const source_position_t builtin_source_position;

typedef struct {
	int                type;
	symbol_t          *symbol;  /**< contains identifier. Contains number suffix for numbers */
	string_t           literal; /**< string value/literal value */
	source_position_t  source_position;
} token_t;

void init_tokens(void);
void exit_tokens(void);
void print_token_type(FILE *out, token_type_t token_type);
void print_token(FILE *out, const token_t *token);

symbol_t *get_token_symbol(const token_t *token);

void print_pp_token_type(FILE *out, int type);
void print_pp_token(FILE *out, const token_t *token);

#endif
