#ifndef LFP_TIF_H
#define LFP_TIF_H

#include <lfp/lfp.h>

/** \file tapeimage.h */

#if (__cplusplus)
extern "C" {
#endif

/** Tape Image Format (TIF)
 *
 * The tape image is an encapsulation format developed for well logs. The file
 * is segmented into records, each preceeded by a record marker of three 4-byte
 * little-endian integers - a record type, offset of the previous record, and
 * offset of the next record. All offsets are absolute, so the file size is
 * limited to 4GB.
 *
 * The tapeimage protocol provides a view of as if the record markers were not
 * present. `lfp_seek()` and `lfp_tell()` consider offsets as if the file had
 * no tape markers.
 */
lfp_protocol* lfp_tapeimage_open(lfp_protocol*);

#if (__cplusplus)
} // extern "C"
#endif

#endif // LFP_TIF_H
