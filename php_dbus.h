/*
   +----------------------------------------------------------------------+
   | PHP Version 5                                                        |
   +----------------------------------------------------------------------+
   | Copyright (c) 1997-2008 The PHP Group                                |
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

#ifndef PHP_DBUS_H
#define PHP_DBUS_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "Zend/zend_hash.h"

#define PHP_DBUS_VERSION "0.2.0"

#if PHP_VERSION_ID >= 80000
# define TSRMLS_DC
# define TSRMLS_CC
# define TSRMLS_D void
# define TSRMLS_C
#endif

# define DBUS_ZEND_OBJECT_PROPERTIES_INIT(_objPtr, _ce) \
    object_properties_init(&_objPtr->std, _ce); \
    if (!_objPtr->std.properties) { \
        rebuild_object_properties(&_objPtr->std); \
    };

# define DBUS_ZEND_OBJECT_ALLOC(_objPtr, _ce) \
    do { \
        _objPtr = ecalloc(1, sizeof(*_objPtr) + zend_object_properties_size(_ce)); \
        if (ptr) { \
            *ptr = _objPtr; \
        } \
    } while(0)

# define DBUS_ZEND_OBJECT_SET_HANDLERS(_objPtr, _objType) \
    do { \
        dbus_object_handlers_##_objType.offset = XtOffsetOf(php_##_objType##_obj, std); \
        dbus_object_handlers_##_objType.free_obj = \
            (zend_object_free_obj_t) dbus_object_free_storage_##_objType; \
        _objPtr->std.handlers = &dbus_object_handlers_##_objType; \
    } while(0)

# define DBUS_ZEND_OBJECT_INIT_RETURN(_objPtr, _ce, _objType) \
    do { \
        DBUS_ZEND_OBJECT_ALLOC(_objPtr, _ce); \
        zend_object_std_init(&_objPtr->std, _ce); \
        DBUS_ZEND_OBJECT_PROPERTIES_INIT(_objPtr, _ce); \
        DBUS_ZEND_OBJECT_SET_HANDLERS(_objPtr, _objType); \
        return &_objPtr->std; \
    } while(0)

# define DBUS_ZVAL_STRING(zv, str, af) \
    do { \
        ZVAL_STRING(zv, str); \
            if (0 == af) { \
            efree(str); \
        } \
    } while(0)

# define DBUS_ZEND_HASH_ADD_PTR(_ht, _key, _ptr, _ptrSz) \
    zend_hash_str_add_ptr(_ht, _key, strlen(_key), _ptr)

# define DBUS_ZEND_HASH_UPDATE(_ht, _key, _zval) \
    zend_hash_str_update(_ht, _key, strlen(_key), *_zval)

# define DBUS_ZEND_HASH_FIND_PTR(_ht, _key, _ptr) \
    _ptr = zend_hash_str_find_ptr(_ht, _key, strlen(_key))

# define DBUS_ZEND_HASH_FIND_PTR_CHECK(_ht, _key, _ptr) \
    (DBUS_ZEND_HASH_FIND_PTR(_ht, _key, _ptr)) != NULL

# define DBUS_ZEND_HASH_EXISTS(_ht, _key) \
    zend_hash_str_exists(_ht, _key, strlen(_key))

# define DBUS_ZEND_HASH_GET_CURRENT_DATA(_ht, _zvalPtr) \
    _zvalPtr = zend_hash_get_current_data(_ht)

# define DBUS_ZEND_HASH_GET_CURRENT_DATA_CHECK(_ht, _zvalPtr) \
    (DBUS_ZEND_HASH_GET_CURRENT_DATA(_ht, _zvalPtr)) != NULL

# define DBUS_ZEND_HASH_GET_CURRENT_DATA_EX(_ht, _zvalPtr, _pos) \
    _zvalPtr = zend_hash_get_current_data_ex(_ht, &_pos)

# define DBUS_ZEND_HASH_GET_CURRENT_DATA_CHECK_EX(_ht, _zvalPtr, _pos) \
    (DBUS_ZEND_HASH_GET_CURRENT_DATA_EX(_ht, _zvalPtr, _pos)) != NULL

# define DBUS_ZEND_HASH_GET_CURRENT_KEY_INFO(_ht, _idx, _info) \
    do { \
        zend_string *tmp_info = NULL; \
        _info.type = zend_hash_get_current_key(_ht, &tmp_info, &_idx); \
        if (tmp_info) { \
            _info.name = ZSTR_VAL(tmp_info); \
            _info.length = ZSTR_LEN(tmp_info); \
        } \
    } while(0)

# define DBUS_ZEND_HASH_DESTROY_CHECK(_ht) \
    do { \
        if (_ht.u.flags) { \
            zend_hash_destroy(&_ht); \
            _ht.u.flags = 0; \
        } \
    } while (0)

# define DBUS_ZEND_ZOBJ_TO_OBJ(_zObj, _objType) \
    (_objType *) ((char *) _zObj - XtOffsetOf(_objType, std))

# define DBUS_ZEND_GET_ZVAL_OBJECT(_zval, _objPtr, _objType) \
    _objPtr = DBUS_ZEND_ZOBJ_TO_OBJ(Z_OBJ_P(_zval), _objType)

# define DBUS_ZEND_MAKE_STD_ZVAL(_zval) \
    { \
        zval tmp; \
        _zval = &tmp; \
    }

# define DBUS_ZEND_ALLOC_ZVAL(_zvalPtr) \
    _zvalPtr = emalloc(sizeof(zval)); \
    DBUS_ZEND_INIT_ZVAL(_zvalPtr)

# define DBUS_ZEND_ZVAL_TYPE_P(_zval) Z_TYPE_INFO_P(_zval)

# define DBUS_ZEND_ADDREF_P(_zval) Z_TRY_ADDREF_P(_zval)

# define DBUS_ZEND_INIT_ZVAL(_zvalPtr) ZVAL_NULL(_zvalPtr)


typedef struct _dbus_zend_hash_key_info {
    char *name;
    unsigned int length;
    unsigned int type;
} dbus_zend_hash_key_info;

extern zend_module_entry dbus_module_entry;
#define phpext_dbus_ptr &dbus_module_entry

PHP_METHOD(Dbus, __construct);
PHP_METHOD(Dbus, addWatch);
PHP_METHOD(Dbus, waitLoop);
PHP_METHOD(Dbus, requestName);
PHP_METHOD(Dbus, registerObject);
PHP_METHOD(Dbus, createProxy);

PHP_METHOD(DbusObject, __construct);
PHP_METHOD(DbusObject, __call);
PHP_METHOD(DbusSignal, __construct);
PHP_METHOD(DbusSignal, matches);
PHP_METHOD(DbusSignal, getData);
PHP_METHOD(DbusSignal, send);

PHP_METHOD(DbusArray, __construct);
PHP_METHOD(DbusArray, getData);
PHP_METHOD(DbusDict, __construct);
PHP_METHOD(DbusDict, getData);
PHP_METHOD(DbusVariant, __construct);
PHP_METHOD(DbusVariant, getData);
PHP_METHOD(DbusSet, __construct);
PHP_METHOD(DbusSet, getData);
PHP_METHOD(DbusStruct, __construct);
PHP_METHOD(DbusStruct, getData);
PHP_METHOD(DbusObjectPath, __construct);
PHP_METHOD(DbusObjectPath, getData);

#define PHP_DBUS_INT_WRAPPER_METHOD_DEF(t) \
	PHP_METHOD(Dbus##t, __construct);

PHP_DBUS_INT_WRAPPER_METHOD_DEF(Byte);
PHP_DBUS_INT_WRAPPER_METHOD_DEF(Bool);
PHP_DBUS_INT_WRAPPER_METHOD_DEF(Int16);
PHP_DBUS_INT_WRAPPER_METHOD_DEF(UInt16);
PHP_DBUS_INT_WRAPPER_METHOD_DEF(Int32);
PHP_DBUS_INT_WRAPPER_METHOD_DEF(UInt32);
PHP_DBUS_INT_WRAPPER_METHOD_DEF(Int64);
PHP_DBUS_INT_WRAPPER_METHOD_DEF(UInt64);
PHP_DBUS_INT_WRAPPER_METHOD_DEF(Double);

PHP_RINIT_FUNCTION(dbus);
PHP_RSHUTDOWN_FUNCTION(dbus);
PHP_MINIT_FUNCTION(dbus);
PHP_MSHUTDOWN_FUNCTION(dbus);
PHP_MINFO_FUNCTION(dbus);

ZEND_BEGIN_MODULE_GLOBALS(dbus)
ZEND_END_MODULE_GLOBALS(dbus)

#ifdef ZTS
#define DBUSG(v) TSRMG(dbus_globals_id, zend_dbus_globals *, v)
#else
#define DBUSG(v) (dbus_globals.v)
#endif

#endif /* PHP_DBUS_H */
