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
#ifndef ENTITY_T_H
#define ENTITY_T_H

#include "lexer.h"
#include "symbol.h"
#include "entity.h"
#include "attribute.h"
#include <libfirm/firm_types.h>

typedef enum {
	ENTITY_INVALID,
	ENTITY_VARIABLE,
	ENTITY_COMPOUND_MEMBER,
	ENTITY_PARAMETER,
	ENTITY_FUNCTION,
	ENTITY_TYPEDEF,
	ENTITY_CLASS,
	ENTITY_STRUCT,
	ENTITY_UNION,
	ENTITY_ENUM,
	ENTITY_ENUM_VALUE,
	ENTITY_LABEL,
	ENTITY_LOCAL_LABEL,
	ENTITY_NAMESPACE
} entity_kind_tag_t;
typedef unsigned char entity_kind_t;

typedef enum namespace_tag_t {
	NAMESPACE_INVALID,
	NAMESPACE_NORMAL,
	NAMESPACE_TAG,
	NAMESPACE_LABEL
} namespace_tag_t;
typedef unsigned char entity_namespace_t;

typedef enum storage_class_tag_t {
	STORAGE_CLASS_NONE,
	STORAGE_CLASS_EXTERN,
	STORAGE_CLASS_STATIC,
	STORAGE_CLASS_TYPEDEF,
	STORAGE_CLASS_AUTO,
	STORAGE_CLASS_REGISTER,
} storage_class_tag_t;
typedef unsigned char storage_class_t;

typedef enum decl_modifier_t {
	DM_NONE              = 0,
	DM_DLLIMPORT         = 1 <<  0,
	DM_DLLEXPORT         = 1 <<  1,
	DM_THREAD            = 1 <<  2,
	DM_NAKED             = 1 <<  3,
	DM_MICROSOFT_INLINE  = 1 <<  4,
	DM_FORCEINLINE       = 1 <<  5,
	DM_SELECTANY         = 1 <<  6,
	DM_NOTHROW           = 1 <<  7,
	DM_NOVTABLE          = 1 <<  8,
	DM_NORETURN          = 1 <<  9,
	DM_NOINLINE          = 1 << 10,
	DM_RESTRICT          = 1 << 11,
	DM_NOALIAS           = 1 << 12,
	DM_TRANSPARENT_UNION = 1 << 13,
	DM_CONST             = 1 << 14,
	DM_PURE              = 1 << 15,
	DM_CONSTRUCTOR       = 1 << 16,
	DM_DESTRUCTOR        = 1 << 17,
	DM_UNUSED            = 1 << 18,
	DM_USED              = 1 << 19,
	DM_CDECL             = 1 << 20,
	DM_FASTCALL          = 1 << 21,
	DM_STDCALL           = 1 << 22,
	DM_THISCALL          = 1 << 23,
	DM_DEPRECATED        = 1 << 24,
	DM_RETURNS_TWICE     = 1 << 25,
	DM_MALLOC            = 1 << 26,
} decl_modifier_t;

/**
 * A scope containing entities.
 */
struct scope_t {
	entity_t *entities;
	entity_t *last_entity; /**< pointer to last entity (so appending is fast) */
	unsigned  depth;       /**< while parsing, the depth of this scope in the
	                            scope stack. */
};

/**
 * a named entity is something which can be referenced by its name
 * (a symbol)
 */
struct entity_base_t {
	entity_kind_t       kind;
	entity_namespace_t  namespc;
	symbol_t           *symbol;
	source_position_t   source_position;
	scope_t            *parent_scope;    /**< The scope where this entity
										      is contained in */

	/** next declaration in a scope */
	entity_t           *next;
	/** next declaration with same symbol */
	entity_t           *symbol_next;
};

struct compound_t {
	entity_base_t     base;
	entity_t         *alias; /* used for name mangling of anonymous types */
	scope_t           members;
	decl_modifiers_t  modifiers;
	bool              layouted          : 1;
	bool              complete          : 1;
	bool              transparent_union : 1;
	bool              packed            : 1;

	il_alignment_t    alignment;
	il_size_t         size;

	/* ast2firm info */
	ir_type          *irtype;
	bool              irtype_complete : 1;
};

struct enum_t {
	entity_base_t  base;
	entity_t      *alias; /* used for name mangling of anonymous types */
	bool           complete : 1;

	/* ast2firm info */
	ir_type       *irtype;
};

struct enum_value_t {
	entity_base_t  base;
	expression_t  *value;
	type_t        *enum_type;

	/* ast2firm info */
	tarval        *tv;
};

struct label_t {
	entity_base_t  base;
	bool           used : 1;
	bool           address_taken : 1;
	statement_t   *statement;

	/* ast2firm info */
	ir_node       *block;
};

struct namespace_t {
	entity_base_t  base;
	scope_t        members;
};

struct typedef_t {
	entity_base_t     base;
	decl_modifiers_t  modifiers;
	type_t           *type;
	il_alignment_t    alignment;
	bool              builtin : 1;
};

struct declaration_t {
	entity_base_t     base;
	type_t           *type;
	storage_class_t   declared_storage_class;
	storage_class_t   storage_class;
	decl_modifiers_t  modifiers;
	il_alignment_t    alignment;
	attribute_t      *attributes;
	bool              used     : 1;  /**< Set if the declaration is used. */
	bool              implicit : 1;  /**< Set for implicit (not found in source code) declarations. */

	/* ast2firm info */
	unsigned char     kind;
};

struct compound_member_t {
	declaration_t  base;
	bool           read          : 1;
	bool           address_taken : 1;  /**< Set if the address of this declaration was taken. */
	unsigned short offset;     /**< the offset of this member in the compound */
	unsigned char  bit_offset; /**< extra bit offset for bitfield members */

	/* ast2firm info */
	ir_entity *entity;
};

struct variable_t {
	declaration_t     base;
	bool              thread_local  : 1;  /**< GCC __thread */
	bool              restricta     : 1;
	bool              deprecated    : 1;
	bool              noalias       : 1;

	bool              address_taken : 1;  /**< Set if the address of this declaration was taken. */
	bool              read          : 1;

	initializer_t    *initializer;

	/* ast2firm info */
	union {
		unsigned int  value_number;
		ir_entity    *entity;
		ir_node      *vla_base;
	} v;
};

struct parameter_t {
	declaration_t  base;
	bool           address_taken : 1;
	bool           read          : 1;

	/* ast2firm info */
	union {
		unsigned int  value_number;
		ir_entity    *entity;
	} v;
};

/**
 * GNU builtin or MS intrinsic functions.
 */
typedef enum builtin_kind_t {
	bk_none = 0,                   /**< no builtin */
	bk_gnu_builtin_alloca,         /**< GNU __builtin_alloca */
	bk_gnu_builtin_huge_val,       /**< GNU __builtin_huge_val */
	bk_gnu_builtin_inf,            /**< GNU __builtin_inf */
	bk_gnu_builtin_inff,           /**< GNU __builtin_inff */
	bk_gnu_builtin_infl,           /**< GNU __builtin_infl */
	bk_gnu_builtin_nan,            /**< GNU __builtin_nan */
	bk_gnu_builtin_nanf,           /**< GNU __builtin_nanf */
	bk_gnu_builtin_nanl,           /**< GNU __builtin_nanl */
	bk_gnu_builtin_va_end,         /**< GNU __builtin_va_end */
	bk_gnu_builtin_expect,         /**< GNU __builtin_expect */
	bk_gnu_builtin_return_address, /**< GNU __builtin_return_address */
	bk_gnu_builtin_frame_address,  /**< GNU __builtin_frame_address */
	bk_gnu_builtin_ffs,            /**< GNU __builtin_ffs */
	bk_gnu_builtin_clz,            /**< GNU __builtin_clz */
	bk_gnu_builtin_ctz,            /**< GNU __builtin_ctz */
	bk_gnu_builtin_popcount,       /**< GNU __builtin_popcount */
	bk_gnu_builtin_parity,         /**< GNU __builtin_parity */
	bk_gnu_builtin_prefetch,       /**< GNU __builtin_prefetch */
	bk_gnu_builtin_trap,           /**< GNU __builtin_trap */

	bk_ms_rotl,                    /**< MS _rotl */
	bk_ms_rotr,                    /**< MS _rotr */
	bk_ms_rotl64,                  /**< MS _rotl64 */
	bk_ms_rotr64,                  /**< MS _rotr64 */
	bk_ms_byteswap_ushort,         /**< MS _byteswap_ushort */
	bk_ms_byteswap_ulong,          /**< MS _byteswap_ulong */
	bk_ms_byteswap_uint64,         /**< MS _byteswap_uint64 */

	bk_ms__debugbreak,             /**< MS __debugbreak */
	bk_ms_ReturnAddress,           /**< MS _ReturnAddress */
	bk_ms_AddressOfReturnAddress,  /**< MS _AddressOfReturnAddress */
	bk_ms__popcount,               /**< MS __popcount */
	bk_ms_enable,                  /**< MS _enable */
	bk_ms_disable,                 /**< MS _disable */
	bk_ms__inbyte,                 /**< MS __inbyte */
	bk_ms__inword,                 /**< MS __inword */
	bk_ms__indword,                /**< MS __indword */
	bk_ms__outbyte,                /**< MS __outbyte */
	bk_ms__outword,                /**< MS __outword */
	bk_ms__outdword,               /**< MS __outdword */
	bk_ms__ud2,                    /**< MS __ud2 */
	bk_ms_BitScanForward,          /**< MS _BitScanForward */
	bk_ms_BitScanReverse,          /**< MS _BitScanReverse */
	bk_ms_InterlockedExchange,     /**< MS _InterlockedExchange */
	bk_ms_InterlockedExchange64,   /**< MS _InterlockedExchange64 */
	bk_ms__readeflags,             /**< MS __readflags */
	bk_ms__writeeflags,            /**< MS __writeflags */
} builtin_kind_t;

struct function_t {
	declaration_t  base;
	bool           is_inline     : 1;

	bool           need_closure  : 1;  /**< Inner function needs closure. */
	bool           goto_to_outer : 1;  /**< Inner function has goto to outer function. */

	builtin_kind_t btk;
	scope_t        parameters;
	statement_t   *statement;

	/* ast2firm info */
	ir_entity     *irentity;
	ir_node       *static_link;        /**< if need_closure is set, the node representing
										    the static link. */
};

union entity_t {
	entity_kind_t      kind;
	entity_base_t      base;
	compound_t         structe;
	compound_t         unione;
	compound_t         compound;
	enum_t             enume;
	enum_value_t       enum_value;
	label_t            label;
	namespace_t        namespacee;
	typedef_t          typedefe;
	declaration_t      declaration;
	variable_t         variable;
	parameter_t        parameter;
	function_t         function;
	compound_member_t  compound_member;
};

#define DECLARATION_KIND_CASES        \
	case ENTITY_FUNCTION:             \
	case ENTITY_VARIABLE:             \
	case ENTITY_PARAMETER:            \
	case ENTITY_COMPOUND_MEMBER:

static inline bool is_declaration(const entity_t *entity)
{
	switch(entity->kind) {
	DECLARATION_KIND_CASES
		return true;
	default:
		return false;
	}
}

const char *get_entity_kind_name(entity_kind_t kind);

#endif
