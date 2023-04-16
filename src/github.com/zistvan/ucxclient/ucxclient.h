#ifndef UCX_CLIENT_H
#define UCX_CLIENT_H

#include <ucp/api/ucp.h>

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <unistd.h>
#include <thread>
#include <cstring>
#include <list>
#include <string>
#include <mutex>

struct ReceivedMessage {
    unsigned char* data;
    size_t size;
};

/**
 * Stream request context. Holds a value to indicate whether or not the
 * request is completed.
 */
typedef struct test_req {
    int complete;
} test_req_t;

class ucx_client {
public:
    int init_conn(std::string sendAddress, int sendPort);
    void send(std::string data, int messageId);

    int getPeerMessageCount();
    int getClientMessageCount();
    int getCoordMessageCount();

    size_t* getPeerMessageSizes(int messageCount);
    size_t* getClientMessageSizes(int messageCount);
    size_t* getCoordMessageSizes(int messageCount);

    unsigned char** getPeerMessages(int messageCount);
    unsigned char** getClientMessages(int messageCount);
    unsigned char** getCoordMessages(int messageCount);

    void cleanup();
private:
    static void err_cb(void *arg, ucp_ep_h ep, ucs_status_t status);
    static void conn_handle_cb(ucp_conn_request_h conn_request, void *arg);
};

#endif
