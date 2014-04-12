/**
 * \file network_interface.cc
 *
 * \brief Source file for the NetworkInterface class.
 *
 * authors : Dong (Kevin) Jin
 */

#include "net/network_interface.h"
#include "os/base/protocols.h"
#include "util/errhandle.h"
#include "net/net.h"
#include "net/host.h"
#include "os/simple_phy/simple_phy.h"
#include "os/simple_mac/simple_mac.h"
#include "net/link.h"
#include "os/simple_mac/simple_mac_message.h"
#include "os/ipv4/ip_message.h"
#include "os/openVZEmu/openVZEmu_message.h"
#include "os/base/protocol_message.h"
#include "env/namesvc.h"
#include "os/openflow_switch/openflow_switch_session.h"
#include "os/openflow_switch/openflow_controller_session.h"

namespace s3f {
namespace s3fnet {

#define DEFAULT_PHY_CLASSNAME SIMPLE_PHY_PROTOCOL_CLASSNAME
#define DEFAULT_MAC_CLASSNAME SIMPLE_MAC_PROTOCOL_CLASSNAME

#ifdef IFACE_DEBUG
#define IFACE_DUMP(x) printf("NW_IFACE: "); x
#else
#define IFACE_DUMP(x)
#endif

NetworkInterface::NetworkInterface(Host* parent, long nicid) :
  DmlObject(parent, nicid), attached_link(0), mac_sess(0), phy_sess(0),
  is_of_ctrl_iface(false), of_switch_sess(NULL), is_promiscuous(false)
{
  assert(myParent); // the parent is the host

  IFACE_DUMP(printf("[id=%ld] new network interface.\n", nicid));

  /* init mac48_addr */
  mac48_addr = Mac48Address::Allocate();
}

NetworkInterface::~NetworkInterface()
{
  IFACE_DUMP(printf("[nhi=\"%s\", ip=\"%s\"] delete network interface.\n",
		    nhi.toString(), IPPrefix::ip2txt(ip_addr)));
}

void NetworkInterface::config(s3f::dml::Configuration* cfg)
{
  // settle the nhi address of this interface
  nhi = myParent->nhi;
  nhi += id;
  nhi.type = Nhi::NHI_INTERFACE;

  // settle the ip address and register the interface to the community
  S3FNET_STRING nhi_str = nhi.toStlString();

  Net* owner_net = getHost()->inNet();
  ip_addr = owner_net->getNameService()->nhi2ip(nhi_str);

#ifdef OPENVZ_EMULATION
  //set ip address for the corresponding VE if the host is an emulation node
  if(getHost()->isOpenVZEmu() == true)
  {
	  int idbase = owner_net->getTopNet()->get_idbase();
	  int veid = getHost()->getVEID();
	  SimCtrl* simCtrl = owner_net->getTopNet()->getSimCtrl();
	  char ip_buf[20];
	  int return_code = simCtrl->set_ve_ifip(idbase+veid, id, IPPrefix::ip2txt(ip_addr, ip_buf));
	  if(return_code != 0)
	  {
		  error_quit("NetworkInterface::config(), simCtrl->set_ve_ifip() error, return code = %s\n", SimCtrl::errcode2txt(return_code));
	  }
  }
#endif

  owner_net->register_interface(this, ip_addr, getHost()->isOpenVZEmu());
  IFACE_DUMP(cout << "NIC = " << nhi_str.c_str()
		  << ", IP = " << IPPrefix::ip2txt(ip_addr)
  	  	  << ", MAC = " << mac48_addr
  	  	  << ", IsOpenVZEmu = " << getHost()->isOpenVZEmu()
  	  	  << ", iface_id = " << id << endl;);

  // configure the protocol sessions (this will skip the mac/phy
  // session config unless they are specified explicitly in dml)
  ProtocolGraph::config(cfg);

  char* str = (char*)cfg->findSingle("of_ctrl_iface");
  if(str)
  {
    if(s3f::dml::dmlConfig::isConf(str))
      error_quit("ERROR: invalid of_ctrl_iface attribute.\n");
    if(!strcasecmp(str, "true")) is_of_ctrl_iface = true;
    else is_of_ctrl_iface = false;
  }
  else is_of_ctrl_iface = false;
  
  str = (char*)cfg->findSingle("promiscuous_mode");
  if(str)
  {
    if(s3f::dml::dmlConfig::isConf(str))
      error_quit("ERROR: invalid promiscuous_mode attribute.\n");
    if(!strcasecmp(str, "true")) is_promiscuous = true;
    else is_promiscuous = false;
  }
  else is_promiscuous = false;

  // if mac protocol is not specified, create the default mac protocol
  mac_sess = (NicProtocolSession*)sessionForName(MAC_PROTOCOL_NAME);
  if(!mac_sess)
  {
    mac_sess = (NicProtocolSession*)Protocols::newInstance(DEFAULT_MAC_CLASSNAME, this);
    if(!mac_sess)
    	error_quit("ERROR: unregistered protocol \"%s\".\n", DEFAULT_MAC_CLASSNAME);

    // configure the mac protocol session using the interface dml attributes
    mac_sess->config(cfg);
    mac_sess->name = sstrdup(MAC_PROTOCOL_NAME);
    mac_sess->use  = sstrdup(DEFAULT_MAC_CLASSNAME);
    insert_session(mac_sess);
  }
  IFACE_DUMP(printf("[nhi=\"%s\", ip=\"%s\"] config(): mac session \"%s\".\n",
		    nhi_str.c_str(), IPPrefix::ip2txt(ip_addr), mac_sess->use));

  // if phy protocol is not specified, create the default phy protocol
  phy_sess = (LowestProtocolSession*)sessionForName(PHY_PROTOCOL_NAME);
  if(!phy_sess)
  {
    phy_sess = (LowestProtocolSession*)Protocols::newInstance(DEFAULT_PHY_CLASSNAME, this);
    if(!phy_sess)
    	error_quit("ERROR: unregistered protocol \"%s\".\n", DEFAULT_PHY_CLASSNAME);

    // configure the phy protocol session using the interface dml attributes
    phy_sess->config(cfg);
    phy_sess->name = sstrdup(PHY_PROTOCOL_NAME);
    phy_sess->use  = sstrdup(DEFAULT_PHY_CLASSNAME);
    insert_session(phy_sess);
  }

  bool is_lowest;
  if(phy_sess->control(PSESS_CTRL_SESSION_IS_LOWEST, &is_lowest, 0) || !is_lowest)
    error_quit("ERROR: PHY session \"%s\" is not the lowest protocol session.\n", phy_sess->use);
  IFACE_DUMP(printf("[nhi=\"%s\", ip=\"%s\"] config(): phy session \"%s\".\n",
		    nhi_str.c_str(), IPPrefix::ip2txt(ip_addr), phy_sess->use));

  ProtocolSession* ip_sess = getHost()->getNetworkLayerProtocol();

  mac_sess->control(PSESS_CTRL_SESSION_SET_CHILD, 0, phy_sess);
  mac_sess->control(PSESS_CTRL_SESSION_SET_PARENT, 0, ip_sess);
  phy_sess->control(PSESS_CTRL_SESSION_SET_PARENT, 0, mac_sess);

  //create InChannel
  stringstream ss;
  ss << "IC-" << nhi.toStlString();
  ic = new InChannel( (Host *)this->myParent, ss.str());

#ifdef OPENVZ_EMULATION
  if(getHost()->isOpenVZEmu() == true)
	  ic->set_ve_attached(true);
#endif
}

void NetworkInterface::init()
{
  IFACE_DUMP(printf("[nhi=\"%s\", ip=\"%s\"] init().\n", nhi.toString(), IPPrefix::ip2txt(ip_addr)));

  ProtocolGraph::init();
  if(is_of_ctrl_iface == true)
  {
    of_switch_sess = (OpenFlowSwitchSession*)(getHost()->sessionForNumber(S3FNET_PROTOCOL_TYPE_OPENFLOW_SWITCH));
    if(!of_switch_sess)
        error_quit("NetworkInterface::init(), no OpenFlowSwitchSession is found.");
    of_switch_sess->ofctrl_port = this;
  }
}

void NetworkInterface::sendPacket(Activation pkt, ltime_t delay)
{
  IFACE_DUMP(printf("[nhi=\"%s\", ip=\"%s\"] %s: send packet with delay %ld.\n",
		    nhi.toString(), IPPrefix::ip2txt(ip_addr), getHost()->getNowWithThousandSeparator(), delay));

  unsigned int pri = getHost()->tie_breaking_seed;

  /** The delay in the argument is transmission delay + queueing delay + jitter + iface_latency (i.e. oc per-write-delay)
   *  Adjust this delay before outChannel write by removing the min_packet_transfer_delay portion (i.e. mini pkt/max_bandwidth)
   *  Link prop_delay will be added (in s3f).
   *  Actually, both min_transfer_delay and prop_delay will be added when calling outchannel.write().
   */

  ltime_t link_min_delay = getHost()->d2t(attached_link->min_delay, 0);
  if(delay - link_min_delay < 0)
  {
	  printf("Warning: packet delay (%ld) < link's min packet transfer delay (%ld), "
			  "consider re-assign the min_packet_transfer_delay in dml\n",
			  delay, link_min_delay);
	  delay = 0;
  }
  else
  {
	  delay = delay - link_min_delay;
  }

  IFACE_DUMP(printf("NetworkInterface::sendPacket, link_min_delay = %ld, "
		  "delay to write to outChannel = %ld, pri = %u\n", link_min_delay, delay, pri));

  oc->write(pkt, delay, pri);
}

void NetworkInterface::receivePacket(Activation pkt)
{
	if (phy_sess->getRandom()->Uniform(0, 1) < attached_link->get_rdrop_rate()) //drop the receiving packets
	{
		IFACE_DUMP(printf("[nhi=\"%s\", ip=\"%s\"] %s: receive a packet, but dropped.\n",
				nhi.toString(), IPPrefix::ip2txt(ip_addr), getHost()->getNowWithThousandSeparator()));
		pkt->erase_all();
		return;
	}
	else
	{
		IFACE_DUMP(printf("[nhi=\"%s\", ip=\"%s\"] %s: receive a packet.\n",
				nhi.toString(), IPPrefix::ip2txt(ip_addr), getHost()->getNowWithThousandSeparator()));
	}

	//if the host is an openflow controller, handle the message (should be switch->controller message) directly
	if(getHost()->is_of_ctrl == true)
	{
		handle_openflow_message_to_controller(pkt);
		return;
	}
	
	// handle the controller-to-switch message
	if(is_of_ctrl_iface == true && getHost()->isOpenFlowSwitch() == true)
	{
		ProtocolMessage* msg = (ProtocolMessage*)pkt;
		if(msg->type() != S3FNET_PROTOCOL_TYPE_OPENFLOW_CONTROLLER)
			error_quit("NetworkInterface::receivePacket(), an OpenFlow switch received a packet on its controller interface, "
					"but the packet type is not S3FNET_PROTOCOL_TYPE_OPENFLOW_CONTROLLER.");

		assert(of_switch_sess);
		of_switch_sess->HandleControlInput(pkt);
		return;
	}
	
#ifdef OPENVZ_EMULATION
	if(getHost()->isOpenVZEmu() == true)
	{
		emu_packet_modify_iface(pkt);
	}
#endif

	// we pass the packet directly to the phy session
	phy_sess->receivePacket(pkt, id);
}

void NetworkInterface::handle_openflow_message_to_controller(Activation pkt)
{
	ProtocolMessage* msg = (ProtocolMessage*)pkt;
	assert(msg!=0);
	if(msg->type() != S3FNET_PROTOCOL_TYPE_OPENFLOW_CONTROLLER)
	{
		error_quit("OpenFlowController received a non-control message.");
	}

	OpenFlowControllerSession* of_ctrl = getHost()->of_ctrl_sess;
	assert(of_ctrl);
	switch(of_ctrl->type)
	{
		case OF_LEARNING_SWITCH_CONTROLLER_ENTITY:
			((LearningControllerSession*)of_ctrl)->HandleInquiryMessageFromOFSwitch(pkt, this);
			break;

		case NO_OF_CONTROLLER:
			error_quit("NetworkInterface::handle_openflow_message_to_controller(), no controller entity.");
			break;

		default:
			error_quit("NetworkInterface::handle_openflow_message_to_controller(), wrong controller entity type.");
			break;
	}
}

void NetworkInterface::emu_packet_modify_iface(Activation pkt)
{
	ProtocolMessage* msg = (ProtocolMessage*)pkt;

	while(msg!=0)
	{
		if(msg->type() == S3FNET_PROTOCOL_TYPE_OPENVZ_EVENT)
		{
			((OpenVZMessage*)msg)->ifid = id;
			break;
		}
		msg = msg->payload();
	}
}

void NetworkInterface::attach_to_link(Link* link)
{
  // we should check that an interface is attached to but one link;
  // however, the pre-processing step should have already done that
  if(attached_link)
    error_quit("ERROR: NetworkInterface::attach_to_link(), the interface cannot be attached to multiple links.\n");
  attached_link = link;
}

Host* NetworkInterface::getHost()
{
  return dynamic_cast<Host*>(myParent);
}

void NetworkInterface::display(int indent)
{
  printf("%*cNetworkInterface %lu", indent, ' ', id);
}

}; // namespace s3fnet
}; // namespace s3f
