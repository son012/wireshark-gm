/* packet-vines.c
 * Routines for Banyan VINES protocol packet disassembly
 *
 * $Id: packet-vines.c,v 1.52 2003/04/18 04:28:07 guy Exp $
 *
 * Don Lafontaine <lafont02@cn.ca>
 *
 * Ethereal - Network traffic analyzer
 * By Gerald Combs <gerald@ethereal.com>
 * Copyright 1998 Gerald Combs
 * Joerg Mayer <jmayer@loplof.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <glib.h>
#include <epan/packet.h>
#include "packet-vines.h"
#include "etypes.h"
#include "ppptypes.h"
#include "ipproto.h"
#include "arcnet_pids.h"
#include "llcsaps.h"

#define UDP_PORT_VINES	573

static int proto_vines = -1;
static int hf_vines_protocol = -1;

static gint ett_vines = -1;
static gint ett_vines_tctl = -1;

static int proto_vines_frp = -1;

static gint ett_vines_frp = -1;

static int proto_vines_llc = -1;

static gint ett_vines_llc = -1;

static int proto_vines_ipc = -1;

static gint ett_vines_ipc = -1;
static gint ett_vines_ipc_control = -1;

static int proto_vines_spp = -1;

static gint ett_vines_spp = -1;
static gint ett_vines_spp_control = -1;

static int proto_vines_arp = -1;

static gint ett_vines_arp = -1;

static int proto_vines_rtp = -1;

static int proto_vines_srtp = -1;

static gint ett_vines_srtp = -1;

static int proto_vines_icp = -1;

static gint ett_vines_icp = -1;

void
capture_vines(packet_counts *ld)
{
	ld->vines++;
}

static dissector_handle_t vines_handle;
static dissector_handle_t data_handle;

/* AFAIK Vines FRP (Fragmentation Protocol) is used on all media except
 * Ethernet and TR (and probably FDDI) - Fragmentation on these media types
 * is not possible
 * FIXME: Do we need to use this header with PPP too?
 */
static void
dissect_vines_frp(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree)
{
	guint8   vines_frp_ctrl;
	proto_tree *vines_frp_tree;
	proto_item *ti;
	gchar	frp_flags_str[32];
	tvbuff_t *next_tvb;

	if (check_col(pinfo->cinfo, COL_PROTOCOL))
		col_set_str(pinfo->cinfo, COL_PROTOCOL, "Vines FRP");
	if (check_col(pinfo->cinfo, COL_INFO))
		col_clear(pinfo->cinfo, COL_INFO);

	if (tree) {
		ti = proto_tree_add_item(tree, proto_vines_frp, tvb, 0, 2,
		    FALSE);
		vines_frp_tree = proto_item_add_subtree(ti, ett_vines_frp);

		vines_frp_ctrl = tvb_get_guint8(tvb, 0);

		/*
		 * 1: first fragment of vines packet
		 * 2: last fragment of vines packet
		 * 4 ... 80: unused
		 */
		switch (vines_frp_ctrl) {

		case 0:
			strcpy(frp_flags_str, "middle");
			break;

		case 1:
			strcpy(frp_flags_str, "first");
			break;

		case 2:
			strcpy(frp_flags_str, "last");
			break;

		case 3:
			strcpy(frp_flags_str, "only");
			break;

		default:
			strcpy(frp_flags_str, "please report: unknown");
			break;
		}

		proto_tree_add_text(vines_frp_tree, tvb, 0, 1,
				    "Control Flags: 0x%02x = %s fragment",
				    vines_frp_ctrl, frp_flags_str);

		proto_tree_add_text(vines_frp_tree, tvb, 1, 1,
				    "Sequence Number: 0x%02x",
				    tvb_get_guint8(tvb, 1));
	}

	/* Decode the "real" Vines now */
	next_tvb = tvb_new_subset(tvb, 2, -1, -1);
	call_dissector(vines_handle, next_tvb, pinfo, tree);
}

void
proto_register_vines_frp(void)
{
	static gint *ett[] = {
		&ett_vines_frp,
	};

	proto_vines_frp = proto_register_protocol(
	    "Banyan Vines Fragmentation Protocol", "Vines FRP", "vines_frp");
	proto_register_subtree_array(ett, array_length(ett));
}

void
proto_reg_handoff_vines_frp(void)
{
	dissector_handle_t vines_frp_handle;

	vines_frp_handle = create_dissector_handle(dissect_vines_frp,
	    proto_vines_frp);
	dissector_add("ip.proto", IP_PROTO_VINES, vines_frp_handle);

	/* XXX: AFAIK, src and dst port must be the same; should
	   the dissector check for that? */
	dissector_add("udp.port", UDP_PORT_VINES, vines_frp_handle);
}

static dissector_table_t vines_llc_dissector_table;

#define VINES_LLC_IP	0xba

static const value_string vines_llc_ptype_vals[] = {
	{ VINES_LLC_IP, "Vines IP" },
	{ 0,            NULL }
};

static void
dissect_vines_llc(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree)
{
	guint8   ptype;
	proto_tree *vines_llc_tree;
	proto_item *ti;
	tvbuff_t *next_tvb;

	if (check_col(pinfo->cinfo, COL_PROTOCOL))
		col_set_str(pinfo->cinfo, COL_PROTOCOL, "Vines LLC");
	if (check_col(pinfo->cinfo, COL_INFO))
		col_clear(pinfo->cinfo, COL_INFO);

	ptype = tvb_get_guint8(tvb, 0);
	if (check_col(pinfo->cinfo, COL_INFO))
		col_add_str(pinfo->cinfo, COL_INFO,
		    val_to_str(ptype, vines_llc_ptype_vals,
		      "Unknown protocol 0x%02x"));
	if (tree) {
		ti = proto_tree_add_item(tree, proto_vines_llc, tvb, 0, 1,
		    FALSE);
		vines_llc_tree = proto_item_add_subtree(ti, ett_vines_llc);

		proto_tree_add_text(vines_llc_tree, tvb, 0, 1,
				    "Packet Type: %s (0x%02x)",
				    val_to_str(ptype, vines_llc_ptype_vals,
				        "Unknown"),
				    ptype);
	}

	next_tvb = tvb_new_subset(tvb, 1, -1, -1);
	if (!dissector_try_port(vines_llc_dissector_table, ptype,
	    next_tvb, pinfo, tree))
		call_dissector(data_handle, next_tvb, pinfo, tree);
}

void
proto_register_vines_llc(void)
{
	static gint *ett[] = {
		&ett_vines_llc,
	};

	proto_vines_llc = proto_register_protocol(
	    "Banyan Vines LLC", "Vines LLC", "vines_llc");
	proto_register_subtree_array(ett, array_length(ett));

	/* subdissector code */
	vines_llc_dissector_table = register_dissector_table("vines_llc.ptype",
	    "Vines LLC protocol", FT_UINT8, BASE_HEX);
}

void
proto_reg_handoff_vines_llc(void)
{
	dissector_handle_t vines_llc_handle;

	vines_llc_handle = create_dissector_handle(dissect_vines_llc,
	    proto_vines_llc);
	dissector_add("llc.dsap", SAP_VINES1, vines_llc_handle);
	dissector_add("llc.dsap", SAP_VINES2, vines_llc_handle);
}

static dissector_table_t vines_dissector_table;

static const value_string class_vals[] = {
	{ 0x00, "Reachable regardless of cost" },
	{ 0x30, "Reachable via LAN" },
	{ 0,    NULL }
};

static const value_string proto_vals[] = {
	{ VIP_PROTO_IPC, "IPC" },
	{ VIP_PROTO_SPP, "SPP" },
	{ VIP_PROTO_ARP, "ARP" },
	{ VIP_PROTO_RTP, "RTP" },
	{ VIP_PROTO_ICP, "ICP" },
	{ 0,             NULL }
};

static const guint8 bcast_addr[VINES_ADDR_LEN] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

static void
dissect_vines(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree)
{
	int         offset = 0;
	e_vip       viph;
	proto_tree *vip_tree, *tctl_tree;
	proto_item *ti;
	const guint8     *dst_addr, *src_addr;
	gboolean is_broadcast = FALSE;
	int  hops = 0;
	tvbuff_t *next_tvb;

	if (check_col(pinfo->cinfo, COL_PROTOCOL))
		col_set_str(pinfo->cinfo, COL_PROTOCOL, "Vines IP");
	if (check_col(pinfo->cinfo, COL_INFO))
		col_clear(pinfo->cinfo, COL_INFO);

	/* To do: check for runts, errs, etc. */

	/* Avoids alignment problems on many architectures. */
	tvb_memcpy(tvb, (guint8 *)&viph, offset, sizeof(e_vip));

	viph.vip_chksum = g_ntohs(viph.vip_chksum);
	viph.vip_pktlen = g_ntohs(viph.vip_pktlen);

	if (check_col(pinfo->cinfo, COL_INFO)) {
		col_add_fstr(pinfo->cinfo, COL_INFO, "%s (0x%02x)",
		    val_to_str(viph.vip_proto, proto_vals,
		        "Unknown VIP protocol"),
		    viph.vip_proto);
	}

	src_addr = tvb_get_ptr(tvb, offset+12, VINES_ADDR_LEN);
	SET_ADDRESS(&pinfo->net_src, AT_VINES, VINES_ADDR_LEN, src_addr);
	SET_ADDRESS(&pinfo->src, AT_VINES, VINES_ADDR_LEN, src_addr);
	dst_addr = tvb_get_ptr(tvb, offset+6, VINES_ADDR_LEN);
	SET_ADDRESS(&pinfo->net_dst, AT_VINES, VINES_ADDR_LEN,dst_addr);
	SET_ADDRESS(&pinfo->dst, AT_VINES, VINES_ADDR_LEN, dst_addr);

 	/* helpers to transport control */
	if (memcmp(viph.vip_dst, bcast_addr, VINES_ADDR_LEN) == 0)
 		is_broadcast = TRUE;
 	hops = viph.vip_tctl & 0xf;

	if (tree) {
		ti = proto_tree_add_item(tree, proto_vines, tvb,
					 offset, viph.vip_pktlen,
					 FALSE);
		vip_tree = proto_item_add_subtree(ti, ett_vines);
		proto_tree_add_text(vip_tree, tvb, offset,      2,
				    "Packet checksum: 0x%04x",
				    viph.vip_chksum);
		proto_tree_add_text(vip_tree, tvb, offset +  2, 2,
				    "Packet length: %u",
				    viph.vip_pktlen);
		ti = proto_tree_add_text(vip_tree, tvb, offset +  4, 1,
				    "Transport control: 0x%02x",
				    viph.vip_tctl);
		tctl_tree = proto_item_add_subtree(ti, ett_vines_tctl);
		/*
		 * XXX - bit 0x80 is "Normal" if 0; what is it if 1?
		 */
		if (is_broadcast) {
			proto_tree_add_text(tctl_tree, tvb, offset + 4, 1,
			    decode_boolean_bitfield(viph.vip_tctl, 0x40, 1*8,
			      "Router nodes",
			      "All nodes"));
			proto_tree_add_text(tctl_tree, tvb, offset + 4, 1, "%s",
			    decode_enumerated_bitfield(viph.vip_tctl, 0x30, 1*8,
				      class_vals, "%s"));
		} else {
			proto_tree_add_text(tctl_tree, tvb, offset + 4, 1,
			    decode_boolean_bitfield(viph.vip_tctl, 0x40, 1*8,
			      "Forwarding router can handle redirect packets",
			      "Forwarding router cannot handle redirect packets"));
			proto_tree_add_text(tctl_tree, tvb, offset + 4, 1,
			    decode_boolean_bitfield(viph.vip_tctl, 0x20, 1*8,
			      "Return metric notification packet",
			      "Do not return metric notification packet"));
			proto_tree_add_text(tctl_tree, tvb, offset + 4, 1,
			    decode_boolean_bitfield(viph.vip_tctl, 0x10, 1*8,
			      "Return exception notification packet",
			      "Do not return exception notification packet"));
		}
		proto_tree_add_text(tctl_tree, tvb, offset + 4, 1,
		    decode_numeric_bitfield(viph.vip_tctl, 0x0F, 1*8,
			"Hop count remaining = %u"));
		proto_tree_add_uint(vip_tree, hf_vines_protocol, tvb,
				    offset +  5, 1,
				    viph.vip_proto);
		proto_tree_add_text(vip_tree, tvb, offset +  6,
				    VINES_ADDR_LEN,
				    "Destination: %s",
				    vines_addr_to_str(dst_addr));
		proto_tree_add_text(vip_tree, tvb, offset +  12,
				    VINES_ADDR_LEN,
				    "Source: %s",
				    vines_addr_to_str(src_addr));
	}

	offset += 18;
	next_tvb = tvb_new_subset(tvb, offset, -1, -1);
	if (!dissector_try_port(vines_dissector_table, viph.vip_proto,
	    next_tvb, pinfo, tree))
		call_dissector(data_handle, next_tvb, pinfo, tree);
}

void
proto_register_vines(void)
{
	static gint *ett[] = {
		&ett_vines,
		&ett_vines_tctl,
	};

	static hf_register_info hf[] = {
	  { &hf_vines_protocol,
	    { "Protocol",			"vines_ip.protocol",
	      FT_UINT8,		BASE_HEX,	VALS(proto_vals),	0x0,
	      "Vines protocol", HFILL }}
	};

	proto_vines = proto_register_protocol("Banyan Vines IP", "Vines IP",
	    "vines_ip");
	proto_register_field_array(proto_vines, hf, array_length(hf));
	proto_register_subtree_array(ett, array_length(ett));

	/* subdissector code */
	vines_dissector_table = register_dissector_table("vines_ip.protocol",
	    "Vines protocol", FT_UINT8, BASE_HEX);

	vines_handle = create_dissector_handle(dissect_vines, proto_vines);
}

void
proto_reg_handoff_vines(void)
{
	dissector_add("ethertype", ETHERTYPE_VINES, vines_handle);
	dissector_add("ppp.protocol", PPP_VINES, vines_handle);
	dissector_add("arcnet.protocol_id", ARCNET_PROTO_BANYAN, vines_handle);
	dissector_add("vines_llc.ptype", VINES_LLC_IP, vines_handle);
	data_handle = find_dissector("data");
}

static const value_string pkttype_vals[] = {
	{ PKTTYPE_DGRAM, "Datagram" },
	{ PKTTYPE_DATA,  "Data" },
	{ PKTTYPE_ERR,   "Error" },
	{ PKTTYPE_DISC,  "Disconnect" },
	{ PKTTYPE_PROBE, "Probe" },
	{ PKTTYPE_ACK,   "Ack" },
	{ 0,             NULL }
};

static heur_dissector_list_t vines_ipc_heur_subdissector_list;

static const value_string vipc_err_vals[] = {
	{ 151, "Bad socket descriptor" },
	{ 152, "Address already in use" },
	{ 153, "Invalid operation" },
	{ 154, "User address parameter fault" },
	{ 155, "Net/host unreachable" },
	{ 156, "Message overflow error" },
	{ 157, "Destination socket does not exist" },
	{ 158, "Address family does not exist" },
	{ 159, "Socket type does not exist" },
	{ 160, "Protocol does not exist" },
	{ 161, "No more sockets available" },
	{ 162, "No buffer space available" },
	{ 163, "Timeout event" },
	{ 164, "Operation not supported" },
	{ 165, "Resource not available" },
	{ 166, "Internal communication service failure" },
	{ 167, "Controller reset failure" },
	{ 0,   NULL }
};

static void
dissect_vines_ipc(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree)
{
	int          offset = 0;
	e_vipc       viph;
	proto_tree *vipc_tree = NULL, *control_tree;
	proto_item *ti;
	tvbuff_t *next_tvb;

	if (check_col(pinfo->cinfo, COL_PROTOCOL))
		col_set_str(pinfo->cinfo, COL_PROTOCOL, "VIPC");
	if (check_col(pinfo->cinfo, COL_INFO))
		col_clear(pinfo->cinfo, COL_INFO);

	/* To do: check for runts, errs, etc. */

	/* Avoids alignment problems on many architectures. */
	tvb_memcpy(tvb, (guint8 *)&viph, offset, sizeof(e_vipc));

	viph.vipc_sport = g_ntohs(viph.vipc_sport);
	viph.vipc_dport = g_ntohs(viph.vipc_dport);
	viph.vipc_lclid = g_ntohs(viph.vipc_lclid);
	viph.vipc_rmtid = g_ntohs(viph.vipc_rmtid);

	if (check_col(pinfo->cinfo, COL_PROTOCOL))
		col_set_str(pinfo->cinfo, COL_PROTOCOL, "Vines IPC");
	if (check_col(pinfo->cinfo, COL_INFO)) {
		switch (viph.vipc_pkttype) {

		case PKTTYPE_DGRAM:
	 		col_add_fstr(pinfo->cinfo, COL_INFO,
				     "%s D=%04x S=%04x",
				     val_to_str(viph.vipc_pkttype, pkttype_vals,
				         "Unknown packet type (0x%02x)"),
				     viph.vipc_dport, viph.vipc_sport);
			break;

		case PKTTYPE_ERR:
	 		col_add_fstr(pinfo->cinfo, COL_INFO,
				     "%s NS=%u NR=%u Err=%s RID=%04x LID=%04x D=%04x S=%04x",
				     val_to_str(viph.vipc_pkttype, pkttype_vals,
				         "Unknown packet type (0x%02x)"),
				     viph.vipc_seqno, viph.vipc_ack,
				     val_to_str(viph.vipc_err_len,
				         vipc_err_vals, "Unknown (%u)"),
				     viph.vipc_rmtid, viph.vipc_lclid,
				     viph.vipc_dport, viph.vipc_sport);
			break;

		default:
	 		col_add_fstr(pinfo->cinfo, COL_INFO,
				     "%s NS=%u NR=%u Len=%u RID=%04x LID=%04x D=%04x S=%04x",
				     val_to_str(viph.vipc_pkttype, pkttype_vals,
				         "Unknown packet type (0x%02x)"),
				     viph.vipc_seqno, viph.vipc_ack,
				     viph.vipc_err_len, viph.vipc_rmtid,
				     viph.vipc_lclid, viph.vipc_dport,
				     viph.vipc_sport);
			break;
		}
	}

	if (tree) {
		ti = proto_tree_add_item(tree, proto_vines_ipc, tvb, offset,
		    sizeof(viph), FALSE);
		vipc_tree = proto_item_add_subtree(ti, ett_vines_ipc);
		proto_tree_add_text(vipc_tree, tvb, offset,      2,
				    "Source port: 0x%04x", viph.vipc_sport);
	}
	offset += 2;
	if (tree) {
		proto_tree_add_text(vipc_tree, tvb, offset,  2,
				    "Destination port: 0x%04x",
				    viph.vipc_dport);
	}
	offset += 2;
	if (tree) {
		proto_tree_add_text(vipc_tree, tvb, offset,  1,
				    "Packet type: 0x%02x (%s)",
				    viph.vipc_pkttype,
				    val_to_str(viph.vipc_pkttype, pkttype_vals,
				        "Unknown"));
	}
	offset += 1;
	if (viph.vipc_pkttype != PKTTYPE_DGRAM) {
		if (tree) {
			ti = proto_tree_add_text(vipc_tree, tvb, offset,  1,
					    "Control: 0x%02x",
					    viph.vipc_control);
			control_tree = proto_item_add_subtree(ti,
			    ett_vines_ipc_control);
			/*
			 * XXX - do reassembly based on BOM/EOM bits.
			 */
			proto_tree_add_text(control_tree, tvb, offset, 1,
			    decode_boolean_bitfield(viph.vipc_control, 0x80,
			      1*8,
			      "Send immediate acknowledgment",
			      "Do not send immediate acknowledgement"));
			proto_tree_add_text(control_tree, tvb, offset, 1,
			    decode_boolean_bitfield(viph.vipc_control, 0x40,
			      1*8,
			      "End of message",
			      "Not end of message"));
			proto_tree_add_text(control_tree, tvb, offset, 1,
			    decode_boolean_bitfield(viph.vipc_control, 0x20,
			      1*8,
			      "Beginning of message",
			      "Not beginning of message"));
			proto_tree_add_text(control_tree, tvb, offset, 1,
			    decode_boolean_bitfield(viph.vipc_control, 0x10,
			      1*8,
			      "Abort current message",
			      "Do not abort current message"));
		}
	}
	offset += 1;
	if (viph.vipc_pkttype != PKTTYPE_DGRAM) {
		if (tree) {
			proto_tree_add_text(vipc_tree, tvb, offset,  2,
					    "Local Connection ID: 0x%04x",
					    viph.vipc_lclid);
		}
		offset += 2;
		if (tree) {
			proto_tree_add_text(vipc_tree, tvb, offset,  2,
					    "Remote Connection ID: 0x%04x",
					    viph.vipc_rmtid);
		}
		offset += 2;
		if (tree) {
			proto_tree_add_text(vipc_tree, tvb, offset, 2,
					    "Sequence number: %u",
					    viph.vipc_seqno);
		}
		offset += 2;
		if (tree) {
			proto_tree_add_text(vipc_tree, tvb, offset, 2,
					    "Ack number: %u", viph.vipc_ack);
		}
		offset += 2;
		if (tree) {
			if (viph.vipc_pkttype == PKTTYPE_ERR) {
				proto_tree_add_text(vipc_tree, tvb, offset, 2,
						    "Error: %s (%u)",
						    val_to_str(viph.vipc_err_len,
						        vipc_err_vals,
						        "Unknown"),
						    viph.vipc_err_len);
			} else {
				proto_tree_add_text(vipc_tree, tvb, offset, 2,
						    "Length: %u",
						    viph.vipc_err_len);
			}
		}
		offset += 2;
	}

	/*
	 * For data packets, try the heuristic dissectors for Vines SPP;
	 * if none of them accept the packet, or if it's not a data packet,
	 * dissect it as data.
	 */
	next_tvb = tvb_new_subset(tvb, offset, -1, -1);
	if (viph.vipc_pkttype != PKTTYPE_DATA ||
	    !dissector_try_heuristic(vines_ipc_heur_subdissector_list,
	      next_tvb, pinfo, tree))
		call_dissector(data_handle, next_tvb, pinfo, tree);
}

void
proto_register_vines_ipc(void)
{
	static gint *ett[] = {
		&ett_vines_ipc,
		&ett_vines_ipc_control,
	};

	proto_vines_ipc = proto_register_protocol("Banyan Vines IPC",
	    "Vines IPC", "vines_ipc");
	proto_register_subtree_array(ett, array_length(ett));

	register_heur_dissector_list("vines_ipc",
	    &vines_ipc_heur_subdissector_list);
}

void
proto_reg_handoff_vines_ipc(void)
{
	dissector_handle_t vines_ipc_handle;

	vines_ipc_handle = create_dissector_handle(dissect_vines_ipc,
	    proto_vines_ipc);
	dissector_add("vines_ip.protocol", VIP_PROTO_IPC, vines_ipc_handle);
}

static heur_dissector_list_t vines_spp_heur_subdissector_list;

static void
dissect_vines_spp(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree)
{
	int          offset = 0;
	e_vspp       viph;
	proto_tree  *vspp_tree, *control_tree;
	proto_item  *ti;
	tvbuff_t    *next_tvb;

	if (check_col(pinfo->cinfo, COL_PROTOCOL))
		col_set_str(pinfo->cinfo, COL_PROTOCOL, "VSPP");
	if (check_col(pinfo->cinfo, COL_INFO))
		col_clear(pinfo->cinfo, COL_INFO);

	/* To do: check for runts, errs, etc. */

	/* Avoids alignment problems on many architectures. */
	tvb_memcpy(tvb, (guint8 *)&viph, offset, sizeof(e_vspp));

	viph.vspp_sport = g_ntohs(viph.vspp_sport);
	viph.vspp_dport = g_ntohs(viph.vspp_dport);
	viph.vspp_lclid = g_ntohs(viph.vspp_lclid);
	viph.vspp_rmtid = g_ntohs(viph.vspp_rmtid);

	if (check_col(pinfo->cinfo, COL_PROTOCOL))
		col_set_str(pinfo->cinfo, COL_PROTOCOL, "Vines SPP");
	if (check_col(pinfo->cinfo, COL_INFO))
 		col_add_fstr(pinfo->cinfo, COL_INFO,
			     "%s NS=%u NR=%u Window=%u RID=%04x LID=%04x D=%04x S=%04x",
			     val_to_str(viph.vspp_pkttype, pkttype_vals,
			         "Unknown packet type (0x%02x)"),
			     viph.vspp_seqno, viph.vspp_ack, viph.vspp_win,
			     viph.vspp_rmtid, viph.vspp_lclid, viph.vspp_dport,
			     viph.vspp_sport);

	if (tree) {
		ti = proto_tree_add_item(tree, proto_vines_spp, tvb, offset,
		    sizeof(viph), FALSE);
		vspp_tree = proto_item_add_subtree(ti, ett_vines_spp);
		proto_tree_add_text(vspp_tree, tvb, offset,      2,
				    "Source port: 0x%04x", viph.vspp_sport);
		proto_tree_add_text(vspp_tree, tvb, offset + 2,  2,
				    "Destination port: 0x%04x",
				    viph.vspp_dport);
		proto_tree_add_text(vspp_tree, tvb, offset + 4,  1,
				    "Packet type: 0x%02x (%s)",
				    viph.vspp_pkttype,
				    val_to_str(viph.vspp_pkttype, pkttype_vals,
				        "Unknown"));
		ti = proto_tree_add_text(vspp_tree, tvb, offset + 5,  1,
				    "Control: 0x%02x", viph.vspp_control);
		control_tree = proto_item_add_subtree(ti, ett_vines_spp_control);
		/*
		 * XXX - do reassembly based on BOM/EOM bits.
		 */
		proto_tree_add_text(control_tree, tvb, offset + 5, 1,
		    decode_boolean_bitfield(viph.vspp_control, 0x80, 1*8,
		      "Send immediate acknowledgment",
		      "Do not send immediate acknowledgement"));
		proto_tree_add_text(control_tree, tvb, offset + 5, 1,
		    decode_boolean_bitfield(viph.vspp_control, 0x40, 1*8,
		      "End of message",
		      "Not end of message"));
		proto_tree_add_text(control_tree, tvb, offset + 5, 1,
		    decode_boolean_bitfield(viph.vspp_control, 0x20, 1*8,
		      "Beginning of message",
		      "Not beginning of message"));
		proto_tree_add_text(control_tree, tvb, offset + 5, 1,
		    decode_boolean_bitfield(viph.vspp_control, 0x10, 1*8,
		      "Abort current message",
		      "Do not abort current message"));
		proto_tree_add_text(vspp_tree, tvb, offset + 6,  2,
				    "Local Connection ID: 0x%04x",
				    viph.vspp_lclid);
		proto_tree_add_text(vspp_tree, tvb, offset + 8,  2,
				    "Remote Connection ID: 0x%04x",
				    viph.vspp_rmtid);
		proto_tree_add_text(vspp_tree, tvb, offset + 10, 2,
				    "Sequence number: %u",
				    viph.vspp_seqno);
		proto_tree_add_text(vspp_tree, tvb, offset + 12, 2,
				    "Ack number: %u", viph.vspp_ack);
		proto_tree_add_text(vspp_tree, tvb, offset + 14, 2,
				    "Window: %u", viph.vspp_win);
	}
	offset += 16; /* sizeof SPP */

	/*
	 * For data packets, try the heuristic dissectors for Vines SPP;
	 * if none of them accept the packet, or if it's not a data packet,
	 * dissect it as data.
	 */
	next_tvb = tvb_new_subset(tvb, offset, -1, -1);
	if (viph.vspp_pkttype != PKTTYPE_DATA ||
	    !dissector_try_heuristic(vines_spp_heur_subdissector_list,
	      next_tvb, pinfo, tree))
		call_dissector(data_handle, next_tvb, pinfo, tree);
}

void
proto_register_vines_spp(void)
{
	static gint *ett[] = {
		&ett_vines_spp,
		&ett_vines_spp_control,
	};

	proto_vines_spp = proto_register_protocol("Banyan Vines SPP",
	    "Vines SPP", "vines_spp");
	proto_register_subtree_array(ett, array_length(ett));

	register_heur_dissector_list("vines_spp",
	    &vines_spp_heur_subdissector_list);
}

void
proto_reg_handoff_vines_spp(void)
{
	dissector_handle_t vines_spp_handle;

	vines_spp_handle = create_dissector_handle(dissect_vines_spp,
	    proto_vines_spp);
	dissector_add("vines_ip.protocol", VIP_PROTO_SPP, vines_spp_handle);
}

static const value_string vines_version_vals[] = {
	{ 0x00, "Pre-5.50" },
	{ 0x01, "5.50" },
	{ 0,    NULL }
};

#define VARP_QUERY_REQ		0x00
#define VARP_SERVICE_RESP	0x01
#define VARP_ASSIGNMENT_REQ	0x02
#define VARP_ASSIGNMENT_RESP	0x03

static const value_string vines_arp_packet_type_vals[] = {
	{ VARP_QUERY_REQ,       "Query request" },
	{ VARP_SERVICE_RESP,    "Service response" },
	{ VARP_ASSIGNMENT_REQ,  "Assignment request" },
	{ VARP_ASSIGNMENT_RESP, "Assignment response" },
	{ 0,                    NULL }
};

static void
dissect_vines_arp(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree)
{
	proto_tree *vines_arp_tree = NULL;
	proto_item *ti;
	guint8   version;
	guint8   packet_type;

	if (check_col(pinfo->cinfo, COL_PROTOCOL))
		col_set_str(pinfo->cinfo, COL_PROTOCOL, "Vines ARP");
	if (check_col(pinfo->cinfo, COL_INFO))
		col_clear(pinfo->cinfo, COL_INFO);

	if (tree) {
		ti = proto_tree_add_item(tree, proto_vines_arp, tvb, 0, -1,
		    FALSE);
		vines_arp_tree = proto_item_add_subtree(ti, ett_vines_arp);

		version = tvb_get_guint8(tvb, 0);
		proto_tree_add_text(vines_arp_tree, tvb, 0, 1,
				    "Version: %s (0x%02x)",
				    val_to_str(version, vines_version_vals,
				      "Unknown"),
				    version);
	}
	packet_type = tvb_get_guint8(tvb, 1);
	if (check_col(pinfo->cinfo, COL_INFO)) {
		col_add_str(pinfo->cinfo, COL_INFO,
		    val_to_str(packet_type, vines_arp_packet_type_vals,
		      "Unknown (0x%02x)"));
	}
	if (tree) {
		proto_tree_add_text(vines_arp_tree, tvb, 1, 1,
				    "Packet Type: %s (0x%02x)",
				    val_to_str(packet_type,
				      vines_arp_packet_type_vals,
				      "Unknown"),
				    packet_type);
	}
	if (packet_type == VARP_ASSIGNMENT_RESP) {
		if (check_col(pinfo->cinfo, COL_INFO)) {
			col_append_fstr(pinfo->cinfo, COL_INFO, ", Address = %s",
				    vines_addr_to_str(tvb_get_ptr(tvb, 2, VINES_ADDR_LEN)));
		}
		if (tree) {
			proto_tree_add_text(vines_arp_tree, tvb, 2,
					    VINES_ADDR_LEN,
					    "Address: %s",
					    vines_addr_to_str(tvb_get_ptr(tvb, 2, VINES_ADDR_LEN)));
		}
	}
	if (tree) {
		proto_tree_add_text(vines_arp_tree, tvb, 2+VINES_ADDR_LEN, 4,
				    "Sequence Number: %u",
				    tvb_get_ntohl(tvb, 2+VINES_ADDR_LEN));
		proto_tree_add_text(vines_arp_tree, tvb, 2+VINES_ADDR_LEN+4, 2,
				    "Interface Metric: %u",
				    tvb_get_ntohs(tvb, 2+VINES_ADDR_LEN+4));
	}
}

void
proto_register_vines_arp(void)
{
	static gint *ett[] = {
		&ett_vines_arp,
	};

	proto_vines_arp = proto_register_protocol(
	    "Banyan Vines ARP", "Vines ARP", "vines_arp");
	proto_register_subtree_array(ett, array_length(ett));
}

void
proto_reg_handoff_vines_arp(void)
{
	dissector_handle_t vines_arp_handle;

	vines_arp_handle = create_dissector_handle(dissect_vines_arp,
	    proto_vines_arp);
	dissector_add("vines_ip.protocol", VIP_PROTO_ARP, vines_arp_handle);
}

#define VRTP_OP_REQUEST		0x01
#define VRTP_OP_UPDATE_RESPONSE	0x02
#define VRTP_OP_REDIRECT	0x03
#define VRTP_OP_REINITIALIZE	0x04

static const value_string vines_rtp_operation_type_vals[] = {
	{ VRTP_OP_REQUEST,         "Request" },
	{ VRTP_OP_UPDATE_RESPONSE, "Update/response" },
	{ VRTP_OP_REDIRECT,        "Redirect" },
	{ VRTP_OP_REINITIALIZE,    "Reinitialize" },
	{ 0,                       NULL }
};

static const value_string vines_rtp_node_type_vals[] = {
	{ 0x01, "Host" },
	{ 0x02, "Router" },
	{ 0,    NULL }
};

static const value_string vines_rtp_compatibility_flags_vals[] = {
	{ 0x00, "Same Revision" },
	{ 0x01, "Version Mismatch" },
	{ 0x02, "Old Neighbors" },
	{ 0x03, "Automatic" },
	{ 0,    NULL }
};

static void
dissect_vines_rtp(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree)
{
	proto_tree *vines_rtp_tree = NULL;
	proto_item *ti;
	guint16  version;
	guint8   operation_type;
	guint8   node_type;
	guint8   compatibility_flags;

	if (check_col(pinfo->cinfo, COL_PROTOCOL))
		col_set_str(pinfo->cinfo, COL_PROTOCOL, "Vines RTP");
	if (check_col(pinfo->cinfo, COL_INFO))
		col_clear(pinfo->cinfo, COL_INFO);

	/*
	 * XXX - can the first byte of a VRTP packet be 0?
	 * Or is that just the case for VSRTP packets?
	 */
	if (tvb_get_guint8(tvb, 0) != 0) {
		proto_tree_add_item(tree, proto_vines_rtp, tvb, 0, -1,
		    FALSE);
		return;
	}

	/*
	 * Assume this is Vines SRTP.
	 */
	if (check_col(pinfo->cinfo, COL_PROTOCOL))
		col_set_str(pinfo->cinfo, COL_PROTOCOL, "Vines SRTP");
	if (tree) {
		ti = proto_tree_add_item(tree, proto_vines_srtp, tvb, 0, -1,
		    FALSE);
		vines_rtp_tree = proto_item_add_subtree(ti, ett_vines_srtp);

		version = tvb_get_ntohs(tvb, 0);
		proto_tree_add_text(vines_rtp_tree, tvb, 0, 2,
				    "Version: %s (0x%04x)",
				    val_to_str(version, vines_version_vals,
				      "Unknown"),
				    version);
	}
	operation_type = tvb_get_guint8(tvb, 2);
	if (check_col(pinfo->cinfo, COL_INFO)) {
		col_add_str(pinfo->cinfo, COL_INFO,
		    val_to_str(operation_type, vines_rtp_operation_type_vals,
		      "Unknown (0x%02x)"));
	}
	if (tree) {
		proto_tree_add_text(vines_rtp_tree, tvb, 2, 1,
				    "Operation Type: %s (0x%02x)",
				    val_to_str(operation_type,
				      vines_rtp_operation_type_vals,
				      "Unknown"),
				    operation_type);
		node_type = tvb_get_guint8(tvb, 3);
		proto_tree_add_text(vines_rtp_tree, tvb, 3, 1,
				    "Node Type: %s (0x%02x)",
				    val_to_str(node_type,
				      vines_rtp_node_type_vals,
				      "Unknown"),
				    node_type);
		compatibility_flags = tvb_get_guint8(tvb, 4);
		proto_tree_add_text(vines_rtp_tree, tvb, 4, 1,
				    "Compatibility Flags: %s (0x%02x)",
				    val_to_str(compatibility_flags,
				      vines_rtp_compatibility_flags_vals,
				      "Unknown"),
				    compatibility_flags);
		proto_tree_add_text(vines_rtp_tree, tvb, 6, -1, "Data");
	}
}

void
proto_register_vines_rtp(void)
{
	static gint *ett[] = {
		&ett_vines_srtp,
	};

	proto_vines_rtp = proto_register_protocol(
	    "Banyan Vines RTP", "Vines RTP", "vines_rtp");
	proto_vines_srtp = proto_register_protocol(
	    "Banyan Vines Sequenced RTP", "Vines SRTP", "vines_srtp");
	proto_register_subtree_array(ett, array_length(ett));
}

void
proto_reg_handoff_vines_rtp(void)
{
	dissector_handle_t vines_rtp_handle;

	vines_rtp_handle = create_dissector_handle(dissect_vines_rtp,
	    proto_vines_rtp);
	dissector_add("vines_ip.protocol", VIP_PROTO_RTP, vines_rtp_handle);
}

#define VICP_EXCEPTION_NOTIFICATION	0x0000
#define VICP_METRIC_NOTIFICATION	0x0001

static const value_string vines_icp_packet_type_vals[] = {
	{ VICP_EXCEPTION_NOTIFICATION, "Exception notification" },
	{ VICP_METRIC_NOTIFICATION,    "Metric notification" },
	{ 0,                           NULL }
};

static void
dissect_vines_icp(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree)
{
	int offset = 0;
	proto_tree *vines_icp_tree = NULL;
	proto_item *ti;
	guint16  packet_type;
	guint16  exception_code;
	guint16  metric;
	gboolean save_in_error_pkt;
	tvbuff_t *next_tvb;

	if (check_col(pinfo->cinfo, COL_PROTOCOL))
		col_set_str(pinfo->cinfo, COL_PROTOCOL, "Vines ICP");
	if (check_col(pinfo->cinfo, COL_INFO))
		col_clear(pinfo->cinfo, COL_INFO);

	if (tree) {
		ti = proto_tree_add_item(tree, proto_vines_icp, tvb, 0, -1,
		    FALSE);
		vines_icp_tree = proto_item_add_subtree(ti, ett_vines_icp);
	}

	packet_type = tvb_get_ntohs(tvb, offset);
	if (check_col(pinfo->cinfo, COL_INFO)) {
		col_add_str(pinfo->cinfo, COL_INFO,
		    val_to_str(packet_type, vines_icp_packet_type_vals,
		      "Unknown (0x%02x)"));
	}
	if (tree) {
		proto_tree_add_text(vines_icp_tree, tvb, offset, 2,
				    "Packet Type: %s (0x%04x)",
				    val_to_str(packet_type,
				      vines_icp_packet_type_vals,
				      "Unknown"),
				    packet_type);
	}
	offset += 2;

	switch (packet_type) {

	case VICP_EXCEPTION_NOTIFICATION:
		exception_code = tvb_get_ntohs(tvb, offset);
		if (check_col(pinfo->cinfo, COL_INFO)) {
			col_append_fstr(pinfo->cinfo, COL_INFO, ", %s",
			    val_to_str(exception_code, vipc_err_vals,
			        "Unknown exception code (%u)"));
		}
		if (tree) {
			proto_tree_add_text(vines_icp_tree, tvb, offset, 2,
					    "Exception Code: %s (%u)",
					    val_to_str(exception_code,
					      vipc_err_vals,
					      "Unknown"),
					    exception_code);
		}
		break;

	case VICP_METRIC_NOTIFICATION:
		metric = tvb_get_ntohs(tvb, offset);
		if (check_col(pinfo->cinfo, COL_INFO)) {
			col_append_fstr(pinfo->cinfo, COL_INFO, ", metric %u",
			    metric);
		}
		if (tree) {
			proto_tree_add_text(vines_icp_tree, tvb, offset, 2,
					    "Metric: %u", metric);
		}
		break;
	}
	offset += 2;

	/*
	 * Save the current value of the "we're inside an error packet"
	 * flag, and set that flag; subdissectors may treat packets
	 * that are the payload of error packets differently from
	 * "real" packets.
	 */
	save_in_error_pkt = pinfo->in_error_pkt;
	pinfo->in_error_pkt = TRUE;

	/* Decode the first 40 bytes of the original VIP datagram. */
	next_tvb = tvb_new_subset(tvb, offset, -1, -1);
	call_dissector(vines_handle, next_tvb, pinfo, vines_icp_tree);

	/* Restore the "we're inside an error packet" flag. */
	pinfo->in_error_pkt = save_in_error_pkt;
}

void
proto_register_vines_icp(void)
{
	static gint *ett[] = {
		&ett_vines_icp,
	};

	proto_vines_icp = proto_register_protocol(
	    "Banyan Vines ICP", "Vines ICP", "vines_icp");
	proto_register_subtree_array(ett, array_length(ett));
}

void
proto_reg_handoff_vines_icp(void)
{
	dissector_handle_t vines_icp_handle;

	vines_icp_handle = create_dissector_handle(dissect_vines_icp,
	    proto_vines_icp);
	dissector_add("vines_ip.protocol", VIP_PROTO_ICP, vines_icp_handle);
}
