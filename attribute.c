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
#include <config.h>

#include <assert.h>
#include "diagnostic.h"
#include "warning.h"
#include "attribute_t.h"
#include "symbol_t.h"
#include "adt/error.h"
#include "type_t.h"

static const char *const attribute_names[ATTRIBUTE_LAST+1] = {
	[ATTRIBUTE_GNU_CONST]                  = "const",
	[ATTRIBUTE_GNU_VOLATILE]               = "volatile",
	[ATTRIBUTE_GNU_CDECL]                  = "cdecl",
	[ATTRIBUTE_GNU_STDCALL]                = "stdcall",
	[ATTRIBUTE_GNU_FASTCALL]               = "fastcall",
	[ATTRIBUTE_GNU_DEPRECATED]             = "deprecated",
	[ATTRIBUTE_GNU_NOINLINE]               = "noinline",
	[ATTRIBUTE_GNU_RETURNS_TWICE]          = "returns_twice",
	[ATTRIBUTE_GNU_NORETURN]               = "noreturn",
	[ATTRIBUTE_GNU_NAKED]                  = "naked",
	[ATTRIBUTE_GNU_PURE]                   = "pure",
	[ATTRIBUTE_GNU_ALWAYS_INLINE]          = "always_inline",
	[ATTRIBUTE_GNU_MALLOC]                 = "malloc",
	[ATTRIBUTE_GNU_WEAK]                   = "weak",
	[ATTRIBUTE_GNU_CONSTRUCTOR]            = "constructor",
	[ATTRIBUTE_GNU_DESTRUCTOR]             = "destructor",
	[ATTRIBUTE_GNU_NOTHROW]                = "nothrow",
	[ATTRIBUTE_GNU_TRANSPARENT_UNION]      = "transparent_union",
	[ATTRIBUTE_GNU_COMMON]                 = "common",
	[ATTRIBUTE_GNU_NOCOMMON]               = "nocommon",
	[ATTRIBUTE_GNU_PACKED]                 = "packed",
	[ATTRIBUTE_GNU_SHARED]                 = "shared",
	[ATTRIBUTE_GNU_NOTSHARED]              = "notshared",
	[ATTRIBUTE_GNU_USED]                   = "used",
	[ATTRIBUTE_GNU_UNUSED]                 = "unused",
	[ATTRIBUTE_GNU_NO_INSTRUMENT_FUNCTION] = "no_instrument_function",
	[ATTRIBUTE_GNU_WARN_UNUSED_RESULT]     = "warn_unused_result",
	[ATTRIBUTE_GNU_LONGCALL]               = "longcall",
	[ATTRIBUTE_GNU_SHORTCALL]              = "shortcall",
	[ATTRIBUTE_GNU_LONG_CALL]              = "long_call",
	[ATTRIBUTE_GNU_SHORT_CALL]             = "short_call",
	[ATTRIBUTE_GNU_FUNCTION_VECTOR]        = "function_vector",
	[ATTRIBUTE_GNU_INTERRUPT]              = "interrupt",
	[ATTRIBUTE_GNU_INTERRUPT_HANDLER]      = "interrupt_handler",
	[ATTRIBUTE_GNU_NMI_HANDLER]            = "nmi_handler",
	[ATTRIBUTE_GNU_NESTING]                = "nesting",
	[ATTRIBUTE_GNU_NEAR]                   = "near",
	[ATTRIBUTE_GNU_FAR]                    = "far",
	[ATTRIBUTE_GNU_SIGNAL]                 = "signal",
	[ATTRIBUTE_GNU_EIGTHBIT_DATA]          = "eightbit_data",
	[ATTRIBUTE_GNU_TINY_DATA]              = "tiny_data",
	[ATTRIBUTE_GNU_SAVEALL]                = "saveall",
	[ATTRIBUTE_GNU_FLATTEN]                = "flatten",
	[ATTRIBUTE_GNU_SSEREGPARM]             = "sseregparm",
	[ATTRIBUTE_GNU_EXTERNALLY_VISIBLE]     = "externally_visible",
	[ATTRIBUTE_GNU_MAY_ALIAS]              = "may_alias",
	[ATTRIBUTE_GNU_MS_STRUCT]              = "ms_struct",
	[ATTRIBUTE_GNU_GCC_STRUCT]             = "gcc_struct",
	[ATTRIBUTE_GNU_DLLIMPORT]              = "dllimport",
	[ATTRIBUTE_GNU_DLLEXPORT]              = "dllexport",
	[ATTRIBUTE_GNU_ALIGNED]                = "aligned",
	[ATTRIBUTE_GNU_ALIAS]                  = "alias",
	[ATTRIBUTE_GNU_SECTION]                = "section",
	[ATTRIBUTE_GNU_FORMAT]                 = "format",
	[ATTRIBUTE_GNU_FORMAT_ARG]             = "format_arg",
	[ATTRIBUTE_GNU_WEAKREF]                = "weakref",
	[ATTRIBUTE_GNU_NONNULL]                = "nonnull",
	[ATTRIBUTE_GNU_TLS_MODEL]              = "tls_model",
	[ATTRIBUTE_GNU_VISIBILITY]             = "visibility",
	[ATTRIBUTE_GNU_REGPARM]                = "regparm",
	[ATTRIBUTE_GNU_MODE]                   = "mode",
	[ATTRIBUTE_GNU_MODEL]                  = "model",
	[ATTRIBUTE_GNU_TRAP_EXIT]              = "trap_exit",
	[ATTRIBUTE_GNU_SP_SWITCH]              = "sp_switch",
	[ATTRIBUTE_GNU_SENTINEL]               = "sentinel",

	[ATTRIBUTE_MS_ALIGN]                   = "align",
	[ATTRIBUTE_MS_ALLOCATE]                = "allocate",
	[ATTRIBUTE_MS_DLLIMPORT]               = "dllimport",
	[ATTRIBUTE_MS_DLLEXPORT]               = "dllexport",
	[ATTRIBUTE_MS_NAKED]                   = "naked",
	[ATTRIBUTE_MS_NOINLINE]                = "noinline",
	[ATTRIBUTE_MS_RETURNS_TWICE]           = "returns_twice",
	[ATTRIBUTE_MS_NORETURN]                = "noreturn",
	[ATTRIBUTE_MS_NOTHROW]                 = "nothrow",
	[ATTRIBUTE_MS_NOVTABLE]                = "novtable",
	[ATTRIBUTE_MS_PROPERTY]                = "property",
	[ATTRIBUTE_MS_SELECTANY]               = "selectany",
	[ATTRIBUTE_MS_THREAD]                  = "thread",
	[ATTRIBUTE_MS_UUID]                    = "uuid",
	[ATTRIBUTE_MS_DEPRECATED]              = "deprecated",
	[ATTRIBUTE_MS_RESTRICT]                = "restrict",
	[ATTRIBUTE_MS_NOALIAS]                 = "noalias",
};

const char *get_attribute_name(attribute_kind_t kind)
{
	assert(kind <= ATTRIBUTE_LAST);
	return attribute_names[kind];
}

/**
 * compare two string, ignoring double underscores on the second.
 */
static int strcmp_underscore(const char *s1, const char *s2)
{
	if (s2[0] == '_' && s2[1] == '_') {
		size_t len2 = strlen(s2);
		size_t len1 = strlen(s1);
		if (len1 == len2-4 && s2[len2-2] == '_' && s2[len2-1] == '_') {
			return strncmp(s1, s2+2, len2-4);
		}
	}

	return strcmp(s1, s2);
}

type_t *handle_attribute_mode(const attribute_t *attribute, type_t *orig_type)
{
	type_t *type = skip_typeref(orig_type);

	/* at least: byte, word, pointer, list of machine modes
	 * __XXX___ is interpreted as XXX */

	/* This isn't really correct, the backend should provide a list of machine
	 * specific modes (according to gcc philosophy that is...) */
	attribute_argument_t *arg        = attribute->a.arguments;
	const char           *symbol_str = arg->v.symbol->string;
	bool                  sign       = is_type_signed(type);
	atomic_type_kind_t  akind;
	if (strcmp_underscore("QI",   symbol_str) == 0 ||
	    strcmp_underscore("byte", symbol_str) == 0) {
		akind = sign ? ATOMIC_TYPE_CHAR : ATOMIC_TYPE_UCHAR;
	} else if (strcmp_underscore("HI", symbol_str) == 0) {
		akind = sign ? ATOMIC_TYPE_SHORT : ATOMIC_TYPE_USHORT;
	} else if (strcmp_underscore("SI",      symbol_str) == 0
	        || strcmp_underscore("word",    symbol_str) == 0
	        || strcmp_underscore("pointer", symbol_str) == 0) {
		akind = sign ? ATOMIC_TYPE_INT : ATOMIC_TYPE_UINT;
	} else if (strcmp_underscore("DI", symbol_str) == 0) {
		akind = sign ? ATOMIC_TYPE_LONGLONG : ATOMIC_TYPE_ULONGLONG;
	} else {
		if (warning.other)
			warningf(&attribute->source_position, "ignoring unknown mode '%s'",
			         symbol_str);
		return orig_type;
	}

	if (type->kind == TYPE_ATOMIC) {
		type_t *copy       = duplicate_type(type);
		copy->atomic.akind = akind;
		return identify_new_type(copy);
	} else if (type->kind == TYPE_ENUM) {
		type_t *copy      = duplicate_type(type);
		copy->enumt.akind = akind;
		return identify_new_type(copy);
	} else if (is_type_pointer(type)) {
		warningf(&attribute->source_position,
		         "__attribute__((mode)) on pointers not implemented yet (ignored)");
		return type;
	}

	errorf(&attribute->source_position,
	       "__attribute__((mode)) only allowed on integer, enum or pointer type");
	return orig_type;
}

static inline bool is_po2(unsigned x)
{
	return (x & (x-1)) == 0;
}

static void handle_attribute_aligned(const attribute_t *attribute,
                                     entity_t *entity)
{
	int alignment = 32; /* TODO: fill in maximum useful alignment for
						   target machine */
	if (attribute->a.arguments) {
		attribute_argument_t *argument = attribute->a.arguments;
		alignment = fold_constant_to_int(argument->v.expression);
	}

	if (!is_po2(alignment)) {
		errorf(&attribute->source_position,
			   "alignment must be a power of 2 but is %d\n",
			   alignment);
		return;
	}
	if (alignment <= 0) {
		errorf(&attribute->source_position,
			   "alignment must be bigger than 0 but is %d\n",
			   alignment);
		return;
	}

	switch (entity->kind) {
	DECLARATION_KIND_CASES
		entity->declaration.alignment = alignment;
	case ENTITY_TYPEDEF:
		entity->typedefe.alignment = alignment;
		break;
	case ENTITY_STRUCT:
	case ENTITY_UNION:
		if (alignment > entity->compound.alignment) {
			entity->compound.alignment = alignment;
		}
		break;
	default:
		if (warning.other) {
			warningf(&attribute->source_position,
					 "alignment attribute specification on '%S' ignored",
					 entity->base.symbol);
		}
		break;
	}
}

static void warn_arguments(const attribute_t *attribute)
{
	if (attribute->a.arguments == NULL)
		return;

	if (warning.other) {
		warningf(&attribute->source_position,
				 "attribute '%s' needs no attributes",
				 get_attribute_name(attribute->kind));
	}
}

static void handle_attribute_packed_e(const attribute_t *attribute,
                                      entity_t *entity)
{
#if 0
	if (entity->kind != ENTITY_STRUCT) {
		warningf(&attribute->source_position,
		         "packed attribute on %s '%s' ignored",
				 get_entity_kind_name(entity->kind),
				 entity->base.symbol->string);
		return;
	}
#endif

	warn_arguments(attribute);
	entity->compound.packed = true;
}

static void handle_attribute_packed(const attribute_t *attribute, type_t *type)
{
	if (type->kind != TYPE_COMPOUND_STRUCT) {
		warningf(&attribute->source_position,
		         "packed attribute on type '%T' ignored", type);
		return;
	}

	handle_attribute_packed_e(attribute, (entity_t*) type->compound.compound);
}

void handle_entity_attributes(const attribute_t *attributes, entity_t *entity)
{
	if (entity->kind == ENTITY_TYPEDEF) {
		type_t *type = entity->typedefe.type;
		type = handle_type_attributes(attributes, type);
		entity->typedefe.type = type;
	} else if (is_declaration(entity)) {
		type_t *type = entity->declaration.type;
		type = handle_type_attributes(attributes, type);
		entity->declaration.type = type;
	}

	decl_modifiers_t modifiers = 0;
	const attribute_t *attribute = attributes;
	for ( ; attribute != NULL; attribute = attribute->next) {
		switch(attribute->kind) {
		case ATTRIBUTE_GNU_CONST:         modifiers |= DM_CONST; break;
		case ATTRIBUTE_GNU_DEPRECATED:    modifiers |= DM_DEPRECATED; break;
		case ATTRIBUTE_GNU_NOINLINE:      modifiers |= DM_NOINLINE; break;
		case ATTRIBUTE_GNU_RETURNS_TWICE: modifiers |= DM_RETURNS_TWICE; break;
		case ATTRIBUTE_GNU_NORETURN:      modifiers |= DM_NORETURN; break;
		case ATTRIBUTE_GNU_NAKED:         modifiers |= DM_NAKED; break;
		case ATTRIBUTE_GNU_PURE:          modifiers |= DM_PURE; break;
		case ATTRIBUTE_GNU_ALWAYS_INLINE: modifiers |= DM_FORCEINLINE; break;
		case ATTRIBUTE_GNU_MALLOC:        modifiers |= DM_MALLOC; break;
		case ATTRIBUTE_GNU_CONSTRUCTOR:   modifiers |= DM_CONSTRUCTOR; break;
		case ATTRIBUTE_GNU_DESTRUCTOR:    modifiers |= DM_DESTRUCTOR; break;
		case ATTRIBUTE_GNU_NOTHROW:       modifiers |= DM_NOTHROW; break;
		case ATTRIBUTE_GNU_TRANSPARENT_UNION:
										  modifiers |= DM_TRANSPARENT_UNION;
										  break;
		case ATTRIBUTE_GNU_USED:          modifiers |= DM_USED; break;
		case ATTRIBUTE_GNU_UNUSED:        modifiers |= DM_UNUSED; break;
		case ATTRIBUTE_GNU_DLLIMPORT:     modifiers |= DM_DLLIMPORT; break;
		case ATTRIBUTE_GNU_DLLEXPORT:     modifiers |= DM_DLLEXPORT; break;
		case ATTRIBUTE_GNU_WEAK:          modifiers |= DM_WEAK; break;

		case ATTRIBUTE_MS_ALLOCATE:      modifiers |= DM_MALLOC; break;
		case ATTRIBUTE_MS_DLLIMPORT:     modifiers |= DM_DLLIMPORT; break;
		case ATTRIBUTE_MS_DLLEXPORT:     modifiers |= DM_DLLEXPORT; break;
		case ATTRIBUTE_MS_NAKED:         modifiers |= DM_NAKED; break;
		case ATTRIBUTE_MS_NOINLINE:      modifiers |= DM_NOINLINE; break;
		case ATTRIBUTE_MS_RETURNS_TWICE: modifiers |= DM_RETURNS_TWICE; break;
		case ATTRIBUTE_MS_NORETURN:      modifiers |= DM_NORETURN; break;
		case ATTRIBUTE_MS_NOTHROW:       modifiers |= DM_NOTHROW; break;
		case ATTRIBUTE_MS_THREAD:        modifiers |= DM_THREAD; break;
		case ATTRIBUTE_MS_DEPRECATED:    modifiers |= DM_DEPRECATED; break;
		case ATTRIBUTE_MS_RESTRICT:      modifiers |= DM_RESTRICT; break;
		case ATTRIBUTE_MS_NOALIAS:       modifiers |= DM_NOALIAS; break;

		case ATTRIBUTE_GNU_PACKED:
			handle_attribute_packed_e(attribute, entity);
			break;

		case ATTRIBUTE_MS_ALIGN:
		case ATTRIBUTE_GNU_ALIGNED:
			handle_attribute_aligned(attribute, entity);
			break;
		default: break;
		}
	}

	if (modifiers != 0) {
		switch(entity->kind) {
		case ENTITY_TYPEDEF:
			entity->typedefe.modifiers |= modifiers;
			break;
		case ENTITY_UNION:
		case ENTITY_STRUCT:
			entity->compound.modifiers |= modifiers;
			break;
		case ENTITY_COMPOUND_MEMBER:
		case ENTITY_VARIABLE:
		case ENTITY_FUNCTION:
			entity->declaration.modifiers |= modifiers;
			break;
		default:
			/* TODO: warning */
			break;
		}
	}
}

static type_t *change_calling_convention(type_t *type, cc_kind_t cconv)
{
	if (is_typeref(type) || !is_type_function(type)) {
		return type;
	}

	if (type->function.calling_convention == cconv)
		return type;

	type_t* new_type = duplicate_type(type);
	new_type->function.calling_convention = cconv;
	return identify_new_type(new_type);
}

type_t *handle_type_attributes(const attribute_t *attributes, type_t *type)
{
	const attribute_t *attribute = attributes;
	for ( ; attribute != NULL; attribute = attribute->next) {
		switch(attribute->kind) {
		case ATTRIBUTE_GNU_PACKED:
			handle_attribute_packed(attribute, type);
			break;
		case ATTRIBUTE_GNU_CDECL:
		case ATTRIBUTE_MS_CDECL:
			type = change_calling_convention(type, CC_CDECL);
			break;
		case ATTRIBUTE_MS_STDCALL:
		case ATTRIBUTE_GNU_STDCALL:
			type = change_calling_convention(type, CC_STDCALL);
			break;
		case ATTRIBUTE_MS_FASTCALL:
		case ATTRIBUTE_GNU_FASTCALL:
			type = change_calling_convention(type, CC_FASTCALL);
			break;
		case ATTRIBUTE_MS_THISCALL:
			type = change_calling_convention(type, CC_THISCALL);
			break;
		case ATTRIBUTE_GNU_MODE:
			type = handle_attribute_mode(attribute, type);
			break;
		default:
			break;
		}
	}

	return type;
}

const char *get_deprecated_string(const attribute_t *attribute)
{
	for ( ; attribute != NULL; attribute = attribute->next) {
		if (attribute->kind != ATTRIBUTE_MS_DEPRECATED)
			continue;

		attribute_argument_t *argument = attribute->a.arguments;
		if (argument == NULL)
			return NULL;
		if (argument->kind != ATTRIBUTE_ARGUMENT_EXPRESSION)
			return NULL;
		expression_t *expression = argument->v.expression;
		if (expression->kind != EXPR_STRING_LITERAL)
			return NULL;
		return expression->literal.value.begin;
	}
	return NULL;
}

static bool property_attribute_equal(const attribute_property_argument_t *prop1,
                                     const attribute_property_argument_t *prop2)
{
	return prop1->put_symbol == prop2->put_symbol
		&& prop1->get_symbol == prop2->get_symbol;
}

static bool attribute_argument_equal(const attribute_argument_t *arg1,
                                     const attribute_argument_t *arg2)
{
	if (arg1->kind != arg2->kind)
		return false;

	switch (arg1->kind) {
	case ATTRIBUTE_ARGUMENT_SYMBOL:
		return arg1->v.symbol == arg2->v.symbol;
	case ATTRIBUTE_ARGUMENT_EXPRESSION:
		/* TODO */
		return false;
	}
	panic("Unknown argument type found");
}

static bool attribute_arguments_equal(const attribute_argument_t *args1,
                                      const attribute_argument_t *args2)
{
	for ( ; args1 != NULL && args2 != NULL;
	     args1 = args1->next, args2 = args2->next) {
		if (!attribute_argument_equal(args1, args2))
			return false;
	}
	/* both should be NULL now */
	return args1 == args2;
}

bool attributes_equal(const attribute_t *attr1, const attribute_t *attr2)
{
	if (attr1->kind != attr2->kind)
		return false;

	switch (attr1->kind) {
	case ATTRIBUTE_MS_PROPERTY:
		return property_attribute_equal(attr1->a.property, attr2->a.property);
	default:
		return attribute_arguments_equal(attr1->a.arguments,
		                                 attr2->a.arguments);
	}
}
