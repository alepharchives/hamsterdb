/**
 * Copyright (C) 2005, 2006 Christoph Rupp (chris@crupp.de)
 * see file LICENSE for license and copyright information
 *
 *
 */

#include <string.h>
#include <ham/hamsterdb.h>

#include "util.h"
#include "mem.h"
#include "txn.h"
#include "db.h"
#include "keys.h"
#include "error.h"
#include "blob.h"

ham_key_t *
util_copy_key(ham_db_t *db, ham_txn_t *txn, 
            const ham_key_t *source, ham_key_t *dest)
{
    /*
     * extended key: copy the whole key
     */
    if (source->_flags&KEY_IS_EXTENDED) {
        ham_status_t st=db_get_extended_key(db, txn, source->data, 
                    source->size, source->_flags, (ham_u8_t **)&dest->data);
        if (st) {
            db_set_error(db, st);
            return (0);
        }
        ham_assert(dest->data!=0, ("invalid extended key"));
        dest->size=source->size;
        /* the extended flag is set later, when this key is inserted */
        dest->_flags=source->_flags&(~KEY_IS_EXTENDED);
    }
    else {
        dest->data=(ham_u8_t *)ham_mem_alloc(source->size);
        if (!dest->data) {
            db_set_error(db, HAM_OUT_OF_MEMORY);
            return (0);
        }

        memcpy(dest->data, source->data, source->size);
        dest->size=source->size;
        dest->_flags=source->_flags;
    }

    return (dest);
}

ham_key_t *
util_copy_key_int2pub(ham_db_t *db, ham_txn_t *txn, 
            const key_t *source, ham_key_t *dest)
{
    /*
     * extended key: copy the whole key
     */
    if (key_get_flags(source)&KEY_IS_EXTENDED) {
        ham_status_t st=db_get_extended_key(db, txn, key_get_key(source),
                    key_get_size(source), key_get_flags(source), 
                    (ham_u8_t **)&dest->data);
        if (st) {
            db_set_error(db, st);
            return (0);
        }
        ham_assert(dest->data!=0, ("invalid extended key"));
        dest->size=key_get_size(source);
        /* the extended flag is set later, when this key is inserted */
        dest->_flags=key_get_flags(source)&(~KEY_IS_EXTENDED);
    }
    else {
        dest->data=(ham_u8_t *)ham_mem_alloc(key_get_size(source));
        if (!dest->data) {
            db_set_error(db, HAM_OUT_OF_MEMORY);
            return (0);
        }

        memcpy(dest->data, key_get_key(source), key_get_size(source));
        dest->size=key_get_size(source);
        dest->_flags=key_get_flags(source);
    }

    return (dest);
}

ham_status_t
util_read_record(ham_db_t *db, ham_txn_t *txn, 
        ham_record_t *record, ham_u32_t flags)
{
    ham_bool_t noblob=HAM_FALSE;
    ham_status_t st;

    /*
     * sometimes (if the record size is small enough), there's
     * no blob available, but the data is stored in the record's
     * offset.
     */
    if (record->_intflags&KEY_BLOB_SIZE_TINY) {
        /* the highest nibble of the record id is the size of the blob */
        char *p=(char *)&record->_rid;
        record->size=p[sizeof(ham_offset_t)-1];
        noblob=HAM_TRUE;
    }
    else if (record->_intflags&KEY_BLOB_SIZE_SMALL) {
        /* record size is sizeof(ham_offset_t) */
        record->size=sizeof(ham_offset_t);
        noblob=HAM_TRUE;
    }
    else if (record->_intflags&KEY_BLOB_SIZE_EMPTY) {
        /* record size is 0 */
        record->size=0;
        noblob=HAM_TRUE;
    }

    if (noblob && record->size>0) {
        if (!(record->flags & HAM_RECORD_USER_ALLOC)) {
            if (record->size>db_get_record_allocsize(db)) {
                if (db_get_record_allocdata(db))
                    ham_mem_free(db_get_record_allocdata(db));
                db_set_record_allocdata(db, ham_mem_alloc(record->size));
                if (!db_get_record_allocdata(db)) {
                    st=HAM_OUT_OF_MEMORY;
                    db_set_record_allocsize(db, 0);
                }
                else {
                    db_set_record_allocsize(db, record->size);
                }
            }
            record->data=db_get_record_allocdata(db);
        }

        if (!st) 
            memcpy(record->data, &record->_rid, record->size);
    }
    else if (noblob && record->size==0) {
        st=0; /* no blob available */
    }
    else {
        st=blob_read(db, txn, record->_rid, record, flags);
    }

    return (st);
}

ham_status_t
util_read_key(ham_db_t *db, ham_txn_t *txn, 
        key_t *source, ham_key_t *dest, ham_u32_t flags)
{
    ham_u8_t *data;

    /*
     * extended key: copy the whole key
     */
    if (key_get_flags(source)&KEY_IS_EXTENDED) {
        ham_status_t st=db_get_extended_key(db, txn, key_get_key(source),
                    key_get_size(source), key_get_flags(source), 
                    (ham_u8_t **)&data);
        if (st) {
            db_set_error(db, st);
            return (0);
        }
        ham_assert(data!=0, ("invalid extended key"));
        dest->size=key_get_size(source);

        if (key_get_size(source)) {
            if (dest->flags&HAM_KEY_USER_ALLOC) {
                memcpy(dest->data, data, key_get_size(source));
                ham_mem_free(data);
            }
            else {
                if (db_get_record_allocdata(db))
                    ham_mem_free(db_get_record_allocdata(db));
                db_set_record_allocdata(db, data);
                db_set_record_allocsize(db, dest->size);
                dest->data=data;
            }
        }
        else {
            dest->data=0;
        }
    
        return (0);
    }

    /*
     * otherwise (non-extended key)...
     */
    else {
        dest->size=key_get_size(source);

        if (key_get_size(source)) {
            if (dest->flags&HAM_KEY_USER_ALLOC) {
                memcpy(dest->data, key_get_key(source), key_get_size(source));
            }
            else {
                if (dest->size>db_get_record_allocsize(db)) {
                    if (db_get_record_allocdata(db))
                        ham_mem_free(db_get_record_allocdata(db));
                    db_set_record_allocdata(db, ham_mem_alloc(dest->size));
                    if (!db_get_record_allocdata(db)) {
                        db_set_record_allocsize(db, 0);
                        return (db_set_error(db, HAM_OUT_OF_MEMORY));
                    }
                    else {
                        db_set_record_allocsize(db, dest->size);
                    }
                }
                dest->data=db_get_record_allocdata(db);

                memcpy(dest->data, key_get_key(source), key_get_size(source));
                db_set_record_allocdata(db, dest->data);
                db_set_record_allocsize(db, dest->size);
            }
        }
        else {
            dest->data=0;
        }
    }

    return (0);
}
