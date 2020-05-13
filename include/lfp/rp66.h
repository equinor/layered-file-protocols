#ifndef LFP_RP66_H
#define LFP_RP66_H

#include <lfp/lfp.h>

#if (__cplusplus)
extern "C" {
#endif

/** Visible Envelope
 *
 * The Visible Envelope (VE) is an access mechanic from the DLIS spec, rp66v1 [1].
 *
 * A dlis file consists of a series of Visible Records (VR), each consisting of
 * a Visible Record Length (VRL), a Format Version (FV) and one or more Logical
 * Record Segments (LRS).
 *
 * The rp66 protocol provides a view as if the VE was not present.
 * `lfp_seek()` and `lfp_tell()` consider offsets as if the file had no VE:
 *
 * \verbatim
          ---------------------------------------------
         | SUL | VRL + FV |  LRSi  | VRL + FV | LRSi+1 |
          ---------------------------------------------
  tell   0    80         84       184        188      288

          -----------------
         |  LRSi  | LRSi+1 |
          -----------------
  tell   0       100      200
  \endverbatim
 * The first 80 bytes of the VE consist of ASCII characters and constitute a
 * Storage Unit Label (SUL). The information in the SUL is of no interest to
 * lfp, but it might be of interest to the caller.  This protocol assumes that
 * the SUL has already been read when the protocol is opened, i.e.  that the
 * first byte of the underlying handle is the Visible Record Length of the
 * first VR.
 *
 * The protocol can be open at any Visible Record, and tells will start at that
 * position. However, it's not possible to open the protocol in the middle
 * of a record.
 *
 * [1] http://w3.energistics.org/RP66/V1/Toc/main.html
 */
lfp_protocol* lfp_rp66_open(lfp_protocol*);

#if (__cplusplus)
} // extern "C"
#endif

#endif // LFP_RP66_H
