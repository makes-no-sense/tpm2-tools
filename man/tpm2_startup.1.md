% tpm2_startup(1) tpm2-tools | General Commands Manual
%
% SEPTEMBER 2017

# NAME

**tpm2_startup**(1) - Send a startup command to the TPM.

# SYNOPSIS

**tpm2_startup** [*OPTIONS*]

# DESCRIPTION

**tpm2_startup**(1) Send a **TPM2_Startup** command with either **TPM_SU_CLEAR** or
**TPM_SU_STATE**.

**NOTE**: Typically a Resource Manager or low-level/boot software will
have already sent this command.

# OPTIONS

  * **-c**, **--clear**:

    Startup type sent will be **TPM_SU_CLEAR** instead of **TPM2_SU_STATE**.

[common options](common/options.md)

[common tcti options](common/tcti.md)

# EXAMPLES

```
tpm2_startup
tpm2_startup -c
```

# RETURNS

0 on success or 1 on failure.

[footer](common/footer.md)
