/**
 * \file dummy_message.cc
 * \brief Source file for the RandomDummyMessage class.
 *
 * authors : Dong (Kevin) Jin
 */

#include "os/dummy/random_dummy_message.h"

namespace s3f {
namespace s3fnet {

S3FNET_REGISTER_MESSAGE(RandomDummyMessage, S3FNET_PROTOCOL_TYPE_RANDOM_DUMMY);

RandomDummyMessage::RandomDummyMessage() {}

RandomDummyMessage::RandomDummyMessage(const S3FNET_STRING& msg) :
  hello_message(msg) {}

RandomDummyMessage::RandomDummyMessage(const RandomDummyMessage& msg) :
  ProtocolMessage(msg), // the base class's copy constructor must be called
  hello_message(msg.hello_message) {}

RandomDummyMessage::~RandomDummyMessage(){}

int RandomDummyMessage::packingSize()
{
  // must add the parent class packing size
  int mysiz = ProtocolMessage::packingSize();

  // add the length of the message string (including the terminating null)
  mysiz += hello_message.length()+1;
  return mysiz;
}

int RandomDummyMessage::realByteCount()
{
	return hello_message.length();
}

}; // namespace s3fnet
}; // namespace s3f
