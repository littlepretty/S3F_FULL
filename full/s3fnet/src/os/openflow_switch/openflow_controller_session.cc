/**
 * \file openflow_controller_session.cc
 * \brief Source file for the OpenFlowControllerSession class and all its child classes (e.g., LearningControllerSession).
 *
 * authors : Dong (Kevin) Jin
 */

#include "openflow_controller_session.h"
#include "openflow_message.h"
#include "util/errhandle.h" // defines error_quit() method
#include "net/host.h"
#include "os/ipv4/ip_interface.h" // defines IPPushOption and IPOptionToAbove classes
#include "os/base/protocols.h" // defines S3FNET_REGISTER_PROTOCOL macro
#include "net/network_interface.h"

//#define OPENFLOW_CONTROLLER_SESSION_DEBUG

#ifdef OPENFLOW_CONTROLLER_SESSION_DEBUG
#define OF_CTRL_SESS_DUMP(x) printf("OF_CTRL_SESS: "); x
#else
#define OF_CTRL_SESS_DUMP(x)
#endif

namespace s3f {
namespace s3fnet {

S3FNET_REGISTER_PROTOCOL(OpenFlowControllerSession, OF_CONTROLLER_PROTOCOL_CLASSNAME);
S3FNET_REGISTER_PROTOCOL(LearningControllerSession, OF_CONTROLLER_LEARNING_SWITCH_PROTOCOL_CLASSNAME);

OpenFlowControllerSession::OpenFlowControllerSession(ProtocolGraph* graph) : ProtocolSession(graph)
{
	OF_CTRL_SESS_DUMP(printf("An openflow controller protocol session is created.\n"));
}

OpenFlowControllerSession::~OpenFlowControllerSession ()
{
	// reclaim your session-related variables here
	OF_CTRL_SESS_DUMP(printf("An openflow controller protocol session is reclaimed.\n"));
	m_switches.clear ();
}

void OpenFlowControllerSession::config(s3f::dml::Configuration* cfg)
{
	// the same method at the parent class must be called
	ProtocolSession::config(cfg);
}

void OpenFlowControllerSession::init()
{
	// the same method at the parent class must be called
	ProtocolSession::init();

	// initialize the session-related variables here
	OF_CTRL_SESS_DUMP(printf("OpenFlow switch session is initialized.\n"));

	inHost()->is_of_ctrl = true;
	inHost()->of_ctrl_sess = this;
}

int OpenFlowControllerSession::control(int ctrltyp, void* ctrlmsg, ProtocolSession* sess)
{
	switch(ctrltyp)
	{
		case OPENFLOW_CONTROLLER_CTRL_COMMAND1:
		case OPENFLOW_CONTROLLER_CTRL_COMMAND2:
			return 0; // openflow controller control commands, not in use yet
		default:
			return ProtocolSession::control(ctrltyp, ctrlmsg, sess);
	}
}

int OpenFlowControllerSession::push(Activation msg, ProtocolSession* hi_sess, void* extinfo, std::size_t extinfo_size)
{
	error_quit("ERROR: a message is pushed down to the openflow controller session from protocol layer above; it's impossible for now.\n");
	return 0;
}

int OpenFlowControllerSession::pop(Activation msg, ProtocolSession* lo_sess, void* extinfo, size_t extinfo_size)
{
	OF_CTRL_SESS_DUMP(printf("A message is popped up to the openflow controller session.\n"));

	IPOptionToAbove* ipopt = (IPOptionToAbove*)extinfo;
	NetworkInterface* netdev = inHost()->getNetworkInterface(ipopt->nw_iface);
	if(netdev == NULL) error_quit("OpenFlowSwitchSession::pop(), not able to find network interface");

	ProtocolMessage* packet = (ProtocolMessage*)msg;

	return 0;
}

void OpenFlowControllerSession::AddSwitch (OpenFlowSwitchSession* swtch)
{
	if (m_switches.find (swtch) != m_switches.end ())
	{
		error_quit ("This Controller has already registered this switch!");
	}
	else
	{
		m_switches.insert (swtch);
	}
}

void OpenFlowControllerSession::SendToSwitch (OpenFlowSwitchSession* swtch, void * msg, size_t length)
{
	swtch->ForwardControlInput (msg, length);
}

ofp_flow_mod* OpenFlowControllerSession::BuildFlow (sw_flow_key key, uint32_t buffer_id, uint16_t command, void* acts, size_t actions_len, int idle_timeout, int hard_timeout)
{
	ofp_flow_mod* ofm = (ofp_flow_mod*)malloc (sizeof(ofp_flow_mod) + actions_len);
	ofm->header.version = OFP_VERSION;
	ofm->header.type = OFPT_FLOW_MOD;
	ofm->header.length = htons (sizeof(ofp_flow_mod) + actions_len);
	ofm->command = htons (command);
	ofm->idle_timeout = htons (idle_timeout);
	ofm->hard_timeout = htons (hard_timeout);
	ofm->buffer_id = htonl (buffer_id);
	ofm->priority = OFP_DEFAULT_PRIORITY;
	memcpy (ofm->actions,acts,actions_len);

	ofm->match.wildcards = key.wildcards;                                 // Wildcard fields
	ofm->match.in_port = key.flow.in_port;                                // Input switch port
	memcpy (ofm->match.dl_src, key.flow.dl_src, sizeof ofm->match.dl_src); // Ethernet source address.
	memcpy (ofm->match.dl_dst, key.flow.dl_dst, sizeof ofm->match.dl_dst); // Ethernet destination address.
	ofm->match.dl_vlan = key.flow.dl_vlan;                                // Input VLAN OFP_VLAN_NONE;
	ofm->match.dl_type = key.flow.dl_type;                                // Ethernet frame type ETH_TYPE_IP;
	ofm->match.nw_proto = key.flow.nw_proto;                              // IP Protocol
	ofm->match.nw_src = key.flow.nw_src;                                  // IP source address
	ofm->match.nw_dst = key.flow.nw_dst;                                  // IP destination address
	ofm->match.tp_src = key.flow.tp_src;                                  // TCP/UDP source port
	ofm->match.tp_dst = key.flow.tp_dst;                                  // TCP/UDP destination port
	ofm->match.mpls_label1 = key.flow.mpls_label1;                        // Top of label stack htonl(MPLS_INVALID_LABEL);
	ofm->match.mpls_label2 = key.flow.mpls_label1;                        // Second label (if available) htonl(MPLS_INVALID_LABEL);

	return ofm;
}

uint8_t OpenFlowControllerSession::GetPacketType (ofpbuf* buffer)
{
	ofp_header* hdr = (ofp_header*)ofpbuf_try_pull (buffer, sizeof (ofp_header));
	uint8_t type = hdr->type;
	ofpbuf_push_uninit (buffer, sizeof (ofp_header));
	return type;
}

void OpenFlowControllerSession::StartDump (StatsDumpCallback* cb)
{
	if (cb != 0)
	{
		int error = 1;
		while (error > 0) // Switch's StatsDump returns 1 if the reply isn't complete.
		{
			error = cb->swtch->StatsDump (cb);
		}

		if (error != 0) // When the reply is complete, error will equal zero if there's no errors.
		{
			OF_CTRL_SESS_DUMP (cout << "Dump Callback Error: " << strerror (-error) << endl );
		}

		// Clean up
		cb->swtch->StatsDone (cb);
	}
}

LearningControllerSession::LearningControllerSession(ProtocolGraph* graph) : OpenFlowControllerSession(graph)
{
	OF_CTRL_SESS_DUMP(printf("An openflow learning switch controller protocol session is created.\n"));
	type = OF_LEARNING_SWITCH_CONTROLLER_ENTITY;
	m_expirationTime = 0;
}

LearningControllerSession::~LearningControllerSession ()
{
	Tables::iterator iter;
	for (iter = tables.begin() ; iter != tables.end(); iter++ )
	{
		(*iter).second.clear();
	}
	tables.clear();
}

void LearningControllerSession::setFlowExpirationTime(ltime_t time)
{
	m_expirationTime = time;
}

ltime_t LearningControllerSession::getFlowExpirationTime()
{
	return m_expirationTime;
}

void LearningControllerSession::ReceiveFromSwitch (OpenFlowSwitchSession* swtch, ofpbuf* buffer)
{
	if (m_switches.find (swtch) == m_switches.end ())
	{
		OF_CTRL_SESS_DUMP (printf("Can't receive from this switch, not registered to the Controller."));
		return;
	}

	// We have received any packet at this point, so we pull the header to figure out what type of packet we're handling.
	uint8_t type = GetPacketType (buffer);

	if (type == OFPT_PACKET_IN) // The switch didn't understand the packet it received, so it forwarded it to the controller.
	{
		ofp_packet_in * opi = (ofp_packet_in*)ofpbuf_try_pull (buffer, offsetof (ofp_packet_in, data));
		int port = ntohs (opi->in_port);

		// Create matching key.
		sw_flow_key key;
		key.wildcards = 0;

		flow_extract (buffer, port != -1 ? port : OFPP_NONE, &key.flow);

		uint16_t out_port = OFPP_FLOOD;
		uint16_t in_port = ntohs (key.flow.in_port);

		//locate the corresponding learning table of this switch
		LearningTable* lt; //learning table for this switch
		Tables::iterator iter1 = tables.find (swtch);
		if(iter1 != tables.end())
		{
			lt = &(*iter1).second;
		}
		else
		{
			error_quit("LearningControllerSession::ReceiveFromSwitch(), the switch is not registered with the controller.");
		}

		// If the destination address is learned to a specific port, find it.
		Mac48Address dst_addr;
		dst_addr.CopyFrom (key.flow.dl_dst);
		if (!dst_addr.IsBroadcast ())
		{
			LearningTable::iterator iter2 = lt->find(dst_addr);
			if(iter2 != lt->end())
			{
				out_port = iter2->second.port;
				OF_CTRL_SESS_DUMP (cout << "Switch[" << swtch->inHost()->nhi.toString()<< "], Find the out port " << out_port << " for dst_addr " << dst_addr << endl);
			}
			else
			{
				OF_CTRL_SESS_DUMP (cout << "Switch[" << swtch->inHost()->nhi.toString()<< "], Setting to flood; don't know yet what port " << dst_addr << " is connected to" << endl);
			}
		}
		else
		{
			OF_CTRL_SESS_DUMP ( printf("Setting to flood; this packet is a broadcast\n") );
		}

		// Create output-to-port action
		ofp_action_output x[1];
		x[0].type = htons (OFPAT_OUTPUT);
		x[0].len = htons (sizeof(ofp_action_output));
		x[0].port = out_port;

		// Create a new flow that outputs matched packets to a learned port, OFPP_FLOOD if there's no learned port.
		ofp_flow_mod* ofm = BuildFlow (key, opi->buffer_id, OFPFC_ADD, x, sizeof(x), OFP_FLOW_PERMANENT, m_expirationTime == 0 ? OFP_FLOW_PERMANENT : m_expirationTime);
		SendToSwitch (swtch, ofm, ofm->header.length);

		// We can learn a specific port for the source address for future use.
		Mac48Address src_addr;
		src_addr.CopyFrom (key.flow.dl_src);
		LearningTable::iterator iter3 = lt->find (src_addr);
		if (iter3 == lt->end ()) // We haven't learned our source MAC yet.
		{
			LearnedState ls;
			ls.port = in_port;
			lt->insert (S3FNET_MAKE_PAIR(src_addr, ls));
			OF_CTRL_SESS_DUMP ( cout << "Switch[" << swtch->inHost()->nhi.toString()<< "], Learned that " << src_addr << " can be found over port " << in_port << endl );

			// Learn src_addr goes to a certain port.
			ofp_action_output x2[1];
			x2[0].type = htons (OFPAT_OUTPUT);
			x2[0].len = htons (sizeof(ofp_action_output));
			x2[0].port = in_port;

			// Switch MAC Addresses and ports to the flow we're modifying
			src_addr.CopyTo (key.flow.dl_dst);
			dst_addr.CopyTo (key.flow.dl_src);
			key.flow.in_port = out_port;

			// Switch IP Address and TCP/UDP ports as well, since wildcards is 0
			uint32_t nw_tmp = key.flow.nw_src;
			key.flow.nw_src = key.flow.nw_dst;
			key.flow.nw_dst = nw_tmp;
			uint16_t tp_tmp = key.flow.tp_src;
			key.flow.tp_src = key.flow.tp_dst;
			key.flow.tp_dst = tp_tmp;

			ofp_flow_mod* ofm2 = BuildFlow (key, -1, OFPFC_MODIFY, x2, sizeof(x2), OFP_FLOW_PERMANENT, m_expirationTime == 0 ? OFP_FLOW_PERMANENT : m_expirationTime);
			SendToSwitch (swtch, ofm2, ofm2->header.length);
		}
	}
}

void LearningControllerSession::HandleInquiryMessageFromOFSwitch(Activation pkt, NetworkInterface* incoming_iface)
{
	OpenFlowMessage* msg = (OpenFlowMessage*)pkt;
	ofpbuf* buffer = msg->buffer;
	OpenFlowSwitchSession* swtch = msg->sender;

	// We have received any packet at this point, so we pull the header to figure out what type of packet we're handling.
	uint8_t type = GetPacketType (buffer);

	if (type == OFPT_PACKET_IN) // The switch didn't understand the packet it received, so it forwarded it to the controller.
	{
		ofp_packet_in * opi = (ofp_packet_in*)ofpbuf_try_pull (buffer, offsetof (ofp_packet_in, data));
		int port = ntohs (opi->in_port);

		// Create matching key.
		sw_flow_key key;
		key.wildcards = 0;

		flow_extract (buffer, port != -1 ? port : OFPP_NONE, &key.flow);

		uint16_t out_port = OFPP_FLOOD;
		uint16_t in_port = ntohs (key.flow.in_port);

		//locate the corresponding learning table of this switch
		LearningTable* lt; //learning table for this switch
		Tables::iterator iter1 = tables.find (swtch);
		if(iter1 != tables.end())
		{
			lt = &(*iter1).second;
		}
		else
		{
			error_quit("LearningControllerSession::HandleInquiryMessageFromOFSwitch(), the switch is not registered with the controller.");
		}

		// If the destination address is learned to a specific port, find it.
		Mac48Address dst_addr;
		dst_addr.CopyFrom (key.flow.dl_dst);
		if (!dst_addr.IsBroadcast ())
		{
			LearningTable::iterator iter2 = lt->find(dst_addr);
			if(iter2 != lt->end())
			{
				out_port = iter2->second.port;
				OF_CTRL_SESS_DUMP (cout << "Switch[" << swtch->inHost()->nhi.toString()<< "], Find the out port " << out_port << " for dst_addr " << dst_addr << endl);
			}
			else
			{
				OF_CTRL_SESS_DUMP (cout << "Switch[" << swtch->inHost()->nhi.toString()<< "], Setting to flood; don't know yet what port " << dst_addr << " is connected to" << endl);
			}
		}
		else
		{
			OF_CTRL_SESS_DUMP ( printf("Setting to flood; this packet is a broadcast\n") );
		}

		// Create output-to-port action
		ofp_action_output x[1];
		x[0].type = htons (OFPAT_OUTPUT);
		x[0].len = htons (sizeof(ofp_action_output));
		x[0].port = out_port;

		// Create a new flow that outputs matched packets to a learned port, OFPP_FLOOD if there's no learned port.
		ofp_flow_mod* ofm = BuildFlow (key, opi->buffer_id, OFPFC_ADD, x, sizeof(x), OFP_FLOW_PERMANENT, m_expirationTime == 0 ? OFP_FLOW_PERMANENT : m_expirationTime);

		// reuse the OpenFlowMessage and send back to switch
		msg->length = ofm->header.length;
		msg->ofm = ofm;
		//send the packet to the incoming network interface (i.e. connected with the corresponding switch) through simplyPHY.push
		incoming_iface->getLowestProtocolSession()->pushdown(pkt, incoming_iface->getHighestProtocolSession());


		// We can learn a specific port for the source address for future use.
		Mac48Address src_addr;
		src_addr.CopyFrom (key.flow.dl_src);
		LearningTable::iterator iter3 = lt->find (src_addr);
		if (iter3 == lt->end ()) // We haven't learned our source MAC yet.
		{
			LearnedState ls;
			ls.port = in_port;
			lt->insert (S3FNET_MAKE_PAIR(src_addr, ls));
			OF_CTRL_SESS_DUMP ( cout << "Switch[" << swtch->inHost()->nhi.toString()<< "], Learned that " << src_addr << " can be found over port " << in_port << endl );

			// Learn src_addr goes to a certain port.
			ofp_action_output x2[1];
			x2[0].type = htons (OFPAT_OUTPUT);
			x2[0].len = htons (sizeof(ofp_action_output));
			x2[0].port = in_port;

			// Switch MAC Addresses and ports to the flow we're modifying
			src_addr.CopyTo (key.flow.dl_dst);
			dst_addr.CopyTo (key.flow.dl_src);
			key.flow.in_port = out_port;

			// Switch IP Address and TCP/UDP ports as well, since wildcards is 0
			uint32_t nw_tmp = key.flow.nw_src;
			key.flow.nw_src = key.flow.nw_dst;
			key.flow.nw_dst = nw_tmp;
			uint16_t tp_tmp = key.flow.tp_src;
			key.flow.tp_src = key.flow.tp_dst;
			key.flow.tp_dst = tp_tmp;

			ofp_flow_mod* ofm2 = BuildFlow (key, -1, OFPFC_MODIFY, x2, sizeof(x2), OFP_FLOW_PERMANENT, m_expirationTime == 0 ? OFP_FLOW_PERMANENT : m_expirationTime);
			OpenFlowMessage* msg2 = new OpenFlowMessage(ofm2, ofm2->header.length);
			Activation pkt2(msg2);
			//send the packet (learned flow) to the incoming network interface (i.e. connected with the corresponding switch) through simplyPHY.push
			incoming_iface->getLowestProtocolSession()->pushdown(pkt2, incoming_iface->getHighestProtocolSession());
		}
	}
}

void LearningControllerSession::AddSwitch (OpenFlowSwitchSession* swtch)
{
	OpenFlowControllerSession::AddSwitch(swtch);
	tables.insert( S3FNET_MAKE_PAIR(swtch, LearningTable()) );
}

}; // namespace s3fnet
}; // namespace s3f
