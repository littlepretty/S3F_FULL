/** \file asyncmsg.cc
 * \brief source file for the asynchronous message handler
 *
 * authors : Yuhao Zheng
 */

#include <simctrl/asyncmsg.h>

#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>


#define PORT 2013
#define BACKLOG 1
#define SOCKET_TIMEOUT 0


/* constructor */
AsyncMessage::AsyncMessage()
{
	bzero(this, sizeof(*this));
}


/* destructor */
AsyncMessage::~AsyncMessage()
{
	if (payload) delete payload;
}


/* resize the message and reallocate memory */
int AsyncMessage::resize(int size)
{
	if (size < 0) return -EINVAL;
	if (payload) delete payload;
	payload = NULL;
	if (!size) return 0;
	payload = new char[size];
	if (!payload) { payloadlen = 0; return -ENOMEM; }
	payloadlen = size;
	return 0;
}


/* constructor */
AsyncMessageHandler::AsyncMessageHandler()
{
	connected = false;
	tcpfd = 0;
}


/* destructor */
AsyncMessageHandler::~AsyncMessageHandler()
{
	if (connected) amh_close();
}


/* socket listen, for slave side */
int AsyncMessageHandler::amh_connect()
{
	if (connected) return -EEXIST;

	int sockfd, newsockfd;
	socklen_t clilen;
	struct sockaddr_in serv_addr, cli_addr;
	int reuseaddr = 1;

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) return -ENODEV;
	bzero(&serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = htons(PORT);
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(reuseaddr));
	if (bind(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) return -EACCES;
	if (listen(sockfd, BACKLOG) < 0) return -EIO;
	clilen = sizeof(cli_addr);
	newsockfd = accept(sockfd, (struct sockaddr*)&cli_addr, &clilen);
	close(sockfd);
	if (newsockfd < 0) return -EIO;

	int on = 1;
	setsockopt(newsockfd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));

	tcpfd = newsockfd;
	connected = true;
	return 0;
}


/* socket connect, for master side */
int AsyncMessageHandler::amh_connect(const char *hostname)
{
	if (connected) return -EEXIST;

	int sockfd;
	struct sockaddr_in serv_addr;
	struct hostent *server;

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) return -ENODEV;
	server = gethostbyname(hostname);
	if (!server) return -EBADF;
	bzero(&serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	bcopy(server->h_addr, &serv_addr.sin_addr.s_addr, server->h_length);
	serv_addr.sin_port = htons(PORT);
	if (connect(sockfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0) return -EIO;

	int on = 1;
	setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));

	tcpfd = sockfd;
	connected = true;
	return 0;
}


/* close socket connection */
int AsyncMessageHandler::amh_close()
{
	if (!connected) return -ENXIO;
	close(tcpfd);
	tcpfd = 0;
	connected = false;
	return 0;
}


/* send a message */
int AsyncMessageHandler::amh_send(const AsyncMessage *msg)
{
	if (!connected) return -ENXIO;

	int size = HDRLEN + msg->payloadlen;
	char *buf = new char[size];
	if (!buf) return -ENOMEM;

	memcpy(buf, msg, HDRLEN);
	if (msg->payloadlen) memcpy(buf + HDRLEN, msg->payload, msg->payloadlen);

	int sent = 0;
	while (sent < size) {
		int n = write(tcpfd, buf + sent, size - sent);
		if (n <= 0) { sched_yield(); continue; }
		sent += n;
	}

	delete buf;
	return 0;
}


/* receive a message (non-blocking) */
int AsyncMessageHandler::amh_recv(AsyncMessage **pmsg)
{
	if (!connected) return -ENXIO;

	fd_set rset;
	FD_ZERO(&rset);
	FD_SET(tcpfd, &rset);
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = SOCKET_TIMEOUT;
	int sel = select(tcpfd+1, &rset, NULL, NULL, &tv);
	if (sel <= 0) { *pmsg = NULL; return 0; }

	char buf[HDRLEN];
	int rcvd = read(tcpfd, buf, HDRLEN);
	if (rcvd <= 0) { *pmsg = NULL; return 0; }
	while (rcvd < HDRLEN) {
		int n = read(tcpfd, buf + rcvd, HDRLEN - rcvd);
		if (n <= 0) { sched_yield(); continue; }
		rcvd += n;
	}

	AsyncMessage *msg = new AsyncMessage;
	if (!msg) return -ENOMEM;
	memcpy(msg, buf, HDRLEN);
	*pmsg = msg;
	if (!msg->payloadlen) return 0;

	msg->payload = new char[msg->payloadlen];
	if (!msg->payload) return -ENOMEM;
	rcvd = 0;
	while (rcvd < msg->payloadlen) {
		int n = read(tcpfd, msg->payload + rcvd, msg->payloadlen - rcvd);
		if (n <= 0) { sched_yield(); continue; }
		rcvd += n;
	}

	return 0;
}

