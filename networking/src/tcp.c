#include "tcp.h"
#include <netinet/in.h>    // for sockaddr_in
#include <sys/types.h>     // for socket
#include <sys/socket.h>    // for socket
#include <arpa/inet.h>
#include <stdio.h>         
#include <stdlib.h>        
#include <string.h>        // for bzero
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <osa.h>


typedef struct NET_Node {
    int        sd;    /* socket descriptor */
    int        port;
} NET_Node;


int NET_init(int port, NET_Handle *handle)
{
    int ret;
    NET_Node *node = NULL;
    struct sockaddr_in socketAddr;    


    node = malloc(sizeof(*node));
    if (NULL == node) {
        OSA_error("Failed to allocate memory.\n");
        ret = OSA_STATUS_ENOMEM;
        goto _failure;
    }

    node->sd = socket(PF_INET, SOCK_STREAM, 0);
    if (node->sd < 0) {
        ret = errno;
        OSA_error("Creating socket failed with %d.\n", ret);
        goto _failure;
    }

    int opt = 1;
    setsockopt(node->sd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    bzero(&socketAddr,sizeof(socketAddr));
    socketAddr.sin_family              = AF_INET;
    socketAddr.sin_addr.s_addr         = htons(INADDR_ANY);
    socketAddr.sin_port                = htons(port);
    ret = bind(node->sd, (struct sockaddr*)&socketAddr, sizeof(socketAddr));
    if (0 != ret) {
        ret = errno;
        OSA_error("Binding on port %d failed with %d.\n", port, ret);         
        goto _failure;
    }

    node->port = port;
    *handle = (NET_Handle)node;
    return OSA_STATUS_OK;

_failure:
    if (NULL != node) {
        if (node->sd >= 0) {
            close(node->sd);
        }
        free(node);
    }
    return ret;
}


int NET_deinit(NET_Handle handle)
{
    NET_Node *node = (NET_Node *)handle;

    if (NULL == node) {        
        return OSA_STATUS_OK;
    }

    if (node->sd >= 0) {
        close(node->sd);
    }

    free(node);

    return 0;
}


int NET_tcpServer(NET_Handle handle)
{ 
    /*
     socket, bind, listen, accept, recv, send, close
     */

    int ret;
    NET_Node *node = (NET_Node *)handle;
    char *buffer;
    char *p;
    size_t receivedSize;
    const size_t bufferSize = 8 * (1 << 20);          /* 8MB */
    const int kMaxConnectionCount = 16;
    int connectionSd;                                 /* connection with a client */
    struct sockaddr_in clientSocketAddr;              /* where the connection comes from */
    socklen_t length = sizeof(clientSocketAddr);


    if (NULL == node) {
        OSA_error("Invalid parameter.\n");
        return OSA_STATUS_EINVAL;
    }

    ret = listen(node->sd, kMaxConnectionCount);
    if (0 != ret) {
        OSA_error("Failed to listen: %d.\n", errno);
        return errno;
    }

    OSA_info("Server listening on port %d...\n", node->port);

    buffer = malloc(bufferSize);
    if (NULL == buffer) {
        OSA_error("Failed to allocate memory for storage of reception.\n");
        return OSA_STATUS_ENOMEM;
    }

    while (1) { 
        //接受一个到server_socket代表的socket的一个连接
        //如果没有连接请求,就等待到有连接请求--这是accept函数的特性
        //accept函数返回一个新的socket,这个socket(connectionSd)用于同连接到的客户的通信
        //connectionSd代表了服务器和客户端之间的一个通信通道
        //accept函数把连接到的客户端信息填写到客户端的socket地址结构clientSocketAddr中
        connectionSd = accept(node->sd, (struct sockaddr*)&clientSocketAddr, &length);
        if (connectionSd < 0) {
            OSA_error("Server accept failed with %d.\n", errno);
            continue;
        }
        
        bzero(buffer, bufferSize);
        for (receivedSize = 0, p = buffer; receivedSize < bufferSize; receivedSize += ret, p += ret) {
            ret = recv(connectionSd, p, bufferSize - receivedSize, 0);
            if (ret < 0) {
                OSA_error("Server receiving data failed with %d.\n", errno);
                break;
            }
            OSA_info("Got data of size %d from client %s:%hu\n", 
               ret, inet_ntoa(clientSocketAddr.sin_addr), ntohs(clientSocketAddr.sin_port));
        }       
        
        sprintf(buffer, "Got your message.\n");
        ret = send(connectionSd, buffer, sizeof(buffer), 0);
        if (ret < 0) {
            OSA_error("Server response failed with %d.\n", errno);            
        }

        close(connectionSd);
    }

    free(buffer);    
    return OSA_STATUS_OK;
}


int NET_tcpClient(NET_Handle handle, const char *remoteIpAddr, const int remotePort)
{
    /*
     socket, connect, send, recv, close
     */

    int ret;
    NET_Node *node = (NET_Node *)handle;
    struct sockaddr_in serverSocketAddr;    /* where are you going to connect */
    char *data;
    char *p;
    const size_t dataSize = 8 * (1 << 20);          /* 8MB */
    size_t sentDataSize = 0;
    
    if (NULL == node || NULL == remoteIpAddr) {
        OSA_error("Invalid parameter.\n");
        return OSA_STATUS_EINVAL;
    }

    data = malloc(dataSize);
    if (NULL == data) {
        OSA_error("Failed to allocate memory for data.\n");
        return OSA_STATUS_ENOMEM;
    }
    
    bzero(&serverSocketAddr,sizeof(serverSocketAddr));
    serverSocketAddr.sin_family              = AF_INET;    
    serverSocketAddr.sin_port                = htons(remotePort);

    ret = inet_aton(remoteIpAddr, &serverSocketAddr.sin_addr);
    if (0 == ret) {
        OSA_error("Invalid remote IP address %s.\n", remoteIpAddr);
        ret = OSA_STATUS_EINVAL;
        goto _return;
    }

    ret = connect(node->sd, (const struct sockaddr *)&serverSocketAddr, sizeof(serverSocketAddr));
    if (0 != ret) {
        OSA_error("Connecting to server %s:%d failed with %d.\n", remoteIpAddr, remotePort, errno);
        ret = errno;
        goto _return;
    }

    /* data is not initialized */
    for (sentDataSize = 0, p = data; sentDataSize < dataSize; sentDataSize += ret, p += ret) {
        ret = send(node->sd, p, dataSize - sentDataSize, 0);
        if (ret < 0) {
            OSA_error("Sending to server failed with %d.\n", errno);
            ret = errno;
            goto _return;
        }
        OSA_info("Sent %d bytes to server.\n", ret);
    }

    OSA_info("All data sent to server.\n");

    bzero(data, dataSize);
    ret = recv(node->sd, data, dataSize, 0);
    if (ret < 0) {
        OSA_error("Receiving from server failed with %d.\n", errno);
        ret = errno;
        goto _return;
    }

    OSA_info("Got server response = %s\n", data);
    ret = OSA_STATUS_OK;
    
_return:
    free(data);
    return ret;
}


int NET_tcpTest(const int role)
{
    int clientPort = 8999;
    int serverPort = 9000;
    const char *serverIpAddr = "127.0.0.1";
    NET_Handle handle;

        
    if (1 == role) {  /* client */
        NET_init(clientPort, &handle);
        NET_tcpClient(handle, serverIpAddr, serverPort);
    }
    else if (2 == role) {  /* server */
        NET_init(serverPort, &handle);
        NET_tcpServer(handle);
    }

    NET_deinit(handle);

    return OSA_STATUS_OK;
}
