#include "universal_gnss_transport/udp_client_transport.hpp"
#if defined(__linux__)
#include <cerrno>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
namespace universal_gnss_transport { namespace { bool Wait(int fd, short events, std::uint32_t timeout) { if (!timeout) return true; pollfd p{fd,events,0}; return ::poll(&p,1,static_cast<int>(timeout))>0 && (p.revents&events); } }
UdpClientTransport::~UdpClientTransport(){ Close(); }
TransportError UdpClientTransport::Open(const UdpClientConfig& c) { Close(); if(c.host.empty()||!c.port) return metrics_.last_error=TransportError::kInvalidArgument; addrinfo h{};h.ai_socktype=SOCK_DGRAM; addrinfo* r=nullptr; if(::getaddrinfo(c.host.c_str(),std::to_string(c.port).c_str(),&h,&r)||!r) return metrics_.last_error=TransportError::kConnectFailure; for(auto p=r;p;p=p->ai_next){int f=::socket(p->ai_family,p->ai_socktype,p->ai_protocol); if(f>=0&&::connect(f,p->ai_addr,p->ai_addrlen)==0){fd_=f;config_=c;::freeaddrinfo(r);return metrics_.last_error=TransportError::kNone;} if(f>=0)::close(f);}::freeaddrinfo(r);return metrics_.last_error=TransportError::kConnectFailure; }
ReadResult UdpClientTransport::Read(std::uint8_t* d,std::size_t n){ if(fd_<0){NoteReadError(metrics_,TransportError::kClosed);return{0,TransportStatus::kClosed,TransportError::kClosed};}if(!n)return{};if(!d){NoteReadError(metrics_,TransportError::kInvalidArgument);return{0,TransportStatus::kError,TransportError::kInvalidArgument};}if(!config_.nonblocking&&!Wait(fd_,POLLIN,config_.read_timeout_ms))return{}; iovec i{d,n};msghdr m{};m.msg_iov=&i;m.msg_iovlen=1;ssize_t z=::recvmsg(fd_,&m,MSG_TRUNC);if(z<0){if(errno==EAGAIN||errno==EWOULDBLOCK)return{};NoteReadError(metrics_,TransportError::kReadFailure);return{0,TransportStatus::kError,TransportError::kReadFailure};}if((m.msg_flags&MSG_TRUNC)||static_cast<std::size_t>(z)>n){NoteReadError(metrics_,TransportError::kOverflow);return{0,TransportStatus::kError,TransportError::kOverflow};}NoteReadBytes(metrics_,z);return{static_cast<std::size_t>(z),TransportStatus::kOk,TransportError::kNone}; }
WriteResult UdpClientTransport::Write(const std::uint8_t*d,std::size_t n){if(fd_<0){NoteWriteError(metrics_,TransportError::kClosed);return{0,TransportStatus::kClosed,TransportError::kClosed};}if(!n)return{};if(!d){NoteWriteError(metrics_,TransportError::kInvalidArgument);return{0,TransportStatus::kError,TransportError::kInvalidArgument};}ssize_t z=::send(fd_,d,n,MSG_NOSIGNAL);if(z!=static_cast<ssize_t>(n)){NoteWriteError(metrics_,TransportError::kWriteFailure);return{0,TransportStatus::kError,TransportError::kWriteFailure};}NoteWrittenBytes(metrics_,n);return{n,TransportStatus::kOk,TransportError::kNone};}
bool UdpClientTransport::IsOpen()const{return fd_>=0;}void UdpClientTransport::Close(){if(fd_>=0){::close(fd_);fd_=-1;}}const TransportMetrics& UdpClientTransport::metrics()const{return metrics_;}
}
#endif
