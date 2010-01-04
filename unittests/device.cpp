/**
 * Copyright (C) 2005-2008 Christoph Rupp (chris@crupp.de).
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or 
 * (at your option) any later version.
 *
 * See files COPYING.* for License information.
 */

#include "../src/config.h"

#include <stdexcept>
#include <cstring>
#include <ham/hamsterdb.h>
#include "../src/db.h"
#include "../src/device.h"
#include "memtracker.h"
#include "os.hpp"

#include "bfc-testsuite.hpp"
#include "hamster_fixture.hpp"

using namespace bfc;

class DeviceTest : public hamsterDB_fixture
{
    define_super(hamsterDB_fixture);

public:
    DeviceTest(bool inmemory=false, const char *name="DeviceTest")
    : hamsterDB_fixture(name), 
        m_db(0), m_inmemory(inmemory), m_dev(0), m_alloc(0)
    {
        testrunner::get_instance()->register_fixture(this);
        BFC_REGISTER_TEST(DeviceTest, newDeleteTest);
        BFC_REGISTER_TEST(DeviceTest, createCloseTest);
        BFC_REGISTER_TEST(DeviceTest, openCloseTest);
        BFC_REGISTER_TEST(DeviceTest, pagesizeTest);
        BFC_REGISTER_TEST(DeviceTest, allocTest);
        BFC_REGISTER_TEST(DeviceTest, allocFreeTest);
        BFC_REGISTER_TEST(DeviceTest, flushTest);
        BFC_REGISTER_TEST(DeviceTest, mmapUnmapTest);
        BFC_REGISTER_TEST(DeviceTest, readWriteTest);
        BFC_REGISTER_TEST(DeviceTest, readWritePageTest);
    }

protected:
    ham_db_t *m_db;
    ham_bool_t m_inmemory;
    ham_device_t *m_dev;
    memtracker_t *m_alloc;

public:
    virtual void setup() 
    { 
        __super::setup();

        (void)os::unlink(BFC_OPATH(".test"));

        m_alloc=memtracker_new();
        BFC_ASSERT_EQUAL(0, ham_new(&m_db));
        db_set_allocator(m_db, (mem_allocator_t *)m_alloc);
        BFC_ASSERT_EQUAL(0, 
                ham_create(m_db, BFC_OPATH(".test"), 
                        m_inmemory ? HAM_IN_MEMORY_DB : 0, 0644));
        m_dev=db_get_device(m_db);
    }
    
    virtual void teardown() 
    { 
        __super::teardown();

        ham_close(m_db, 0);
        ham_delete(m_db);
        BFC_ASSERT(!memtracker_get_leaks(m_alloc));
    }

    void newDeleteTest()
    {
    }

    void createCloseTest()
    {
        BFC_ASSERT(m_dev->is_open(m_dev));
        BFC_ASSERT(m_dev->close(m_dev)==HAM_SUCCESS);
        BFC_ASSERT(!m_dev->is_open(m_dev));
    }

    void openCloseTest()
    {
        if (!m_inmemory) {
            BFC_ASSERT(m_dev->is_open(m_dev));
            BFC_ASSERT(m_dev->close(m_dev)==HAM_SUCCESS);
            BFC_ASSERT(!m_dev->is_open(m_dev));
            BFC_ASSERT(m_dev->open(m_dev, BFC_OPATH(".test"), 0)==HAM_SUCCESS);
            BFC_ASSERT(m_dev->is_open(m_dev));
            BFC_ASSERT(m_dev->close(m_dev)==HAM_SUCCESS);
            BFC_ASSERT(!m_dev->is_open(m_dev));
        }
    }

    void pagesizeTest()
    {
        ham_size_t cps;
        ham_size_t ps=m_dev->get_pagesize(m_dev);
        BFC_ASSERT(ps!=0);
        BFC_ASSERT(ps%1024==0);
        cps=device_get_pagesize(m_dev);
        BFC_ASSERT(cps!=0);
        BFC_ASSERT(cps % DB_CHUNKSIZE == 0);
        if (!m_inmemory)
            BFC_ASSERT_EQUAL(cps, ps);
    }

    void allocTest()
    {
        int i;
        ham_offset_t address;

        BFC_ASSERT(m_dev->is_open(m_dev));
        for (i=0; i<10; i++) {
            BFC_ASSERT_EQUAL(0, m_dev->alloc(m_dev, 1024, &address));
            BFC_ASSERT_EQUAL((db_get_pagesize(m_db)*2)+1024*i, address);
        }
        BFC_ASSERT(m_dev->close(m_dev)==HAM_SUCCESS);
    }

    void allocFreeTest()
    {
        ham_page_t page;
        memset(&page, 0, sizeof(page));
        page_set_owner(&page, m_db);

        BFC_ASSERT(m_dev->is_open(m_dev));
        BFC_ASSERT(m_dev->alloc_page(m_dev, &page, 
                    db_get_pagesize(m_db))==HAM_SUCCESS);
        BFC_ASSERT(page_get_pers(&page)!=0);
        BFC_ASSERT(m_dev->free_page(m_dev, &page)==HAM_SUCCESS);
        BFC_ASSERT(m_dev->close(m_dev)==HAM_SUCCESS);
    }

    void flushTest()
    {
        BFC_ASSERT(m_dev->is_open(m_dev));
        BFC_ASSERT(m_dev->flush(m_dev)==HAM_SUCCESS);
        BFC_ASSERT(m_dev->is_open(m_dev));
    }

    void mmapUnmapTest()
    {
        int i;
        ham_page_t pages[10];
        ham_size_t ps=device_get_pagesize(m_dev);
        ham_u8_t *temp=(ham_u8_t *)malloc(ps);

        BFC_ASSERT(m_dev->is_open(m_dev));
        BFC_ASSERT(m_dev->truncate(m_dev, ps*10)==HAM_SUCCESS);
        for (i=0; i<10; i++) {
            memset(&pages[i], 0, sizeof(ham_page_t));
            page_set_owner(&pages[i], m_db);
            page_set_self(&pages[i], i*ps);
            BFC_ASSERT(m_dev->read_page(m_dev, &pages[i], 
                        db_get_pagesize(m_db))==HAM_SUCCESS);
        }
        for (i=0; i<10; i++)
            memset(page_get_pers(&pages[i]), i, ps);
        for (i=0; i<10; i++) {
            BFC_ASSERT(m_dev->write_page(m_dev, &pages[i])==HAM_SUCCESS);
        }
        for (i=0; i<10; i++) {
            ham_u8_t *buffer;
            memset(temp, i, ps);
            BFC_ASSERT(m_dev->free_page(m_dev, &pages[i])==HAM_SUCCESS);

            BFC_ASSERT(m_dev->read_page(m_dev, &pages[i], 
                        db_get_pagesize(m_db))==HAM_SUCCESS);
            buffer=(ham_u8_t *)page_get_pers(&pages[i]);
            BFC_ASSERT(memcmp(buffer, temp, ps)==0);
        }
        for (i=0; i<10; i++) {
            BFC_ASSERT(m_dev->free_page(m_dev, &pages[i])==HAM_SUCCESS);
        }
        m_dev->close(m_dev);
        free(temp);
    }

    void readWriteTest()
    {
        int i;
        ham_u8_t *buffer[10]={ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        ham_size_t ps=device_get_pagesize(m_dev);
        ham_u8_t *temp=(ham_u8_t *)malloc(ps);

        m_dev->set_flags(m_dev, DEVICE_NO_MMAP);

        BFC_ASSERT(m_dev->is_open(m_dev));
        BFC_ASSERT(m_dev->truncate(m_dev, ps*10)==HAM_SUCCESS);
        for (i=0; i<10; i++) {
            buffer[i]=(ham_u8_t *)malloc(ps);
            BFC_ASSERT_EQUAL(0,
                    m_dev->read(m_db, m_dev, i*ps, buffer[i], ps));
        }
        for (i=0; i<10; i++)
            memset(buffer[i], i, ps);
        for (i=0; i<10; i++) {
            BFC_ASSERT_EQUAL(0, 
                    m_dev->write(m_db, m_dev, i*ps, buffer[i], ps));
        }
        for (i=0; i<10; i++) {
            BFC_ASSERT_EQUAL(0, 
                    m_dev->read(m_db, m_dev, i*ps, buffer[i], ps));
            memset(temp, i, ps);
            BFC_ASSERT(memcmp(buffer[i], temp, ps)==0);
            free(buffer[i]);
        }
        m_dev->close(m_dev);
        free(temp);
    }

    void readWritePageTest()
    {
        int i;
        ham_page_t *pages[2];
        ham_size_t ps=device_get_pagesize(m_dev);

        m_dev->set_flags(m_dev, HAM_DISABLE_MMAP);

        BFC_ASSERT(m_dev->is_open(m_dev));
        BFC_ASSERT(m_dev->truncate(m_dev, ps*2)==HAM_SUCCESS);
        for (i=0; i<2; i++) {
            BFC_ASSERT((pages[i]=page_new(m_db, 0)));
            page_set_self(pages[i], ps*i);
            BFC_ASSERT_EQUAL(0,
                    m_dev->read_page(m_dev, pages[i], 0));
        }
        for (i=0; i<2; i++) {
            BFC_ASSERT(page_get_npers_flags(pages[i])&PAGE_NPERS_MALLOC);
            memset(page_get_pers(pages[i]), i+1, ps);
            BFC_ASSERT_EQUAL(0, 
                    m_dev->write_page(m_dev, pages[i]));
            BFC_ASSERT_EQUAL(0, page_free(pages[i]));
            page_delete(pages[i]);
        }

        for (i=0; i<2; i++) {
            char temp[1024];
            memset(temp, i+1, sizeof(temp));
            BFC_ASSERT((pages[i]=page_new(m_db, 0)));
            page_set_self(pages[i], ps*i);
            BFC_ASSERT_EQUAL(0, 
                    m_dev->read_page(m_dev, pages[i], ps));
            BFC_ASSERT_EQUAL(0, 
                    memcmp(page_get_pers(pages[i]), temp, sizeof(temp)));
            BFC_ASSERT_EQUAL(0, page_free(pages[i]));
            page_delete(pages[i]);
        }
        m_dev->close(m_dev);
    }

};

class InMemoryDeviceTest : public DeviceTest
{
public:
    InMemoryDeviceTest()
    :   DeviceTest(true, "InMemoryDeviceTest")
    {
        clear_tests(); // don't inherit tests
        testrunner::get_instance()->register_fixture(this);
        BFC_REGISTER_TEST(InMemoryDeviceTest, newDeleteTest);
        BFC_REGISTER_TEST(InMemoryDeviceTest, createCloseTest);
        BFC_REGISTER_TEST(InMemoryDeviceTest, openCloseTest);
        BFC_REGISTER_TEST(InMemoryDeviceTest, pagesizeTest);
        BFC_REGISTER_TEST(InMemoryDeviceTest, allocFreeTest);
        BFC_REGISTER_TEST(InMemoryDeviceTest, flushTest);
    }

};

BFC_REGISTER_FIXTURE(DeviceTest);
BFC_REGISTER_FIXTURE(InMemoryDeviceTest);

