/*
   +----------------------------------------------------------------------+
   | PHP Version 5                                                        |
   +----------------------------------------------------------------------+
   | Copyright (c) 1997-2014 The PHP Group                                |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.php.net/license/3_01.txt                                  |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
   | Authors: Felipe Pena <felipe@php.net>                                |
   | Authors: Joe Watkins <joe.watkins@live.co.uk>                        |
   | Authors: Bob Weinand <bwoebi@php.net>                                |
   +----------------------------------------------------------------------+
*/

#include "zend.h"

#include "php.h"
#include "phpdbg.h"
#include "phpdbg_opcode.h"
#include "phpdbg_utils.h"

#if defined(HAVE_SYS_IOCTL_H)
#	include "sys/ioctl.h"
#	ifndef GWINSZ_IN_SYS_IOCTL
#		include <termios.h>
#	endif
#endif

ZEND_EXTERN_MODULE_GLOBALS(phpdbg);

/* {{{ color structures */
const static phpdbg_color_t colors[] = {
	PHPDBG_COLOR_D("none",             "0;0"),

	PHPDBG_COLOR_D("white",            "0;64"),
	PHPDBG_COLOR_D("white-bold",       "1;64"),
	PHPDBG_COLOR_D("white-underline",  "4;64"),
	PHPDBG_COLOR_D("red",              "0;31"),
	PHPDBG_COLOR_D("red-bold",         "1;31"),
	PHPDBG_COLOR_D("red-underline",    "4;31"),
	PHPDBG_COLOR_D("green",            "0;32"),
	PHPDBG_COLOR_D("green-bold",       "1;32"),
	PHPDBG_COLOR_D("green-underline",  "4;32"),
	PHPDBG_COLOR_D("yellow",           "0;33"),
	PHPDBG_COLOR_D("yellow-bold",      "1;33"),
	PHPDBG_COLOR_D("yellow-underline", "4;33"),
	PHPDBG_COLOR_D("blue",             "0;34"),
	PHPDBG_COLOR_D("blue-bold",        "1;34"),
	PHPDBG_COLOR_D("blue-underline",   "4;34"),
	PHPDBG_COLOR_D("purple",           "0;35"),
	PHPDBG_COLOR_D("purple-bold",      "1;35"),
	PHPDBG_COLOR_D("purple-underline", "4;35"),
	PHPDBG_COLOR_D("cyan",             "0;36"),
	PHPDBG_COLOR_D("cyan-bold",        "1;36"),
	PHPDBG_COLOR_D("cyan-underline",   "4;36"),
	PHPDBG_COLOR_D("black",            "0;30"),
	PHPDBG_COLOR_D("black-bold",       "1;30"),
	PHPDBG_COLOR_D("black-underline",  "4;30"),
	PHPDBG_COLOR_END
}; /* }}} */

/* {{{ */
const static phpdbg_element_t elements[] = {
	PHPDBG_ELEMENT_D("prompt", PHPDBG_COLOR_PROMPT),
	PHPDBG_ELEMENT_D("error", PHPDBG_COLOR_ERROR),
	PHPDBG_ELEMENT_D("notice", PHPDBG_COLOR_NOTICE),
	PHPDBG_ELEMENT_END
}; /* }}} */

PHPDBG_API int phpdbg_is_numeric(const char *str) /* {{{ */
{
	if (!str)
		return 0;

	for (; *str; str++) {
		if (isspace(*str) || *str == '-') {
			continue;
		}
		return isdigit(*str);
	}
	return 0;
} /* }}} */

PHPDBG_API int phpdbg_is_empty(const char *str) /* {{{ */
{
	if (!str)
		return 1;

	for (; *str; str++) {
		if (isspace(*str)) {
			continue;
		}
		return 0;
	}
	return 1;
} /* }}} */

PHPDBG_API int phpdbg_is_addr(const char *str) /* {{{ */
{
	return str[0] && str[1] && memcmp(str, "0x", 2) == 0;
} /* }}} */

PHPDBG_API int phpdbg_is_class_method(const char *str, size_t len, char **class, char **method) /* {{{ */
{
	char *sep = NULL;

	if (strstr(str, "#") != NULL)
		return 0;

	if (strstr(str, " ") != NULL)
		return 0;

	sep = strstr(str, "::");

	if (!sep || sep == str || sep+2 == str+len-1) {
		return 0;
	}

	if (class != NULL) {

		if (str[0] == '\\') {
			str++;
			len--;
		}

		*class = estrndup(str, sep - str);
		(*class)[sep - str] = 0;
	}

	if (method != NULL) {
		*method = estrndup(sep+2, str + len - (sep + 2));
	}

	return 1;
} /* }}} */

PHPDBG_API char *phpdbg_resolve_path(const char *path TSRMLS_DC) /* {{{ */
{
	char resolved_name[MAXPATHLEN];

	if (expand_filepath(path, resolved_name TSRMLS_CC) == NULL) {
		return NULL;
	}

	return estrdup(resolved_name);
} /* }}} */

PHPDBG_API const char *phpdbg_current_file(TSRMLS_D) /* {{{ */
{
	const char *file = zend_get_executed_filename(TSRMLS_C);

	if (memcmp(file, "[no active file]", sizeof("[no active file]")) == 0) {
		return PHPDBG_G(exec);
	}

	return file;
} /* }}} */

PHPDBG_API const zend_function *phpdbg_get_function(const char *fname, const char *cname TSRMLS_DC) /* {{{ */
{
	zend_function *func = NULL;
	size_t fname_len = strlen(fname);
	char *lcname = zend_str_tolower_dup(fname, fname_len);

	if (cname) {
		zend_class_entry **ce;
		size_t cname_len = strlen(cname);
		char *lc_cname = zend_str_tolower_dup(cname, cname_len);
		int ret = zend_lookup_class(lc_cname, cname_len, &ce TSRMLS_CC);

		efree(lc_cname);

		if (ret == SUCCESS) {
			zend_hash_find(&(*ce)->function_table, lcname, fname_len+1,
				(void**)&func);
		}
	} else {
		zend_hash_find(EG(function_table), lcname, fname_len+1,
			(void**)&func);
	}

	efree(lcname);
	return func;
} /* }}} */

PHPDBG_API char *phpdbg_trim(const char *str, size_t len, size_t *new_len) /* {{{ */
{
	const char *p = str;
	char *new = NULL;

	while (p && isspace(*p)) {
		++p;
		--len;
	}

	while (*p && isspace(*(p + len -1))) {
		--len;
	}

	if (len == 0) {
		new = estrndup("", sizeof(""));
		*new_len = 0;
	} else {
		new = estrndup(p, len);
		*(new + len) = '\0';

		if (new_len) {
			*new_len = len;
		}
	}

	return new;

} /* }}} */

PHPDBG_API const phpdbg_color_t *phpdbg_get_color(const char *name, size_t name_length TSRMLS_DC) /* {{{ */
{
	const phpdbg_color_t *color = colors;

	while (color && color->name) {
		if (name_length == color->name_length &&
			memcmp(name, color->name, name_length) == SUCCESS) {
			phpdbg_debug("phpdbg_get_color(%s, %lu): %s", name, name_length, color->code);
			return color;
		}
		++color;
	}

	phpdbg_debug("phpdbg_get_color(%s, %lu): failed", name, name_length);

	return NULL;
} /* }}} */

PHPDBG_API void phpdbg_set_color(int element, const phpdbg_color_t *color TSRMLS_DC) /* {{{ */
{
	PHPDBG_G(colors)[element] = color;
} /* }}} */

PHPDBG_API void phpdbg_set_color_ex(int element, const char *name, size_t name_length TSRMLS_DC) /* {{{ */
{
	const phpdbg_color_t *color = phpdbg_get_color(name, name_length TSRMLS_CC);

	if (color) {
		phpdbg_set_color(element, color TSRMLS_CC);
	} else PHPDBG_G(colors)[element] = colors;
} /* }}} */

PHPDBG_API const phpdbg_color_t* phpdbg_get_colors(TSRMLS_D) /* {{{ */
{
	return colors;
} /* }}} */

PHPDBG_API int phpdbg_get_element(const char *name, size_t len TSRMLS_DC) {
	const phpdbg_element_t *element = elements;

	while (element && element->name) {
		if (len == element->name_length) {
			if (strncasecmp(name, element->name, len) == SUCCESS) {
				return element->id;
			}
		}
		element++;
	}

	return PHPDBG_COLOR_INVALID;
}

PHPDBG_API void phpdbg_set_prompt(const char *prompt TSRMLS_DC) /* {{{ */
{
	/* free formatted prompt */
	if (PHPDBG_G(prompt)[1]) {
		free(PHPDBG_G(prompt)[1]);
		PHPDBG_G(prompt)[1] = NULL;
	}
	/* free old prompt */
	if (PHPDBG_G(prompt)[0]) {
		free(PHPDBG_G(prompt)[0]);
		PHPDBG_G(prompt)[0] = NULL;
	}

	/* copy new prompt */
	PHPDBG_G(prompt)[0] = strdup(prompt);
} /* }}} */

PHPDBG_API const char *phpdbg_get_prompt(TSRMLS_D) /* {{{ */
{
	/* find cached prompt */
	if (PHPDBG_G(prompt)[1]) {
		return PHPDBG_G(prompt)[1];
	}

	/* create cached prompt */
#ifndef HAVE_LIBEDIT
	/* TODO: libedit doesn't seems to support coloured prompt */
	if ((PHPDBG_G(flags) & PHPDBG_IS_COLOURED)) {
		asprintf(
			&PHPDBG_G(prompt)[1], "\033[%sm%s\033[0m ",
			PHPDBG_G(colors)[PHPDBG_COLOR_PROMPT]->code,
			PHPDBG_G(prompt)[0]);
	} else
#endif
	{
		asprintf(
			&PHPDBG_G(prompt)[1], "%s ",
			PHPDBG_G(prompt)[0]);
	}

	return PHPDBG_G(prompt)[1];
} /* }}} */

int phpdbg_rebuild_symtable(TSRMLS_D) {
	if (!EG(active_op_array)) {
		phpdbg_error("inactive", "type=\"op_array\"", "No active op array!");
		return FAILURE;
	}

	if (!EG(active_symbol_table)) {
		zend_rebuild_symbol_table(TSRMLS_C);

		if (!EG(active_symbol_table)) {
			phpdbg_error("inactive", "type=\"symbol_table\"", "No active symbol table!");
			return FAILURE;
		}
	}

	return SUCCESS;
}

PHPDBG_API int phpdbg_get_terminal_width(TSRMLS_D) /* {{{ */
{
	int columns;
#ifdef _WIN32
	CONSOLE_SCREEN_BUFFER_INFO csbi;

	GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
	columns = csbi.srWindow.Right - csbi.srWindow.Left + 1;
#elif defined(HAVE_SYS_IOCTL_H) && defined(TIOCGWINSZ)
	struct winsize w;

	columns = ioctl(fileno(stdout), TIOCGWINSZ, &w) == 0 ? w.ws_col : 80;
#else
	columns = 80;
#endif
	return columns;
} /* }}} */

PHPDBG_API void phpdbg_set_async_io(int fd) {
#ifndef _WIN32
	int flags;
	fcntl(STDIN_FILENO, F_SETOWN, getpid());
	flags = fcntl(STDIN_FILENO, F_GETFL);
	fcntl(STDIN_FILENO, F_SETFL, flags | FASYNC);
#endif
}

int phpdbg_safe_class_lookup(const char *name, int name_length, zend_class_entry ***ce TSRMLS_DC) {
	if (PHPDBG_G(flags) & PHPDBG_IN_SIGNAL_HANDLER) {
		char *lc_name, *lc_free;
		int lc_length, ret = FAILURE;

		if (name == NULL || !name_length) {
			return FAILURE;
		}

		lc_free = lc_name = emalloc(name_length + 1);
		zend_str_tolower_copy(lc_name, name, name_length);
		lc_length = name_length + 1;

		if (lc_name[0] == '\\') {
			lc_name += 1;
			lc_length -= 1;
		}

		phpdbg_try_access {
			ret = zend_hash_find(EG(class_table), lc_name, lc_length, (void **) &ce);
		} phpdbg_catch_access {
			phpdbg_error("signalsegv", "class=\"%.*s\"", "Could not fetch class %.*s, invalid data source", name_length, name);
		} phpdbg_end_try_access();

		efree(lc_free);
		return ret;
	} else {
		return zend_lookup_class(name, name_length, ce TSRMLS_CC);
	}
}

char *phpdbg_get_property_key(char *key) {
	if (*key != 0) {
		return key;
	}
	return strchr(key + 1, 0) + 1;
}

static int phpdbg_parse_variable_arg_wrapper(char *name, size_t len, char *keyname, size_t keylen, HashTable *parent, zval **zv, phpdbg_parse_var_func callback TSRMLS_DC) {
	return callback(name, len, keyname, keylen, parent, zv TSRMLS_CC);
}

PHPDBG_API int phpdbg_parse_variable(char *input, size_t len, HashTable *parent, size_t i, phpdbg_parse_var_func callback, zend_bool silent TSRMLS_DC) {
	return phpdbg_parse_variable_with_arg(input, len, parent, i, (phpdbg_parse_var_with_arg_func) phpdbg_parse_variable_arg_wrapper, silent, callback TSRMLS_CC);
}

PHPDBG_API int phpdbg_parse_variable_with_arg(char *input, size_t len, HashTable *parent, size_t i, phpdbg_parse_var_with_arg_func callback, zend_bool silent, void *arg TSRMLS_DC) {
	int ret = FAILURE;
	zend_bool new_index = 1;
	char *last_index;
	size_t index_len = 0;
	zval **zv;

	if (len < 2 || *input != '$') {
		goto error;
	}

	while (i++ < len) {
		if (i == len) {
			new_index = 1;
		} else {
			switch (input[i]) {
				case '[':
					new_index = 1;
					break;
				case ']':
					break;
				case '>':
					if (last_index[index_len - 1] == '-') {
						new_index = 1;
						index_len--;
					}
					break;

				default:
					if (new_index) {
						last_index = input + i;
						new_index = 0;
					}
					if (input[i - 1] == ']') {
						goto error;
					}
					index_len++;
			}
		}

		if (new_index && index_len == 0) {
			HashPosition position;
			for (zend_hash_internal_pointer_reset_ex(parent, &position);
			     zend_hash_get_current_data_ex(parent, (void **)&zv, &position) == SUCCESS;
			     zend_hash_move_forward_ex(parent, &position)) {
				if (i == len || (i == len - 1 && input[len - 1] == ']')) {
					zval *key = emalloc(sizeof(zval));
					size_t namelen;
					char *name;
					char *keyname = estrndup(last_index, index_len);
					zend_hash_get_current_key_zval_ex(parent, key, &position);
					convert_to_string(key);
					name = emalloc(i + Z_STRLEN_P(key) + 2);
					namelen = sprintf(name, "%.*s%s%s", (int)i, input, phpdbg_get_property_key(Z_STRVAL_P(key)), input[len - 1] == ']'?"]":"");
					efree(key);

					ret = callback(name, namelen, keyname, index_len, parent, zv, arg TSRMLS_CC) == SUCCESS || ret == SUCCESS?SUCCESS:FAILURE;
				} else if (Z_TYPE_PP(zv) == IS_OBJECT) {
					phpdbg_parse_variable_with_arg(input, len, Z_OBJPROP_PP(zv), i, callback, silent, arg TSRMLS_CC);
				} else if (Z_TYPE_PP(zv) == IS_ARRAY) {
					phpdbg_parse_variable_with_arg(input, len, Z_ARRVAL_PP(zv), i, callback, silent, arg TSRMLS_CC);
				} else {
					/* Ignore silently */
				}
			}
			return ret;
		} else if (new_index) {
			char last_chr = last_index[index_len];
			last_index[index_len] = 0;
			if (zend_symtable_find(parent, last_index, index_len + 1, (void **)&zv) == FAILURE) {
				if (!silent) {
					phpdbg_error("variable", "type=\"undefined\" variable=\"%.*s\"", "%.*s is undefined", (int) i, input);
				}
				return FAILURE;
			}
			last_index[index_len] = last_chr;
			if (i == len) {
				char *name = estrndup(input, len);
				char *keyname = estrndup(last_index, index_len);

				ret = callback(name, len, keyname, index_len, parent, zv, arg TSRMLS_CC) == SUCCESS || ret == SUCCESS?SUCCESS:FAILURE;
			} else if (Z_TYPE_PP(zv) == IS_OBJECT) {
				parent = Z_OBJPROP_PP(zv);
			} else if (Z_TYPE_PP(zv) == IS_ARRAY) {
				parent = Z_ARRVAL_PP(zv);
			} else {
				phpdbg_error("variable", "type=\"notiterable\" variable=\"%.*s\"", "%.*s is nor an array nor an object", (int) i, input);
				return FAILURE;
			}
			index_len = 0;
		}
	}

	return ret;
	error:
		phpdbg_error("variable", "type=\"invalidinput\"", "Malformed input");
		return FAILURE;
}

static int phpdbg_xml_array_element_dump(zval **zv TSRMLS_DC, int num_args, va_list args, zend_hash_key *hash_key) {
	phpdbg_xml("<element");

	phpdbg_try_access {
		if (hash_key->nKeyLength == 0) { /* numeric key */
			phpdbg_xml(" name=\"%ld\"", hash_key->h);
		} else { /* string key */
			phpdbg_xml(" name=\"%.*s\"", hash_key->nKeyLength - 1, hash_key->arKey);
		}
	} phpdbg_catch_access {
		phpdbg_xml(" severity=\"error\" ></element>");
		return 0;
	} phpdbg_end_try_access();

	phpdbg_xml(">");

	phpdbg_xml_var_dump(zv TSRMLS_CC);

	phpdbg_xml("</element>");

	return 0;
}

static int phpdbg_xml_object_property_dump(zval **zv TSRMLS_DC, int num_args, va_list args, zend_hash_key *hash_key) {
	phpdbg_xml("<property");

	phpdbg_try_access {
		if (hash_key->nKeyLength == 0) { /* numeric key */
			phpdbg_xml(" name=\"%ld\"", hash_key->h);
		} else { /* string key */
			const char *prop_name, *class_name;
			int unmangle = zend_unmangle_property_name(hash_key->arKey, hash_key->nKeyLength - 1, &class_name, &prop_name);

			if (class_name && unmangle == SUCCESS) {
				phpdbg_xml(" name=\"%s\"", prop_name);
				if (class_name[0] == '*') {
					phpdbg_xml(" protection=\"protected\"");
				} else {
					phpdbg_xml(" class=\"%s\" protection=\"private\"", class_name);
				}
			} else {
				phpdbg_xml(" name=\"%.*s\" protection=\"public\"", hash_key->nKeyLength - 1, hash_key->arKey);
			}
		}
	} phpdbg_catch_access {
		phpdbg_xml(" severity=\"error\" ></property>");
		return 0;
	} phpdbg_end_try_access();

	phpdbg_xml(">");

	phpdbg_xml_var_dump(zv TSRMLS_CC);

	phpdbg_xml("</property>");

	return 0;
}

#define COMMON (Z_ISREF_PP(zv) ? "&" : "")

PHPDBG_API void phpdbg_xml_var_dump(zval **zv TSRMLS_DC) {
	HashTable *myht;
	const char *class_name;
	zend_uint class_name_len;
	int (*element_dump_func)(zval** TSRMLS_DC, int, va_list, zend_hash_key*);
	int is_temp;

	phpdbg_try_access {
		switch (Z_TYPE_PP(zv)) {
			case IS_BOOL:
				phpdbg_xml("<bool refstatus=\"%s\" value=\"%s\" />", COMMON, Z_LVAL_PP(zv) ? "true" : "false");
				break;
			case IS_NULL:
				phpdbg_xml("<null refstatus=\"%s\" />", COMMON);
				break;
			case IS_LONG:
				phpdbg_xml("<int refstatus=\"%s\" value=\"%ld\" />", COMMON, Z_LVAL_PP(zv));
				break;
			case IS_DOUBLE:
				phpdbg_xml("<float refstatus=\"%s\" value=\"%.*G\" />", COMMON, (int) EG(precision), Z_DVAL_PP(zv));
				break;
			case IS_STRING:
				phpdbg_xml("<string refstatus=\"%s\" length=\"%d\" value=\"%.*s\" />", COMMON, Z_STRLEN_PP(zv), Z_STRLEN_PP(zv), Z_STRVAL_PP(zv));
				break;
			case IS_ARRAY:
				myht = Z_ARRVAL_PP(zv);
				if (++myht->nApplyCount > 1) {
					phpdbg_xml("<recursion />");
					--myht->nApplyCount;
					break;
				}
				phpdbg_xml("<array refstatus=\"%s\" num=\"%d\">", COMMON, zend_hash_num_elements(myht));
				element_dump_func = phpdbg_xml_array_element_dump;
				is_temp = 0;
				goto head_done;
			case IS_OBJECT:
				myht = Z_OBJDEBUG_PP(zv, is_temp);
				if (myht && ++myht->nApplyCount > 1) {
					phpdbg_xml("<recursion />");
					--myht->nApplyCount;
					break;
				}
	
				if (Z_OBJ_HANDLER(**zv, get_class_name)) {
					Z_OBJ_HANDLER(**zv, get_class_name)(*zv, &class_name, &class_name_len, 0 TSRMLS_CC);
					phpdbg_xml("<object refstatus=\"%s\" class=\"%s\" id=\"%d\" num=\"%d\">", COMMON, class_name, Z_OBJ_HANDLE_PP(zv), myht ? zend_hash_num_elements(myht) : 0);
					efree((char*)class_name);
				} else {
					phpdbg_xml("<object refstatus=\"%s\" class=\"\" id=\"%d\" num=\"%d\">", COMMON, Z_OBJ_HANDLE_PP(zv), myht ? zend_hash_num_elements(myht) : 0);
				}
				element_dump_func = phpdbg_xml_object_property_dump;
head_done:
				if (myht) {
					zend_hash_apply_with_arguments(myht TSRMLS_CC, (apply_func_args_t) element_dump_func, 0);
					--myht->nApplyCount;
					if (is_temp) {
						zend_hash_destroy(myht);
						efree(myht);
					}
				}
				if (Z_TYPE_PP(zv) == IS_ARRAY) {
					phpdbg_xml("</array>");
				} else {
					phpdbg_xml("</object>");
				}
				break;
			case IS_RESOURCE: {
				const char *type_name = zend_rsrc_list_get_rsrc_type(Z_LVAL_PP(zv) TSRMLS_CC);
				phpdbg_xml("<resource refstatus=\"%s\" id=\"%ld\" type=\"%s\" />", COMMON, Z_LVAL_PP(zv), type_name ? type_name : "unknown");
				break;
			}
			default:
				break;
		}
	} phpdbg_end_try_access();
}

static int phpdbg_print_array_element_dump(zval **zv TSRMLS_DC, int num_args, va_list args, zend_hash_key *hash_key) {
	int *len = va_arg(args, int *);
	zend_bool *first = va_arg(args, zend_bool *);

	if (*first) {
		*first = 0;
	} else {
		*len -= phpdbg_out(", ");
	}

	if (*len < 0) {
		phpdbg_out("...");
		return ZEND_HASH_APPLY_STOP;
	}

	phpdbg_try_access {
		if (hash_key->nKeyLength == 0) { /* numeric key */
			*len -= phpdbg_out("%ld => ", hash_key->h);
		} else { /* string key */
			*len -= phpdbg_out("\"%.*s\" => ", hash_key->nKeyLength - 1, hash_key->arKey);
		}
	} phpdbg_catch_access {
		*len -= phpdbg_out("???");
		return 0;
	} phpdbg_end_try_access();

	*len = phpdbg_print_flat_zval_r(zv, *len TSRMLS_CC);

	return 0;
}

static int phpdbg_print_object_property_dump(zval **zv TSRMLS_DC, int num_args, va_list args, zend_hash_key *hash_key) {
	int *len = va_arg(args, int *);
	zend_bool *first = va_arg(args, zend_bool *);

	if (*first) {
		*first = 0;
	} else {
		*len -= phpdbg_out(", ");
	}

	if (*len < 0) {
		phpdbg_out("...");
		return ZEND_HASH_APPLY_STOP;
	}

	phpdbg_try_access {
		if (hash_key->nKeyLength == 0) { /* numeric key */
			*len -= phpdbg_out("%ld => ", hash_key->h);
		} else { /* string key */
			const char *prop_name, *class_name;
			int unmangle = zend_unmangle_property_name(hash_key->arKey, hash_key->nKeyLength - 1, &class_name, &prop_name);

			if (class_name && unmangle == SUCCESS && class_name[0] != '*') {
				*len -= phpdbg_out("\"%s:%s\" => ", class_name, prop_name);
			} else {
				*len -= phpdbg_out("\"%s\" => ", prop_name);
			}
		}
	} phpdbg_catch_access {
		*len -= phpdbg_out("???");
		return 0;
	} phpdbg_end_try_access();


	*len = phpdbg_print_flat_zval_r(zv, *len TSRMLS_CC);

	return 0;
}

#define COMMON (Z_ISREF_PP(zv) ? "&" : "")

PHPDBG_API int phpdbg_print_flat_zval_r(zval **zv, int len TSRMLS_DC) {
	HashTable *myht;
	const char *class_name;
	zend_uint class_name_len;
	int (*element_dump_func)(zval ** TSRMLS_DC, int, va_list, zend_hash_key*);
	int is_temp;
	zend_bool first = 1;

	phpdbg_try_access {
		switch (Z_TYPE_PP(zv)) {
			case IS_BOOL:
				len -= phpdbg_out("%sbool(%s)", COMMON, Z_LVAL_PP(zv) ? "true" : "false");
				break;
			case IS_NULL:
				len -= phpdbg_out("%snull", COMMON);
				break;
			case IS_LONG:
				len -= phpdbg_out("%sint(%ld)", COMMON, Z_LVAL_PP(zv));
				break;
			case IS_DOUBLE:
				len -= phpdbg_out("%sfloat(%.*G)", COMMON, (int) EG(precision), Z_DVAL_PP(zv));
				break;
			case IS_STRING:
				len -= phpdbg_out("%sstring(%d) \"%.*s%s\"", COMMON, Z_STRLEN_PP(zv), Z_STRLEN_PP(zv) > len + 3 ? len < 3 ? 3 : len : Z_STRLEN_PP(zv), Z_STRVAL_PP(zv), Z_STRLEN_PP(zv) > len + 3 ? "..." : "");
				break;
			case IS_ARRAY:
				myht = Z_ARRVAL_PP(zv);
				if (++myht->nApplyCount > 1) {
					len -= phpdbg_out("** RECURSION **");
					--myht->nApplyCount;
					break;
				}
				len -= phpdbg_out("%sarray(%d) [", COMMON, zend_hash_num_elements(myht));
				element_dump_func = phpdbg_print_array_element_dump;
				is_temp = 0;
				goto head_done;
			case IS_OBJECT:
				myht = Z_OBJDEBUG_PP(zv, is_temp);
				if (myht && ++myht->nApplyCount > 1) {
					len -= phpdbg_out("** RECURSION **");
					--myht->nApplyCount;
					break;
				}
	
				if (Z_OBJ_HANDLER(**zv, get_class_name)) {
					Z_OBJ_HANDLER(**zv, get_class_name)(*zv, &class_name, &class_name_len, 0 TSRMLS_CC);
					len -= phpdbg_out("%s%s#%u (%d) [", COMMON, class_name, Z_OBJ_HANDLE_PP(zv), myht ? zend_hash_num_elements(myht) : 0);
					efree((char*)class_name);
				} else {
					len -= phpdbg_out("%s(Unknown class#%u (%d) [", COMMON, Z_OBJ_HANDLE_PP(zv), myht ? zend_hash_num_elements(myht) : 0);
				}
				element_dump_func = phpdbg_print_object_property_dump;
head_done:
				if (myht) {
					zend_hash_apply_with_arguments(myht TSRMLS_CC, (apply_func_args_t) element_dump_func, 2, &len, &first);
					--myht->nApplyCount;
					if (is_temp) {
						zend_hash_destroy(myht);
						efree(myht);
					}
				}
				len -= phpdbg_out("]");
				break;
			case IS_RESOURCE: {
				const char *type_name = zend_rsrc_list_get_rsrc_type(Z_LVAL_PP(zv) TSRMLS_CC);
				len -= phpdbg_out("%sresource(#%ld) \"%s\"", COMMON, Z_LVAL_PP(zv), type_name ? type_name : "unknown");
				break;
			}
			default:
				break;
		}
	} phpdbg_end_try_access();

	return len;
}

