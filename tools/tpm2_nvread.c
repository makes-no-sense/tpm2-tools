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
#include <string.h>

#include "files.h"
#include "pcr.h"
#include "tpm2_nv_util.h"
#include "tpm2_options.h"
#include "tpm2_password_util.h"
#include "tpm2_policy.h"

typedef struct TPM2_nvread_ctx TPM2_nvread_ctx;
struct TPM2_nvread_ctx {
    UINT32 nv_index;
    UINT32 auth_handle;
    UINT32 size_to_read;
    UINT32 offset;
    TPMS_AUTH_COMMAND session_data;
    char *output_file;
    char *raw_pcrs_file;
    SESSION *policy_session;
    TPML_PCR_SELECTION pcr_selection;
    struct {
        UINT8 L : 1;
    } flags;
};

static TPM2_nvread_ctx ctx = {
    .auth_handle = TPM2_RH_PLATFORM,
    .session_data = TPMS_AUTH_COMMAND_INIT(TPM2_RS_PW),
};

static bool nv_read(TSS2_SYS_CONTEXT *sapi_context, tpm2_option_flags flags) {


    TSS2L_SYS_AUTH_RESPONSE sessions_data_out;
    TSS2L_SYS_AUTH_COMMAND sessions_data = { 1, { ctx.session_data }};


    TPM2B_NV_PUBLIC nv_public = TPM2B_EMPTY_INIT;
    TSS2_RC rval = tpm2_util_nv_read_public(sapi_context, ctx.nv_index, &nv_public);
    if (rval != TSS2_RC_SUCCESS) {
        LOG_ERR("Failed to read NVRAM public area at index 0x%x (%d). Error:0x%x",
                ctx.nv_index, ctx.nv_index, rval);
        return false;
    }

    UINT16 data_size = nv_public.nvPublic.dataSize;

    /* if no size was specified, assume the whole object */
    if (ctx.size_to_read == 0) {
        ctx.size_to_read = data_size;
    }

    if (ctx.offset > data_size) {
        LOG_ERR(
            "Requested offset to read from is greater than size. offset=%u"
            ", size=%u", ctx.offset, data_size);
        return false;
    }

    if (ctx.offset + ctx.size_to_read > data_size) {
        LOG_WARN(
            "Requested to read more bytes than available from offset,"
            " truncating read! offset=%u, request-read-size=%u"
            " actual-data-size=%u", ctx.offset, ctx.size_to_read, data_size);
        ctx.size_to_read = data_size - ctx.offset;
        return false;
    }

    UINT32 max_data_size;
    rval = tpm2_util_nv_max_buffer_size(sapi_context, &max_data_size);
    if (rval != TSS2_RC_SUCCESS) {
        return false;
    }

    if (max_data_size > TPM2_MAX_NV_BUFFER_SIZE) {
        max_data_size = TPM2_MAX_NV_BUFFER_SIZE;
    }
    else if (max_data_size == 0) {
        max_data_size = NV_DEFAULT_BUFFER_SIZE;
    }

    UINT8 *data_buffer = malloc(data_size);
    if (!data_buffer) {
        LOG_ERR("oom");
        return false;
    }

    bool result = false;
    UINT16 data_offset = 0;
    while (ctx.size_to_read) {

        UINT16 bytes_to_read = ctx.size_to_read > max_data_size ?
                max_data_size : ctx.size_to_read;

        TPM2B_MAX_NV_BUFFER nv_data = TPM2B_TYPE_INIT(TPM2B_MAX_NV_BUFFER, buffer);

        rval = TSS2_RETRY_EXP(Tss2_Sys_NV_Read(sapi_context, ctx.auth_handle, ctx.nv_index,
                &sessions_data, bytes_to_read, ctx.offset, &nv_data, &sessions_data_out));
        if (rval != TSS2_RC_SUCCESS) {
            LOG_ERR("Failed to read NVRAM area at index 0x%x (%d). Error:0x%x",
                    ctx.nv_index, ctx.nv_index, rval);
            goto out;
        }

        ctx.size_to_read -= nv_data.size;
        ctx.offset += nv_data.size;

        memcpy(data_buffer + data_offset, nv_data.buffer, nv_data.size);
        data_offset += nv_data.size;
    }

    /* dump data_buffer to output file, if specified */
    if (ctx.output_file) {
        if (!files_save_bytes_to_file(ctx.output_file, data_buffer, data_offset)) {
            goto out;
        }
    /* else use stdout if quiet is not specified */
    } else if (!flags.quiet) {
        if (!files_write_bytes(stdout, data_buffer, data_offset)) {
            goto out;
        }
    }

    result = true;

out:
    free(data_buffer);
    return result;
}

static bool on_option(char key, char *value) {

    bool result;

    switch (key) {
    case 'x':
        result = tpm2_util_string_to_uint32(value, &ctx.nv_index);
        if (!result) {
            LOG_ERR("Could not convert NV index to number, got: \"%s\"",
                    optarg);
            return false;
        }

        if (ctx.nv_index == 0) {
            LOG_ERR("NV Index cannot be 0");
            return false;
        }
        break;
    case 'a':
        result = tpm2_util_string_to_uint32(value, &ctx.auth_handle);
        if (!result) {
            LOG_ERR("Could not convert auth handle to number, got: \"%s\"",
                    optarg);
            return false;
        }

        if (ctx.auth_handle == 0) {
            LOG_ERR("Auth handle cannot be 0");
            return false;
        }
        break;
    case 'f':
        ctx.output_file = value;
        break;
    case 'P':
        result = tpm2_password_util_from_optarg(value, &ctx.session_data.hmac);
        if (!result) {
            LOG_ERR("Invalid handle password, got\"%s\"", optarg);
            return false;
        }
        break;
    case 's':
        result = tpm2_util_string_to_uint32(value, &ctx.size_to_read);
        if (!result) {
            LOG_ERR("Could not convert size to number, got: \"%s\"",
                    optarg);
            return false;
        }
        break;
    case 'o':
        result = tpm2_util_string_to_uint32(value, &ctx.offset);
        if (!result) {
            LOG_ERR("Could not convert offset to number, got: \"%s\"",
                    optarg);
            return false;
        }
        break;
    case 'S':
        if (!tpm2_util_string_to_uint32(value, &ctx.session_data.sessionHandle)) {
            LOG_ERR("Could not convert session handle to number, got: \"%s\"",
                    optarg);
            return false;
        }
        break;
    case 'L':
        if (!pcr_parse_selections(value, &ctx.pcr_selection)) {
            return false;
        }
        ctx.flags.L = 1;
        break;
    case 'F':
        ctx.raw_pcrs_file = optarg;
        break;
        /* no default */
    }
    return true;
}

bool tpm2_tool_onstart(tpm2_options **opts) {

    const struct option topts[] = {
        { "index"       , required_argument, NULL, 'x' },
        { "auth-handle"  , required_argument, NULL, 'a' },
        { "output"      , required_argument, NULL, 'f' },
        { "size"        , required_argument, NULL, 's' },
        { "offset"      , required_argument, NULL, 'o' },
        { "handle-passwd", required_argument, NULL, 'P' },
        { "input-session-handle", required_argument, NULL, 'S' },
        {"set-list",       required_argument, NULL, 'L' },
        {"pcr-input-file", required_argument, NULL, 'F' },
    };

    *opts = tpm2_options_new("x:a:f:s:o:P:S:L:F:", ARRAY_LEN(topts),
            topts, on_option, NULL, TPM2_OPTIONS_SHOW_USAGE);

    return *opts != NULL;
}

int tpm2_tool_onrun(TSS2_SYS_CONTEXT *sapi_context, tpm2_option_flags flags) {

    if (ctx.flags.L) {
        TPM2B_DIGEST pcr_digest = TPM2B_TYPE_INIT(TPM2B_DIGEST, buffer);

        TSS2_RC rval = tpm2_policy_build(sapi_context, &ctx.policy_session,
                                        TPM2_SE_POLICY, TPM2_ALG_SHA256, &ctx.pcr_selection,
                                        ctx.raw_pcrs_file, &pcr_digest, true,
                                        tpm2_policy_pcr_build);
        if (rval != TSS2_RC_SUCCESS) {
            LOG_ERR("Building PCR policy failed: 0x%x", rval);
            return 1;
        }
        ctx.session_data.sessionHandle = ctx.policy_session->sessionHandle;
        ctx.session_data.sessionAttributes |= TPMA_SESSION_CONTINUESESSION;
    }


    bool res = nv_read(sapi_context, flags);

    if (ctx.policy_session) {
        TSS2_RC rval = Tss2_Sys_FlushContext(sapi_context,
                                            ctx.policy_session->sessionHandle);
        if (rval != TSS2_RC_SUCCESS) {
            LOG_ERR("Failed Flush Context: 0x%x", rval);
            return 1;
        }

        tpm_session_auth_end(ctx.policy_session);
    }

    return res != true;
}
