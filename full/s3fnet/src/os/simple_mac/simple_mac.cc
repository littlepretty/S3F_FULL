/**
 * \file simple_mac.cc
 * \brief Source file for the SimpleMac class.
 *
 * authors : Dong (Kevin) Jin
 */

#include "os/simple_mac/simple_mac.h"
#include "util/errhandle.h"
#include "net/network_interface.h"
#include "net/mac48_address.h"
#include "os/base/protocols.h"
#include "net/forwardingtable.h"
#include "os/ipv4/ip_message.h"
#include "os/simple_mac/simple_mac_message.h"
#include "net/mac48_address.h"
#include "net/host.h"
#include "env/namesvc.h"

namespace s3f {
namespace s3fnet {

#ifdef SMAC_DEBUG
#define SMAC_DUMP(x) printf("SMAC: "); x
#else
#define SMAC_DUMP(x)
#endif

S3FNET_REGISTER_PROTOCOL(SimpleMac, SIMPLE_MAC_PROTOCOL_CLASSNAME);

SimpleMac::SimpleMac(ProtocolGraph* graph) : NicProtocolSession(graph)
{
	SMAC_DUMP(printf("[nic=\"%s\"] new simple_mac session.\n", ((NetworkInterface*)inGraph())->nhi.toString()));
}

SimpleMac::~SimpleMac() 
{
	SMAC_DUMP(printf("[nic=\"%s\"] delete simple_mac session.\n", ((NetworkInterface*)inGraph())->nhi.toString()));
}

void SimpleMac::init()
{  
	SMAC_DUMP(printf("[nic=\"%s\", ip=\"%s\"] init().\n",
			((NetworkInterface*)inGraph())->nhi.toString(),
			IPPrefix::ip2txt(getNetworkInterface()->getIP())));
	NicProtocolSession::init();

	if(strcasecmp(name, MAC_PROTOCOL_NAME))
		error_quit("ERROR: SimpleMac::init(), unmatched protocol name: \"%s\", expecting \"MAC\".\n", name);
}

int SimpleMac::push(Activation msg, ProtocolSession* hi_sess, void* extinfo, size_t extinfo_size)
{
	SMAC_DUMP(printf("[nic=\"%s\", ip=\"%s\"] %s: push().\n",
			((NetworkInterface*)inGraph())->nhi.toString(),
			IPPrefix::ip2txt(getNetworkInterface()->getIP()), getNowWithThousandSeparator()));

	Mac48Address* src_mac;
	Mac48Address* dst_mac;

	src_mac = inHost()->getTopNet()->getNameService()->ip2mac48(((IPMessage*)msg)->src_ip);

	if(((IPMessage*)msg)->dst_ip == IPADDR_ANYDEST) // handle broadcast case, set destination MAC to broadcast as well
	{
		dst_mac = new Mac48Address ("ff:ff:ff:ff:ff:ff");
	}
	else //non-broadcast case
	{
		dst_mac = inHost()->getTopNet()->getNameService()->ip2mac48(((IPMessage*)msg)->dst_ip);
	}

	SMAC_DUMP(cout << "push() [nic=\"" << ((NetworkInterface*)inGraph())->nhi.toString()
			<< "\", ip=\"" << IPPrefix::ip2txt(getNetworkInterface()->getIP())
	<< "\"] src48_mac = " << src_mac
	<< ", dst48_mac = " << dst_mac << endl;);

	//Generate the MAC Header and put it in front of the IP Message
	SimpleMacMessage* simple_mac_header = new SimpleMacMessage(src_mac, dst_mac);
	simple_mac_header->carryPayload((IPMessage*)msg);
	Activation simp_mac_hdr(simple_mac_header);

	if(!child_prot)
		error_quit("ERROR: SimpleMac::push(), child protocol session has not been set.\n");

	return child_prot->pushdown(simp_mac_hdr, this);
}

int SimpleMac::pop(Activation msg, ProtocolSession* lo_sess, void* extinfo, size_t extinfo_size)
{  
	SMAC_DUMP(printf("[nic=\"%s\", ip=\"%s\"] %s: pop().\n",
			((NetworkInterface*)inGraph())->nhi.toString(),
			IPPrefix::ip2txt(getNetworkInterface()->getIP()), getNowWithThousandSeparator()));

	PHYOptionToAbove* phyopt = (PHYOptionToAbove*)extinfo;
	SimpleMacMessage* mac_hdr = (SimpleMacMessage*)msg;

	SMAC_DUMP(cout << "[nic=\"" << ((NetworkInterface*)inGraph())->nhi.toString()
			<< "\", ip=\"" << IPPrefix::ip2txt(getNetworkInterface()->getIP())
	<< "\"] src48_mac = " << mac_hdr->src48
	<< ", dst48_mac = " << mac_hdr->dst48 << endl;);

	// popup packet when it is for myself or the node is a router/openflow-switch/openVZEmu-host or promiscuous mode enabled
	// currently the checking mac_hdr->dst48->IsBroadcast(), is for handle real ARP packet: a non-openVZemu host should not be the APR destination
	bool to_popup = false;
	if( mac_hdr->dst48->IsBroadcast() || getNetworkInterface()->getMac48Addr()->IsEqual(mac_hdr->dst48)
			|| inHost()->isOpenFlowSwitch() == true || inHost()->isOpenFlowSwitch() == true
			|| inHost()->isRouter() == true || getNetworkInterface()->isPromiscuousMode() == true)
	{
		to_popup = true;
	}

	if(to_popup == false)
	{
		msg->erase_all();
		return 0;
	}

	if(!parent_prot)
	{
		error_quit("ERROR: SimpleMac::pop(), parent protocol session has not been set.\n");
	}

	/* pop up the entire packet (including the MAC header) to the openflow switch layer (bypass the IP layer) */
	if(inHost()->isOpenFlowSwitch() == true)
	{
		ProtocolSession* prot = inHost()->sessionForNumber(S3FNET_PROTOCOL_TYPE_OPENFLOW_SWITCH);
		if(prot)
		{
			MACOptionToAbove macupopt;
			macupopt.nw_iface = phyopt->nw_iface;
			IPMessage* ip_message = (IPMessage*)(((ProtocolMessage*)msg)->payload());
			macupopt.protocol_no = ip_message->protocol_no;
			return prot->popup(msg, this, (void*)&macupopt, sizeof(MACOptionToAbove));
		}
		else
		{
			error_quit("SimpleMac::pop, isOpenFlowSwitch is true, but no S3FNET_PROTOCOL_TYPE_OPENFLOW_SWITCH is found.");
		}
	}
	else
	{
		//send to upper layer, currently it is the IP layer
		ProtocolMessage* payload = mac_hdr->dropPayload();
		assert(payload);
		Activation ip_msg (payload);
		mac_hdr->erase(); //delete MAC header

		return parent_prot->popup(ip_msg, this);
	}
}

}; // namespace s3fnet
}; // namespace s3f
