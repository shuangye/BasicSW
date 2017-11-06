#ifndef __TCP_H__
#define __TCP_H__

#include <stdlib.h>
#include <osa.h>


#ifdef __cplusplus
extern "C" {
#endif

typedef struct NET_DataChunk {
    Int32       type;
    Uint32     *pLength;    /* when passing in, specify the available buffer length; when passing out, specify the actual (received) data length */
    void       *pData;
} NET_DataChunk;


typedef void* NET_Handle;
typedef int (*NET_DataHandler)(NET_DataChunk *pDataChunk);

int NET_init(int port, NET_Handle *handle);

int NET_deinit(NET_Handle handle);

int NET_tcpConnect(NET_Handle handle, const char *remoteIpAddr, const int remotePort);

int NET_tcpSend(NET_Handle handle, const NET_DataChunk *pDataChunk);

int NET_tcpRecv(NET_Handle handle, NET_DataChunk *pDataChunk);

int NET_tcpServer(NET_Handle handle, NET_DataHandler handler);

int NET_tcpClient(NET_Handle handle);

int NET_tcpTest(const int role);

#ifdef __cplusplus
}
#endif

#endif  /* __TCP_H__ */
