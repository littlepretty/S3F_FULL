/**
 * \file net.h
 * \brief Header file for the Net class.
 *
 * authors : Dong (Kevin) Jin
 */

#ifndef __NET_H__
#define __NET_H__

#include "s3fnet/src/util/shstl.h"
#include "s3fnet/src/net/ip_prefix.h"
#include "s3fnet/src/net/dmlobj.h"
#include "s3fnet/src/s3fnet.h"
#include <net/ethernet.h>
#include <netinet/in.h>
#include <pthread.h>

#define OF_FLOW_EXPIRATION_TIME 0

namespace s3f {
namespace s3fnet {

class SimInterface;
class Link;
class NetworkInterface;
class Traffic;
class NetAccessory;
class CidrBlock;
class Host;
class NameService;
class Controller;
class LearningController;

#ifndef ETH_ALEN
/* Ethernet addresses are 6 bytes */
#define ETH_ALEN 6
#endif

/* Ethernet headers are always exactly 14 bytes */
#define SIZE_ETHERNET 14
#define SIZE_UDP 8


/** Ethernet header */
struct sniff_ethernet {
	u_char ether_dhost[ETHER_ADDR_LEN]; ///< Destination host address
	u_char ether_shost[ETHER_ADDR_LEN]; ///< Source host address
	u_short ether_type; ///< IP? ARP? RARP? etc
};

/** IP header */
struct sniff_ip {
	u_char ip_vhl;		///< version << 4 | header length >> 2
	u_char ip_tos;		///< type of service
	u_short ip_len;		///< total length
	u_short ip_id;		///< identification
	u_short ip_off;		///< fragment offset field
#define IP_RF 0x8000		///< reserved fragment flag
#define IP_DF 0x4000		///< dont fragment flag
#define IP_MF 0x2000		///< more fragments flag
#define IP_OFFMASK 0x1fff	///< mask for fragmenting bits
	u_char ip_ttl;		///< time to live
	u_char ip_p;		///< protocol
	u_short ip_sum;		///< checksum
	struct in_addr ip_src,ip_dst; ///< source and destination address
};
#define IP_HL(ip)		(((ip)->ip_vhl) & 0x0f)
#define IP_V(ip)		(((ip)->ip_vhl) >> 4)

/** ARP header */
struct sniff_arp {
	unsigned short int ar_hrd;		///< Format of hardware address.
	unsigned short int ar_pro;		///< Format of protocol address.
	unsigned char ar_hln;		///< Length of hardware address.
	unsigned char ar_pln;		///< Length of protocol address.
	unsigned short int ar_op;		///< ARP opcode (command).

	/* Ethernet looks like this : This bit is variable sized however...  */
	unsigned char __ar_sha[ETH_ALEN];	///< Sender hardware address.
	unsigned char __ar_sip[4];		///< Sender IP address.
	//struct in_addr __ar_sip;
	unsigned char __ar_tha[ETH_ALEN];	///< Target hardware address.
	unsigned char __ar_tip[4];		///< Target IP address.
	//struct in_addr __ar_tip;
};

/** TCP header */
struct sniff_tcp {
	u_short th_sport;	///< source port
	u_short th_dport;	///< destination port
	//tcp_seq th_seq;		///< sequence number
	//tcp_seq th_ack;		///< acknowledgement number

	u_char th_offx2;	///< data offset, rsvd
#define TH_OFF(th)	(((th)->th_offx2 & 0xf0) >> 4)
	u_char th_flags;
#define TH_FIN 0x01
#define TH_SYN 0x02
#define TH_RST 0x04
#define TH_PUSH 0x08
#define TH_ACK 0x10
#define TH_URG 0x20
#define TH_ECE 0x40
#define TH_CWR 0x80
#define TH_FLAGS (TH_FIN|TH_SYN|TH_RST|TH_ACK|TH_URG|TH_ECE|TH_CWR)
	u_short th_win;		///< window
	u_short th_sum;		///< checksum
	u_short th_urp;		///< urgent pointer
};

/** UDP header */
struct sniff_udp {
	u_short	uh_sport;		///< source port
	u_short	uh_dport;		///< destination port
	u_short	uh_ulen;		///< datagram length
	u_short	uh_sum;			///< datagram checksum
};

/** only TRAFFIC is in use */
enum {
	NET_ACCESSORY_IS_AS_BOUNDARY	= 1,
	NET_ACCESSORY_AS_NUMBER	= 2,
	NET_ACCESSORY_AREA_NUMBER	= 3,
	NET_ACCESSORY_TRAFFIC		= 4,
	NET_ACCESSORY_QUEUE_MONITORS	= 5,
	NET_ACCESSORY_UNSPECIFIED	= 1000
};

/** control message and types and responses */
enum {
	NET_CTRL_IS_AS_BOUNDARY	= 1,
	NET_CTRL_GET_AS_NUMBER	= 2,
	NET_CTRL_GET_AREA_NUMBER	= 3,
	NET_CTRL_GET_TRAFFIC		= 4,
	NET_CTRL_GET_QUEUE_MONITORS	= 5,

	AS_NUMBER_UNSPECIFIED		= -1,
	AREA_NUMBER_UNSPECIFIED	= 0,
};

/**
 * \brief A network.
 *
 * The net class is the container for everything.  All DML objects
 * (including internal nets) must be contained in a net.  Conceptually
 * a net is just a collection of hosts and routers and other nets. The
 * concept of a net is not that important for protocols like TCP and
 * IP, but it's crucial for OSPF and BGP because a net can be an
 * autonomous system (AS) or an OSPF area.  This net class will start
 * the configuration of the network.  it will scan for hosts, routers,
 * links and other nets in the DML and create/link these entities.
 * nets also play a very important role in resolution of NHI
 * addresses.
 */
class Net: public DmlObject {
public:
	/** The constructor used by the top net. */
	Net(SimInterface* sim_interface);

	/** The constructor used by nets other than the top net. */
	Net(SimInterface* sim_interface, Net* parent, long id);

	/** The destructor. */
	virtual ~Net();

	/** Configure this net from DML. */
	void config(s3f::dml::Configuration* cfg);

	/**
	 * Initialize the network. This function is called after the entire
	 * network has been read from the DML but before the simulation
	 * actually starts.
	 */
	void init();

	/** Displays the contents of this net.*/
	virtual void display(int indent = 0);

	/**
	 * Resolves the given NHI address relative to this network.  Returns
	 * NULL if no such object exists in this network.  Depending on the
	 * type of NHI, we resolve net/host/interface addresses.  It is
	 * important that the NHI must have determined its type before this
	 * method can be called.
	 */
	DmlObject* nhi2obj(Nhi* nhi);

	/**
	 * Resolve the given interface NHI to an address.  If failed, it
	 * returns IPADDR_INVALID.
	 */
	IPADDR nicnhi2ip(S3FNET_STRING nhi);

	/**
	 * Resolve IPADDR to the corresponding nic nhi. If failed, it
	 * returns null. */
	const char* ip2nicnhi(IPADDR addr);

	/** Get the IP prefix of this network. */
	IPPrefix getPrefix() { return ip_prefix; }

	/** Get the limits of an id-range */
	static void getIdrangeLimits(s3f::dml::Configuration* rcfg, int& low, int& high);

	/** Return all hosts in this level of net, including routers and switches.*/
	S3FNET_INT2PTR_MAP& getHosts() { return hosts; }

	/** Return all subnets of this level of net. */
	S3FNET_INT2PTR_MAP& getNets() { return nets; }

	/** Return all links in this level of net. */
	S3FNET_POINTER_VECTOR& getLinks() { return links; }

	/** Return global name resolution service. */
	NameService* getNameService();

	/** Return the network interface object by giving the NHI of the interface */
	NetworkInterface* nicnhi_to_obj(S3FNET_STRING nhi_str);

	/** Register local host with given name.
	 *  This method should be called in the config phase of each connected interface.
	 */
	void register_host(Host* host, const S3FNET_STRING& name, bool isOpenVZEmu);

	/**
	 * Register local interface with the given IPADDR.
	 * This method should be called in the config phase of each connected interface.
	 */
	void register_interface(NetworkInterface* iface, IPADDR ip, bool isOpenVZEmu);

	/**
	 * Ask some information about the current network
	 * For some queries, if the current network does not know the
	 * answer, it will forward the question to its parent network.
	 *
	 * [ctrltyp = NET_CTRL_GET_TRAFFIC] get the traffic manager from the top net;
	 * for a sub-network the query will be forwarded to its parent network.
	 * ctrlmsg should be a pointer to a pointer to a Traffic object.
	 * The control method returns 1 if found; 0 otherwise.
	 */
	int control(int ctrltyp, void* ctrlmsg);

	/** return pointer of the top net */
	Net* getTopNet() {return top_net;}

#ifdef OPENVZ_EMULATION
	/**
	 * Inject the timestamped emulation events (mostly packets sent from VEs)
	 * into the simulator (through waitFor()), at the end of emulation cycle.
	 */
	void injectEmuEvents();

	/** Return the pointer to the SimCtrl object */
	SimCtrl* getSimCtrl() { return sim_ctrl; }

	/**
	 * Vector of VEHandle.
	 * VEHandle contains basic information/reference of a virtual-time-enabled VE
	 */
	vector<VEHandle*> ve;

	/**
	 * Return the ID base of VE.
	 * Note that the VEs number have to be continuous, e.g., 100, 101, 102, base is 100.
	 */
	int get_idbase(){return idbase;}

	/** Indicate if the openvz_emulation is supported as specified in DML topnet */
	bool is_openvz_emu(){return top_net->isOpenVZEmu;}

	/** Return the number of VE, specified in DML topnet */
	int get_nr_ve(){return nr_ve;}
#endif

	/** Return the type of openflow controller.
	 *  0 means non-entity-based controller, and 1 means entity-based controller.
	 */
	int openflow_controller_type() { return sim_iface->openflow_controller_type; }

	/** Return the model of openflow controller.
	 *  E.g., 1 - non-entity-based learning switch, 3 - entity-based learning switch. Refer to enum OPENFLOW_CONTROLLER_MODEL in openflow_interface.h
	 */
	int openflow_controller_model() { return sim_iface->openflow_controller_model; }

	/** Return the pointer to the non-entity-based controller */
	Controller* get_of_controller() { return top_net->of_controller; }

	/** Add lock to access openflow buffer in openflow library (topnet only attribute). */
	pthread_mutex_t of_buf_mutex; //temp solution, todo: should be one buffer per switch

protected:
	SimInterface* sim_iface; ///< the interface for running experiments; created in s3fnet.cc
	int alignment; ///< the timeline that the net (and all its hosts) are aligned to
	SimCtrl* sim_ctrl; ///< to control openvz-based emulation
	int idbase; ///< ID base of VE, also the first VE id
	int nr_ve; ///< numbers of VEs */
	bool isOpenVZEmu; ///< whether openvz-based emulation is supported

	// for parsing real packet from openVZ emulation
	struct sniff_ethernet *ethernet; ///< The Ethernet header
	const struct sniff_ip *ip; ///< The IP header
	const struct sniff_arp *arp; ///< The ARP header
	const struct sniff_tcp *tcp; ///< The TCP header
	const char *payload; ///< Packet payload
	u_int size_ip; ///< size of IP header
	u_int size_tcp; ///< size of TCP header

	/**
	 * The extra options for a net.
	 * Not all Net objects should own some attributes like as ASBoundary (whether this net is
	 * at the boudary of an AS), AS number, etc.
	 */
	NetAccessory* netacc;

	/** IP prefix of this net. */
	IPPrefix ip_prefix;

	/** List of hosts: a map from id to a pointer of the host. */
	S3FNET_INT2PTR_MAP hosts;

	/** List of sub-networks: a map from id to a pointer of the network. */
	S3FNET_INT2PTR_MAP nets;

	/** List of links. */
	S3FNET_POINTER_VECTOR links;

	/** Used by the config methods to refer the cidr block of the
      current network it is processing. */
	CidrBlock* curCidr;

	/** indicator on whether this net object is the top net */
	bool is_top_net;

	/* top net, for accessing nicnhi_to_obj_map and hostnhi_to_obj_map */
	Net* top_net;

	/** The global name resolution service, created at topnet, passed to all subnets */
	NameService* namesvc;

	Traffic* traffic; //topnet only

	/** The non-entity-based openflow controller, created at topnet during initialization. */
	Controller* of_controller; //topnet only

	/** Configure the top net. */
	void config_top_net(s3f::dml::Configuration* cfg);

	/** Configure a link. */
	void config_link(s3f::dml::Configuration* cfg);

	/** Configure a host. */
	void config_host(s3f::dml::Configuration* cfg, S3FNET_LONG_SET& uniqset, bool is_router=false, bool is_of_switch=false);

	/** Configure a router. */
	void config_router(s3f::dml::Configuration* cfg, S3FNET_LONG_SET& uniqset);

	/** Configure a openflow switch, currently simply indicate that the host is an openflow switch. */
	void config_of_switch(s3f::dml::Configuration* cfg, S3FNET_LONG_SET& uniqset);

	/** Configure a sub network. */
	void config_net(s3f::dml::Configuration* cfg, S3FNET_LONG_SET& uniqset);

	/**
	 * This function is a helper function for the configure
	 * function. Once the configure method has read in all the hosts,
	 * routers and links, this function will go through the links and
	 * connect the interfaces.
	 */
	void connect_links();

	/**
	 * Process configurations that can't be done until all
	 * hosts/nets/links have been configured.
	 */
	void finish_config_top_net(s3f::dml::Configuration* cfg);

	/** load forwarding tables of all the hosts containing in this net */
	void load_fwdtable(s3f::dml::Configuration* cfg);

#ifdef OPENVZ_EMULATION
	Host* findOpenVZEmuHost(int veid_src); ///< Find the Host object from VE ID
	IPADDR getDstIPFromRealPacket(unsigned char* data); ///< extract the destination IP address from a real packet
	IPADDR getSrcIPFromRealPacket(unsigned char* data); ///< extract the source IP address from a real packet
	int process_ARP_packet(EmuPacket *ppkt); ///< send ARP packet to its destination VE immediately
	void printEthernetHeader(); ///< print Ethernet Src and Dst MAC address
#endif
	long int pkt_from_ve_counter; ///< store the total number of packet received from VEs

};

/**
 * \brief Extra options associated with a net.
 *
 * We observe that not all net objects should own some attributes like
 * as AS boundary (whether this net is at the boundary of an AS), AS
 * number, etc. This class provides a uniform interface for these
 * attributes.
 */
class NetAccessory {
public:
	/** The constructor. */
	NetAccessory()  {}

	/** The destructor. */
	~NetAccessory();

	/** Add an attribute of bool type. */
	void addAttribute(byte at, bool value);

	/** Add an attribute of int type. */
	void addAttribute(byte at, int value);

	/** Add an attribute of double type. */
	void addAttribute(byte at, double value);

	/** Add an attribute of pointer type. */
	void addAttribute(byte at, void* value);

	/** Request an attribute of bool type. */
	bool requestAttribute(byte at, bool& value);

	/** Request an attribute of int type. */
	bool requestAttribute(byte at, int& value);

	/** Request an attribute of double type. */
	bool requestAttribute(byte at, double& value);

	/** Request an attribute of pointer type. */
	bool requestAttribute(byte at, void*& value);

protected:
	/**
	 * \internal
	 * A common attribute in Net.
	 */
	struct NetAttribute {
		/** The attribute name. */
		byte name;
		/** The attribute value. */
		union {
			bool bool_val;     ///< boolean type value
			int int_val;       ///< integer type value
			double double_val; ///< double type value
			void* pointer_val; ///< pointer type value
		} value;

		/** The destructor. */
		~NetAttribute();
	};

	typedef S3FNET_VECTOR(NetAttribute*) NET_ATTRIB_VECTOR;

	/** List of attributes. */
	NET_ATTRIB_VECTOR netAttribList;
};

}; // namespace s3fnet
}; // namespace s3f

#endif /*__NET_H__*/
