//**********************************************************************;
// Copyright (c) 2015-2018, Intel Corporation
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// 3. Neither the name of Intel Corporation nor the names of its contributors
// may be used to endorse or promote products derived from this software without
// specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
// THE POSSIBILITY OF SUCH DAMAGE.
//**********************************************************************;

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <curl/curl.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/sha.h>

#include "tpm2_options.h"
#include "log.h"
#include "files.h"
#include "tpm2_alg_util.h"
#include "tpm2_password_util.h"
#include "tpm2_util.h"

typedef struct tpm_getmanufec_ctx tpm_getmanufec_ctx;
struct tpm_getmanufec_ctx {
    char *output_file;
    TPMS_AUTH_COMMAND endorse_session_data;
    TPMS_AUTH_COMMAND owner_session_data;
    TPM2B_AUTH ek_password;
    char *ec_cert_path;
    bool hex_passwd;
    TPM2_HANDLE persistent_handle;
    UINT32 algorithm_type;
    FILE *ec_cert_file;
    char *ek_server_addr;
    unsigned int non_persistent_read;
    unsigned int SSL_NO_VERIFY;
    char *ek_path;
    bool verbose;
    TPM2B_PUBLIC outPublic;
};

static tpm_getmanufec_ctx ctx = {
    .algorithm_type = TPM2_ALG_RSA,
    .endorse_session_data = TPMS_AUTH_COMMAND_INIT(TPM2_RS_PW),
    .owner_session_data = TPMS_AUTH_COMMAND_INIT(TPM2_RS_PW),
};

BYTE authPolicy[] = {0x83, 0x71, 0x97, 0x67, 0x44, 0x84, 0xB3, 0xF8,
                     0x1A, 0x90, 0xCC, 0x8D, 0x46, 0xA5, 0xD7, 0x24,
                     0xFD, 0x52, 0xD7, 0x6E, 0x06, 0x52, 0x0B, 0x64,
                     0xF2, 0xA1, 0xDA, 0x1B, 0x33, 0x14, 0x69, 0xAA};

int set_key_algorithm(TPM2B_PUBLIC *inPublic) {
    inPublic->publicArea.nameAlg = TPM2_ALG_SHA256;
    // First clear attributes bit field.
    *(UINT32 *)&(inPublic->publicArea.objectAttributes) = 0;
    inPublic->publicArea.objectAttributes |= TPMA_OBJECT_RESTRICTED;
    inPublic->publicArea.objectAttributes &= ~TPMA_OBJECT_USERWITHAUTH;
    inPublic->publicArea.objectAttributes |= TPMA_OBJECT_ADMINWITHPOLICY;
    inPublic->publicArea.objectAttributes &= ~TPMA_OBJECT_SIGN_ENCRYPT;
    inPublic->publicArea.objectAttributes |= TPMA_OBJECT_DECRYPT;
    inPublic->publicArea.objectAttributes |= TPMA_OBJECT_FIXEDTPM;
    inPublic->publicArea.objectAttributes |= TPMA_OBJECT_FIXEDPARENT;
    inPublic->publicArea.objectAttributes |= TPMA_OBJECT_SENSITIVEDATAORIGIN;
    inPublic->publicArea.authPolicy.size = 32;
    memcpy(inPublic->publicArea.authPolicy.buffer, authPolicy, 32);

    inPublic->publicArea.type = ctx.algorithm_type;

    switch (ctx.algorithm_type) {
    case TPM2_ALG_RSA:
        inPublic->publicArea.parameters.rsaDetail.symmetric.algorithm = TPM2_ALG_AES;
        inPublic->publicArea.parameters.rsaDetail.symmetric.keyBits.aes = 128;
        inPublic->publicArea.parameters.rsaDetail.symmetric.mode.aes = TPM2_ALG_CFB;
        inPublic->publicArea.parameters.rsaDetail.scheme.scheme = TPM2_ALG_NULL;
        inPublic->publicArea.parameters.rsaDetail.keyBits = 2048;
        inPublic->publicArea.parameters.rsaDetail.exponent = 0x0;
        inPublic->publicArea.unique.rsa.size = 256;
        break;
    case TPM2_ALG_KEYEDHASH:
        inPublic->publicArea.parameters.keyedHashDetail.scheme.scheme = TPM2_ALG_XOR;
        inPublic->publicArea.parameters.keyedHashDetail.scheme.details.exclusiveOr.hashAlg = TPM2_ALG_SHA256;
        inPublic->publicArea.parameters.keyedHashDetail.scheme.details.exclusiveOr.kdf = TPM2_ALG_KDF1_SP800_108;
        inPublic->publicArea.unique.keyedHash.size = 0;
        break;
    case TPM2_ALG_ECC:
        inPublic->publicArea.parameters.eccDetail.symmetric.algorithm = TPM2_ALG_AES;
        inPublic->publicArea.parameters.eccDetail.symmetric.keyBits.aes = 128;
        inPublic->publicArea.parameters.eccDetail.symmetric.mode.sym = TPM2_ALG_CFB;
        inPublic->publicArea.parameters.eccDetail.scheme.scheme = TPM2_ALG_NULL;
        inPublic->publicArea.parameters.eccDetail.curveID = TPM2_ECC_NIST_P256;
        inPublic->publicArea.parameters.eccDetail.kdf.scheme = TPM2_ALG_NULL;
        inPublic->publicArea.unique.ecc.x.size = 32;
        inPublic->publicArea.unique.ecc.y.size = 32;
        break;
    case TPM2_ALG_SYMCIPHER:
        inPublic->publicArea.parameters.symDetail.sym.algorithm = TPM2_ALG_AES;
        inPublic->publicArea.parameters.symDetail.sym.keyBits.aes = 128;
        inPublic->publicArea.parameters.symDetail.sym.mode.sym = TPM2_ALG_CFB;
        inPublic->publicArea.unique.sym.size = 0;
        break;
    default:
        LOG_ERR("The algorithm type input(%4.4x) is not supported!", ctx.algorithm_type);
        return 1;
    }

    return 0;
}

int createEKHandle(TSS2_SYS_CONTEXT *sapi_context)
{
    UINT32 rval;

    TPM2B_SENSITIVE_CREATE inSensitive = TPM2B_TYPE_INIT(TPM2B_SENSITIVE_CREATE, sensitive);
    TPM2B_PUBLIC inPublic = TPM2B_TYPE_INIT(TPM2B_PUBLIC, publicArea);

    TPM2B_DATA outsideInfo = TPM2B_EMPTY_INIT;
    TPML_PCR_SELECTION creationPCR;

    TPM2B_NAME name = TPM2B_TYPE_INIT(TPM2B_NAME, name);

    TPM2B_CREATION_DATA creationData = TPM2B_EMPTY_INIT;
    TPM2B_DIGEST creationHash = TPM2B_TYPE_INIT(TPM2B_DIGEST, buffer);
    TPMT_TK_CREATION creationTicket = TPMT_TK_CREATION_EMPTY_INIT;

    TPM2_HANDLE handle2048ek;

    TSS2L_SYS_AUTH_COMMAND sessionsData = { 1, { ctx.endorse_session_data }};
    TSS2L_SYS_AUTH_RESPONSE sessionsDataOut;

    memcpy(&inSensitive.sensitive.userAuth, &ctx.ek_password,
           sizeof(ctx.ek_password));

    inSensitive.sensitive.data.size = 0;
    inSensitive.size = inSensitive.sensitive.userAuth.size + 2;

    if (set_key_algorithm(&inPublic) )
          return 1;

    creationPCR.count = 0;

    rval = TSS2_RETRY_EXP(Tss2_Sys_CreatePrimary(sapi_context, TPM2_RH_ENDORSEMENT, &sessionsData,
                                  &inSensitive, &inPublic, &outsideInfo,
                                  &creationPCR, &handle2048ek, &ctx.outPublic,
                                  &creationData, &creationHash, &creationTicket,
                                  &name, &sessionsDataOut));
    if (rval != TSS2_RC_SUCCESS ) {
          LOG_ERR("TPM2_CreatePrimary Error. TPM Error:0x%x", rval);
          return 1;
    }
    LOG_INFO("EK create succ.. Handle: 0x%8.8x", handle2048ek);

    if (!ctx.non_persistent_read) {

        if (!ctx.persistent_handle) {
            LOG_ERR("Persistent handle for EK was not provided");
            return 1;
        }

        sessionsData.auths[0] = ctx.owner_session_data;

        rval = TSS2_RETRY_EXP(Tss2_Sys_EvictControl(sapi_context, TPM2_RH_OWNER, handle2048ek,
                                     &sessionsData, ctx.persistent_handle, &sessionsDataOut));
        if (rval != TSS2_RC_SUCCESS ) {
            LOG_ERR("EvictControl:Make EK persistent Error. TPM Error:0x%x", rval);
            return 1;
        }
        LOG_INFO("EvictControl EK persistent succ.");
    }

    rval = TSS2_RETRY_EXP(Tss2_Sys_FlushContext(sapi_context,
                                 handle2048ek));
    if (rval != TSS2_RC_SUCCESS ) {
        LOG_ERR("Flush transient EK failed. TPM Error:0x%x", rval);
        return 1;
    }

    LOG_INFO("Flush transient EK succ.");

    return files_save_public(&ctx.outPublic, ctx.output_file) != true;
}

static unsigned char *HashEKPublicKey(void) {

    unsigned char *hash = (unsigned char*)malloc(SHA256_DIGEST_LENGTH);
    if (!hash) {
        LOG_ERR ("OOM");
        return NULL;
    }


    SHA256_CTX sha256;
    int is_success = SHA256_Init(&sha256);
    if (!is_success) {
        LOG_ERR ("SHA256_Init failed");
        goto err;
    }

    is_success = SHA256_Update(&sha256, ctx.outPublic.publicArea.unique.rsa.buffer,
            ctx.outPublic.publicArea.unique.rsa.size);
    if (!is_success) {
        LOG_ERR ("SHA256_Update failed");
        goto err;
    }

    /* TODO what do these magic bytes line up to? */
    BYTE buf[3] = {
        0x1,
        0x00,
        0x01 //Exponent
    };

    is_success = SHA256_Update(&sha256, buf, sizeof(buf));
    if (!is_success) {
        LOG_ERR ("SHA256_Update failed");
        goto err;
    }

    is_success = SHA256_Final(hash, &sha256);
    if (!is_success) {
        LOG_ERR ("SHA256_Final failed");
        goto err;
    }

    if (ctx.verbose) {
        unsigned i;
        for (i = 0; i < SHA256_DIGEST_LENGTH; i++) {
            printf("%02X", hash[i]);
        }
        printf("\n");
    }

    return hash;
err:
    free(hash);
    return NULL;
}

char *Base64Encode(const unsigned char* buffer)
{
    BIO *bio, *b64;
    BUF_MEM *bufferPtr;
    char *final_string = NULL;

    LOG_INFO("Calculating the Base64Encode of the hash of the Endorsement Public Key:");

    if (buffer == NULL) {
        LOG_ERR("HashEKPublicKey returned null");
        return NULL;
    }

    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(bio, buffer, SHA256_DIGEST_LENGTH);
    int rc = BIO_flush(bio);
    if (rc < 0) {
        LOG_ERR("BIO_flush() failed");
        goto bio_out;
    }

    BIO_get_mem_ptr(bio, &bufferPtr);

    rc = BIO_set_close(bio, BIO_NOCLOSE);
    if (rc < 0) {
        LOG_ERR("BIO_set_close() failed");
        goto bio_out;
    }

    /* these are not NULL terminated */
    char *b64text = bufferPtr->data;
    size_t len = bufferPtr->length;

    size_t i;
    for (i = 0; i < len; i++) {
        if (b64text[i] == '+') {
            b64text[i] = '-';
        }
        if (b64text[i] == '/') {
            b64text[i] = '_';
        }
    }

    CURL *curl = curl_easy_init();
    if (curl) {
        char *output = curl_easy_escape(curl, b64text, len);
        if (output) {
            final_string = strdup(output);
            curl_free(output);
        }
    }
    curl_easy_cleanup(curl);
    curl_global_cleanup();
bio_out:
    BIO_free_all(bio);

    /* format to a proper NULL terminated string */
    return final_string;
}

int RetrieveEndorsementCredentials(char *b64h)
{
    int ret = -1;

    size_t len = 1 + strlen(b64h) + strlen(ctx.ek_server_addr);
    char *weblink = (char *)malloc(len);
    if (!weblink) {
        LOG_ERR("oom");
        return ret;
    }

    snprintf(weblink, len, "%s%s", ctx.ek_server_addr, b64h);

    CURLcode rc = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (rc != CURLE_OK) {
        LOG_ERR("curl_global_init failed: %s", curl_easy_strerror(rc));
        goto out_memory;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        LOG_ERR("curl_easy_init failed");
        goto out_global_cleanup;
    }

    /*
     * should not be used - Used only on platforms with older CA certificates.
     */
    if (ctx.SSL_NO_VERIFY) {
        rc = curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
        if (rc != CURLE_OK) {
            LOG_ERR("curl_easy_setopt for CURLOPT_SSL_VERIFYPEER failed: %s", curl_easy_strerror(rc));
            goto out_easy_cleanup;
        }
    }

    rc = curl_easy_setopt(curl, CURLOPT_URL, weblink);
    if (rc != CURLE_OK) {
        LOG_ERR("curl_easy_setopt for CURLOPT_URL failed: %s", curl_easy_strerror(rc));
        goto out_easy_cleanup;
    }

    /*
     * If verbose is set, add in diagnostic information for debugging connections.
     * https://curl.haxx.se/libcurl/c/CURLOPT_VERBOSE.html
     */
    rc = curl_easy_setopt(curl, CURLOPT_VERBOSE, (long)ctx.verbose);
    if (rc != CURLE_OK) {
        LOG_ERR("curl_easy_setopt for CURLOPT_VERBOSE failed: %s", curl_easy_strerror(rc));
        goto out_easy_cleanup;
    }

    /*
     * If an output file is specified, write to the file, else curl will use stdout:
     * https://curl.haxx.se/libcurl/c/CURLOPT_WRITEDATA.html
     */
    if (ctx.ec_cert_file) {
        rc = curl_easy_setopt(curl, CURLOPT_WRITEDATA, ctx.ec_cert_file);
        if (rc != CURLE_OK) {
            LOG_ERR("curl_easy_setopt for CURLOPT_WRITEDATA failed: %s", curl_easy_strerror(rc));
            goto out_easy_cleanup;
        }
    }

    rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) {
        LOG_ERR("curl_easy_perform() failed: %s", curl_easy_strerror(rc));
        goto out_easy_cleanup;
    }

    ret = 0;

out_easy_cleanup:
    curl_easy_cleanup(curl);
out_global_cleanup:
    curl_global_cleanup();
out_memory:
    free(weblink);

    return ret;
}


int TPMinitialProvisioning(void)
{
    char *b64 = Base64Encode(HashEKPublicKey());
    if (!b64) {
        LOG_ERR("Base64Encode returned null");
        return 1;
    }

    LOG_INFO("%s", b64);

    int rc = RetrieveEndorsementCredentials(b64);
    free(b64);
    return rc;
}

static bool on_option(char key, char *value) {

    bool return_val;
    UINT32 handle;

    switch (key) {
    case 'H':
        if (!tpm2_util_string_to_uint32(value, &ctx.persistent_handle)) {
            LOG_ERR("Please input the handle used to make EK persistent(hex) in correct format.");
            return false;
        }
        break;
    case 'e':
        return_val = tpm2_password_util_from_optarg(value, &ctx.endorse_session_data.hmac);
        if (!return_val) {
            LOG_ERR("Invalid endorsement password, got\"%s\"", optarg);
            return false;
        }
        break;
    case 'o':
        return_val = tpm2_password_util_from_optarg(value, &ctx.owner_session_data.hmac);
        if (!return_val) {
            LOG_ERR("Invalid owner password, got\"%s\"", optarg);
            return false;
        }
        break;
    case 'P':
        return_val = tpm2_password_util_from_optarg(value, &ctx.ek_password);
        if (!return_val) {
            LOG_ERR("Invalid EK password, got\"%s\"", optarg);
            return false;
        }
        break;
    case 'g':
        ctx.algorithm_type = tpm2_alg_util_from_optarg(value);
        if (ctx.algorithm_type == TPM2_ALG_ERROR) {
             LOG_ERR("Please input the algorithm type in correct format.");
            return false;
        }
        break;
    case 'f':
        if (value == NULL ) {
            LOG_ERR("Please input the file used to save the pub ek.");
            return false;
        }
        ctx.output_file = value;
        break;
    case 'E':
        ctx.ec_cert_path = value;
        break;
    case 'N':
        ctx.non_persistent_read = 1;
        break;
    case 'O':
        ctx.ek_path = value;
        break;
    case 'U':
        ctx.SSL_NO_VERIFY = 1;
        LOG_WARN("TLS communication with the said TPM manufacturer server setup with SSL_NO_VERIFY!");
        break;
    case 'S':
        return_val = tpm2_util_string_to_uint32(value, &handle);
        if (!return_val) {
            LOG_ERR("Could not convert session handle to number, got: \"%s\"",
                    value);
            return false;
        }

        ctx.owner_session_data.sessionHandle =
        ctx.endorse_session_data.sessionHandle = handle;

        break;
    }

    return true;
}

static bool on_args(int argc, char **argv) {

    if (argc > 1) {
        LOG_ERR("Only supports one remote server url, got: %d", argc);
        return false;
    }

    ctx.ek_server_addr = argv[0];

    return true;
}

bool tpm2_tool_onstart(tpm2_options **opts) {

    const struct option topts[] =
    {
        { "endorse-passwd", required_argument, NULL, 'e' },
        { "owner-passwd", required_argument, NULL, 'o' },
        { "handle", required_argument, NULL, 'H' },
        { "ek-passwd", required_argument, NULL, 'P' },
        { "alg", required_argument, NULL, 'g' },
        { "output", required_argument, NULL, 'f' },
        { "non-persistent", no_argument, NULL, 'N' },
        { "offline", required_argument, NULL, 'O' },
        { "ec-cert", required_argument, NULL, 'E' },
        { "SSL-NO-VERIFY", no_argument, NULL, 'U' },
        { "input-session-handle", required_argument,NULL,'S'},
    };

    *opts = tpm2_options_new("e:o:H:P:g:f:NO:E:S:i:U", ARRAY_LEN(topts), topts,
            on_option, on_args, TPM2_OPTIONS_SHOW_USAGE);

    return *opts != NULL;
}

int tpm2_tool_onrun(TSS2_SYS_CONTEXT *sapi_context, tpm2_option_flags flags) {

    UNUSED(flags);
    int return_val = 1;
    int provisioning_return_val = 0;

    if (!ctx.ek_server_addr) {
        LOG_ERR("Must specify a remote server url!");
        return 1;
    }

    ctx.verbose = flags.verbose;

    if (ctx.ec_cert_path) {
        ctx.ec_cert_file = fopen(ctx.ec_cert_path, "wb");
        if (!ctx.ec_cert_file) {
            LOG_ERR("Could not open file for writing: \"%s\"", ctx.ec_cert_path);
            return 1;
        }
    }

    if (!ctx.ek_path) {
        return_val = createEKHandle(sapi_context);
    } else {
        bool res = files_load_public(ctx.ek_path, &ctx.outPublic);
        if (!res) {
            LOG_ERR("Could not load exiting EK public from file");
            return 1;
        }
    }

    provisioning_return_val = TPMinitialProvisioning();

    if (return_val && provisioning_return_val) {
        goto err;
    }

    return_val = 0;

err:
    if (ctx.ec_cert_file) {
        fclose(ctx.ec_cert_file);
    }

    return return_val;
}
