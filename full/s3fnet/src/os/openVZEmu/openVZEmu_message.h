/**
 * \file openVZEmu_message.h
 * \brief Header file for the OpenVZMessage class.
 *
 * authors : Dong (Kevin) Jin
 */

#ifndef __OPENVZEMU_MESSAGE_H__
#define __OPENVZEMU_MESSAGE_H__

#include "os/base/protocol_message.h"
#include "util/shstl.h" // defines S3FNET_STRING type, which is a replacement of std::string
#include "s3fnet.h"

namespace s3f {
namespace s3fnet {

/**
 * \brief Protocol message for the OpenVZEventSession.
 *
 * Define the OpenVZMessage protocol message, which contains a pointer to the EmuPacket
 * carried in the message and the the network interface to receive the packet in the VE
 * proxy node in S3FNet (the interface of the VE that the packet will be delivered to).
 */
class OpenVZMessage : public ProtocolMessage {
 public:
  /** the default constructor */
  OpenVZMessage();

#ifdef OPENVZ_EMULATION
  /** the copy constructor */
  OpenVZMessage(const OpenVZMessage& msg);
#endif
  /** the constructor with EmuPacket */
  OpenVZMessage(EmuPacket* packet);

 protected:
  /**
   * The destructor is protected to avoid direct deletion;
   * erase() should be called upon reclaiming a protocol message.
   */
  virtual ~OpenVZMessage(){}

 public:
  /** Clone the protocol message (as required by the ProtocolMessage base class). */
  virtual ProtocolMessage* clone() { return new OpenVZMessage(*this); }

  /** Return the protocol type that the message represents. */
  virtual int type() { return S3FNET_PROTOCOL_TYPE_OPENVZ_EVENT; }

  /**
   * This method returns the number of bytes that are needed to
   * pack this message. In particular, add the length of the EmuPacket.
   */
  virtual int packingSize();

  /** Return the number of bytes used by the protocol message on a real network */
  //virtual int realByteCount() { return packingSize(); }
  virtual int realByteCount() { return ppkt->len; }

  /** Pointer to the EmuPacket carried in the message, containing the real packet */
  EmuPacket* ppkt;

  /**
   * To support OpenVZEmu, the network interface to receive the packet in the VE proxy node in S3FNet,
   * i.e. the interface of the VE that the packet will be delivered to
   */
  int ifid;
};

}; // namespace s3fnet
}; // namespace s3f

#endif /*__OPENVZEMU_MESSAGE_H__*/
