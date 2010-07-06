#define SQUID_UNIT_TEST 1

#include "squid.h"
#include "testAuth.h"
#include "auth/Gadgets.h"
#include "auth/UserRequest.h"
#include "auth/Scheme.h"
#include "auth/Config.h"
#include "Mem.h"

CPPUNIT_TEST_SUITE_REGISTRATION( testAuth );
CPPUNIT_TEST_SUITE_REGISTRATION( testAuthConfig );
CPPUNIT_TEST_SUITE_REGISTRATION( testAuthUserRequest );
#if HAVE_AUTH_MODULE_BASIC
CPPUNIT_TEST_SUITE_REGISTRATION( testAuthBasicUserRequest );
#endif
#if HAVE_AUTH_MODULE_DIGEST
CPPUNIT_TEST_SUITE_REGISTRATION( testAuthDigestUserRequest );
#endif
#if HAVE_AUTH_MODULE_NTLM
CPPUNIT_TEST_SUITE_REGISTRATION( testAuthNTLMUserRequest );
#endif
#if HAVE_AUTH_MODULE_NEGOTIATE
CPPUNIT_TEST_SUITE_REGISTRATION( testAuthNegotiateUserRequest );
#endif

/* Instantiate all auth framework types */
void
testAuth::instantiate()
{}

char const * stub_config="auth_param digest program /home/robertc/install/squid/libexec/digest_pw_auth /home/robertc/install/squid/etc/digest.pwd\n"
                         "auth_param digest children 5\n"
                         "auth_param digest realm Squid proxy-caching web server\n"
                         "auth_param digest nonce_garbage_interval 5 minutes\n"
                         "auth_param digest nonce_max_duration 30 minutes\n"
                         "auth_param digest nonce_max_count 50\n";

static
char const *
find_proxy_auth(char const *type)
{
    char const * proxy_auths[][2]= { {"basic","Basic QWxhZGRpbjpvcGVuIHNlc2FtZQ=="},

        {"digest", "Digest username=\"robertdig\", realm=\"Squid proxy-caching web server\", nonce=\"yy8rQXjEWwixXVBj\", uri=\"/images/bg8.gif\", response=\"f75a7d3edd48d93c681c75dc4fb58700\", qop=auth, nc=00000012, cnonce=\"e2216641961e228e\" "},
        {"ntlm", "NTLM "},
        {"negotiate", "Negotiate "}
    };

    for (unsigned count = 0; count < 4 ; count++) {
        if (strcasecmp(type, proxy_auths[count][0]) == 0)
            return proxy_auths[count][1];
    }

    return NULL;
}

static
AuthConfig *
getConfig(char const *type_str)
{
    Auth::authConfig &config = Auth::TheConfig;
    /* find a configuration for the scheme */
    AuthConfig *scheme = AuthConfig::Find(type_str);

    if (scheme == NULL) {
        /* Create a configuration */
        AuthScheme::Pointer theScheme = AuthScheme::Find(type_str);

        if (theScheme == NULL) {
            return NULL;
            //fatalf("Unknown authentication scheme '%s'.\n", type_str);
        }

        config.push_back(theScheme->createConfig());
        scheme = config.back();
        assert(scheme);
    }

    return scheme;
}

static
void
setup_scheme(AuthConfig *scheme, char const **params, unsigned param_count)
{
    Auth::authConfig &config = Auth::TheConfig;

    for (unsigned position=0; position < param_count; position++) {
        char *param_str=xstrdup(params[position]);
        strtok(param_str, w_space);
        scheme->parse(scheme, config.size(), param_str);
    }
}

static
void
fake_auth_setup()
{
    static bool setup(false);

    if (setup)
        return;

    Mem::Init();

    Auth::authConfig &config = Auth::TheConfig;

    char const *digest_parms[]= {"program /home/robertc/install/squid/libexec/digest_pw_auth /home/robertc/install/squid/etc/digest.pwd",
                                 "realm foo"
                                };

    char const *basic_parms[]= {"program /home/robertc/install/squid/libexec/digest_pw_auth /home/robertc/install/squid/etc/digest.pwd",
                                "realm foo"
                               };

    char const *ntlm_parms[]= {"program /home/robertc/install/squid/libexec/digest_pw_auth /home/robertc/install/squid/etc/digest.pwd"};

    char const *negotiate_parms[]= {"program /home/robertc/install/squid/libexec/digest_pw_auth /home/robertc/install/squid/etc/digest.pwd"};

    struct _scheme_params {
        char const *name;
        char const **params;
        unsigned paramlength;
    }

    params[]={ {"digest", digest_parms, 2},
        {"basic", basic_parms, 2},
        {"ntlm", ntlm_parms, 1},
        {"negotiate", negotiate_parms, 1}
    };

    for (unsigned scheme=0; scheme < 4; scheme++) {
        AuthConfig *schemeConfig;
        schemeConfig = getConfig(params[scheme].name);
        if (schemeConfig != NULL)
            setup_scheme(schemeConfig, params[scheme].params,
                         params[scheme].paramlength);
        else
            fprintf(stderr,"Skipping unknown authentication scheme '%s'.\n",
                    params[scheme].name);
    }

    authenticateInit(&config);

    setup=true;
}

/* AuthConfig::CreateAuthUser works for all
 * authentication types
 */
void
testAuthConfig::create()
{
    Debug::Levels[29]=9;
    fake_auth_setup();

    for (AuthScheme::iterator i = AuthScheme::GetSchemes().begin(); i != AuthScheme::GetSchemes().end(); ++i) {
        AuthUserRequest::Pointer authRequest = AuthConfig::CreateAuthUser(find_proxy_auth((*i)->type()));
        CPPUNIT_ASSERT(authRequest != NULL);
    }
}

#if HAVE_IOSTREAM
#include <iostream>
#endif

/* AuthUserRequest::scheme returns the correct scheme for all
 * authentication types
 */
void
testAuthUserRequest::scheme()
{
    Debug::Levels[29]=9;
    fake_auth_setup();

    for (AuthScheme::iterator i = AuthScheme::GetSchemes().begin(); i != AuthScheme::GetSchemes().end(); ++i) {
        // create a user request
        // check its scheme matches *i
        AuthUserRequest::Pointer authRequest = AuthConfig::CreateAuthUser(find_proxy_auth((*i)->type()));
        CPPUNIT_ASSERT_EQUAL(authRequest->scheme(), *i);
    }
}

#if HAVE_AUTH_MODULE_BASIC
#include "auth/basic/basicUserRequest.h"
#include "auth/basic/auth_basic.h"
/* AuthBasicUserRequest::AuthBasicUserRequest works
 */
void
testAuthBasicUserRequest::construction()
{
    AuthBasicUserRequest();
    AuthBasicUserRequest *temp=new AuthBasicUserRequest();
    delete temp;
}

void
testAuthBasicUserRequest::username()
{
    AuthUserRequest::Pointer temp = new AuthBasicUserRequest();
    BasicUser *basic_auth=new BasicUser(AuthConfig::Find("basic"));
    basic_auth->username("John");
    temp->user(basic_auth);
    CPPUNIT_ASSERT_EQUAL(0, strcmp("John", temp->username()));
}
#endif /* HAVE_AUTH_MODULE_BASIC */

#if HAVE_AUTH_MODULE_DIGEST
#include "auth/digest/auth_digest.h"
/* AuthDigestUserRequest::AuthDigestUserRequest works
 */
void
testAuthDigestUserRequest::construction()
{
    AuthDigestUserRequest();
    AuthDigestUserRequest *temp=new AuthDigestUserRequest();
    delete temp;
}

void
testAuthDigestUserRequest::username()
{
    AuthUserRequest::Pointer temp = new AuthDigestUserRequest();
    DigestUser *duser=new DigestUser(AuthConfig::Find("digest"));
    duser->username("John");
    temp->user(duser);
    CPPUNIT_ASSERT_EQUAL(0, strcmp("John", temp->username()));
}
#endif /* HAVE_AUTH_MODULE_DIGEST */

#if HAVE_AUTH_MODULE_NTLM
#include "auth/ntlm/auth_ntlm.h"
/* AuthNTLMUserRequest::AuthNTLMUserRequest works
 */
void
testAuthNTLMUserRequest::construction()
{
    AuthNTLMUserRequest();
    AuthNTLMUserRequest *temp=new AuthNTLMUserRequest();
    delete temp;
}

void
testAuthNTLMUserRequest::username()
{
    AuthUserRequest::Pointer temp = new AuthNTLMUserRequest();
    NTLMUser *nuser=new NTLMUser(AuthConfig::Find("ntlm"));
    nuser->username("John");
    temp->user(nuser);
    CPPUNIT_ASSERT_EQUAL(0, strcmp("John", temp->username()));
}
#endif /* HAVE_AUTH_MODULE_NTLM */

#if HAVE_AUTH_MODULE_NEGOTIATE
#include "auth/negotiate/auth_negotiate.h"
/* AuthNegotiateUserRequest::AuthNegotiateUserRequest works
 */
void
testAuthNegotiateUserRequest::construction()
{
    AuthNegotiateUserRequest();
    AuthNegotiateUserRequest *temp=new AuthNegotiateUserRequest();
    delete temp;
}

void
testAuthNegotiateUserRequest::username()
{
    AuthUserRequest::Pointer temp = new AuthNegotiateUserRequest();
    NegotiateUser *nuser=new NegotiateUser(AuthConfig::Find("negotiate"));
    nuser->username("John");
    temp->user(nuser);
    CPPUNIT_ASSERT_EQUAL(0, strcmp("John", temp->username()));
}

#endif /* HAVE_AUTH_MODULE_NEGOTIATE */
