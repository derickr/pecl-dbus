/*
   +----------------------------------------------------------------------+
   | PHP Version 5                                                        |
   +----------------------------------------------------------------------+
   | Copyright (c) 2008-2013 The PHP Group                                |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.php.net/license/3_01.txt                                  |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
   | Authors: Derick Rethans <derick@derickrethans.nl>                    |
   +----------------------------------------------------------------------+
 */

/* $Id$ */

#include "php.h"
#include "php_main.h"
#include "php_globals.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "ext/standard/php_versioning.h"
#include "php_dbus.h"
#include "zend_extensions.h"
#include "zend_exceptions.h"
#include "zend_interfaces.h"
#include "zend_hash.h"
#include "dbus/dbus.h"
#include "libxml/parser.h"
#include "libxml/parserInternals.h"
#include "introspect.h"

#ifndef Z_ADDREF_P
#define Z_ADDREF_P(z) ((z)->refcount++)
#endif

/* {{{ arginfo */
ZEND_BEGIN_ARG_INFO_EX(arginfo_void, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_dbus_object___call, 0, 0, 2)
	ZEND_ARG_INFO(0, function_name)
	ZEND_ARG_INFO(0, arguments)
ZEND_END_ARG_INFO()
/* }}} */

const zend_function_entry dbus_funcs_dbus[] = {
	PHP_ME(Dbus, __construct, arginfo_void, ZEND_ACC_CTOR|ZEND_ACC_PUBLIC)
	PHP_ME(Dbus, addWatch,    arginfo_void, ZEND_ACC_PUBLIC)
	PHP_ME(Dbus, waitLoop,    arginfo_void, ZEND_ACC_PUBLIC)
	PHP_ME(Dbus, requestName, arginfo_void, ZEND_ACC_PUBLIC)
	PHP_ME(Dbus, registerObject, arginfo_void, ZEND_ACC_PUBLIC)
	PHP_ME(Dbus, createProxy, arginfo_void, ZEND_ACC_PUBLIC)
	PHP_FE_END
};

const zend_function_entry dbus_funcs_dbus_object[] = {
	PHP_ME(DbusObject, __construct, arginfo_void, ZEND_ACC_CTOR|ZEND_ACC_PRIVATE)
	PHP_ME(DbusObject, __call,      arginfo_dbus_object___call, ZEND_ACC_PUBLIC)
	PHP_FE_END
};

const zend_function_entry dbus_funcs_dbus_signal[] = {
	PHP_ME(DbusSignal, __construct, arginfo_void, ZEND_ACC_CTOR|ZEND_ACC_PUBLIC)
	PHP_ME(DbusSignal, matches,     arginfo_void, ZEND_ACC_PUBLIC)
	PHP_ME(DbusSignal, getData,     arginfo_void, ZEND_ACC_PUBLIC)
	PHP_ME(DbusSignal, send,        arginfo_void, ZEND_ACC_PUBLIC)
	PHP_FE_END
};

const zend_function_entry dbus_funcs_dbus_array[] = {
	PHP_ME(DbusArray, __construct, arginfo_void, ZEND_ACC_CTOR|ZEND_ACC_PUBLIC)
	PHP_ME(DbusArray, getData,     arginfo_void, ZEND_ACC_PUBLIC)
	PHP_FE_END
};

const zend_function_entry dbus_funcs_dbus_dict[] = {
	PHP_ME(DbusDict, __construct, arginfo_void, ZEND_ACC_CTOR|ZEND_ACC_PUBLIC)
	PHP_ME(DbusDict, getData,     arginfo_void, ZEND_ACC_PUBLIC)
	PHP_FE_END
};

#define PHP_DBUS_INT_WRAPPER_DEF(s,t) \
	const zend_function_entry dbus_funcs_dbus_##s[] = { \
		PHP_ME(Dbus##t, __construct, arginfo_void, ZEND_ACC_CTOR|ZEND_ACC_PUBLIC) \
		PHP_FE_END \
	};

PHP_DBUS_INT_WRAPPER_DEF(byte, Byte);
PHP_DBUS_INT_WRAPPER_DEF(bool, Bool);
PHP_DBUS_INT_WRAPPER_DEF(int16, Int16);
PHP_DBUS_INT_WRAPPER_DEF(uint16,UInt16);
PHP_DBUS_INT_WRAPPER_DEF(int32, Int32);
PHP_DBUS_INT_WRAPPER_DEF(uint32,UInt32);
PHP_DBUS_INT_WRAPPER_DEF(int64, Int64);
PHP_DBUS_INT_WRAPPER_DEF(uint64,UInt64);
PHP_DBUS_INT_WRAPPER_DEF(double, Double);

const zend_function_entry dbus_funcs_dbus_variant[] = {
	PHP_ME(DbusVariant, __construct, arginfo_void, ZEND_ACC_CTOR|ZEND_ACC_PUBLIC)
	PHP_ME(DbusVariant, getData,     arginfo_void, ZEND_ACC_PUBLIC)
	PHP_FE_END
};

const zend_function_entry dbus_funcs_dbus_set[] = {
	PHP_ME(DbusSet, __construct, arginfo_void, ZEND_ACC_CTOR|ZEND_ACC_PUBLIC)
	PHP_ME(DbusSet, getData,     arginfo_void, ZEND_ACC_PUBLIC)
	PHP_FE_END
};

const zend_function_entry dbus_funcs_dbus_struct[] = {
	PHP_ME(DbusStruct, __construct, arginfo_void, ZEND_ACC_CTOR|ZEND_ACC_PUBLIC)
	PHP_ME(DbusStruct, getData,     arginfo_void, ZEND_ACC_PUBLIC)
	PHP_FE_END
};

const zend_function_entry dbus_funcs_dbus_object_path[] = {
	PHP_ME(DbusObjectPath, __construct, arginfo_void, ZEND_ACC_CTOR|ZEND_ACC_PUBLIC)
	PHP_ME(DbusObjectPath, getData,     arginfo_void, ZEND_ACC_PUBLIC)
	PHP_FE_END
};

#define PHP_DBUS_CALL_FUNCTION     1
#define PHP_DBUS_RETURN_FUNCTION   2

static void dbus_register_classes(TSRMLS_D);
static zval * dbus_instantiate(zend_class_entry *pce, zval *object TSRMLS_DC);
static int php_dbus_handle_reply(zval *return_value, DBusMessage *msg, int always_array TSRMLS_DC);
static int php_dbus_append_parameters(DBusMessage *msg, zval *data, xmlNode *inXml, int type TSRMLS_DC);
static int php_dbus_fetch_child_type(zval *child TSRMLS_DC);
/* }}} */

ZEND_DECLARE_MODULE_GLOBALS(dbus)
static PHP_GINIT_FUNCTION(dbus);

/* {{{ INI Settings */
PHP_INI_BEGIN()
PHP_INI_END()
/* }}} */

#if PHP_VERSION_ID < 50300
#define dbus_set_error_handling php_set_error_handling
#else
static inline void dbus_set_error_handling(error_handling_t error_handling, zend_class_entry *exception_class TSRMLS_DC)
{
	    zend_replace_error_handling(error_handling, exception_class, NULL TSRMLS_CC);
}
#endif

zend_class_entry *dbus_ce_dbus, *dbus_ce_dbus_object, *dbus_ce_dbus_signal;
zend_class_entry *dbus_ce_dbus_array, *dbus_ce_dbus_dict, *dbus_ce_dbus_variant;
zend_class_entry *dbus_ce_dbus_variant, *dbus_ce_dbus_set, *dbus_ce_dbus_struct;
zend_class_entry *dbus_ce_dbus_object_path;
zend_class_entry *dbus_ce_dbus_exception, *dbus_ce_dbus_exception_service, *dbus_ce_dbus_exception_method;

static zend_object_handlers dbus_object_handlers_dbus, dbus_object_handlers_dbus_object;
static zend_object_handlers dbus_object_handlers_dbus_signal;
static zend_object_handlers dbus_object_handlers_dbus_array, dbus_object_handlers_dbus_dict;
static zend_object_handlers dbus_object_handlers_dbus_variant, dbus_object_handlers_dbus_set;
static zend_object_handlers dbus_object_handlers_dbus_struct, dbus_object_handlers_dbus_object_path;

#define PHP_DBUS_SIGNAL_IN  1
#define PHP_DBUS_SIGNAL_OUT 2

typedef struct _php_dbus_obj {
	DBusConnection *con;
	int             useIntrospection;
	HashTable       objects; /* A hash with all the registered objects that can be called */
	zend_object     std;
} php_dbus_obj;

typedef struct _php_dbus_object_obj {
	php_dbus_obj    *dbus;
	char            *destination;
	char            *path;
	char            *interface;
	xmlDocPtr        introspect_xml_doc;
	xmlNode         *introspect_xml;
	zend_object     std;
} php_dbus_object_obj;

typedef struct _php_dbus_signal_obj {
	php_dbus_obj    *dbus;
	DBusMessage     *msg;
	char            *object;
	char            *interface;
	char            *signal;
	int              direction;
	zend_object     std;
} php_dbus_signal_obj;

typedef struct _php_dbus_array_obj {
	long             type;
	char            *signature;
	zval            *elements;
	zend_object     std;
} php_dbus_array_obj;

typedef struct _php_dbus_dict_obj {
	long             type;
	char            *signature;
	zval            *elements;
	zend_object     std;
} php_dbus_dict_obj;

typedef struct _php_dbus_variant_obj {
	zval            *data;
	char            *signature;
	zend_object     std;
} php_dbus_variant_obj;

typedef struct _php_dbus_set_obj {
	int              element_count;
	zval           **elements;
	zend_object     std;
} php_dbus_set_obj;

typedef struct _php_dbus_struct_obj {
	zval            *elements;
	zend_object     std;
} php_dbus_struct_obj;

typedef struct _php_dbus_object_path_obj {
	char            *path;
	zend_object     std;
} php_dbus_object_path_obj;

#define PHP_DBUS_SETUP_TYPE_OBJ(t,dt) \
	zend_class_entry *dbus_ce_dbus_##t; \
	static zend_object_handlers dbus_object_handlers_dbus_##t; \
	typedef struct _php_dbus_##t##_obj { \
		dt               data; \
		zend_object      std; \
	} php_dbus_##t##_obj;

PHP_DBUS_SETUP_TYPE_OBJ(byte,unsigned char);
PHP_DBUS_SETUP_TYPE_OBJ(bool,dbus_bool_t);
PHP_DBUS_SETUP_TYPE_OBJ(int16,dbus_int16_t);
PHP_DBUS_SETUP_TYPE_OBJ(uint16,dbus_uint16_t);
PHP_DBUS_SETUP_TYPE_OBJ(int32,dbus_int32_t);
PHP_DBUS_SETUP_TYPE_OBJ(uint32,dbus_uint32_t);
PHP_DBUS_SETUP_TYPE_OBJ(int64,dbus_int64_t);
PHP_DBUS_SETUP_TYPE_OBJ(uint64,dbus_uint64_t);
PHP_DBUS_SETUP_TYPE_OBJ(double,double);

static void dbus_object_free_storage_dbus(void *object TSRMLS_DC);
static void dbus_object_free_storage_dbus_object(void *object TSRMLS_DC);
static void dbus_object_free_storage_dbus_signal(void *object TSRMLS_DC);
static void dbus_object_free_storage_dbus_array(void *object TSRMLS_DC);
static void dbus_object_free_storage_dbus_dict(void *object TSRMLS_DC);
static void dbus_object_free_storage_dbus_variant(void *object TSRMLS_DC);
static void dbus_object_free_storage_dbus_set(void *object TSRMLS_DC);
static void dbus_object_free_storage_dbus_struct(void *object TSRMLS_DC);
static void dbus_object_free_storage_dbus_object_path(void *object TSRMLS_DC);

static zend_object* dbus_object_new_dbus(zend_class_entry *class_type TSRMLS_DC);
static zend_object* dbus_object_new_dbus_object(zend_class_entry *class_type TSRMLS_DC);
static zend_object* dbus_object_new_dbus_signal(zend_class_entry *class_type TSRMLS_DC);
static zend_object* dbus_object_new_dbus_array(zend_class_entry *class_type TSRMLS_DC);
static zend_object* dbus_object_new_dbus_dict(zend_class_entry *class_type TSRMLS_DC);
static zend_object* dbus_object_new_dbus_variant(zend_class_entry *class_type TSRMLS_DC);
static zend_object* dbus_object_new_dbus_set(zend_class_entry *class_type TSRMLS_DC);
static zend_object* dbus_object_new_dbus_struct(zend_class_entry *class_type TSRMLS_DC);
static zend_object* dbus_object_new_dbus_object_path(zend_class_entry *class_type TSRMLS_DC);

static HashTable *dbus_byte_get_properties(zval *object TSRMLS_DC);
static HashTable *dbus_bool_get_properties(zval *object TSRMLS_DC);
static HashTable *dbus_int16_get_properties(zval *object TSRMLS_DC);
static HashTable *dbus_uint16_get_properties(zval *object TSRMLS_DC);
static HashTable *dbus_int32_get_properties(zval *object TSRMLS_DC);
static HashTable *dbus_uint32_get_properties(zval *object TSRMLS_DC);
static HashTable *dbus_int64_get_properties(zval *object TSRMLS_DC);
static HashTable *dbus_uint64_get_properties(zval *object TSRMLS_DC);
static HashTable *dbus_double_get_properties(zval *object TSRMLS_DC);

static HashTable *dbus_array_get_properties(zval *object TSRMLS_DC);
static HashTable *dbus_dict_get_properties(zval *object TSRMLS_DC);
static HashTable *dbus_variant_get_properties(zval *object TSRMLS_DC);
static HashTable *dbus_set_get_properties(zval *object TSRMLS_DC);
static HashTable *dbus_struct_get_properties(zval *object TSRMLS_DC);
static HashTable *dbus_object_path_get_properties(zval *object TSRMLS_DC);

static int dbus_variant_initialize(php_dbus_variant_obj *dbusobj, zval *data, char *signature TSRMLS_DC);

#define PHP_DBUS_FORWARD_DECL_TYPE_FUNCS(t) \
	static void dbus_object_free_storage_dbus_##t(void *object TSRMLS_DC); \
	static zend_object* dbus_object_new_dbus_##t(zend_class_entry *class_type TSRMLS_DC);

PHP_DBUS_FORWARD_DECL_TYPE_FUNCS(byte);
PHP_DBUS_FORWARD_DECL_TYPE_FUNCS(bool);
PHP_DBUS_FORWARD_DECL_TYPE_FUNCS(int16);
PHP_DBUS_FORWARD_DECL_TYPE_FUNCS(uint16);
PHP_DBUS_FORWARD_DECL_TYPE_FUNCS(int32);
PHP_DBUS_FORWARD_DECL_TYPE_FUNCS(uint32);
PHP_DBUS_FORWARD_DECL_TYPE_FUNCS(int64);
PHP_DBUS_FORWARD_DECL_TYPE_FUNCS(uint64);
PHP_DBUS_FORWARD_DECL_TYPE_FUNCS(double);


#if PHP_VERSION_ID >= 80000
static zend_object* dbus_object_clone_dbus(zend_object *this_obj);
#else
static zend_object* dbus_object_clone_dbus(zval *this_ptr TSRMLS_DC);
static int dbus_object_compare_dbus(zval *d1, zval *d2 TSRMLS_DC);
#endif

static const zend_module_dep dbus_deps[] = {
	ZEND_MOD_REQUIRED("libxml")
	ZEND_MOD_END
};

#ifdef COMPILE_DL_DBUS
ZEND_GET_MODULE(dbus)
#endif

/* {{{ Module struct */
zend_module_entry dbus_module_entry = {
	STANDARD_MODULE_HEADER_EX,
	NULL,
	dbus_deps,
	"dbus",                     /* extension name */
	NULL,                       /* function list */
	PHP_MINIT(dbus),            /* process startup */
	PHP_MSHUTDOWN(dbus),        /* process shutdown */
	PHP_RINIT(dbus),            /* request startup */
	PHP_RSHUTDOWN(dbus),        /* request shutdown */
	PHP_MINFO(dbus),            /* extension info */
	PHP_DBUS_VERSION,           /* extension version */
	PHP_MODULE_GLOBALS(dbus),   /* globals descriptor */
	PHP_GINIT(dbus),            /* globals ctor */
	NULL,                       /* globals dtor */
	NULL,                       /* post deactivate */
	STANDARD_MODULE_PROPERTIES_EX
};
/* }}} */


/* {{{ PHP_GINIT_FUNCTION */
static PHP_GINIT_FUNCTION(dbus)
{
}
/* }}} */


/* {{{ PHP_RINIT_FUNCTION */
PHP_RINIT_FUNCTION(dbus)
{
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_RSHUTDOWN_FUNCTION */
PHP_RSHUTDOWN_FUNCTION(dbus)
{
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINIT_FUNCTION */
PHP_MINIT_FUNCTION(dbus)
{
	REGISTER_INI_ENTRIES();
	dbus_register_classes(TSRMLS_C);
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION */
PHP_MSHUTDOWN_FUNCTION(dbus)
{
	UNREGISTER_INI_ENTRIES();

	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION */
PHP_MINFO_FUNCTION(dbus)
{
	php_info_print_table_start();
	php_info_print_table_row(2, "Dbus support", "enabled");
	php_info_print_table_row(2, "Version", PHP_DBUS_VERSION);
	php_info_print_table_end();

	DISPLAY_INI_ENTRIES();
}
/* }}} */

#define PHP_DBUS_REGISTER_TYPE_CLASS(t,n) \
	{ \
		zend_class_entry ce_dbus_##t; \
		INIT_CLASS_ENTRY(ce_dbus_##t, n, dbus_funcs_dbus_##t); \
		ce_dbus_##t.create_object = dbus_object_new_dbus_##t; \
		dbus_ce_dbus_##t = zend_register_internal_class_ex(&ce_dbus_##t, NULL); \
		memcpy(&dbus_object_handlers_dbus_##t, zend_get_std_object_handlers(), sizeof(zend_object_handlers)); \
		dbus_object_handlers_dbus_##t.get_properties = dbus_##t##_get_properties; \
	}

static void dbus_register_classes(TSRMLS_D)
{
	zend_class_entry ce_dbus, ce_dbus_object, ce_dbus_array, ce_dbus_dict;
	zend_class_entry ce_dbus_variant, ce_dbus_signal, ce_dbus_set, ce_dbus_struct;
	zend_class_entry ce_dbus_object_path;
	zend_class_entry ce_dbus_exception, ce_dbus_exception_service, ce_dbus_exception_method;

	INIT_CLASS_ENTRY(ce_dbus, "Dbus", dbus_funcs_dbus);
	ce_dbus.create_object = dbus_object_new_dbus;
	dbus_ce_dbus = zend_register_internal_class_ex(&ce_dbus, NULL);
	memcpy(&dbus_object_handlers_dbus, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	dbus_object_handlers_dbus.clone_obj = dbus_object_clone_dbus;
#if PHP_VERSION_ID < 80000
	dbus_object_handlers_dbus.compare_objects = dbus_object_compare_dbus;
#else
	dbus_object_handlers_dbus.compare = zend_objects_not_comparable;
#endif

	zend_declare_class_constant_long(dbus_ce_dbus, "BYTE", sizeof("BYTE")-1, DBUS_TYPE_BYTE TSRMLS_CC);
	zend_declare_class_constant_long(dbus_ce_dbus, "BOOLEAN", sizeof("BOOLEAN")-1, DBUS_TYPE_BOOLEAN TSRMLS_CC);
	zend_declare_class_constant_long(dbus_ce_dbus, "INT16", sizeof("INT16")-1, DBUS_TYPE_INT16 TSRMLS_CC);
	zend_declare_class_constant_long(dbus_ce_dbus, "UINT16", sizeof("UINT16")-1, DBUS_TYPE_UINT16 TSRMLS_CC);
	zend_declare_class_constant_long(dbus_ce_dbus, "INT32", sizeof("INT32")-1, DBUS_TYPE_INT32 TSRMLS_CC);
	zend_declare_class_constant_long(dbus_ce_dbus, "UINT32", sizeof("UINT32")-1, DBUS_TYPE_UINT32 TSRMLS_CC);
	zend_declare_class_constant_long(dbus_ce_dbus, "INT64", sizeof("INT64")-1, DBUS_TYPE_INT64 TSRMLS_CC);
	zend_declare_class_constant_long(dbus_ce_dbus, "UINT64", sizeof("UINT64")-1, DBUS_TYPE_UINT64 TSRMLS_CC);
	zend_declare_class_constant_long(dbus_ce_dbus, "DOUBLE", sizeof("DOUBLE")-1, DBUS_TYPE_DOUBLE TSRMLS_CC);
	zend_declare_class_constant_long(dbus_ce_dbus, "STRING", sizeof("STRING")-1, DBUS_TYPE_STRING TSRMLS_CC);
	zend_declare_class_constant_long(dbus_ce_dbus, "ARRAY", sizeof("ARRAY")-1, DBUS_TYPE_ARRAY TSRMLS_CC);
	zend_declare_class_constant_long(dbus_ce_dbus, "VARIANT", sizeof("VARIANT")-1, DBUS_TYPE_VARIANT TSRMLS_CC);
	zend_declare_class_constant_long(dbus_ce_dbus, "STRUCT", sizeof("STRUCT")-1, DBUS_TYPE_STRUCT TSRMLS_CC);
	zend_declare_class_constant_long(dbus_ce_dbus, "OBJECT_PATH", sizeof("OBJECT_PATH")-1, DBUS_TYPE_OBJECT_PATH TSRMLS_CC);

	zend_declare_class_constant_long(dbus_ce_dbus, "BUS_SESSION", sizeof("BUS_SESSION")-1, DBUS_BUS_SESSION TSRMLS_CC);
	zend_declare_class_constant_long(dbus_ce_dbus, "BUS_SYSTEM", sizeof("BUS_SYSTEM")-1, DBUS_BUS_SYSTEM TSRMLS_CC);

	INIT_CLASS_ENTRY(ce_dbus_exception, "DbusException", NULL);
	dbus_ce_dbus_exception = zend_register_internal_class_ex(&ce_dbus_exception, zend_exception_get_default(TSRMLS_C));
	dbus_ce_dbus_exception->ce_flags |= ZEND_ACC_FINAL;

	INIT_CLASS_ENTRY(ce_dbus_exception_service, "DbusExceptionServiceUnknown", NULL);
	dbus_ce_dbus_exception_service = zend_register_internal_class_ex(&ce_dbus_exception_service, zend_exception_get_default(TSRMLS_C));
	dbus_ce_dbus_exception_service->ce_flags |= ZEND_ACC_FINAL;

	INIT_CLASS_ENTRY(ce_dbus_exception_method, "DbusExceptionUnknownMethod", NULL);
	dbus_ce_dbus_exception_method = zend_register_internal_class_ex(&ce_dbus_exception_method, zend_exception_get_default(TSRMLS_C));
	dbus_ce_dbus_exception_method->ce_flags |= ZEND_ACC_FINAL;

	INIT_CLASS_ENTRY(ce_dbus_object, "DbusObject", dbus_funcs_dbus_object);
	ce_dbus_object.create_object = dbus_object_new_dbus_object;
	dbus_ce_dbus_object = zend_register_internal_class_ex(&ce_dbus_object, NULL);
	memcpy(&dbus_object_handlers_dbus_object, zend_get_std_object_handlers(), sizeof(zend_object_handlers));

	INIT_CLASS_ENTRY(ce_dbus_signal, "DbusSignal", dbus_funcs_dbus_signal);
	ce_dbus_signal.create_object = dbus_object_new_dbus_signal;
	dbus_ce_dbus_signal = zend_register_internal_class_ex(&ce_dbus_signal, NULL);
	memcpy(&dbus_object_handlers_dbus_signal, zend_get_std_object_handlers(), sizeof(zend_object_handlers));

	INIT_CLASS_ENTRY(ce_dbus_array, "DbusArray", dbus_funcs_dbus_array);
	ce_dbus_array.create_object = dbus_object_new_dbus_array;
	dbus_ce_dbus_array = zend_register_internal_class_ex(&ce_dbus_array, NULL);
	memcpy(&dbus_object_handlers_dbus_array, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	dbus_object_handlers_dbus_array.get_properties = dbus_array_get_properties;

	INIT_CLASS_ENTRY(ce_dbus_dict, "DbusDict", dbus_funcs_dbus_dict);
	ce_dbus_dict.create_object = dbus_object_new_dbus_dict;
	dbus_ce_dbus_dict = zend_register_internal_class_ex(&ce_dbus_dict, NULL);
	memcpy(&dbus_object_handlers_dbus_dict, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	dbus_object_handlers_dbus_dict.get_properties = dbus_dict_get_properties;

	INIT_CLASS_ENTRY(ce_dbus_variant, "DbusVariant", dbus_funcs_dbus_variant);
	ce_dbus_variant.create_object = dbus_object_new_dbus_variant;
	dbus_ce_dbus_variant = zend_register_internal_class_ex(&ce_dbus_variant, NULL);
	memcpy(&dbus_object_handlers_dbus_variant, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	dbus_object_handlers_dbus_variant.get_properties = dbus_variant_get_properties;

	INIT_CLASS_ENTRY(ce_dbus_set, "DbusSet", dbus_funcs_dbus_set);
	ce_dbus_set.create_object = dbus_object_new_dbus_set;
	dbus_ce_dbus_set = zend_register_internal_class_ex(&ce_dbus_set, NULL);
	memcpy(&dbus_object_handlers_dbus_set, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	dbus_object_handlers_dbus_set.get_properties = dbus_set_get_properties;

	INIT_CLASS_ENTRY(ce_dbus_struct, "DbusStruct", dbus_funcs_dbus_struct);
	ce_dbus_struct.create_object = dbus_object_new_dbus_struct;
	dbus_ce_dbus_struct = zend_register_internal_class_ex(&ce_dbus_struct, NULL);
	memcpy(&dbus_object_handlers_dbus_struct, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	dbus_object_handlers_dbus_struct.get_properties = dbus_struct_get_properties;

	INIT_CLASS_ENTRY(ce_dbus_object_path, "DbusObjectPath", dbus_funcs_dbus_object_path);
	ce_dbus_object_path.create_object = dbus_object_new_dbus_object_path;
	dbus_ce_dbus_object_path = zend_register_internal_class_ex(&ce_dbus_object_path, NULL);
	memcpy(&dbus_object_handlers_dbus_object_path, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	dbus_object_handlers_dbus_object_path.get_properties = dbus_object_path_get_properties;

	PHP_DBUS_REGISTER_TYPE_CLASS(byte, "DbusByte");
	PHP_DBUS_REGISTER_TYPE_CLASS(bool, "DbusBool");
	PHP_DBUS_REGISTER_TYPE_CLASS(int16, "DbusInt16");
	PHP_DBUS_REGISTER_TYPE_CLASS(uint16, "DbusUInt16");
	PHP_DBUS_REGISTER_TYPE_CLASS(int32, "DbusInt32");
	PHP_DBUS_REGISTER_TYPE_CLASS(uint32, "DbusUInt32");
	PHP_DBUS_REGISTER_TYPE_CLASS(int64, "DbusInt64");
	PHP_DBUS_REGISTER_TYPE_CLASS(uint64, "DbusUInt64");
	PHP_DBUS_REGISTER_TYPE_CLASS(double, "DbusDouble");
}

static inline zend_object* dbus_object_new_dbus_ex(zend_class_entry *class_type, php_dbus_obj **ptr TSRMLS_DC)
{
	php_dbus_obj *intern;
	DBUS_ZEND_OBJECT_INIT_RETURN(intern, class_type, dbus);
}

static zend_object* dbus_object_new_dbus(zend_class_entry *class_type TSRMLS_DC)
{
	return dbus_object_new_dbus_ex(class_type, NULL TSRMLS_CC);
}

#if PHP_VERSION_ID >= 80000
static zend_object* dbus_object_clone_dbus(zend_object *this_obj)
{
	zend_object* new_ov;

	zend_object *new_obj = NULL;
	zend_objects_clone_members(new_obj, this_obj);
	new_ov = new_obj;

	return new_ov;
}
#else
static zend_object* dbus_object_clone_dbus(zval *this_ptr TSRMLS_DC)
{
	zend_object* new_ov;

	zend_object *new_obj = NULL, *old_obj = NULL;
	old_obj = Z_OBJ_P(this_ptr);
	zend_objects_clone_members(new_obj, old_obj);
	new_ov = new_obj;

	return new_ov;
}

static int dbus_object_compare_dbus(zval *d1, zval *d2 TSRMLS_DC)
{
	return 0;
}
#endif

static void dbus_object_free_storage_dbus(void *object TSRMLS_DC)
{
	php_dbus_obj *intern = DBUS_ZEND_ZOBJ_TO_OBJ(object, php_dbus_obj);

	if (intern->con) {
		dbus_connection_unref(intern->con);
	}

	DBUS_ZEND_HASH_DESTROY_CHECK(intern->objects);
	zend_object_std_dtor(&intern->std);
}

/* DBUS Object*/
static inline zend_object* dbus_object_new_dbus_object_ex(zend_class_entry *class_type, php_dbus_object_obj **ptr TSRMLS_DC)
{
	php_dbus_object_obj *intern;
	DBUS_ZEND_OBJECT_INIT_RETURN(intern, class_type, dbus_object);
}

static zend_object* dbus_object_new_dbus_object(zend_class_entry *class_type TSRMLS_DC)
{
	return dbus_object_new_dbus_object_ex(class_type, NULL TSRMLS_CC);
}

static void dbus_object_free_storage_dbus_object(void *object TSRMLS_DC)
{
	php_dbus_object_obj *intern = DBUS_ZEND_ZOBJ_TO_OBJ(object, php_dbus_object_obj);

	xmlFreeDoc(intern->introspect_xml_doc);
	if (intern->destination) {
		efree(intern->destination);
	}
	if (intern->path) {
		efree(intern->path);
	}
	if (intern->interface) {
		efree(intern->interface);
	}
	zend_object_std_dtor(&intern->std);
}

/* DBUS Signal */
static inline zend_object* dbus_object_new_dbus_signal_ex(zend_class_entry *class_type, php_dbus_signal_obj **ptr TSRMLS_DC)
{
	php_dbus_signal_obj *intern;
	DBUS_ZEND_OBJECT_INIT_RETURN(intern, class_type, dbus_signal);
}

static zend_object* dbus_object_new_dbus_signal(zend_class_entry *class_type TSRMLS_DC)
{
	return dbus_object_new_dbus_signal_ex(class_type, NULL TSRMLS_CC);
}

static void dbus_object_free_storage_dbus_signal(void *object TSRMLS_DC)
{
	php_dbus_signal_obj *intern = DBUS_ZEND_ZOBJ_TO_OBJ(object, php_dbus_signal_obj);

	dbus_message_unref(intern->msg);

	if (intern->interface) {
		efree(intern->interface);
	}
	if (intern->signal) {
		efree(intern->signal);
	}
	if (intern->object) {
		efree(intern->object);
	}
	zend_object_std_dtor(&intern->std);
}

/* DBUS Array*/
static inline zend_object* dbus_object_new_dbus_array_ex(zend_class_entry *class_type, php_dbus_array_obj **ptr TSRMLS_DC)
{
	php_dbus_array_obj *intern;
	DBUS_ZEND_OBJECT_INIT_RETURN(intern, class_type, dbus_array);
}

static zend_object* dbus_object_new_dbus_array(zend_class_entry *class_type TSRMLS_DC)
{
	return dbus_object_new_dbus_array_ex(class_type, NULL TSRMLS_CC);
}

static void dbus_object_free_storage_dbus_array(void *object TSRMLS_DC)
{
	php_dbus_array_obj *intern = DBUS_ZEND_ZOBJ_TO_OBJ(object, php_dbus_array_obj);

	if (intern->signature) {
		efree(intern->signature);
	}

	zend_object_std_dtor(&intern->std);
}

/* DBUS DICT */
static inline zend_object* dbus_object_new_dbus_dict_ex(zend_class_entry *class_type, php_dbus_dict_obj **ptr TSRMLS_DC)
{
	php_dbus_dict_obj *intern;
	DBUS_ZEND_OBJECT_INIT_RETURN(intern, class_type, dbus_dict);
}

static zend_object* dbus_object_new_dbus_dict(zend_class_entry *class_type TSRMLS_DC)
{
	return dbus_object_new_dbus_dict_ex(class_type, NULL TSRMLS_CC);
}

static void dbus_object_free_storage_dbus_dict(void *object TSRMLS_DC)
{
	php_dbus_dict_obj *intern = DBUS_ZEND_ZOBJ_TO_OBJ(object, php_dbus_dict_obj);

	if (intern->signature) {
		efree(intern->signature);
	}

	zend_object_std_dtor(&intern->std);
}

/* DBUS VARIANT */
static inline zend_object* dbus_object_new_dbus_variant_ex(zend_class_entry *class_type, php_dbus_variant_obj **ptr TSRMLS_DC)
{
	php_dbus_variant_obj *intern;
	DBUS_ZEND_OBJECT_INIT_RETURN(intern, class_type, dbus_variant);
}

static zend_object* dbus_object_new_dbus_variant(zend_class_entry *class_type TSRMLS_DC)
{
	return dbus_object_new_dbus_variant_ex(class_type, NULL TSRMLS_CC);
}

static void dbus_object_free_storage_dbus_variant(void *object TSRMLS_DC)
{
	php_dbus_variant_obj *intern = DBUS_ZEND_ZOBJ_TO_OBJ(object, php_dbus_variant_obj);

	if (intern->signature) {
		efree(intern->signature);
	}

	zend_object_std_dtor(&intern->std);
}

/* DBUS SET */
static inline zend_object* dbus_object_new_dbus_set_ex(zend_class_entry *class_type, php_dbus_set_obj **ptr TSRMLS_DC)
{
	php_dbus_set_obj *intern;
	DBUS_ZEND_OBJECT_INIT_RETURN(intern, class_type, dbus_set);
}

static zend_object* dbus_object_new_dbus_set(zend_class_entry *class_type TSRMLS_DC)
{
	return dbus_object_new_dbus_set_ex(class_type, NULL TSRMLS_CC);
}

static void dbus_object_free_storage_dbus_set(void *object TSRMLS_DC)
{
	php_dbus_set_obj *intern = DBUS_ZEND_ZOBJ_TO_OBJ(object, php_dbus_set_obj);

	if (intern->elements) {
		int i;

		for (i = 0; i < intern->element_count; i++) {
			if (intern->elements[i]) {
				zval_ptr_dtor(intern->elements[i]);
			}
		}
		efree(intern->elements);
	}

	zend_object_std_dtor(&intern->std);
}

/* DBUS Struct*/
static inline zend_object* dbus_object_new_dbus_struct_ex(zend_class_entry *class_type, php_dbus_struct_obj **ptr TSRMLS_DC)
{
	php_dbus_struct_obj *intern;
	DBUS_ZEND_OBJECT_INIT_RETURN(intern, class_type, dbus_struct);
}

static zend_object* dbus_object_new_dbus_struct(zend_class_entry *class_type TSRMLS_DC)
{
	return dbus_object_new_dbus_struct_ex(class_type, NULL TSRMLS_CC);
}

static void dbus_object_free_storage_dbus_struct(void *object TSRMLS_DC)
{
	php_dbus_struct_obj *intern = DBUS_ZEND_ZOBJ_TO_OBJ(object, php_dbus_struct_obj);

	zend_object_std_dtor(&intern->std);
}

/* DBUS Object Path*/
static inline zend_object* dbus_object_new_dbus_object_path_ex(zend_class_entry *class_type, php_dbus_object_path_obj **ptr TSRMLS_DC)
{
	php_dbus_object_path_obj *intern;
	DBUS_ZEND_OBJECT_INIT_RETURN(intern, class_type, dbus_object_path);
}

static zend_object* dbus_object_new_dbus_object_path(zend_class_entry *class_type TSRMLS_DC)
{
	return dbus_object_new_dbus_object_path_ex(class_type, NULL TSRMLS_CC);
}

static void dbus_object_free_storage_dbus_object_path(void *object TSRMLS_DC)
{
	php_dbus_object_path_obj *intern = DBUS_ZEND_ZOBJ_TO_OBJ(object, php_dbus_object_path_obj);

	zend_object_std_dtor(&intern->std);
}

#define PHP_DBUS_DEFINE_TYPE_OBJ_FUNCS(t,pt,pf) \
	static inline zend_object* dbus_object_new_dbus_##t##_ex(zend_class_entry *class_type, php_dbus_##t##_obj **ptr TSRMLS_DC) \
	{ \
		php_dbus_##t##_obj *intern; \
		DBUS_ZEND_OBJECT_INIT_RETURN(intern, class_type, dbus_##t); \
	} \
 \
	static zend_object* dbus_object_new_dbus_##t(zend_class_entry *class_type TSRMLS_DC) \
	{ \
		return dbus_object_new_dbus_##t##_ex(class_type, NULL TSRMLS_CC); \
	} \
 \
	static void dbus_object_free_storage_dbus_##t(void *object TSRMLS_DC) \
	{ \
		php_dbus_##t##_obj *intern = DBUS_ZEND_ZOBJ_TO_OBJ(object, php_dbus_##t##_obj); \
 \
		zend_object_std_dtor(&intern->std); \
	} \
 \
	static HashTable *dbus_##t##_get_properties(zval *object TSRMLS_DC) \
	{ \
		HashTable *props; \
		zval *zv; \
		php_dbus_##t##_obj *intern; \
 \
		DBUS_ZEND_GET_ZVAL_OBJECT(object, intern, php_dbus_##t##_obj); \
		DBUS_ZEND_MAKE_STD_ZVAL(zv); \
		DBUS_ZEND_ZVAL_TYPE_P(zv) = pt; \
		(*zv).value.pf = intern->data; \
 \
		props = intern->std.properties; \
 \
		DBUS_ZEND_HASH_UPDATE(props, "value", &zv); \
 \
		return props; \
	}


PHP_DBUS_DEFINE_TYPE_OBJ_FUNCS(byte,IS_LONG,lval);
PHP_DBUS_DEFINE_TYPE_OBJ_FUNCS(bool,IS_TRUE || IS_FALSE,lval);
PHP_DBUS_DEFINE_TYPE_OBJ_FUNCS(int16,IS_LONG,lval);
PHP_DBUS_DEFINE_TYPE_OBJ_FUNCS(uint16,IS_LONG,lval);
PHP_DBUS_DEFINE_TYPE_OBJ_FUNCS(int32,IS_LONG,lval);
PHP_DBUS_DEFINE_TYPE_OBJ_FUNCS(uint32,IS_LONG,lval);
PHP_DBUS_DEFINE_TYPE_OBJ_FUNCS(int64,IS_LONG,lval);
PHP_DBUS_DEFINE_TYPE_OBJ_FUNCS(uint64,IS_LONG,lval);
PHP_DBUS_DEFINE_TYPE_OBJ_FUNCS(double,IS_DOUBLE,dval);

/* Advanced Interface */
static zval * dbus_instantiate(zend_class_entry *pce, zval *object TSRMLS_DC)
{
	if (!object) {
		object = emalloc(sizeof(zval));
	}

	object_init_ex(object, pce);
	Z_TYPE_INFO_P(object) = IS_OBJECT_EX;
	Z_SET_REFCOUNT_P(object, 1);
	if (Z_ISREF_P(object))
		ZVAL_UNREF(object);

	return object;
}

static int dbus_initialize(php_dbus_obj *dbusobj, int type, int introspect TSRMLS_DC)
{
	DBusError err;
	DBusConnection *con;
	dbus_error_init(&err);

	con = dbus_bus_get(type, &err);
	if (dbus_error_is_set(&err)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Connection Error (%s)\n", err.message);
		dbus_error_free(&err);
	}
	dbusobj->con = con;
	dbusobj->useIntrospection = introspect;
	zend_hash_init(&dbusobj->objects, 16, NULL, NULL, 0);

	return 1;
}


/* {{{ proto Dbus::__construct([int type[, int useIntrospection]])
   Creates new Dbus object
*/
PHP_METHOD(Dbus, __construct)
{
	long type = DBUS_BUS_SESSION;
	long introspect = 0;
	php_dbus_obj *dbus = NULL;

	dbus_set_error_handling(EH_THROW, NULL TSRMLS_CC);
	if (SUCCESS == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|ll", &type, &introspect)) {
		DBUS_ZEND_GET_ZVAL_OBJECT(getThis(), dbus, php_dbus_obj);
		dbus_initialize(dbus, type, introspect TSRMLS_CC);
	}
	dbus_set_error_handling(EH_NORMAL, NULL TSRMLS_CC);
}
/* }}} */

static int dbus_object_initialize(php_dbus_object_obj *dbusobj, php_dbus_obj *dbus, char *destination, char *path, char *interface TSRMLS_DC)
{
	dbusobj->dbus = dbus;
	dbusobj->destination = estrdup(destination);
	dbusobj->path = estrdup(path);
	dbusobj->interface = interface ? estrdup(interface) : NULL;

	return 1;
}

static void php_dbus_introspect(php_dbus_object_obj *dbusobj, php_dbus_obj* dbus, char *dest, char *path, char *interface TSRMLS_DC)
{
	DBusMessage     *msg;
	DBusMessageIter  iter;
	DBusPendingCall* pending;
	long             bool_false = 0;

	msg = dbus_message_new_method_call(dest, path, "org.freedesktop.DBus.Introspectable", "Introspect");
	dbus_message_iter_init_append(msg, &iter);
	dbus_message_iter_append_basic(&iter, DBUS_TYPE_BOOLEAN, &bool_false);

	if (!dbus_connection_send_with_reply(dbus->con, msg, &pending, -1)) {
		dbus_message_unref(msg);
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Out of memory.");
	}

	if (NULL == pending) {
		dbus_message_unref(msg);
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Pending call null.");
	}
	dbus_connection_flush(dbus->con);

	/* free message */
	dbus_message_unref(msg);

	/* block until we recieve a reply */
	dbus_pending_call_block(pending);

	/* get the reply message */
	msg = dbus_pending_call_steal_reply(pending);

	if (msg == NULL || dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_METHOD_RETURN) {
		dbus_pending_call_unref(pending);
	} else {
		DBusMessageIter args;
		dbus_int64_t stat;

		if (!dbus_message_iter_init(msg, &args)) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "%s", dbus_message_get_error_name(msg));
			return;
		}
		dbus_message_iter_get_basic(&args, &stat);
		{
			dbusobj->introspect_xml_doc = xmlParseMemory((char*) stat, strlen((char*) stat));
			dbusobj->introspect_xml = php_dbus_find_interface_node(dbusobj->introspect_xml_doc, interface);
		}

		dbus_message_unref(msg);
		dbus_pending_call_unref(pending);
	}
}

/* {{{ proto DbusObject::__construct(Dbus $dbus, string destination, string path, string interface)
   Creates new DbusObject object
*/
PHP_METHOD(DbusObject, __construct)
{
	zval *object;
	php_dbus_obj *dbus;
	php_dbus_object_obj *dbus_object = NULL;
	char *destination, *path, *interface;
	size_t  destination_len, path_len, interface_len;

	dbus_set_error_handling(EH_THROW, NULL TSRMLS_CC);
	if (SUCCESS == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Osss!",
		&object, dbus_ce_dbus,
		&destination, &destination_len, &path, &path_len,
		&interface, &interface_len))
	{
		DBUS_ZEND_ADDREF_P(object);
		DBUS_ZEND_GET_ZVAL_OBJECT(object, dbus, php_dbus_obj);
		DBUS_ZEND_GET_ZVAL_OBJECT(getThis(), dbus_object, php_dbus_object_obj);
		dbus_object_initialize(dbus_object, dbus, destination, path, interface TSRMLS_CC);
		if (dbus->useIntrospection) {
			php_dbus_introspect(dbus_object, dbus, destination, path, interface TSRMLS_CC);
		}
	}
	dbus_set_error_handling(EH_NORMAL, NULL TSRMLS_CC);
}
/* }}} */

/* {{{ proto Dbus::createProxy(string destination, string path, string interface)
   Creates new DbusObject object
*/
PHP_METHOD(Dbus, createProxy)
{
	zval *object = getThis();
	php_dbus_obj *dbus;
	php_dbus_object_obj *dbus_object;
	char *destination, *path, *interface;
	size_t destination_len, path_len, interface_len;

	dbus_set_error_handling(EH_THROW, NULL TSRMLS_CC);
	if (SUCCESS == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sss!",
		&destination, &destination_len, &path, &path_len,
		&interface, &interface_len))
	{
		DBUS_ZEND_GET_ZVAL_OBJECT(object, dbus, php_dbus_obj);
		DBUS_ZEND_ADDREF_P(object);
		dbus_instantiate(dbus_ce_dbus_object, return_value TSRMLS_CC);
		DBUS_ZEND_GET_ZVAL_OBJECT(return_value, dbus_object, php_dbus_object_obj);
		dbus_object_initialize(dbus_object, dbus, destination, path, interface TSRMLS_CC);
		if (dbus->useIntrospection) {
			php_dbus_introspect(dbus_object, dbus, destination, path, interface TSRMLS_CC);
		}
	}
	dbus_set_error_handling(EH_NORMAL, NULL TSRMLS_CC);
}
/* }}} */

static void php_dbus_accept_incoming_signal(DBusMessage *msg, zval **return_value TSRMLS_DC)
{
	php_dbus_signal_obj *signalobj;
	dbus_instantiate(dbus_ce_dbus_signal, *return_value TSRMLS_CC);
	DBUS_ZEND_GET_ZVAL_OBJECT(*return_value, signalobj, php_dbus_signal_obj);
	signalobj->msg = msg;
	signalobj->direction = PHP_DBUS_SIGNAL_IN;
}

static void php_dbus_do_error_message(php_dbus_obj *dbus, DBusMessage *msg, char *type, const char *message)
{
	DBusMessage *reply;

	/* it's a different kind of method, so send the
	 * "unknown method" error */
	dbus_uint32_t serial = 0;
	reply = dbus_message_new_error(msg, type, message);
	dbus_connection_send(dbus->con, reply, &serial);
	dbus_connection_flush(dbus->con);
	dbus_message_unref(reply);
}

static void
php_dbus_do_method_call(php_dbus_obj *dbus,
						DBusMessage *msg,
						char *class,
		                const char *member)
{
	HashTable *params_ar;
	int num_elems, element = 0;
	zval params, callback, *object = NULL;
	zval retval;
	zval *method_args = NULL;
	zval *param = NULL;
	DBusMessage *reply;

	php_dbus_handle_reply(&params, msg, 1);

	array_init(&callback);
	add_next_index_string(&callback, class);
	add_next_index_string(&callback, member);
	params_ar = HASH_OF(&params);
	if (params_ar) {
		num_elems = zend_hash_num_elements(params_ar);
		method_args = safe_emalloc(sizeof(zval), num_elems, 0);
		for (zend_hash_internal_pointer_reset(params_ar);
			(param = zend_hash_get_current_data(params_ar)) != NULL;
			zend_hash_move_forward(params_ar)
		) {
			method_args[element] = *param;
			element++;
		}
	} else {
		num_elems = 0;
		method_args = safe_emalloc(sizeof(zval *), num_elems, 0);
	}

#if PHP_VERSION_ID >= 80000
	if (call_user_function(EG(function_table), object, &callback, &retval, num_elems, method_args) == SUCCESS) {
#else
	if (call_user_function_ex(EG(function_table), object, &callback, &retval,
				              num_elems, method_args, 0, NULL) == SUCCESS) {
#endif
		if (!Z_ISUNDEF(retval)) {
			reply = dbus_message_new_method_return(msg);
			php_dbus_append_parameters(reply, &retval, NULL,
					                   PHP_DBUS_RETURN_FUNCTION TSRMLS_CC);

			if (!dbus_connection_send(dbus->con, reply, NULL)) {
				dbus_message_unref(msg);
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "Out of memory.");
			}

			dbus_connection_flush(dbus->con);

			/* free message */
			dbus_message_unref(reply);
		}
	} else {
		php_error_docref(NULL, E_WARNING, "Unable to call %s()", Z_STRVAL(callback));
	}
	efree(method_args);

}

static void php_dbus_accept_incoming_method_call(php_dbus_obj *dbus, DBusMessage *msg, zval **return_value TSRMLS_DC)
{
	char *key, *class;
	const char *interface;

	/* See if we can find a class mapping for this object */
	interface = dbus_message_get_interface(msg);
	spprintf(&key, 0, "%s:%s", dbus_message_get_path(msg), interface ? interface : "(null)");
	if (DBUS_ZEND_HASH_FIND_PTR_CHECK(&(dbus->objects), key, class)) {
		zend_class_entry *ce = NULL;

		/* We found a registered class, now lets see if this class is actually available */
		if ((ce = zend_lookup_class(zend_string_init(class, strlen(class), 0))) != NULL) {
			char *lcname;
			const char *member = dbus_message_get_member(msg);

			/* Now we have the class, we can see if the callback method exists */
			lcname = zend_str_tolower_dup(member, strlen(member));
			if (!DBUS_ZEND_HASH_EXISTS(&ce->function_table, lcname)) {
				/* If no method is found, we try to see whether we
				 * can do some introspection stuff for our built-in
				 * classes. */
				if (strcmp("introspect", lcname) == 0) {
				} else {
					php_dbus_do_error_message(dbus, msg, DBUS_ERROR_UNKNOWN_METHOD, member);
					efree(lcname);
				}
			} else {
				php_dbus_do_method_call(dbus, msg, class, member TSRMLS_CC);
			}
		} else {
			php_dbus_do_error_message(dbus, msg, DBUS_ERROR_UNKNOWN_METHOD, "call back class not found");
		}
	} else {
		php_dbus_do_error_message(dbus, msg, DBUS_ERROR_UNKNOWN_METHOD, "call back class not registered");
	}
}

/* {{{ proto Dbus::waitLoop()
   Checks for received signals or method calls.
*/
PHP_METHOD(Dbus, waitLoop)
{
	zval *object;
	long timeout = 0;
	DBusMessage *msg;
	php_dbus_obj *dbus;

	dbus_set_error_handling(EH_THROW, NULL TSRMLS_CC);
	if (FAILURE == zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(),
		"O|l", &object, dbus_ce_dbus, &timeout)) {
		RETURN_FALSE;
	}

	DBUS_ZEND_GET_ZVAL_OBJECT(object, dbus, php_dbus_obj);

	dbus_connection_read_write(dbus->con, timeout);
	msg = dbus_connection_pop_message(dbus->con);

	if (msg != NULL) {
		switch (dbus_message_get_type(msg)) {
			case DBUS_MESSAGE_TYPE_SIGNAL:
				php_dbus_accept_incoming_signal(msg, &return_value TSRMLS_CC);
				break;
			case DBUS_MESSAGE_TYPE_METHOD_CALL:
				php_dbus_accept_incoming_method_call(dbus, msg, &return_value TSRMLS_CC);
				break;
		}
	}
	dbus_set_error_handling(EH_NORMAL, NULL TSRMLS_CC);
}
/* }}} */

/* {{{ proto Dbus::requestName(string name)
   Requests a name to be assigned to this Dbus connection.
*/
PHP_METHOD(Dbus, requestName)
{
	zval *object;
	DBusError err;
	int ret;
	char* name;
	size_t name_len;
	php_dbus_obj *dbus;

	dbus_set_error_handling(EH_THROW, NULL TSRMLS_CC);
	if (FAILURE == zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(),
		"Os", &object, dbus_ce_dbus, &name, &name_len)) {
		RETURN_FALSE;
	}

	DBUS_ZEND_GET_ZVAL_OBJECT(object, dbus, php_dbus_obj);
	dbus_error_init(&err);

	/* request our name on the bus and check for errors */
	ret = dbus_bus_request_name(dbus->con, name, DBUS_NAME_FLAG_REPLACE_EXISTING, &err);
	if (DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER != ret) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Not Primary Owner (%d)\n", ret);
	}
	dbus_set_error_handling(EH_NORMAL, NULL TSRMLS_CC);
}
/* }}} */

/* {{{ proto Dbus::registerObject(string path, string interface, string class)
   Registers a callback class for the path, and with the specified interface
*/
PHP_METHOD(Dbus, registerObject)
{
	zval *object;
	char *path, *interface, *class;
	size_t path_len, interface_len, class_len;
	php_dbus_obj *dbus;
	char *key;

	dbus_set_error_handling(EH_THROW, NULL TSRMLS_CC);
	if (FAILURE == zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(),
		"Oss!s", &object, dbus_ce_dbus, &path, &path_len, &interface, &interface_len,
		&class, &class_len)) {
		RETURN_FALSE;
	}
	DBUS_ZEND_GET_ZVAL_OBJECT(object, dbus, php_dbus_obj);

	/* Create the key out of the path and interface */
	spprintf(&key, 0, "%s:%s", path, interface ? interface : "(null)");

	DBUS_ZEND_HASH_ADD_PTR(&(dbus->objects), key, estrdup(class), strlen(class)+1);
	dbus_set_error_handling(EH_NORMAL, NULL TSRMLS_CC);
}
/* }}} */


static int dbus_append_var(zval **val, DBusMessageIter *iter, char *type_hint TSRMLS_DC);

static int dbus_append_var_php_array(DBusMessageIter *iter, zval *array TSRMLS_DC)
{
	DBusMessageIter listiter;
	char type_string[2];
	int type = DBUS_TYPE_STRING;
	zval *entry = NULL;

	/* First we find the first element, to find the type that we need to set.
	 * We'll only consider elements of the type that the first element of the
	 * array has */
	zend_hash_internal_pointer_reset(Z_ARRVAL_P(array));
	if (DBUS_ZEND_HASH_GET_CURRENT_DATA_CHECK(Z_ARRVAL_P(array), entry)) {
		type = php_dbus_fetch_child_type(entry TSRMLS_CC);
	}

	type_string[0] = (char) type;
	type_string[1] = '\0';

	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, type_string, &listiter);

	zend_hash_internal_pointer_reset(Z_ARRVAL_P(array));
	while (DBUS_ZEND_HASH_GET_CURRENT_DATA_CHECK(Z_ARRVAL_P(array), entry)) {
		if (php_dbus_fetch_child_type(entry TSRMLS_CC) == type) {
			dbus_append_var(&entry, &listiter, NULL TSRMLS_CC);
		}
		zend_hash_move_forward(Z_ARRVAL_P(array));
	}
	dbus_message_iter_close_container(iter, &listiter);

	return 1;
}

static int dbus_append_var_array(DBusMessageIter *iter, php_dbus_array_obj *obj TSRMLS_DC)
{
	DBusMessageIter listiter;
	char *type_string;
	zval *entry;
	HashPosition pos;

	if (obj->signature) {
		type_string = ecalloc(1, 1 + strlen(obj->signature));
		strcpy(type_string, obj->signature);
	} else {
		type_string = emalloc(2);
		type_string[0] = (char) obj->type;
		type_string[1] = '\0';
	}

	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, type_string, &listiter);

	zend_hash_internal_pointer_reset_ex(Z_ARRVAL_P(obj->elements), &pos);
	while (DBUS_ZEND_HASH_GET_CURRENT_DATA_CHECK(Z_ARRVAL_P(obj->elements), entry)) {
		if (php_dbus_fetch_child_type(entry TSRMLS_CC) == obj->type) {
			dbus_append_var(&entry, &listiter, NULL TSRMLS_CC);
		}
		zend_hash_move_forward(Z_ARRVAL_P(obj->elements));
	}
	dbus_message_iter_close_container(iter, &listiter);

	return 1;
}

static int dbus_append_var_dict(DBusMessageIter *iter, php_dbus_dict_obj *obj TSRMLS_DC)
{
	DBusMessageIter listiter, dictiter;
	char *type_string;
	zval *entry;
	dbus_zend_hash_key_info key;
	ulong num_index;
	HashPosition pos;

	if (obj->signature) {
		if (strlen(obj->signature) < 4) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "%s", "Signature should be at least four characters");
			return 0;
		}
		if (obj->signature[0] != '{' || obj->signature[strlen(obj->signature)-1] != '}') {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "%s", "The signature needs to start with { and end with }");
			return 0;
		}
		if (obj->signature[1] != 's') {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "%s", "Only string keys are supported so far");
		}

		type_string = ecalloc(1, 1 + strlen(obj->signature));
		strcpy(type_string, obj->signature);
	} else {
		type_string = emalloc(5);
		type_string[0] = '{';
		type_string[1] = 's';
		type_string[2] = (char) obj->type;
		type_string[3] = '}';
		type_string[4] = '\0';
	}

	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, type_string, &listiter);

	zend_hash_internal_pointer_reset_ex(Z_ARRVAL_P(obj->elements), &pos);
	while (DBUS_ZEND_HASH_GET_CURRENT_DATA_CHECK(Z_ARRVAL_P(obj->elements), entry)) {
		DBUS_ZEND_HASH_GET_CURRENT_KEY_INFO(Z_ARRVAL_P(obj->elements), num_index, key);
		if (key.type == HASH_KEY_IS_STRING) {
			dbus_message_iter_open_container(&listiter, DBUS_TYPE_DICT_ENTRY, NULL, &dictiter);
			dbus_message_iter_append_basic(&dictiter, DBUS_TYPE_STRING, &(key.name));
			dbus_append_var(&entry, &dictiter, NULL TSRMLS_CC);
			dbus_message_iter_close_container(&listiter, &dictiter);
		}
		zend_hash_move_forward(Z_ARRVAL_P(obj->elements));
	}

	dbus_message_iter_close_container(iter, &listiter);

	return 1;
}

#define PHP_DBUS_MARSHAL_FIND_TYPE_CASE(t,s) \
	if (obj->ce == dbus_ce_dbus_##t) { \
		return DBUS_TYPE_##s; \
	}

static int php_dbus_fetch_child_type(zval *child TSRMLS_DC)
{
	zend_object *obj;

	switch (Z_TYPE_P(child)) {
		case IS_TRUE:
		case IS_FALSE:
			return DBUS_TYPE_BOOLEAN;
		case IS_LONG:
			return DBUS_TYPE_INT32;
		case IS_DOUBLE:
			return DBUS_TYPE_DOUBLE;
		case IS_STRING:
			return DBUS_TYPE_STRING;
		case IS_OBJECT:
			/* We need to check for DbusArray, DbusDict and DbusVariant and the DBus types */
			obj = Z_OBJ_P(child);
			PHP_DBUS_MARSHAL_FIND_TYPE_CASE(variant, VARIANT);
			PHP_DBUS_MARSHAL_FIND_TYPE_CASE(array, ARRAY);
			PHP_DBUS_MARSHAL_FIND_TYPE_CASE(struct, STRUCT);
			PHP_DBUS_MARSHAL_FIND_TYPE_CASE(object_path, OBJECT_PATH);
			PHP_DBUS_MARSHAL_FIND_TYPE_CASE(byte, BYTE);
			PHP_DBUS_MARSHAL_FIND_TYPE_CASE(bool, BOOLEAN);
			PHP_DBUS_MARSHAL_FIND_TYPE_CASE(int16, INT16);
			PHP_DBUS_MARSHAL_FIND_TYPE_CASE(uint16, UINT16);
			PHP_DBUS_MARSHAL_FIND_TYPE_CASE(int32, INT32);
			PHP_DBUS_MARSHAL_FIND_TYPE_CASE(uint32, UINT32);
			PHP_DBUS_MARSHAL_FIND_TYPE_CASE(int64, INT64);
			PHP_DBUS_MARSHAL_FIND_TYPE_CASE(uint64, UINT64);
			PHP_DBUS_MARSHAL_FIND_TYPE_CASE(double, DOUBLE);
	}

	return IS_NULL;
}

#define PHP_DBUS_MARSHAL_FIND_TYPE_CASE_AS_STRING(t,s) \
	if (obj->ce == dbus_ce_dbus_##t) { \
		return DBUS_TYPE_##s##_AS_STRING; \
	}

static char* php_dbus_fetch_child_type_as_string(zval *child TSRMLS_DC)
{
	zend_object     *obj;

	switch (Z_TYPE_P(child)) {
		case IS_TRUE:
		case IS_FALSE:
			return DBUS_TYPE_BOOLEAN_AS_STRING;
		case IS_LONG:
			return DBUS_TYPE_INT32_AS_STRING;
		case IS_DOUBLE:
			return DBUS_TYPE_DOUBLE_AS_STRING;
		case IS_STRING:
			return DBUS_TYPE_STRING_AS_STRING;
		case IS_OBJECT:
			/* We need to check for DbusArray, DbusDict and DbusVariant and the DBus types */
			obj = Z_OBJ_P(child);
			PHP_DBUS_MARSHAL_FIND_TYPE_CASE_AS_STRING(variant, VARIANT);
			PHP_DBUS_MARSHAL_FIND_TYPE_CASE_AS_STRING(array, ARRAY);
			PHP_DBUS_MARSHAL_FIND_TYPE_CASE_AS_STRING(struct, STRUCT);
			PHP_DBUS_MARSHAL_FIND_TYPE_CASE_AS_STRING(object_path, OBJECT_PATH);
			PHP_DBUS_MARSHAL_FIND_TYPE_CASE_AS_STRING(byte, BYTE);
			PHP_DBUS_MARSHAL_FIND_TYPE_CASE_AS_STRING(bool, BOOLEAN);
			PHP_DBUS_MARSHAL_FIND_TYPE_CASE_AS_STRING(int16, INT16);
			PHP_DBUS_MARSHAL_FIND_TYPE_CASE_AS_STRING(uint16, UINT16);
			PHP_DBUS_MARSHAL_FIND_TYPE_CASE_AS_STRING(int32, INT32);
			PHP_DBUS_MARSHAL_FIND_TYPE_CASE_AS_STRING(uint32, UINT32);
			PHP_DBUS_MARSHAL_FIND_TYPE_CASE_AS_STRING(int64, INT64);
			PHP_DBUS_MARSHAL_FIND_TYPE_CASE_AS_STRING(uint64, UINT64);
			PHP_DBUS_MARSHAL_FIND_TYPE_CASE_AS_STRING(double, DOUBLE);
	}

	return NULL;
}

static int dbus_append_var_variant(DBusMessageIter *iter, php_dbus_variant_obj *obj TSRMLS_DC)
{
	DBusMessageIter variant;
	char *type;

	if (!obj->signature) {
		type = php_dbus_fetch_child_type_as_string(obj->data TSRMLS_CC);
	} else {
		type = obj->signature;
	}
	dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, type, &variant );
	dbus_append_var(&(obj->data), &variant, NULL TSRMLS_CC);
	dbus_message_iter_close_container(iter, &variant);

	return 1;
}

static int dbus_append_var_set(DBusMessageIter *iter, php_dbus_set_obj *obj TSRMLS_DC)
{
	int   i;

	for (i = 0; i < obj->element_count; i++) {
		dbus_append_var(&(obj->elements[i]), iter, NULL TSRMLS_CC);
	}

	return 1;
}

static int dbus_append_var_struct(DBusMessageIter *iter, php_dbus_struct_obj *obj TSRMLS_DC)
{
	DBusMessageIter listiter;
	zval *entry;

	zend_hash_internal_pointer_reset(Z_ARRVAL_P(obj->elements));
	dbus_message_iter_open_container(iter, DBUS_TYPE_STRUCT, NULL, &listiter);
	while (DBUS_ZEND_HASH_GET_CURRENT_DATA_CHECK(Z_ARRVAL_P(obj->elements), entry)) {
		dbus_append_var(&entry, &listiter, NULL TSRMLS_CC);

		zend_hash_move_forward(Z_ARRVAL_P(obj->elements));
	}
	dbus_message_iter_close_container(iter, &listiter);

	return 1;
}

static int dbus_append_var_object_path(DBusMessageIter *iter, php_dbus_object_path_obj *obj TSRMLS_DC)
{
	dbus_message_iter_append_basic(iter, DBUS_TYPE_OBJECT_PATH, &obj->path);

	return 1;
}

#define PHP_DBUS_APPEND_TYPE_FUNC(t,dt) \
	static int dbus_append_var_##t(DBusMessageIter *iter, long val) \
	{ \
		dbus_message_iter_append_basic(iter, DBUS_TYPE_##dt, &val); \
		return 1; \
	}

PHP_DBUS_APPEND_TYPE_FUNC(byte, BYTE);
PHP_DBUS_APPEND_TYPE_FUNC(bool, BOOLEAN);
PHP_DBUS_APPEND_TYPE_FUNC(double, DOUBLE);
PHP_DBUS_APPEND_TYPE_FUNC(int16, INT16);
PHP_DBUS_APPEND_TYPE_FUNC(uint16, UINT16);
PHP_DBUS_APPEND_TYPE_FUNC(int32, INT32);
PHP_DBUS_APPEND_TYPE_FUNC(uint32, UINT32);
PHP_DBUS_APPEND_TYPE_FUNC(int64, INT64);
PHP_DBUS_APPEND_TYPE_FUNC(uint64, UINT64);

#define PHP_DBUS_MARSHAL_TO_DBUS_CASE(t) \
	if (obj->ce == dbus_ce_dbus_##t) { \
		php_dbus_##t##_obj *_val = \
			DBUS_ZEND_ZOBJ_TO_OBJ(obj, php_dbus_##t##_obj); \
		dbus_append_var_##t(iter, _val->data); \
	}

static int dbus_append_var(zval **val, DBusMessageIter *iter, char *type_hint TSRMLS_DC)
{
	zend_object     *obj;

	switch (Z_TYPE_P(*val)) {
		case IS_TRUE:
		case IS_FALSE: {
			dbus_bool_t bval = Z_TYPE_P(*val) == IS_TRUE ? 1 : 0;
			dbus_message_iter_append_basic(iter, DBUS_TYPE_BOOLEAN, &bval);
			break;
		}
		case IS_LONG: {
			char type = type_hint ? (char)type_hint[0] : 0;
			long lval = Z_LVAL_P(*val);
			switch (type)
			{
				case 'u':
					dbus_message_iter_append_basic(iter, DBUS_TYPE_UINT32, &lval);
					break;

				default:
					dbus_message_iter_append_basic(iter, DBUS_TYPE_INT32, &lval);
			}
			break;
		}
		case IS_DOUBLE: {
			double dval = Z_DVAL_P(*val);
			dbus_message_iter_append_basic(iter, DBUS_TYPE_DOUBLE, &dval);
			break;
		}
		case IS_STRING: {
			const char *sval = Z_STRVAL_P(*val);
			dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &sval);
			break;
		}
		case IS_ARRAY:
			dbus_append_var_php_array(iter, *val TSRMLS_CC);
			break;
		case IS_OBJECT:
			/* We need to check for DbusArray, DbusDict, DbusVariant, DbusSet and the DBus types */
			obj = Z_OBJ_P(*val);
			if (obj->ce == dbus_ce_dbus_array) {
				dbus_append_var_array(iter,
					DBUS_ZEND_ZOBJ_TO_OBJ(obj, php_dbus_array_obj) TSRMLS_CC);
			}
			if (obj->ce == dbus_ce_dbus_dict) {
				dbus_append_var_dict(iter,
					DBUS_ZEND_ZOBJ_TO_OBJ(obj, php_dbus_dict_obj) TSRMLS_CC);
			}
			if (obj->ce == dbus_ce_dbus_variant) {
				dbus_append_var_variant(iter,
					DBUS_ZEND_ZOBJ_TO_OBJ(obj, php_dbus_variant_obj) TSRMLS_CC);
			}
			if (obj->ce == dbus_ce_dbus_set) {
				dbus_append_var_set(iter,
					DBUS_ZEND_ZOBJ_TO_OBJ(obj, php_dbus_set_obj) TSRMLS_CC);
			}
			if (obj->ce == dbus_ce_dbus_struct) {
				dbus_append_var_struct(iter,
					DBUS_ZEND_ZOBJ_TO_OBJ(obj, php_dbus_struct_obj) TSRMLS_CC);
			}
			if (obj->ce == dbus_ce_dbus_object_path) {
				dbus_append_var_object_path(iter,
					DBUS_ZEND_ZOBJ_TO_OBJ(obj, php_dbus_object_path_obj) TSRMLS_CC);
			}
			PHP_DBUS_MARSHAL_TO_DBUS_CASE(byte);
			PHP_DBUS_MARSHAL_TO_DBUS_CASE(bool);
			PHP_DBUS_MARSHAL_TO_DBUS_CASE(int16);
			PHP_DBUS_MARSHAL_TO_DBUS_CASE(uint16);
			PHP_DBUS_MARSHAL_TO_DBUS_CASE(int32);
			PHP_DBUS_MARSHAL_TO_DBUS_CASE(uint32);
			PHP_DBUS_MARSHAL_TO_DBUS_CASE(int64);
			PHP_DBUS_MARSHAL_TO_DBUS_CASE(uint64);
			PHP_DBUS_MARSHAL_TO_DBUS_CASE(double);
	}

	return 1;
}

static int php_dbus_append_parameters(DBusMessage *msg, zval *data, xmlNode *inXml, int type TSRMLS_DC)
{
	DBusMessageIter dbus_args;
	HashPosition pos;
	zval *entry;
	int i;
	xmlNode **it = NULL;

	if (inXml) {
		it = &inXml->children;
	}

	dbus_message_iter_init_append(msg, &dbus_args);

	if (type == PHP_DBUS_CALL_FUNCTION) {
		zend_hash_internal_pointer_reset_ex(Z_ARRVAL_P(data), &pos);
		while (DBUS_ZEND_HASH_GET_CURRENT_DATA_CHECK_EX(Z_ARRVAL_P(data), entry, pos)) {
			char *sig = NULL;
			if (it) {
				it = php_dbus_get_next_sig(it, &sig);
			}
			dbus_append_var(&entry, &dbus_args, it ? sig : NULL TSRMLS_CC);
			zend_hash_move_forward_ex(Z_ARRVAL_P(data), &pos);
		}
	} else if (type == PHP_DBUS_RETURN_FUNCTION) {
		dbus_append_var(&data, &dbus_args, NULL TSRMLS_CC);
	}

	return 1;
}

static zval *php_dbus_to_zval(DBusMessageIter *args, zval **key TSRMLS_DC)
{
	zval *return_value;
	DBusMessageIter subiter;
	int arg_type = 0;

	DBUS_ZEND_ALLOC_ZVAL(return_value);

	*key = NULL;

	arg_type = dbus_message_iter_get_arg_type(args);
	switch (arg_type) {
		case DBUS_TYPE_ARRAY:
			{
				int init = 0;

				dbus_message_iter_recurse(args, &subiter);
				do {
					zval *new_key = NULL;
					php_dbus_dict_obj *dictobj;
					php_dbus_array_obj *arrayobj;
					zval *val = php_dbus_to_zval(&subiter, &new_key TSRMLS_CC);

					if (new_key) {
						if (!init) {
							dbus_instantiate(dbus_ce_dbus_dict, return_value TSRMLS_CC);
							DBUS_ZEND_GET_ZVAL_OBJECT(return_value, dictobj, php_dbus_dict_obj);
							dictobj->type = php_dbus_fetch_child_type(val TSRMLS_CC);
							DBUS_ZEND_ALLOC_ZVAL(dictobj->elements);
							array_init(dictobj->elements);
							init = 1;
						}

						if (val && Z_TYPE_P(new_key) == IS_STRING) {
							add_assoc_zval_ex(dictobj->elements, Z_STRVAL_P(new_key), Z_STRLEN_P(new_key), val);
						} else if (val && Z_TYPE_P(new_key) == IS_OBJECT) {
							zend_object *obj = Z_OBJ_P(new_key);
							if (obj->ce == dbus_ce_dbus_object_path) {
								php_dbus_object_path_obj *intern = DBUS_ZEND_ZOBJ_TO_OBJ(obj, php_dbus_object_path_obj);
								add_assoc_zval_ex(dictobj->elements, intern->path, strlen(intern->path), val);
							}
						} else if (val && Z_TYPE_P(new_key) == IS_LONG) {
							add_index_zval(dictobj->elements, Z_LVAL_P(new_key), val);
						}
					} else {
						if (!init) {
							dbus_instantiate(dbus_ce_dbus_array, return_value TSRMLS_CC);
							DBUS_ZEND_GET_ZVAL_OBJECT(return_value, arrayobj, php_dbus_array_obj);
							arrayobj->type = val ? php_dbus_fetch_child_type(val TSRMLS_CC) : DBUS_TYPE_INVALID;
							DBUS_ZEND_ALLOC_ZVAL(arrayobj->elements);
							array_init(arrayobj->elements);
							arrayobj->signature = estrdup(dbus_message_iter_get_signature(&subiter));
							init = 1;
						}

						if (val) {
							add_next_index_zval(arrayobj->elements, val);
						}
					}
				} while (dbus_message_iter_next(&subiter));
			}
			break;
		case DBUS_TYPE_DICT_ENTRY:
			{
				zval *new_key = NULL;

				dbus_message_iter_recurse(args, &subiter);

				*key = php_dbus_to_zval(&subiter, &new_key TSRMLS_CC);
				dbus_message_iter_next(&subiter);
				return_value = php_dbus_to_zval(&subiter, &new_key TSRMLS_CC);
			}
			break;
		case DBUS_TYPE_VARIANT:
			{
				zval *new_key = NULL, *val;
				php_dbus_variant_obj *variantobj;

				dbus_message_iter_recurse(args, &subiter);
				val = php_dbus_to_zval(&subiter, &new_key TSRMLS_CC);

				dbus_instantiate(dbus_ce_dbus_variant, return_value TSRMLS_CC);
				DBUS_ZEND_GET_ZVAL_OBJECT(return_value, variantobj, php_dbus_variant_obj);
				variantobj->data = val;
			}
			break;
		case DBUS_TYPE_STRUCT:
			{
				php_dbus_struct_obj *structobj;

				dbus_instantiate(dbus_ce_dbus_struct, return_value TSRMLS_CC);
				DBUS_ZEND_GET_ZVAL_OBJECT(return_value, structobj, php_dbus_struct_obj);
				DBUS_ZEND_ALLOC_ZVAL(structobj->elements);
				array_init(structobj->elements);

				dbus_message_iter_recurse(args, &subiter);
				do {
					zval *new_key = NULL;
					zval *val = php_dbus_to_zval(&subiter, &new_key TSRMLS_CC);
					add_next_index_zval(structobj->elements, val);
				} while (dbus_message_iter_next(&subiter));
			}
			break;
		case DBUS_TYPE_OBJECT_PATH:
			{
				php_dbus_object_path_obj *object_pathobj;
				dbus_int64_t stat;

				dbus_instantiate(dbus_ce_dbus_object_path, return_value TSRMLS_CC);
				DBUS_ZEND_GET_ZVAL_OBJECT(return_value, object_pathobj, php_dbus_object_path_obj);
				dbus_message_iter_get_basic(args, &stat);
				object_pathobj->path = estrdup((char*) stat);
			}
			break;
		case 0:
			return NULL;
			break;
		default:
			if (dbus_message_iter_get_arg_type(args) == DBUS_TYPE_DOUBLE) {
				double stat;
				dbus_message_iter_get_basic(args, &stat);
				RETVAL_DOUBLE((double) stat);
			} else {
				dbus_int64_t stat;
				dbus_message_iter_get_basic(args, &stat);
				switch (dbus_message_iter_get_arg_type(args)) {
					case DBUS_TYPE_BOOLEAN: RETVAL_BOOL((dbus_bool_t) stat);  break;
					case DBUS_TYPE_BYTE:    RETVAL_LONG((unsigned char) stat); break;
					case DBUS_TYPE_INT16:   RETVAL_LONG((dbus_int16_t) stat);  break;
					case DBUS_TYPE_UINT16:  RETVAL_LONG((dbus_uint16_t) stat); break;
					case DBUS_TYPE_INT32:   RETVAL_LONG((dbus_int32_t) stat);  break;
					case DBUS_TYPE_UINT32:  RETVAL_LONG((dbus_uint32_t) stat); break;
					case DBUS_TYPE_INT64:   RETVAL_LONG((dbus_int64_t) stat);  break;
					case DBUS_TYPE_UINT64:  RETVAL_LONG((dbus_uint64_t) stat); break;
					case DBUS_TYPE_STRING:
						RETVAL_STRING((char*) stat);
						break;
				}
			}
	}

	return return_value;
}

static int php_dbus_handle_reply(zval *return_value, DBusMessage *msg, int always_array TSRMLS_DC)
{
	zend_class_entry *exception_ce = NULL;
	DBusMessageIter   args;
	dbus_int64_t      stat;
	zval             *val;

	if (dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_ERROR) {
		const char* error_msg_str = dbus_message_get_error_name(msg);

		if (!strcmp(error_msg_str, DBUS_ERROR_SERVICE_UNKNOWN)) {
			exception_ce = dbus_ce_dbus_exception_service;
		} else if (!strcmp(error_msg_str, DBUS_ERROR_UNKNOWN_METHOD)) {
			exception_ce = dbus_ce_dbus_exception_method;
		} else {
			exception_ce = dbus_ce_dbus_exception;
		}

		dbus_set_error_handling(EH_THROW, exception_ce TSRMLS_CC);
		if (!dbus_message_iter_init(msg, &args)) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "%s", dbus_message_get_error_name(msg));
			return 0;
		}
		dbus_message_iter_get_basic(&args, &stat);
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "%s: %s", dbus_message_get_error_name(msg), (char*) stat);
		dbus_set_error_handling(EH_NORMAL, NULL TSRMLS_CC);
	}

	ZVAL_NULL(return_value);
	// No return values
	if (!dbus_message_iter_init(msg, &args)) {
		return 0;
	}

	// if we're setting up a method call to be called by PHP, we just need a normal array
	if (always_array) {
		array_init(return_value);
		do {
			zval *key = NULL;
			val = php_dbus_to_zval(&args, &key TSRMLS_CC);
			add_next_index_zval(return_value, val);
		} while (dbus_message_iter_next(&args));
	} else {
		if (dbus_message_iter_has_next(&args)) {
			// More than one return value
			php_dbus_set_obj *set;
			size_t i = 0;

			dbus_instantiate(dbus_ce_dbus_set, return_value TSRMLS_CC);
			DBUS_ZEND_GET_ZVAL_OBJECT(return_value, set, php_dbus_set_obj);
			set->elements = (zval **) safe_emalloc(sizeof(zval*), 64, 0);

			do {
				zval *key = NULL;
				val = php_dbus_to_zval(&args, &key TSRMLS_CC);

				DBUS_ZEND_ALLOC_ZVAL(set->elements[i]);
				ZVAL_ZVAL(set->elements[i], val, 1, 0);
				DBUS_ZEND_ADDREF_P(set->elements[i]);

				i++;
			} while (dbus_message_iter_next(&args));

			set->element_count = i;
		} else {
			// Exactly one return value
			zval *key = NULL;
			val = php_dbus_to_zval(&args, &key TSRMLS_CC);
			*return_value = *val;
		}
	}

	return 1;
}

PHP_METHOD(DbusObject, __call)
{
	char *name;
	size_t name_len;
	zval *data;
	zval *object;
	php_dbus_object_obj *dbus_object;
	DBusMessage *msg;
	DBusPendingCall* pending;

	if (FAILURE == zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(),
		"Osz", &object, dbus_ce_dbus_object, &name, &name_len, &data)) {
		RETURN_FALSE;
	}
	DBUS_ZEND_GET_ZVAL_OBJECT(object, dbus_object, php_dbus_object_obj);

	dbus_set_error_handling(EH_THROW, NULL TSRMLS_CC);

	msg = dbus_message_new_method_call(dbus_object->destination, dbus_object->path, dbus_object->interface, name);
	php_dbus_append_parameters(msg, data, dbus_object->introspect_xml ? php_dbus_find_method_node(dbus_object->introspect_xml->children, name) : NULL, PHP_DBUS_CALL_FUNCTION TSRMLS_CC);

	/* send message and get a handle for a reply */
	if (!dbus_connection_send_with_reply(dbus_object->dbus->con, msg, &pending, -1)) {
		dbus_message_unref(msg);
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Out of memory.");
	}

	if (NULL == pending) {
		dbus_message_unref(msg);
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Pending call null.");
	}
	dbus_connection_flush(dbus_object->dbus->con);

	/* free message */
	dbus_message_unref(msg);

	/* block until we recieve a reply */
	dbus_pending_call_block(pending);

	/* get the reply message */
	msg = dbus_pending_call_steal_reply(pending);

	dbus_set_error_handling(EH_NORMAL, NULL TSRMLS_CC);

	if (msg == NULL) {
		dbus_pending_call_unref(pending);
		RETURN_NULL();
	} else {
		php_dbus_handle_reply(return_value, msg, 0 TSRMLS_CC);
		dbus_message_unref(msg);
		dbus_pending_call_unref(pending);
	}
}

PHP_METHOD(Dbus, addWatch)
{
	char *interface = NULL, *member = NULL;
	size_t interface_len, member_len;
	zval *object;
	php_dbus_obj *dbus;
	DBusError err;
	char *match_str;

	if (FAILURE == zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(),
		"O|ss", &object, dbus_ce_dbus, &interface, &interface_len, &member, &member_len)) {
		RETURN_FALSE;
	}
	DBUS_ZEND_GET_ZVAL_OBJECT(object, dbus, php_dbus_obj);

	dbus_set_error_handling(EH_THROW, NULL TSRMLS_CC);

	if (interface && member) {
		spprintf(&match_str, 0, "type='signal',interface='%s',member='%s'", interface, member);
	} else if (interface) {
		spprintf(&match_str, 0, "type='signal',interface='%s'", interface);
	} else {
		spprintf(&match_str, 0, "type='signal'");
	}

	dbus_error_init(&err);
	dbus_bus_add_match(dbus->con, match_str, &err);
	efree(match_str);
	dbus_connection_flush(dbus->con);
	dbus_set_error_handling(EH_NORMAL, NULL TSRMLS_CC);
	if (dbus_error_is_set(&err)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Match error (%s)", err.message);
		RETURN_FALSE;
	}
	RETURN_TRUE;
}

static int dbus_signal_initialize(php_dbus_signal_obj *dbusobj, php_dbus_obj *dbus, char *object, char *interface, char *signal TSRMLS_DC)
{
	dbusobj->dbus = dbus;
	dbusobj->object = estrdup(object);
	dbusobj->interface = estrdup(interface);
	dbusobj->signal = estrdup(signal);

	return 1;
}


/* {{{ proto DbusSignal::__construct(Dbus $dbus, string object, string interface, string signal)
   Creates new DbusSignal object
*/
PHP_METHOD(DbusSignal, __construct)
{
	zval *object;
	php_dbus_obj *dbus;
	php_dbus_signal_obj *signal_object = NULL;
	char *object_name, *interface, *signal;
	size_t object_name_len, interface_len, signal_len;

	dbus_set_error_handling(EH_THROW, NULL TSRMLS_CC);
	if (SUCCESS == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Osss",
		&object, dbus_ce_dbus,
		&object_name, &object_name_len, &interface, &interface_len,
		&signal, &signal_len))
	{
		DBUS_ZEND_ADDREF_P(object);
		DBUS_ZEND_GET_ZVAL_OBJECT(object, dbus, php_dbus_obj);
		DBUS_ZEND_GET_ZVAL_OBJECT(getThis(), signal_object, php_dbus_signal_obj);
		dbus_signal_initialize(signal_object, dbus, object_name, interface, signal TSRMLS_CC);
	}
	dbus_set_error_handling(EH_NORMAL, NULL TSRMLS_CC);
}
/* }}} */

PHP_METHOD(DbusSignal, matches)
{
	char *interface;
	size_t interface_len;
	char *method;
	size_t method_len;
	zval *object;
	php_dbus_signal_obj *signal_obj;

	if (FAILURE == zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(),
		"Oss", &object, dbus_ce_dbus_signal, &interface, &interface_len, &method, &method_len)) {
		RETURN_FALSE;
	}
	DBUS_ZEND_GET_ZVAL_OBJECT(object, signal_obj, php_dbus_signal_obj);

	if (dbus_message_is_signal(signal_obj->msg, interface, method)) {
		RETURN_TRUE;
	}
	RETURN_FALSE;
}

PHP_METHOD(DbusSignal, getData)
{
	zval *object;
	php_dbus_signal_obj *signal_obj;

	if (FAILURE == zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(),
		"O", &object, dbus_ce_dbus_signal)) {
		RETURN_FALSE;
	}
	DBUS_ZEND_GET_ZVAL_OBJECT(object, signal_obj, php_dbus_signal_obj);

	if (signal_obj->direction == PHP_DBUS_SIGNAL_OUT) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "This signal is outgoing, and therefore does not have data.");
		RETURN_FALSE;
	}

	php_dbus_handle_reply(return_value, signal_obj->msg, 0 TSRMLS_CC);
}

PHP_METHOD(DbusSignal, send)
{
	int     elements = ZEND_NUM_ARGS();
	php_dbus_signal_obj *signal_obj;
	DBusMessageIter dbus_args;
	int i;
	dbus_uint32_t serial = 0;

	dbus_set_error_handling(EH_THROW, NULL TSRMLS_CC);
	DBUS_ZEND_GET_ZVAL_OBJECT(getThis(), signal_obj, php_dbus_signal_obj);

	if (signal_obj->direction == PHP_DBUS_SIGNAL_IN) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "This signal is incoming, and therefore can not be send.");
		RETURN_FALSE;
	}
	zval *data = NULL;
	data = (zval *) safe_emalloc(elements, sizeof(zval), 1);
	if (FAILURE == zend_get_parameters_array_ex(elements, data)) {
		efree(data);
		return;
	}

	signal_obj->msg = dbus_message_new_signal(signal_obj->object, signal_obj->interface, signal_obj->signal);
	dbus_message_iter_init_append(signal_obj->msg, &dbus_args);

	for (i = 0; i < elements; i++) {
		zval *tmp = &data[i];
		dbus_append_var(&tmp, &dbus_args, NULL);
	}

	/* send the message and flush the connection */
	if (!dbus_connection_send(signal_obj->dbus->con, signal_obj->msg, &serial)) {
		dbus_message_unref(signal_obj->msg);
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Out of memory.");
	}
	dbus_connection_flush(signal_obj->dbus->con);

	efree(data);

	dbus_set_error_handling(EH_NORMAL, NULL TSRMLS_CC);
}

static int dbus_array_initialize(php_dbus_array_obj *dbusobj, long type, zval *elements, char *signature TSRMLS_DC)
{
	dbusobj->type = type;

	DBUS_ZEND_ALLOC_ZVAL(dbusobj->elements);
	ZVAL_ZVAL(dbusobj->elements, elements, 1, 0);
	DBUS_ZEND_ADDREF_P(dbusobj->elements);

	dbusobj->signature = signature ? estrdup(signature) : NULL;

	return 1;
}

static HashTable *dbus_array_get_properties(zval *object TSRMLS_DC)
{
	HashTable *props;
	php_dbus_array_obj *array_obj;
	zval *sig;

	DBUS_ZEND_GET_ZVAL_OBJECT(object, array_obj, php_dbus_array_obj);

	props = array_obj->std.properties;
	if (array_obj->signature) {
		DBUS_ZEND_MAKE_STD_ZVAL(sig);
		DBUS_ZVAL_STRING(sig, array_obj->signature, 1);
		DBUS_ZEND_HASH_UPDATE(props, "signature", &sig);
	}

	DBUS_ZEND_ADDREF_P(array_obj->elements);
	DBUS_ZEND_HASH_UPDATE(props, "array", &array_obj->elements);

	return props;
}

/* {{{ proto DbusArray::__construct(int $type, array $elements [, string signature] )
   Creates new DbusArray object
*/
PHP_METHOD(DbusArray, __construct)
{
	long  type;
	zval *array;
	char *signature = NULL;
	size_t   signature_len;
	php_dbus_array_obj *array_obj = NULL;

#warning test whether signature is present when type == struct

	dbus_set_error_handling(EH_THROW, NULL TSRMLS_CC);
	if (SUCCESS == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "la|s", &type, &array, &signature, &signature_len)) {
		DBUS_ZEND_GET_ZVAL_OBJECT(getThis(), array_obj, php_dbus_array_obj);
		dbus_array_initialize(array_obj, type, array, signature TSRMLS_CC);
	}
	dbus_set_error_handling(EH_NORMAL, NULL TSRMLS_CC);
}
/* }}} */

PHP_METHOD(DbusArray, getData)
{
	zval *object;
	php_dbus_array_obj *array_obj;

	if (FAILURE == zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(),
		"O", &object, dbus_ce_dbus_array)) {
		RETURN_FALSE;
	}
	DBUS_ZEND_GET_ZVAL_OBJECT(object, array_obj, php_dbus_array_obj);
	*return_value = *array_obj->elements;
	zval_copy_ctor(return_value);
}

static int dbus_dict_initialize(php_dbus_dict_obj *dbusobj, long type, zval *elements, char *signature TSRMLS_DC)
{
	dbusobj->type = type;

	DBUS_ZEND_ALLOC_ZVAL(dbusobj->elements);
	ZVAL_ZVAL(dbusobj->elements, elements, 1, 0);
	DBUS_ZEND_ADDREF_P(dbusobj->elements);

	dbusobj->signature = signature ? estrdup(signature) : NULL;

	return 1;
}

static HashTable *dbus_dict_get_properties(zval *object TSRMLS_DC)
{
	HashTable *props;
	php_dbus_dict_obj *dict_obj;
	zval *sig;

	DBUS_ZEND_GET_ZVAL_OBJECT(object, dict_obj, php_dbus_dict_obj);

	props = dict_obj->std.properties;

	if (dict_obj->signature) {
		DBUS_ZEND_MAKE_STD_ZVAL(sig);
		DBUS_ZVAL_STRING(sig, dict_obj->signature, 1);
		DBUS_ZEND_HASH_UPDATE(props, "signature", &sig);
	}

	DBUS_ZEND_ADDREF_P(dict_obj->elements);
	DBUS_ZEND_HASH_UPDATE(props, "dict", &dict_obj->elements);

	return props;
}

/* {{{ proto DbusDict::__construct(int $type, dict $elements)
   Creates new DbusDict object
*/
PHP_METHOD(DbusDict, __construct)
{
	long  type;
	zval *dict;
	char *signature = NULL;
	size_t signature_len;
	php_dbus_dict_obj *dict_obj = NULL;

	dbus_set_error_handling(EH_THROW, NULL TSRMLS_CC);
	if (SUCCESS == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "la|s", &type, &dict, &signature, &signature_len)) {
		DBUS_ZEND_GET_ZVAL_OBJECT(getThis(), dict_obj, php_dbus_dict_obj);
		dbus_dict_initialize(dict_obj, type, dict, signature TSRMLS_CC);
	}
	dbus_set_error_handling(EH_NORMAL, NULL TSRMLS_CC);
}
/* }}} */

PHP_METHOD(DbusDict, getData)
{
	zval *object;
	php_dbus_dict_obj *dict_obj;

	if (FAILURE == zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(),
		"O", &object, dbus_ce_dbus_dict)) {
		RETURN_FALSE;
	}
	DBUS_ZEND_GET_ZVAL_OBJECT(object, dict_obj, php_dbus_dict_obj);
	*return_value = *dict_obj->elements;
	zval_copy_ctor(return_value);
}

static int dbus_variant_initialize(php_dbus_variant_obj *dbusobj, zval *data, char *signature TSRMLS_DC)
{
	DBUS_ZEND_ALLOC_ZVAL(dbusobj->data);
	ZVAL_ZVAL(dbusobj->data, data, 1, 0);
	DBUS_ZEND_ADDREF_P(dbusobj->data);

	dbusobj->signature = signature ? estrdup(signature) : NULL;

	return 1;
}

static HashTable *dbus_variant_get_properties(zval *object TSRMLS_DC)
{
	HashTable *props;
	php_dbus_variant_obj *variant_obj;

	DBUS_ZEND_GET_ZVAL_OBJECT(object, variant_obj, php_dbus_variant_obj);

	props = variant_obj->std.properties;

	DBUS_ZEND_ADDREF_P(variant_obj->data);
	DBUS_ZEND_HASH_UPDATE(props, "variant", &variant_obj->data);

	return props;
}

/* {{{ proto DbusVariant::__construct(mixed $data)
   Creates new DbusVariant object
*/
PHP_METHOD(DbusVariant, __construct)
{
	zval *data;
	char *signature = NULL;
	size_t signature_len;
	php_dbus_variant_obj *variant_obj = NULL;

	dbus_set_error_handling(EH_THROW, NULL TSRMLS_CC);
	if (SUCCESS == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z|s", &data, &signature, &signature_len)) {
		DBUS_ZEND_GET_ZVAL_OBJECT(getThis(), variant_obj, php_dbus_variant_obj);
		dbus_variant_initialize(variant_obj, data, signature TSRMLS_CC);
	}
	dbus_set_error_handling(EH_NORMAL, NULL TSRMLS_CC);
}
/* }}} */

PHP_METHOD(DbusVariant, getData)
{
	zval *object;
	php_dbus_variant_obj *variant_obj;

	if (FAILURE == zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(),
		"O", &object, dbus_ce_dbus_variant)) {
		RETURN_FALSE;
	}

	DBUS_ZEND_GET_ZVAL_OBJECT(object, variant_obj, php_dbus_variant_obj);
	*return_value = *variant_obj->data;
	zval_copy_ctor(return_value);
}

static int dbus_set_initialize(php_dbus_set_obj *dbusobj, zval **data, int elements TSRMLS_DC)
{
	int i;
	zval *param;

	dbusobj->elements = ecalloc(sizeof(zval *), elements);
	dbusobj->element_count = elements;

	for (i = 0; i < elements; i++) {
		DBUS_ZEND_ALLOC_ZVAL(dbusobj->elements[i]);
		param = &(*data)[i];
		ZVAL_ZVAL(dbusobj->elements[i], param, 1, 0);
		DBUS_ZEND_ADDREF_P(dbusobj->elements[i]);
	}

	return 1;
}

static HashTable *dbus_set_get_properties(zval *object TSRMLS_DC)
{
	int i;
	HashTable *props;
	php_dbus_set_obj *set_obj;
	zval *set_contents;

	DBUS_ZEND_GET_ZVAL_OBJECT(object, set_obj, php_dbus_set_obj);

	props = set_obj->std.properties;

	DBUS_ZEND_MAKE_STD_ZVAL(set_contents);
	array_init(set_contents);
	for (i = 0; i < set_obj->element_count; i++) {
		DBUS_ZEND_ADDREF_P(set_obj->elements[i]);
		add_next_index_zval(set_contents, set_obj->elements[i]);
	}

	DBUS_ZEND_HASH_UPDATE(props, "set", &set_contents);

	return props;
}


/* {{{ proto DbusSet::__construct(mixed $data)
   Creates new DbusSet object
*/
PHP_METHOD(DbusSet, __construct)
{
	int elements = ZEND_NUM_ARGS();
	php_dbus_set_obj *set_obj = NULL;

	dbus_set_error_handling(EH_THROW, NULL TSRMLS_CC);
	zval *data = NULL;
	data = (zval *) safe_emalloc(elements, sizeof(zval), 1);
	if (SUCCESS == zend_get_parameters_array_ex(elements, data)) {
		DBUS_ZEND_GET_ZVAL_OBJECT(getThis(), set_obj, php_dbus_set_obj);
		dbus_set_initialize(set_obj, &data, elements TSRMLS_CC);
	}
	efree(data);
	dbus_set_error_handling(EH_NORMAL, NULL TSRMLS_CC);
}
/* }}} */

PHP_METHOD(DbusSet, getData)
{
	int i;
	zval *object;
	php_dbus_set_obj *set_obj;

	if (FAILURE == zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(),
		"O", &object, dbus_ce_dbus_set)) {
		RETURN_FALSE;
	}
	DBUS_ZEND_GET_ZVAL_OBJECT(object, set_obj, php_dbus_set_obj);
	array_init(return_value);
	for (i = 0; i < set_obj->element_count; i++) {
		DBUS_ZEND_ADDREF_P(set_obj->elements[i]);
		add_next_index_zval(return_value, set_obj->elements[i]);
	}
}

static int dbus_struct_initialize(php_dbus_struct_obj *dbusobj, char *signature, zval *elements TSRMLS_DC)
{
	DBUS_ZEND_ALLOC_ZVAL(dbusobj->elements);
	ZVAL_ZVAL(dbusobj->elements, elements, 1, 0);
	DBUS_ZEND_ADDREF_P(dbusobj->elements);

	return 1;
}

static HashTable *dbus_struct_get_properties(zval *object TSRMLS_DC)
{
	HashTable *props;
	php_dbus_struct_obj *struct_obj;

	DBUS_ZEND_GET_ZVAL_OBJECT(object, struct_obj, php_dbus_struct_obj);

	props = struct_obj->std.properties;

	DBUS_ZEND_ADDREF_P(struct_obj->elements);
	DBUS_ZEND_HASH_UPDATE(props, "struct", &struct_obj->elements);

	return props;
}

/* {{{ proto DbusStruct::__construct(string $signature, struct $elements)
   Creates new DbusStruct object
*/
PHP_METHOD(DbusStruct, __construct)
{
	zval *array;
	char *signature;
	size_t signature_len;
	php_dbus_struct_obj *struct_obj = NULL;

	dbus_set_error_handling(EH_THROW, NULL TSRMLS_CC);
	if (SUCCESS == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sa", &signature, &signature_len, &array)) {
		DBUS_ZEND_GET_ZVAL_OBJECT(getThis(), struct_obj, php_dbus_struct_obj);
		dbus_struct_initialize(struct_obj, signature, array TSRMLS_CC);
	}
	dbus_set_error_handling(EH_NORMAL, NULL TSRMLS_CC);
}
/* }}} */

PHP_METHOD(DbusStruct, getData)
{
	zval *object;
	php_dbus_struct_obj *struct_obj;

	if (FAILURE == zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(),
		"O", &object, dbus_ce_dbus_struct)) {
		RETURN_FALSE;
	}
	DBUS_ZEND_GET_ZVAL_OBJECT(object, struct_obj, php_dbus_struct_obj);
	*return_value = *struct_obj->elements;
	zval_copy_ctor(return_value);
}

static int dbus_object_path_initialize(php_dbus_object_path_obj *dbusobj, char *path TSRMLS_DC)
{
	dbusobj->path = estrdup(path);

	return 1;
}

static HashTable *dbus_object_path_get_properties(zval *object TSRMLS_DC)
{
	HashTable *props;
	php_dbus_object_path_obj *object_path_obj;
	zval *path;

	DBUS_ZEND_GET_ZVAL_OBJECT(object, object_path_obj, php_dbus_object_path_obj);

	props = object_path_obj->std.properties;

	DBUS_ZEND_MAKE_STD_ZVAL(path);
	DBUS_ZVAL_STRING(path, object_path_obj->path, 1);
	DBUS_ZEND_HASH_UPDATE(props, "path", &path);

	return props;
}

/* {{{ proto DbusObjectPath::__construct(string $path)
   Creates new DbusObjectPath object
*/
PHP_METHOD(DbusObjectPath, __construct)
{
	char *path;
	size_t path_len;
	php_dbus_object_path_obj *object_path_obj = NULL;

	dbus_set_error_handling(EH_THROW, NULL TSRMLS_CC);
	if (SUCCESS == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &path, &path_len)) {
		DBUS_ZEND_GET_ZVAL_OBJECT(getThis(), object_path_obj, php_dbus_object_path_obj);
		dbus_object_path_initialize(object_path_obj, path TSRMLS_CC);
	}
	dbus_set_error_handling(EH_NORMAL, NULL TSRMLS_CC);
}
/* }}} */

PHP_METHOD(DbusObjectPath, getData)
{
	zval *object;
	php_dbus_object_path_obj *object_path_obj;

	if (FAILURE == zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(),
		"O", &object, dbus_ce_dbus_object_path)) {
		RETURN_FALSE;
	}
	DBUS_ZEND_GET_ZVAL_OBJECT(object, object_path_obj, php_dbus_object_path_obj);

	RETURN_STRING(object_path_obj->path);
}


#define PHP_DBUS_INT_WRAPPER(t,s,n) \
	static int dbus_##s##_initialize(php_dbus_##s##_obj *dbusobj, t data TSRMLS_DC) \
	{ \
		dbusobj->data = data; \
		return 1; \
	} \
	PHP_METHOD(n, __construct) \
	{ \
		long data; \
		dbus_set_error_handling(EH_THROW, NULL TSRMLS_CC); \
		if (SUCCESS == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "l", &data)) { \
			php_dbus_##s##_obj *dbusobj = NULL; \
			DBUS_ZEND_GET_ZVAL_OBJECT(getThis(), dbusobj, php_dbus_##s##_obj); \
			dbus_##s##_initialize(dbusobj, (t)data TSRMLS_CC); \
		} \
		dbus_set_error_handling(EH_NORMAL, NULL TSRMLS_CC); \
	}

PHP_DBUS_INT_WRAPPER(unsigned char,byte,  DbusByte);
PHP_DBUS_INT_WRAPPER(dbus_bool_t,  bool,  DbusBool);
PHP_DBUS_INT_WRAPPER(dbus_int16_t, int16, DbusInt16);
PHP_DBUS_INT_WRAPPER(dbus_uint16_t,uint16,DbusUInt16);
PHP_DBUS_INT_WRAPPER(dbus_int32_t, int32, DbusInt32);
PHP_DBUS_INT_WRAPPER(dbus_uint32_t,uint32,DbusUInt32);
PHP_DBUS_INT_WRAPPER(dbus_int64_t, int64, DbusInt64);
PHP_DBUS_INT_WRAPPER(dbus_uint64_t,uint64,DbusUInt64);
PHP_DBUS_INT_WRAPPER(double,       double,DbusDouble);
/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: fdm=marker
 * vim: noet sw=4 ts=4
 */
