//**********************************************************************;
// Copyright (c) 2015-2018, Intel Corporation
// Copyright (c) 2016, Atom Software Studios
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

#include "tpm2_options.h"
#include "tpm2_password_util.h"
#include "log.h"
#include "tpm2_util.h"

typedef struct tpm_nvreadlock_ctx tpm_nvreadlock_ctx;
struct tpm_nvreadlock_ctx {
    UINT32 nv_index;
    UINT32 auth_handle;
    UINT32 size_to_read;
    UINT32 offset;
    TPMS_AUTH_COMMAND session_data;
};

static tpm_nvreadlock_ctx ctx = {
    .auth_handle = TPM2_RH_PLATFORM,
    .session_data = TPMS_AUTH_COMMAND_INIT(TPM2_RS_PW),

};

static bool nv_readlock(TSS2_SYS_CONTEXT *sapi_context) {

    TSS2L_SYS_AUTH_RESPONSE sessions_data_out;
    TSS2L_SYS_AUTH_COMMAND sessions_data = { 1, { ctx.session_data }};

    TSS2_RC rval = TSS2_RETRY_EXP(Tss2_Sys_NV_ReadLock(sapi_context, ctx.auth_handle, ctx.nv_index,
            &sessions_data, &sessions_data_out));
    if (rval != TPM2_RC_SUCCESS) {
        LOG_ERR("Failed to lock NVRAM area at index 0x%x (%d).Error:0x%x",
                ctx.nv_index, ctx.nv_index, rval);
        return false;
    }

    return true;
}

static bool on_option(char key, char *value) {

    bool result;

    switch (key) {
    case 'x':
        result = tpm2_util_string_to_uint32(value, &ctx.nv_index);
        if (!result) {
            LOG_ERR("Could not convert NV index to number, got: \"%s\"",
                    value);
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
                    value);
            return false;
        }

        if (ctx.auth_handle == 0) {
            LOG_ERR("Auth handle cannot be 0");
            return false;
        }
        break;
    case 'P':
        result = tpm2_password_util_from_optarg(value, &ctx.session_data.hmac);
        if (!result) {
            LOG_ERR("Invalid handle password, got\"%s\"", value);
                return false;
        }
        break;
    case 'S':
        if (!tpm2_util_string_to_uint32(value, &ctx.session_data.sessionHandle)) {
            LOG_ERR("Could not convert session handle to number, got: \"%s\"",
                    value);
            return false;
        }
        break;
    }

    return true;
}

bool tpm2_tool_onstart(tpm2_options **opts) {

    const struct option topts[] = {
        { "index"       , required_argument, NULL, 'x' },
        { "auth-handle"  , required_argument, NULL, 'a' },
        { "handle-passwd", required_argument, NULL, 'P' },
        { "input-session-handle", required_argument, NULL, 'S' },
    };

    *opts = tpm2_options_new("x:a:P:p:d:S:hv", ARRAY_LEN(topts), topts,
            on_option, NULL, TPM2_OPTIONS_SHOW_USAGE);

    return *opts != NULL;
}

int tpm2_tool_onrun(TSS2_SYS_CONTEXT *sapi_context, tpm2_option_flags flags) {

    UNUSED(flags);

    return nv_readlock(sapi_context) != true;
}
