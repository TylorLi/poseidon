#include "../precompiled.hpp"
#include "tcp_client_base.hpp"
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include "exception.hpp"
#include "log.hpp"
using namespace Poseidon;

namespace {

void socketConnect(ScopedFile &client, const std::string &ip, unsigned port){
	union {
		::sockaddr sa;
		::sockaddr_in sin;
		::sockaddr_in6 sin6;
	} u;
	::socklen_t salen = sizeof(u);

	if(::inet_pton(AF_INET, ip.c_str(), &u.sin.sin_addr) == 1){
		u.sin.sin_family = AF_INET;
		u.sin.sin_port = htons(port);
		salen = sizeof(::sockaddr_in);
	} else if(::inet_pton(AF_INET6, ip.c_str(), &u.sin6.sin6_addr) == 1){
		u.sin6.sin6_family = AF_INET6;
		u.sin6.sin6_port = htons(port);
		salen = sizeof(::sockaddr_in6);
	} else {
		DEBUG_THROW(Exception, "Unknown address format. IP expected.");
	}

	client.reset(::socket(u.sa.sa_family, SOCK_STREAM, IPPROTO_TCP));
	if(!client){
		DEBUG_THROW(SystemError, errno);
	}
	if(::connect(client.get(), &u.sa, salen) != 0){
		DEBUG_THROW(SystemError, errno);
	}
}

}

TcpClientBase::TcpClientBase(const std::string &ip, unsigned port)
	: TcpSessionBase()
{
	ScopedFile client;
	socketConnect(client, ip, port);
	TcpSessionBase::init(client);
}