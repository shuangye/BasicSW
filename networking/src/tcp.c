#include "tcp.h"
#include <netinet/in.h>    /* for sockaddr_in */
#include <sys/types.h>     /* for socket */
#include <sys/socket.h>    /* for socket */
#include <arpa/inet.h>
#include <stdio.h>         
#include <stdlib.h>        
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <osa.h>

#define NET_MAX_DATA_CHUNK_SIZE    (1 << 20)    /* 1MB */


typedef struct NET_Node {
    int        sd;    /* socket descriptor */
    int        port;
} NET_Node;


typedef struct NET_MessageHeader {
    Int32       dataType;
    Uint32      length;    /* body length */
} NET_MessageHeader;


int gClientPort = 8999;
int gServerPort = 9000;
const char *gpServerIpAddr = "127.0.0.1";




int NET_init(int port, NET_Handle *pHandle)
{
    int ret;
    NET_Node *pNode = NULL;
    struct sockaddr_in socketAddr;    


    pNode = malloc(sizeof(*pNode));
    if (NULL == pNode) {
        OSA_error("Failed to allocate memory.\n");
        ret = OSA_STATUS_ENOMEM;
        goto _failure;
    }

    pNode->sd = socket(PF_INET, SOCK_STREAM, 0);
    if (pNode->sd < 0) {
        ret = errno;
        OSA_error("Creating socket failed with %d.\n", ret);
        goto _failure;
    }

    int opt = 1;
    setsockopt(pNode->sd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    bzero(&socketAddr,sizeof(socketAddr));
    socketAddr.sin_family              = AF_INET;
    socketAddr.sin_addr.s_addr         = htons(INADDR_ANY);
    socketAddr.sin_port                = htons(port);
    ret = bind(pNode->sd, (struct sockaddr*)&socketAddr, sizeof(socketAddr));
    if (0 != ret) {
        ret = errno;
        OSA_error("Binding on port %d failed with %d.\n", port, ret);         
        goto _failure;
    }

    pNode->port = port;
    *pHandle = (NET_Handle)pNode;
    return OSA_STATUS_OK;

_failure:
    if (NULL != pNode) {
        if (pNode->sd >= 0) {
            close(pNode->sd);
        }
        free(pNode);
    }
    return ret;
}


int NET_deinit(NET_Handle handle)
{
    NET_Node *pNode = (NET_Node *)handle;

    if (NULL == pNode) {        
        return OSA_STATUS_OK;
    }

    if (pNode->sd >= 0) {
        close(pNode->sd);
    }

    free(pNode);

    return 0;
}


/* for client side */
int NET_tcpConnect(NET_Handle handle, const char *pRemoteIpAddr, const int remotePort)
{
    /*
     socket, connect, send, recv, close
     */

    int ret;
    NET_Node *pNode = (NET_Node *)handle;
    struct sockaddr_in serverSocketAddr;              /* where are you going to connect */    
    

    if (NULL == pNode || NULL == pRemoteIpAddr) {
        OSA_error("Invalid parameter.\n");
        return OSA_STATUS_EINVAL;
    }
    
    bzero(&serverSocketAddr,sizeof(serverSocketAddr));
    serverSocketAddr.sin_family  = AF_INET;    
    serverSocketAddr.sin_port    = htons(remotePort);

    ret = inet_aton(pRemoteIpAddr, &serverSocketAddr.sin_addr);
    if (0 == ret) {
        OSA_error("Invalid remote IP address %s.\n", pRemoteIpAddr);
        ret = OSA_STATUS_EINVAL;
        goto _return;
    }

    ret = connect(pNode->sd, (const struct sockaddr *)&serverSocketAddr, sizeof(serverSocketAddr));
    if (0 != ret) {
        OSA_error("Connecting to server %s:%d failed with %d.\n", pRemoteIpAddr, remotePort, errno);
        ret = errno;
        goto _return;
    }
    
    ret = OSA_STATUS_OK;
    
_return:
    return ret;
}


int NET_tcpSend(NET_Handle handle, const NET_DataChunk *pDataChunk)
{
    int ret = OSA_STATUS_OK;
    NET_Node *pNode;
    NET_MessageHeader header;
    char *p;
    Uint32 sentSize;    


    pNode = (NET_Node *)handle;
    if (NULL == pNode || NULL == pDataChunk || NULL == pDataChunk->pLength) {
        OSA_error("Invalid parameter.\n");
        return OSA_STATUS_EINVAL;
    }

    header.dataType = pDataChunk->type;
    header.length = *(pDataChunk->pLength);
    ret = send(pNode->sd, &header, sizeof(header), 0);
    if (ret < sizeof(header)) {
        ret = errno;
        OSA_error("Sending header failed with %d.\n", ret);
        return ret;
    }

    for (sentSize = 0, p = (char*)pDataChunk->pData; sentSize < header.length; sentSize += ret, p += ret) {
       ret = send(pNode->sd, p, header.length - sentSize, 0);
       if (ret < 0) {
           OSA_error("Sending failed with %d.\n", errno);
           ret = errno;
           break;
       }
       OSA_debug("Sent %d bytes.\n", ret);
   }

   if (header.length == sentSize) {
       ret = OSA_STATUS_OK;
   }

   return ret;
}


int NET_tcpRecv(NET_Handle handle, NET_DataChunk *pDataChunk)
{
    int ret = OSA_STATUS_OK;    
    NET_Node *pNode = NULL;
    NET_MessageHeader header;
    char *p;
    size_t receivedSize;    


    pNode = (NET_Node *)handle;
    
    if (NULL == pNode || NULL == pDataChunk|| NULL == pDataChunk->pLength) {
        OSA_error("Invalid parameter.\n");
        return OSA_STATUS_EINVAL;
    }
    
    /* CAUTION: What if the header and body misorders? */

    /* receive the header */
    bzero(&header, sizeof(header));
    ret = recv(pNode->sd, &header, sizeof(header), 0);
    if (0 == ret && 0 == errno) {
        return OSA_STATUS_ESHUTDOWN;  /* the other side has closed the socket */
    }
    else if (ret < sizeof(header)) {
        ret = errno;
        OSA_error("No header received: %d\n", ret);
        return ret;
    }

    OSA_debug("Received %d bytes header.\n", ret);

    /* parse the header */
    if (header.length > *(pDataChunk->pLength)) {
        OSA_error("Message length %u is greater than the pBuffer supplied %u.\n", 
            header.length, *(pDataChunk->pLength));
        return OSA_STATUS_EINVAL;
    }

    /* then receive the body */
    for (receivedSize = 0, p = pDataChunk->pData; receivedSize < header.length; receivedSize += ret, p += ret) {
        ret = recv(pNode->sd, p, header.length - receivedSize, 0);
        if (ret < 0) {
            ret = errno;
            OSA_error("Receiving failed with %d.\n", ret);
            break;
        }        
    }

    OSA_debug("Received %lu bytes body.\n", receivedSize);

    if (receivedSize == header.length) {
        ret = OSA_STATUS_OK;
    }

    pDataChunk->type = header.dataType;
    *(pDataChunk->pLength) = receivedSize;
    return ret;
}


int NET_tcpClient(NET_Handle handle)
{
    int ret;
    NET_Node *pNode = (NET_Node *)handle;
    NET_DataChunk  dataChunk;
    long long data = 0x12345678;
    Int32 responseCode = -1;
    Uint32 length;
    

    ret = NET_tcpConnect(handle, gpServerIpAddr, gServerPort);
    if (OSA_isFailed(ret)) {
        OSA_error("Connecting to server %s:%d failed with %d.\n", gpServerIpAddr, gServerPort, ret);
        return ret;
    }
    
    /* send data */
    length = sizeof(data);
    dataChunk.type = 1;
    dataChunk.pLength = &length;
    dataChunk.pData = &data;    
    ret = NET_tcpSend(handle, &dataChunk);
    if (OSA_isFailed(ret)) {
        OSA_error("Sending data to server failed: %d.\n", ret);
        return ret;
    }

    length = sizeof(responseCode);
    dataChunk.pData = &responseCode;
    dataChunk.pLength = &length;
    ret = NET_tcpRecv(handle, &dataChunk);
    if (OSA_isFailed(ret)) {
        OSA_error("Receiving response code from server failed: %d.\n", ret);
        return ret;
    }

    OSA_info("Server responsed status code %d.\n", responseCode);

    return ret;
}


int NET_tcpServer(NET_Handle handle, NET_DataHandler handler)
{ 
    /*
     socket, bind, listen, accept, recv, send, close
     */

    int ret;
    Int32 statusCode;
    NET_Node *pNode = (NET_Node *)handle;
    NET_Node acceptedNode = { 0 };
    NET_DataChunk receivedDataChunk;
    Uint32 receivedDataSize;
    const int kMaxConnectionCount = 16;
    int connectionSd;                                 /* connection with a client */
    struct sockaddr_in clientSocketAddr;              /* where the connection comes from */
    socklen_t sockAddrLength = sizeof(clientSocketAddr);


    if (NULL == pNode) {
        OSA_error("Invalid parameter.\n");
        return OSA_STATUS_EINVAL;
    }

    ret = listen(pNode->sd, kMaxConnectionCount);
    if (0 != ret) {
        OSA_error("Failed to listen: %d.\n", errno);
        return errno;
    }

    OSA_info("Server listening on port %d...\n", pNode->port);

    receivedDataSize = NET_MAX_DATA_CHUNK_SIZE;
    receivedDataChunk.pLength = &receivedDataSize;
    receivedDataChunk.pData = malloc(receivedDataSize);
    if (NULL == receivedDataChunk.pData) {
        OSA_error("Failed to allocate memory for storage of reception.\n");
        return OSA_STATUS_ENOMEM;
    }

    /* keep listening */
    while (1) { 
        /* 
          接受一个到server_socket代表的socket的一个连接
          如果没有连接请求,就等待到有连接请求--这是accept函数的特性
          accept函数返回一个新的socket,这个socket(connectionSd)用于同连接到的客户的通信
          connectionSd代表了服务器和客户端之间的一个通信通道
          accept函数把连接到的客户端信息填写到客户端的socket地址结构clientSocketAddr中
         */
        connectionSd = accept(pNode->sd, (struct sockaddr*)&clientSocketAddr, &sockAddrLength);
        if (connectionSd < 0) {
            OSA_error("Server accept failed with %d.\n", errno);
            continue;
        }

        /* keep receiving from the same socket, until closed by the client */
        while (1) {
            acceptedNode.sd = connectionSd;
            ret = NET_tcpRecv(&acceptedNode, &receivedDataChunk);
            if (OSA_STATUS_ESHUTDOWN == ret) {                
                OSA_info("Connection %d closed by client.\n", connectionSd);
                close(connectionSd);  /* server closes the socket too */
                break;
            }
            else if (OSA_isFailed(ret)) {
                OSA_error("Receiving failed with %d.\n", ret);
                continue;
            }

            OSA_info("Received %u bytes from client %s:%hu\n", 
                *receivedDataChunk.pLength, inet_ntoa(clientSocketAddr.sin_addr), ntohs(clientSocketAddr.sin_port));
                
            if (NULL != handler) {
                statusCode = handler(&receivedDataChunk);
            }
            else {
                statusCode = OSA_STATUS_OK;
            }

            NET_DataChunk responseChunk;
            Uint32 length = sizeof(statusCode);
            responseChunk.type = 0;
            responseChunk.pData = &statusCode;
            responseChunk.pLength = &length;
            ret = NET_tcpSend(&acceptedNode, &responseChunk);
            if (OSA_isFailed(ret)) {
                OSA_warn("Sending status code to client failed with %d.\n", ret);
            }
        }
    }

    free(receivedDataChunk.pData);    
    return OSA_STATUS_OK;
}


int NET_tcpTest(const int role)
{
    NET_Handle handle;

        
    if (1 == role) {  /* client */
        NET_init(gClientPort, &handle);
        NET_tcpClient(handle);
    }
    else if (2 == role) {  /* server */
        NET_init(gServerPort, &handle);
        NET_tcpServer(handle, NULL);
    }

    NET_deinit(handle);

    return OSA_STATUS_OK;
}

