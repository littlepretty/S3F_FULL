/**
 * \file openVZEmu_event_session.h
 *
 * \brief Header file for the OpenVZEventSession class.
 *
 * authors : Dong (Kevin) Jin
 */

#ifndef __OPENVZEMU_EVENT_INPUT_SESSION_H__
#define __OPENVZEMU_EVENT_INPUT_SESSION_H__

#include "os/base/protocol_session.h"
#include "os/ipv4/ip_session.h"
#include "util/shstl.h"
#include "s3fnet.h"

namespace s3f {
namespace s3fnet {

#define OPENVZ_EVENT_PROTOCOL_CLASSNAME "S3F.OS.OPENVZ_EVENT"

/**
 * \brief A protocol layer to interact with the associated OpenVZ-based VE.
 * In particular, transmit and receive emulation packet from/to the VE.
 */
class OpenVZEventSession : public ProtocolSession {

 public:
  /** the constructor */
  OpenVZEventSession(ProtocolGraph* graph);

  /** the destructor */
  virtual ~OpenVZEventSession();

  /**
   * Return the protocol number.
   * This protocol number will be stored as the index to the protocol graph.
   * Each protocol must have a unique protocol number
   */
  virtual int getProtocolNumber() { return S3FNET_PROTOCOL_TYPE_OPENVZ_EVENT; }

  /** called to configure this protocol session */
  virtual void config(s3f::dml::Configuration* cfg);

  /** called after config() to initialize this protocol session */
  virtual void init();

  /** called by other protocol sessions to send special control messages */
  virtual int control(int ctrltyp, void* ctrlmsg, ProtocolSession* sess);

  /**
   * Handle packet reception from the VE.
   * Called by the protocol session above to push a protocol message down the protocol stack
   */
  virtual int push(Activation msg, ProtocolSession* hi_sess, void* extinfo = 0, size_t extinfo_size = 0);

  /**
   * Handle packet transmission to the VE.
   * Called by the protocol session below to pop a protocol message up the protocol stack.
   */
  virtual int pop(Activation msg, ProtocolSession* lo_sess, void* extinfo = 0, size_t extinfo_size = 0);

  /** the S3F process used by waitFor() function in the OpenVZEventSession protocol, provide call_back functionality */
  Process* callback_proc;

  /** the callback function registered with the callback_proc */
  void callback(Activation);

  /** the actual body of the callback function */
  void callback_body(EmuPacket*, IPADDR, IPADDR);

  /**
   *  Storing the data for the callback function.
   *  In this case, the OpenVZEventSession session object is stored.
   */
  ProtocolCallbackActivation* dcac;
  
  /**
   * Inject an openvz-based emulation packet into the s3fnet simulator.
   * Schedule an waitFor() event for the packet receiving event.
   */
  void injectEvent(IPADDR dst_ip, IPADDR src_ip, EmuPacket* ppkt, int priority);

  /** the IP layer below this protocol */
  IPSession* ip_session;

  /** store the total number of packet output to VE */
  long int pkt_to_ve_counter;
};

/**
 *  Storing the data for the callback function.
 *  In this case, the OpenVZEventSession object, destination IP address and the emulation packet are stored.
 */
class OpenVZEventSessionCallbackActivation : public ProtocolCallbackActivation
{
  public:
	OpenVZEventSessionCallbackActivation(ProtocolSession* sess, IPADDR _dst_ip, IPADDR _src_ip, EmuPacket* ppkt);
	virtual ~OpenVZEventSessionCallbackActivation(){}

	IPADDR dst_ip; ///< the destination IP address
	IPADDR src_ip; ///< the source IP address
	EmuPacket* packet; ///< pointer to the emulation packet
	int iface_id;
};

}; // namespace s3fnet
}; // namespace s3f

#endif /*__OPENVZEMU_EVENT_INPUT_SESSION_H__*/
