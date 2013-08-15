#ifndef SQUID_ACL_GADGETS_H
#define SQUID_ACL_GADGETS_H

#include "err_type.h"
#include "acl/forward.h"

#if HAVE_SSTREAM
#include <sstream>
#endif

class ConfigParser;
class dlink_list;
class StoreEntry;
class wordlist;

/// \ingroup ACLAPI
void aclDestroyAccessList(acl_access **list);
/// \ingroup ACLAPI
void aclDestroyAcls(ACL **);
/// \ingroup ACLAPI
void aclDestroyAclList(ACLList **);
/// Parses a single line of a "action followed by acls" directive (e.g., http_access).
/// \ingroup ACLAPI
void aclParseAccessLine(const char *directive, ConfigParser &parser, Acl::Tree **);
/// Parses a single line of a "some context followed by acls" directive (e.g., note n v).
/// The label parameter identifies the context (for debugging).
/// \ingroup ACLAPI
void aclParseAclList(ConfigParser &parser, Acl::Tree **, const char *label);
/// Template to convert various context lables to strings. \ingroup ACLAPI
template <class Any>
inline
void aclParseAclList(ConfigParser &parser, Acl::Tree **tree, const Any any)
{
    std::ostringstream buf;
    buf << any;
    aclParseAclList(parser, tree, buf.str().c_str());
}

/// \ingroup ACLAPI
int aclIsProxyAuth(const char *name);
/// \ingroup ACLAPI
err_type aclGetDenyInfoPage(AclDenyInfoList ** head, const char *name, int redirect_allowed);
/// \ingroup ACLAPI
void aclParseDenyInfoLine(AclDenyInfoList **);
/// \ingroup ACLAPI
void aclDestroyDenyInfoList(AclDenyInfoList **);
/// \ingroup ACLAPI
wordlist *aclDumpGeneric(const ACL *);
/// \ingroup ACLAPI
void aclCacheMatchFlush(dlink_list * cache);
/// \ingroup ACLAPI
void dump_acl_access(StoreEntry * entry, const char *name, acl_access * head);
/// \ingroup ACLAPI
void dump_acl_list(StoreEntry * entry, ACLList * head);

#endif /* SQUID_ACL_GADGETS_H */
