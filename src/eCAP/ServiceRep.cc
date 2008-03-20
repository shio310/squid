/*
 * DEBUG: section 93    ICAP (RFC 3507) Client
 */

#include "squid.h"
#include "eCAP/ServiceRep.h"

#include "ICAP/TextException.h"
#include "HttpReply.h"
#include "eCAP/ServiceRep.h"
#include "ICAP/ICAPOptions.h"
#include "ConfigParser.h"
#include "ICAP/ICAPConfig.h"
#include "SquidTime.h"

CBDATA_CLASS_INIT(EcapServiceRep);

EcapServiceRep::EcapServiceRep(): method(ICAP::methodNone),
        point(ICAP::pointNone), bypass(false),
        theOptions(NULL),
        theSessionFailures(0), isSuspended(0), notifying(false),
        self(NULL),
        wasAnnouncedUp(true) // do not announce an "up" service at startup
{}

EcapServiceRep::~EcapServiceRep()
{
    changeOptions(0);
}

const char *
EcapServiceRep::methodStr() const
{
    return ICAP::methodStr(method);
}

ICAP::Method
EcapServiceRep::parseMethod(const char *str) const
{
    if (!strncasecmp(str, "REQMOD", 6))
        return ICAP::methodReqmod;

    if (!strncasecmp(str, "RESPMOD", 7))
        return ICAP::methodRespmod;

    return ICAP::methodNone;
}


const char *
EcapServiceRep::vectPointStr() const
{
    return ICAP::vectPointStr(point);
}

ICAP::VectPoint
EcapServiceRep::parseVectPoint(const char *service) const
{
    const char *t = service;
    const char *q = strchr(t, '_');

    if (q)
        t = q + 1;

    if (!strcasecmp(t, "precache"))
        return ICAP::pointPreCache;

    if (!strcasecmp(t, "postcache"))
        return ICAP::pointPostCache;

    return ICAP::pointNone;
}

bool
EcapServiceRep::configure(Pointer &aSelf)
{
    assert(!self && aSelf != NULL);
    self = aSelf;

    char *service_type = NULL;

    ConfigParser::ParseString(&key);
    ConfigParser::ParseString(&service_type);
    ConfigParser::ParseBool(&bypass);
    ConfigParser::ParseString(&uri);

    debugs(3, 5, "eCAPService::parseConfigLine (line " << config_lineno << "): " << key.buf() << " " << service_type << " " << bypass);

    method = parseMethod(service_type);
    point = parseVectPoint(service_type);

    debugs(3, 5, "eCAPService::parseConfigLine (line " << config_lineno << "): service is " << methodStr() << "_" << vectPointStr());

    if (uri.cmp("ecap://", 7) != 0) {
        debugs(3, 0, "eCAPService::parseConfigLine (line " << config_lineno << "): wrong uri: " << uri.buf());
        return false;
    }

    const char *s = uri.buf() + 7;

    const char *e;

    bool have_port = false;

    if ((e = strchr(s, ':')) != NULL) {
        have_port = true;
    } else if ((e = strchr(s, '/')) != NULL) {
        have_port = false;
    } else {
        return false;
    }

    int len = e - s;
    host.limitInit(s, len);
    s = e;

    s++;
    e = strchr(s, '\0');
    len = e - s;

    if (len > 1024) {
        debugs(3, 0, "ecap_service_process (line " << config_lineno << "): long resource name (>1024), probably wrong");
    }

    resource.limitInit(s, len + 1);

    if ((bypass != 0) && (bypass != 1)) {
        return false;
    }

    return true;

};

void EcapServiceRep::invalidate()
{
    assert(self != NULL);
    Pointer savedSelf = self; // to prevent destruction when we nullify self
    self = NULL;

    announceStatusChange("invalidated by reconfigure", false);

    savedSelf = NULL; // may destroy us and, hence, invalidate cbdata(this)
    // TODO: it would be nice to invalidate cbdata(this) when not destroyed
}

void EcapServiceRep::noteFailure() {
    ++theSessionFailures;
    debugs(93,4, theSessionFailures << " ICAPService failures, out of " << 
        TheICAPConfig.service_failure_limit << " allowed " << status());

    if (isSuspended)
        return;

    if (TheICAPConfig.service_failure_limit >= 0 &&
        theSessionFailures > TheICAPConfig.service_failure_limit)
        suspend("too many failures");

    // TODO: Should bypass setting affect how much Squid tries to talk to
    // the ICAP service that is currently unusable and is likely to remain 
    // so for some time? The current code says "no". Perhaps the answer 
    // should be configurable.
}

void EcapServiceRep::suspend(const char *reason) {
    if (isSuspended) {
        debugs(93,4, "keeping ICAPService suspended, also for " << reason);
    } else {
        isSuspended = reason;
        debugs(93,1, "suspending ICAPService for " << reason);
        scheduleUpdate(squid_curtime + TheICAPConfig.service_revival_delay);
        announceStatusChange("suspended", true);
    }
}

bool EcapServiceRep::probed() const
{
    return true; // theLastUpdate != 0;
}

bool EcapServiceRep::hasOptions() const {
    return theOptions && theOptions->valid() && theOptions->fresh();
}

bool EcapServiceRep::up() const
{
    return self != NULL && !isSuspended && hasOptions();
}

bool EcapServiceRep::broken() const
{
    return probed() && !up();
}

bool EcapServiceRep::wantsUrl(const String &urlPath) const
{
    Must(hasOptions());
    return theOptions->transferKind(urlPath) != ICAPOptions::xferIgnore;
}

bool EcapServiceRep::wantsPreview(const String &urlPath, size_t &wantedSize) const
{
    Must(hasOptions());

    if (theOptions->preview < 0)
        return false;

    if (theOptions->transferKind(urlPath) != ICAPOptions::xferPreview)
        return false;

    wantedSize = theOptions->preview;

    return true;
}

bool EcapServiceRep::allows204() const
{
    Must(hasOptions());
    return true; // in the future, we may have ACLs to prevent 204s
}


static
void EcapServiceRep_noteTimeToUpdate(void *data)
{
    EcapServiceRep *service = static_cast<EcapServiceRep*>(data);
    Must(service);
    service->noteTimeToUpdate();
}

void EcapServiceRep::noteTimeToUpdate()
{
    if (self != NULL)
        updateScheduled = false;

    if (!self) {
        debugs(93,5, "ICAPService ignores options update " << status());
        return;
    }

    debugs(93,5, "ICAPService performs a regular options update " << status());
    startGettingOptions();
}

static
void EcapServiceRep_noteTimeToNotify(void *data)
{
    EcapServiceRep *service = static_cast<EcapServiceRep*>(data);
    Must(service);
    service->noteTimeToNotify();
}

void EcapServiceRep::noteTimeToNotify()
{
    Must(!notifying);
    notifying = true;
    debugs(93,7, "ICAPService notifies " << theClients.size() << " clients " <<
           status());

    // note: we must notify even if we are invalidated

    Pointer us = NULL;

    while (!theClients.empty()) {
        Client i = theClients.pop_back();
        us = i.service; // prevent callbacks from destroying us while we loop

        if (cbdataReferenceValid(i.data))
            (*i.callback)(i.data, us);

        cbdataReferenceDone(i.data);
    }

    notifying = false;
}

void EcapServiceRep::callWhenReady(Callback *cb, void *data)
{
    debugs(93,5, HERE << "ICAPService is asked to call " << data <<
        " when ready " << status());

    Must(cb);
    Must(self != NULL);
    Must(!broken()); // we do not wait for a broken service

    Client i;
    i.service = self;
    i.callback = cb;
    i.data = cbdataReference(data);
    theClients.push_back(i);

    if (theOptionsFetcher || notifying)
        return; // do nothing, we will be picked up in noteTimeToNotify()

    if (needNewOptions())
        startGettingOptions();
    else
        scheduleNotification();
}

void EcapServiceRep::scheduleNotification()
{
    debugs(93,7, "ICAPService will notify " << theClients.size() << " clients");
    eventAdd("EcapServiceRep::noteTimeToNotify", &EcapServiceRep_noteTimeToNotify, this, 0, 0, true);
}

bool EcapServiceRep::needNewOptions() const
{
    return self != NULL && !up();
}

void EcapServiceRep::changeOptions(ICAPOptions *newOptions)
{
    debugs(93,8, "ICAPService changes options from " << theOptions << " to " <<
           newOptions << ' ' << status());

    delete theOptions;
    theOptions = newOptions;
    theSessionFailures = 0;
    isSuspended = 0;
    theLastUpdate = squid_curtime;

    checkOptions();
    announceStatusChange("down after an options fetch failure", true);
}

void EcapServiceRep::checkOptions()
{
    if (theOptions == NULL)
        return;

    if (!theOptions->valid()) {
        debugs(93,1, "WARNING: Squid got an invalid ICAP OPTIONS response " <<
            "from service " << uri << "; error: " << theOptions->error);
        return;
    }

    /*
     * Issue a warning if the ICAP server returned methods in the
     * options response that don't match the method from squid.conf.
     */

    if (!theOptions->methods.empty()) {
        bool method_found = false;
        String method_list;
        Vector <ICAP::Method>::iterator iter = theOptions->methods.begin();

        while (iter != theOptions->methods.end()) {

            if (*iter == method) {
                method_found = true;
                break;
            }

            method_list.append(ICAP::methodStr(*iter));
            method_list.append(" ", 1);
            iter++;
        }

        if (!method_found) {
            debugs(93,1, "WARNING: Squid is configured to use ICAP method " <<
                   ICAP::methodStr(method) <<
                   " for service " << uri.buf() <<
                   " but OPTIONS response declares the methods are " << method_list.buf());
        }
    }


    /*
     *  Check the ICAP server's date header for clock skew
     */
    const int skew = (int)(theOptions->timestamp() - squid_curtime);
    if (abs(skew) > theOptions->ttl()) {
        // TODO: If skew is negative, the option will be considered down
        // because of stale options. We should probably change this.
        debugs(93, 1, "ICAP service's clock is skewed by " << skew <<
            " seconds: " << uri.buf());
    }
}

void EcapServiceRep::announceStatusChange(const char *downPhrase, bool important) const
{
    if (wasAnnouncedUp == up()) // no significant changes to announce
        return;

    const char *what = bypass ? "optional" : "essential";
    const char *state = wasAnnouncedUp ? downPhrase : "up";
    const int level = important ? 1 : 2;
    debugs(93,level, what << " ICAP service is " << state << ": " << uri <<
        ' ' << status());

    wasAnnouncedUp = !wasAnnouncedUp;
}

static
void EcapServiceRep_noteGenerateOptions(void *data)
{
    EcapServiceRep *service = static_cast<EcapServiceRep*>(data);
    Must(service);
    service->noteGenerateOptions();
}

// we are receiving ICAP OPTIONS response headers here or NULL on failures
void EcapServiceRep::noteGenerateOptions()
{
    Must(theOptionsFetcher);
    theOptionsFetcher = NULL;

    debugs(93,5, "ICAPService is generating new options " << status());

    ICAPOptions *newOptions = new ICAPOptions;
    throw TexcHere("configure eCAP options");

    handleNewOptions(newOptions);
}

void EcapServiceRep::handleNewOptions(ICAPOptions *newOptions)
{
    // new options may be NULL
    changeOptions(newOptions);

    debugs(93,3, "ICAPService got new options and is now " << status());

    scheduleUpdate(optionsFetchTime());
    scheduleNotification();
}

void EcapServiceRep::startGettingOptions()
{
    Must(!theOptionsFetcher);
    debugs(93,6, "ICAPService will generate new options " << status());

    theOptionsFetcher = &EcapServiceRep_noteGenerateOptions;
    eventAdd("EcapServiceRep::GenerateOptions",
             theOptionsFetcher, this, 0, 0, true);
}

void EcapServiceRep::scheduleUpdate(time_t when)
{
    if (updateScheduled) {
        debugs(93,7, "ICAPService reschedules update");
        // XXX: check whether the event is there because AR saw
        // an unreproducible eventDelete assertion on 2007/06/18
        if (eventFind(&EcapServiceRep_noteTimeToUpdate, this))
            eventDelete(&EcapServiceRep_noteTimeToUpdate, this);
        else
            debugs(93,1, "XXX: ICAPService lost an update event.");
        updateScheduled = false;
    }

    debugs(93,7, HERE << "raw OPTIONS fetch at " << when << " or in " <<
        (when - squid_curtime) << " sec");
    debugs(93,9, HERE << "last fetched at " << theLastUpdate << " or " <<
        (squid_curtime - theLastUpdate) << " sec ago");

    /* adjust update time to prevent too-frequent updates */

    if (when < squid_curtime)
        when = squid_curtime;

    // XXX: move hard-coded constants from here to TheICAPConfig
    const int minUpdateGap = 30; // seconds
    if (when < theLastUpdate + minUpdateGap)
        when = theLastUpdate + minUpdateGap;

    const int delay = when - squid_curtime;
    debugs(93,5, "ICAPService will fetch OPTIONS in " << delay << " sec");

    eventAdd("EcapServiceRep::noteTimeToUpdate",
             &EcapServiceRep_noteTimeToUpdate, this, delay, 0, true);
    updateScheduled = true;
}

// returns absolute time when OPTIONS should be fetched
time_t
EcapServiceRep::optionsFetchTime() const
{
    if (theOptions && theOptions->valid()) {
        const time_t expire = theOptions->expire();
        debugs(93,7, "ICAPService options expire on " << expire << " >= " << squid_curtime);

        // conservative estimate of how long the OPTIONS transaction will take
        // XXX: move hard-coded constants from here to TheICAPConfig
        const int expectedWait = 20; // seconds

        // Unknown or invalid (too small) expiration times should not happen.
        // ICAPOptions should use the default TTL, and ICAP servers should not
        // send invalid TTLs, but bugs and attacks happen.
        if (expire < expectedWait)
            return squid_curtime;
        else
            return expire - expectedWait; // before the current options expire
    }

    // use revival delay as "expiration" time for a service w/o valid options
    return squid_curtime + TheICAPConfig.service_revival_delay;
}

// returns a temporary string depicting service status, for debugging
const char *EcapServiceRep::status() const
{
    static MemBuf buf;

    buf.reset();
    buf.append("[", 1);

    if (up())
        buf.append("up", 2);
    else {
        buf.append("down", 4);
        if (!self)
            buf.append(",gone", 5);
        if (isSuspended)
            buf.append(",susp", 5);

        if (!theOptions)
            buf.append(",!opt", 5);
        else
        if (!theOptions->valid())
            buf.append(",!valid", 7);
        else
        if (!theOptions->fresh())
            buf.append(",stale", 6);
    }

    if (theOptionsFetcher)
        buf.append(",fetch", 6);

    if (notifying)
        buf.append(",notif", 6);

    if (theSessionFailures > 0)
        buf.Printf(",fail%d", theSessionFailures);

    buf.append("]", 1);
    buf.terminate();

    return buf.content();
}
