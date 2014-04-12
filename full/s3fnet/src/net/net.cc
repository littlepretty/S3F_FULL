/**
 * \file net.cc
 * \brief Source file for the Net class.
 *
 * authors : Dong (Kevin) Jin
 */

#include "s3fnet/src/os/openflow_switch/openflow_controller.h"
#include "net/net.h"
#include "net/host.h"
#include "net/traffic.h"
#include "util/errhandle.h"
#include "env/namesvc.h"
#include "net/link.h"
#include "net/network_interface.h"
#include "os/openVZEmu/openVZEmu_event_session.h"
#include <arpa/inet.h>
//#include <netinet/ether.h>
#include <netinet/if_ether.h>
#include "net/nhi.h"

namespace s3f {
namespace s3fnet {

#ifdef NET_DEBUG
#define NET_DUMP(x) printf("NET: "); x
#else
#define NET_DUMP(x)
#endif

Net::Net(SimInterface* sim_interface) :
				DmlObject(0), ip_prefix(0, 0), is_top_net(true), namesvc(0),
				sim_iface(sim_interface), top_net(this), traffic(0), netacc(0), sim_ctrl(0), pkt_from_ve_counter(0),
				idbase(0), nr_ve(0), isOpenVZEmu(false)
{
	NET_DUMP(printf("new topnet.\n"));

	// create openflow controller (non-entity-type) based on the model specified in DML ("simulated_openflow_controller_model")
	if(openflow_controller_type() == 0)
	{
		switch( openflow_controller_model() )
		{
		case OF_LEARNING_SWITCH_CONTROLLER:
			of_controller = new LearningController();
			of_controller->type = OF_LEARNING_SWITCH_CONTROLLER;
			((LearningController*)of_controller)->setFlowExpirationTime(OF_FLOW_EXPIRATION_TIME); //todo use dml attribute later
			break;

		case OF_DROP_CONTROLLER:
			of_controller = new DropController();
			of_controller->type = OF_DROP_CONTROLLER;
			break;

		case NO_OF_CONTROLLER:
			of_controller = 0;
			break;

		default:
			error_quit("Net:Net(), not able to create openflow controller, unknown type().");
			break;
		}
	}

	pthread_mutex_init(&of_buf_mutex,NULL);
}

Net::Net(SimInterface* sim_interface, Net* parent, long myid):
				DmlObject(parent, myid), ip_prefix(0, 0), is_top_net(false), netacc(0),
				namesvc(parent->namesvc), sim_iface(sim_interface), top_net(parent->top_net),
				traffic(0), sim_ctrl(0), pkt_from_ve_counter(0), idbase(0), nr_ve(0), isOpenVZEmu(false)
{
	NET_DUMP(printf("new subnet: id=%ld.\n", myid));
}

Net::~Net()
{
	NET_DUMP(printf("delete net: nhi=\"%s\".\n", nhi.toString()));

#ifdef OPENVZ_EMULATION
	if(is_top_net == true && isOpenVZEmu == true)
	{
		printf("total #packet in from VEs = %ld\n", pkt_from_ve_counter);
		pthread_mutex_destroy(&of_buf_mutex);
	}
#endif

	if(netacc) delete netacc;

	for(S3FNET_INT2PTR_MAP::iterator iter = nets.begin(); iter != nets.end(); iter++)
	{
		assert(iter->second);
		delete (Net*)(iter->second);
	}

	for(S3FNET_INT2PTR_MAP::iterator iter = hosts.begin(); iter != hosts.end(); iter++) {
		assert(iter->second);
		delete (Host*)(iter->second);
	}

	for(unsigned i=0; i<links.size(); i++)
	{
		assert(links[i]);
		delete (Link*)(links[i]);
	}
}

void Net::config(s3f::dml::Configuration* cfg)
{
	NET_DUMP(printf("config().\n"));

	s3f::dml::Configuration* topcfg;
	char* str;
	set<long> uniqset;

	if(!myParent) //topnet
	{
		config_top_net(cfg);
		topcfg = cfg;
		cfg = (s3f::dml::Configuration*)cfg->findSingle("net");
		if(!cfg || !s3f::dml::dmlConfig::isConf(cfg))
			error_quit("ERROR: missing or invalid (top) NET attribute.\n");

#ifdef OPENVZ_EMULATION
		char* str = (char*)cfg->findSingle("ve_id_base");
		if(!str) idbase = -1;
		else
		{
			if(s3f::dml::dmlConfig::isConf(str))
				error_quit("ERROR: invalid idbase attribute.\n");
			idbase = atoi(str);
			if(idbase < 100)
				error_quit("ERROR: idbase attribute must >= 100.\n");
		}

		str = (char*)cfg->findSingle("number_ve");
		if(!str) nr_ve = -1;
		else
		{
			if(s3f::dml::dmlConfig::isConf(str))
				error_quit("ERROR: invalid number_ve attribute.\n");
			nr_ve = atoi(str);
			if(nr_ve < 2)
				error_quit("ERROR: number_ve attribute must >= 2.\n");
		}

		if(idbase == -1 || nr_ve == -1)
		{
			isOpenVZEmu = false;
		}
		else
		{
			isOpenVZEmu = true;
		}

		//pass variables to sim_ctrl class in the TimelineInterface
		if(isOpenVZEmu == true)
		{
			sim_ctrl = sim_iface->get_timeline_interface()->sc;
			assert(sim_ctrl);
			int return_code = sim_ctrl->set_ve_boundary(idbase, nr_ve);
			if(return_code != 0)
			{
				printf("error in set_ve_boundary(), error code = %s\n",
						SimCtrl::errcode2txt(return_code));
				error_quit("Net::config(), sim_ctrl->set_ve_boundary\n");
			}
			return_code = sim_ctrl->emu_reset();
			if(return_code != 0)
			{
				printf("error in emu_reset(), error code = %s\n",
						SimCtrl::errcode2txt(return_code));
				error_quit("Net::config(), sim_ctrl->emu_reset\n");
			}
			sim_ctrl->siminf = sim_iface;
		}

		NET_DUMP(printf("config(): openVZ idbase=%d, number_ve=%d.\n", idbase, nr_ve));
#endif
	}
	else
	{
		nhi = myParent->nhi;
		nhi += id;
	}
	nhi.type = Nhi::NHI_NET;

	// config alignment
	// now timeline is assigned to the net, default one is 0,
	// todo we can provide option to assign to individual host

	str = (char*)cfg->findSingle("alignment");
	if(!str) alignment = 0;
	else
	{
		if(s3f::dml::dmlConfig::isConf(str))
			error_quit("ERROR: Net::config(), invalid NET.ALIGNMENT attribute.\n");

		alignment = atoi(str);
	}

	NET_DUMP(printf("config(): nhi=\"%s\", alignment=\"%d\"\n", nhi.toString(), alignment));

	//config subnetworks, hosts and links in the sequence in dml, deepest first
	// config sub networks
	NET_DUMP(printf("net \"%s\" config sub networks.\n", nhi.toString()));
	s3f::dml::Enumeration* nenum = cfg->find("net");
	while(nenum->hasMoreElements())
	{
		s3f::dml::Configuration* ncfg = (s3f::dml::Configuration*)nenum->nextElement();
		if(!s3f::dml::dmlConfig::isConf(ncfg))
			error_quit("ERROR: Net::config(), invalid NET.NET attribute.\n");
		config_net(ncfg, uniqset);
	}
	delete nenum;

	// config hosts/routers/switches
	NET_DUMP(printf("net \"%s\" config hosts.\n", nhi.toString()));
	s3f::dml::Enumeration* henum = cfg->find("host");
	while (henum->hasMoreElements())
	{
		s3f::dml::Configuration* hcfg = (s3f::dml::Configuration*)henum->nextElement();
		if(!s3f::dml::dmlConfig::isConf(hcfg))
			error_quit("ERROR: Net::config(), invalid NET.HOST attribute.\n");
		config_host(hcfg, uniqset, false);
	}
	delete henum;

	henum = cfg->find("router");
	while (henum->hasMoreElements())
	{
		s3f::dml::Configuration* hcfg = (s3f::dml::Configuration*)henum->nextElement();
		if(!s3f::dml::dmlConfig::isConf(hcfg))
			error_quit("ERROR: Net::config(), invalid NET.ROUTER attribute.\n");
		config_router(hcfg, uniqset);
	}
	delete henum;

	henum = cfg->find("ofswitch");
	while (henum->hasMoreElements())
	{
		s3f::dml::Configuration* hcfg = (s3f::dml::Configuration*)henum->nextElement();
		if(!s3f::dml::dmlConfig::isConf(hcfg))
			error_quit("ERROR: Net::config(), invalid NET.openflow_switch attribute.\n");
		config_of_switch(hcfg, uniqset);
	}
	delete henum;

	// config links
	NET_DUMP(printf("net \"%s\" config links.\n", nhi.toString()));
	s3f::dml::Enumeration* lenum = cfg->find("link");
	while(lenum->hasMoreElements())
	{
		s3f::dml::Configuration* lcfg = (s3f::dml::Configuration*)lenum->nextElement();
		if(!s3f::dml::dmlConfig::isConf(lcfg))
			error_quit("ERROR: Net::config(), invalid NET.LINK attribute.\n");
		config_link(lcfg);
	}
	delete lenum;

	if(!myParent) finish_config_top_net(topcfg);
}

void Net::connect_links()
{
	NET_DUMP(printf("net \"%s\" connect links.\n", nhi.toString()));

	for(S3FNET_INT2PTR_MAP::iterator iter = nets.begin(); iter != nets.end(); iter++)
	{
		Net* nn = (Net*)(iter->second);
		nn->connect_links();
	}

	for(unsigned i=0; i < links.size(); i++)
	{
		Link* pLink = (Link*)links[i];
		pLink->connect(this);
	}
}

NameService* Net::getNameService()
{
	return namesvc;
}

IPADDR Net::nicnhi2ip(S3FNET_STRING nhi_str)
{
	assert(namesvc);
	return namesvc->nhi2ip(nhi_str);
}

DmlObject* Net::nhi2obj(Nhi* pNhi)
{
	// if it's a network interface, use nicnhi2iface_map in the namesvc
	if(pNhi->type == Nhi::NHI_INTERFACE)
	{
		return namesvc->nicnhi2iface(pNhi->toStlString());
	}

	// if it's host or net NHI, search in its nets / hosts
	S3FNET_VECTOR(long)* pIDs = &(pNhi->ids);
	int origStart = pNhi->start;
	int len = pIDs->size() - origStart;;
	int type = pNhi->type;
	// The maximum length for the nhi to be in this net.
	int maxlength = (type == Nhi::NHI_INTERFACE)?2:1;

	// If the NHI is in some other Net, let that net do the work
	if(len > maxlength)
	{
		int id = (*pIDs)[origStart];
		// Find the Net with this id.
		S3FNET_INT2PTR_MAP::iterator iter = nets.find(id);
		if(iter == nets.end()) return 0;
		Net* pNet = (Net*)iter->second;
		pNhi->start++;
		DmlObject* pObject = pNet->nhi2obj(pNhi);
		pNhi->start = origStart;
		return pObject;
	}

	// The net/machine is in this net.
	int id = (*pIDs)[origStart];
	if(type == Nhi::NHI_NET)
	{
		S3FNET_INT2PTR_MAP::iterator iter = nets.find(id);
		if(iter == nets.end()) return 0;
		return (Net*)iter->second;
	}
	else
	{
		S3FNET_INT2PTR_MAP::iterator iter = hosts.find(id);
		if(iter == hosts.end()) return 0;
		return  (Host*)iter->second;
	}
}

void Net::getIdrangeLimits(s3f::dml::Configuration* cfg,
		int& low, int& high)
{
	// get id start and end
	char* str = (char*)cfg->findSingle("id");
	if(str) {
		if(s3f::dml::dmlConfig::isConf(str))
			error_quit("ERROR: Net::getIdrangeLimits(), invalid ID attribute.\n");
		low = high = atoi(str);
		return;
	}

	s3f::dml::Configuration* rcfg = (s3f::dml::Configuration*)
    		cfg->findSingle("idrange");
	if(!rcfg)
		error_quit("Error: Net::getIdrangeLimits(), "
				"neither ID nor IDRANGE is specified.\n");
	if(!s3f::dml::dmlConfig::isConf(rcfg))
		error_quit("ERROR: Net::getIdrangeLimits(), invalid IDRANGE attribute.\n");

	str = (char*)rcfg->findSingle("from");
	if(!str  || s3f::dml::dmlConfig::isConf(str))
		error_quit("ERROR: Net::getIdrangeLimits(), "
				"missing or invalid IDRANGE.FROM attribute.\n");
	low = atoi(str);

	str = (char*)rcfg->findSingle("to");
	if(!str  || s3f::dml::dmlConfig::isConf(str))
		error_quit("ERROR: Net::getIdrangeLimits(), "
				"missing or invalid IDRANGE.TO attribute.\n");
	high = atoi(str);

	if(low > high)
		error_quit("ERROR: Net::getIdrangeLimits(), "
				"IDRANGE FROM %d is greater then TO %d.\n", low, high);
	if(low < 0)
		error_quit("ERROR: Net::getIdrangeLimits(), "
				"IDRANGE.FROM must be non-negative.\n");
}

void Net::init()
{
	NET_DUMP(printf("net \"%s\" init.\n", nhi.toString()));

	if(!myParent) // top net
	{
		if(netacc)
		{
			// find the traffic attribute; initialize the traffic
			Traffic* traffic = 0;
			if(netacc->requestAttribute(NET_ACCESSORY_TRAFFIC, (void*&)traffic))
			{
				if(traffic) traffic->init();
			}
		}
#ifdef OPENVZ_EMULATION
		//init ve handler, store send queue and recv queue in topnet
		if(isOpenVZEmu == true)
		{
			for (int i = 0; i < nr_ve; i++)
			{
				VEHandle* vh = sim_ctrl->get_vehandle(idbase+i);
				ve.push_back(vh);
			}
		}
#endif
	}

	// initialize nets
	for(S3FNET_INT2PTR_MAP::iterator iter = nets.begin(); iter != nets.end(); iter++)
	{
		((Net*)iter->second)->init();
	}

	// initialize links.
	// Links must be initialized before hosts because protocols which are initialized with each host might want links initialized.
	for(unsigned i=0; i < links.size(); i++)
	{
		((Link*)links[i])->init();
	}
}

void Net::display(int indent)
{
	char strNhi[50];

	nhi.toString(strNhi);
	printf("%*cNet [\n", indent, ' ');
	indent += DML_OBJECT_INDENT;
	printf("%*cnhi %s\n", indent, ' ', strNhi);
	printf("%*cip_prefix ", indent, ' ');
	ip_prefix.display();
	printf("\n");

	// display hosts/routers
	for(S3FNET_INT2PTR_MAP::iterator iter = hosts.begin();
			iter != hosts.end(); iter++)
		((Host*)(iter->second))->display(indent);

	// display nets
	for(S3FNET_INT2PTR_MAP::iterator iter = nets.begin();
			iter != nets.end(); iter++)
		((Net*)(iter->second))->display(indent);

	// display Links
	for(unsigned i = 0; i < links.size(); i++)
		((Link*)links[i])->display(indent);

	printf("%*c]\n", indent-DML_OBJECT_INDENT, ' ');
}

void Net::config_host(s3f::dml::Configuration* cfg, S3FNET_LONG_SET& uniqset, bool is_router, bool is_of_switch)
{
	// get id start and end
	int id_start, id_end;
	getIdrangeLimits(cfg, id_start, id_end);

	// now the id range is fixed. config them one by one.
	for(int i = id_start; i <= id_end; i++)
	{
		if(!uniqset.insert((long)i).second)
			error_quit("ERROR: duplicate ID %d used for NET.HOST/ROUTER.\n", i);

		NET_DUMP(printf("config_host(), created host %d on timeline %d\n", i, alignment));

		//todo: for host-based alignment assignment. generate hostnhi first,
		Timeline* tl = sim_iface->get_Timeline(alignment);
		Host* h = new Host(tl, this, (long)i);
		hosts.insert(S3FNET_MAKE_PAIR(i, h));
		if(is_router) h->is_router = true;
		if(is_of_switch) h->is_of_switch = true;
		h->config(cfg);
	}
}

void Net::config_router(s3f::dml::Configuration* cfg, S3FNET_LONG_SET& uniqset)
{
	config_host(cfg, uniqset, true, false);
}

void Net::config_of_switch(s3f::dml::Configuration* cfg, S3FNET_LONG_SET& uniqset)
{
	config_host(cfg, uniqset, false, true);
}

void Net::config_net(s3f::dml::Configuration* cfg, S3FNET_LONG_SET& uniqset)
{
	//CidrBlock* self_cidr = curCidr;

	// get id start and end
	int id_start, id_end;
	getIdrangeLimits(cfg, id_start, id_end);

	// now the id range is fixed. config them one by one.
	for(int i = id_start; i <= id_end; i++) {
		if(!uniqset.insert((long)i).second)
			error_quit("ERROR: duplicate ID %d used for NET.NET.\n", i);

		Net* nn = new Net(sim_iface, this, (long)i);
		assert(nn);

		assert(curCidr);
		CidrBlock* subcidr = curCidr->getSubCidrBlock(i);
		assert(subcidr);
		nn->curCidr = subcidr;
		nn->ip_prefix = subcidr->getPrefix();

		nets.insert(S3FNET_MAKE_PAIR(i, nn));
		nn->config(cfg);
	}

	// before quit, assign self_cidr back.
	//curCidr = self_cidr;
}


void Net::config_link(s3f::dml::Configuration* cfg)
{
	NET_DUMP(printf("config_link().\n"));
	// construct default link
	Link* newlink = Link::createLink(this, cfg);
	links.push_back(newlink);
}

void Net::config_top_net(s3f::dml::Configuration* cfg)
{
	NET_DUMP(printf("config_top_net().\n"));

	id = 0;

	// start name resolution service
	s3f::dml::Configuration* acfg = (s3f::dml::Configuration*)cfg->findSingle("environment_info");
	if (!acfg)
		error_quit("ERROR: Net::config_top_net(), missing ENVIRONMENT_INFO attribute; run dmlenv first!\n");
	else if(!s3f::dml::dmlConfig::isConf(acfg))
		error_quit("ERROR: Net::config_top_net(), invalid ENVIRONMENT_INFO attribute.\n");

	namesvc = new NameService;
	namesvc->config(acfg);
	assert(namesvc);

	curCidr = &namesvc->top_cidr_block;
}

void Net::finish_config_top_net(s3f::dml::Configuration* cfg)
{
	NET_DUMP(printf("finish_config_top_net().\n"));

	// configure traffic
	s3f::dml::Configuration* tcfg = (s3f::dml::Configuration*)cfg->findSingle("net.traffic");
	if(tcfg)
	{
		if(!s3f::dml::dmlConfig::isConf(tcfg))
			error_quit("ERROR: Net::config(), invalid NET.TRAFFIC attribute.\n");
		traffic = new Traffic(this);
		assert(traffic);
		traffic->config(tcfg);
		if(!netacc) netacc = new NetAccessory;
		netacc->addAttribute(NET_ACCESSORY_TRAFFIC, (void*)traffic);
	}
	else
		traffic = 0;

	// connect the links in the net
	connect_links();

	// load the forwarding tables if specified
	s3f::dml::Enumeration* fenum = cfg->find("forwarding_table");
	while(fenum->hasMoreElements())
	{
		s3f::dml::Configuration* fcfg = (s3f::dml::Configuration*)fenum->nextElement();
		if(!s3f::dml::dmlConfig::isConf(fcfg))
			error_quit("ERROR: Net::finish_config_top_net(), invalid FORWARDING_TABLE attribute.\n");
		load_fwdtable(fcfg);
	}
	delete fenum;
}

void Net::load_fwdtable(s3f::dml::Configuration* cfg)
{
	char* str = (char*)cfg->findSingle("node_nhi");
	if (!str || s3f::dml::dmlConfig::isConf(str))
		error_quit("ERROR: Net::load_fwdtable(), missing or invalid FORWARDING_TABLE.NODE_NHI attribute.\n");
	S3FNET_STRING hostnhi = str;

	Host* host = namesvc->hostnhi2hostobj(hostnhi);
	assert(host);
	host->loadForwardingTable(cfg);
}

void Net::register_host(Host* host, const S3FNET_STRING& name, bool isOpenVZEmu)
{
	assert(host);

#ifdef OPENVZ_EMULATION
	// register all hosts with its nhi
	if( isOpenVZEmu == true )
	{
		if( !(namesvc->get_veid2hostobj_map()->insert(S3FNET_MAKE_PAIR(host->veid, host))).second )
		{
			error_quit("ERROR: Net::register_host(), duplicate veid in veid2hostobj_map, "
					"veid = (%d), host = \"%s\".\n", host->veid, host->nhi.toString());
		}
	}
#endif
}

void Net::register_interface(NetworkInterface* iface, IPADDR ip_addr, bool isOpenVZEmu)
{
	assert(iface);

	NET_DUMP(printf("register_interface(): ip=\"%s\".\n", IPPrefix::ip2txt(ip_addr)));

	if( !(namesvc->get_ip2iface_map()->insert(S3FNET_MAKE_PAIR(ip_addr, iface))).second )
	{
		error_quit("ERROR: Net::register_interface(), "
				"duplicate registered network interface: IP=\"%s\"", IPPrefix::ip2txt(ip_addr));
	}
}

NetworkInterface* Net::nicnhi_to_obj(S3FNET_STRING nhi_str)
{
	return namesvc->nicnhi2iface(nhi_str);
}

int Net::control(int ctrltyp, void* ctrlmsg)
{
	switch(ctrltyp) {

	case NET_CTRL_GET_TRAFFIC:
		// only the top net has the right to specify traffic.
		if(myParent) return ((Net*)myParent)->control(NET_CTRL_GET_TRAFFIC, ctrlmsg);
		else
		{
			if(netacc)
			{
				netacc->requestAttribute(NET_ACCESSORY_TRAFFIC, *((void**)ctrlmsg));
				return 1;
			}
			else
			{
				*((void**)ctrlmsg) = 0;
				return 0;
			}
		}

	default:
		error_quit("ERROR: Unknown Net::control() command!\n");
		return 0;
	}
}

#ifdef OPENVZ_EMULATION
Host* Net::findOpenVZEmuHost(int veid_src)
{
	//namesvc->print_veid2hostobj_map();
	//namesvc->print_ip2iface_map();

	S3FNET_STRING src_hostnhi;

	Host* host = namesvc->veid2hostobj(veid_src);
	if(!host)
	{
		NET_DUMP(printf("Net::findOpenVZEmuHost(): Host* not found for src_veid = %d.\n", veid_src));
	}
	return host;
}

int Net::process_ARP_packet(EmuPacket *ppkt)
{
	if(ppkt->len <= 0)
	{
		printf("Net::process_ARP_packet(), len = %d \n", ppkt->len);
		return 0;
	}

	char veip_target[32];
	arp = (struct sniff_arp*)(ppkt->data + SIZE_ETHERNET);

	/* get the Target IP address */
	sprintf (veip_target, "%u.%u.%u.%u", arp->__ar_tip[0],arp->__ar_tip[1],arp->__ar_tip[2],arp->__ar_tip[3]);
	if(veip_target == NULL)
	{
		printf("Net::process_ARP_packet(), inet_ntoa(), veip_target == NULL \n");
		return 0;
	}

	IPADDR ip_target = IPPrefix::txt2ip(veip_target);

	if(ip_target == IPADDR_INVALID)
	{
		NET_DUMP(printf("Net::process_ARP_packet(): veip_target (%s) not found.\n", veip_target));
		return 0;
	}
	//find interface number from ip
	const char* ifacenhi_str = namesvc->ip2nicnhi(ip_target);
	if(ifacenhi_str == 0) return 0; // ip_target does not belong to any host in this network
	Nhi ifacenhi;
	ifacenhi.convert((char*)ifacenhi_str, Nhi::NHI_INTERFACE);
	int iface_num = ifacenhi.ids[ifacenhi.ids.size()-1];

	//find target veid from ip
	int veid_target = namesvc->ip2veid(ip_target);

	if(veid_target < 0 || veid_target >= nr_ve)
	{
		//e.g. ip_target belongs to some simulated node, not emulated node
		return 0;
	}

	//deliver the packet to the target VE immediately
	ppkt->ifid = iface_num;
	ve[veid_target]->recvq.push_back(ppkt);
	return 1;
}

//currently print Ethernet src and dst MAC address
void Net::printEthernetHeader()
{
	u_char *ptr = ethernet->ether_dhost;
	int j = ETHER_ADDR_LEN;
	printf(" Destination Address:  ");
	do
	{
		printf("%s%x",(j == ETHER_ADDR_LEN) ? " " : ":",*ptr++);
	}
	while(--j>0);
	printf("\n");

	ptr = ethernet->ether_shost;
	j = ETHER_ADDR_LEN;
	printf(" Source Address:  ");
	do
	{
		printf("%s%x",(j == ETHER_ADDR_LEN) ? " " : ":",*ptr++);
	}
	while(--j>0);
	printf("\n");
}

IPADDR Net::getDstIPFromRealPacket(unsigned char* data)
{
	char* veip_dst;

	ip = (struct sniff_ip*)(data + SIZE_ETHERNET);
	size_ip = IP_HL(ip)*4;
	if (size_ip < 20)
	{
		printf("Net::getDstIPFromRealPacket(), Invalid IP header length: %u bytes\n", size_ip);
		return IPADDR_INVALID;
	}

	/* get the destination IP address in a human readable form */
	veip_dst = inet_ntoa(ip->ip_dst);
	if(veip_dst == NULL)
	{
		printf("Net::getDstIPFromRealPacket(), inet_ntoa(), veip_dst == NULL \n");
		return IPADDR_INVALID;
	}

	/* now get the dst_ip in s3fnet from veip_dst */
	IPADDR ip_dst = IPPrefix::txt2ip(veip_dst);
	//printf("getDstIPFromRealPacket(), veip_dst = %s,  ip_dst = %d\n", veip_dst, ip_dst);
	return ip_dst;
}

IPADDR Net::getSrcIPFromRealPacket(unsigned char* data)
{
	char* veip_src;

	ip = (struct sniff_ip*)(data + SIZE_ETHERNET);
	size_ip = IP_HL(ip)*4;
	if (size_ip < 20)
	{
		printf("Net::getDstIPFromRealPacket(), Invalid IP header length: %u bytes\n", size_ip);
		return IPADDR_INVALID;
	}

	/* get the destination IP address in a human readable form */
	veip_src = inet_ntoa(ip->ip_src);
	if(veip_src == NULL)
	{
		printf("Net::getSrcIPFromRealPacket(), inet_ntoa(), veip_src == NULL \n");
		return IPADDR_INVALID;
	}

	/* now get the dst_ip in s3fnet from veip_dst */
	IPADDR ip_src = IPPrefix::txt2ip(veip_src);
	//printf("getSrcIPFromRealPacket(), veip_src = %s,  ip_src = %d\n", veip_src, ip_src);
	return ip_src;
}

void Net::injectEmuEvents()
{
	// used to break tie for sending packet event with the same timestamp
	// particularly need for events injected from the openvz emulation
	int priority = 0;
	IPADDR dst_ip;
	IPADDR src_ip;

	if(isOpenVZEmu == true)
	{
		for (int i = 0; i < nr_ve; i++)
		{
			priority = 0;
			while (!ve[i]->sendq.empty())
			{
				EmuPacket *ppkt = ve[i]->sendq.front();
				ve[i]->sendq.pop_front();

				if(ppkt->len <= 0)
				{
					delete ppkt;
					continue;
				}

				Host* host = findOpenVZEmuHost(i);
				if(host == NULL || host->isOpenVZEmu() == false)
				{
					delete ppkt;
					continue;
				}

				OpenVZEventSession* sess =
						(OpenVZEventSession*)host->sessionForName(OPENVZ_EVENT_PROTOCOL_NAME);

				ethernet = (struct sniff_ethernet*)(ppkt->data);

				//printEthernetHeader();

				/* Do a couple of checks to see what packet type we have.*/
				if(ntohs(ethernet->ether_type) == ETHERTYPE_IP)
				{
					NET_DUMP(printf("Net::injectEmuEvents(), Ethernet type hex:%x dec:%d is an IP packet\n",
							ntohs(ethernet->ether_type), ntohs(ethernet->ether_type)));
					dst_ip = getDstIPFromRealPacket(ppkt->data);
					src_ip = getSrcIPFromRealPacket(ppkt->data);
				}
				else if(ntohs (ethernet->ether_type) == ETHERTYPE_ARP)
				{
					NET_DUMP(printf("Net::injectEmuEvents(), Ethernet type hex:%x dec:%d is an ARP packet\n",
							ntohs(ethernet->ether_type), ntohs(ethernet->ether_type)));
					if(process_ARP_packet(ppkt) == 0)
					{
						delete ppkt; //there is an error in the packet
					}
					continue;
				}
				else
				{
					printf("Net::injectEmuEvents(), Ethernet type %x not IP or ARP", ntohs(ethernet->ether_type));
					delete ppkt;
					continue;
				}

				sess->injectEvent(dst_ip, src_ip, ppkt, priority);

				//we can edit VEHandle to record per VE in/out #packets
				pkt_from_ve_counter++;

				priority++;
			}
		}
	}
}
#endif //#ifdef OPENVZ_EMULATION

void NetAccessory::addAttribute(byte at, bool value)
{
	NetAttribute* new_attrib = new NetAttribute;
	new_attrib->name = at;
	new_attrib->value.bool_val = value;

	// put it in the list
	netAttribList.push_back(new_attrib);
}

void NetAccessory::addAttribute(byte at, int value)
{
	NetAttribute* new_attrib = new NetAttribute;
	new_attrib->name = at;
	new_attrib->value.int_val = value;

	// put it in the list
	netAttribList.push_back(new_attrib);
}

void NetAccessory::addAttribute(byte at, double value)
{
	NetAttribute* new_attrib = new NetAttribute;
	new_attrib->name = at;
	new_attrib->value.double_val = value;

	// put it in the list
	netAttribList.push_back(new_attrib);
}

void NetAccessory::addAttribute(byte at, void* value)
{
	NetAttribute* new_attrib = new NetAttribute;
	new_attrib->name = at;
	new_attrib->value.pointer_val = value;

	// put it in the list
	netAttribList.push_back(new_attrib);
}

bool NetAccessory::requestAttribute(byte at, bool& value)
{
	int size = netAttribList.size();
	for(int i = 0; i < size; i++)
	{
		if(netAttribList[i]->name == at)
		{
			value = netAttribList[i]->value.bool_val;
			return true;
		}
	}

	// if the attribute cannot be found, return 0.
	return false;
}

bool NetAccessory::requestAttribute(byte at, int& value)
{
	int size = netAttribList.size();
	for(int i = 0; i < size; i++)
	{
		if(netAttribList[i]->name == at)
		{
			value = netAttribList[i]->value.int_val;
			return true;
		}
	}

	// if the attribute cannot be found, return 0.
	return false;
}

bool NetAccessory::requestAttribute(byte at, double& value)
{
	int size = netAttribList.size();
	for(int i = 0; i < size; i++)
	{
		if(netAttribList[i]->name == at)
		{
			value = netAttribList[i]->value.double_val;
			return true;
		}
	}

	// if the attribute cannot be found, return 0.
	return false;
}

bool NetAccessory::requestAttribute(byte at, void*& value)
{
	int size = netAttribList.size();
	for(int i = 0; i < size; i++)
	{
		if(netAttribList[i]->name == at)
		{
			value = netAttribList[i]->value.pointer_val;
			return true;
		}
	}

	// if the attribute cannot be found, return 0.
	return false;
}

NetAccessory::~NetAccessory()
{
	unsigned size= netAttribList.size();
	for(unsigned i=0; i<size; i++)
		delete netAttribList[i];
}

NetAccessory::NetAttribute::~NetAttribute()
{
	switch (name)
	{
	case NET_ACCESSORY_TRAFFIC:
		delete (Traffic*)value.pointer_val;
		break;
	default:
		break; // do nothing for other types
	}
}

}; // namespace s3fnet
}; // namespace s3f
