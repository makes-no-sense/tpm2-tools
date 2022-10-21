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
#include <stdio.h>

#include "tpm2_options.h"
#include "log.h"
#include "files.h"
#include "tpm2_util.h"

typedef struct tpm_random_ctx tpm_random_ctx;
struct tpm_random_ctx {
    bool output_file_specified;
    char *output_file;
    UINT16 num_of_bytes;
};

static tpm_random_ctx ctx;

static bool get_random_and_save(TSS2_SYS_CONTEXT *sapi_context) {

    TPM2B_DIGEST random_bytes = TPM2B_TYPE_INIT(TPM2B_DIGEST, buffer);

    TSS2_RC rval = TSS2_RETRY_EXP(Tss2_Sys_GetRandom(sapi_context, NULL, ctx.num_of_bytes,
            &random_bytes, NULL));
    if (rval != TPM2_RC_SUCCESS) {
        LOG_ERR("TPM2_GetRandom Error. TPM Error:0x%x", rval);
        return false;
    }

    if (!ctx.output_file_specified) {
        UINT16 i;
        for (i = 0; i < random_bytes.size; i++) {
            printf("%s0x%2.2X", i ? " " : "", random_bytes.buffer[i]);
        }
        printf("\n");
        return true;
    }

    return files_save_bytes_to_file(ctx.output_file, random_bytes.buffer,
            random_bytes.size);
}

static bool on_option(char key, char *value) {

    UNUSED(key);

    ctx.output_file_specified = true;
    ctx.output_file = value;

    return true;
}

static bool on_args(int argc, char **argv) {

    if (argc > 1) {
        LOG_ERR("Only supports one SIZE octets, got: %d", argc);
        return false;
    }

    bool result = tpm2_util_string_to_uint16(argv[0], &ctx.num_of_bytes);
    if (!result) {
        LOG_ERR("Error converting size to a number, got: \"%s\".",
                argv[0]);
        return false;
    }

    return true;
}

bool tpm2_tool_onstart(tpm2_options **opts) {

    const struct option topts[] = {
        { "output",   required_argument, NULL, 'o' },
    };

    *opts = tpm2_options_new("o:", ARRAY_LEN(topts), topts,
            on_option, on_args, TPM2_OPTIONS_SHOW_USAGE);

    return *opts != NULL;
}

int tpm2_tool_onrun(TSS2_SYS_CONTEXT *sapi_context, tpm2_option_flags flags) {

    UNUSED(flags);

    return get_random_and_save(sapi_context) != true;
}
