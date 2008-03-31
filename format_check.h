/*
 * This file is part of cparser.
 * Copyright (C) 2007-2008 Matthias Braun <matze@braunis.de>
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
#ifndef FORMAT_CHECK_H
#define FORMAT_CHECK_H

#include "ast.h"

typedef enum {
	FORMAT_PRINTF,   /**< printf style format */
	FORMAT_SCANF,    /**< scanf style format */
	FORMAT_STRFTIME, /**< strftime time format */
	FORMAT_STRFMON   /**< strfmon monetary format */
} format_kind_t;

void check_format(const call_expression_t *call);

#endif
