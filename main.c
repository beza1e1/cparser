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

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

#ifdef _WIN32

#include <fcntl.h>
#include <io.h>

/* no eXecute on Win32 */
#define X_OK 0
#define W_OK 2
#define R_OK 4

#define O_RDWR          _O_RDWR
#define O_CREAT         _O_CREAT
#define O_EXCL          _O_EXCL
#define O_BINARY        _O_BINARY

/* remap some names, we are not in the POSIX world */
#define access(fname, mode)      _access(fname, mode)
#define mktemp(tmpl)             _mktemp(tmpl)
#define open(fname, oflag, mode) _open(fname, oflag, mode)
#define fdopen(fd, mode)         _fdopen(fd, mode)
#define popen(cmd, mode)         _popen(cmd, mode)
#define pclose(file)             _pclose(file)
#define unlink(filename)         _unlink(filename)

#else
#include <unistd.h>
#define HAVE_MKSTEMP
#endif

#include <libfirm/firm.h>
#include <libfirm/be.h>

#include "lexer.h"
#include "token_t.h"
#include "types.h"
#include "type_hash.h"
#include "parser.h"
#include "type_t.h"
#include "ast2firm.h"
#include "diagnostic.h"
#include "lang_features.h"
#include "driver/firm_opt.h"
#include "driver/firm_cmdline.h"
#include "driver/firm_timing.h"
#include "adt/error.h"
#include "wrappergen/write_fluffy.h"
#include "wrappergen/write_caml.h"
#include "wrappergen/write_jna.h"
#include "revision.h"
#include "warning.h"
#include "mangle.h"
#include "printer.h"

#ifndef PREPROCESSOR
#ifndef __WIN32__
#define PREPROCESSOR "gcc -E -m32 -U__STRICT_ANSI__"
#else
#define PREPROCESSOR "cpp -m32 -U__STRICT_ANSI__"
#endif
#endif

#ifndef LINKER
#define LINKER    "gcc -m32"
#endif

#ifndef ASSEMBLER
#ifdef __APPLE__
#define ASSEMBLER "gcc -m32 -c -xassembler"
#else
#define ASSEMBLER "as --32"
#endif
#endif

/** The current c mode/dialect. */
unsigned int c_mode = _C89 | _ANSI | _C99 | _GNUC;

/** The 'machine size', 16, 32 or 64 bit, 32bit is the default. */
unsigned int machine_size = 32;

/** true if the char type is signed. */
bool char_is_signed = true;

/** true for strict language checking. */
bool strict_mode = false;

/** use builtins for some libc functions */
bool use_builtins = false;

/** we have extern function with const attribute. */
bool have_const_functions = false;

atomic_type_kind_t wchar_atomic_kind = ATOMIC_TYPE_INT;

/* to switch on printing of implicit casts */
extern bool print_implicit_casts;

/* to switch on printing of parenthesis to indicate operator precedence */
extern bool print_parenthesis;

static int             verbose;
static struct obstack  cppflags_obst, ldflags_obst;
static char            dep_target[1024];
static const char     *outname;

typedef enum lang_standard_t {
	STANDARD_DEFAULT, /* gnu99 (for C, GCC does gnu89) or gnu++98 (for C++) */
	STANDARD_ANSI,    /* c89 (for C) or c++98 (for C++) */
	STANDARD_C89,     /* ISO C90 (sic) */
	STANDARD_C90,     /* ISO C90 as modified in amendment 1 */
	STANDARD_C99,     /* ISO C99 */
	STANDARD_GNU89,   /* ISO C90 plus GNU extensions (including some C99) */
	STANDARD_GNU99,   /* ISO C99 plus GNU extensions */
	STANDARD_CXX98,   /* ISO C++ 1998 plus amendments */
	STANDARD_GNUXX98  /* ISO C++ 1998 plus amendments and GNU extensions */
} lang_standard_t;

static lang_standard_t standard;

typedef struct file_list_entry_t file_list_entry_t;

typedef enum filetype_t {
	FILETYPE_AUTODETECT,
	FILETYPE_C,
	FILETYPE_PREPROCESSED_C,
	FILETYPE_CXX,
	FILETYPE_PREPROCESSED_CXX,
	FILETYPE_ASSEMBLER,
	FILETYPE_PREPROCESSED_ASSEMBLER,
	FILETYPE_OBJECT,
	FILETYPE_IR,
	FILETYPE_UNKNOWN
} filetype_t;

struct file_list_entry_t {
	const char  *name; /**< filename or NULL for stdin */
	filetype_t   type;
	file_list_entry_t *next;
};

static file_list_entry_t *temp_files;

static void initialize_firm(void)
{
	firm_early_init();
}

static void get_output_name(char *buf, size_t buflen, const char *inputname,
                            const char *newext)
{
	if (inputname == NULL)
		inputname = "a";

	char const *const last_slash = strrchr(inputname, '/');
	char const *const filename   =
		last_slash != NULL ? last_slash + 1 : inputname;
	char const *const last_dot   = strrchr(filename, '.');
	char const *const name_end   =
		last_dot != NULL ? last_dot : strchr(filename, '\0');

	int const len = snprintf(buf, buflen, "%.*s%s",
			(int)(name_end - filename), filename, newext);
#ifdef _WIN32
	if (len < 0 || buflen <= (size_t)len)
#else
	if (buflen <= (size_t)len)
#endif
		panic("filename too long");
}

#include "gen_builtins.h"

static translation_unit_t *do_parsing(FILE *const in, const char *const input_name)
{
	start_parsing();

	if (use_builtins) {
		lexer_open_buffer(builtins, sizeof(builtins)-1, "<builtin>");
		parse();
	}

	lexer_open_stream(in, input_name);
	parse();

	translation_unit_t *unit = finish_parsing();
	return unit;
}

static void lextest(FILE *in, const char *fname)
{
	lexer_open_stream(in, fname);

	do {
		lexer_next_preprocessing_token();
		print_token(stdout, &lexer_token);
		putchar('\n');
	} while (lexer_token.type != T_EOF);
}

static void add_flag(struct obstack *obst, const char *format, ...)
{
	char buf[65536];
	va_list ap;

	va_start(ap, format);
#ifdef _WIN32
	int len =
#endif
		vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);

	obstack_1grow(obst, ' ');
#ifdef _WIN32
	obstack_1grow(obst, '"');
	obstack_grow(obst, buf, len);
	obstack_1grow(obst, '"');
#else
	/* escape stuff... */
	for (char *c = buf; *c != '\0'; ++c) {
		switch(*c) {
		case ' ':
		case '"':
		case '$':
		case '&':
		case '(':
		case ')':
		case ';':
		case '<':
		case '>':
		case '\'':
		case '\\':
		case '\n':
		case '\r':
		case '\t':
		case '`':
		case '|':
			obstack_1grow(obst, '\\');
			/* FALLTHROUGH */
		default:
			obstack_1grow(obst, *c);
			break;
		}
	}
#endif
}

static const char *type_to_string(type_t *type)
{
	assert(type->kind == TYPE_ATOMIC);
	return get_atomic_kind_name(type->atomic.akind);
}

static FILE *preprocess(const char *fname, filetype_t filetype)
{
	static const char *common_flags = NULL;

	if (common_flags == NULL) {
		obstack_1grow(&cppflags_obst, '\0');
		const char *flags = obstack_finish(&cppflags_obst);

		/* setup default defines */
		add_flag(&cppflags_obst, "-U__WCHAR_TYPE__");
		add_flag(&cppflags_obst, "-D__WCHAR_TYPE__=%s", type_to_string(type_wchar_t));
		add_flag(&cppflags_obst, "-U__SIZE_TYPE__");
		add_flag(&cppflags_obst, "-D__SIZE_TYPE__=%s", type_to_string(type_size_t));

		add_flag(&cppflags_obst, "-U__VERSION__");
		add_flag(&cppflags_obst, "-D__VERSION__=\"%s\"", cparser_REVISION);

		if (flags[0] != '\0') {
			size_t len = strlen(flags);
			obstack_1grow(&cppflags_obst, ' ');
			obstack_grow(&cppflags_obst, flags, len);
		}
		obstack_1grow(&cppflags_obst, '\0');
		common_flags = obstack_finish(&cppflags_obst);
	}

	assert(obstack_object_size(&cppflags_obst) == 0);

	const char *preprocessor = getenv("CPARSER_PP");
	if (preprocessor == NULL)
		preprocessor = PREPROCESSOR;

	obstack_printf(&cppflags_obst, "%s ", preprocessor);
	switch (filetype) {
	case FILETYPE_C:
		add_flag(&cppflags_obst, "-std=c99");
		break;
	case FILETYPE_CXX:
		add_flag(&cppflags_obst, "-std=c++98");
		break;
	case FILETYPE_ASSEMBLER:
		add_flag(&cppflags_obst, "-x");
		add_flag(&cppflags_obst, "assembler-with-cpp");
		break;
	default:
		break;
	}
	obstack_printf(&cppflags_obst, "%s", common_flags);

	/* handle dependency generation */
	if (dep_target[0] != '\0') {
		add_flag(&cppflags_obst, "-MF");
		add_flag(&cppflags_obst, dep_target);
		if (outname != NULL) {
				add_flag(&cppflags_obst, "-MQ");
				add_flag(&cppflags_obst, outname);
		}
	}
	add_flag(&cppflags_obst, fname);

	obstack_1grow(&cppflags_obst, '\0');
	char *buf = obstack_finish(&cppflags_obst);
	if (verbose) {
		puts(buf);
	}

	FILE *f = popen(buf, "r");
	if (f == NULL) {
		fprintf(stderr, "invoking preprocessor failed\n");
		exit(1);
	}

	/* we don't really need that anymore */
	obstack_free(&cppflags_obst, buf);

	return f;
}

static void assemble(const char *out, const char *in)
{
	char buf[65536];

	const char *assembler = getenv("CPARSER_AS");
	if (assembler == NULL)
		assembler = ASSEMBLER;

	snprintf(buf, sizeof(buf), "%s %s -o %s", assembler, in, out);
	if (verbose) {
		puts(buf);
	}

	int err = system(buf);
	if (err != 0) {
		fprintf(stderr, "assembler reported an error\n");
		exit(1);
	}
}

static void print_file_name(const char *file)
{
	add_flag(&ldflags_obst, "-print-file-name=%s", file);

	obstack_1grow(&ldflags_obst, '\0');
	const char *flags = obstack_finish(&ldflags_obst);

	/* construct commandline */
	const char *linker = getenv("CPARSER_LINK");
	if (linker == NULL)
		linker = LINKER;
	obstack_printf(&ldflags_obst, "%s ", linker);
	obstack_printf(&ldflags_obst, "%s", flags);
	obstack_1grow(&ldflags_obst, '\0');

	char *commandline = obstack_finish(&ldflags_obst);

	if (verbose) {
		puts(commandline);
	}
	int err = system(commandline);
	if (err != EXIT_SUCCESS) {
		fprintf(stderr, "linker reported an error\n");
		exit(1);
	}
}

static const char *try_dir(const char *dir)
{
	if (dir == NULL)
		return dir;
	if (access(dir, R_OK | W_OK | X_OK) == 0)
		return dir;
	return NULL;
}

static const char *get_tempdir(void)
{
	static const char *tmpdir = NULL;

	if (tmpdir != NULL)
		return tmpdir;

	if (tmpdir == NULL)
		tmpdir = try_dir(getenv("TMPDIR"));
	if (tmpdir == NULL)
		tmpdir = try_dir(getenv("TMP"));
	if (tmpdir == NULL)
		tmpdir = try_dir(getenv("TEMP"));

#ifdef P_tmpdir
	if (tmpdir == NULL)
		tmpdir = try_dir(P_tmpdir);
#endif

	if (tmpdir == NULL)
		tmpdir = try_dir("/var/tmp");
	if (tmpdir == NULL)
		tmpdir = try_dir("/usr/tmp");
	if (tmpdir == NULL)
		tmpdir = try_dir("/tmp");

	if (tmpdir == NULL)
		tmpdir = ".";

	return tmpdir;
}

#ifndef HAVE_MKSTEMP
/* cheap and nasty mkstemp replacement */
static int mkstemp(char *templ)
{
	mktemp(templ);
	return open(templ, O_RDWR|O_CREAT|O_EXCL|O_BINARY, 0600);
}
#endif

/**
 * an own version of tmpnam, which: writes in a buffer, emits no warnings
 * during linking (like glibc/gnu ld do for tmpnam)...
 */
static FILE *make_temp_file(char *buffer, size_t buflen, const char *prefix)
{
	const char *tempdir = get_tempdir();

	snprintf(buffer, buflen, "%s/%sXXXXXX", tempdir, prefix);

	int fd = mkstemp(buffer);
	if (fd == -1) {
		fprintf(stderr, "couldn't create temporary file: %s\n",
		        strerror(errno));
		exit(1);
	}
	FILE *out = fdopen(fd, "w");
	if (out == NULL) {
		fprintf(stderr, "couldn't create temporary file FILE*\n");
		exit(1);
	}

	file_list_entry_t *entry = xmalloc(sizeof(*entry));
	memset(entry, 0, sizeof(*entry));

	size_t  name_len = strlen(buffer) + 1;
	char   *name     = malloc(name_len);
	memcpy(name, buffer, name_len);
	entry->name      = name;

	entry->next = temp_files;
	temp_files  = entry;

	return out;
}

static void free_temp_files(void)
{
	file_list_entry_t *entry = temp_files;
	file_list_entry_t *next;
	for ( ; entry != NULL; entry = next) {
		next = entry->next;

		unlink(entry->name);
		free((char*) entry->name);
		free(entry);
	}
	temp_files = NULL;
}

/**
 * Do the necessary lowering for compound parameters.
 */
void lower_compound_params(void)
{
	lower_params_t params;

	params.def_ptr_alignment    = 4;
	params.flags                = LF_COMPOUND_RETURN | LF_RETURN_HIDDEN;
	params.hidden_params        = ADD_HIDDEN_ALWAYS_IN_FRONT;
	params.find_pointer_type    = NULL;
	params.ret_compound_in_regs = NULL;
	lower_calls_with_compounds(&params);
}

typedef enum compile_mode_t {
	BenchmarkParser,
	PreprocessOnly,
	ParseOnly,
	Compile,
	CompileDump,
	CompileExportIR,
	CompileAssemble,
	CompileAssembleLink,
	LexTest,
	PrintAst,
	PrintFluffy,
	PrintCaml,
	PrintJna
} compile_mode_t;

static void usage(const char *argv0)
{
	fprintf(stderr, "Usage %s [options] input [-o output]\n", argv0);
}

static void print_cparser_version(void)
{
	printf("cparser (%s) using libFirm (%u.%u",
	       cparser_REVISION, ir_get_version_major(),
	       ir_get_version_minor());

	const char *revision = ir_get_version_revision();
	if (revision[0] != 0) {
		putchar(' ');
		fputs(revision, stdout);
	}

	const char *build = ir_get_version_build();
	if (build[0] != 0) {
		putchar(' ');
		fputs(build, stdout);
	}
	puts(")");
	puts("This is free software; see the source for copying conditions.  There is NO\n"
	     "warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n");
}

static void print_help(const char *argv0)
{
	usage(argv0);
	puts("");
	puts("\t-fhelp     Display help about firm optimisation options");
	puts("\t-bhelp     Display help about firm backend options");
	puts("A big number of gcc flags is also supported");
}

static void set_be_option(const char *arg)
{
	int res = be_parse_arg(arg);
	(void) res;
	assert(res);
}

static void set_option(const char *arg)
{
	int res = firm_option(arg);
	(void) res;
	assert(res);
}

static void copy_file(FILE *dest, FILE *input)
{
	char buf[16384];

	while (!feof(input) && !ferror(dest)) {
		size_t read = fread(buf, 1, sizeof(buf), input);
		if (fwrite(buf, 1, read, dest) != read) {
			perror("couldn't write output");
		}
	}
}

static inline bool streq(char const* a, char const* b)
{
	return strcmp(a, b) == 0;
}

static inline bool strstart(char const* str, char const* start)
{
	do {
		if (*start == '\0')
			return true;
	} while (*str++ == *start++);
	return false;
}

static FILE *open_file(const char *filename)
{
	if (streq(filename, "-")) {
		return stdin;
	}

	FILE *in = fopen(filename, "r");
	if (in == NULL) {
		fprintf(stderr, "Couldn't open '%s': %s\n", filename,
				strerror(errno));
		exit(1);
	}

	return in;
}

static filetype_t get_filetype_from_string(const char *string)
{
	if (streq(string, "c") || streq(string, "c-header"))
		return FILETYPE_C;
	if (streq(string, "c++") || streq(string, "c++-header"))
		return FILETYPE_CXX;
	if (streq(string, "assembler"))
		return FILETYPE_PREPROCESSED_ASSEMBLER;
	if (streq(string, "assembler-with-cpp"))
		return FILETYPE_ASSEMBLER;
	if (streq(string, "none"))
		return FILETYPE_AUTODETECT;

	return FILETYPE_UNKNOWN;
}

static void init_os_support(void)
{
	/* OS option must be set to the backend */
	switch (firm_opt.os_support) {
	case OS_SUPPORT_MINGW:
		set_be_option("ia32-gasmode=mingw");
		wchar_atomic_kind = ATOMIC_TYPE_USHORT;
		break;
	case OS_SUPPORT_LINUX:
		set_be_option("ia32-gasmode=elf");
		break;
	case OS_SUPPORT_MACHO:
		set_be_option("ia32-gasmode=macho");
		set_be_option("ia32-stackalign=4");
		set_be_option("pic");
		break;
	}
}

int main(int argc, char **argv)
{
	initialize_firm();

	const char        *dumpfunction         = NULL;
	const char        *print_file_name_file = NULL;
	compile_mode_t     mode                 = CompileAssembleLink;
	int                opt_level            = 1;
	int                result               = EXIT_SUCCESS;
	char               cpu_arch[16]         = "ia32";
	file_list_entry_t *files                = NULL;
	file_list_entry_t *last_file            = NULL;
	bool               construct_dep_target = false;
	bool               do_timing            = false;
	struct obstack     file_obst;

	atexit(free_temp_files);

	/* hack for now... */
	if (strstr(argv[0], "pptest") != NULL) {
		extern int pptest_main(int argc, char **argv);
		return pptest_main(argc, argv);
	}

	obstack_init(&cppflags_obst);
	obstack_init(&ldflags_obst);
	obstack_init(&file_obst);

#define GET_ARG_AFTER(def, args)                                             \
	def = &arg[sizeof(args)-1];                                              \
	if (def[0] == '\0') {                                                     \
		++i;                                                                 \
		if (i >= argc) {                                                      \
			fprintf(stderr, "error: expected argument after '" args "'\n");  \
			argument_errors = true;                                          \
			break;                                                           \
		}                                                                    \
		def = argv[i];                                                       \
		if (def[0] == '-' && def[1] != '\0') {                                \
			fprintf(stderr, "error: expected argument after '" args "'\n");  \
			argument_errors = true;                                          \
			continue;                                                        \
		}                                                                    \
	}

#define SINGLE_OPTION(ch) (option[0] == (ch) && option[1] == '\0')

	/* early options parsing (find out optimisation level and OS) */
	for (int i = 1; i < argc; ++i) {
		const char *arg = argv[i];
		if (arg[0] != '-')
			continue;

		const char *option = &arg[1];
		if (option[0] == 'O') {
			sscanf(&option[1], "%d", &opt_level);
		}
		if (strcmp(arg, "-fwin32") == 0) {
			firm_opt.os_support = OS_SUPPORT_MINGW;
		} else if (strcmp(arg, "-fmac") == 0) {
			firm_opt.os_support = OS_SUPPORT_MACHO;
		} else if (strcmp(arg, "-flinux") == 0) {
			firm_opt.os_support = OS_SUPPORT_LINUX;
		}
	}

	/* set target/os specific stuff */
	init_os_support();

	/* apply optimisation level */
	switch(opt_level) {
	case 0:
		set_option("no-opt");
		break;
	case 1:
		set_option("no-inline");
		break;
	default:
	case 4:
		/* use_builtins = true; */
		/* fallthrough */
	case 3:
		set_option("thread-jumps");
		set_option("if-conversion");
		/* fallthrough */
	case 2:
		set_option("strict-aliasing");
		set_option("inline");
		set_option("deconv");
		set_be_option("omitfp");
		break;
	}

	/* parse rest of options */
	standard                        = STANDARD_DEFAULT;
	unsigned        features_on     = 0;
	unsigned        features_off    = 0;
	filetype_t      forced_filetype = FILETYPE_AUTODETECT;
	bool            help_displayed  = false;
	bool            argument_errors = false;
	for (int i = 1; i < argc; ++i) {
		const char *arg = argv[i];
		if (arg[0] == '-' && arg[1] != '\0') {
			/* an option */
			const char *option = &arg[1];
			if (option[0] == 'o') {
				GET_ARG_AFTER(outname, "-o");
			} else if (option[0] == 'g') {
				set_be_option("debuginfo=stabs");
				set_be_option("omitfp=no");
				set_be_option("ia32-nooptcc=yes");
			} else if (SINGLE_OPTION('c')) {
				mode = CompileAssemble;
			} else if (SINGLE_OPTION('E')) {
				mode = PreprocessOnly;
			} else if (SINGLE_OPTION('S')) {
				mode = Compile;
			} else if (option[0] == 'O') {
				continue;
			} else if (option[0] == 'I') {
				const char *opt;
				GET_ARG_AFTER(opt, "-I");
				add_flag(&cppflags_obst, "-I%s", opt);
			} else if (option[0] == 'D') {
				const char *opt;
				GET_ARG_AFTER(opt, "-D");
				add_flag(&cppflags_obst, "-D%s", opt);
			} else if (option[0] == 'U') {
				const char *opt;
				GET_ARG_AFTER(opt, "-U");
				add_flag(&cppflags_obst, "-U%s", opt);
			} else if (option[0] == 'l') {
				const char *opt;
				GET_ARG_AFTER(opt, "-l");
				add_flag(&ldflags_obst, "-l%s", opt);
			} else if (option[0] == 'L') {
				const char *opt;
				GET_ARG_AFTER(opt, "-L");
				add_flag(&ldflags_obst, "-L%s", opt);
			} else if (SINGLE_OPTION('v')) {
				verbose = 1;
			} else if (SINGLE_OPTION('w')) {
				memset(&warning, 0, sizeof(warning));
			} else if (option[0] == 'x') {
				const char *opt;
				GET_ARG_AFTER(opt, "-x");
				forced_filetype = get_filetype_from_string(opt);
				if (forced_filetype == FILETYPE_UNKNOWN) {
					fprintf(stderr, "Unknown language '%s'\n", opt);
					argument_errors = true;
				}
			} else if (streq(option, "M")) {
				mode = PreprocessOnly;
				add_flag(&cppflags_obst, "-M");
			} else if (streq(option, "MMD") ||
			           streq(option, "MD")) {
			    construct_dep_target = true;
				add_flag(&cppflags_obst, "-%s", option);
			} else if (streq(option, "MM")  ||
			           streq(option, "MP")) {
				add_flag(&cppflags_obst, "-%s", option);
			} else if (streq(option, "MT") ||
			           streq(option, "MQ") ||
			           streq(option, "MF")) {
				const char *opt;
				GET_ARG_AFTER(opt, "-MT");
				add_flag(&cppflags_obst, "-%s", option);
				add_flag(&cppflags_obst, "%s", opt);
			} else if (streq(option, "include")) {
				const char *opt;
				GET_ARG_AFTER(opt, "-include");
				add_flag(&cppflags_obst, "-include");
				add_flag(&cppflags_obst, "%s", opt);
			} else if (streq(option, "isystem")) {
				const char *opt;
				GET_ARG_AFTER(opt, "-isystem");
				add_flag(&cppflags_obst, "-isystem");
				add_flag(&cppflags_obst, "%s", opt);
			} else if (streq(option, "nostdinc")
					|| streq(option, "trigraphs")) {
				/* pass these through to the preprocessor */
				add_flag(&cppflags_obst, "%s", arg);
			} else if (streq(option, "pipe")) {
				/* here for gcc compatibility */
			} else if (option[0] == 'f') {
				char const *orig_opt;
				GET_ARG_AFTER(orig_opt, "-f");

				if (strstart(orig_opt, "input-charset=")) {
					char const* const encoding = strchr(orig_opt, '=') + 1;
					select_input_encoding(encoding);
				} else if (streq(orig_opt, "verbose-asm")) {
					/* ignore: we always print verbose assembler */
				} else {
					char const *opt         = orig_opt;
					bool        truth_value = true;
					if (opt[0] == 'n' && opt[1] == 'o' && opt[2] == '-') {
						truth_value = false;
						opt += 3;
					}

					if (streq(opt, "builtins")) {
						use_builtins = truth_value;
					} else if (streq(opt, "dollars-in-identifiers")) {
						allow_dollar_in_symbol = truth_value;
					} else if (streq(opt, "omit-frame-pointer")) {
						set_be_option(truth_value ? "omitfp" : "omitfp=no");
					} else if (streq(opt, "short-wchar")) {
						wchar_atomic_kind = truth_value ? ATOMIC_TYPE_USHORT
							: ATOMIC_TYPE_INT;
					} else if (streq(opt, "signed-char")) {
						char_is_signed = truth_value;
					} else if (streq(opt, "strength-reduce")) {
						firm_option(truth_value ? "strength-red" : "no-strength-red");
					} else if (streq(opt, "syntax-only")) {
						mode = truth_value ? ParseOnly : CompileAssembleLink;
					} else if (streq(opt, "unsigned-char")) {
						char_is_signed = !truth_value;
					} else if (truth_value == false &&
					           streq(opt, "asynchronous-unwind-tables")) {
					    /* nothing todo, a gcc feature which we don't support
					     * anyway was deactivated */
					} else if (strstart(orig_opt, "align-loops=") ||
							strstart(orig_opt, "align-jumps=") ||
							strstart(orig_opt, "align-functions=")) {
						fprintf(stderr, "ignoring gcc option '-f%s'\n", orig_opt);
					} else if (strstart(orig_opt, "message-length=")) {
							/* ignore: would only affect error message format */
					} else if (streq(opt, "fast-math")               ||
					           streq(opt, "jump-tables")             ||
					           streq(opt, "expensive-optimizations") ||
					           streq(opt, "common")                  ||
					           streq(opt, "optimize-sibling-calls")  ||
					           streq(opt, "align-loops")             ||
					           streq(opt, "align-jumps")             ||
					           streq(opt, "align-functions")         ||
					           streq(opt, "PIC")) {
						fprintf(stderr, "ignoring gcc option '-f%s'\n", orig_opt);
					} else {
						int res = firm_option(orig_opt);
						if (res == 0) {
							fprintf(stderr, "error: unknown Firm option '-f%s'\n",
							        orig_opt);
							argument_errors = true;
							continue;
						} else if (res == -1) {
							help_displayed = true;
						}
					}
				}
			} else if (option[0] == 'b') {
				const char *opt;
				GET_ARG_AFTER(opt, "-b");
				int res = be_parse_arg(opt);
				if (res == 0) {
					fprintf(stderr, "error: unknown Firm backend option '-b %s'\n",
					        opt);
					argument_errors = true;
				} else if (res == -1) {
					help_displayed = true;
				} else if (strstart(opt, "isa=")) {
					strncpy(cpu_arch, opt, sizeof(cpu_arch));
				}
			} else if (option[0] == 'W') {
				if (option[1] == '\0') {
					/* ignore -W, out defaults are already quiet verbose */
				} else if (strstart(option + 1, "p,")) {
					// pass options directly to the preprocessor
					const char *opt;
					GET_ARG_AFTER(opt, "-Wp,");
					add_flag(&cppflags_obst, "-Wp,%s", opt);
				} else if (strstart(option + 1, "l,")) {
					// pass options directly to the linker
					const char *opt;
					GET_ARG_AFTER(opt, "-Wl,");
					add_flag(&ldflags_obst, "-Wl,%s", opt);
				} else if (streq(option + 1, "no-trigraphs")
							|| streq(option + 1, "undef")) {
					add_flag(&cppflags_obst, "%s", arg);
				} else {
					set_warning_opt(&option[1]);
				}
			} else if (option[0] == 'm') {
				/* -m options */
				const char *opt;
				char arch_opt[64];

				GET_ARG_AFTER(opt, "-m");
				if (strstart(opt, "arch=")) {
					GET_ARG_AFTER(opt, "-march=");
					snprintf(arch_opt, sizeof(arch_opt), "%s-arch=%s", cpu_arch, opt);
					int res = be_parse_arg(arch_opt);
					if (res == 0) {
						fprintf(stderr, "Unknown architecture '%s'\n", arch_opt);
						argument_errors = true;
					} else {
						snprintf(arch_opt, sizeof(arch_opt), "%s-opt=%s", cpu_arch, opt);
						int res = be_parse_arg(arch_opt);
						if (res == 0)
							argument_errors = true;
					}
				} else if (strstart(opt, "tune=")) {
					GET_ARG_AFTER(opt, "-mtune=");
					snprintf(arch_opt, sizeof(arch_opt), "%s-opt=%s", cpu_arch, opt);
					int res = be_parse_arg(arch_opt);
					if (res == 0)
						argument_errors = true;
				} else if (strstart(opt, "cpu=")) {
					GET_ARG_AFTER(opt, "-mcpu=");
					snprintf(arch_opt, sizeof(arch_opt), "%s-arch=%s", cpu_arch, opt);
					int res = be_parse_arg(arch_opt);
					if (res == 0)
						argument_errors = true;
				} else if (strstart(opt, "fpmath=")) {
					GET_ARG_AFTER(opt, "-mfpmath=");
					if (streq(opt, "387"))
						opt = "x87";
					else if (streq(opt, "sse"))
						opt = "sse2";
					else {
						fprintf(stderr, "error: option -mfpumath supports only 387 or sse\n");
						argument_errors = true;
					}
					if (!argument_errors) {
						snprintf(arch_opt, sizeof(arch_opt), "%s-fpunit=%s", cpu_arch, opt);
						int res = be_parse_arg(arch_opt);
						if (res == 0)
							argument_errors = true;
					}
				} else if (strstart(opt, "preferred-stack-boundary=")) {
					GET_ARG_AFTER(opt, "-mpreferred-stack-boundary=");
					snprintf(arch_opt, sizeof(arch_opt), "%s-stackalign=%s", cpu_arch, opt);
					int res = be_parse_arg(arch_opt);
					if (res == 0)
						argument_errors = true;
				} else if (streq(opt, "omit-leaf-frame-pointer")) {
					set_be_option("omitleaffp=1");
				} else if (streq(opt, "no-omit-leaf-frame-pointer")) {
					set_be_option("omitleaffp=0");
				} else if (streq(opt, "rtd")) {
					default_calling_convention = CC_STDCALL;
				} else if (strstart(opt, "regparm=")) {
					fprintf(stderr, "error: regparm convention not supported yet\n");
					argument_errors = true;
				} else if (streq(opt, "soft-float")) {
					fprintf(stderr, "error: software floatingpoint not supported yet\n");
					argument_errors = true;
				} else {
					char *endptr;
					long int value = strtol(opt, &endptr, 10);
					if (*endptr != '\0') {
						fprintf(stderr, "error: wrong option '-m %s'\n",  opt);
						argument_errors = true;
					}
					if (value != 16 && value != 32 && value != 64) {
						fprintf(stderr, "error: option -m supports only 16, 32 or 64\n");
						argument_errors = true;
					} else {
						machine_size = (unsigned int)value;
					}
				}
			} else if (streq(option, "pg")) {
				set_be_option("gprof");
				add_flag(&ldflags_obst, "-pg");
			} else if (streq(option, "pedantic") ||
			           streq(option, "ansi")) {
				fprintf(stderr, "warning: ignoring gcc option '%s'\n", arg);
			} else if (streq(option, "shared")) {
				add_flag(&ldflags_obst, "-shared");
			} else if (strstart(option, "std=")) {
				const char *const o = &option[4];
				standard =
					streq(o, "c++")            ? STANDARD_CXX98   :
					streq(o, "c++98")          ? STANDARD_CXX98   :
					streq(o, "c89")            ? STANDARD_C89     :
					streq(o, "c99")            ? STANDARD_C99     :
					streq(o, "c9x")            ? STANDARD_C99     : // deprecated
					streq(o, "gnu++98")        ? STANDARD_GNUXX98 :
					streq(o, "gnu89")          ? STANDARD_GNU89   :
					streq(o, "gnu99")          ? STANDARD_GNU99   :
					streq(o, "gnu9x")          ? STANDARD_GNU99   : // deprecated
					streq(o, "iso9899:1990")   ? STANDARD_C89     :
					streq(o, "iso9899:199409") ? STANDARD_C90     :
					streq(o, "iso9899:1999")   ? STANDARD_C99     :
					streq(o, "iso9899:199x")   ? STANDARD_C99     : // deprecated
					(fprintf(stderr, "warning: ignoring gcc option '%s'\n", arg), standard);
			} else if (streq(option, "version")) {
				print_cparser_version();
			} else if (strstart(option, "print-file-name=")) {
				GET_ARG_AFTER(print_file_name_file, "-print-file-name=");
			} else if (option[0] == '-') {
				/* double dash option */
				++option;
				if (streq(option, "gcc")) {
					features_on  |=  _GNUC;
					features_off &= ~_GNUC;
				} else if (streq(option, "no-gcc")) {
					features_on  &= ~_GNUC;
					features_off |=  _GNUC;
				} else if (streq(option, "ms")) {
					features_on  |=  _MS;
					features_off &= ~_MS;
				} else if (streq(option, "no-ms")) {
					features_on  &= ~_MS;
					features_off |=  _MS;
				} else if (streq(option, "strict")) {
					strict_mode = true;
				} else if (streq(option, "lextest")) {
					mode = LexTest;
				} else if (streq(option, "benchmark")) {
					mode = BenchmarkParser;
				} else if (streq(option, "print-ast")) {
					mode = PrintAst;
				} else if (streq(option, "print-implicit-cast")) {
					print_implicit_casts = true;
				} else if (streq(option, "print-parenthesis")) {
					print_parenthesis = true;
				} else if (streq(option, "print-fluffy")) {
					mode = PrintFluffy;
				} else if (streq(option, "print-caml")) {
					mode = PrintCaml;
				} else if (streq(option, "print-jna")) {
					mode = PrintJna;
				} else if (streq(option, "time")) {
					do_timing = true;
				} else if (streq(option, "version")) {
					print_cparser_version();
					exit(EXIT_SUCCESS);
				} else if (streq(option, "help")) {
					print_help(argv[0]);
					help_displayed = true;
				} else if (streq(option, "dump-function")) {
					++i;
					if (i >= argc) {
						fprintf(stderr, "error: "
						        "expected argument after '--dump-function'\n");
						argument_errors = true;
						break;
					}
					dumpfunction = argv[i];
					mode         = CompileDump;
				} else if (streq(option, "export-ir")) {
					mode = CompileExportIR;
				} else {
					fprintf(stderr, "error: unknown argument '%s'\n", arg);
					argument_errors = true;
				}
			} else {
				fprintf(stderr, "error: unknown argument '%s'\n", arg);
				argument_errors = true;
			}
		} else {
			filetype_t type = forced_filetype;
			if (type == FILETYPE_AUTODETECT) {
				if (streq(arg, "-")) {
					/* - implicitly means C source file */
					type = FILETYPE_C;
				} else {
					const char *suffix = strrchr(arg, '.');
					/* Ensure there is at least one char before the suffix */
					if (suffix != NULL && suffix != arg) {
						++suffix;
						type =
							streq(suffix, "S")   ? FILETYPE_ASSEMBLER              :
							streq(suffix, "a")   ? FILETYPE_OBJECT                 :
							streq(suffix, "c")   ? FILETYPE_C                      :
							streq(suffix, "cc")  ? FILETYPE_CXX                    :
							streq(suffix, "cpp") ? FILETYPE_CXX                    :
							streq(suffix, "cxx") ? FILETYPE_CXX                    :
							streq(suffix, "h")   ? FILETYPE_C                      :
							streq(suffix, "ir")  ? FILETYPE_IR                     :
							streq(suffix, "o")   ? FILETYPE_OBJECT                 :
							streq(suffix, "s")   ? FILETYPE_PREPROCESSED_ASSEMBLER :
							streq(suffix, "so")  ? FILETYPE_OBJECT                 :
							FILETYPE_OBJECT; /* gcc behavior: unknown file extension means object file */
					}
				}
			}

			file_list_entry_t *entry
				= obstack_alloc(&file_obst, sizeof(entry[0]));
			memset(entry, 0, sizeof(entry[0]));
			entry->name = arg;
			entry->type = type;

			if (last_file != NULL) {
				last_file->next = entry;
			} else {
				files = entry;
			}
			last_file = entry;
		}
	}

	if (help_displayed) {
		return !argument_errors;
	}

	if (print_file_name_file != NULL) {
		print_file_name(print_file_name_file);
		return 0;
	}
	if (files == NULL) {
		fprintf(stderr, "error: no input files specified\n");
		argument_errors = true;
	}

	if (argument_errors) {
		usage(argv[0]);
		return 1;
	}

	/* we do the lowering in ast2firm */
	firm_opt.lower_bitfields = FALSE;

	/* set the c_mode here, types depends on it */
	c_mode |= features_on;
	c_mode &= ~features_off;

	gen_firm_init();
	init_symbol_table();
	init_types();
	init_typehash();
	init_basic_types();
	init_lexer();
	init_ast();
	init_parser();
	init_ast2firm();
	init_mangle();

	if (do_timing)
		timer_init();

	if (construct_dep_target) {
		if (outname != 0 && strlen(outname) >= 2) {
			get_output_name(dep_target, sizeof(dep_target), outname, ".d");
		} else {
			get_output_name(dep_target, sizeof(dep_target), files->name, ".d");
		}
	} else {
		dep_target[0] = '\0';
	}

	char outnamebuf[4096];
	if (outname == NULL) {
		const char *filename = files->name;

		switch(mode) {
		case BenchmarkParser:
		case PrintAst:
		case PrintFluffy:
		case PrintCaml:
		case PrintJna:
		case LexTest:
		case PreprocessOnly:
		case ParseOnly:
			outname = "-";
			break;
		case Compile:
			get_output_name(outnamebuf, sizeof(outnamebuf), filename, ".s");
			outname = outnamebuf;
			break;
		case CompileAssemble:
			get_output_name(outnamebuf, sizeof(outnamebuf), filename, ".o");
			outname = outnamebuf;
			break;
		case CompileDump:
			get_output_name(outnamebuf, sizeof(outnamebuf), dumpfunction,
			                ".vcg");
			outname = outnamebuf;
			break;
		case CompileExportIR:
			get_output_name(outnamebuf, sizeof(outnamebuf), filename, ".ir");
			outname = outnamebuf;
			break;
		case CompileAssembleLink:
#ifdef _WIN32
			outname = "a.exe";
#else
			outname = "a.out";
#endif
			break;
		}
	}

	assert(outname != NULL);

	FILE *out;
	if (streq(outname, "-")) {
		out = stdout;
	} else {
		out = fopen(outname, "w");
		if (out == NULL) {
			fprintf(stderr, "Couldn't open '%s' for writing: %s\n", outname,
					strerror(errno));
			return 1;
		}
	}

	file_list_entry_t *file;
	bool               already_constructed_firm = false;
	for (file = files; file != NULL; file = file->next) {
		char        asm_tempfile[1024];
		const char *filename = file->name;
		filetype_t  filetype = file->type;

		if (filetype == FILETYPE_OBJECT)
			continue;

		FILE *in = NULL;
		if (mode == LexTest) {
			if (in == NULL)
				in = open_file(filename);
			lextest(in, filename);
			fclose(in);
			exit(EXIT_SUCCESS);
		}

		FILE *preprocessed_in = NULL;
		filetype_t next_filetype = filetype;
		switch (filetype) {
			case FILETYPE_C:
				next_filetype = FILETYPE_PREPROCESSED_C;
				goto preprocess;
			case FILETYPE_CXX:
				next_filetype = FILETYPE_PREPROCESSED_CXX;
				goto preprocess;
			case FILETYPE_ASSEMBLER:
				next_filetype = FILETYPE_PREPROCESSED_ASSEMBLER;
				goto preprocess;
preprocess:
				/* no support for input on FILE* yet */
				if (in != NULL)
					panic("internal compiler error: in for preprocessor != NULL");

				preprocessed_in = preprocess(filename, filetype);
				if (mode == PreprocessOnly) {
					copy_file(out, preprocessed_in);
					int result = pclose(preprocessed_in);
					fclose(out);
					/* remove output file in case of error */
					if (out != stdout && result != EXIT_SUCCESS) {
						unlink(outname);
					}
					return result;
				}

				in = preprocessed_in;
				filetype = next_filetype;
				break;

			default:
				break;
		}

		FILE *asm_out;
		if (mode == Compile) {
			asm_out = out;
		} else {
			asm_out = make_temp_file(asm_tempfile, sizeof(asm_tempfile), "ccs");
		}

		if (in == NULL)
			in = open_file(filename);

		/* preprocess and compile */
		if (filetype == FILETYPE_PREPROCESSED_C) {
			char const* invalid_mode;
			switch (standard) {
				case STANDARD_ANSI:
				case STANDARD_C89:   c_mode = _C89;                break;
				/* TODO determine difference between these two */
				case STANDARD_C90:   c_mode = _C89;                break;
				case STANDARD_C99:   c_mode = _C89 | _C99;         break;
				case STANDARD_GNU89: c_mode = _C89 |        _GNUC; break;

default_c_warn:
					fprintf(stderr,
							"warning: command line option \"-std=%s\" is not valid for C\n",
							invalid_mode);
					/* FALLTHROUGH */
				case STANDARD_DEFAULT:
				case STANDARD_GNU99:   c_mode = _C89 | _C99 | _GNUC; break;

				case STANDARD_CXX98:   invalid_mode = "c++98"; goto default_c_warn;
				case STANDARD_GNUXX98: invalid_mode = "gnu98"; goto default_c_warn;
			}
			goto do_parsing;
		} else if (filetype == FILETYPE_PREPROCESSED_CXX) {
			char const* invalid_mode;
			switch (standard) {
				case STANDARD_C89:   invalid_mode = "c89";   goto default_cxx_warn;
				case STANDARD_C90:   invalid_mode = "c90";   goto default_cxx_warn;
				case STANDARD_C99:   invalid_mode = "c99";   goto default_cxx_warn;
				case STANDARD_GNU89: invalid_mode = "gnu89"; goto default_cxx_warn;
				case STANDARD_GNU99: invalid_mode = "gnu99"; goto default_cxx_warn;

				case STANDARD_ANSI:
				case STANDARD_CXX98: c_mode = _CXX; break;

default_cxx_warn:
					fprintf(stderr,
							"warning: command line option \"-std=%s\" is not valid for C++\n",
							invalid_mode);
				case STANDARD_DEFAULT:
				case STANDARD_GNUXX98: c_mode = _CXX | _GNUC; break;
			}

do_parsing:
			c_mode |= features_on;
			c_mode &= ~features_off;

			/* do the actual parsing */
			ir_timer_t *t_parsing = ir_timer_new();
			timer_register(t_parsing, "Frontend: Parsing");
			timer_push(t_parsing);
			init_tokens();
			translation_unit_t *const unit = do_parsing(in, filename);
			timer_pop(t_parsing);

			/* prints the AST even if errors occurred */
			if (mode == PrintAst) {
				print_to_file(out);
				print_ast(unit);
			}

			if (error_count > 0) {
				/* parsing failed because of errors */
				fprintf(stderr, "%u error(s), %u warning(s)\n", error_count,
				        warning_count);
				result = EXIT_FAILURE;
				continue;
			} else if (warning_count > 0) {
				fprintf(stderr, "%u warning(s)\n", warning_count);
			}

			if (in == preprocessed_in) {
				int pp_result = pclose(preprocessed_in);
				if (pp_result != EXIT_SUCCESS) {
					/* remove output file */
					if (out != stdout)
						unlink(outname);
					exit(EXIT_FAILURE);
				}
			}

			if (mode == BenchmarkParser) {
				return result;
			} else if (mode == PrintFluffy) {
				write_fluffy_decls(out, unit);
				continue;
			} else if (mode == PrintCaml) {
				write_caml_decls(out, unit);
				continue;
			} else if (mode == PrintJna) {
				write_jna_decls(out, unit);
				continue;
			}

			/* build the firm graph */
			ir_timer_t *t_construct = ir_timer_new();
			timer_register(t_construct, "Frontend: Graph construction");
			timer_push(t_construct);
			if (already_constructed_firm) {
				panic("compiling multiple files/translation units not possible");
			}
			translation_unit_to_firm(unit);
			already_constructed_firm = true;
			timer_pop(t_construct);

graph_built:
			if (mode == ParseOnly) {
				continue;
			}

			if (mode == CompileDump) {
				/* find irg */
				ident    *id     = new_id_from_str(dumpfunction);
				ir_graph *irg    = NULL;
				int       n_irgs = get_irp_n_irgs();
				for (int i = 0; i < n_irgs; ++i) {
					ir_graph *tirg   = get_irp_irg(i);
					ident    *irg_id = get_entity_ident(get_irg_entity(tirg));
					if (irg_id == id) {
						irg = tirg;
						break;
					}
				}

				if (irg == NULL) {
					fprintf(stderr, "No graph for function '%s' found\n",
					        dumpfunction);
					exit(1);
				}

				dump_ir_graph_file(out, irg);
				fclose(out);
				exit(0);
			}

			if (mode == CompileExportIR) {
				fclose(out);
				ir_export(outname);
				exit(0);
			}

			gen_firm_finish(asm_out, filename, have_const_functions);
			if (asm_out != out) {
				fclose(asm_out);
			}
		} else if (filetype == FILETYPE_IR) {
			fclose(in);
			ir_import(filename);
			goto graph_built;
		} else if (filetype == FILETYPE_PREPROCESSED_ASSEMBLER) {
			copy_file(asm_out, in);
			if (in == preprocessed_in) {
				int pp_result = pclose(preprocessed_in);
				if (pp_result != EXIT_SUCCESS) {
					/* remove output in error case */
					if (out != stdout)
						unlink(outname);
					return pp_result;
				}
			}
			if (asm_out != out) {
				fclose(asm_out);
			}
		}

		if (mode == Compile)
			continue;

		/* if we're here then we have preprocessed assembly */
		filename = asm_tempfile;
		filetype = FILETYPE_PREPROCESSED_ASSEMBLER;

		/* assemble */
		if (filetype == FILETYPE_PREPROCESSED_ASSEMBLER) {
			char        temp[1024];
			const char *filename_o;
			if (mode == CompileAssemble) {
				fclose(out);
				filename_o = outname;
			} else {
				FILE *tempf = make_temp_file(temp, sizeof(temp), "cco");
				fclose(tempf);
				filename_o = temp;
			}

			assemble(filename_o, filename);

			size_t len = strlen(filename_o) + 1;
			filename = obstack_copy(&file_obst, filename_o, len);
			filetype = FILETYPE_OBJECT;
		}

		/* ok we're done here, process next file */
		file->name = filename;
		file->type = filetype;
	}

	if (result != EXIT_SUCCESS) {
		if (out != stdout)
			unlink(outname);
		return result;
	}

	/* link program file */
	if (mode == CompileAssembleLink) {
		obstack_1grow(&ldflags_obst, '\0');
		const char *flags = obstack_finish(&ldflags_obst);

		/* construct commandline */
		const char *linker = getenv("CPARSER_LINK");
		if (linker == NULL)
			linker = LINKER;
		obstack_printf(&file_obst, "%s", linker);
		for (file_list_entry_t *entry = files; entry != NULL;
				entry = entry->next) {
			if (entry->type != FILETYPE_OBJECT)
				continue;

			add_flag(&file_obst, "%s", entry->name);
		}

		add_flag(&file_obst, "-o");
		add_flag(&file_obst, outname);
		obstack_printf(&file_obst, "%s", flags);
		obstack_1grow(&file_obst, '\0');

		char *commandline = obstack_finish(&file_obst);

		if (verbose) {
			puts(commandline);
		}
		int err = system(commandline);
		if (err != EXIT_SUCCESS) {
			fprintf(stderr, "linker reported an error\n");
			exit(1);
		}
	}

	if (do_timing)
		timer_term(stderr);

	obstack_free(&cppflags_obst, NULL);
	obstack_free(&ldflags_obst, NULL);
	obstack_free(&file_obst, NULL);

	exit_mangle();
	exit_ast2firm();
	exit_parser();
	exit_ast();
	exit_lexer();
	exit_typehash();
	exit_types();
	exit_tokens();
	exit_symbol_table();
	return 0;
}
