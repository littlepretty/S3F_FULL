/**
 * \file openVZEmu_message.cc
 *
 * \brief Source file for the OpenVZMessage class.
 *
 * authors : Dong (Kevin) Jin
 */

#include "os/openVZEmu/openVZEmu_message.h"
#include "util/errhandle.h"

namespace s3f {
namespace s3fnet {

S3FNET_REGISTER_MESSAGE(OpenVZMessage, S3FNET_PROTOCOL_TYPE_OPENVZ_EVENT);

OpenVZMessage::OpenVZMessage() {}

#ifdef OPENVZ_EMULATION
OpenVZMessage::OpenVZMessage(const OpenVZMessage& msg) :
  ProtocolMessage(msg) // the base class's copy constructor must be called
{
	ppkt = msg.ppkt->duplicate();
	ifid = msg.ifid;
}
#endif

OpenVZMessage::OpenVZMessage(EmuPacket* packet)
{
	ppkt = packet;
	ifid = 0;
}

int OpenVZMessage::packingSize()
{
  // must add the parent class packing size
  int mysiz = ProtocolMessage::packingSize();

  // add the length of the message string (including the terminating null)
  mysiz += ppkt->len;
  return mysiz;
}

}; // namespace s3fnet
}; // namespace s3f
