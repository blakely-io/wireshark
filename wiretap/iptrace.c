/* iptrace.c
 *
 * Wiretap Library
 * Copyright (c) 1998 by Gilbert Ramirez <gram@alumni.rice.edu>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */
#include "config.h"
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include "wtap-int.h"
#include "file_wrappers.h"
#include "atm.h"
#include "iptrace.h"

#define IPTRACE_IFT_HF	0x3d    /* Support for PERCS IP-HFI*/
#define IPTRACE_IFT_IB  0xc7    /* IP over Infiniband. Number by IANA */

static gboolean iptrace_read_1_0(wtap *wth, wtap_rec *rec,
    Buffer *buf, int *err, gchar **err_info, gint64 *data_offset);
static gboolean iptrace_seek_read_1_0(wtap *wth, gint64 seek_off,
    wtap_rec *rec, Buffer *buf, int *err, gchar **err_info);

static gboolean iptrace_read_2_0(wtap *wth, wtap_rec *rec,
    Buffer *buf, int *err, gchar **err_info, gint64 *data_offset);
static gboolean iptrace_seek_read_2_0(wtap *wth, gint64 seek_off,
    wtap_rec *rec, Buffer *buf, int *err, gchar **err_info);

static gboolean iptrace_read_rec_data(FILE_T fh, Buffer *buf,
    wtap_rec *rec, int *err, gchar **err_info);
static void fill_in_pseudo_header(int encap,
    union wtap_pseudo_header *pseudo_header, const char *pkt_text);
static int wtap_encap_ift(unsigned int  ift);

/*
 * Size of the version string in the file header.
 */
#define VERSION_STRING_SIZE	11

wtap_open_return_val iptrace_open(wtap *wth, int *err, gchar **err_info)
{
	char version_string[VERSION_STRING_SIZE+1];

	if (!wtap_read_bytes(wth->fh, version_string, VERSION_STRING_SIZE,
	    err, err_info)) {
		if (*err != WTAP_ERR_SHORT_READ)
			return WTAP_OPEN_ERROR;
		return WTAP_OPEN_NOT_MINE;
	}
	version_string[VERSION_STRING_SIZE] = '\0';

	if (strcmp(version_string, "iptrace 1.0") == 0) {
		wth->file_type_subtype = WTAP_FILE_TYPE_SUBTYPE_IPTRACE_1_0;
		wth->subtype_read = iptrace_read_1_0;
		wth->subtype_seek_read = iptrace_seek_read_1_0;
		wth->file_tsprec = WTAP_TSPREC_SEC;
	}
	else if (strcmp(version_string, "iptrace 2.0") == 0) {
		wth->file_type_subtype = WTAP_FILE_TYPE_SUBTYPE_IPTRACE_2_0;
		wth->subtype_read = iptrace_read_2_0;
		wth->subtype_seek_read = iptrace_seek_read_2_0;
		wth->file_tsprec = WTAP_TSPREC_NSEC;
	}
	else {
		return WTAP_OPEN_NOT_MINE;
	}

	return WTAP_OPEN_MINE;
}

/***********************************************************
 * iptrace 1.0                                             *
 ***********************************************************/

/*
 * iptrace 1.0, discovered through inspection
 *
 * Packet record contains:
 *
 *	an initial header, with a length field and a time stamp, in
 *	seconds since the Epoch;
 *
 *	data, with the specified length.
 *
 * The data contains:
 *
 *	a bunch of information about the packet;
 *
 *	padding, at least for FDDI;
 *
 *	the raw packet data.
 */

/*
 * Offsets of fields in the initial header.
 */
#define IPTRACE_1_0_REC_LENGTH_OFFSET	0	/* 0-3: size of record data */
#define IPTRACE_1_0_TV_SEC_OFFSET	4	/* 4-7: time stamp, seconds since the Epoch */

#define IPTRACE_1_0_PHDR_SIZE	8	/* initial header */

/*
 * Offsets of fields in the packet information.
 */
/* Bytes 0-2 unknown */
#define IPTRACE_1_0_UNIT_OFFSET		3	/* 3: interface unit number */
#define IPTRACE_1_0_PREFIX_OFFSET	4	/* 4-7: null-terminated name prefix */
#define IPTRACE_1_0_PKT_TEXT_OFFSET	8	/* 8-19: text in 2.0; what is it in 1.0? */
#define IPTRACE_1_0_IF_TYPE_OFFSET	20	/* 20: SNMP ifType value */
#define IPTRACE_1_0_TX_FLAGS_OFFSET	21	/* 21: 0=receive, 1=transmit */

#define IPTRACE_1_0_PINFO_SIZE	22	/* packet information */

static gboolean
iptrace_read_rec_1_0(FILE_T fh, wtap_rec *rec, Buffer *buf,
    int *err, gchar **err_info)
{
	guint8			header[IPTRACE_1_0_PHDR_SIZE];
	guint32			record_length;
	guint8			pkt_info[IPTRACE_1_0_PINFO_SIZE];
	guint8			if_type;
	guint32			packet_size;

	if (!wtap_read_bytes_or_eof(fh, header, IPTRACE_1_0_PHDR_SIZE, err,
	    err_info)) {
		/* Read error or EOF */
		return FALSE;
	}

	/* Get the record length */
	record_length = pntoh32(&header[IPTRACE_1_0_REC_LENGTH_OFFSET]);
	if (record_length < IPTRACE_1_0_PINFO_SIZE) {
		/*
		 * Uh-oh, the record isn't big enough to even have a
		 * packet information header.
		 */
		*err = WTAP_ERR_BAD_FILE;
		*err_info = g_strdup_printf("iptrace: file has a %u-byte record, too small to have even a packet information header",
		    record_length);
		return FALSE;
	}

	/*
	 * Get the packet information.
	 */
	if (!wtap_read_bytes(fh, pkt_info, IPTRACE_1_0_PINFO_SIZE, err,
	    err_info)) {
		/* Read error or EOF */
		return FALSE;
	}

	/*
	 * The if_type field of the frame header appears to be an SNMP
	 * ifType value giving the type of the interface.  Check out the
	 * <net/if_types.h> header file.
	 */
	if_type = pkt_info[IPTRACE_1_0_IF_TYPE_OFFSET];
	rec->rec_header.packet_header.pkt_encap = wtap_encap_ift(if_type);
	if (rec->rec_header.packet_header.pkt_encap == WTAP_ENCAP_UNKNOWN) {
		*err = WTAP_ERR_UNSUPPORTED;
		*err_info = g_strdup_printf("iptrace: interface type IFT=0x%02x unknown or unsupported",
		    if_type);
		return FALSE;
	}

	/* Get the packet data size */
	packet_size = record_length - IPTRACE_1_0_PINFO_SIZE;

	/*
	 * AIX appears to put 3 bytes of padding in front of FDDI
	 * frames; strip that crap off.
	 */
	if (rec->rec_header.packet_header.pkt_encap == WTAP_ENCAP_FDDI_BITSWAPPED) {
		/*
		 * The packet size is really a record size and includes
		 * the padding.
		 */
		if (packet_size < 3) {
			/*
			 * Uh-oh, the record isn't big enough to even have
			 * the padding.
			 */
			*err = WTAP_ERR_BAD_FILE;
			*err_info = g_strdup_printf("iptrace: file has a %u-byte record, too small to have even a packet meta-data header",
			    record_length);
			return FALSE;
		}
		packet_size -= 3;

		/*
		 * Skip the padding.
		 */
		if (!wtap_read_bytes(fh, NULL, 3, err, err_info))
			return FALSE;
	}
	if (packet_size > WTAP_MAX_PACKET_SIZE_STANDARD) {
		/*
		 * Probably a corrupt capture file; don't blow up trying
		 * to allocate space for an immensely-large packet.
		 */
		*err = WTAP_ERR_BAD_FILE;
		*err_info = g_strdup_printf("iptrace: File has %u-byte packet, bigger than maximum of %u",
		    packet_size, WTAP_MAX_PACKET_SIZE_STANDARD);
		return FALSE;
	}

	rec->rec_type = REC_TYPE_PACKET;
	rec->presence_flags = WTAP_HAS_TS;
	rec->rec_header.packet_header.len = packet_size;
	rec->rec_header.packet_header.caplen = packet_size;
	rec->ts.secs = pntoh32(&header[IPTRACE_1_0_TV_SEC_OFFSET]);
	rec->ts.nsecs = 0;

	/* Fill in the pseudo-header. */
	fill_in_pseudo_header(rec->rec_header.packet_header.pkt_encap,
	    &rec->rec_header.packet_header.pseudo_header,
	    (const char *)&pkt_info[IPTRACE_1_0_PKT_TEXT_OFFSET]);

	/* Get the packet data */
	return iptrace_read_rec_data(fh, buf, rec, err, err_info);
}

/* Read the next packet */
static gboolean iptrace_read_1_0(wtap *wth, wtap_rec *rec,
    Buffer *buf, int *err, gchar **err_info, gint64 *data_offset)
{
	*data_offset = file_tell(wth->fh);

	/* Read the packet */
	if (!iptrace_read_rec_1_0(wth->fh, rec, buf, err, err_info)) {
		/* Read error or EOF */
		return FALSE;
	}

	/* If the per-file encapsulation isn't known, set it to this
	   packet's encapsulation.

	   If it *is* known, and it isn't this packet's encapsulation,
	   set it to WTAP_ENCAP_PER_PACKET, as this file doesn't
	   have a single encapsulation for all packets in the file. */
	if (wth->file_encap == WTAP_ENCAP_UNKNOWN)
		wth->file_encap = rec->rec_header.packet_header.pkt_encap;
	else {
		if (wth->file_encap != rec->rec_header.packet_header.pkt_encap)
			wth->file_encap = WTAP_ENCAP_PER_PACKET;
	}

	return TRUE;
}

static gboolean iptrace_seek_read_1_0(wtap *wth, gint64 seek_off,
    wtap_rec *rec, Buffer *buf, int *err, gchar **err_info)
{
	if (file_seek(wth->random_fh, seek_off, SEEK_SET, err) == -1)
		return FALSE;

	/* Read the packet */
	if (!iptrace_read_rec_1_0(wth->random_fh, rec, buf, err, err_info)) {
		if (*err == 0)
			*err = WTAP_ERR_SHORT_READ;
		return FALSE;
	}
	return TRUE;
}

/***********************************************************
 * iptrace 2.0                                             *
 ***********************************************************/

/*
 * iptrace 2.0, discovered through inspection
 *
 * Packet record contains:
 *
 *	an initial header, with a length field and a time stamp, in
 *	seconds since the Epoch;
 *
 *	data, with the specified length.
 *
 * The data contains:
 *
 *	a bunch of information about the packet;
 *
 *	padding, at least for FDDI;
 *
 *	the raw packet data.
 */
/*
 * Offsets of fields in the initial header.
 */
#define IPTRACE_2_0_REC_LENGTH_OFFSET	0	/* 0-3: size of record data */
#define IPTRACE_2_0_TV_SEC0_OFFSET	4	/* 4-7: time stamp, seconds since the Epoch */

#define IPTRACE_2_0_PHDR_SIZE	8	/* initial header */

/*
 * Offsets of fields in the packet information.
 */
/* Bytes 0-2 unknown */
#define IPTRACE_2_0_UNIT_OFFSET		3	/* 3: interface unit number */
#define IPTRACE_2_0_PREFIX_OFFSET	4	/* 4-7: null-terminated name prefix */
#define IPTRACE_2_0_PKT_TEXT_OFFSET	8	/* 8-19: text stuff */
#define IPTRACE_2_0_IF_TYPE_OFFSET	20	/* 20: SNMP ifType value */
#define IPTRACE_2_0_TX_FLAGS_OFFSET	21	/* 21: 0=receive, 1=transmit */
/* Bytes 22-23 unknown */
#define IPTRACE_2_0_TV_SEC_OFFSET	24	/* 24-27: time stamp, seconds since the Epoch */
#define IPTRACE_2_0_TV_NSEC_OFFSET	28	/* 28-31: nanoseconds since that second */

#define IPTRACE_2_0_PINFO_SIZE	32	/* packet information */

static gboolean
iptrace_read_rec_2_0(FILE_T fh, wtap_rec *rec, Buffer *buf,
    int *err, gchar **err_info)
{
	guint8			header[IPTRACE_2_0_PHDR_SIZE];
	guint32			record_length;
	guint8			pkt_info[IPTRACE_2_0_PINFO_SIZE];
	guint8			if_type;
	guint32			packet_size;

	if (!wtap_read_bytes_or_eof(fh, header, IPTRACE_2_0_PHDR_SIZE, err,
	    err_info)) {
		/* Read error or EOF */
		return FALSE;
	}

	/* Get the record length */
	record_length = pntoh32(&header[IPTRACE_2_0_REC_LENGTH_OFFSET]);
	if (record_length < IPTRACE_2_0_PINFO_SIZE) {
		/*
		 * Uh-oh, the record isn't big enough to even have a
		 * packet information header.
		 */
		*err = WTAP_ERR_BAD_FILE;
		*err_info = g_strdup_printf("iptrace: file has a %u-byte record, too small to have even a packet information header",
		    record_length);
		return FALSE;
	}

	/*
	 * Get the packet information.
	 */
	if (!wtap_read_bytes(fh, pkt_info, IPTRACE_2_0_PINFO_SIZE, err,
	    err_info)) {
		/* Read error or EOF */
		return FALSE;
	}

	/*
	 * The if_type field of the frame header appears to be an SNMP
	 * ifType value giving the type of the interface.  Check out the
	 * <net/if_types.h> header file.
	 */
	if_type = pkt_info[IPTRACE_2_0_IF_TYPE_OFFSET];
	rec->rec_header.packet_header.pkt_encap = wtap_encap_ift(if_type);
#if 0
	/*
	 * We used to error out if the interface type in iptrace was
	 * unknown/unhandled, but an iptrace may contain packets
	 * from a variety of interfaces, some known, and others
	 * unknown.
	 *
	 * It is better to display the data even for unknown interface
	 * types, isntead of erroring out. In the future, it would be
	 * nice to be able to flag which frames are shown as data
	 * because their interface type is unknown, and also present
	 * the interface type number to the user so that it can be
	 * reported easily back to the Wireshark developer.
	 *
	 * XXX - what types are there that are used in files but
	 * that we don't handle?
	 */
	if (rec->rec_header.packet_header.pkt_encap == WTAP_ENCAP_UNKNOWN) {
		*err = WTAP_ERR_UNSUPPORTED;
		*err_info = g_strdup_printf("iptrace: interface type IFT=0x%02x unknown or unsupported",
		    if_type);
		return FALSE;
	}
#endif

	/* Get the packet data size */
	packet_size = record_length - IPTRACE_2_0_PINFO_SIZE;

	/*
	 * AIX appears to put 3 bytes of padding in front of FDDI
	 * frames; strip that crap off.
	 */
	if (rec->rec_header.packet_header.pkt_encap == WTAP_ENCAP_FDDI_BITSWAPPED) {
		/*
		 * The packet size is really a record size and includes
		 * the padding.
		 */
		if (packet_size < 3) {
			/*
			 * Uh-oh, the record isn't big enough to even have
			 * the padding.
			 */
			*err = WTAP_ERR_BAD_FILE;
			*err_info = g_strdup_printf("iptrace: file has a %u-byte record, too small to have even a packet meta-data header",
			    record_length);
			return FALSE;
		}
		packet_size -= 3;

		/*
		 * Skip the padding.
		 */
		if (!wtap_read_bytes(fh, NULL, 3, err, err_info))
			return FALSE;
	}
	if (packet_size > WTAP_MAX_PACKET_SIZE_STANDARD) {
		/*
		 * Probably a corrupt capture file; don't blow up trying
		 * to allocate space for an immensely-large packet.
		 */
		*err = WTAP_ERR_BAD_FILE;
		*err_info = g_strdup_printf("iptrace: File has %u-byte packet, bigger than maximum of %u",
		    packet_size, WTAP_MAX_PACKET_SIZE_STANDARD);
		return FALSE;
	}

	rec->rec_type = REC_TYPE_PACKET;
	rec->presence_flags = WTAP_HAS_TS;
	rec->rec_header.packet_header.len = packet_size;
	rec->rec_header.packet_header.caplen = packet_size;
	rec->ts.secs = pntoh32(&pkt_info[IPTRACE_2_0_TV_SEC_OFFSET]);
	rec->ts.nsecs = pntoh32(&pkt_info[IPTRACE_2_0_TV_NSEC_OFFSET]);

	/* Fill in the pseudo-header. */
	fill_in_pseudo_header(rec->rec_header.packet_header.pkt_encap,
	    &rec->rec_header.packet_header.pseudo_header,
	    (const char *)&pkt_info[IPTRACE_1_0_PKT_TEXT_OFFSET]);

	/* Get the packet data */
	return iptrace_read_rec_data(fh, buf, rec, err, err_info);
}

/* Read the next packet */
static gboolean iptrace_read_2_0(wtap *wth, wtap_rec *rec,
    Buffer *buf, int *err, gchar **err_info, gint64 *data_offset)
{
	*data_offset = file_tell(wth->fh);

	/* Read the packet */
	if (!iptrace_read_rec_2_0(wth->fh, rec, buf, err, err_info)) {
		/* Read error or EOF */
		return FALSE;
	}

	/* If the per-file encapsulation isn't known, set it to this
	   packet's encapsulation.

	   If it *is* known, and it isn't this packet's encapsulation,
	   set it to WTAP_ENCAP_PER_PACKET, as this file doesn't
	   have a single encapsulation for all packets in the file. */
	if (wth->file_encap == WTAP_ENCAP_UNKNOWN)
		wth->file_encap = rec->rec_header.packet_header.pkt_encap;
	else {
		if (wth->file_encap != rec->rec_header.packet_header.pkt_encap)
			wth->file_encap = WTAP_ENCAP_PER_PACKET;
	}

	return TRUE;
}

static gboolean iptrace_seek_read_2_0(wtap *wth, gint64 seek_off,
    wtap_rec *rec, Buffer *buf, int *err, gchar **err_info)
{
	if (file_seek(wth->random_fh, seek_off, SEEK_SET, err) == -1)
		return FALSE;

	/* Read the packet */
	if (!iptrace_read_rec_2_0(wth->random_fh, rec, buf, err, err_info)) {
		if (*err == 0)
			*err = WTAP_ERR_SHORT_READ;
		return FALSE;
	}
	return TRUE;
}

static gboolean
iptrace_read_rec_data(FILE_T fh, Buffer *buf, wtap_rec *rec,
    int *err, gchar **err_info)
{
	if (!wtap_read_packet_bytes(fh, buf, rec->rec_header.packet_header.caplen, err, err_info))
		return FALSE;

	if (rec->rec_header.packet_header.pkt_encap == WTAP_ENCAP_ATM_PDUS) {
		/*
		 * Attempt to guess from the packet data, the VPI,
		 * and the VCI information about the type of traffic.
		 */
		atm_guess_traffic_type(rec, ws_buffer_start_ptr(buf));
	}

	return TRUE;
}

/*
 * Fill in the pseudo-header information we can.
 *
 * For ATM traffic, "iptrace", alas, doesn't tell us what type of traffic
 * is in the packet - it was presumably run on a machine that was one of
 * the endpoints of the connection, so in theory it could presumably have
 * told us, but, for whatever reason, it failed to do so - perhaps the
 * low-level mechanism that feeds the presumably-AAL5 frames to us doesn't
 * have access to that information (e.g., because it's in the ATM driver,
 * and the ATM driver merely knows that stuff on VPI/VCI X.Y should be
 * handed up to some particular client, it doesn't know what that client is).
 *
 * We let our caller try to figure out what kind of traffic it is, either
 * by guessing based on the VPI/VCI, guessing based on the header of the
 * packet, seeing earlier traffic that set up the circuit and specified
 * in some fashion what sort of traffic it is, or being told by the user.
 */
static void
fill_in_pseudo_header(int encap, union wtap_pseudo_header *pseudo_header,
    const char *pkt_text)
{
	char	if_text[9];
	char	*decimal;
	int	Vpi = 0;
	int	Vci = 0;

	switch (encap) {

	case WTAP_ENCAP_ATM_PDUS:
		/* Rip apart the "x.y" text into Vpi/Vci numbers */
		memcpy(if_text, &pkt_text[4], 8);
		if_text[8] = '\0';
		decimal = strchr(if_text, '.');
		if (decimal) {
			*decimal = '\0';
			Vpi = (int)strtoul(if_text, NULL, 10);
			decimal++;
			Vci = (int)strtoul(decimal, NULL, 10);
		}

		/*
		 * OK, which value means "DTE->DCE" and which value means
		 * "DCE->DTE"?
		 */
		pseudo_header->atm.channel = pkt_text[13];

		pseudo_header->atm.vpi = Vpi;
		pseudo_header->atm.vci = Vci;

		/* We don't have this information */
		pseudo_header->atm.flags = 0;
		pseudo_header->atm.cells = 0;
		pseudo_header->atm.aal5t_u2u = 0;
		pseudo_header->atm.aal5t_len = 0;
		pseudo_header->atm.aal5t_chksum = 0;
		break;

	case WTAP_ENCAP_ETHERNET:
		/* We assume there's no FCS in this frame. */
		pseudo_header->eth.fcs_len = 0;
		break;
	}
}

/* Given an RFC1573 (SNMP ifType) interface type,
 * return the appropriate Wiretap Encapsulation Type.
 */
static int
wtap_encap_ift(unsigned int  ift)
{

	static const int ift_encap[] = {
/* 0x0 */	WTAP_ENCAP_UNKNOWN,	/* nothing */
/* 0x1 */	WTAP_ENCAP_UNKNOWN,	/* IFT_OTHER */
/* 0x2 */	WTAP_ENCAP_UNKNOWN,	/* IFT_1822 */
/* 0x3 */	WTAP_ENCAP_UNKNOWN,	/* IFT_HDH1822 */
/* 0x4 */	WTAP_ENCAP_RAW_IP,	/* IFT_X25DDN */
/* 0x5 */	WTAP_ENCAP_UNKNOWN,	/* IFT_X25 */
/* 0x6 */	WTAP_ENCAP_ETHERNET,	/* IFT_ETHER */
/* 0x7 */	WTAP_ENCAP_ETHERNET,	/* IFT_ISO88023 */
/* 0x8 */	WTAP_ENCAP_UNKNOWN,	/* IFT_ISO88024 */
/* 0x9 */	WTAP_ENCAP_TOKEN_RING,	/* IFT_ISO88025 */
/* 0xa */	WTAP_ENCAP_UNKNOWN,	/* IFT_ISO88026 */
/* 0xb */	WTAP_ENCAP_UNKNOWN,	/* IFT_STARLAN */
/* 0xc */	WTAP_ENCAP_RAW_IP,	/* IFT_P10, IBM SP switch */
/* 0xd */	WTAP_ENCAP_UNKNOWN,	/* IFT_P80 */
/* 0xe */	WTAP_ENCAP_UNKNOWN,	/* IFT_HY */
/* 0xf */	WTAP_ENCAP_FDDI_BITSWAPPED,	/* IFT_FDDI */
/* 0x10 */	WTAP_ENCAP_LAPB,	/* IFT_LAPB */	/* no data to back this up */
/* 0x11 */	WTAP_ENCAP_UNKNOWN,	/* IFT_SDLC */
/* 0x12 */	WTAP_ENCAP_UNKNOWN,	/* IFT_T1 */
/* 0x13 */	WTAP_ENCAP_UNKNOWN,	/* IFT_CEPT */
/* 0x14 */	WTAP_ENCAP_UNKNOWN,	/* IFT_ISDNBASIC */
/* 0x15 */	WTAP_ENCAP_UNKNOWN,	/* IFT_ISDNPRIMARY */
/* 0x16 */	WTAP_ENCAP_UNKNOWN,	/* IFT_PTPSERIAL */
/* 0x17 */	WTAP_ENCAP_UNKNOWN,	/* IFT_PPP */
/* 0x18 */	WTAP_ENCAP_RAW_IP,	/* IFT_LOOP */
/* 0x19 */	WTAP_ENCAP_UNKNOWN,	/* IFT_EON */
/* 0x1a */	WTAP_ENCAP_UNKNOWN,	/* IFT_XETHER */
/* 0x1b */	WTAP_ENCAP_UNKNOWN,	/* IFT_NSIP */
/* 0x1c */	WTAP_ENCAP_UNKNOWN,	/* IFT_SLIP */
/* 0x1d */	WTAP_ENCAP_UNKNOWN,	/* IFT_ULTRA */
/* 0x1e */	WTAP_ENCAP_UNKNOWN,	/* IFT_DS3 */
/* 0x1f */	WTAP_ENCAP_UNKNOWN,	/* IFT_SIP */
/* 0x20 */	WTAP_ENCAP_UNKNOWN,	/* IFT_FRELAY */
/* 0x21 */	WTAP_ENCAP_UNKNOWN,	/* IFT_RS232 */
/* 0x22 */	WTAP_ENCAP_UNKNOWN,	/* IFT_PARA */
/* 0x23 */	WTAP_ENCAP_UNKNOWN,	/* IFT_ARCNET */
/* 0x24 */	WTAP_ENCAP_UNKNOWN,	/* IFT_ARCNETPLUS */
/* 0x25 */	WTAP_ENCAP_ATM_PDUS,	/* IFT_ATM */
	};
	#define NUM_IFT_ENCAPS (sizeof ift_encap / sizeof ift_encap[0])

	if (ift < NUM_IFT_ENCAPS) {
		return ift_encap[ift];
	}
	else {
		switch(ift) {
			/* Infiniband*/
			case IPTRACE_IFT_IB:
				return WTAP_ENCAP_INFINIBAND;
				break;

			/* Host Fabric Interface */
			case IPTRACE_IFT_HF:
				/* The HFI interface on AIX provides raw IP
				in the packet trace. It's unclear if the HFI
				can be configured for any other protocol, and if
				any field in the iptrace header indicates what
				that protocol is. For now, we are hard-coding
				this as RAW_IP, but if we find another iptrace file
				using HFI that provides another protocol, we will
				have to figure out which field in the iptrace file
				encodes it. */
				return WTAP_ENCAP_RAW_IP;
				break;

			default:
				return WTAP_ENCAP_UNKNOWN;
		}
	}
}

/*
 * Editor modelines  -  https://www.wireshark.org/tools/modelines.html
 *
 * Local variables:
 * c-basic-offset: 8
 * tab-width: 8
 * indent-tabs-mode: t
 * End:
 *
 * vi: set shiftwidth=8 tabstop=8 noexpandtab:
 * :indentSize=8:tabSize=8:noTabs=false:
 */
