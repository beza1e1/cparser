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

#include <libfirm/firm.h>
#include <string.h>

#include "entity_t.h"
#include "type_t.h"
#include "symbol_t.h"
#include "mangle.h"
#include "diagnostic.h"
#include "ast2firm.h"
#include "lang_features.h"
#include "adt/error.h"

static struct obstack obst;

static void mangle_type(type_t *type);

static char get_atomic_type_mangle(atomic_type_kind_t kind)
{
	switch (kind) {
	case ATOMIC_TYPE_INVALID: break;
	case ATOMIC_TYPE_VOID:        return 'v';
	case ATOMIC_TYPE_WCHAR_T:     return 'w';
	case ATOMIC_TYPE_BOOL:        return 'b';
	case ATOMIC_TYPE_CHAR:        return 'c';
	case ATOMIC_TYPE_SCHAR:       return 'a';
	case ATOMIC_TYPE_UCHAR:       return 'h';
	case ATOMIC_TYPE_INT:         return 'i';
	case ATOMIC_TYPE_UINT:        return 'j';
	case ATOMIC_TYPE_SHORT:       return 's';
	case ATOMIC_TYPE_USHORT:      return 't';
	case ATOMIC_TYPE_LONG:        return 'l';
	case ATOMIC_TYPE_ULONG:       return 'm';
	case ATOMIC_TYPE_LONGLONG:    return 'x';
	case ATOMIC_TYPE_ULONGLONG:   return 'y';
	case ATOMIC_TYPE_LONG_DOUBLE: return 'e';
	case ATOMIC_TYPE_FLOAT:       return 'f';
	case ATOMIC_TYPE_DOUBLE:      return 'd';
	}
	panic("invalid atomic type in mangler");
}

static void mangle_atomic_type(const atomic_type_t *type)
{
	obstack_1grow(&obst, get_atomic_type_mangle(type->akind));
}

static void mangle_pointer_type(const pointer_type_t *type)
{
	obstack_1grow(&obst, 'P');
	mangle_type(type->points_to);
}

static void mangle_reference_type(const reference_type_t *type)
{
	obstack_1grow(&obst, 'R');
	mangle_type(type->refers_to);
}

static void mangle_parameters(const function_type_t *type)
{
	if (type->unspecified_parameters)
		panic("can't mangle unspecified parameter types");
	if (type->kr_style_parameters)
		panic("can't mangle kr_style_parameters type");

	const function_parameter_t *parameter = type->parameters;
	if (parameter != NULL) {
		for ( ; parameter != NULL; parameter = parameter->next) {
			mangle_type(parameter->type);
		}
		if (type->variadic) {
			obstack_1grow(&obst, 'z');
		}
	} else {
		obstack_1grow(&obst, 'v');
	}
}

static void mangle_function_type(const function_type_t *type)
{
	obstack_1grow(&obst, 'F');
	if (type->linkage == LINKAGE_C) {
		obstack_1grow(&obst, 'Y');
	}

	mangle_type(type->return_type);
	mangle_parameters(type);

	obstack_1grow(&obst, 'E');
}

static void print_name(const char* name)
{
	obstack_printf(&obst, "%u%s", (unsigned)strlen(name), name);
}

static void mangle_class_type(const compound_type_t *type)
{
	const symbol_t *sym = type->compound->base.symbol;
	if (sym == NULL) {
		if (type->compound->alias == NULL)
			panic("mangling anonymous type");
		sym = type->compound->alias->base.symbol;
	}
	print_name(sym->string);
}

static void mangle_enum_type(const enum_type_t *type)
{
	const symbol_t *sym = type->enume->base.symbol;
	if (sym == NULL) {
		if (type->enume->alias == NULL)
			panic("mangling anonymous type");
		sym = type->enume->alias->base.symbol;
	}
	print_name(sym->string);
}

static void mangle_array_type(const array_type_t *type)
{
	if (type->is_vla) {
		obstack_1grow(&obst, 'A');
		obstack_1grow(&obst, '_');
	} else if (type->size_constant) {
		obstack_printf(&obst, "A%u_", (unsigned) type->size);
	} else {
		panic("mangling of non-constant sized array types not implemented yet");
	}
	mangle_type(type->element_type);
}

static void mangle_complex_type(const complex_type_t *type)
{
	obstack_1grow(&obst, 'C');
	obstack_1grow(&obst, get_atomic_type_mangle(type->akind));
}

static void mangle_imaginary_type(const imaginary_type_t *type)
{
	obstack_1grow(&obst, 'G');
	obstack_1grow(&obst, get_atomic_type_mangle(type->akind));
}

static void mangle_qualifiers(type_qualifiers_t qualifiers)
{
#if 0 /* Do not mangle restrict qualifiers.  GCC doesn't either */
	if (qualifiers & TYPE_QUALIFIER_RESTRICT)
		obstack_1grow(&obst, 'r');
#endif
	if (qualifiers & TYPE_QUALIFIER_VOLATILE)
		obstack_1grow(&obst, 'V');
	if (qualifiers & TYPE_QUALIFIER_CONST)
		obstack_1grow(&obst, 'K');

	/* handle MS extended qualifiers? */
}

static void mangle_type(type_t *orig_type)
{
	type_t *type = skip_typeref(orig_type);

	mangle_qualifiers(type->base.qualifiers);

	switch (type->kind) {
	case TYPE_ATOMIC:
		mangle_atomic_type(&type->atomic);
		return;
	case TYPE_POINTER:
		mangle_pointer_type(&type->pointer);
		return;
	case TYPE_REFERENCE:
		mangle_reference_type(&type->reference);
		return;
	case TYPE_FUNCTION:
		mangle_function_type(&type->function);
		return;
	case TYPE_COMPOUND_STRUCT:
	case TYPE_COMPOUND_UNION:
		mangle_class_type(&type->compound);
		return;
	case TYPE_ENUM:
		mangle_enum_type(&type->enumt);
		return;
	case TYPE_ARRAY:
		mangle_array_type(&type->array);
		return;
	case TYPE_COMPLEX:
		mangle_complex_type(&type->complex);
		return;
	case TYPE_IMAGINARY:
		mangle_imaginary_type(&type->imaginary);
		return;
	case TYPE_INVALID:
		panic("invalid type encountered while mangling");
	case TYPE_ERROR:
		panic("error type encountered while mangling");
	case TYPE_BUILTIN:
	case TYPE_TYPEDEF:
	case TYPE_TYPEOF:
		panic("typeref not resolved while manging?!?");

	case TYPE_BITFIELD:
		panic("no mangling for this type implemented yet");
	}
	panic("invalid type encountered while mangling");
}

static void mangle_namespace(entity_t *entity)
{
	for (entity_t *e = entity->base.parent_entity; e != NULL;
	     e = e->base.parent_entity) {
	    /* TODO: we need something similar (or the same?) for classes */
		if (e->kind == ENTITY_NAMESPACE) {
			mangle_namespace(e);
			print_name(e->base.symbol->string);
			return;
		}
	}
}

static void mangle_entity(entity_t *entity)
{
	obstack_1grow(&obst, '_');
	obstack_1grow(&obst, 'Z');

	if (entity->base.parent_entity != NULL) {
		obstack_1grow(&obst, 'N');
		mangle_namespace(entity);
	}

	print_name(entity->base.symbol->string);

	if (entity->kind == ENTITY_FUNCTION) {
		mangle_parameters(&entity->declaration.type->function);
	}
}

static ident *make_id_from_obst(void)
{
	size_t  size = obstack_object_size(&obst);
	char   *str  = obstack_finish(&obst);
	ident  *id   = new_id_from_chars(str, size);
	obstack_free(&obst, str);
	return id;
}

/**
 * Mangles an entity linker (ld) name for win32 usage.
 *
 * @param ent          the entity to be mangled
 * @param declaration  the declaration
 */
ident *create_name_win32(entity_t *entity)
{
	struct obstack *o = &obst;

	assert(is_declaration(entity));

	if (entity->kind == ENTITY_FUNCTION) {
		type_t *type = skip_typeref(entity->declaration.type);
		assert(is_type_function(type));

		if (entity->declaration.modifiers & DM_DLLIMPORT)
			/* add prefix for imported symbols */
			obstack_printf(o, "__imp_");

		cc_kind_t cc = type->function.calling_convention;

		/* calling convention prefix */
		switch (cc) {
		case CC_DEFAULT:
		case CC_CDECL:
		case CC_STDCALL:  obstack_1grow(o, '_'); break;
		case CC_FASTCALL: obstack_1grow(o, '@'); break;
		default:          panic("unhandled calling convention");
		}

		switch (type->function.linkage) {
		case LINKAGE_INVALID:
			panic("linkage type of function is invalid");

		case LINKAGE_C:
			obstack_printf(o, "%s", entity->base.symbol->string);
			break;

		case LINKAGE_CXX:
			mangle_entity(entity);
			break;
		}

		/* calling convention suffix */
		switch (cc) {
		case CC_DEFAULT:
		case CC_CDECL:
			break;

		case CC_STDCALL:
		case CC_FASTCALL: {
			ir_type  *irtype = get_ir_type(entity->declaration.type);
			unsigned size    = 0;
			for (int i = get_method_n_params(irtype) - 1; i >= 0; --i) {
				size += get_type_size_bytes(get_method_param_type(irtype, i));
			}
			obstack_printf(o, "@%u", size);
			break;
		}

		default:
			panic("unhandled calling convention");
		}
	} else {
		obstack_printf(o, "_%s", entity->base.symbol->string);
	}

	return make_id_from_obst();
}

/**
 * Mangles an entity linker (ld) name for Linux ELF usage.
 *
 * @param ent          the entity to be mangled
 * @param declaration  the declaration
 */
ident *create_name_linux_elf(entity_t *entity)
{
	const char *name = entity->base.symbol->string;

	if (entity->kind == ENTITY_FUNCTION) {
		type_t *type = skip_typeref(entity->declaration.type);
		assert(is_type_function(type));
		switch (type->function.linkage) {
			case LINKAGE_INVALID:
				panic("linkage type of function is invalid");
			case LINKAGE_C:
				if (entity->function.actual_name != NULL)
					name = entity->function.actual_name->string;
				break;
			case LINKAGE_CXX:
				// TODO What about __REDIRECT/actual_name with mangling?
				mangle_entity(entity);
				return make_id_from_obst();
		}
	}

	return new_id_from_str(name);
}

/**
 * Mangles an entity linker (ld) name for Mach-O usage.
 *
 * @param ent          the entity to be mangled
 * @param declaration  the declaration
 */
ident *create_name_macho(entity_t *entity)
{
	if (entity->kind == ENTITY_FUNCTION) {
		type_t *type = skip_typeref(entity->declaration.type);
		assert(is_type_function(type));
		if (type->function.linkage == LINKAGE_INVALID)
			panic("linkage type of function is invalid");
	}

	obstack_printf(&obst, "_%s", entity->base.symbol->string);
	return make_id_from_obst();
}

void init_mangle(void)
{
	obstack_init(&obst);
}

void exit_mangle(void)
{
	obstack_free(&obst, NULL);
}
