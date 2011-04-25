/*
 * $Id$
 *
 * DEBUG: section 54    Interprocess Communication
 *
 */

#include "config.h"
#include "base/TextException.h"
#include "base/RunnersRegistry.h"
#include "ipc/mem/PagePool.h"
#include "ipc/mem/Pages.h"
#include "structs.h"
#include "SwapDir.h"

// Uses a single PagePool instance, for now.
// Eventually, we may have pools dedicated to memory caching, disk I/O, etc.

// TODO: make pool id more unique so it does not conflict with other Squids?
static const char *PagePoolId = "squid-page-pool";
static Ipc::Mem::PagePool *ThePagePool = 0;

// TODO: make configurable to avoid waste when mem-cached objects are small/big
size_t
Ipc::Mem::PageSize() {
    return 32*1024;
}

bool
Ipc::Mem::GetPage(PageId &page)
{
    return ThePagePool ? ThePagePool->get(page) : false;
}

void
Ipc::Mem::PutPage(PageId &page)
{
    Must(ThePagePool);
    ThePagePool->put(page);
}

char *
Ipc::Mem::PagePointer(const PageId &page)
{
    Must(ThePagePool);
    return ThePagePool->pagePointer(page);
}

size_t
Ipc::Mem::Limit()
{
    // TODO: adjust cache_mem description to say that in SMP mode,
    // in-transit objects are not allocated using cache_mem. Eventually,
    // they should not use cache_mem even if shared memory is not used:
    // in-transit objects have nothing to do with caching.
    return Config.memMaxSize;
}

size_t
Ipc::Mem::Level()
{
    return ThePagePool ?
        (ThePagePool->capacity() - ThePagePool->size()) * PageSize() : 0;
}

/// initializes shared memory pages
class SharedMemPagesRr: public RegisteredRunner
{
public:
    /* RegisteredRunner API */
    SharedMemPagesRr(): owner(NULL) {}
    virtual void run(const RunnerRegistry &);
    virtual ~SharedMemPagesRr();

private:
    Ipc::Mem::PagePool::Owner *owner;
};

RunnerRegistrationEntry(rrAfterConfig, SharedMemPagesRr);


void SharedMemPagesRr::run(const RunnerRegistry &)
{
    if (!UsingSmp())
        return;

    // When cache_dirs start using shared memory pages, they would
    // need to communicate their needs to us somehow.
    if (!Ipc::Mem::Limit())
        return;

    if (Ipc::Mem::Limit() < Ipc::Mem::PageSize()) {
        if (IamMasterProcess()) {
            debugs(54, DBG_IMPORTANT, "WARNING: mem-cache size is too small ("
                   << (Ipc::Mem::Limit() / 1024.0) << " KB), should be >= " <<
                   (Ipc::Mem::PageSize() / 1024.0) << " KB");
        }
        return;
    }

    if (IamMasterProcess()) {
        Must(!owner);
        const size_t capacity = Ipc::Mem::Limit() / Ipc::Mem::PageSize();
        owner = Ipc::Mem::PagePool::Init(PagePoolId, capacity, Ipc::Mem::PageSize());
    }

    Must(!ThePagePool);
    ThePagePool = new Ipc::Mem::PagePool(PagePoolId);
}

SharedMemPagesRr::~SharedMemPagesRr()
{
    if (!UsingSmp())
        return;

    delete ThePagePool;
    ThePagePool = NULL;
    delete owner;
}
