#ifndef __TCP_H__
#define __TCP_H__

#ifdef __cplusplus
extern "C" {
#endif


typedef void* NET_Handle;

int NET_init(int port, NET_Handle *handle);

int NET_deinit(NET_Handle handle);

int NET_tcpServer(NET_Handle handle);

int NET_tcpClient(NET_Handle handle, const char *remoteIpAddr, const int remotePort);

int NET_tcpTest(const int role);

#ifdef __cplusplus
}
#endif

#endif  /* __TCP_H__ */
