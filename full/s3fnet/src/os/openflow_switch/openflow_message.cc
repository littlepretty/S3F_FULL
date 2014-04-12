/**
 * \file openflow_message.cc
 * \brief Source file for the OpenFlowMessage class.
 *
 * authors : Dong (Kevin) Jin
 */

#include "os/openflow_switch/openflow_message.h"

namespace s3f {
namespace s3fnet {

S3FNET_REGISTER_MESSAGE(OpenFlowMessage, S3FNET_PROTOCOL_TYPE_OPENFLOW_CONTROLLER);

OpenFlowMessage::OpenFlowMessage() : buffer(0), packet_uid(-1), ofm(0), length(0), sender(0)
{}

OpenFlowMessage::OpenFlowMessage(ofpbuf* buf, uint32_t pkt_uid, OpenFlowSwitchSession* snd_switch) :
		  buffer(buf), packet_uid(pkt_uid), sender(snd_switch), ofm(0), length(0)
{}

OpenFlowMessage::OpenFlowMessage(ofp_flow_mod* msg, uint16_t len, OpenFlowSwitchSession* snd_switch, ofpbuf* buf, uint32_t pkt_uid) :
			buffer(buf), packet_uid(pkt_uid), sender(snd_switch), ofm(msg), length(len)
{}

OpenFlowMessage::OpenFlowMessage(ofp_flow_mod* msg, uint16_t len) :
			buffer(0), packet_uid(-1), sender(0), ofm(msg), length(len)
{}

OpenFlowMessage::OpenFlowMessage(const OpenFlowMessage& msg) :
		  ProtocolMessage(msg), // the base class's copy constructor must be called
		  buffer(msg.buffer), packet_uid(msg.packet_uid), sender(msg.sender),
		  ofm(msg.ofm), length(msg.length)
{}

OpenFlowMessage::~OpenFlowMessage(){}

int OpenFlowMessage::packingSize()
{
	// must add the parent class packing size
	int mysiz = ProtocolMessage::packingSize();

	if(length > 0) //controller to switch msg
	{
		mysiz += length;
	}
	else //switch to controller msg
	{
		mysiz += buffer->size;
	}
	return mysiz;
}

int OpenFlowMessage::realByteCount()
{
	if(length > 0) //controller to switch msg
		return length;
	else //switch to controller msg
		return buffer->size;
}

}; // namespace s3fnet
}; // namespace s3f
