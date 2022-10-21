% tpm2_rsaencrypt(1) tpm2-tools | General Commands Manual
%
% SEPTEMBER 2017

# NAME

**tpm2_rsaencrypt**(1) - Performs an RSA Encryption operation using the TPM.

# SYNOPSIS

**tpm2_rsaencrypt** [*OPTIONS*] _FILE_

# DESCRIPTION

**tpm2_rsaencrypt**(1) performs RSA encryption on the contents of _FILE_
(defaulting to stdin) using the indicated padding scheme according to
IETF RFC 3447 (PKCS#1). The scheme of keyHandle should not be **TPM_ALG_NULL**.

The key referenced by keyHandle is **required** to be:

1. an RSA key
2. Have the attribute *decrypt* **SET** in it's attributes.

# OPTIONS

  * **-k**, **--key-handle**=_KEY\_HANDLE_:

    the public portion of RSA key to use for encryption.

  * **-c**, **--key-context**=_KEY\_CONTEXT\_FILE_:

    filename of the key context used for the operation.

  * **-o**, **--out-file**=_OUTPUT\_FILE_:

    Output file path, record the decrypted data. The default is to print an
    xxd compatible hexdump to stdout. If a file is specified, raw binary
    output is performed.

[common options](common/options.md)

[common tcti options](common/tcti.md)

[password formatting](common/password.md)

# EXAMPLES

```
tpm2_rsaencrypt -k 0x81010001 -o encrypted.out plain.in
```

# RETURNS

0 on success or 1 on failure.

[footer](common/footer.md)
