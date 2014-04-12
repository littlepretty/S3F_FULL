/** \file asyncmsg.h
 * \brief header file for the asynchronous message handler
 *
 * authors : Yuhao Zheng
 */

#ifndef __VTIME_ASYNCMSG_H__
#define __VTIME_ASYNCMSG_H__

#include <s3f.h>


/**
 * Class for an asynchronous message.
 * This class is a single message, for master-slave communication.
 */
class AsyncMessage
{
public:
	/** message type enumeration */
	enum type_t { SUCC, FAIL, TEST, SETIP, EXEC, INIT, FINI, CLOSE, PACKET, CYCLE };

	/** constructor */
	AsyncMessage();

	/** destructor */
	~AsyncMessage();

	/** resize the message and reallocate memory */
	int resize(int size);
	
	/** message type */
	type_t type;

	/** timestamp */
	ltime_t time;

	/** integer field 1 */
	int int1;
	
	/** integer field 2 */
	int int2;

	/** length of payload, can be 0 */
	int payloadlen;

	/** message payload */
	char *payload;
};


/**
 * Class for an asynchronous message handler.
 * This class handles all the socket operation between master and slaves.
 */
class AsyncMessageHandler
{
public:
	/** constructor */
	AsyncMessageHandler();

	/** destructor */
	~AsyncMessageHandler();

	/** is the network socket currently connected? */
	inline bool isconnected() { return connected; }

	/** socket listen, for slave side */
	int amh_connect();

	/** socket connect, for master side */
	int amh_connect(const char *address);

	/** close socket connection */
	int amh_close();

	/** send a message */
	int amh_send(const AsyncMessage *msg);

	/** receive a message (non-blocking) */
	int amh_recv(AsyncMessage **pmsg);

private:
	/** header length of a async message */
	static const int HDRLEN = sizeof(AsyncMessage) - sizeof(char*);
	
	/** flag: is the TCP socket connected? */
	bool connected;

	/** TCP socket */
	int tcpfd;
};

#endif /* __VTIME_ASYNCMSG_H__ */

