/**
 * \file openflow_switch_session.cc
 * \brief Source file for the OpenFlowSwitchSession class.
 *
 * authors : Dong (Kevin) Jin
 */

#include "openflow_switch_session.h"
#include "openflow_controller.h"
#include "openflow_controller_session.h"
#include "openflow_message.h"
#include "util/errhandle.h" // defines error_quit() method
#include "net/host.h" // defines Host class
#include "os/base/protocols.h" // defines S3FNET_REGISTER_PROTOCOL macro
#include "os/base/nic_protocol_session.h"
#include "os/simple_mac/simple_mac_message.h"
#include "os/ipv4/ip_message.h"
#include "os/udp/udp_message.h"
#include "os/tcp/tcp_message.h"
#include "net/net.h"
#include "env/namesvc.h"
#include "net/network_interface.h"
#include <pthread.h>

//#define OPENFLOW_SWITCH_DEBUG

#ifdef OPENFLOW_SWITCH_DEBUG
#define OF_SWITCH_DUMP(x) printf("OF_SWITCH: "); x
#else
#define OF_SWITCH_DUMP(x)
#endif

namespace s3f {
namespace s3fnet {

S3FNET_REGISTER_PROTOCOL(OpenFlowSwitchSession, OF_SWITCH_PROTOCOL_CLASSNAME);

const char * OpenFlowSwitchSession::GetManufacturerDescription ()
{
	return "The S3F team";
}

const char * OpenFlowSwitchSession::GetHardwareDescription ()
{
	return "N/A";
}

const char * OpenFlowSwitchSession::GetSoftwareDescription ()
{
	return "Simulated OpenFlow Switch";
}

const char * OpenFlowSwitchSession::GetSerialNumber ()
{
	return "N/A";
}

static uint64_t GenerateId ()
{
	uint8_t ea[ETH_ADDR_LEN];
	eth_addr_random (ea);
	return eth_addr_to_uint64 (ea);
}


OpenFlowSwitchSession::OpenFlowSwitchSession(ProtocolGraph* graph) :
				ProtocolSession(graph), m_mtu(0xffff), m_lastExecute(0),
				np_controller(0), m_flags(0), m_missSendLen(OFP_DEFAULT_MISS_SEND_LEN), m_address(0),
				ofctrl_port(NULL), is_ctrl_sess(false), ctrl_host(0), ctrl_sess(0)
{
	// create your session-related variables here
	OF_SWITCH_DUMP(printf("An OpenFlow switch protocol session is created.\n"));

	time_init (); // OFSI's clock; needed to use the buffer storage system.

	m_chain = chain_create ();
	if (m_chain == 0)
	{
		error_quit ("Not enough memory to create the flow table.");
	}

	m_id = GenerateId();

	m_ports.reserve (DP_MAX_PORTS);
	vport_table_init (&m_vportTable);
}

OpenFlowSwitchSession::~OpenFlowSwitchSession ()
{
	// reclaim your session-related variables here
	OF_SWITCH_DUMP(printf("An openflow switch protocol session is reclaimed.\n"));

	DoDispose();
}

void OpenFlowSwitchSession::config(s3f::dml::Configuration* cfg)
{
	// the same method at the parent class must be called
	ProtocolSession::config(cfg);

	char* str = (char*) cfg->findSingle("controller");
	if(str)
	{
		if(s3f::dml::dmlConfig::isConf(str))
			error_quit("ERROR: OpenFlowSwitchSession::config(), invalid controller attribute.\n");

		ctrl_nhi = str;
		OF_SWITCH_DUMP(printf("\t active controller = %s \n", ctrl_nhi.c_str()));
	}
}

void OpenFlowSwitchSession::init()
{
	// the same method at the parent class must be called
	ProtocolSession::init();

	// initialize the session-related variables here
	OF_SWITCH_DUMP(printf("OpenFlow switch session is initialized.\n"));

	if( inHost()->inNet()->openflow_controller_type() == 1 ) // entity-based controller, controller is a protocol session
		is_ctrl_sess = true;
	else
		is_ctrl_sess = false;

	/* set up controller */
	OF_SWITCH_DUMP(printf("**** Set up Controller\n"));
	SetController();

	/* add switch ports */
	S3FNET_HOST_IFACE_MAP ifaces = inHost()->getNetworkInterfaceMap();
	S3FNET_HOST_IFACE_MAP::iterator iter;
	for(iter=ifaces.begin(); iter!=ifaces.end(); ++iter)
	{
		OF_SWITCH_DUMP( printf("Add SwitchPort %d\n", (*iter).first) );
		NetworkInterface* nw_iface = (*iter).second;
		AddSwitchPort (nw_iface);
	}

	SetMtu(DEFAULT_MTU);
}

int OpenFlowSwitchSession::control(int ctrltyp, void* ctrlmsg, ProtocolSession* sess)
{
	switch(ctrltyp)
	{
	case OPENFLOW_SWITCH_CTRL_COMMAND1:
	case OPENFLOW_SWITCH_CTRL_COMMAND2:
		return 0; // openflow switch control commands, not in use yet
	default:
		return ProtocolSession::control(ctrltyp, ctrlmsg, sess);
	}
}

//in the future, if we allow the switch to have upper layer, such as udp/tcp/app, then push will be used
int OpenFlowSwitchSession::push(Activation msg, ProtocolSession* hi_sess, void* extinfo, std::size_t extinfo_size)
{
	error_quit("ERROR: a message is pushed down to the openflow switch session from protocol layer above; it's impossible for now.\n");
	return 0;
}

int OpenFlowSwitchSession::pop(Activation msg, ProtocolSession* lo_sess, void* extinfo, size_t extinfo_size)
{
	OF_SWITCH_DUMP(printf("A message is popped up to the openflow switch session from the MAC layer.\n"));

	MACOptionToAbove* macopt = (MACOptionToAbove*)extinfo;
	NetworkInterface* netdev = inHost()->getNetworkInterface(macopt->nw_iface);
	if(netdev == NULL) error_quit("OpenFlowSwitchSession::pop(), not able to find network interface");

	ProtocolMessage* packet = (ProtocolMessage*)msg;

	OF_SWITCH_DUMP (cout << "Switch[" << inHost()->nhi.toString()<< "], ID [" << m_address << "] --------------------------------------------" << endl);

	for (size_t i = 0; i < m_ports.size (); i++)
	{
		if (m_ports[i].netdev == netdev)
		{
			SwitchPacketMetadata data;
			//data.packet = packet->clone();
			data.packet = packet;

			//pthread_mutex_lock(&(inHost()->getTopNet()->of_buf_mutex));
			ofpbuf *buffer = BufferFromPacket (data.packet); //kevin: as for now, all Mtu has the same size
			//pthread_mutex_unlock(&(inHost()->getTopNet()->of_buf_mutex));

			m_ports[i].rx_packets++;
			m_ports[i].rx_bytes += buffer->size;
			data.buffer = buffer;

			pthread_mutex_lock(&(inHost()->getTopNet()->of_buf_mutex));
			uint32_t packet_uid = save_buffer (buffer);
			pthread_mutex_unlock(&(inHost()->getTopNet()->of_buf_mutex));

			SimpleMacMessage* l2_hdr = (SimpleMacMessage*)packet; //kevn: as for now, directly use SimpleMacMessage, will need to check layer2 msg type later
			data.src = l2_hdr->src48;
			data.dst = l2_hdr->dst48;
			data.protocolNumber = (uint16_t)(macopt->protocol_no);
			m_packetData.insert (std::make_pair (packet_uid, data));
			OF_SWITCH_DUMP ( cout << "Switch[" << inHost()->nhi.toString()<< "], Received packet from " << l2_hdr->src48 << " looking for " << l2_hdr->dst48 << endl );

			//temp
			//cout << "Switch[" << inHost()->nhi.toString() << "], packet_uid = " << packet_uid << " i = " << i << endl;

			RunThroughFlowTable (packet_uid, i);

			break;
		}
	}

	// Run periodic execution.
	ltime_t now = inHost()->now();
	if (now >= m_lastExecute + 1) // If a second or more has passed from the simulation time, execute.
	{
		// If port status is modified in any way, notify the controller.
		for (size_t i = 0; i < m_ports.size (); i++)
		{
			if (UpdatePortStatus (m_ports[i]))
			{
				SendPortStatus (m_ports[i], OFPPR_MODIFY);
			}
		}

		// If any flows have expired, delete them and notify the controller.
		List deleted = LIST_INITIALIZER (&deleted);
		sw_flow *f, *n;
		chain_timeout (m_chain, &deleted);
		LIST_FOR_EACH_SAFE (f, n, sw_flow, node, &deleted)
		{
			std::ostringstream str;
			str << "Flow [";
			for (int i = 0; i < 6; i++)
				str << (i!=0 ? ":" : "") << std::hex << f->key.flow.dl_src[i]/16 << f->key.flow.dl_src[i]%16;
			str << " -> ";
			for (int i = 0; i < 6; i++)
				str << (i!=0 ? ":" : "") << std::hex << f->key.flow.dl_dst[i]/16 << f->key.flow.dl_dst[i]%16;
			str <<  "] expired.";

			OF_SWITCH_DUMP (cout << str.str () << endl);
			SendFlowExpired (f, (ofp_flow_expired_reason)f->reason);
			list_remove (&f->node);
			flow_free (f);
		}

		m_lastExecute = now;
	}

	// returning 0 indicates success
	return 0;
}

void OpenFlowSwitchSession::DoDispose ()
{
	for (Ports_t::iterator b = m_ports.begin (), e = m_ports.end (); b != e; b++)
	{
		if(is_ctrl_sess == false)
		{
			//SendPortStatus (*b, OFPPR_DELETE); //temp prevent seg fault at the end
		}
		b->netdev = 0;
	}
	m_ports.clear ();

	if(is_ctrl_sess == false)
	{
		np_controller = 0;
	}
	chain_destroy (m_chain);
	RBTreeDestroy (m_vportTable.table);
}

void OpenFlowSwitchSession::SetController ()
{
	if(is_ctrl_sess == true) //controller protocol session
	{
		ctrl_host = inHost()->inNet()->getNameService()->hostnhi2hostobj(ctrl_nhi);
		assert(ctrl_host);
		ctrl_sess = (OpenFlowControllerSession*)(ctrl_host->sessionForNumber(S3FNET_PROTOCOL_TYPE_OPENFLOW_CONTROLLER));
		assert(ctrl_sess);

		switch(ctrl_sess->type)
		{
		case OF_LEARNING_SWITCH_CONTROLLER_ENTITY:
			((LearningControllerSession*)ctrl_sess)->setFlowExpirationTime(OF_FLOW_EXPIRATION_TIME);
			((LearningControllerSession*)ctrl_sess)->AddSwitch(this);
			break;

		case NO_OF_CONTROLLER:
			error_quit("OpenFlowSwitchSession::SetController(), no controller entity.");
			break;

		default:
			error_quit("OpenFlowSwitchSession::SetController(), wrong controller entity type.");
			break;
		}
	}
	else //controller non-protocol session, direct function calls
	{
		np_controller = inHost()->inNet()->get_of_controller();
		assert(np_controller);

		switch(np_controller->type)
		{
		case OF_LEARNING_SWITCH_CONTROLLER:
			((LearningController*)np_controller)->AddSwitch(this);
			break;

		case OF_DROP_CONTROLLER:
			((DropController*)np_controller)->AddSwitch(this);
			break;

		case NO_OF_CONTROLLER:
			error_quit("OpenFlowSwitchSession::SetController(), no controller.");
			break;

		default:
			error_quit("OpenFlowSwitchSession::SetController(), wrong controller type.");
			break;
		}
	}
}

int OpenFlowSwitchSession::AddSwitchPort (NetworkInterface* switchPort)
{
	if (m_address == 0)
	{
		m_address = switchPort->getMac48Addr();
	}

	if (m_ports.size () < DP_MAX_PORTS)
	{
		Port p;
		p.config = 0;
		p.netdev = switchPort;
		m_ports.push_back (p);

		// Notify the controller that this port has been added
		SendPortStatus (p, OFPPR_ADD);
	}
	else
	{
		return EXFULL;
	}

	return 0;
}

bool OpenFlowSwitchSession::SetMtu (const uint16_t mtu)
{
	m_mtu = mtu;
	return true;
}

uint16_t OpenFlowSwitchSession::GetMtu (void) const
{
	return m_mtu;
}


bool OpenFlowSwitchSession::IsLinkUp (void) const
{
	return true;
}

void OpenFlowSwitchSession::DoOutput (uint32_t packet_uid, int in_port, size_t max_len, int out_port, bool ignore_no_fwd)
{
	if (out_port != OFPP_CONTROLLER)
	{
		OutputPort (packet_uid, in_port, out_port, ignore_no_fwd);
	}
	else
	{
		OutputControl (packet_uid, in_port, max_len, OFPR_ACTION);
	}
}

// Add a virtual port table entry.
int OpenFlowSwitchSession::AddVPort (const ofp_vport_mod *ovpm)
{
	size_t actions_len = ntohs (ovpm->header.length) - sizeof *ovpm;
	unsigned int vport = ntohl (ovpm->vport);
	unsigned int parent_port = ntohl (ovpm->parent_port);

	// check whether port table entry exists for specified port number
	vport_table_entry *vpe = vport_table_lookup (&m_vportTable, vport);
	if (vpe != 0)
	{
		OF_SWITCH_DUMP ( cout << "vport " << vport << " already exists!" << endl );
		SendErrorMsg (OFPET_BAD_ACTION, OFPET_VPORT_MOD_FAILED, ovpm, ntohs (ovpm->header.length));
		return EINVAL;
	}

	// check whether actions are valid
	uint16_t v_code = ValidateVPortActions (ovpm->actions, actions_len);
	if (v_code != ACT_VALIDATION_OK)
	{
		SendErrorMsg (OFPET_BAD_ACTION, v_code, ovpm, ntohs (ovpm->header.length));
		return EINVAL;
	}

	vpe = vport_table_entry_alloc (actions_len);

	vpe->vport = vport;
	vpe->parent_port = parent_port;
	if (vport < OFPP_VP_START || vport > OFPP_VP_END)
	{
		OF_SWITCH_DUMP ( cout << "port " << vport << " is not in the virtual port range (" << OFPP_VP_START << "-" << OFPP_VP_END << ")" << endl );
		SendErrorMsg (OFPET_BAD_ACTION, OFPET_VPORT_MOD_FAILED, ovpm, ntohs (ovpm->header.length));
		free_vport_table_entry (vpe); // free allocated entry
		return EINVAL;
	}

	vpe->port_acts->actions_len = actions_len;
	memcpy (vpe->port_acts->actions, ovpm->actions, actions_len);

	int error = insert_vport_table_entry (&m_vportTable, vpe);
	if (error)
	{
		OF_SWITCH_DUMP ( cout << "could not insert port table entry for port " << vport << endl );
	}

	return error;
}

ofpbuf * OpenFlowSwitchSession::BufferFromPacket (ProtocolMessage* packet)
{
	OF_SWITCH_DUMP (cout << "Switch[" << inHost()->nhi.toString()<< "], OpenFlowSwitchSession::BufferFromPacket(), Creating Openflow buffer from packet." << endl);

	int mtu = GetMtu();

	/*
	 * Allocate buffer with some headroom to add headers in forwarding
	 * to the controller or adding a vlan tag, plus an extra 2 bytes to
	 * allow IP headers to be aligned on a 4-byte boundary.
	 */
	const int headroom = 128 + 2;
	const int hard_header = VLAN_ETH_HEADER_LEN;
	ofpbuf *buffer = ofpbuf_new (headroom + hard_header + mtu);
	buffer->data = (char*)buffer->data + headroom + hard_header;

	int l2_length = 0, l3_length = 0, l4_length = 0;

	/* layer 2 */
	//kevin: will do msg type check later
	SimpleMacMessage* l2_hdr = (SimpleMacMessage*)packet;
	buffer->l2 = new eth_header;
	eth_header* eth_h = (eth_header*)buffer->l2;
	l2_hdr->dst48->CopyTo (eth_h->eth_dst);              // Destination Mac Address
	l2_hdr->src48->CopyTo (eth_h->eth_src);              // Source Mac Address
	eth_h->eth_type = htons (ETH_TYPE_IP);    // Ether Type
	OF_SWITCH_DUMP ( cout << "Switch[" << inHost()->nhi.toString()<< "], Parsed EthernetHeader" << endl );
	l2_length = ETH_HEADER_LEN;

	/* layer 3 */
	IPMessage* l3_hdr = (IPMessage*)(l2_hdr->payload());
	buffer->l3 = new ip_header;
	ip_header* ip_h = (ip_header*)buffer->l3;
	ip_h->ip_ihl_ver  = IP_IHL_VER (5, IP_VERSION);       // Version
	ip_h->ip_tos      = l3_hdr->tos;                  // Type of Service/Differentiated Services
	ip_h->ip_tot_len  = l3_hdr->total_length;               // Total Length
	ip_h->ip_id       = l3_hdr->identification;       // Identification
	ip_h->ip_frag_off = l3_hdr->fragmentOffset;       // Fragment Offset
	ip_h->ip_ttl      = l3_hdr->time_to_live;                  // Time to Live
	ip_h->ip_proto    = l3_hdr->protocol_no;             // Protocol
	ip_h->ip_src      = htonl ((uint32_t)l3_hdr->src_ip); // Source Address
	ip_h->ip_dst      = htonl ((uint32_t)l3_hdr->dst_ip); // Destination Address
	ip_h->ip_csum     = csum (&ip_h, sizeof ip_h);        // Header Checksum
	OF_SWITCH_DUMP ( cout << "Switch[" << inHost()->nhi.toString()<< "], Parsed Ipv4Header" << endl );
	l3_length = IP_HEADER_LEN;

	/*
      // ARP Packet; the underlying OpenFlow header isn't used to match, so this is probably superfluous.
      ArpHeader arp_hd;
      if (packet->PeekHeader (arp_hd))
        {
          buffer->l3 = new arp_eth_header;
          arp_eth_header* arp_h = (arp_eth_header*)buffer->l3;
          arp_h->ar_hrd = ARP_HRD_ETHERNET;                             // Hardware type.
          arp_h->ar_pro = ARP_PRO_IP;                                   // Protocol type.
          arp_h->ar_op = arp_hd.m_type;                                 // Opcode.
          arp_hd.GetDestinationHardwareAddress ().CopyTo (arp_h->ar_tha); // Target hardware address.
          arp_hd.GetSourceHardwareAddress ().CopyTo (arp_h->ar_sha);    // Sender hardware address.
          arp_h->ar_tpa = arp_hd.GetDestinationIpv4Address ().Get ();   // Target protocol address.
          arp_h->ar_spa = arp_hd.GetSourceIpv4Address ().Get ();        // Sender protocol address.
          arp_h->ar_hln = sizeof arp_h->ar_tha;                         // Hardware address length.
          arp_h->ar_pln = sizeof arp_h->ar_tpa;                         // Protocol address length.
          OF_SWITCH_DUMP ( cout << "Parsed ArpHeader" << endl );

          l3_length = ARP_ETH_HEADER_LEN;
        }

	 */
	if(l3_hdr->protocol_no == S3FNET_PROTOCOL_TYPE_TCP)
	{
		TCPMessage* tcp_hd = (TCPMessage*)(l3_hdr->payload());
		buffer->l4 = new tcp_header;
		tcp_header* tcp_h = (tcp_header*)buffer->l4;
		tcp_h->tcp_src = htonl (tcp_hd->src_port);         // Source Port
		tcp_h->tcp_dst = htonl (tcp_hd->dst_port);    // Destination Port
		tcp_h->tcp_seq = tcp_hd->seqno; // Sequence Number
		tcp_h->tcp_ack = tcp_hd->ackno;      // ACK Number
		tcp_h->tcp_ctl = TCP_FLAGS (tcp_hd->flags);  // Data Offset + Reserved + Flags
		tcp_h->tcp_winsz = tcp_hd->wsize;       // Window Size
		tcp_h->tcp_urg = 0;      // Urgent Pointer (currently not set)
		tcp_h->tcp_csum = csum (&tcp_h, sizeof tcp_h);    // Header Checksum

		OF_SWITCH_DUMP ( cout << "Switch[" << inHost()->nhi.toString()<< "], Parsed TcpHeader" << endl );
		l4_length = TCP_HEADER_LEN;
	}
	else if (l3_hdr->protocol_no == S3FNET_PROTOCOL_TYPE_UDP)
	{
		UDPMessage* udp_hd = (UDPMessage*)(l3_hdr->payload());
		buffer->l4 = new udp_header;
		udp_header* udp_h = (udp_header*)buffer->l4;
		udp_h->udp_src = htonl (udp_hd->src_port);     // Source Port
		udp_h->udp_dst = htonl (udp_hd->dst_port); // Destination Port
		//udp_h->udp_len = htons (UDP_HEADER_LEN + udp_hd->payload()->packingSize());
		udp_h->udp_len = htons (UDP_HEADER_LEN);

		//if (ipopt->protocol_no == Ipv4L3Protocol::PROT_NUMBER)
		//{
		/*  ip_header* ip_h = (ip_header*)buffer->l3;
	  uint32_t udp_csum = csum_add32 (0, ip_h->ip_src);
	  udp_csum = csum_add32 (udp_csum, ip_h->ip_dst);
	  udp_csum = csum_add16 (udp_csum, IP_TYPE_UDP << 8);
	  udp_csum = csum_add16 (udp_csum, udp_h->udp_len);
	  udp_csum = csum_continue (udp_csum, udp_h, sizeof udp_h);
	  udp_h->udp_csum = csum_finish (csum_continue (udp_csum, buffer->data, buffer->size)); // Header Checksum
		 */
		//}
		//else
		//{
		udp_h->udp_csum = htons (0);
		//}

		OF_SWITCH_DUMP ( cout << "Switch[" << inHost()->nhi.toString()<< "], Parsed UdpHeader" << endl );
		l4_length = UDP_HEADER_LEN;
	}

	// Load Packet data into buffer data
	// as for now, payload is not inspected for rules, just add something here, will replace with payload data in the future
	//packet->CopyData ((uint8_t*)buffer->data, packet->GetSize ());

	if (buffer->l4)
	{
		/*  cout 	  << " udp_len = " << ((udp_header*)(buffer->l4))->udp_len
			  << " udp_src = " << udp_h->udp_src
			  << " udp_dst = " << ((udp_header*)(buffer->l4))->udp_dst
			  << " udp_csum = " << ((udp_header*)(buffer->l4))->udp_csum
			  << " l4_length = " <<  l4_length
			  << " udp_hd->src_port = " << udp_hd->src_port
			  << " udp_hd->dst_port = " << udp_hd->dst_port
			  << endl;*/

		ofpbuf_push (buffer, buffer->l4, l4_length);
		//ofpbuf_push (buffer, buffer->l4, 1);
		if(l3_hdr->protocol_no == S3FNET_PROTOCOL_TYPE_TCP)
			delete (tcp_header*)buffer->l4;
		else if(l3_hdr->protocol_no == S3FNET_PROTOCOL_TYPE_UDP)
			delete (udp_header*)buffer->l4;
	}
	if (buffer->l3)
	{
		ofpbuf_push (buffer, buffer->l3, l3_length);
		delete (ip_header*)buffer->l3;
	}
	if (buffer->l2)
	{
		ofpbuf_push (buffer, buffer->l2, l2_length);
		delete (eth_header*)buffer->l2;
	}

	return buffer;
}

int OpenFlowSwitchSession::OutputAll (uint32_t packet_uid, int in_port, bool flood)
{
	OF_SWITCH_DUMP( printf("Flooding over ports.\n") );

	int prev_port = -1;

	for (size_t i = 0; i < m_ports.size (); i++)
	{
		if (m_ports[i].netdev->id == (unsigned)in_port) // Originating port
		{
			continue;
		}

		if(m_ports[i].netdev->is_of_ctrl_iface == true) //do not send to the port connecting to an active controller
		{
			continue;
		}

		if (flood && m_ports[i].config & OFPPC_NO_FLOOD) // Port configured to not allow flooding
		{
			continue;
		}
		if (prev_port != -1)
		{
			OutputPort (packet_uid, in_port, prev_port, false);
		}
		prev_port = m_ports[i].netdev->id;
	}
	if (prev_port != -1)
	{
		OutputPort (packet_uid, in_port, prev_port, false);
	}

	return 0;
}

void OpenFlowSwitchSession::OutputPacket (uint32_t packet_uid, int out_port)
{
	if (out_port >= 0 && out_port < DP_MAX_PORTS)
	{
		Port& p = m_ports[out_port];
		if (p.netdev != 0 && !(p.config & OFPPC_PORT_DOWN))
		{
			SwitchPacketMetadata data = m_packetData.find (packet_uid)->second;
			size_t bufsize = data.buffer->size;
			OF_SWITCH_DUMP (cout << "Switch[" << inHost()->nhi.toString()<< "], Sending packet over port " << out_port << endl);

			//kevin: use clone first since flooding, suspect FlowTableLookup::m_packetData.erase (packet_uid) will not delete the packet. thus copy will cause memory leak
			Activation pkt(data.packet->clone());
			//send directly to the outgoing network interface (simplyPHY.push)
			p.netdev->getLowestProtocolSession()->pushdown(pkt, p.netdev->getHighestProtocolSession());
			p.tx_packets++;
			p.tx_bytes += bufsize;

			//p.tx_dropped++; //currently ignore drop status
			return;
		}
	}

	OF_SWITCH_DUMP (cout << "Switch[" << inHost()->nhi.toString()<< "] Can't forward to bad port " << out_port << endl);
}

void OpenFlowSwitchSession::OutputPort (uint32_t packet_uid, int in_port, int out_port, bool ignore_no_fwd)
{
	if (out_port == OFPP_FLOOD)
	{
		OutputAll (packet_uid, in_port, true);
	}
	else if (out_port == OFPP_ALL)
	{
		OutputAll (packet_uid, in_port, false);
	}
	else if (out_port == OFPP_CONTROLLER)
	{
		OutputControl (packet_uid, in_port, 0, OFPR_ACTION);
	}
	else if (out_port == OFPP_IN_PORT)
	{
		OutputPacket (packet_uid, in_port);
	}
	else if (out_port == OFPP_TABLE)
	{
		RunThroughFlowTable (packet_uid, in_port < DP_MAX_PORTS ? in_port : -1, false);
	}
	else if (out_port >= OFPP_VP_START && out_port <= OFPP_VP_END)
	{
		// port is a virtual port
		OF_SWITCH_DUMP (cout << "Switch[" << inHost()->nhi.toString()<< "], packet sent to virtual port " << out_port << endl );
		if (in_port < DP_MAX_PORTS)
		{
			RunThroughVPortTable (packet_uid, in_port, out_port);
		}
		else
		{
			RunThroughVPortTable (packet_uid, -1, out_port);
		}
	}
	else if (in_port == out_port)
	{
		OF_SWITCH_DUMP ( cout << "Switch[" << inHost()->nhi.toString()<< "], can't directly forward to input port" << endl );
	}
	else
	{
		OutputPacket (packet_uid, out_port);
	}
}

void* OpenFlowSwitchSession::MakeOpenflowReply (size_t openflow_len, uint8_t type, ofpbuf **bufferp)
{
	return make_openflow_xid (openflow_len, type, 0, bufferp);
}

int OpenFlowSwitchSession::SendOpenflowBuffer (ofpbuf *buffer)
{
	pthread_mutex_lock(&(inHost()->getTopNet()->of_buf_mutex));
	update_openflow_length (buffer);
	pthread_mutex_unlock(&(inHost()->getTopNet()->of_buf_mutex));

	if(is_ctrl_sess == false) //non-protocol-session controller
	{
		if(np_controller != 0) np_controller->ReceiveFromSwitch (this, buffer);
	}
	else // protocol session controller
	{
		if(ctrl_sess != 0)
		{
			ctrl_sess->ReceiveFromSwitch(this, buffer);
		}
	}
	return 0;
}

void OpenFlowSwitchSession::OutputControl (uint32_t packet_uid, int in_port, size_t max_len, int reason)
{
	OF_SWITCH_DUMP (cout << "Switch[" << inHost()->nhi.toString()<< "], Sending packet to controller" << endl );

	ofpbuf* buffer = m_packetData.find (packet_uid)->second.buffer;
	size_t total_len = buffer->size;
	if (packet_uid != std::numeric_limits<uint32_t>::max () && max_len != 0 && buffer->size > max_len)
	{
		buffer->size = max_len;
	}

	ofp_packet_in *opi = (ofp_packet_in*)ofpbuf_push_uninit (buffer, offsetof (ofp_packet_in, data));
	opi->header.version = OFP_VERSION;
	opi->header.type    = OFPT_PACKET_IN;
	opi->header.length  = htons (buffer->size);
	opi->header.xid     = htonl (0);
	opi->buffer_id      = htonl (packet_uid);
	opi->total_len      = htons (total_len);
	opi->in_port        = htons (in_port);
	opi->reason         = reason;
	opi->pad            = 0;

	if(is_ctrl_sess == false) //non-protocol-session controller
	{
		SendOpenflowBuffer (buffer);
	}
	else //non-protocol-session controller
	{
		pthread_mutex_lock(&(inHost()->getTopNet()->of_buf_mutex));
		update_openflow_length (buffer);
		pthread_mutex_unlock(&(inHost()->getTopNet()->of_buf_mutex));

		OpenFlowMessage* msg = new OpenFlowMessage(buffer, packet_uid, this);
		Activation pkt(msg);
		//send directly to the outgoing network interface connected with controller (simplyPHY.push)
		ofctrl_port->getLowestProtocolSession()->pushdown(pkt, ofctrl_port->getHighestProtocolSession());
	}
}

void OpenFlowSwitchSession::FillPortDesc (Port p, ofp_phy_port *desc)
{
	desc->port_no = htons (GetSwitchPortIndex (p));

	std::ostringstream nm;
	nm << "eth" << GetSwitchPortIndex (p);
	strncpy ((char *)desc->name, nm.str ().c_str (), sizeof desc->name);

	p.netdev->getMac48Addr()->CopyTo (desc->hw_addr);
	desc->config = htonl (p.config);
	desc->state = htonl (p.state);

	// TODO: This should probably be fixed eventually to specify different available features.
	desc->curr = 0; // htonl(netdev_get_features(p->netdev, NETDEV_FEAT_CURRENT));
	desc->supported = 0; // htonl(netdev_get_features(p->netdev, NETDEV_FEAT_SUPPORTED));
	desc->advertised = 0; // htonl(netdev_get_features(p->netdev, NETDEV_FEAT_ADVERTISED));
	desc->peer = 0; // htonl(netdev_get_features(p->netdev, NETDEV_FEAT_PEER));
}

void OpenFlowSwitchSession::SendFeaturesReply ()
{
	ofpbuf *buffer;
	ofp_switch_features *ofr = (ofp_switch_features*)MakeOpenflowReply (sizeof *ofr, OFPT_FEATURES_REPLY, &buffer);
	ofr->datapath_id  = htonll (m_id);
	ofr->n_tables     = m_chain->n_tables;
	ofr->n_buffers    = htonl (N_PKT_BUFFERS);
	ofr->capabilities = htonl (OFP_SUPPORTED_CAPABILITIES);
	ofr->actions      = htonl (OFP_SUPPORTED_ACTIONS);

	for (size_t i = 0; i < m_ports.size (); i++)
	{
		ofp_phy_port* opp = (ofp_phy_port*)ofpbuf_put_zeros (buffer, sizeof *opp);
		FillPortDesc (m_ports[i], opp);
	}

	SendOpenflowBuffer (buffer);
}

void OpenFlowSwitchSession::SendVPortTableFeatures ()
{
	ofpbuf *buffer;
	ofp_vport_table_features *ovtfr = (ofp_vport_table_features*)MakeOpenflowReply (sizeof *ovtfr, OFPT_VPORT_TABLE_FEATURES_REPLY, &buffer);
	ovtfr->actions = htonl (OFP_SUPPORTED_VPORT_TABLE_ACTIONS);
	ovtfr->max_vports = htonl (m_vportTable.max_vports);
	ovtfr->max_chain_depth = htons (-1); // support a chain depth of 2^16
	ovtfr->mixed_chaining = true;
	SendOpenflowBuffer (buffer);
}

int OpenFlowSwitchSession::UpdatePortStatus (Port& p)
{
	uint32_t orig_config = p.config;
	uint32_t orig_state = p.state;

	// Port is always enabled because the Net Device is always enabled.
	p.config &= ~OFPPC_PORT_DOWN;

	//assume link is always up first
	p.state &= ~OFPPS_LINK_DOWN;

	return ((orig_config != p.config) || (orig_state != p.state));
}

void OpenFlowSwitchSession::SendPortStatus (Port p, uint8_t status)
{
	ofpbuf *buffer;
	ofp_port_status *ops = (ofp_port_status*)MakeOpenflowReply (sizeof *ops, OFPT_PORT_STATUS, &buffer);
	ops->reason = status;
	memset (ops->pad, 0, sizeof ops->pad);
	FillPortDesc (p, &ops->desc);

	SendOpenflowBuffer (buffer);

	pthread_mutex_lock(&(inHost()->getTopNet()->of_buf_mutex));
	ofpbuf_delete (buffer);
	pthread_mutex_unlock(&(inHost()->getTopNet()->of_buf_mutex));
}

void OpenFlowSwitchSession::SendFlowExpired (sw_flow *flow, enum ofp_flow_expired_reason reason)
{
	ofpbuf *buffer;
	ofp_flow_expired *ofe = (ofp_flow_expired*)MakeOpenflowReply (sizeof *ofe, OFPT_FLOW_EXPIRED, &buffer);
	flow_fill_match (&ofe->match, &flow->key);

	ofe->priority = htons (flow->priority);
	ofe->reason = reason;
	memset (ofe->pad, 0, sizeof ofe->pad);

	ofe->duration     = htonl (inHost()->now() - flow->created);
	memset (ofe->pad2, 0, sizeof ofe->pad2);
	ofe->packet_count = htonll (flow->packet_count);
	ofe->byte_count   = htonll (flow->byte_count);
	SendOpenflowBuffer (buffer);
}

void OpenFlowSwitchSession::SendErrorMsg (uint16_t type, uint16_t code, const void *data, size_t len)
{
	ofpbuf *buffer;
	ofp_error_msg *oem = (ofp_error_msg*)MakeOpenflowReply (sizeof(*oem) + len, OFPT_ERROR, &buffer);
	oem->type = htons (type);
	oem->code = htons (code);
	memcpy (oem->data, data, len);
	SendOpenflowBuffer (buffer);
}

void OpenFlowSwitchSession::FlowTableLookup (sw_flow_key key, ofpbuf* buffer, uint32_t packet_uid, int port, bool send_to_controller)
{
	sw_flow *flow = chain_lookup (m_chain, &key);
	if (flow != 0)
	{
		/* kevin: if the rule is flooding, ask controller again, need to double verify */
		ofp_action_output* oa = (ofp_action_output*)(flow->sf_acts->actions);
		if(oa->port == OFPP_FLOOD)
		{
			OF_SWITCH_DUMP (cout << "Switch[" << inHost()->nhi.toString()<< "], Flow not matched." << endl);
			if (send_to_controller)
			{
				OutputControl (packet_uid, port, m_missSendLen, OFPR_NO_MATCH);
			}
		}
		else
		{
			OF_SWITCH_DUMP (cout << "Switch[" << inHost()->nhi.toString()<< "], Flow matched" << endl);
			flow_used (flow, buffer);
			ExecuteActions (this, packet_uid, buffer, &key, flow->sf_acts->actions, flow->sf_acts->actions_len, false);
		}
	}
	else
	{
		OF_SWITCH_DUMP (cout << "Switch[" << inHost()->nhi.toString()<< "], Flow not matched." << endl);

		if (send_to_controller)
		{
			OutputControl (packet_uid, port, m_missSendLen, OFPR_NO_MATCH);
		}
	}

	if(is_ctrl_sess == false)
	{
		// Clean up; at this point we're done with the packet.
		pthread_mutex_lock(&(inHost()->getTopNet()->of_buf_mutex));
		m_packetData.erase (packet_uid);
		discard_buffer (packet_uid); //kevin: todo: this line cause double free error for multi-timline, need to investigate
		ofpbuf_delete (buffer);
		pthread_mutex_unlock(&(inHost()->getTopNet()->of_buf_mutex));
	}
}

void OpenFlowSwitchSession::RunThroughFlowTable (uint32_t packet_uid, int port, bool send_to_controller)
{
	SwitchPacketMetadata data = m_packetData.find (packet_uid)->second;
	ofpbuf* buffer = data.buffer;

	sw_flow_key key;
	key.wildcards = 0; // Lookup cannot take wildcards.
	// Extract the matching key's flow data from the packet's headers; if the policy is to drop fragments and the message is a fragment, drop it.
	if (flow_extract (buffer, port != -1 ? port : OFPP_NONE, &key.flow) && (m_flags & OFPC_FRAG_MASK) == OFPC_FRAG_DROP)
	{
		pthread_mutex_lock(&(inHost()->getTopNet()->of_buf_mutex));
		ofpbuf_delete (buffer);
		pthread_mutex_unlock(&(inHost()->getTopNet()->of_buf_mutex));
		return;
	}

	// drop MPLS packets with TTL 1
	if (buffer->l2_5)
	{
		mpls_header mpls_h;
		mpls_h.value = ntohl (*((uint32_t*)buffer->l2_5));
		if (mpls_h.ttl == 1)
		{
			// increment mpls drop counter
			if (port != -1)
			{
				m_ports[port].mpls_ttl0_dropped++;
			}
			return;
		}
	}

	// If we received the packet on a port, and opted not to receive any messages from it...
	if (port != -1)
	{
		uint32_t config = m_ports[port].config;
		if ( ( config & (OFPPC_NO_RECV | OFPPC_NO_RECV_STP))
				&& (config & (!eth_addr_equals (key.flow.dl_dst, stp_eth_addr) ? OFPPC_NO_RECV : OFPPC_NO_RECV_STP)) )
		{
			return;
		}
	}

	OF_SWITCH_DUMP ( cout << "Switch[" << inHost()->nhi.toString()<< "], Matching against the flow table." << endl );

	//todo: directly call FlowTableLookup first, once we have the timing model for switch <-> controller, then we may change here
	FlowTableLookup (key, buffer, packet_uid, port, send_to_controller);
}

int OpenFlowSwitchSession::RunThroughVPortTable (uint32_t packet_uid, int port, uint32_t vport)
{
	ofpbuf* buffer = m_packetData.find (packet_uid)->second.buffer;

	// extract the flow again since we need it
	// and the layer pointers may changed
	sw_flow_key key;
	key.wildcards = 0;
	if (flow_extract (buffer, port != -1 ? port : OFPP_NONE, &key.flow)
			&& (m_flags & OFPC_FRAG_MASK) == OFPC_FRAG_DROP)
	{
		return 0;
	}

	// run through the chain of port table entries
	vport_table_entry *vpe = vport_table_lookup (&m_vportTable, vport);
	m_vportTable.lookup_count++;
	if (vpe)
	{
		m_vportTable.port_match_count++;
	}
	while (vpe != 0)
	{
		ExecuteVPortActions (this, packet_uid, m_packetData.find (packet_uid)->second.buffer, &key, vpe->port_acts->actions, vpe->port_acts->actions_len);
		vport_used (vpe, buffer); // update counters for virtual port
		if (vpe->parent_port_ptr == 0)
		{
			// if a port table's parent_port_ptr is 0 then
			// the parent_port should be a physical port
			if (vpe->parent_port <= OFPP_VP_START) // done traversing port chain, send packet to output port
			{
				OutputPort (packet_uid, port != -1 ? port : OFPP_NONE, vpe->parent_port, false);
			}
			else
			{
				error_quit("virtual port points to parent port\n");
			}
		}
		else // increment the number of port entries accessed by chaining
		{
			m_vportTable.chain_match_count++;
		}
		// move to the parent port entry
		vpe = vpe->parent_port_ptr;
	}

	return 0;
}

int OpenFlowSwitchSession::ReceiveFeaturesRequest (const void *msg)
{
	SendFeaturesReply ();
	return 0;
}

int OpenFlowSwitchSession::ReceiveVPortTableFeaturesRequest (const void *msg)
{
	SendVPortTableFeatures ();
	return 0;
}

int OpenFlowSwitchSession::ReceiveGetConfigRequest (const void *msg)
{
	ofpbuf *buffer;
	ofp_switch_config *osc = (ofp_switch_config*)MakeOpenflowReply (sizeof *osc, OFPT_GET_CONFIG_REPLY, &buffer);
	osc->flags = htons (m_flags);
	osc->miss_send_len = htons (m_missSendLen);

	return SendOpenflowBuffer (buffer);
}

int OpenFlowSwitchSession::ReceiveSetConfig (const void *msg)
{
	const ofp_switch_config *osc = (ofp_switch_config*)msg;

	int n_flags = ntohs (osc->flags) & (OFPC_SEND_FLOW_EXP | OFPC_FRAG_MASK);
	if ((n_flags & OFPC_FRAG_MASK) != OFPC_FRAG_NORMAL && (n_flags & OFPC_FRAG_MASK) != OFPC_FRAG_DROP)
	{
		n_flags = (n_flags & ~OFPC_FRAG_MASK) | OFPC_FRAG_DROP;
	}

	m_flags = n_flags;
	m_missSendLen = ntohs (osc->miss_send_len);
	return 0;
}

int OpenFlowSwitchSession::ReceivePacketOut (const void *msg)
{
	const ofp_packet_out *opo = (ofp_packet_out*)msg;
	ofpbuf *buffer;
	size_t actions_len = ntohs (opo->actions_len);

	if (actions_len > (ntohs (opo->header.length) - sizeof *opo))
	{
		OF_SWITCH_DUMP (cout << "Switch[" << inHost()->nhi.toString()<< "], message too short for number of actions" << endl);
		return -EINVAL;
	}

	if (ntohl (opo->buffer_id) == (uint32_t) -1)
	{
		// FIXME: can we avoid copying data here?
		int data_len = ntohs (opo->header.length) - sizeof *opo - actions_len;
		buffer = ofpbuf_new (data_len);
		ofpbuf_put (buffer, (uint8_t *)opo->actions + actions_len, data_len);
	}
	else
	{
		//pthread_mutex_lock(&(inHost()->getTopNet()->of_buf_mutex));
		buffer = retrieve_buffer (ntohl (opo->buffer_id));
		//pthread_mutex_unlock(&(inHost()->getTopNet()->of_buf_mutex));

		if (buffer == 0)
		{
			return -ESRCH;
		}
	}

	sw_flow_key key;
	flow_extract (buffer, opo->in_port, &key.flow); // ntohs(opo->in_port)

	uint16_t v_code = ValidateActions (&key, opo->actions, actions_len);
	if (v_code != ACT_VALIDATION_OK)
	{
		SendErrorMsg (OFPET_BAD_ACTION, v_code, msg, ntohs (opo->header.length));
		pthread_mutex_lock(&(inHost()->getTopNet()->of_buf_mutex));
		ofpbuf_delete (buffer);
		pthread_mutex_unlock(&(inHost()->getTopNet()->of_buf_mutex));
		return -EINVAL;
	}

	ExecuteActions (this, opo->buffer_id, buffer, &key, opo->actions, actions_len, true);
	return 0;
}

int OpenFlowSwitchSession::ReceivePortMod (const void *msg)
{
	ofp_port_mod* opm = (ofp_port_mod*)msg;

	int port = opm->port_no; // ntohs(opm->port_no);
	if (port < DP_MAX_PORTS)
	{
		Port& p = m_ports[port];

		// Make sure the port id hasn't changed since this was sent
		Mac48Address hw_addr = Mac48Address ();
		hw_addr.CopyFrom (opm->hw_addr);
		if (*(p.netdev->getMac48Addr()) != hw_addr)
		{
			OF_SWITCH_DUMP(cout << "Switch[" << inHost()->nhi.toString()<< "], OpenFlowSwitchSession::ReceivePortMod(), hw_addr != p.netdev->getMac48Addr()" << endl);
			return 0;
		}

		if (opm->mask)
		{
			uint32_t config_mask = ntohl (opm->mask);
			p.config &= ~config_mask;
			p.config |= ntohl (opm->config) & config_mask;
		}

		if (opm->mask & htonl (OFPPC_PORT_DOWN))
		{
			if ((opm->config & htonl (OFPPC_PORT_DOWN)) && (p.config & OFPPC_PORT_DOWN) == 0)
			{
				p.config |= OFPPC_PORT_DOWN;
				// TODO: Possibly disable the Port's Net Device via the appropriate interface.
			}
			else if ((opm->config & htonl (OFPPC_PORT_DOWN)) == 0 && (p.config & OFPPC_PORT_DOWN))
			{
				p.config &= ~OFPPC_PORT_DOWN;
				// TODO: Possibly enable the Port's Net Device via the appropriate interface.
			}
		}
	}

	return 0;
}

// add or remove a virtual port table entry
int OpenFlowSwitchSession::ReceiveVPortMod (const void *msg)
{
	const ofp_vport_mod *ovpm = (ofp_vport_mod*)msg;

	uint16_t command = ntohs (ovpm->command);
	if (command == OFPVP_ADD)
	{
		return AddVPort (ovpm);
	}
	else if (command == OFPVP_DELETE)
	{
		if (remove_vport_table_entry (&m_vportTable, ntohl (ovpm->vport)))
		{
			SendErrorMsg (OFPET_BAD_ACTION, OFPET_VPORT_MOD_FAILED, ovpm, ntohs (ovpm->header.length));
		}
	}

	return 0;
}

int OpenFlowSwitchSession::AddFlow (const ofp_flow_mod *ofm)
{
	size_t actions_len = ntohs (ofm->header.length) - sizeof *ofm;

	// Allocate memory.
	sw_flow *flow = flow_alloc (actions_len);
	if (flow == 0)
	{
		if (ntohl (ofm->buffer_id) != (uint32_t) -1)
		{
			pthread_mutex_lock(&(inHost()->getTopNet()->of_buf_mutex));
			discard_buffer (ntohl (ofm->buffer_id));
			pthread_mutex_unlock(&(inHost()->getTopNet()->of_buf_mutex));
		}
		return -ENOMEM;
	}

	flow_extract_match (&flow->key, &ofm->match);

	uint16_t v_code = ValidateActions (&flow->key, ofm->actions, actions_len);
	if (v_code != ACT_VALIDATION_OK)
	{
		SendErrorMsg (OFPET_BAD_ACTION, v_code, ofm, ntohs (ofm->header.length));
		flow_free (flow);
		if (ntohl (ofm->buffer_id) != (uint32_t) -1)
		{
			pthread_mutex_lock(&(inHost()->getTopNet()->of_buf_mutex));
			discard_buffer (ntohl (ofm->buffer_id));
			pthread_mutex_unlock(&(inHost()->getTopNet()->of_buf_mutex));
		}
		return -ENOMEM;
	}

	// Fill out flow.
	flow->priority = flow->key.wildcards ? ntohs (ofm->priority) : -1;
	flow->idle_timeout = ntohs (ofm->idle_timeout);
	flow->hard_timeout = ntohs (ofm->hard_timeout);
	flow->used = flow->created = (time_t)(inHost()->now());
	flow->sf_acts->actions_len = actions_len;
	flow->byte_count = 0;
	flow->packet_count = 0;
	memcpy (flow->sf_acts->actions, ofm->actions, actions_len);

	// Act.
	int error = chain_insert (m_chain, flow);
	if (error)
	{
		if (error == -ENOBUFS)
		{
			SendErrorMsg (OFPET_FLOW_MOD_FAILED, OFPFMFC_ALL_TABLES_FULL, ofm, ntohs (ofm->header.length));
		}
		flow_free (flow);
		if (ntohl (ofm->buffer_id) != (uint32_t) -1)
		{
			pthread_mutex_lock(&(inHost()->getTopNet()->of_buf_mutex));
			discard_buffer (ntohl (ofm->buffer_id));
			pthread_mutex_unlock(&(inHost()->getTopNet()->of_buf_mutex));
		}
		return error;
	}

	OF_SWITCH_DUMP(cout << "Switch[" << inHost()->nhi.toString()<< "], Added new flow." << endl);

	if (ntohl (ofm->buffer_id) != std::numeric_limits<uint32_t>::max ())
	{
		//pthread_mutex_lock(&(inHost()->getTopNet()->of_buf_mutex));
		ofpbuf *buffer = retrieve_buffer (ofm->buffer_id); // ntohl(ofm->buffer_id)
		//pthread_mutex_unlock(&(inHost()->getTopNet()->of_buf_mutex));

		if (buffer)
		{
			sw_flow_key key;
			flow_used (flow, buffer);
			flow_extract (buffer, ofm->match.in_port, &key.flow); // ntohs(ofm->match.in_port);
			ExecuteActions (this, ofm->buffer_id, buffer, &key, ofm->actions, actions_len, false);
			pthread_mutex_lock(&(inHost()->getTopNet()->of_buf_mutex));
			ofpbuf_delete (buffer);
			pthread_mutex_unlock(&(inHost()->getTopNet()->of_buf_mutex));
		}
		else
		{
			return -ESRCH;
		}
	}
	return 0;
}

int OpenFlowSwitchSession::ModFlow (const ofp_flow_mod *ofm)
{
	sw_flow_key key;
	flow_extract_match (&key, &ofm->match);

	size_t actions_len = ntohs (ofm->header.length) - sizeof *ofm;

	uint16_t v_code = ValidateActions (&key, ofm->actions, actions_len);

	OF_SWITCH_DUMP( cout << "Switch[" << inHost()->nhi.toString()<< "], Modify a flow." << endl );

	if (v_code != ACT_VALIDATION_OK)
	{
		SendErrorMsg ((ofp_error_type)OFPET_BAD_ACTION, v_code, ofm, ntohs (ofm->header.length));
		if (ntohl (ofm->buffer_id) != (uint32_t) -1)
		{
			pthread_mutex_lock(&(inHost()->getTopNet()->of_buf_mutex));
			discard_buffer (ntohl (ofm->buffer_id));
			pthread_mutex_unlock(&(inHost()->getTopNet()->of_buf_mutex));
		}
		return -ENOMEM;
	}

	uint16_t priority = key.wildcards ? ntohs (ofm->priority) : -1;
	int strict = (ofm->command == htons (OFPFC_MODIFY_STRICT)) ? 1 : 0;
	chain_modify (m_chain, &key, priority, strict, ofm->actions, actions_len);

	if (ntohl (ofm->buffer_id) != std::numeric_limits<uint32_t>::max ())
	{
		//pthread_mutex_lock(&(inHost()->getTopNet()->of_buf_mutex));
		ofpbuf *buffer = retrieve_buffer (ofm->buffer_id); // ntohl (ofm->buffer_id)
		//pthread_mutex_unlock(&(inHost()->getTopNet()->of_buf_mutex));

		if (buffer)
		{
			sw_flow_key skb_key;
			flow_extract (buffer, ofm->match.in_port, &skb_key.flow); // ntohs(ofm->match.in_port);
			ExecuteActions (this, ofm->buffer_id, buffer, &skb_key, ofm->actions, actions_len, false);

			pthread_mutex_lock(&(inHost()->getTopNet()->of_buf_mutex));
			ofpbuf_delete (buffer);
			pthread_mutex_unlock(&(inHost()->getTopNet()->of_buf_mutex));
		}
		else
		{
			return -ESRCH;
		}
	}
	return 0;
}

int OpenFlowSwitchSession::ReceiveFlow (const void *msg)
{
	const ofp_flow_mod *ofm = (ofp_flow_mod*)msg;
	uint16_t command = ntohs (ofm->command);

	if (command == OFPFC_ADD)
	{
		return AddFlow (ofm);
	}
	else if ((command == OFPFC_MODIFY) || (command == OFPFC_MODIFY_STRICT))
	{
		return ModFlow (ofm);
	}
	else if (command == OFPFC_DELETE)
	{
		sw_flow_key key;
		flow_extract_match (&key, &ofm->match);
		return chain_delete (m_chain, &key, ofm->out_port, 0, 0) ? 0 : -ESRCH;
	}
	else if (command == OFPFC_DELETE_STRICT)
	{
		sw_flow_key key;
		uint16_t priority;
		flow_extract_match (&key, &ofm->match);
		priority = key.wildcards ? ntohs (ofm->priority) : -1;
		return chain_delete (m_chain, &key, ofm->out_port, priority, 1) ? 0 : -ESRCH;
	}
	else
	{
		return -ENODEV;
	}
}

int OpenFlowSwitchSession::StatsDump (StatsDumpCallback *cb)
{
	ofp_stats_reply *osr;
	ofpbuf *buffer;
	int err;

	if (cb->done)
	{
		return 0;
	}

	osr = (ofp_stats_reply*)MakeOpenflowReply (sizeof *osr, OFPT_STATS_REPLY, &buffer);
	osr->type = htons (cb->s->type);
	osr->flags = 0;

	err = cb->s->DoDump (this, cb->state, buffer);
	if (err >= 0)
	{
		if (err == 0)
		{
			cb->done = true;
		}
		else
		{
			// Buffer might have been reallocated, so find our data again.
			osr = (ofp_stats_reply*)ofpbuf_at_assert (buffer, 0, sizeof *osr);
			osr->flags = ntohs (OFPSF_REPLY_MORE);
		}

		int err2 = SendOpenflowBuffer (buffer);
		if (err2)
		{
			err = err2;
		}
	}

	return err;
}

void OpenFlowSwitchSession::StatsDone (StatsDumpCallback *cb)
{
	if (cb)
	{
		cb->s->DoCleanup (cb->state);
		free (cb->s);
		free (cb);
	}
}

int OpenFlowSwitchSession::ReceiveStatsRequest (const void *oh)
{
	const ofp_stats_request *rq = (ofp_stats_request*)oh;
	size_t rq_len = ntohs (rq->header.length);
	int type = ntohs (rq->type);
	int body_len = rq_len - offsetof (ofp_stats_request, body);
	Stats* st = new Stats ((ofp_stats_types)type, (unsigned)body_len);

	if (st == 0)
	{
		return -EINVAL;
	}

	StatsDumpCallback cb;
	cb.done = false;
	cb.rq = (ofp_stats_request*)xmemdup (rq, rq_len);
	cb.s = st;
	cb.state = 0;
	cb.swtch = this;

	if (cb.s)
	{
		int err = cb.s->DoInit (rq->body, body_len, &cb.state);
		if (err)
		{
			OF_SWITCH_DUMP ( cout << "Switch[" << inHost()->nhi.toString()<< "], failed initialization of stats request type " << type << ": " << strerror (-err) << endl );
			free (cb.rq);
			return err;
		}
	}

	if(is_ctrl_sess == false)
	{
		if(np_controller != 0) np_controller->StartDump (&cb);
		else error_quit ("Switch needs to be registered to a passive controller in order to start the stats reply.");
	}
	else
	{
		if(ctrl_sess != 0) ctrl_sess->StartDump(&cb);
		else error_quit ("Switch needs to be registered to an active controller in order to start the stats reply.");
	}

	return 0;
}

int OpenFlowSwitchSession::ReceiveEchoRequest (const void *oh)
{
	return SendOpenflowBuffer (make_echo_reply ((ofp_header*)oh));
}

int OpenFlowSwitchSession::ReceiveEchoReply (const void *oh)
{
	return 0;
}

void OpenFlowSwitchSession::HandleControlInput (Activation pkt)
{
	OpenFlowMessage* msg = (OpenFlowMessage*)pkt;

	ForwardControlInput(msg->ofm, msg->length);

	//claim buffer and message
	if(msg->packet_uid != -1) //direct inquiry response
	{
		pthread_mutex_lock(&(inHost()->getTopNet()->of_buf_mutex));
		m_packetData.erase(msg->packet_uid);
		discard_buffer(msg->packet_uid);
		ofpbuf_delete(msg->buffer);
		msg->erase();
		pthread_mutex_unlock(&(inHost()->getTopNet()->of_buf_mutex));
	}
	else //other response, e.g. learned flow in learning switch
	{
		msg->erase();
	}
}


int OpenFlowSwitchSession::ForwardControlInput (const void *msg, size_t length)
{
	// Check encapsulated length.
	ofp_header *oh = (ofp_header*) msg;
	if (ntohs (oh->length) > length)
	{
		return -EINVAL;
	}
	assert (oh->version == OFP_VERSION);

	int error = 0;

	// Figure out how to handle it.
	switch (oh->type)
	{
	case OFPT_FEATURES_REQUEST:
		error = length < sizeof(ofp_header) ? -EFAULT : ReceiveFeaturesRequest (msg);
		break;
	case OFPT_GET_CONFIG_REQUEST:
		error = length < sizeof(ofp_header) ? -EFAULT : ReceiveGetConfigRequest (msg);
		break;
	case OFPT_SET_CONFIG:
		error = length < sizeof(ofp_switch_config) ? -EFAULT : ReceiveSetConfig (msg);
		break;
	case OFPT_PACKET_OUT:
		error = length < sizeof(ofp_packet_out) ? -EFAULT : ReceivePacketOut (msg);
		break;
	case OFPT_FLOW_MOD:
		error = length < sizeof(ofp_flow_mod) ? -EFAULT : ReceiveFlow (msg);
		break;
	case OFPT_PORT_MOD:
		error = length < sizeof(ofp_port_mod) ? -EFAULT : ReceivePortMod (msg);
		break;
	case OFPT_STATS_REQUEST:
		error = length < sizeof(ofp_stats_request) ? -EFAULT : ReceiveStatsRequest (msg);
		break;
	case OFPT_ECHO_REQUEST:
		error = length < sizeof(ofp_header) ? -EFAULT : ReceiveEchoRequest (msg);
		break;
	case OFPT_ECHO_REPLY:
		error = length < sizeof(ofp_header) ? -EFAULT : ReceiveEchoReply (msg);
		break;
	case OFPT_VPORT_MOD:
		error = length < sizeof(ofp_vport_mod) ? -EFAULT : ReceiveVPortMod (msg);
		break;
	case OFPT_VPORT_TABLE_FEATURES_REQUEST:
		error = length < sizeof(ofp_header) ? -EFAULT : ReceiveVPortTableFeaturesRequest (msg);
		break;
	default:
		SendErrorMsg ((ofp_error_type)OFPET_BAD_REQUEST, (ofp_bad_request_code)OFPBRC_BAD_TYPE, msg, length);
		error = -EINVAL;
		break;
	}

	if (msg != 0)
	{
		free ((ofpbuf*)msg);
	}
	return error;
}

sw_chain* OpenFlowSwitchSession::GetChain ()
{
	return m_chain;
}

uint32_t OpenFlowSwitchSession::GetNSwitchPorts (void) const
{
	return m_ports.size ();
}

Port OpenFlowSwitchSession::GetSwitchPort (uint32_t n) const
{
	return m_ports[n];
}

int OpenFlowSwitchSession::GetSwitchPortIndex (Port p)
{
	for (size_t i = 0; i < m_ports.size (); i++)
	{
		if (m_ports[i].netdev == p.netdev)
		{
			//return i;
			return m_ports[i].netdev->id;
		}
	}
	return -1;
}

vport_table_t OpenFlowSwitchSession::GetVPortTable ()
{
	return m_vportTable;
}

}; // namespace s3fnet
}; // namespace s3f
