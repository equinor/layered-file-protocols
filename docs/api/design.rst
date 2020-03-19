API design
==========
Layered file protocols exposes a fairly standard C interface, with opaque
pointers and a small set of functions to operate on them. It is designed with a
strong emphasis on keeping the number of (opaque) structs low, and to use
primitive types whenever possible. This gives the most flexibility at call-site
to decide on storage.

Namespacing
-----------
In good C fashion, all symbols are prefixed with `lfp_` or `LFP_`, to avoid
collision with other symbols.

Layers visualization
--------------------
Assume file is layered like that:
::

    Outer layer
         -------        -------
        | data1 |      | data2 |
         -------        -------
        0  1  2  _____ 3  4  5  6

    Inner layer
      ----------------------------
     |  | data1 |      | data2 |  |
      ----------------------------
     0  1  2  3  4  5  6  7  8  9  10

*Inner layer* has 10 bytes. Actual data is present in bytes *[1, 4)* and 
*[6, 9)*. *Outer layer* perceives itself to have just 6 bytes *[0, 6)*.

Operations on newly created **outer layer** handle would mean:

- **read** 6 bytes: *data1* and *data2* will be read into buffer
    (**tell** after operation would reply: I am at position 6)
- **seek** to 3: file position will be at the beginning of *data2*
    (**tell** after operation would reply: I am at position 3)

Function signatures
-------------------
All functions are structured similarly - the file handle is the first
parameter, returned values are (output) pointers, input parameters are copies
or const pointers. Like the C stdlib, buffers are `void*`, and size is
determined through the other parameters.

All functions, unless for good reasons and explicitly specified, return
simple ints, which maps to one of the `lfp_status` enum values. Integers are
signed to behave predictably like numbers - int64 should be plenty for all
sizes and offsets for a very long time.

Unless documentation clearly specifies otherwise, functions that return new
`lfp_protocol` instances expect that lfp_close is called on it (by you).
Constructor functions that take `lfp_protocol*` values as a parameter takes
ownership - when the parent is closed, all underlying layers will be closed
too.

.. code:: cpp

    lfp_protocol* inner = lfp_cfile(fp);
    if (!inner) handle_failure_inner();
    lfp_protocol* outer = lfp_buffered(fp);
    if (!outer) {
        // release inner, because outer never got to take ownership
        lfp_close(inner);
        handle_failure_outer();
        return;
    }
    /* rest of the program */
    lfp_close(outer); // only release outer!

Status codes and backwards compatibility
----------------------------------------
The only status code guaranteed to not change (its numerical value) is
LFP_OK, which is pinned to 0. This enables this pattern:

.. code:: cpp

    int err = lfp_fun();
    if (err) {
        handle_error();
    }

Error codes other than OK are not guaranteed to be stable - they may be
reorganised, renumbered, or refined between versions, so a paranoid approach
is useful:

.. code:: cpp

    int err = lfp_fun();
    switch (err) {
        case LFP_OK:
            ok();
            break;
        case LFP_OKINCOMPLETE:
            ok_incomplete();
            break;
        case LFP_SOME_ERROR:
            handle_error();
            break;
        default:
            handle_unknown_error();
    }

Always have a fallthrough case!

Error codes names will never be removed, but their use may change, and more
specific error codes may be added in the future. Unless otherwise specified,
the listed return values for functions are *not* complete, and functions may
return any number of other status codes.
