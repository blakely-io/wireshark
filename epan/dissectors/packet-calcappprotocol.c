/* packet-calcappprotocol.c
 * Routines for the Calculation Application Protocol, a test application of the
 * RSPLIB RSerPool implementation
 * https://www.uni-due.de/~be0001/rserpool/
 *
 * Copyright 2006-2021 by Thomas Dreibholz <dreibh [AT] iem.uni-due.de>
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * Copied from README.developer
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config.h"

#include <epan/packet.h>
#include <epan/sctpppids.h>


#define CALCAPPPROTOCOL_PAYLOAD_PROTOCOL_ID_LEGACY 0x29097603

void proto_register_calcappprotocol(void);
void proto_reg_handoff_calcappprotocol(void);

/* Initialize the protocol and registered fields */
static int proto_calcappprotocol = -1;
static int hf_message_type       = -1;
static int hf_message_flags      = -1;
static int hf_message_length     = -1;
static int hf_message_jobid      = -1;
static int hf_message_jobsize    = -1;
static int hf_message_completed  = -1;

/* Initialize the subtree pointers */
static gint ett_calcappprotocol = -1;

/* Dissectors for messages. This is specific to CalcAppProtocol */
#define MESSAGE_TYPE_LENGTH      1
#define MESSAGE_FLAGS_LENGTH     1
#define MESSAGE_LENGTH_LENGTH    2
#define MESSAGE_JOBID_LENGTH     4
#define MESSAGE_JOBSIZE_LENGTH   8
#define MESSAGE_COMPLETED_LENGTH 8

#define MESSAGE_TYPE_OFFSET      0
#define MESSAGE_FLAGS_OFFSET     (MESSAGE_TYPE_OFFSET    + MESSAGE_TYPE_LENGTH)
#define MESSAGE_LENGTH_OFFSET    (MESSAGE_FLAGS_OFFSET   + MESSAGE_FLAGS_LENGTH)
#define MESSAGE_JOBID_OFFSET     (MESSAGE_LENGTH_OFFSET  + MESSAGE_LENGTH_LENGTH)
#define MESSAGE_JOBSIZE_OFFSET   (MESSAGE_JOBID_OFFSET   + MESSAGE_JOBID_OFFSET)
#define MESSAGE_COMPLETED_OFFSET (MESSAGE_JOBSIZE_OFFSET + MESSAGE_JOBSIZE_OFFSET)


#define CALCAPP_REQUEST_MESSAGE_TYPE       1
#define CALCAPP_ACCEPT_MESSAGE_TYPE        2
#define CALCAPP_REJECT_MESSAGE_TYPE        3
#define CALCAPP_ABORT_MESSAGE_TYPE         4
#define CALCAPP_COMPLETE_MESSAGE_TYPE      5
#define CALCAPP_KEEPALIVE_MESSAGE_TYPE     6
#define CALCAPP_KEEPALIVE_ACK_MESSAGE_TYPE 7


static const value_string message_type_values[] = {
  { CALCAPP_REQUEST_MESSAGE_TYPE,        "CalcApp Request" },
  { CALCAPP_ACCEPT_MESSAGE_TYPE,         "CalcApp Accept" },
  { CALCAPP_REJECT_MESSAGE_TYPE,         "CalcApp Reject" },
  { CALCAPP_ABORT_MESSAGE_TYPE,          "CalcApp Abort" },
  { CALCAPP_COMPLETE_MESSAGE_TYPE,       "CalcApp Complete" },
  { CALCAPP_KEEPALIVE_MESSAGE_TYPE,      "CalcApp Keep-Alive" },
  { CALCAPP_KEEPALIVE_ACK_MESSAGE_TYPE,  "CalcApp Keep-Alive Ack" },
  { 0, NULL }
};


static void
dissect_calcappprotocol_message(tvbuff_t *message_tvb, packet_info *pinfo, proto_tree *calcappprotocol_tree)
{
  guint8 type;

  type = tvb_get_guint8(message_tvb, MESSAGE_TYPE_OFFSET);
  col_add_fstr(pinfo->cinfo, COL_INFO, "%s ", val_to_str_const(type, message_type_values, "Unknown CalcAppProtocol type"));

  proto_tree_add_item(calcappprotocol_tree, hf_message_type,      message_tvb, MESSAGE_TYPE_OFFSET,      MESSAGE_TYPE_LENGTH,      ENC_BIG_ENDIAN);
  proto_tree_add_item(calcappprotocol_tree, hf_message_flags,     message_tvb, MESSAGE_FLAGS_OFFSET,     MESSAGE_FLAGS_LENGTH,     ENC_BIG_ENDIAN);
  proto_tree_add_item(calcappprotocol_tree, hf_message_length,    message_tvb, MESSAGE_LENGTH_OFFSET,    MESSAGE_LENGTH_LENGTH,    ENC_BIG_ENDIAN);
  proto_tree_add_item(calcappprotocol_tree, hf_message_jobid,     message_tvb, MESSAGE_JOBID_OFFSET,     MESSAGE_JOBID_LENGTH,     ENC_BIG_ENDIAN);
  proto_tree_add_item(calcappprotocol_tree, hf_message_jobsize,   message_tvb, MESSAGE_JOBSIZE_OFFSET,   MESSAGE_JOBSIZE_LENGTH,   ENC_BIG_ENDIAN);
  proto_tree_add_item(calcappprotocol_tree, hf_message_completed, message_tvb, MESSAGE_COMPLETED_OFFSET, MESSAGE_COMPLETED_LENGTH, ENC_BIG_ENDIAN);
}


static int
dissect_calcappprotocol(tvbuff_t *message_tvb, packet_info *pinfo, proto_tree *tree, void *data _U_)
{
  proto_item *calcappprotocol_item;
  proto_tree *calcappprotocol_tree;

  col_set_str(pinfo->cinfo, COL_PROTOCOL, "CalcAppProtocol");

  /* In the interest of speed, if "tree" is NULL, don't do any work not
     necessary to generate protocol tree items. */
  if (tree) {
    /* create the calcappprotocol protocol tree */
    calcappprotocol_item = proto_tree_add_item(tree, proto_calcappprotocol, message_tvb, 0, -1, ENC_NA);
    calcappprotocol_tree = proto_item_add_subtree(calcappprotocol_item, ett_calcappprotocol);
  } else {
    calcappprotocol_tree = NULL;
  };
  /* dissect the message */
  dissect_calcappprotocol_message(message_tvb, pinfo, calcappprotocol_tree);
  return(TRUE);
}


/* Register the protocol with Wireshark */
void
proto_register_calcappprotocol(void)
{

  /* Setup list of header fields */
  static hf_register_info hf[] = {
    { &hf_message_type,      { "Type",      "calcappprotocol.message_type",      FT_UINT8,  BASE_DEC, VALS(message_type_values), 0x0, NULL, HFILL } },
    { &hf_message_flags,     { "Flags",     "calcappprotocol.message_flags",     FT_UINT8,  BASE_DEC, NULL,                      0x0, NULL, HFILL } },
    { &hf_message_length,    { "Length",    "calcappprotocol.message_length",    FT_UINT16, BASE_DEC, NULL,                      0x0, NULL, HFILL } },
    { &hf_message_jobid,     { "JobID",     "calcappprotocol.message_jobid",     FT_UINT32, BASE_DEC, NULL,                      0x0, NULL, HFILL } },
    { &hf_message_jobsize,   { "JobSize",   "calcappprotocol.message_jobsize",   FT_UINT64, BASE_DEC, NULL,                      0x0, NULL, HFILL } },
    { &hf_message_completed, { "Completed", "calcappprotocol.message_completed", FT_UINT64, BASE_DEC, NULL,                      0x0, NULL, HFILL } },
  };

  /* Setup protocol subtree array */
  static gint *ett[] = {
    &ett_calcappprotocol
  };

  /* Register the protocol name and description */
  proto_calcappprotocol = proto_register_protocol("Calculation Application Protocol", "CalcAppProtocol", "calcappprotocol");

  /* Required function calls to register the header fields and subtrees used */
  proto_register_field_array(proto_calcappprotocol, hf, array_length(hf));
  proto_register_subtree_array(ett, array_length(ett));
}

void
proto_reg_handoff_calcappprotocol(void)
{
  dissector_handle_t calcappprotocol_handle;

  calcappprotocol_handle = create_dissector_handle(dissect_calcappprotocol, proto_calcappprotocol);
  dissector_add_uint("sctp.ppi", CALCAPPPROTOCOL_PAYLOAD_PROTOCOL_ID_LEGACY, calcappprotocol_handle);
  dissector_add_uint("sctp.ppi", CALCAPP_PAYLOAD_PROTOCOL_ID, calcappprotocol_handle);
}

/*
 * Editor modelines  -  https://www.wireshark.org/tools/modelines.html
 *
 * Local Variables:
 * c-basic-offset: 2
 * tab-width: 8
 * indent-tabs-mode: nil
 * End:
 *
 * ex: set shiftwidth=2 tabstop=8 expandtab:
 * :indentSize=2:tabSize=8:noTabs=true:
 */
