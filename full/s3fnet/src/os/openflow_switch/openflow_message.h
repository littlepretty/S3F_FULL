/**
 * \file openflow_message.h
 * \brief Header file for the OpenFlowMessage class.
 *
 * authors : Dong (Kevin) Jin
 */

#ifndef __OPENFLOW_MESSAGE_H__
#define __OPENFLOW_MESSAGE_H__

#include "os/base/protocol_message.h"
#include "util/shstl.h"
#include "s3fnet.h"
#include "os/openflow_switch/openflow_interface.h"

namespace s3f {
namespace s3fnet {

class OpenFlowSwitchSession;

/**
 *  Message between OpenFlow controller and OpenFlow switch, for both directions.
 *  Not used by entity-based controllers.
 */

class OpenFlowMessage : public ProtocolMessage {
public:
	/** the default constructor */
	OpenFlowMessage();

	/** the constructor used for packet from switch to controller */
	OpenFlowMessage(ofpbuf* buf, uint32_t pkt_uid, OpenFlowSwitchSession* snd_switch);

	/** the constructor used for packet from controller to switch */
	OpenFlowMessage(ofp_flow_mod* msg, uint16_t len, OpenFlowSwitchSession* snd_switch, ofpbuf* buf, uint32_t pkt_uid);
	OpenFlowMessage(ofp_flow_mod* msg, uint16_t len);

	/** the copy constructor */
	OpenFlowMessage(const OpenFlowMessage& msg);

protected:
	/** the destructor is protected to avoid direct deletion; erase() should be called upon reclaiming a protocol message */
	virtual ~OpenFlowMessage();

public:
	virtual ProtocolMessage* clone() { return new OpenFlowMessage(*this); }

	/** each protocol message must have a unique type */
	virtual int type() { return S3FNET_PROTOCOL_TYPE_OPENFLOW_CONTROLLER; }

	/** return the buffer size needed to serialize this protocol message */
	virtual int packingSize();

	/** return the number of bytes used by the protocol message on a real network */
	virtual int realByteCount();

	ofpbuf* buffer;
	uint32_t packet_uid;

	ofp_flow_mod* ofm;
	uint16_t length;

	OpenFlowSwitchSession* sender; ///< the OpenFlow switch that sends the packet
};

}; // namespace s3fnet
}; // namespace s3f

#endif /*__OPENFLOW_MESSAGE_H__*/
