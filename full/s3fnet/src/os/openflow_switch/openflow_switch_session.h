/**
 * \file openflow_switch_session.h
 * \brief Header file for the OpenFlowSwitchSession class.
 *
 * authors : Dong (Kevin) Jin
 */

#ifndef __OPENFLOW_SWITCH_SESSION_H__
#define __OPENFLOW_SWITCH_SESSION_H__

#include "openflow_interface.h"
#include "os/base/protocol_session.h"
#include "util/shstl.h"
#include "net/ip_prefix.h"
#include "net/mac48_address.h"

namespace s3f {
namespace s3fnet {

class Controller;
class IPSession;
class NetworkInterface;
class OpenFlowControllerSession;

#define OF_SWITCH_PROTOCOL_CLASSNAME "S3F.OS.OPENFLOW_SWITCH"

/**
 *
 * \brief An OpenFlow switch that switches multiple LAN segments via an OpenFlow-compatible flow table
 *
 * The OpenFlowSwitchSession object aggregates multiple NetworkInterface as ports and acts like a switch.
 * It implements OpenFlow-compatibility, according to the OpenFlow Switch Specification v0.8.9
 * <www.openflowswitch.org/documents/openflow-spec-v0.8.9.pdf>.
 * It implements a flow table that all received packets are run through.
 * It implements a connection to a controller via a subclass of the Controller class,
 * which can send messages to manipulate the flow table, thereby manipulating how the OpenFlow switch behaves.
 */

class OpenFlowSwitchSession : public ProtocolSession
{
public:

	/**
	 * \name OpenFlowSwitchSession Description Data
	 * \brief These four data describe the OpenFlowSwitchSession as if it were a real OpenFlow switch.
	 *
	 * There is a type of stats request that OpenFlow switches are supposed
	 * to handle that returns the description of the OpenFlow switch. Currently
	 * manufactured by "The S3F team", software description is "Simulated
	 * OpenFlow Switch", and the other two are "N/A".
	 */
	static const char * GetManufacturerDescription ();
	static const char * GetHardwareDescription ();
	static const char * GetSoftwareDescription ();
	static const char * GetSerialNumber ();

	/** the constructor must be take an argument of the protocol graph */
	OpenFlowSwitchSession (ProtocolGraph* graph);

	/** the destructor */
	virtual ~OpenFlowSwitchSession ();

	/** each protocol must have a unique protocol number */
	virtual int getProtocolNumber() { return S3FNET_PROTOCOL_TYPE_OPENFLOW_SWITCH; }

	/** called to configure this protocol session */
	virtual void config(s3f::dml::Configuration* cfg);

	/** called after config() to initialize this protocol session */
	virtual void init();

	/** called by other protocol sessions to send special control messages */
	virtual int control(int ctrltyp, void* ctrlmsg, ProtocolSession* sess);

	/** called by the protocol session above to push a protocol message down the protocol stack */
	virtual int push(Activation msg, ProtocolSession* hi_sess, void* extinfo = 0, std::size_t extinfo_size = 0);

	/** called by the protocol session below to pop a protocol message up the protocol stack */
	virtual int pop(Activation msg, ProtocolSession* lo_sess, void* extinfo = 0, size_t extinfo_size = 0);

	/**
	 * \brief Set up the Switch's controller connection.
	 */
	void SetController ();

	/**
	 * \brief Add a 'port' to a switch device
	 *
	 * This method adds a new switch port to a OpenFlowSwitchSession, so that
	 * the new switch port (NetworkInterface) becomes part of the switch and L2
	 * frames start being forwarded to/from this NetworkInterface.
	 *
	 * \param switchPort The port to add.
	 * \return 0 if everything's ok, otherwise an error number.
	 */
	int AddSwitchPort (NetworkInterface* switchPort);

	/**
	 * \brief Add a virtual port to a switch device
	 *
	 * The Ericsson OFSID has the concept of virtual ports and virtual
	 * port tables. These are implemented in the OpenFlowSwitchSession, but
	 * don't have an understood use [perhaps it may have to do with
	 * MPLS integration].
	 *
	 *
	 * \param ovpm The data for adding a virtual port.
	 * \return 0 if everything's ok, otherwise an error number.
	 */
	int AddVPort (const ofp_vport_mod *ovpm);

	/**
	 * \brief Stats callback is ready for a dump.
	 *
	 * Controllers have a callback system for status requests which calls this function.
	 *
	 * \param cb_ The callback data.
	 * \return 0 if everything's ok, otherwise an error number.
	 */
	int StatsDump (StatsDumpCallback *cb_);

	/**
	 * \brief Stats callback is done.
	 *
	 * Controllers have a callback system for status requests which calls this function.
	 *
	 * \param cb_ The callback data.
	 */
	void StatsDone (StatsDumpCallback *cb_);

	/**
	 * \brief Called from the OpenFlow Interface to output the Packet on either a Port or the Controller
	 *
	 * \param packet_uid Packet UID; used to fetch the packet and its metadata.
	 * \param in_port The index of the port the Packet was initially received on.
	 * \param max_len The maximum number of bytes the caller wants to be sent; a value of 0 indicates the entire packet should be sent. Used when outputting to controller.
	 * \param out_port The port we want to output on.
	 * \param ignore_no_fwd If true, Ports that are set to not forward are forced to forward.
	 */
	void DoOutput (uint32_t packet_uid, int in_port, size_t max_len, int out_port, bool ignore_no_fwd);

	/**
	 * \brief The registered controller calls this method when sending a message to the switch.
	 *
	 * \param msg The message received from the controller.
	 * \param length Length of the message.
	 * \return 0 if everything's ok, otherwise an error number.
	 */
	int ForwardControlInput (const void *msg, size_t length);

	/* Used to handle the flow inquiry response from controller to switch  */
	virtual void HandleControlInput (Activation pkt);

	/**
	 * \return The flow table chain.
	 */
	sw_chain* GetChain ();

	/**
	 * \return Number of switch ports attached to this switch.
	 */
	uint32_t GetNSwitchPorts (void) const;

	/**
	 * \param p The Port to get the index of.
	 * \return The index of the provided Port.
	 */
	int GetSwitchPortIndex (Port p);

	/**
	 * \param n index of the Port.
	 * \return The Port.
	 */
	Port GetSwitchPort (uint32_t n) const;

	/**
	 * \return The virtual port table.
	 */
	vport_table_t GetVPortTable ();

	///\name From NetDevice
	//\{
	//virtual void SetAddress (Address address);
	//virtual Address GetAddress (void) const;
	virtual bool SetMtu (const uint16_t mtu);
	virtual uint16_t GetMtu (void) const;
	virtual bool IsLinkUp (void) const;
	//virtual void AddLinkChangeCallback (Callback<void> callback);
	//virtual bool IsBroadcast (void) const;
	//virtual Address GetBroadcast (void) const;
	//virtual bool IsMulticast (void) const;
	//virtual bool IsPointToPoint (void) const;
	//virtual bool IsBridge (void) const;
	//virtual bool Send (ProtocolMessage* packet, const Mac48Address& dest, uint16_t protocolNumber);
	//virtual bool SendFrom (ProtocolMessage* packet, const Mac48Address& source, const Mac48Address& dest, uint16_t protocolNumber);
	//virtual Ptr<Node> GetNode (void) const;
	//virtual void SetNode (Ptr<Node> node);
	//virtual bool NeedsArp (void) const;
	//virtual void SetReceiveCallback (NetDevice::ReceiveCallback cb);
	//virtual void SetPromiscReceiveCallback (NetDevice::PromiscReceiveCallback cb);
	//virtual bool SupportsSendFrom () const;
	//\}

	//to support controller session, i.e. a controller is a S3F Host Entity with a controller protocol session
	NetworkInterface* ofctrl_port; ///< the NetworkInterface of the OpenFlow controller
	bool is_ctrl_sess; ///< whether it connects to an entity-based controller instead of non-entity controller
	S3FNET_STRING ctrl_nhi; ///< the nhi of the controller entity
	Host* ctrl_host; ///< the Host* of the active controller
	OpenFlowControllerSession* ctrl_sess; ///< pointer of the controller protocol session

protected:
	virtual void DoDispose (void);

	/**
	 * \internal
	 *
	 * Takes a packet and generates an OpenFlow buffer from it, loading the packet data as well as its headers.
	 */
	ofpbuf * BufferFromPacket (ProtocolMessage* packet);

private:
	/**
	 * \internal
	 *
	 * Add a flow.
	 *
	 * \param ofm The flow data to add.
	 * \return 0 if everything's ok, otherwise an error number.
	 */
	int AddFlow (const ofp_flow_mod *ofm);

	/**
	 * \internal
	 *
	 * Modify a flow.
	 *
	 * \param ofm The flow data to modify.
	 * \return 0 if everything's ok, otherwise an error number.
	 */
	int ModFlow (const ofp_flow_mod *ofm);

	/**
	 * \internal
	 *
	 * Send packets out all the ports except the originating one
	 *
	 * \param packet_uid Packet UID; used to fetch the packet and its metadata.
	 * \param in_port The index of the port the Packet was initially received on. This port doesn't forward when flooding.
	 * \param flood If true, don't send out on the ports with flooding disabled.
	 * \return 0 if everything's ok, otherwise an error number.
	 */
	int OutputAll (uint32_t packet_uid, int in_port, bool flood);

	/**
	 * \internal
	 *
	 * Sends a copy of the Packet over the provided output port
	 *
	 * \param packet_uid Packet UID; used to fetch the packet and its metadata.
	 * \param out_port the port that the switch will forward/send a packet out.
	 */
	void OutputPacket (uint32_t packet_uid, int out_port);

	/**
	 * \internal
	 *
	 * Seeks to send out a Packet over the provided output port. This is called generically
	 * when we may or may not know the specific port we're outputting on. There are many
	 * pre-set types of port options besides a Port that's hooked to our OpenFlowSwitchSession.
	 * For example, it could be outputting as a flood, or seeking to output to the controller.
	 *
	 * \param packet_uid Packet UID; used to fetch the packet and its metadata.
	 * \param in_port The index of the port the Packet was initially received on.
	 * \param out_port The port we want to output on.
	 * \param ignore_no_fwd If true, Ports that are set to not forward are forced to forward.
	 */
	void OutputPort (uint32_t packet_uid, int in_port, int out_port, bool ignore_no_fwd);

	/**
	 * \internal
	 *
	 * Sends a copy of the Packet to the controller. If the packet can be saved
	 * in an OpenFlow buffer, then only the first 'max_len' bytes of the packet
	 * are sent; otherwise, all of the packet is sent.
	 *
	 * \param packet_uid Packet UID; used to fetch the packet and its metadata.
	 * \param in_port The index of the port the Packet was initially received on.
	 * \param max_len The maximum number of bytes that the caller wants to be sent; a value of 0 indicates the entire packet should be sent.
	 * \param reason Why the packet is being sent.
	 */
	void OutputControl (uint32_t packet_uid, int in_port, size_t max_len, int reason);

	/**
	 * \internal
	 *
	 * If an error message happened during the controller's request, send it to the controller.
	 *
	 * \param type The type of error.
	 * \param code The error code.
	 * \param data The faulty data that lead to the error.
	 * \param len The length of the faulty data.
	 */
	void SendErrorMsg (uint16_t type, uint16_t code, const void *data, size_t len);

	/**
	 * \internal
	 *
	 * Send a reply about this OpenFlow switch's features to the controller.
	 *
	 * List of capabilities and actions to support are found in the specification
	 * <www.openflowswitch.org/documents/openflow-spec-v0.8.9.pdf>.
	 *
	 * Supported capabilities and actions are defined in the openflow interface.
	 * To recap, flow status, flow table status, port status, virtual port table
	 * status can all be requested. It can also transmit over multiple physical
	 * interfaces.
	 *
	 * It supports every action: outputting over a port, and all of the flow table
	 * manipulation actions: setting the 802.1q VLAN ID, the 802.1q priority,
	 * stripping the 802.1 header, setting the Ethernet source address and destination,
	 * setting the IP source address and destination, setting the TCP/UDP source address
	 * and destination, and setting the MPLS label and EXP bits.
	 *
	 * \attention Capabilities STP (Spanning Tree Protocol) and IP packet
	 * reassembly are not currently supported.
	 *
	 */
	void SendFeaturesReply ();

	/**
	 * \internal
	 *
	 * Send a reply to the controller that a specific flow has expired.
	 *
	 * \param flow The flow that expired.
	 * \param reason The reason for sending this expiration notification.
	 */
	void SendFlowExpired (sw_flow *flow, enum ofp_flow_expired_reason reason);

	/**
	 * \internal
	 *
	 * Send a reply about a Port's status to the controller.
	 *
	 * \param p The port to get status from.
	 * \param status The reason for sending this reply.
	 */
	void SendPortStatus (Port p, uint8_t status);

	/**
	 * \internal
	 *
	 * Send a reply about this OpenFlow switch's virtual port table features to the controller.
	 */
	void SendVPortTableFeatures ();

	/**
	 * \internal
	 *
	 * Send a message to the controller. This method is the key
	 * to communicating with the controller, it does the actual
	 * sending. The other Send methods call this one when they
	 * are ready to send a message.
	 *
	 * \param buffer Buffer of the message to send out.
	 * \return 0 if successful, otherwise an error number.
	 */
	int SendOpenflowBuffer (ofpbuf *buffer);

	/**
	 * \internal
	 *
	 * Run the packet through the flow table. Looks up in the flow table for a match.
	 * If it doesn't match, it forwards the packet to the registered controller, if the flag is set.
	 *
	 * \param packet_uid Packet UID; used to fetch the packet and its metadata.
	 * \param port The port this packet was received over.
	 * \param send_to_controller If set, sends to the controller if the packet isn't matched.
	 */
	void RunThroughFlowTable (uint32_t packet_uid, int port, bool send_to_controller = true);

	/**
	 * \internal
	 *
	 * Run the packet through the vport table. As with AddVPort,
	 * this doesn't have an understood use yet.
	 *
	 * \param packet_uid Packet UID; used to fetch the packet and its metadata.
	 * \param port The port this packet was received over.
	 * \param vport The virtual port this packet identifies itself by.
	 * \return 0 if everything's ok, otherwise an error number.
	 */
	int RunThroughVPortTable (uint32_t packet_uid, int port, uint32_t vport);

	/**
	 * \internal
	 *
	 * Called by RunThroughFlowTable on a scheduled delay
	 * to account for the flow table lookup overhead.
	 *
	 * \param key Matching key to look up in the flow table.
	 * \param buffer Buffer of the packet received.
	 * \param packet_uid Packet UID; used to fetch the packet and its metadata.
	 * \param port The port the packet was received over.
	 * \param send_to_controller
	 */
	void FlowTableLookup (sw_flow_key key, ofpbuf* buffer, uint32_t packet_uid, int port, bool send_to_controller);

	/**
	 * \internal
	 *
	 * Update the port status field of the switch port.
	 * A non-zero return value indicates some field has changed.
	 *
	 * \param p A reference to a Port; used to change its config and flag fields.
	 * \return true if the status of the Port is changed, false if unchanged (was already the right status).
	 */
	int UpdatePortStatus (Port& p);

	/**
	 * \internal
	 *
	 * Fill out a description of the switch port.
	 *
	 * \param p The port to get the description from.
	 * \param desc A pointer to the description message; used to fill the description message with the data from the port.
	 */
	void FillPortDesc (Port p, ofp_phy_port *desc);

	/**
	 * \internal
	 *
	 * Generates an OpenFlow reply message based on the type.
	 *
	 * \param openflow_len Length of the reply to make.
	 * \param type Type of reply message to make.
	 * \param bufferp Message buffer; used to make the reply.
	 * \return The OpenFlow reply message.
	 */
	void* MakeOpenflowReply (size_t openflow_len, uint8_t type, ofpbuf **bufferp);

	/**
	 * \internal
	 * \name Receive Methods
	 *
	 * Actions to do when a specific OpenFlow message/packet is received
	 *
	 * \param msg The OpenFlow message received.
	 * \return 0 if everything's ok, otherwise an error number.
	 */
	int ReceiveFeaturesRequest (const void *msg);
	int ReceiveGetConfigRequest (const void *msg);
	int ReceiveSetConfig (const void *msg);
	int ReceivePacketOut (const void *msg); //kevin: have a data copy here, can fix?
	int ReceiveFlow (const void *msg);
	int ReceivePortMod (const void *msg);
	int ReceiveStatsRequest (const void *oh);
	int ReceiveEchoRequest (const void *oh);
	int ReceiveEchoReply (const void *oh);
	int ReceiveVPortMod (const void *msg);
	int ReceiveVPortTableFeaturesRequest (const void *msg);

	Mac48Address* m_address;              ///< Address of this device. (the base MAC address, should not be used as for now)
	uint16_t m_mtu;                       ///< Maximum Transmission Unit

	typedef std::map<uint32_t,SwitchPacketMetadata> PacketData_t;
	PacketData_t m_packetData;            ///< Packet data

	typedef std::vector<Port> Ports_t;
	Ports_t m_ports;                      ///< Switch's ports

	Controller* np_controller;    		///< Connection to the non-entity controller; controller is not implemented as a protocol session, but as direct function calls

	uint64_t m_id;                        ///< Unique identifier for this switch, needed for OpenFlow
	//ltime_t m_lookupDelay;              ///< Flow Table Lookup Delay [overhead].

	ltime_t m_lastExecute;                ///< Last time the periodic execution occurred.
	uint16_t m_flags;                     ///< Flags; configurable by the controller.
	uint16_t m_missSendLen;               ///< Flow Table Miss Send Length; configurable by the controller.

	sw_chain *m_chain;             ///< Flow Table; forwarding rules.
	vport_table_t m_vportTable;    ///< Virtual Port Table

	static const uint16_t DEFAULT_MTU = 1500;
};

}; // namespace s3fnet
}; // namespace s3f

#endif /* __OPENFLOW_SWITCH_SESSION_H__ */
