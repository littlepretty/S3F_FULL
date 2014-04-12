/**
 * \file ip_session.cc
 * \brief Source file for the IPSession class.
 *
 * authors : Dong (Kevin) Jin
 */

#include "os/ipv4/ip_session.h"
#include "net/forwardingtable.h"
#include "util/errhandle.h"
#include "net/host.h"
#include "os/base/protocols.h"
#include "net/network_interface.h"
#include "os/ipv4/ip_message.h"
#include "os/ipv4/ip_interface.h"
#include "os/openVZEmu/openVZEmu_event_session.h"
#include "os/openVZEmu/openVZEmu_message.h"

#ifdef IP_DEBUG
#define IP_DUMP(x) printf("IP: "); x
#else
#define IP_DUMP(x)
#endif

namespace s3f {
namespace s3fnet {

S3FNET_REGISTER_PROTOCOL(IPSession, IP_PROTOCOL_CLASSNAME);

IPSession::IPSession(ProtocolGraph* graph) : 
  ProtocolSession(graph),
  forwarding_table(0), show_drop(false),
  route_vec(0), dml_route_vec(0), listener_vec(0),
  pop_intercept_sess(0)
{
  IP_DUMP(printf("[host=\"%s\"] new ip session.\n", inHost()->nhi.toString()));
}

IPSession::~IPSession()
{
  IP_DUMP(printf("delete ip session.\n"));

  if(route_vec)
  {
    for(unsigned i=0; i<route_vec->size(); i++)
      delete (*route_vec)[i];
    delete route_vec;
  }
  if(dml_route_vec)
  {
    for(unsigned i=0; i<dml_route_vec->size(); i++)
      delete (*dml_route_vec)[i];
    delete dml_route_vec;
  }
  if(listener_vec) delete listener_vec;

  if(forwarding_table) delete forwarding_table;
}

void IPSession::config(s3f::dml::Configuration *cfg)
{   
  IP_DUMP(printf("[host=\"%s\"] config().\n", inHost()->nhi.toString()));
  ProtocolSession::config(cfg);

  char* str = (char*) cfg->findSingle("show_drop");
  if(str)
  {
    if(s3f::dml::dmlConfig::isConf(str))
      error_quit("ERROR: IPSession::config(), invalid SHOW_DROP attribute.\n");
    if(!strcasecmp(str, "true")) show_drop = true;
    else if(!strcasecmp(str, "false")) show_drop = false;
    else error_quit("ERROR: IPSession::config(), invalid SHOW_DROP: %s.\n", str);
  }
}

void IPSession::init()
{ 
    IP_DUMP(printf("[host=\"%s\"] init().\n", inHost()->nhi.toString()));
    ProtocolSession::init();

    if(strcasecmp(name, NET_PROTOCOL_NAME) && strcasecmp(name, IP_PROTOCOL_NAME))
      error_quit("ERROR: IpSession::init(), unmatched protocol name: \"%s\", expecting \"NET\" or \"IP\".\n", name);

    // create a forwarding table for this machine
    if(!forwarding_table)
      forwarding_table = new ForwardingTable(this);

    // we need to add those routes specified in DML file.
    if(route_vec)
    {
      int size = route_vec->size();
      for(int i = 0; i < size; i++)
      {
    	RouteInfo* route = (*route_vec)[i]->resolve(inHost());
	    if(route)
	    {
	      int ret = forwarding_table->addRoute(route);
	      if(ret != FT_ROUTE_SUCCESS)
	      {
			fprintf(stderr, "IPSession::init(), duplicate route to \"%s\" on host nhi=\"%s\".\n",
		    IPPrefix::ip2txt(route->destination.addr), inHost()->nhi.toString());
	      }
	    }
      }

      // finally, remove the vector.
      delete route_vec;
      route_vec = 0;
    }

    if(dml_route_vec)
    {
      int size = dml_route_vec->size();
      for(int i = 0; i < size; i++)
      {
	    // route type is static by default (ML)
	    RouteInfo* route = (*dml_route_vec)[i]->resolve(inHost());
	    if(route)
	    {
	      int ret = forwarding_table->addRoute(route);
	      if(ret != FT_ROUTE_SUCCESS)
	      {
			fprintf(stderr, "IPSession::init(), duplicate route to \"%s\" on host nhi=\"%s\".\n",
		    IPPrefix::ip2txt(route->destination.addr), inHost()->nhi.toString());
	      }
	    }
      }
      
      // finally, remove the vector.
      for(int i = 0; i < size; i++) delete (*dml_route_vec)[i];
      delete dml_route_vec;
      dml_route_vec = 0;
    }

    if(listener_vec)
    {
      int size = listener_vec->size();
      for(int i = 0; i < size; i++)
      {
	    ProtocolSession* sess = (*listener_vec)[i];
	    forwarding_table->addListener(sess);
      }
      delete listener_vec;
      listener_vec = 0;
    }
}

int IPSession::control(int ctrltyp, void* ctrlmsg, ProtocolSession* sess)
{
  IP_DUMP(printf("[host=\"%s\"] control(): type=%d.\n", inHost()->nhi.toString(), ctrltyp));

  switch(ctrltyp) {
  case IP_CTRL_VERIFY_TARGET_IP_ADDRESS: {
    // ctrlmsg should be a pointer to a 32-bit destination address.
    assert(ctrlmsg);
    return verify_target_ip_address(*((uint32*)ctrlmsg));
  }

  case IP_CTRL_VERIFY_LOCAL_IP_ADDRESS: {
    // ctrlmsg should be a pointer to a 32-bit destination address.
    assert(ctrlmsg);
    return verify_local_ip_address(*((uint32*)ctrlmsg));
  }

  case IP_CTRL_GET_FORWARDING_TABLE: {
    // ctrlmsg should be a pointer to a pointer to a forwarding table.
    assert(ctrlmsg);
    *((ForwardingTable**)ctrlmsg) = forwarding_table;
    return 0;
  }
    
  case IP_CTRL_LOAD_FORWARDING_TABLE: {
    assert(ctrlmsg);
    load_forwarding_table((s3f::dml::Configuration*)ctrlmsg);
    return 0;
  }

  case IP_CTRL_INSERT_ROUTE: {
    // ctrlmsg should be a pointer to a RouteInfo object.
    assert(ctrlmsg);
    RouteInfo* route = (RouteInfo*)ctrlmsg;
    if(forwarding_table)
    {
      if(route->resolve(inHost()))
      { // if resolved, it'll return itself
    	int ret = forwarding_table->addRoute(route);
    	if(ret != FT_ROUTE_SUCCESS)
    	{
		  fprintf(stderr, "IPSession::control(IP_CTRL_INSERT_ROUTE), duplicate route to \"%s\".\n",
		  IPPrefix::ip2txt(route->destination.addr));
	    }
      }
    }
    else
    {
      if(!route_vec) route_vec = new S3FNET_VECTOR(RouteInfo*);
      route_vec->push_back(route);
    }
    return 0;
  }

  case IP_CTRL_DELETE_ROUTE: {
    // ctrlmsg should be a pointer to a prefix.
    assert(ctrlmsg);
    RouteInfo* route = (RouteInfo*)ctrlmsg;
    if(forwarding_table)
    {
      if(route->resolve(inHost()))
      { // if resolved, it'll return itself
	    int ret = forwarding_table->removeRoute(route);
	    if(ret == FT_ROUTE_NOT_FOUND)
	    {
	      fprintf(stderr, "IPSession::control(IP_CTRL_DELETE_ROUTE), delete route to \"%s\" not found.\n",
		  IPPrefix::ip2txt(route->destination.addr));
	    }
      }
    }
    else
    {
      fprintf(stderr, "IPSession::control(IP_CTRL_DELETE_ROUTE), delete route to \"%s\" from empty table.\n",
	          IPPrefix::ip2txt(route->destination.addr));
    }
    return 0;
  }

  case IP_CTRL_INSERT_DMLROUTE: {
    // ctrlmsg should be a pointer to a DMLRouteInfo object.
    assert(ctrlmsg);
    DMLRouteInfo* dml_route = (DMLRouteInfo*)ctrlmsg;
    if(forwarding_table)
    {
      RouteInfo* route = dml_route->resolve(inHost());
      if(route)
      {
	    int ret = forwarding_table->addRoute(route);
	    if(ret != FT_ROUTE_SUCCESS)
	    {
		  fprintf(stderr, "IPSession::control(IP_CTRL_INSERT_DMLROUTE), duplicate route to \"%s\".\n",
		  IPPrefix::ip2txt(route->destination.addr));
	    }
      }
      delete dml_route;
    }
    else
    {
      if(!dml_route_vec) dml_route_vec = new S3FNET_VECTOR(DMLRouteInfo*);
      dml_route_vec->push_back(dml_route);
    }
    return 0;
  }

  case IP_CTRL_ADD_INTERCEPT_SESSION: {
    if(pop_intercept_sess)
    {
      error_quit("ERROR: IPSession::control(), duplicate intercept session; only one allowed.\n");
    }
    else pop_intercept_sess = sess;
    return 0;
  }

  case IP_CTRL_PUSH_REQUEST: {
    IPPushRequest* req = (IPPushRequest*)ctrlmsg;
    assert(req && req->options && req->msg);
    return pushdown(req->msg, 0, req->options, sizeof(IPPushOption)); 
  }

  case IP_CTRL_ADD_FIB_LISTENER: {
    if(forwarding_table) forwarding_table->addListener(sess);
    else
    {
      if(!listener_vec) listener_vec = new S3FNET_VECTOR(ProtocolSession*);
      listener_vec->push_back(sess);
    }
    return 0;
  }

  default:
    return ProtocolSession::control(ctrltyp, ctrlmsg, sess); 
  } 
}

int IPSession::push(Activation msg, ProtocolSession* hi_sess, void* extinfo, size_t extinfo_size)
{
	assert(msg);
	assert(extinfo_size == sizeof(IPPushOption));
	IPPushOption* pushopt = (IPPushOption*)extinfo;

	// handle the case that the packet is sent to itself
	if(verify_local_ip_address(pushopt->dst_ip))
	{
		if(hi_sess)
		{
			// if the protocol is found on local stack
			IP_DUMP(printf("[host=\"%s\"] %s: send itself a packet for protocol %u.\n",
					inHost()->nhi.toString(), getNowWithThousandSeparator(), pushopt->prot_id));

			// extended information for ip address information
			IPOptionToAbove ipupopt;
			ipupopt.src_ip = pushopt->src_ip;
			ipupopt.dst_ip = pushopt->dst_ip;
			ipupopt.ttl = DEFAULT_IP_TIMETOLIVE;

			// if the upper layer has not set the source ip address, reset
			// it to the same as the destination.
			if(IPADDR_INADDR_ANY == ipupopt.src_ip) ipupopt.src_ip = ipupopt.dst_ip;

			hi_sess->popup(msg, this, (void*)&ipupopt, sizeof(IPOptionToAbove));

			return IPPUSHRET_TO_LOCAL_DONE;
		}
		else
		{
			// no such protocol, drop the packet directly (no need for icmp)
			if(show_drop)
			{
				printf("[host=\"%s\"] %s: drop a local packet of missing protocol %u.\n",
						inHost()->nhi.toString(), getNowWithThousandSeparator(), pushopt->prot_id);
			}

			msg->erase_all();

			return IPPUSHRET_TO_LOCAL_FAIL;
		}
	} // end of the send-to-self case

	if(pushopt->ttl == 0) //Reach TTL limit
	{
		if(show_drop)
		{
			printf("[host=\"%s\"] %s: drop packet due to ttl.\n", inHost()->nhi.toString(), getNowWithThousandSeparator());
		}

		msg->erase_all();
		return IPPUSHRET_DROPPED_TTL_LIMIT;
	}

	// figure out the outgoing interface in case it's not specified
	NetworkInterface* outgoing_nic = 0;
	void* route_info = 0;

	if(inHost()->isOpenVZEmu() == true) // outgoing_nic is obtained from the interface id info in EmuPacket
	{
		//default iface_id is 0
		outgoing_nic = inHost()->getNetworkInterface(pushopt->iface_id);
	}
	else
	{
		assert(forwarding_table);
		RouteInfo* pRoute = forwarding_table->getRoute(pushopt->dst_ip);
		if(0 == pRoute)
		{
			if(show_drop)
			{
				printf("[host=\"%s\"] %s: no route to \"%s\", dropping Packet.\n",
						inHost()->nhi.toString(), getNowWithThousandSeparator(), IPPrefix::ip2txt(pushopt->dst_ip));
			}

			msg->erase_all();
			return IPPUSHRET_NO_ROUTE;
		}

		outgoing_nic = pRoute->nic;
		route_info = (void*)pRoute;
	}
	assert(outgoing_nic);

	/* Figure out the source ip address and destination ip address.
	 * If the src ip address designated by the upper layer is INADDR_ANY,
	 * it will be changed as the network interface's ip address.*/
	IPADDR src_ip;
	if(IPADDR_INADDR_ANY == pushopt->src_ip) src_ip = outgoing_nic->getIP();
	else src_ip = pushopt->src_ip;

	// create ip_msg (ip header and upper layer msg as payload)
	// note: IP header is not needed for pure emulation case (i.e., every host/switch/router entity is attached to an openVZ-based VE), but it is needed for mixed sim/emu case
	IPMessage* ip_message = new IPMessage(src_ip, pushopt->dst_ip, pushopt->prot_id, pushopt->ttl);
	ip_message->carryPayload((ProtocolMessage*)msg);
	Activation ip_msg (ip_message);

	// send the packet to the outgoing_mac
	ProtocolSession* outgoing_mac = outgoing_nic->getHighestProtocolSession();
	if(inHost()->isOpenVZEmu() == true)
	{

		IP_DUMP(char s1[50]; char s2[50];
		printf("IPSession::push(), [host=\"%s\"] %s: send packet from \"%s\" [interface %d] to \"%s\" (%d real bytes) .\n",
				inHost()->nhi.toString(), getNowWithThousandSeparator(),
				IPPrefix::ip2txt(src_ip, s1),
				pushopt->iface_id,
				IPPrefix::ip2txt(pushopt->dst_ip, s2),
				((OpenVZMessage*)msg)->totalRealBytes()););
		outgoing_mac->pushdown(ip_msg, this);
	}
	else
	{
		IP_DUMP(char s1[50]; char s2[50];
		printf("[host=\"%s\"] %s: send packet from \"%s\" to \"%s\" (%d real bytes).\n",
				inHost()->nhi.toString(), getNowWithThousandSeparator(),
				IPPrefix::ip2txt(ip_message->src_ip, s1),
				IPPrefix::ip2txt(ip_message->dst_ip, s2),
				ip_message->totalRealBytes()););

		IPOptionToBelow mac_ext_info(route_info, false);
		outgoing_mac->pushdown(ip_msg, this, (void*)&mac_ext_info, sizeof(IPOptionToBelow));
	}

	return IPPUSHRET_DOWN_DONE;
}

int IPSession::pop(Activation msg, ProtocolSession* lo_sess, void* extinfo, size_t extinfo_size)
{
  assert(lo_sess);
  IP_DUMP(char buf1[50]; char buf2[50];
	  	  printf("[host=\"%s\"] %s: pop() receive packet from \"%s\" to \"%s\".\n",
	  			  inHost()->nhi.toString(), getNowWithThousandSeparator(),
	  			  IPPrefix::ip2txt(((IPMessage*)msg)->src_ip, buf1),
	  			  IPPrefix::ip2txt(((IPMessage*)msg)->dst_ip, buf2)));

  // perform checksum -- add later
  return handle_pop_pkt(msg, lo_sess, extinfo, extinfo_size);
}

bool IPSession::verify_local_ip_address(IPADDR ipaddr)
{
  if(inHost()->getNetworkInterfaceByIP(ipaddr)) return true;
  else return false;
}

bool IPSession::verify_target_ip_address(IPADDR ipaddr)
{
  if(ipaddr == IPADDR_ANYDEST || (ipaddr & 0xe0000000) || verify_local_ip_address(ipaddr))
	return true;
  else
	return false;
}

void IPSession::load_forwarding_table(s3f::dml::Configuration* cfg)
{
	if (!forwarding_table) {
		int trie_version = -1;
		char* str = (char*)cfg->findSingle("trie_version");
		if (str && !(s3f::dml::dmlConfig::isConf(str))) {
			trie_version = strtonum(str);
			if(trie_version < 0) {
				error_quit("ERROR: trie_version attribute must be >= 0.\n");
			}
		}

		int cache_version = -1;
		str = (char*)cfg->findSingle("cache_version");
		if (str && !(s3f::dml::dmlConfig::isConf(str))) {
			cache_version = strtonum(str);
			if(cache_version < 0) {
				error_quit("ERROR: cache_version attribute must be >= 0.\n");
			}
		}

		forwarding_table = new ForwardingTable(this,trie_version,cache_version);
		assert(forwarding_table);
	}

  Host* h = inHost();
  s3f::dml::Enumeration* renum = cfg->find("route");
  while (renum->hasMoreElements())
  {
    s3f::dml::Configuration* rcfg = (s3f::dml::Configuration*)renum->nextElement();
    if(!s3f::dml::dmlConfig::isConf(rcfg))
      error_quit("ERROR: IPSession::load_forwarding_table(), invalid ROUTE attribute.\n");

    RouteInfo* route = new RouteInfo();
    route->config(h, rcfg);
    control(IP_CTRL_INSERT_ROUTE, route, NULL);
  }
  delete renum;

  s3f::dml::Enumeration* denum = cfg->find("nhi_route");
  while(denum->hasMoreElements())
  {
    s3f::dml::Configuration* rcfg = (s3f::dml::Configuration*)denum->nextElement();
    if(!s3f::dml::dmlConfig::isConf(rcfg))
      error_quit("ERROR: IPSession::load_forwarding_table(), invalid DML_ROUTE attribute.\n");
    DMLRouteInfo* dml_route = new DMLRouteInfo;
    dml_route->config(rcfg);
    control(IP_CTRL_INSERT_DMLROUTE, dml_route, NULL);
  }
  delete denum;
}

ProtocolSession* IPSession::get_parent_by_protocol(uint32 protocol_no)
{
  return inHost()->sessionForNumber(protocol_no);
}

int IPSession::handle_pop_pkt(Activation ip_msg, ProtocolSession* lo_sess, void* extinfo, int extinfo_size)
{
  IPMessage* ip_message = (IPMessage*)ip_msg;

  // decrement the time-to-live field of the packet
  ip_message->time_to_live--;

  // if packet is for me deliver it to proper protocol
  if(verify_target_ip_address(ip_message->dst_ip) || inHost()->isOpenVZEmu() == true)
  {
    return handle_pop_local_pkt(ip_msg, lo_sess);
  }
  else
  {
    // otherwise just send the packet on its way further
    IP_DUMP(printf("[host=\"%s\"] %s: received a packet but not for me.\n",
    		inHost()->nhi.toString(), getNowWithThousandSeparator()));

    if(ip_message->time_to_live > 0)
    {
      // figure out the outgoing interface
      NetworkInterface* outgoing_nic = 0;
      void* carried_route_info = 0;

	  // on_demand_routing is turned off, find the route for the destination
	  RouteInfo* pRoute = forwarding_table->getRoute(ip_message->dst_ip);
	  if(0 == pRoute)
	  {
	      if(show_drop)
	      {
	        char s1[50]; char s2[50];
	        printf("[host=\"%s\"] %s: drop packet from \"%s\" to \"%s\" (no route).\n",
		     inHost()->nhi.toString(), getNowWithThousandSeparator(),
		     IPPrefix::ip2txt(ip_message->src_ip, s1),
		     IPPrefix::ip2txt(ip_message->dst_ip, s2));
	      }

	      ip_message->erase_all();

	    return IPPOPRET_DROPPED_NO_ROUTE;
	  }
	
	  IP_DUMP(char s1[50]; char s2[50];
	  printf("[host=\"%s\"] %s: forward packet from \"%s\" to \"%s\" for protocol %d.\n",
		       inHost()->nhi.toString(), getNowWithThousandSeparator(),
		       IPPrefix::ip2txt(ip_message->src_ip, s1),
		       IPPrefix::ip2txt(ip_message->dst_ip, s2), ip_message->protocol_no));

	  outgoing_nic = pRoute->nic;
	  carried_route_info = (void*)pRoute;
      assert(outgoing_nic);
      IPOptionToBelow mac_ext_info(carried_route_info, true);

      outgoing_nic->getHighestProtocolSession()->pushdown(ip_msg, this, (void*)&mac_ext_info, sizeof(IPOptionToBelow));
      return IPPOPRET_FORWARD_DONE;
    }
    else
    {
    	if(show_drop)
    	{
	      char s1[50]; char s2[50];
	      printf("[host=\"%s\"] %s: drop packet from \"%s\" to \"%s\" (ttl expires).\n",
	    		  inHost()->nhi.toString(), getNowWithThousandSeparator(),
	    		  IPPrefix::ip2txt(ip_message->src_ip, s1),
	    		  IPPrefix::ip2txt(ip_message->dst_ip, s2));
    	}
      ip_message->erase_all();

      return IPPOPRET_DROPPED_TTL_LIMIT;
    }
  }
}

int IPSession::handle_pop_local_pkt(Activation msg, ProtocolSession* lo_sess, void* extinfo, int extinfo_size)
{	  
  IPMessage* ip_message = (IPMessage*)msg;
  
  ProtocolSession* prot = get_parent_by_protocol((int)(uint32)ip_message->protocol_no);
  IP_DUMP(printf("[host=\"%s\"] %s: receive local packet for protocol %u.\n",
                 inHost()->nhi.toString(), getNowWithThousandSeparator(), ip_message->protocol_no));
    
  // send the packet upward
  int ret_val;
  if(prot)
  {
      IPOptionToAbove ipupopt;
      ipupopt.src_ip = ip_message->src_ip;
      ipupopt.dst_ip = ip_message->dst_ip;
      ipupopt.ttl = ip_message->time_to_live;

      ProtocolMessage* pmsg = ip_message->dropPayload();
      assert(pmsg);
      Activation upper_msg (pmsg);
      ip_message->erase(); //delete IP header

      prot->popup(upper_msg, this, (void*)&ipupopt, sizeof(IPOptionToAbove));
      ret_val = IPPOPRET_UP_DONE;
  }
  else
  {
      if(show_drop)
      {
    	char s1[50]; char s2[50];
    	printf("[host=\"%s\"] %s: drop packet from \"%s\" to \"%s\" (no protocol)\n",
    			inHost()->nhi.toString(), getNowWithThousandSeparator(),
    			IPPrefix::ip2txt(ip_message->src_ip, s1),
    			IPPrefix::ip2txt(ip_message->dst_ip, s2));
      }
    
    // no such protocol! drop the packet
    ip_message->erase_all();

    ret_val = IPPOPRET_DROPPED_NO_PROTOCOL;
  }
  return ret_val;
}

int IPSession::strtonum(const char* str) {
    std::istringstream iss(str);
    int res;

    iss >> std::ws >> res >> std::ws;

    if(!iss.eof()) {
		return -1;
	}

    return res; 
}

void IPSession::print_hex_ascii_line(const u_char *payload, int len, int offset)
{
	int i;
	int gap;
	const u_char *ch;

	/* offset */
	printf("%05d   ", offset);

	/* hex */
	ch = payload;
	for(i = 0; i < len; i++) {
		printf("%02x ", *ch);
		ch++;
		/* print extra space after 8th byte for visual aid */
		if (i == 7)
			printf(" ");
	}
	/* print space to handle line less than 8 bytes */
	if (len < 8)
		printf(" ");

	/* fill hex gap with spaces if not full line */
	if (len < 16) {
		gap = 16 - len;
		for (i = 0; i < gap; i++) {
			printf("   ");
		}
	}
	printf("   ");

	/* ascii (if printable) */
	ch = payload;
	for(i = 0; i < len; i++) {
		if (isprint(*ch))
			printf("%c", *ch);
		else
			printf(".");
		ch++;
	}

	printf("\n");
}

void IPSession::print_payload(const u_char *payload, int len)
{
	int len_rem = len;
	int line_width = 16;			/* number of bytes per line */
	int line_len;
	int offset = 0;					/* zero-based offset counter */
	const u_char *ch = payload;

	if (len <= 0)
		return;

	/* data fits on one line */
	if (len <= line_width) {
		print_hex_ascii_line(ch, len, offset);
		return;
	}

	/* data spans multiple lines */
	for ( ;; ) {
		/* compute current line length */
		line_len = line_width % len_rem;
		/* print line */
		print_hex_ascii_line(ch, line_len, offset);
		/* compute total remaining */
		len_rem = len_rem - line_len;
		/* shift pointer to remaining bytes to print */
		ch = ch + line_len;
		/* add offset */
		offset = offset + line_width;
		/* check if we have line width chars or less */
		if (len_rem <= line_width) {
			/* print last line and get out */
			print_hex_ascii_line(ch, len_rem, offset);
			break;
		}
	}
}

}; // namespace s3fnet
}; // namespace s3f
