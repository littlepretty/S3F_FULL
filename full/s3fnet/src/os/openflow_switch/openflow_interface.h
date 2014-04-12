/**
 * \file openflow_interface.h
 * \brief Interfaces for interaction among openflow switches and controllers, as well as interacting with openflow library.
 *
 * authors : Dong (Kevin) Jin
 */

#ifndef __OPENFLOW_INTERFACE_H__
#define __OPENFLOW_INTERFACE_H__

#include <errno.h>
#include <limits>

// Include main header and Vendor Extension files
#include "openflow/openflow.h"
#include "openflow/nicira-ext.h"
#include "openflow/ericsson-ext.h"

extern "C"
{
// Inexplicably, the OpenFlow implementation uses these two reserved words as member names.
#define private _private
#define delete _delete
#define list List

// Include OFSI Library files
#include "openflow/private/csum.h"
#include "openflow/private/poll-loop.h"
#include "openflow/private/rconn.h"
#include "openflow/private/stp.h"
#include "openflow/private/vconn.h"
#include "openflow/private/xtoxll.h"

// Include OFSI Switch files
#include "openflow/private/chain.h"
#include "openflow/private/table.h"
#include "openflow/private/datapath.h" // The functions below are defined in datapath.c
uint32_t save_buffer (ofpbuf *);
ofpbuf * retrieve_buffer (uint32_t id);
void discard_buffer (uint32_t id);
#include "openflow/private/dp_act.h" // The functions below are defined in dp_act.c
void set_vlan_vid (ofpbuf *buffer, sw_flow_key *key, const ofp_action_header *ah);
void set_vlan_pcp (ofpbuf *buffer, sw_flow_key *key, const ofp_action_header *ah);
void strip_vlan (ofpbuf *buffer, sw_flow_key *key, const ofp_action_header *ah);
void set_dl_addr (ofpbuf *buffer, sw_flow_key *key, const ofp_action_header *ah);
void set_nw_addr (ofpbuf *buffer, sw_flow_key *key, const ofp_action_header *ah);
void set_tp_port (ofpbuf *buffer, sw_flow_key *key, const ofp_action_header *ah);
void set_mpls_label (ofpbuf *buffer, sw_flow_key *key, const ofp_action_header *ah);
void set_mpls_exp (ofpbuf *buffer, sw_flow_key *key, const ofp_action_header *ah);
#include "openflow/private/pt_act.h" // The function below is defined in pt_act.c
void update_checksums (ofpbuf *buffer, const sw_flow_key *key, uint32_t old_word, uint32_t new_word);

#undef list
#undef private
#undef delete
}

// Capabilities supported by this implementation.
#define OFP_SUPPORTED_CAPABILITIES ( OFPC_FLOW_STATS            \
                                     | OFPC_TABLE_STATS         \
                                     | OFPC_PORT_STATS          \
                                     | OFPC_MULTI_PHY_TX        \
                                     | OFPC_VPORT_TABLE)

// Actions supported by this implementation.
#define OFP_SUPPORTED_ACTIONS ( (1 << OFPAT_OUTPUT)             \
                                | (1 << OFPAT_SET_VLAN_VID)     \
                                | (1 << OFPAT_SET_VLAN_PCP)     \
                                | (1 << OFPAT_STRIP_VLAN)       \
                                | (1 << OFPAT_SET_DL_SRC)       \
                                | (1 << OFPAT_SET_DL_DST)       \
                                | (1 << OFPAT_SET_NW_SRC)       \
                                | (1 << OFPAT_SET_NW_DST)       \
                                | (1 << OFPAT_SET_TP_SRC)       \
                                | (1 << OFPAT_SET_TP_DST)       \
                                | (1 << OFPAT_SET_MPLS_LABEL)   \
                                | (1 << OFPAT_SET_MPLS_EXP) )

#define OFP_SUPPORTED_VPORT_TABLE_ACTIONS ( (1 << OFPPAT_OUTPUT)                \
                                            | (1 << OFPPAT_POP_MPLS)            \
                                            | (1 << OFPPAT_PUSH_MPLS)           \
                                            | (1 << OFPPAT_SET_MPLS_LABEL)      \
                                            | (1 << OFPPAT_SET_MPLS_EXP) )      \

namespace s3f {
namespace s3fnet {

class NetworkInterface;
class OpenFlowSwitchSession;
class ProtocolMessage;
class Mac48Address;

/** Model of OpenFlow controller.  Ending wit h_ENTITY means entity-based controller, otherwise non-entity-based controller. */
enum OPENFLOW_CONTROLLER_MODEL
{
	NO_OF_CONTROLLER = 0,
	OF_LEARNING_SWITCH_CONTROLLER = 1,
	OF_DROP_CONTROLLER = 2,
	OF_LEARNING_SWITCH_CONTROLLER_ENTITY = 3,
};

/**
 * \brief Port and its metadata.
 *
 * We need to store port metadata, because OpenFlow dictates that there
 * exists a type of request where the Controller asks for data about a
 * port, or multiple ports.
 */
struct Port
{
  Port () : config (0),
            state (0),
            netdev (0),
            rx_packets (0),
            tx_packets (0),
            rx_bytes (0),
            tx_bytes (0),
            tx_dropped (0),
            mpls_ttl0_dropped (0)
  {}

  uint32_t config;            ///< Some subset of OFPPC_* flags.
  uint32_t state;             ///< Some subset of OFPPS_* flags.
  NetworkInterface* netdev;   ///< the corresponding NetworkInterface of the port
  unsigned long long int rx_packets, tx_packets;
  unsigned long long int rx_bytes, tx_bytes;
  unsigned long long int tx_dropped;
  unsigned long long int mpls_ttl0_dropped;
};

class Stats
{
public:
  Stats (ofp_stats_types _type, size_t body_len);

  /**
   * \brief Prepares to dump some kind of statistics on the connected OpenFlowSwitchSession.
   *
   * \param body Body member of the struct ofp_stats_request.
   * \param body_len Length of the body member.
   * \param state State information.
   * \return 0 if successful, otherwise a negative error code.
   */
  int DoInit (const void *body, int body_len, void **state);

  /**
   * \brief Appends statistics for OpenFlowSwitchSession to 'buffer'.
   *
   * \param swtch The OpenFlowSwitchSession this callback is associated with.
   * \param state State information.
   * \param buffer Buffer to append stats reply to.
   * \return 1 if it should be called again later with another buffer, 0 if it is done, or a negative errno value on failure.
   */
  int DoDump (OpenFlowSwitchSession* swtch, void *state, ofpbuf *buffer);

  /**
   * \brief Cleans any state created by the init or dump functions.
   *
   * May not be implemented if no cleanup is required.
   *
   * \param state State information to clear.
   */
  void DoCleanup (void *state);

  /**
   * \brief State of the FlowStats request/reply.
   */
  struct FlowStatsState
  {
    int table_idx;
    sw_table_position position;
    ofp_flow_stats_request rq;
    time_t now;

    ofpbuf *buffer;
  };

  /**
   * \brief State of the PortStats request/reply.
   */
  struct PortStatsState
  {
    uint32_t num_ports; ///< Number of ports in host byte order
    uint32_t *ports;    ///< Array of ports in network byte order
  };

  ofp_stats_types type;
private:
  int DescStatsDump (void *state, ofpbuf *buffer);

  int FlowStatsInit (const void *body, int body_len, void **state);
  int (*FlowDumpCallback)(sw_flow *flow, void *state);
  int FlowStatsDump (OpenFlowSwitchSession* dp, FlowStatsState *s, ofpbuf *buffer);

  int AggregateStatsInit (const void *body, int body_len, void **state);
  int (*AggregateDumpCallback)(sw_flow *flow, void *state);
  int AggregateStatsDump (OpenFlowSwitchSession* dp, ofp_aggregate_stats_request *s, ofpbuf *buffer);

  int TableStatsDump (OpenFlowSwitchSession* dp, void *state, ofpbuf *buffer);

  int PortStatsInit (const void *body, int body_len, void **state);
  int PortStatsDump (OpenFlowSwitchSession* dp, PortStatsState *s, ofpbuf *buffer);

  int PortTableStatsDump (OpenFlowSwitchSession* dp, void *state, ofpbuf *buffer);
};

/**
 * \brief Class for handling flow table actions.
 */
struct Action
{
  /**
   * \param type Type of Flow Table Action.
   * \return true if the provided type is a type of flow table action.
   */
  static bool IsValidType (ofp_action_type type);

  /**
   * \brief Validates the action on whether its data is valid or not.
   *
   * \param type Type of action to validate.
   * \param len Length of the action data.
   * \param key Matching key for the flow that is tied to this action.
   * \param ah Action's data header.
   * \return ACT_VALIDATION_OK if the action checks out, otherwise an error type.
   */
  static uint16_t Validate (ofp_action_type type, size_t len, const sw_flow_key *key, const ofp_action_header *ah);

  /**
   * \brief Executes the action.
   *
   * \param type Type of action to execute.
   * \param buffer Buffer of the Packet if it's needed for the action.
   * \param key Matching key for the flow that is tied to this action.
   * \param ah Action's data header.
   */
  static void Execute (ofp_action_type type, ofpbuf *buffer, sw_flow_key *key, const ofp_action_header *ah);
};

/**
 * \brief Class for handling virtual port table actions.
 */
struct VPortAction
{
  /**
   * \param type Type of virtual port table Action.
   * \return true if the provided type is a type of virtual port table action.
   */
  static bool IsValidType (ofp_vport_action_type type);

  /**
   * \brief Validates the action on whether its data is valid or not.
   *
   * \param type Type of action to validate.
   * \param len Length of the action data.
   * \param ah Action's data header.
   * \return ACT_VALIDATION_OK if the action checks out, otherwise an error type.
   */
  static uint16_t Validate (ofp_vport_action_type type, size_t len, const ofp_action_header *ah);

  /**
   * \brief Executes the action.
   *
   * \param type Type of action to execute.
   * \param buffer Buffer of the Packet if it's needed for the action.
   * \param key Matching key for the flow that is tied to this action.
   * \param ah Action's data header.
   */
  static void Execute (ofp_vport_action_type type, ofpbuf *buffer, const sw_flow_key *key, const ofp_action_header *ah);
};

/**
 * \brief Class for handling Ericsson Vendor-defined actions.
 */
struct EricssonAction
{
  /**
   * \param type Type of Ericsson Vendor-defined Action.
   * \return true if the provided type is a type of Ericsson Vendor-defined action.
   */
  static bool IsValidType (er_action_type type);

  /**
   * \brief Validates the action on whether its data is valid or not.
   *
   * \param type Type of action to validate.
   * \param len Length of the action data.
   * \return ACT_VALIDATION_OK if the action checks out, otherwise an error type.
   */
  static uint16_t Validate (er_action_type type, size_t len);

  /**
   * \brief Executes the action.
   *
   * \param type Type of action to execute.
   * \param buffer Buffer of the Packet if it's needed for the action.
   * \param key Matching key for the flow that is tied to this action.
   * \param ah Action's data header.
   */
  static void Execute (er_action_type type, ofpbuf *buffer, const sw_flow_key *key, const er_action_header *ah);
};

/**
 * \brief Callback for a stats dump request.
 */
struct StatsDumpCallback //kevin: need to look at details
{
  bool done;                            ///< Whether we are done requesting stats.
  ofp_stats_request *rq;                ///< Current stats request.
  Stats *s;                             ///< Handler of the stats request.
  void *state;                          ///< Stats request state data.
  OpenFlowSwitchSession* swtch;   ///< The switch that we're requesting data from.
};

/**
 * \brief Packet Metadata, allows us to track the packet's metadata as it passes through the switch.
 */
struct SwitchPacketMetadata //kevin: need to replace packet with ProtocolMessage
{
  ProtocolMessage* packet; ///< The Packet itself.
  ofpbuf* buffer;               ///< The OpenFlow buffer as created from the Packet, with its data and headers.
  uint16_t protocolNumber;      ///< Protocol type of the Packet when the Packet is received
  Mac48Address* src;             ///< Source Address of the Packet when the Packet is received
  Mac48Address* dst;             ///< Destination Address of the Packet when the Packet is received.
};


/**
 * \brief Executes a list of flow table actions.
 *
 * \param swtch OpenFlowSwitchSession these actions are being executed on.
 * \param packet_uid Packet UID; used to fetch the packet and its metadata.
 * \param buffer The Packet OpenFlow buffer.
 * \param key The matching key for the flow tied to this list of actions.
 * \param actions A buffer of actions.
 * \param actions_len Length of actions buffer.
 * \param ignore_no_fwd If true, during port forwarding actions, ports that are set to not forward are forced to forward.
 */
void ExecuteActions (OpenFlowSwitchSession* swtch, uint64_t packet_uid, ofpbuf* buffer, sw_flow_key *key, const ofp_action_header *actions, size_t actions_len, int ignore_no_fwd);

/**
 * \brief Validates a list of flow table actions.
 *
 * \param key The matching key for the flow tied to this list of actions.
 * \param actions A buffer of actions.
 * \param actions_len Length of actions buffer.
 * \return If the action list validates, ACT_VALIDATION_OK is returned. Otherwise, a code for the OFPET_BAD_ACTION error type is returned.
 */
uint16_t ValidateActions (const sw_flow_key *key, const ofp_action_header *actions, size_t actions_len);

/**
 * \brief Executes a list of virtual port table entry actions.
 *
 * \param swtch OpenFlowSwitchSession these actions are being executed on.
 * \param packet_uid Packet UID; used to fetch the packet and its metadata.
 * \param buffer The Packet OpenFlow buffer.
 * \param key The matching key for the flow tied to this list of actions.
 * \param actions A buffer of actions.
 * \param actions_len Length of actions buffer.
 */
void ExecuteVPortActions (OpenFlowSwitchSession* swtch, uint64_t packet_uid, ofpbuf* buffer, sw_flow_key *key, const ofp_action_header *actions, size_t actions_len);

/**
 * \brief Validates a list of virtual port table entry actions.
 *
 * \param actions A buffer of actions.
 * \param actions_len Length of actions buffer.
 * \return If the action list validates, ACT_VALIDATION_OK is returned. Otherwise, a code for the OFPET_BAD_ACTION error type is returned.
 */
uint16_t ValidateVPortActions (const ofp_action_header *actions, size_t actions_len);

/**
 * \brief Executes a vendor-defined action.
 *
 * \param buffer The Packet OpenFlow buffer.
 * \param key The matching key for the flow tied to this list of actions.
 * \param ah Header of the action.
 */
void ExecuteVendor (ofpbuf *buffer, const sw_flow_key *key, const ofp_action_header *ah);

/**
 * \brief Validates a vendor-defined action.
 *
 * \param key The matching key for the flow tied to this list of actions.
 * \param ah Header of the action.
 * \param len Length of the action.
 * \return If the action list validates, ACT_VALIDATION_OK is returned. Otherwise, a code for the OFPET_BAD_ACTION error type is returned.
 */
uint16_t ValidateVendor (const sw_flow_key *key, const ofp_action_header *ah, uint16_t len);

/* 
 * From datapath.c
 * Buffers are identified to userspace by a 31-bit opaque ID.  We divide the ID
 * into a buffer number (low bits) and a cookie (high bits).  The buffer number
 * is an index into an array of buffers.  The cookie distinguishes between
 * different packets that have occupied a single buffer.  Thus, the more
 * buffers we have, the lower-quality the cookie...
 */
#define PKT_BUFFER_BITS 8
#define N_PKT_BUFFERS (1 << PKT_BUFFER_BITS)
#define PKT_BUFFER_MASK (N_PKT_BUFFERS - 1)
#define PKT_COOKIE_BITS (32 - PKT_BUFFER_BITS)

}; // namespace s3fnet
}; // namespace s3f

#endif /* __OPENFLOW_INTERFACE_H__ */
