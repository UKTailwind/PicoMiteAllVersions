/*
 * @cond
 * The following section will be excluded from the documentation.
 */
/* Workaround for some mbedtls source files using INT_MAX without including limits.h */
#include <limits.h>
void *CallocMemory(size_t num, size_t size);
void FreeMemory(void *addr);
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT

#define MBEDTLS_SSL_OUT_CONTENT_LEN 2048
/* Cap the TLS record IN buffer at 8 KB instead of the 16 KB spec default.
   Saves ~8 KB heap per active session. Server cert chains larger than ~7 KB
   will fail the handshake; in practice that excludes a small minority of
   public HTTPS sites (those with 4+ cert chains plus large CT extensions). */
#define MBEDTLS_SSL_IN_CONTENT_LEN 8192

/* lwIP's altcp_tls_mbedtls layer installs its own mbedtls allocator
   (tls_malloc/tls_free backed by lwIP's MEM_SIZE heap) via
   mbedtls_platform_set_calloc_free during altcp_tls_create_config_client.
   That call overwrites any allocator we'd install with
   MBEDTLS_MEMORY_BUFFER_ALLOC_C / MBEDTLS_PLATFORM_MEMORY, so leaving those
   on would mean we'd burn 48 KB on an unused arena while mbedtls runs out
   of the (default 1.6 KB) lwIP heap. Instead, let lwIP own the budget —
   MEM_SIZE in lwipopts.h is sized to accommodate one TLS session. */
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_PLATFORM_CALLOC_MACRO CallocMemory
#define MBEDTLS_PLATFORM_FREE_MACRO FreeMemory

#define MBEDTLS_ALLOW_PRIVATE_ACCESS
/* MBEDTLS_HAVE_TIME enables time-based features (cert NotBefore/NotAfter,
   session resumption freshness).
   MBEDTLS_PLATFORM_MS_TIME_ALT — we provide mbedtls_ms_time() directly.
   MBEDTLS_PLATFORM_TIME_MACRO — compile-time alias: mbedtls_time() becomes
   picomite_mbedtls_time(). (TIME_ALT would require runtime registration of
   a function pointer via mbedtls_platform_set_time(); the MACRO form is
   simpler and avoids an init-ordering hazard.)
   Both implementations use NTP-corrected wall-clock when WEB NTP has run
   (via TimeOffsetToUptime), otherwise fall back to uptime — uptime is fine
   for session resumption but cert expiry will fail unless NTP has been run. */
#define MBEDTLS_HAVE_TIME
#define MBEDTLS_PLATFORM_MS_TIME_ALT
/* Pin mbedtls_time_t to long long so the prototype below doesn't depend on
   <time.h> being included in this header. Pico SDK's time_t is also 64-bit
   so the values match the system time interpretation. */
#define MBEDTLS_PLATFORM_TIME_TYPE_MACRO long long
#define MBEDTLS_PLATFORM_TIME_MACRO picomite_mbedtls_time
/* Prototype visible to every mbedtls TU that includes this config. */
extern long long picomite_mbedtls_time(long long *tp);

#define MBEDTLS_CIPHER_MODE_CBC
#define MBEDTLS_ECP_DP_SECP192R1_ENABLED
#define MBEDTLS_ECP_DP_SECP224R1_ENABLED
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_ECP_DP_SECP521R1_ENABLED
#define MBEDTLS_ECP_DP_SECP192K1_ENABLED
#define MBEDTLS_ECP_DP_SECP224K1_ENABLED
#define MBEDTLS_ECP_DP_SECP256K1_ENABLED
#define MBEDTLS_ECP_DP_BP256R1_ENABLED
#define MBEDTLS_ECP_DP_BP384R1_ENABLED
#define MBEDTLS_ECP_DP_BP512R1_ENABLED
#define MBEDTLS_ECP_DP_CURVE25519_ENABLED
#define MBEDTLS_KEY_EXCHANGE_RSA_ENABLED
#define MBEDTLS_PKCS1_V15
/* RSA-PSS signatures. RSA_C + PKCS1_V15 only cover RSASSA-PKCS1-v1_5
   (OID sha256WithRSAEncryption). A cert signed RSASSA-PSS carries a
   different signatureAlgorithm OID (rsassa-pss) whose descriptor is
   compiled out without PKCS1_V21, so mbedtls_x509_crt_parse_der fails with
   UNKNOWN_SIG_ALG and the handshake aborts (ERR_CLSD -15) — the same parse
   trap as the missing SHA384_C above. PKCS1_V21 adds the PSS/MGF1 padding;
   X509_RSASSA_PSS_SUPPORT lets the cert parser read the PSS parameters.
   A growing minority of RSA certs (and all RSA certs under TLS 1.3) use PSS. */
#define MBEDTLS_PKCS1_V21
#define MBEDTLS_X509_RSASSA_PSS_SUPPORT
#define MBEDTLS_SHA256_SMALLER
#define MBEDTLS_SSL_SERVER_NAME_INDICATION
#define MBEDTLS_AES_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_CTR_DRBG_C
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_ERROR_C
#define MBEDTLS_MD_C
#define MBEDTLS_MD5_C
#define MBEDTLS_OID_C
/* Needed for WEB TLS CA — loaded CA files are PEM-encoded. Without
   MBEDTLS_PEM_PARSE_C the parser only accepts DER (binary) and silently
   rejects valid PEM input, surfacing as "Failed to parse CA bundle".
   MBEDTLS_BASE64_C is a prerequisite of the PEM parser. */
#define MBEDTLS_PEM_PARSE_C
#define MBEDTLS_BASE64_C
#define MBEDTLS_PKCS5_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_RSA_C
#define MBEDTLS_SHA1_C
#define MBEDTLS_SHA224_C
#define MBEDTLS_SHA256_C
/* SHA-384 is REQUIRED, not optional. In mbedtls 3.x SHA384_C is a separate
   switch from SHA512_C, and MBEDTLS_MD_CAN_SHA384 is derived only from it.
   Without it the ecdsa-with-SHA384 / sha384WithRSA OID descriptors are
   compiled out (oid.c), so mbedtls_x509_crt_parse_der FAILS to parse any
   certificate signed with SHA-384 — which is every Let's Encrypt ECDSA leaf
   (E5-E8 intermediates) plus many RSA chains. The handshake then aborts
   before verification even runs, surfacing as lwIP ERR_CLSD ("TLS client
   error -15"). Near-zero flash cost: SHA-384 shares sha512.c, already
   compiled via SHA512_C below. */
#define MBEDTLS_SHA384_C
#define MBEDTLS_SHA512_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_SRV_C
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_X509_USE_C
#define MBEDTLS_AES_FEWER_TABLES

/* TLS 1.2 */
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_GCM_C
/* ChaCha20-Poly1305 AEAD. Enables the ECDHE-RSA / ECDHE-ECDSA
   -CHACHA20-POLY1305-SHA256 suites (ssl_ciphersuites.c). Not an interop
   requirement — every server offering ChaCha20 also offers AES-GCM, which
   we already have — but the RP2350 has NO AES hardware, so ChaCha20-Poly1305
   is faster in software than AES-GCM and lets the client prefer it. Only
   prerequisites are CHACHA20_C + POLY1305_C (check_config.h:264); both
   source files were already in the mbedtls tree. */
#define MBEDTLS_CHACHA20_C
#define MBEDTLS_POLY1305_C
#define MBEDTLS_CHACHAPOLY_C
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ASN1_WRITE_C
/*  @endcond */
