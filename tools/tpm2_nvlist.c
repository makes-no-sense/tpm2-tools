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

#include <stdlib.h>

#include "tpm2_alg_util.h"
#include "tpm2_attr_util.h"
#include "tpm2_nv_util.h"
#include "tpm2_tool.h"

static void print_nv_public(TPM2B_NV_PUBLIC *nv_public) {

    char *attrs = tpm2_attr_util_nv_attrtostr(nv_public->nvPublic.attributes);
    if (!attrs) {
        LOG_ERR("Could not convert attributes to string form");
    }

    const char *alg = tpm2_alg_util_algtostr(nv_public->nvPublic.nameAlg);
    if (!alg) {
        LOG_ERR("Could not convert algorithm to string form");
    }

    tpm2_tool_output("  hash algorithm:\n");
    tpm2_tool_output("    friendly: %s\n", alg);
    tpm2_tool_output("    value: 0x%X\n",
            nv_public->nvPublic.nameAlg);

    tpm2_tool_output("  attributes:\n");
    tpm2_tool_output("    friendly: %s\n", attrs);
    tpm2_tool_output("    value: 0x%X\n",
            tpm2_util_ntoh_32(nv_public->nvPublic.attributes));

    tpm2_tool_output("  size: %d\n",
               nv_public->nvPublic.dataSize);

    if (nv_public->nvPublic.authPolicy.size) {
        tpm2_tool_output("  authorization policy: ");

        UINT16 i;
        for (i=0; i<nv_public->nvPublic.authPolicy.size; i++) {
            tpm2_tool_output("%02X", nv_public->nvPublic.authPolicy.buffer[i] );
        }
        tpm2_tool_output("\n");
    }

    free(attrs);
}

static bool nv_list(TSS2_SYS_CONTEXT *sapi_context) {

    TPMI_YES_NO moreData;
    TPMS_CAPABILITY_DATA capabilityData;
    UINT32 property = tpm2_util_hton_32(TPM2_HT_NV_INDEX);
    TSS2_RC rval = TSS2_RETRY_EXP(Tss2_Sys_GetCapability(sapi_context, 0, TPM2_CAP_HANDLES,
            property, TPM2_PT_NV_INDEX_MAX, &moreData, &capabilityData, 0));
    if (rval != TPM2_RC_SUCCESS) {
        LOG_ERR("GetCapability:Get NV Index list Error. TPM Error:0x%x", rval);
        return false;
    }

    UINT32 i;
    for (i = 0; i < capabilityData.data.handles.count; i++) {
        TPMI_RH_NV_INDEX index = capabilityData.data.handles.handle[i];

        tpm2_tool_output("0x%x:\n", index);

        TPM2B_NV_PUBLIC nv_public = TPM2B_EMPTY_INIT;
        rval = tpm2_util_nv_read_public(sapi_context, index, &nv_public);
        if (rval != TPM2_RC_SUCCESS) {
            LOG_ERR("Reading the public part of the nv index failed with: 0x%x", rval);
            return false;
        }
        print_nv_public(&nv_public);
        tpm2_tool_output("\n");
    }

    return true;
}

int tpm2_tool_onrun(TSS2_SYS_CONTEXT *sapi_context, tpm2_option_flags flags) {

    UNUSED(flags);

    return !nv_list(sapi_context);
}
