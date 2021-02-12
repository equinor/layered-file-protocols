#ifndef LFP_INCLUDE_H
#define LFP_INCLUDE_H

#include <stddef.h>
#include <stdint.h>

/*
 * symbol visibility (export and import)
 *
 * The LFP_EXPORT symbol is set by cmake when building shared libraries.
 *
 * If linking to a shared build on Windows the LFP_SHARED symbol must be
 * defined too.
 */

/* make sure the symbol always exists */
#if defined(LFP_API)
    #error LFP_API is already defined
#endif

/*
 * The LFP_API stuff is not interesting for doxygen, so just skip it
 * altogether.
 */
#if !defined(DOXYGEN_SHOULD_SKIP_THIS)

#define LFP_API

#if defined(LFP_EXPORT)
    #if defined(_MSC_VER)
        #undef LFP_API
        #define LFP_API __declspec(dllexport)
    #endif

    /*
     * nothing in particular is needed for symbols to be visible on non-MSVC
     * compilers.
     */
#endif

#if !defined(LFP_EXPORT)
    #if defined(_MSC_VER) && defined(LFP_SHARED)
        /*
         * TODO: maybe this could be addressed by checking #ifdef _DLL, rather
         * than relying on LFP_SHARED being set.
         */
        #undef LFP_API
        #define LFP_API __declspec(dllimport)
    #endif

    /* assume gcc style exports if gcc or clang */
    #if defined(__GNUC__) || defined(__clang__)
        #undef LFP_API
        #define LFP_API __attribute__ ((visibility ("default")))
    #endif
#endif

#endif // DOXYGEN_SHOULD_SKIP_THIS

#if (__cplusplus)
extern "C" {
#endif

/** \file lfp.h */

struct lfp_protocol;
typedef struct lfp_protocol lfp_protocol;

/** Status codes for return values
 *
 * Unless very explicitly documented otherwise, public functions in lfp return
 * an integer corresponding to one of these error codes.
 */
enum lfp_status {
    LFP_OK = 0,

    /**
     * Returned in successful-but-incomplete scenarios, for example by
     * `lfp_readinto()` when the underlying IO is blocked from providing more
     * bytes. For end-of-file, see LFP_EOF.
     */
    LFP_OKINCOMPLETE,

    /**
     * Returned when functionality is not implemented by a specific handle.
     */
    LFP_NOTIMPLEMENTED,

    /**
     * The functionality is implemented and supported in general, but not
     * supported for leaf protocols.
     */
    LFP_LEAF_PROTOCOL,

    /**
     * The functionality is implemented and supported in general, but not
     * supported for a specific configuration for this handle. An example is
     * `lfp_seek()` or `lfp_tell()` in an unseekable `lfp_cfile` stream (pipe).
     */
    LFP_NOTSUPPORTED,

    /**
     * An implementation threw a C++ exception that was not properly caught,
     * and would be next to bubble up to call site, which expects C and is not
     * prepared for it (which would in the best case call std::terminate).
     */
    LFP_UNHANDLED_EXCEPTION,

    /**
     * A problem with a phyiscal device - depending on the device, this might
     * be recoverable, but it means a read or write operation could not be
     * performed correctly.
     */
    LFP_IOERROR,

    /**
     * Some error in the runtime, for example being unable to allocate or
     * resize a buffer.
     *
     * This does not mean *an error at runtime*, it means *an error from the
     * runtime*.
     */
    LFP_RUNTIME_ERROR,

    /**
     * Returned whenever an invalid argument is passed to the function, such as
     * trying to seek beyond end-of-file.
     */
    LFP_INVALID_ARGS,

    /**
     * A fatal, unrecoverable protocol error. This is returned when actual
     * reads or writes were successful, but the bytes are inconsistent with
     * what the protocol expects.
     */
    LFP_PROTOCOL_FATAL_ERROR,

    /**
     * There was a protocol violation, but simple recovery seems to work. An
     * example is formats that point backwards, and the backwards pointer is
     * wrong.
     *
     * In some cases you might want to consider this fatal as well.
     */
    LFP_PROTOCOL_TRYRECOVERY,

    /**
     * When more protocol recovery was happening, but more errors occured.
     */
    LFP_PROTOCOL_FAILEDRECOVERY,

    /**
     * Returned in successful-but-incomplete scenarios, for example by
     * `lfp_readinto()` when the end of file is reached before reading all
     * requested bytes.
     */
    LFP_EOF,

    /**
     * Returned when the underlying handle reports end-of-file while an outer
     * protocol expected there to be more data.
     */
    LFP_UNEXPECTED_EOF,
};

/** \defgroup public-functions Functions */
/** \addtogroup public-functions
 * @{
 */

/** Close the file and release resources
 *
 * Close a file and release resources recursively - if the pointer argument is
 * `NULL`, nothing happens.
 *
 * This is similar to `fclose()`, and will *release* the handle. It is
 * undefined behaviour to call close more than once on the same file.
 *
 * Using an `lfp*` after it has been `lfp_close()`d is undefined behaviour.
 */
LFP_API
int lfp_close(lfp_protocol*);

/** Read len bytes into dst
 *
 * Read up to len bytes into dst. The exact number of bytes read are written to
 * nread, which will always be `*nread <= len`. nread is usually only smaller
 * than len at EOF.
 *
 * nread can be `NULL`. If it is, it is not possible to determine how many
 * bytes were written to dst when less-than-len bytes were asked for.
 *
 * This function returns `LFP_OKINCOMPLETE` when not enough bytes were
 * available, but the read was otherwise successful. This is common when
 * reading from pipes.
 *
 * \retval LFP_OK Success
 * \retval LFP_OKINCOMPLETE Successful, but incomplete read
 * \retval LFP_EOF Successful, but end of file was reach during the read
 */
LFP_API
int lfp_readinto(lfp_protocol*, void* dst, int64_t len, int64_t* nread);

/** Set the file position to (absolute) byte offset n
 *
 * Protocols are not required to implement seek, e.g. file streams (pipes) are
 * usually not seekable.
 *
 * \param n byte offset to seek to, must not be negative
 *
 * \retval LFP_OK Success
 * \retval LFP_INVALID_ARGS N is negative
 * \retval LFP_NOTIMPLEMENTED Layer does not support seek
 */
LFP_API
int lfp_seek(lfp_protocol*, int64_t n);

/** Get current position
 *
 * Obtain the current logical value of the file position. The value is
 * relative to the used protocol, 0-based, in bytes.
 */
LFP_API
int lfp_tell(lfp_protocol*, int64_t* n);

/** Get current physical position
 *
 * Obtain the current physical value of the file position. The value is
 * absolute with regards to the underlying handle of the leaf protocol, 0-based,
 * in bytes.
 * As a consequence, same value will be returned for all the protocols stacked
 * together.
 */
LFP_API
int lfp_ptell(lfp_protocol*, int64_t* n);

/** Peels off the current protocol to expose the underlying one
 *
 * Conceptually this is similar to calling release() on a std::unique_ptr.
 * `lfp_peel()` is not implemented for leaf protocols such as the cfile
 * protocol.
 *
 * \param outer Outer protocol that will be peeled off
 * \param inner Reference to the underlying protocol
 *
 * \retval LFP_OK Success
 * \retval LFP_LEAF_PROTOCOL Leaf protocols does not support peel
 * \retval LFP_IOERROR There is no underlying protocol to peel into. Typically
 *                     this would be the case if peel is called multiple times
 *                     on the same protocol.
 */
LFP_API
int lfp_peel(lfp_protocol* outer, lfp_protocol** inner);

/** Expose a const view of the underlying protocol
 *
 * Conceptually, this function is similar to calling get() on a
 * std::unique_ptr. Performing a non-const operation such as `lfp_seek()` and
 * `lfp_readinto()` on the exposed protocol *will* leave the outer protocol in
 * an undefined state.

 * Like `lfp_peel()`, peek is not implemented for leaf protocols such as the
 * cfile protocol.
 *
 * \param outer Outer protocol
 * \param inner Reference to the underlying protocol
 *
 * \retval LFP_OK Success
 * \retval LFP_LEAF_PROTOCOL Leaf protocols does not support peek
 * \retval LFP_IOERROR There is no underlying protocol to peek into. Typically
 *                     this would be the case if the protocol is already been
 *                     peel'ed.
 */
LFP_API
int lfp_peek(lfp_protocol* outer, lfp_protocol** inner);

/** Checks if the end of file is reached
 *
 * This does not return a `lfp_status` code.
 *
 * \retval non-zero End-of-file reached
 * \retval 0 End-of-file not reached
 */
LFP_API
int lfp_eof(lfp_protocol*);

/** Get last set error message
 *
 * Obtain a human-readable error message, or `NULL` if no error is set. This
 * does not return a status code.
 *
 * This function should be called immediately after the error occured, to
 * accurately describe the nature of the error. The string is human readable,
 * not guaranteed to be stable, and is not suited for parsing.
 */
LFP_API
const char* lfp_errormsg(lfp_protocol*);

/** @} */

#include <stdio.h>

/** C FILE protocol
 *
 * This protocol provides an lfp interface to `FILE`.
 *
 * The protocol will immediately `ftell()` and consider this as the start
 * of the file by lfp. This allows reading past particular garbage, noise, or
 * sensitive details before giving control to lfp.
 *
 * The cfile protocol supports everything the specific `FILE` instance support,
 * which means features may degrade when `FILE` is a stream (pipe) or similar.
 * Typically, this means seek and tell will fail.
 *
 * This function takes *ownership* of the handle, and the `FILE` will be
 * `fclose()`d when `lfp_close()` is called on it.
 */
LFP_API
lfp_protocol* lfp_cfile(FILE*);

/** C FILE protocol
 *
 * This protocol provides an lfp interface to `FILE`.
 *
 * The protocol will `fseek()` to provided zero and consider this as the start
 * of the file by lfp. This allows reading past particular garbage, noise, or
 * sensitive details before giving control to lfp.
 *
 * The cfile protocol supports everything the specific `FILE` instance support,
 * which means features may degrade when `FILE` is a stream (pipe) or similar.
 * Typically, this means seek and tell will fail.
 *
 * This function takes *ownership* of the handle, and the `FILE` will be
 * `fclose()`d when `lfp_close()` is called on it.
 *
 * \param zero Absolute offset to be considered as zero
 */
LFP_API
lfp_protocol* lfp_cfile_open_at_offset(FILE*, int64_t zero);

#if (__cplusplus)
} // extern "C"
#endif

#endif //LFP_INCLUDE_H
