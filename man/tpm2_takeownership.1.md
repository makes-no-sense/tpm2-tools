% tpm2_takeownership(1) tpm2-tools | General Commands Manual
%
% SEPTEMBER 2017

# NAME

**tpm2_takeownership**(1) - Insert authorization values for the owner, platform,
endorsement and lockout authorizations.

# SYNOPSIS

**tpm2_takeownership** [*OPTIONS*]

# DESCRIPTION

**tpm2_takeownership**(1) - performs a hash operation on _FILE_ and returns the results. If
_FILE_ is not specified, then data is read from stdin. If the results of the
hash will be used in a signing operation that uses a restricted signing key,
then the ticket returned by this command can indicate that the hash is safe to
sign.

# OPTIONS

  * **-o**, **--owner-passwd**=_OWNER\_PASSWORD_:

    The new owner authorization value. Passwords should follow the password
    formatting standards, see section "Password Formatting".

  * **-p**, **--platform-passwd**=_PLATFORM\_PASSWORD_:

    The new platform authorization value. Passwords should follow the same
    formatting requirements as the -o option.

  * **-e**, **--endorse-passwd**=_ENDORSE\_PASSWORD_:

    The new endorse authorization value. Passwords should follow the same
    formatting requirements as the -o option.

  * **-l**, **--lock-passwd**=_LOCKOUT\_PASSWORD_:

    The new lockout authorization value. Passwords should follow the same
    formatting requirements as the -o option.

  * **-O**, **--oldOwnerPasswd**=_OLD\_OWNER\_PASSWORD_:

    The old owner authorization value. Passwords should follow the same
    formatting requirements as the -o option.

  * **-P**, **--oldPlatfromPasswd**=_OLD\_PLATFORM\_PASSWORD_:

    The old platform authorization value. Passwords should follow the same
    formatting requirements as the -o option.

  * **-E**, **--oldEndorsePasswd**=_OLD\_ENDORSE\_PASSWORD_:

    The old endorse authorization value. Passwords should follow the same
    formatting requirements as the -o option.

  * **-L**, **--oldLockPasswd**=_OLD\_LOCKOUT\_PASSWORD_:

    The old lockout authorization value. Passwords should follow the same
    formatting requirements as the -o option.

  * **-c**, **--clear**:

    Clears the 3 authorizations values with lockout auth, thus one must specify
    -L.

[common options](common/options.md)

[common tcti options](common/tcti.md)

[password formatting](common/password.md)

# EXAMPLES

Set owner, platform, endorsement and lockout authorizations to an empty auth value:

```
tpm2_takeownership -c -L oldlockoutpasswd
```

Set owner, platform, endorsement and lockout authorizations:

```
tpm2_takeownership -o oldo -p oldp -e olde -l oldl
```

Set owner, platform, endorsement and lockout authorizations to a new value:

```
tpm2_takeownership -o newo -p newp -e newe -l newl -O oldo -P oldp -E olde -L oldl`
```

# RETURNS

0 on success or 1 on failure.

[footer](common/footer.md)
