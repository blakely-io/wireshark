/* packet-wap.c
 *
 * Utility routines for WAP dissectors
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * WAP dissector based on original work by Ben Fowler
 * Updated by Neil Hunter <neil.hunter@energis-squared.com>
 * WTLS support by Alexandre P. Ferreira (Splice IP)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config.h"

#include <epan/packet.h>
#include "packet-wap.h"

/*
 * Accessor to retrieve variable length int as used in WAP protocol.
 * The value is encoded in the lower 7 bits. If the top bit is set, then the
 * value continues into the next byte.
 * The octetCount parameter holds the number of bytes read in order to return
 * the final value. Can be pre-initialised to start at offset+count.
*/
guint
tvb_get_guintvar (tvbuff_t *tvb, guint offset, guint *octetCount, packet_info *pinfo, expert_field *ei)
{
    guint value   = 0;
    guint octet;
    guint counter = 0;
    char  cont    = 1;

#ifdef DEBUG
    fprintf (stderr,
            "dissect_wap: Starting tvb_get_guintvar at offset %d\n", offset);
#endif

    while (cont != 0)
    {
        value <<= 7;  /* Value only exists in 7 of the 8 bits */
        octet = tvb_get_guint8 (tvb, offset+counter);
        counter += 1;
        value   += (octet & 0x7F);
        cont = (octet & 0x80);
#ifdef DEBUG
        fprintf (stderr, "dissect_wap: computing: octet is %d (0x%02x), count=%d, value=%d, cont=%d\n",
                 octet, octet, counter, value, cont);
#endif
    }

    if (counter > sizeof(value)) {
        proto_tree_add_expert(NULL, pinfo, ei, tvb, offset, counter);
        value = 0;
    }
    if (octetCount != NULL)
    {
        *octetCount = counter;
#ifdef DEBUG
        fprintf (stderr, "dissect_wap: Leaving tvb_get_guintvar count=%d, value=%u\n", *octetCount, value);
#endif
    }

    return (value);
}

/*
 * Editor modelines  -  http://www.wireshark.org/tools/modelines.html
 *
 * Local variables:
 * c-basic-offset: 4
 * tab-width: 8
 * indent-tabs-mode: nil
 * End:
 *
 * vi: set shiftwidth=4 tabstop=8 expandtab:
 * :indentSize=4:tabSize=8:noTabs=true:
 */
