//**********************************************************************;
// Copyright (c) 2015, Intel Corporation
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

#include <string.h>

#include "conversion.h"
#include "tpm2_password_util.h"
#include "files.h"
#include "log.h"
#include "tpm2_util.h"
#include "tpm_session.h"
#include "tpm2_alg_util.h"
#include "tpm2_tool.h"

typedef struct getpubak_context getpubak_context;
struct getpubak_context {
    struct {
        TPM2_HANDLE ek;
        TPM2_HANDLE ak;
    } persistent_handle;
    struct {
        TPM2B_AUTH endorse;
        TPM2B_AUTH ak;
        TPM2B_AUTH owner;
    } passwords;
    char *output_file;
    char *akname_file;
    TPM2_ALG_ID algorithm_type;
    TPM2_ALG_ID digest_alg;
    TPM2_ALG_ID sign_alg;
};

static getpubak_context ctx = {
    .algorithm_type = TPM2_ALG_RSA,
    .digest_alg = TPM2_ALG_SHA256,
    .sign_alg = TPM2_ALG_NULL,
    .passwords = {
        .endorse = TPM2B_EMPTY_INIT,
        .ak      = TPM2B_EMPTY_INIT,
        .owner   = TPM2B_EMPTY_INIT,
    },
};

/*
 * TODO: All these set_xxx_signing_algorithm() routines could likely somehow be refactored into one.
 */
static bool set_rsa_signing_algorithm(UINT32 sign_alg, UINT32 digest_alg, TPM2B_PUBLIC *in_public) {

    if (sign_alg == TPM2_ALG_NULL) {
        sign_alg = TPM2_ALG_RSASSA;
    }

    in_public->publicArea.parameters.rsaDetail.scheme.scheme = sign_alg;
    switch (sign_alg) {
    case TPM2_ALG_RSASSA :
    case TPM2_ALG_RSAPSS :
        in_public->publicArea.parameters.rsaDetail.scheme.details.anySig.hashAlg =
                digest_alg;
        break;
    default:
        LOG_ERR("The RSA signing algorithm type input(%4.4x) is not supported!",
                sign_alg);
        return false;
    }

    return true;
}

static bool set_ecc_signing_algorithm(UINT32 sign_alg, UINT32 digest_alg,
        TPM2B_PUBLIC *in_public) {

    if (sign_alg == TPM2_ALG_NULL) {
        sign_alg = TPM2_ALG_ECDSA;
    }

    in_public->publicArea.parameters.eccDetail.scheme.scheme = sign_alg;
    switch (sign_alg) {
    case TPM2_ALG_ECDSA :
    case TPM2_ALG_SM2 :
    case TPM2_ALG_ECSCHNORR :
    case TPM2_ALG_ECDAA :
        in_public->publicArea.parameters.eccDetail.scheme.details.anySig.hashAlg =
                digest_alg;
        break;
    default:
        LOG_ERR("The ECC signing algorithm type input(%4.4x) is not supported!",
                sign_alg);
        return false;
    }

    return true;
}

static bool set_keyed_hash_signing_algorithm(UINT32 sign_alg, UINT32 digest_alg,
        TPM2B_PUBLIC *in_public) {

    if (sign_alg == TPM2_ALG_NULL) {
        sign_alg = TPM2_ALG_HMAC;
    }

    in_public->publicArea.parameters.keyedHashDetail.scheme.scheme = sign_alg;
    switch (sign_alg) {
    case TPM2_ALG_HMAC :
        in_public->publicArea.parameters.keyedHashDetail.scheme.details.hmac.hashAlg =
                digest_alg;
        break;
    default:
        LOG_ERR(
                "The Keyedhash signing algorithm type input(%4.4x) is not supported!",
                sign_alg);
        return false;
    }

    return true;
}

static bool set_key_algorithm(TPM2B_PUBLIC *in_public)
{
    in_public->publicArea.nameAlg = TPM2_ALG_SHA256;
    // First clear attributes bit field.
    in_public->publicArea.objectAttributes = 0;
    in_public->publicArea.objectAttributes |= TPMA_OBJECT_RESTRICTED;
    in_public->publicArea.objectAttributes |= TPMA_OBJECT_USERWITHAUTH;
    in_public->publicArea.objectAttributes |= TPMA_OBJECT_SIGN_ENCRYPT;
    in_public->publicArea.objectAttributes &= ~TPMA_OBJECT_DECRYPT;
    in_public->publicArea.objectAttributes |= TPMA_OBJECT_FIXEDTPM;
    in_public->publicArea.objectAttributes |= TPMA_OBJECT_FIXEDPARENT;
    in_public->publicArea.objectAttributes |= TPMA_OBJECT_SENSITIVEDATAORIGIN;
    in_public->publicArea.authPolicy.size = 0;

    in_public->publicArea.type = ctx.algorithm_type;

    switch(ctx.algorithm_type)
    {
    case TPM2_ALG_RSA:
        in_public->publicArea.parameters.rsaDetail.symmetric.algorithm = TPM2_ALG_NULL;
        in_public->publicArea.parameters.rsaDetail.symmetric.keyBits.aes = 0;
        in_public->publicArea.parameters.rsaDetail.symmetric.mode.aes = TPM2_ALG_NULL;
        in_public->publicArea.parameters.rsaDetail.keyBits = 2048;
        in_public->publicArea.parameters.rsaDetail.exponent = 0;
        in_public->publicArea.unique.rsa.size = 0;
        return set_rsa_signing_algorithm(ctx.sign_alg, ctx.digest_alg, in_public);
    case TPM2_ALG_ECC:
        in_public->publicArea.parameters.eccDetail.symmetric.algorithm = TPM2_ALG_NULL;
        in_public->publicArea.parameters.eccDetail.symmetric.mode.sym = TPM2_ALG_NULL;
        in_public->publicArea.parameters.eccDetail.symmetric.keyBits.sym = 0;
        in_public->publicArea.parameters.eccDetail.curveID = TPM2_ECC_NIST_P256;
        in_public->publicArea.parameters.eccDetail.kdf.scheme = TPM2_ALG_NULL;
        in_public->publicArea.unique.ecc.x.size = 0;
        in_public->publicArea.unique.ecc.y.size = 0;
        return set_ecc_signing_algorithm(ctx.sign_alg, ctx.digest_alg, in_public);
    case TPM2_ALG_KEYEDHASH:
        in_public->publicArea.unique.keyedHash.size = 0;
        return set_keyed_hash_signing_algorithm(ctx.sign_alg, ctx.digest_alg, in_public);
    case TPM2_ALG_SYMCIPHER:
    default:
        LOG_ERR("The algorithm type input(%4.4x) is not supported!", ctx.algorithm_type);
        return false;
    }

    return true;
}

static bool create_ak(TSS2_SYS_CONTEXT *sapi_context) {

    TPML_PCR_SELECTION creation_pcr;
    TSS2L_SYS_AUTH_RESPONSE sessions_data_out;
    TSS2L_SYS_AUTH_COMMAND sessions_data = {1, {
        {
        .sessionHandle = TPM2_RS_PW,
        .nonce = TPM2B_EMPTY_INIT,
        .hmac = TPM2B_EMPTY_INIT,
        .sessionAttributes = 0,
    }}};

    TPM2B_DATA outsideInfo = TPM2B_EMPTY_INIT;
    TPM2B_PUBLIC out_public = TPM2B_EMPTY_INIT;
    TPM2B_NONCE nonce_caller = TPM2B_EMPTY_INIT;
    TPMT_TK_CREATION creation_ticket = TPMT_TK_CREATION_EMPTY_INIT;
    TPM2B_CREATION_DATA creation_data = TPM2B_EMPTY_INIT;
    TPM2B_ENCRYPTED_SECRET encrypted_salt = TPM2B_EMPTY_INIT;

    TPMT_SYM_DEF symmetric = {
            .algorithm = TPM2_ALG_NULL,
    };

    TPM2B_SENSITIVE_CREATE inSensitive = TPM2B_TYPE_INIT(TPM2B_SENSITIVE_CREATE, sensitive);

    TPM2B_PUBLIC inPublic = TPM2B_TYPE_INIT(TPM2B_PUBLIC, publicArea);

    TPM2B_NAME name = TPM2B_TYPE_INIT(TPM2B_NAME, name);

    TPM2B_PRIVATE out_private = TPM2B_TYPE_INIT(TPM2B_PRIVATE, buffer);

    TPM2B_DIGEST creation_hash = TPM2B_TYPE_INIT(TPM2B_DIGEST, buffer);

    TPM2_HANDLE handle_2048_rsa = ctx.persistent_handle.ek;

    inSensitive.sensitive.data.size = 0;
    inSensitive.size = inSensitive.sensitive.userAuth.size + 2;
    creation_pcr.count = 0;

    memcpy(&inSensitive.sensitive.userAuth, &ctx.passwords.ak, sizeof(ctx.passwords.ak));

    bool result = set_key_algorithm(&inPublic);
    if (!result) {
        return false;
    }

    memcpy(&sessions_data.auths[0].hmac, &ctx.passwords.endorse, sizeof(ctx.passwords.endorse));

    SESSION *session = NULL;
    UINT32 rval = tpm_session_start_auth_with_params(sapi_context, &session, TPM2_RH_NULL, 0, TPM2_RH_NULL, 0,
            &nonce_caller, &encrypted_salt, TPM2_SE_POLICY, &symmetric,
            TPM2_ALG_SHA256);
    if (rval != TPM2_RC_SUCCESS) {
        LOG_ERR("tpm_session_start_auth_with_params Error. TPM Error:0x%x", rval);
        return false;
    }

    LOG_INFO("tpm_session_start_auth_with_params succ");

    rval = TSS2_RETRY_EXP(Tss2_Sys_PolicySecret(sapi_context, TPM2_RH_ENDORSEMENT,
            session->sessionHandle, &sessions_data, 0, 0, 0, 0, 0, 0, 0));
    if (rval != TPM2_RC_SUCCESS) {
        LOG_ERR("Tss2_Sys_PolicySecret Error. TPM Error:0x%x", rval);
        return false;
    }

    LOG_INFO("Tss2_Sys_PolicySecret succ");

    sessions_data.auths[0].sessionHandle = session->sessionHandle;
    sessions_data.auths[0].sessionAttributes |= TPMA_SESSION_CONTINUESESSION;
    sessions_data.auths[0].hmac.size = 0;

    rval = TSS2_RETRY_EXP(Tss2_Sys_Create(sapi_context, handle_2048_rsa, &sessions_data,
            &inSensitive, &inPublic, &outsideInfo, &creation_pcr, &out_private,
            &out_public, &creation_data, &creation_hash, &creation_ticket,
            &sessions_data_out));
    if (rval != TPM2_RC_SUCCESS) {
        LOG_ERR("TPM2_Create Error. TPM Error:0x%x", rval);
        return false;
    }
    LOG_INFO("TPM2_Create succ");

    // Need to flush the session here.
    rval = TSS2_RETRY_EXP(Tss2_Sys_FlushContext(sapi_context, session->sessionHandle));
    if (rval != TPM2_RC_SUCCESS) {
        LOG_INFO("TPM2_Sys_FlushContext Error. TPM Error:0x%x", rval);
        return false;
    }
    // And remove the session from sessions table.
    tpm_session_auth_end(session);

    sessions_data.auths[0].sessionHandle = TPM2_RS_PW;
    sessions_data.auths[0].sessionAttributes &= ~TPMA_SESSION_CONTINUESESSION;
    sessions_data.auths[0].hmac.size = 0;

    memcpy(&sessions_data.auths[0].hmac, &ctx.passwords.endorse, sizeof(ctx.passwords.endorse));

    rval = tpm_session_start_auth_with_params(sapi_context, &session, TPM2_RH_NULL, 0, TPM2_RH_NULL, 0,
            &nonce_caller, &encrypted_salt, TPM2_SE_POLICY, &symmetric,
            TPM2_ALG_SHA256);
    if (rval != TPM2_RC_SUCCESS) {
        LOG_ERR("tpm_session_start_auth_with_params Error. TPM Error:0x%x", rval);
        return false;
    }
    LOG_INFO("tpm_session_start_auth_with_params succ");

    rval = TSS2_RETRY_EXP(Tss2_Sys_PolicySecret(sapi_context, TPM2_RH_ENDORSEMENT,
            session->sessionHandle, &sessions_data, 0, 0, 0, 0, 0, 0, 0));
    if (rval != TPM2_RC_SUCCESS) {
        LOG_ERR("Tss2_Sys_PolicySecret Error. TPM Error:0x%x", rval);
        return false;
    }
    LOG_INFO("Tss2_Sys_PolicySecret succ");

    sessions_data.auths[0].sessionHandle = session->sessionHandle;
    sessions_data.auths[0].sessionAttributes |= TPMA_SESSION_CONTINUESESSION;
    sessions_data.auths[0].hmac.size = 0;

    TPM2_HANDLE loaded_sha1_key_handle;
    rval = TSS2_RETRY_EXP(Tss2_Sys_Load(sapi_context, handle_2048_rsa, &sessions_data, &out_private,
            &out_public, &loaded_sha1_key_handle, &name, &sessions_data_out));
    if (rval != TPM2_RC_SUCCESS) {
        LOG_ERR("TPM2_Load Error. TPM Error:0x%x", rval);
        return false;
    }

    /* Output in YAML format */
    tpm2_tool_output("loaded-key:\n");
    tpm2_tool_output("  handle: %8.8x\n  name: ", loaded_sha1_key_handle);
    tpm2_util_print_tpm2b((TPM2B *)&name);
    tpm2_tool_output("\n");

    // write name to ak.name file
    if (ctx.akname_file) {
        result = files_save_bytes_to_file(ctx.akname_file, &name.name[0], name.size);
        if (!result) {
            LOG_ERR("Failed to save AK name into file \"%s\"", ctx.akname_file);
            return false;
        }
    }

    // Need to flush the session here.
    rval = TSS2_RETRY_EXP(Tss2_Sys_FlushContext(sapi_context, session->sessionHandle));
    if (rval != TPM2_RC_SUCCESS) {
        LOG_ERR("TPM2_Sys_FlushContext Error. TPM Error:0x%x", rval);
        return false;
    }

    // And remove the session from sessions table.
    tpm_session_auth_end(session);

    sessions_data.auths[0].sessionHandle = TPM2_RS_PW;
    sessions_data.auths[0].sessionAttributes &= ~TPMA_SESSION_CONTINUESESSION;
    sessions_data.auths[0].hmac.size = 0;

    // use the owner auth here.
    memcpy(&sessions_data.auths[0].hmac, &ctx.passwords.owner, sizeof(ctx.passwords.owner));

    rval = TSS2_RETRY_EXP(Tss2_Sys_EvictControl(sapi_context, TPM2_RH_OWNER, loaded_sha1_key_handle,
            &sessions_data, ctx.persistent_handle.ak, &sessions_data_out));
    if (rval != TPM2_RC_SUCCESS) {
        LOG_ERR("\n......TPM2_EvictControl Error. TPM Error:0x%x......",
                rval);
        return false;
    }
    LOG_INFO("EvictControl: Make AK persistent succ.");

    rval = TSS2_RETRY_EXP(Tss2_Sys_FlushContext(sapi_context, loaded_sha1_key_handle));
    if (rval != TPM2_RC_SUCCESS) {
        LOG_ERR("Flush transient AK error. TPM Error:0x%x", rval);
        return false;
    }
    LOG_INFO("Flush transient AK succ.");

    return tpm2_convert_pubkey(&out_public, pubkey_format_tss, ctx.output_file);
}

static bool on_option(char key, char *value) {

    bool result;

    switch (key) {
    case 'E':
        result = tpm2_util_string_to_uint32(value, &ctx.persistent_handle.ek);
        if (!result) {
            LOG_ERR("Could not convert persistent EK handle.");
            return false;
        }
        break;
    case 'k':
        result = tpm2_util_string_to_uint32(value, &ctx.persistent_handle.ak);
        if (!result) {
            LOG_ERR("Could not convert persistent AK handle.");
            return false;
        }
        break;
    case 'g':
        ctx.algorithm_type = tpm2_alg_util_from_optarg(value);
        if (ctx.algorithm_type == TPM2_ALG_ERROR) {
            LOG_ERR("Could not convert algorithm. got: \"%s\".", value);
            return false;
        }
        break;
    case 'D':
        ctx.digest_alg = tpm2_alg_util_from_optarg(value);
        if (ctx.digest_alg == TPM2_ALG_ERROR) {
            LOG_ERR("Could not convert digest algorithm.");
            return false;
        }
        break;
    case 's':
        ctx.sign_alg = tpm2_alg_util_from_optarg(value);
        if (ctx.sign_alg == TPM2_ALG_ERROR) {
            LOG_ERR("Could not convert signing algorithm.");
            return false;
        }
        break;
    case 'o':
        result = tpm2_password_util_from_optarg(value, &ctx.passwords.owner);
        if (!result) {
            LOG_ERR("Invalid owner password, got\"%s\"", value);
            return false;
        }
        break;
    case 'e':
        result = tpm2_password_util_from_optarg(value, &ctx.passwords.endorse);
        if (!result) {
            LOG_ERR("Invalid endorse password, got\"%s\"", value);
            return false;
        }
        break;
    case 'P':
        result = tpm2_password_util_from_optarg(value, &ctx.passwords.ak);
        if (!result) {
            LOG_ERR("Invalid AK password, got\"%s\"", value);
            return false;
        }
        break;
    case 'f':
        if (!value) {
            LOG_ERR("Please specify the output file used to save the pub ek.");
            return false;
        }
        ctx.output_file = value;
        break;
    case 'n':
        if (!value) {
            LOG_ERR("Please specify the output file used to save the ak name.");
            return false;
        }
        ctx.akname_file = value;
        break;
    }

    return true;
}

bool tpm2_tool_onstart(tpm2_options **opts) {

    const struct option topts[] = {
        { "owner-passwd", required_argument, NULL, 'o' },
        { "endorse-passwd", required_argument, NULL, 'e' },
        { "ek-handle"   , required_argument, NULL, 'E' },
        { "ak-handle"   , required_argument, NULL, 'k' },
        { "alg"        , required_argument, NULL, 'g' },
        { "digest-alg"  , required_argument, NULL, 'D' },
        { "sign-alg"    , required_argument, NULL, 's' },
        { "ak-passwd"   , required_argument, NULL, 'P' },
        { "file"       , required_argument, NULL, 'f' },
        { "ak-name"     , required_argument, NULL, 'n' },
    };

    *opts = tpm2_options_new("o:E:e:k:g:D:s:P:f:n:p:", ARRAY_LEN(topts), topts,
            on_option, NULL, TPM2_OPTIONS_SHOW_USAGE);

    return *opts != NULL;
}

int tpm2_tool_onrun(TSS2_SYS_CONTEXT *sapi_context, tpm2_option_flags flags) {

    UNUSED(flags);

    return !create_ak(sapi_context);
}
