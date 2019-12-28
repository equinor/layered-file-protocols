#ifndef LFP_MEMFILE_H
#define LFP_MEMFILE_H

#include <stddef.h>

#include <lfp/lfp.h>

#if (__cplusplus)
extern "C" {
#endif

/*
 * The memfile is akin to the python3 io.StringIO [1], an in-memory file,
 * growable file, but for byte streams, not string streams.
 *
 * The main use case for this type of file is really testing cases - it behaves
 * like an on-disk file, but is simple and cheap to construct for testing
 * purposes, as there's no I/O, no tempfiles, no directories, and no cleanup.
 *
 * It has other uses, most notably as a cache, but if that is a use case, a
 * less file-like approach should generally be preferred.
 *
 * [1] https://docs.python.org/3/library/io.html#io.StringIO
 */
lfp_protocol* lfp_memfile_open();
lfp_protocol* lfp_memfile_openwith(const unsigned char*, size_t);

#if (__cplusplus)
} // extern "C"
#endif

#endif // LFP_MEMFILE_H
