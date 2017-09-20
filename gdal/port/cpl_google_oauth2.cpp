/******************************************************************************
 *
 * Project:  Common Portability Library
 * Purpose:  Google OAuth2 Authentication Services
 * Author:   Frank Warmerdam, warmerdam@pobox.com
 *           Even Rouault, even.rouault at spatialys.com
 ******************************************************************************
 * Copyright (c) 2013, Frank Warmerdam
 * Copyright (c) 2017, Even Rouault
 * Copyright (c) 2017, Planet Labs
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 ****************************************************************************/

#include "cpl_http.h"
#include "cpl_port.h"

#include <cstring>

#include "cpl_conv.h"
#include "cpl_error.h"
#include "cpl_string.h"

#ifdef HAVE_CRYPTOPP

/* Begin of crypto++ headers */
#ifdef _MSC_VER
#pragma warning( push )
#pragma warning( disable : 4189 )
#pragma warning( disable : 4512 )
#pragma warning( disable : 4244 )
#endif

#ifdef USE_ONLY_CRYPTODLL_ALG
#include "cryptopp/dll.h"
#else
#include "cryptopp/rsa.h"
#include "cryptopp/queue.h"
#endif

#include "cryptopp/base64.h"
#include "cryptopp/osrng.h"

#ifdef _MSC_VER
#pragma warning( pop )
#endif

#endif // HAVE_CRYPTOPP

#ifdef HAVE_OPENSSL_CRYPTO
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#endif

CPL_CVSID("$Id$")

/* ==================================================================== */
/*      Values related to OAuth2 authorization to use fusion            */
/*      tables.  Many of these values are related to the                */
/*      gdalautotest@gmail.com account for GDAL managed by Even         */
/*      Rouault and Frank Warmerdam.  Some information about OAuth2     */
/*      as managed by that account can be found at the following url    */
/*      when logged in as gdalautotest@gmail.com:                       */
/*                                                                      */
/*      https://code.google.com/apis/console/#project:265656308688:access*/
/*                                                                      */
/*      Applications wanting to use their own client id and secret      */
/*      can set the following configuration options:                    */
/*       - GOA2_CLIENT_ID                                               */
/*       - GOA2_CLIENT_SECRET                                           */
/* ==================================================================== */
#define GDAL_CLIENT_ID "265656308688.apps.googleusercontent.com"
#define GDAL_CLIENT_SECRET "0IbTUDOYzaL6vnIdWTuQnvLz"

#define GOOGLE_AUTH_URL "https://accounts.google.com/o/oauth2"

/************************************************************************/
/*                          ParseSimpleJson()                           */
/*                                                                      */
/*      Return a string list of name/value pairs extracted from a       */
/*      JSON doc.  The Google OAuth2 web service returns simple JSON    */
/*      responses.  The parsing as done currently is very fragile       */
/*      and depends on JSON documents being in a very very simple       */
/*      form.                                                           */
/************************************************************************/

static CPLStringList ParseSimpleJson(const char *pszJson)

{
/* -------------------------------------------------------------------- */
/*      We are expecting simple documents like the following with no    */
/*      hierarchy or complex structure.                                 */
/* -------------------------------------------------------------------- */
/*
    {
        "access_token":"1/fFBGRNJru1FQd44AzqT3Zg",
        "expires_in":3920,
        "token_type":"Bearer"
    }
*/

    CPLStringList oWords(
        CSLTokenizeString2(pszJson, " \n\t,:{}", CSLT_HONOURSTRINGS ));
    CPLStringList oNameValue;

    for( int i=0; i < oWords.size(); i += 2 )
    {
        oNameValue.SetNameValue(oWords[i], oWords[i+1]);
    }

    return oNameValue;
}

/************************************************************************/
/*                      GOA2GetAuthorizationURL()                       */
/************************************************************************/

/**
 * Return authorization url for a given scope.
 *
 * Returns the URL that a user should visit, and use for authentication
 * in order to get an "auth token" indicating their willingness to use a
 * service.
 *
 * Note that when the user visits this url they will be asked to login
 * (using a google/gmail/etc) account, and to authorize use of the
 * requested scope for the application "GDAL/OGR".  Once they have done
 * so, they will be presented with a lengthy string they should "enter
 * into their application".  This is the "auth token" to be passed to
 * GOA2GetRefreshToken().  The "auth token" can only be used once.
 *
 * This function should never fail.
 *
 * @param pszScope the service being requested, not yet URL encoded, such as
 * "https://www.googleapis.com/auth/fusiontables".
 *
 * @return the URL to visit - should be freed with CPLFree().
 */

char *GOA2GetAuthorizationURL(const char *pszScope)

{
    CPLString osScope;
    osScope.Seize(CPLEscapeString(pszScope, -1, CPLES_URL));

    CPLString osURL;
    osURL.Printf(
        "%s/auth?scope=%s&redirect_uri=urn:ietf:wg:oauth:2.0:oob&"
        "response_type=code&client_id=%s",
        GOOGLE_AUTH_URL,
        osScope.c_str(),
        CPLGetConfigOption("GOA2_CLIENT_ID", GDAL_CLIENT_ID));
    return CPLStrdup(osURL);
}

/************************************************************************/
/*                        GOA2GetRefreshToken()                         */
/************************************************************************/

/**
 * Turn Auth Token into a Refresh Token.
 *
 * A one time "auth token" provided by the user is turned into a
 * reusable "refresh token" using a google oauth2 web service.
 *
 * A CPLError will be reported if the translation fails for some reason.
 * Common reasons include the auth token already having been used before,
 * it not being appropriate for the passed scope and configured client api
 * or http connection problems.  NULL is returned on error.
 *
 * @param pszAuthToken the authorization token from the user.
 * @param pszScope the scope for which it is valid.
 *
 * @return refresh token, to be freed with CPLFree(), null on failure.
 */

char CPL_DLL *GOA2GetRefreshToken( const char *pszAuthToken,
                                   const char *pszScope )

{
/* -------------------------------------------------------------------- */
/*      Prepare request.                                                */
/* -------------------------------------------------------------------- */
    CPLString osItem;
    CPLStringList oOptions;

    oOptions.AddString(
        "HEADERS=Content-Type: application/x-www-form-urlencoded");

    osItem.Printf(
        "POSTFIELDS="
        "code=%s"
        "&client_id=%s"
        "&client_secret=%s"
        "&redirect_uri=urn:ietf:wg:oauth:2.0:oob"
        "&grant_type=authorization_code",
        pszAuthToken,
        CPLGetConfigOption("GOA2_CLIENT_ID", GDAL_CLIENT_ID),
        CPLGetConfigOption("GOA2_CLIENT_SECRET", GDAL_CLIENT_SECRET));
    oOptions.AddString(osItem);

/* -------------------------------------------------------------------- */
/*      Submit request by HTTP.                                         */
/* -------------------------------------------------------------------- */
    CPLHTTPResult * psResult =
        CPLHTTPFetch(CPLGetConfigOption("GOA2_AUTH_URL_TOKEN",
                                        GOOGLE_AUTH_URL "/token"), oOptions);

    if( psResult == NULL )
        return NULL;

/* -------------------------------------------------------------------- */
/*      One common mistake is to try and reuse the auth token.          */
/*      After the first use it will return invalid_grant.               */
/* -------------------------------------------------------------------- */
    if( psResult->pabyData != NULL &&
        strstr(reinterpret_cast<char *>(psResult->pabyData), "invalid_grant")
        != NULL )
    {
        CPLString osURL;
        osURL.Seize(GOA2GetAuthorizationURL(pszScope));
        CPLError(CE_Failure, CPLE_AppDefined,
                 "Attempt to use a OAuth2 authorization code multiple times.  "
                 "Request a fresh authorization token at %s.",
                 osURL.c_str());
        CPLHTTPDestroyResult(psResult);
        return NULL;
    }

    if( psResult->pabyData == NULL ||
        psResult->pszErrBuf != NULL )
    {
        if( psResult->pszErrBuf != NULL )
            CPLDebug("GOA2", "%s", psResult->pszErrBuf);
        if( psResult->pabyData != NULL )
            CPLDebug("GOA2", "%s", psResult->pabyData);

        CPLError(CE_Failure, CPLE_AppDefined,
                 "Fetching OAuth2 access code from auth code failed.");
        CPLHTTPDestroyResult(psResult);
        return NULL;
    }

    CPLDebug("GOA2", "Access Token Response:\n%s",
             reinterpret_cast<char *>(psResult->pabyData));

/* -------------------------------------------------------------------- */
/*      This response is in JSON and will look something like:          */
/* -------------------------------------------------------------------- */
/*
{
 "access_token" : "ya29.AHES6ZToqkIJkat5rIqMixR1b8PlWBACNO8OYbqqV-YF1Q13E2Kzjw",
 "token_type" : "Bearer",
 "expires_in" : 3600,
 "refresh_token" : "1/eF88pciwq9Tp_rHEhuiIv9AS44Ufe4GOymGawTVPGYo"
}
*/
    CPLStringList oResponse = ParseSimpleJson(
        reinterpret_cast<char *>(psResult->pabyData));
    CPLHTTPDestroyResult(psResult);

    CPLString osAccessToken = oResponse.FetchNameValueDef("access_token", "");
    CPLString osRefreshToken = oResponse.FetchNameValueDef("refresh_token", "");
    CPLDebug("GOA2", "Access Token : '%s'", osAccessToken.c_str());
    CPLDebug("GOA2", "Refresh Token : '%s'", osRefreshToken.c_str());

    if( osRefreshToken.empty() )
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "Unable to identify a refresh token in the OAuth2 response.");
        return NULL;
    }
    else
    {
        // Currently we discard the access token and just return the
        // refresh token.
        return CPLStrdup(osRefreshToken);
    }
}


/************************************************************************/
/*                       GOA2ProcessResponse()                          */
/************************************************************************/

static char** GOA2ProcessResponse(CPLHTTPResult *psResult)
{

    if( psResult == NULL )
        return NULL;

    if( psResult->pabyData == NULL ||
        psResult->pszErrBuf != NULL )
    {
        if( psResult->pszErrBuf != NULL )
            CPLDebug("GOA2", "%s", psResult->pszErrBuf);
        if( psResult->pabyData != NULL )
            CPLDebug("GOA2", "%s", psResult->pabyData);

        CPLError(CE_Failure, CPLE_AppDefined,
                 "Fetching OAuth2 access code from auth code failed.");
        CPLHTTPDestroyResult(psResult);
        return NULL;
    }

    CPLDebug("GOA2", "Refresh Token Response:\n%s",
             reinterpret_cast<char *>(psResult->pabyData));

/* -------------------------------------------------------------------- */
/*      This response is in JSON and will look something like:          */
/* -------------------------------------------------------------------- */
/*
{
"access_token":"1/fFBGRNJru1FQd44AzqT3Zg",
"expires_in":3920,
"token_type":"Bearer"
}
*/
    CPLStringList oResponse = ParseSimpleJson(
        reinterpret_cast<char *>(psResult->pabyData));
    CPLHTTPDestroyResult(psResult);

    CPLString osAccessToken = oResponse.FetchNameValueDef("access_token", "");

    CPLDebug("GOA2", "Access Token : '%s'", osAccessToken.c_str());

    if( osAccessToken.empty() )
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "Unable to identify an access token in the OAuth2 response.");
        return NULL;
    }

    return oResponse.StealList();
}

/************************************************************************/
/*                       GOA2GetAccessTokenEx()                         */
/************************************************************************/

static char** GOA2GetAccessTokenEx(const char *pszRefreshToken,
                                   const char* pszClientId,
                                   const char* pszClientSecret,
                                   char** /*papszOptions*/)
{

/* -------------------------------------------------------------------- */
/*      Prepare request.                                                */
/* -------------------------------------------------------------------- */
    CPLString osItem;
    CPLStringList oOptions;

    oOptions.AddString(
        "HEADERS=Content-Type: application/x-www-form-urlencoded" );

    osItem.Printf(
        "POSTFIELDS="
        "refresh_token=%s"
        "&client_id=%s"
        "&client_secret=%s"
        "&grant_type=refresh_token",
        pszRefreshToken,
        (pszClientId && !EQUAL(pszClientId, "")) ? pszClientId:
                CPLGetConfigOption("GOA2_CLIENT_ID", GDAL_CLIENT_ID),
        (pszClientSecret && !EQUAL(pszClientSecret, "")) ? pszClientSecret:
                CPLGetConfigOption("GOA2_CLIENT_SECRET", GDAL_CLIENT_SECRET)
    );
    oOptions.AddString(osItem);

/* -------------------------------------------------------------------- */
/*      Submit request by HTTP.                                         */
/* -------------------------------------------------------------------- */
    CPLHTTPResult *psResult = CPLHTTPFetch(
        CPLGetConfigOption("GOA2_AUTH_URL_TOKEN", GOOGLE_AUTH_URL "/token"),
        oOptions);

    return GOA2ProcessResponse(psResult);
}

/************************************************************************/
/*                         GOA2GetAccessToken()                         */
/************************************************************************/

/**
 * Fetch access token using refresh token.
 *
 * The permanent refresh token is used to fetch a temporary (usually one
 * hour) access token using Google OAuth2 web services.
 *
 * A CPLError will be reported if the request fails for some reason.
 * Common reasons include the refresh token having been revoked by the
 * user or http connection problems.
 *
 * @param pszRefreshToken the refresh token from GOA2GetRefreshToken().
 * @param pszScope the scope for which it is valid. Currently unused
 *
 * @return access token, to be freed with CPLFree(), null on failure.
 */

char *GOA2GetAccessToken( const char *pszRefreshToken,
                          CPL_UNUSED const char * pszScope )
{
    char** papszRet = GOA2GetAccessTokenEx(pszRefreshToken, NULL, NULL, NULL);
    const char* pszAccessToken = CSLFetchNameValue(papszRet,
                                                   "access_token");
    char* pszRet = pszAccessToken ? CPLStrdup(pszAccessToken) : NULL;
    CSLDestroy(papszRet);
    return pszRet;
}

/************************************************************************/
/*               GOA2GetAccessTokenFromCloudEngineVM()                  */
/************************************************************************/

/**
 * Fetch access token using Cloud Engine internal REST API
 *
 * The default service accounts bound to the current Google Cloud Engine VM
 * is used for OAuth2 authentication
 *
 * A CPLError will be reported if the request fails for some reason.
 * Common reasons include the refresh token having been revoked by the
 * user or http connection problems.
 *
 * @param papszOptions NULL terminated list of options. None currently
 *
 * @return a list of key=value pairs, including a access_token and expires_in
 * @since GDAL 2.3
 */

char **GOA2GetAccessTokenFromCloudEngineVM( char** papszOptions )
{
    CPLStringList oOptions;

    oOptions.AddString(
        "HEADERS=Metadata-Flavor: Google" );

/* -------------------------------------------------------------------- */
/*      Submit request by HTTP.                                         */
/* -------------------------------------------------------------------- */
    const char* pszURL =
        CSLFetchNameValueDef(papszOptions,
            "URL",
            CPLGetConfigOption("CPL_GCE_CREDENTIALS_URL",
                "http://metadata.google.internal/computeMetadata/v1/instance/service-accounts/default/token"));
    CPLHTTPResult *psResult = CPLHTTPFetch(pszURL, oOptions);

    return GOA2ProcessResponse(psResult);
}


/************************************************************************/
/*                  CPLOpenSSLNullPassphraseCallback()                  */
/************************************************************************/

#if defined(HAVE_OPENSSL_CRYPTO)

static int CPLOpenSSLNullPassphraseCallback(char * /*buf*/,
                                            int /*size*/,
                                            int /*rwflag*/, void * /*u*/)
{
    CPLError(CE_Failure, CPLE_NotSupported,
             "A passphrase was required for this private key, "
             "but this is not supported");
    return 0;
}

#endif

/************************************************************************/
/*                         CPL_RSA_SHA256_Sign()                        */
/************************************************************************/

static GByte* CPL_RSA_SHA256_Sign(const char* pszPrivateKey,
                                  const void* pabyData,
                                  unsigned int nDataLen,
                                  unsigned int* pnSignatureLen)
{
    *pnSignatureLen = 0;

#ifdef HAVE_CRYPTOPP
  if( EQUAL(CPLGetConfigOption("CPL_RSA_SHA256_Sign", "CRYPTOPP"), "CRYPTOPP") )
  {
    // See https://www.cryptopp.com/wiki/RSA_Cryptography
    // https://www.cryptopp.com/wiki/RSA_Signature_Schemes#RSA_Signature_Scheme_.28PKCS_v1.5.29
    // https://www.cryptopp.com/wiki/Keys_and_Formats#PEM_Encoded_Keys

    CPLString osRSAPrivKey(pszPrivateKey);
    static std::string HEADER = "-----BEGIN PRIVATE KEY-----";
    static std::string HEADER_RSA = "-----BEGIN RSA PRIVATE KEY-----";
    static std::string HEADER_ENCRYPTED = "-----BEGIN ENCRYPTED PRIVATE KEY-----";
    static std::string FOOTER = "-----END PRIVATE KEY-----";

    size_t pos1, pos2;
    pos1 = osRSAPrivKey.find(HEADER);
    if(pos1 == std::string::npos)
    {
        if( osRSAPrivKey.find(HEADER_RSA) != std::string::npos )
        {
            CPLError(CE_Failure, CPLE_AppDefined,
                     "'Traditional' PEM header found, whereas PKCS#8 is "
                     "expected. You can use for example "
                     "'openssl pkcs8 -topk8 -inform pem -in file.key "
                     "-outform pem -nocrypt -out file.pem' to generate "
                     "a compatible PEM file");
        }
        else if( osRSAPrivKey.find(HEADER_ENCRYPTED) != std::string::npos )
        {
            CPLError(CE_Failure, CPLE_AppDefined,
                     "Encrypted PEM header found. Only PKCS#8 unencrypted "
                     "private keys are supported");
        }
        else
        {
            CPLError(CE_Failure, CPLE_AppDefined, "PEM header not found");
        }
        return NULL;
    }

    pos2 = osRSAPrivKey.find(FOOTER, pos1+1);
    if(pos2 == std::string::npos)
    {
        CPLError(CE_Failure, CPLE_AppDefined, "PEM footer not found");
        return NULL;
    }

    // Strip header and footer to get the base64-only portion
    pos1 = pos1 + HEADER.size();
    std::string osKeyB64 = osRSAPrivKey.substr(pos1, pos2 - pos1);

    // Base64 decode, place in a ByteQueue
    CryptoPP::ByteQueue queue;
    CryptoPP::Base64Decoder decoder;

    decoder.Attach(new CryptoPP::Redirector(queue));
    decoder.Put((const byte*)osKeyB64.data(), osKeyB64.length());
    decoder.MessageEnd();

    CryptoPP::RSA::PrivateKey rsaPrivate;
    try
    {
        rsaPrivate.BERDecode(queue);
    }
    catch( const std::exception& e )
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "Exception while decoding private key: %s", e.what());
        return NULL;
    }

    // Check that we have consumed all bytes.
    if( !queue.IsEmpty() )
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "Invalid private key: extraneous trailing bytes");
        return NULL;
    }

    CryptoPP::AutoSeededRandomPool prng;
    bool valid = rsaPrivate.Validate(prng, 3);
    if(!valid)
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "Invalid private key: validation failed");
        return NULL;
    }

    std::string signature;
    try
    {
        typedef CryptoPP::RSASS<CryptoPP::PKCS1v15,
                                CryptoPP::SHA256>::Signer
                                            RSASSA_PKCS1v15_SHA256_Signer;
        RSASSA_PKCS1v15_SHA256_Signer signer(rsaPrivate);

        std::string message;
        message.assign(static_cast<const char*>(pabyData), nDataLen);

        CryptoPP::StringSource stringSource(message, true, 
            new CryptoPP::SignerFilter(prng, signer,
                new CryptoPP::StringSink(signature)));
    }
    catch( const std::exception& e )
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "Exception while signing: %s", e.what());
        return NULL;
    }

    *pnSignatureLen = static_cast<unsigned int>(signature.size());
    GByte* pabySignature = static_cast<GByte*>(CPLMalloc(signature.size()));
    memcpy(pabySignature, signature.c_str(), signature.size());
    return pabySignature;
  }
#endif

#if defined(HAVE_OPENSSL_CRYPTO)
  if( EQUAL(CPLGetConfigOption("CPL_RSA_SHA256_Sign", "OPENSSL"), "OPENSSL") )
  {
    const EVP_MD* digest = EVP_sha256();
    if( digest == NULL )
    {
        CPLError(CE_Failure, CPLE_AppDefined, "EVP_sha256() failed");
        return NULL;
    }

    // Old versions expect a void*, newer a const void*
    BIO* bio = BIO_new_mem_buf(const_cast<void*>(static_cast<const void*>(pszPrivateKey)),
                               static_cast<int>(strlen(pszPrivateKey)));
    if( bio == NULL )
    {
        CPLError(CE_Failure, CPLE_AppDefined, "BIO_new_mem_buf() failed");
        return NULL;
    }
    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, NULL,
                                             CPLOpenSSLNullPassphraseCallback,
                                             NULL);
    BIO_free(bio);
    if( pkey == NULL )
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "PEM_read_bio_PrivateKey() failed");
        return NULL;
    }
    EVP_MD_CTX* md_ctx = EVP_MD_CTX_create();
    CPLAssert(md_ctx != NULL);
    int ret = EVP_SignInit(md_ctx, digest);
    CPLAssert(ret == 1);
    ret = EVP_SignUpdate(md_ctx, pabyData, nDataLen);
    CPLAssert(ret == 1);
    const int nPKeyLength = EVP_PKEY_size(pkey);
    CPLAssert(nPKeyLength > 0);
    GByte* abyBuffer = static_cast<GByte*>(CPLMalloc(nPKeyLength));
    ret = EVP_SignFinal(md_ctx, abyBuffer, pnSignatureLen, pkey);
    if( ret != 1 )
    {
        CPLError(CE_Failure, CPLE_AppDefined, "EVP_SignFinal() failed");
        EVP_MD_CTX_destroy(md_ctx);
        EVP_PKEY_free(pkey);
        CPLFree(abyBuffer);
        return NULL;
    }

    EVP_MD_CTX_destroy(md_ctx);
    EVP_PKEY_free(pkey);
    return abyBuffer;
  }
#endif

    CPL_IGNORE_RET_VAL(pszPrivateKey);
    CPL_IGNORE_RET_VAL(pabyData);
    CPL_IGNORE_RET_VAL(nDataLen);

    CPLError(CE_Failure, CPLE_NotSupported,
             "CPLRSASHA256Sign() not implemented: "
             "GDAL must be built against libcrypto++ or libcrypto (openssl)");
    return NULL;
}


/************************************************************************/
/*                 GOA2GetAccessTokenFromServiceAccount()               */
/************************************************************************/

/**
 * Fetch access token using Service Account OAuth2
 *
 * See https://developers.google.com/identity/protocols/OAuth2ServiceAccount
 *
 * A CPLError will be reported if the request fails for some reason.
 *
 * @param pszPrivateKey Private key as a RSA private key
 * @param pszClientEmail Client email
 * @param pszScope the service being requested
 * @param papszAdditionalClaims additional claims, or NULL
 * @param papszOptions NULL terminated list of options. None currently
 *
 * @return a list of key=value pairs, including a access_token and expires_in
 * @since GDAL 2.3
 */

char** GOA2GetAccessTokenFromServiceAccount(const char* pszPrivateKey,
                                        const char* pszClientEmail,
                                        const char* pszScope,
                                        char** papszAdditionalClaims,
                                        char** papszOptions)
{
    CPL_IGNORE_RET_VAL(papszOptions);

    /** See https://developers.google.com/identity/protocols/OAuth2ServiceAccount
    * and https://jwt.io/ */

    // JWT header '{"alg":"RS256","typ":"JWT"}' encoded in Base64
    const char* pszB64JWTHeader = "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9";
    const char* pszAud = CPLGetConfigOption("GO2A_AUD",
                                "https://www.googleapis.com/oauth2/v4/token");

    CPLString osClaim;
    osClaim = "{\"iss\": \"";
    osClaim += pszClientEmail;
    osClaim += "\", \"scope\": \"";
    osClaim += pszScope;
    osClaim += "\", \"aud\": \"";
    osClaim += pszAud;
    osClaim += "\", \"iat\": ";
    GIntBig now = static_cast<GIntBig>(time(NULL));
    const char* pszNow = CPLGetConfigOption("GOA2_NOW", NULL);
    if( pszNow )
        now = CPLAtoGIntBig(pszNow);
    osClaim += CPLSPrintf(CPL_FRMT_GIB, now);
    osClaim += ", \"exp\": ";
    osClaim += CPLSPrintf(CPL_FRMT_GIB, now +
                    atoi(CPLGetConfigOption("GOA2_EXPIRATION_DELAY", "3600")));
    for( char** papszIter = papszAdditionalClaims;
                    papszIter && *papszIter; ++papszIter )
    {
        char* pszKey = NULL;
        const char* pszValue = CPLParseNameValue(*papszIter, &pszKey);
        if( pszKey && pszValue )
        {
            osClaim += ", \"";
            osClaim += pszKey;
            osClaim += "\": ";
            osClaim += pszValue;
            CPLFree(pszKey);
        }
    }
    osClaim += "}";
#ifdef DEBUG_VERBOSE
    CPLDebug("GOA2", "%s", osClaim.c_str());
#endif

    char* pszB64Claim = CPLBase64Encode(static_cast<int>(osClaim.size()),
                            reinterpret_cast<const GByte*>(osClaim.c_str()));
    // Build string to sign
    CPLString osToSign(CPLString(pszB64JWTHeader) + "." + pszB64Claim);
    CPLFree(pszB64Claim);

    unsigned int nSignatureLen = 0;
    // Sign request
    GByte* pabySignature = CPL_RSA_SHA256_Sign(
                                    pszPrivateKey,
                                    osToSign.c_str(),
                                    static_cast<int>(osToSign.size()),
                                    &nSignatureLen);
    if( pabySignature == NULL )
    {
        return NULL;
    }
    char* pszB64Signature = CPLBase64Encode(nSignatureLen, pabySignature);
    CPLFree(pabySignature);
    // Build signed request
    CPLString osRequest(osToSign + "." + pszB64Signature);
    CPLFree(pszB64Signature);

    // Issue request
    CPLString osPostData("grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Ajwt-bearer&assertion=");
    char* pszAssertionEncoded = CPLEscapeString(osRequest, -1, CPLES_URL);
    CPLString osAssertionEncoded(pszAssertionEncoded);
    CPLFree(pszAssertionEncoded);
    // Required in addition to URL escaping otherwise is considered to be space
    osAssertionEncoded.replaceAll("+", "%2B");
    osPostData += osAssertionEncoded;

    char** papszHTTPOptions = NULL;
    papszHTTPOptions = CSLSetNameValue(papszHTTPOptions, "POSTFIELDS", osPostData);
    CPLHTTPResult* psResult = CPLHTTPFetch(pszAud, papszHTTPOptions);
    CSLDestroy(papszHTTPOptions);

    return GOA2ProcessResponse(psResult);
}


/************************************************************************/
/*                              GOA2Manager()                           */
/************************************************************************/

/** Constructor */
GOA2Manager::GOA2Manager() :
    m_nExpirationTime(0),
    m_eMethod(NONE)
{
}

/************************************************************************/
/*                         SetAuthFromGCE()                             */
/************************************************************************/

/** Specifies that the authentication will be done using the local
 * credentials of the current Google Compute Engine VM 
 * 
 * This queries http://metadata.google.internal/computeMetadata/v1/instance/service-accounts/default/token
 *
 * @param papszOptions NULL terminated list of options.
 * @return true in case of success (no network access is done at this stage)
 */
bool GOA2Manager::SetAuthFromGCE( char** papszOptions )
{
    m_eMethod = GCE;
    m_aosOptions = CPLStringList(papszOptions, FALSE);
    return true;
}

/************************************************************************/
/*                       SetAuthFromRefreshToken()                      */
/************************************************************************/


/** Specifies that the authentication will be done using the OAuth2 client
 * id method.
 *
 * See http://code.google.com/apis/accounts/docs/OAuth2.html
 *
 * @param pszRefreshToken refresh token. Must be non NULL.
 * @param pszClientId client id (may be NULL, in which case the GOA2_CLIENT_ID
 *                    configuration option is used)
 * @param pszClientSecret client secret (may be NULL, in which case the
 *                        GOA2_CLIENT_SECRET configuration option is used)
 * @param papszOptions NULL terminated list of options, or NULL.
 * @return true in case of success (no network access is done at this stage)
 */
bool GOA2Manager::SetAuthFromRefreshToken( const char* pszRefreshToken,
                                           const char* pszClientId,
                                           const char* pszClientSecret,
                                           char** papszOptions )
{
    if( pszRefreshToken == NULL )
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "Refresh token should be set");
        return false;
    }
    m_eMethod = ACCESS_TOKEN_FROM_REFRESH;
    m_osRefreshToken = pszRefreshToken;
    m_osClientId = pszClientId ? pszClientId : "";
    m_osClientSecret = pszClientSecret ? pszClientSecret : "";
    m_aosOptions = CPLStringList(papszOptions, FALSE);
    return true;
}

/************************************************************************/
/*                      SetAuthFromServiceAccount()                     */
/************************************************************************/


/** Specifies that the authentication will be done using the OAuth2 service
 * account method.
 * 
 * See https://developers.google.com/identity/protocols/OAuth2ServiceAccount
 *
 * @param pszPrivateKey RSA private key. Must be non NULL.
 * @param pszClientEmail client email. Must be non NULL.
 * @param pszScope authorization scope. Must be non NULL.
 * @param papszAdditionalClaims NULL terminate list of additional claims, or NULL.
 * @param papszOptions NULL terminated list of options, or NULL.
 * @return true in case of success (no network access is done at this stage)
 */
bool GOA2Manager::SetAuthFromServiceAccount(const char* pszPrivateKey,
                                            const char* pszClientEmail,
                                            const char* pszScope,
                                            char** papszAdditionalClaims,
                                            char** papszOptions )
{
    if( pszPrivateKey == NULL || EQUAL(pszPrivateKey, "") )
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "Private key should be set");
        return false;
    }
    if( pszClientEmail == NULL || EQUAL(pszClientEmail, "") )
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "Client email should be set");
        return false;
    }
    if( pszScope == NULL || EQUAL(pszScope, "") )
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "Scope should be set");
        return false;
    }
    m_eMethod = SERVICE_ACCOUNT;
    m_osPrivateKey = pszPrivateKey;
    m_osClientEmail = pszClientEmail;
    m_osScope = pszScope;
    m_aosAdditionalClaims = CPLStringList(papszAdditionalClaims, FALSE);
    m_aosOptions = CPLStringList(papszOptions, FALSE);
    return true;
}

/************************************************************************/
/*                             GetBearer()                              */
/************************************************************************/

/** Return the access token.
 * 
 * This is the value to append to a "Authorization: Bearer " HTTP header.
 * 
 * A network request is issued only if no access token has been yet queried,
 * or if its expiration delay has been reached.
 *
 * @return the access token, or NULL in case of error.
 */
const char* GOA2Manager::GetBearer() const
{
    time_t nCurTime = time(NULL);
    if( nCurTime < m_nExpirationTime - 5 )
        return m_osCurrentBearer.c_str();

    char** papszRet = NULL;
    if( m_eMethod == GCE )
    {
        papszRet = GOA2GetAccessTokenFromCloudEngineVM(m_aosOptions.List());
    }
    else if( m_eMethod == ACCESS_TOKEN_FROM_REFRESH )
    {
        papszRet = GOA2GetAccessTokenEx(m_osRefreshToken.c_str(),
                                        m_osClientId.c_str(),
                                        m_osClientSecret.c_str(),
                                        m_aosOptions.List());
    }
    else if( m_eMethod == SERVICE_ACCOUNT )
    {
        papszRet = GOA2GetAccessTokenFromServiceAccount(m_osPrivateKey,
                                                        m_osClientEmail,
                                                        m_osScope,
                                                        m_aosAdditionalClaims.List(),
                                                        m_aosOptions.List());
    }

    const char* pszAccessToken = CSLFetchNameValue(papszRet, "access_token");
    const char* pszExpires = CSLFetchNameValue(papszRet, "expires_in");
    if( pszAccessToken && pszExpires )
    {
        m_nExpirationTime = nCurTime + atoi(pszExpires);
        m_osCurrentBearer = pszAccessToken;
    }
    return pszAccessToken;
}
