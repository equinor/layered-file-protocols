Writing protocols
=================
Protocols are implemented in C++, by deriving the `lfp_protocol` class. This is
*not* a part of the public interface for *consumers* of lfp, who should use the
C interface in lfp.h. 

Errors are signalled with exception, which allows passing a status code and an
error message from deep layers without problems. If a protocol has more
information to add, exceptions can be caught, augmented, and re-thrown. The C
API boundary will intercept any exception, and set the error message in the
outer-most handle.

This makes protocols a lot easier to write - native C++ idioms can be used, and
a lot of boilerplate goes away, such as:

   int err = lfp_fun();
   if (err) return err;

This pattern can be error prone, in particular when layered protocols, which
are unknown in advance, raise unexpected errors.

Errors should be fairly rare, so any performance implications of stack
unwinding should not be a problem. Besides, indirection is built into the
design, and this is an I/O library, and tiny hits in the error path are
acceptable.

Naturally, protocols should be written with exception safety in mind.
