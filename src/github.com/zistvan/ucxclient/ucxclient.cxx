#include "ucxclient.h"

ucp_context_h myContext;
ucp_worker_h myWorker;
ucp_ep_h myEndpoint;

ucp_listener_h myListener;

std::mutex peerMessageMutex;
std::mutex clientMessageMutex;
std::mutex coordMessageMutex;

std::mutex sendMutex;

std::list<struct ReceivedMessage> peerMessages;
std::list<struct ReceivedMessage> clientMessages;
std::list<struct ReceivedMessage> coordMessages;

int peerMessageCount = 0;
size_t* peerMessageSizesPtr = NULL;
unsigned char** peerMessagesPtr = NULL;

int clientMessageCount = 0;
size_t* clientMessageSizesPtr = NULL;
unsigned char** clientMessagesPtr = NULL;

int coordMessageCount = 0;
size_t* coordMessageSizesPtr = NULL;
unsigned char** coordMessagesPtr = NULL;

bool stop = false;

static void progress_worker()  {
	while(!stop) {
		sendMutex.lock();
		ucp_worker_progress(myWorker);
		sendMutex.unlock();
	}
}

// Error handling callback.
void ucx_client::err_cb(void *arg, ucp_ep_h ep, ucs_status_t status)
{
	printf("error handling callback was invoked with status %d (%s)\n",status, ucs_status_string(status));
	stop = true;
}

// The callback on the sending side, which is invoked after finishing sending the message.
static void send_cb(void *request, ucs_status_t status, void *user_data)
{
    test_req_t *ctx;
    ctx           = (test_req_t*) user_data;
    ctx->complete = 1;
}

ucs_status_t receivePeerMessage(void *arg, const void *header, size_t header_length,
                         void *data, size_t length, const ucp_am_recv_param_t *param) {
	peerMessageMutex.lock();

	unsigned char* charData = (unsigned char*) data;
	int index = 0;
	uint32_t pbSize;

	while(index < length) {
		pbSize = 0;
		pbSize += (uint32_t) ((unsigned char) charData[index]) << 0; 
		pbSize += (uint32_t) (((unsigned char) charData[index + 1]) << 8); 
		pbSize += (uint32_t) (((unsigned char) charData[index + 2]) << 16); 
		pbSize += (uint32_t) (((unsigned char) charData[index + 3]) << 24);

		unsigned char* msg = (unsigned char*) malloc(pbSize * sizeof(unsigned char));
		memcpy(msg, &charData[index + 4], pbSize);

		ReceivedMessage receivedMessage;
		receivedMessage.data = msg;
		receivedMessage.size = pbSize;

		peerMessages.push_back(receivedMessage);

		index += 4 + pbSize;
	}

	peerMessageMutex.unlock();

	return UCS_OK;
}

ucs_status_t receiveClientMessage(void *arg, const void *header, size_t header_length,
                         void *data, size_t length, const ucp_am_recv_param_t *param) {
	clientMessageMutex.lock();
	
	unsigned char* charData = (unsigned char*) data;
	int index = 0;
	uint32_t pbSize;

	while(index < length) {
		pbSize = 0;
		pbSize += (uint32_t) ((unsigned char) charData[index]) << 0; 
		pbSize += (uint32_t) (((unsigned char) charData[index + 1]) << 8); 
		pbSize += (uint32_t) (((unsigned char) charData[index + 2]) << 16); 
		pbSize += (uint32_t) (((unsigned char) charData[index + 3]) << 24);

		unsigned char* msg = (unsigned char*) malloc(pbSize * sizeof(unsigned char));
		memcpy(msg, &charData[index + 4], pbSize);

		ReceivedMessage receivedMessage;
		receivedMessage.data = msg;
		receivedMessage.size = pbSize;

		clientMessages.push_back(receivedMessage);

		index += 4 + pbSize;
	}

	clientMessageMutex.unlock();

	return UCS_OK;
}

ucs_status_t receiveCoordMessage(void *arg, const void *header, size_t header_length,
                         void *data, size_t length, const ucp_am_recv_param_t *param) {
	coordMessageMutex.lock();

	unsigned char* charData = (unsigned char*) data;
	int index = 0;
	uint32_t pbSize;

	while(index < length) {
		pbSize = 0;
		pbSize += (uint32_t) ((unsigned char) charData[index]) << 0; 
		pbSize += (uint32_t) (((unsigned char) charData[index + 1]) << 8); 
		pbSize += (uint32_t) (((unsigned char) charData[index + 2]) << 16); 
		pbSize += (uint32_t) (((unsigned char) charData[index + 3]) << 24);

		unsigned char* msg = (unsigned char*) malloc(pbSize * sizeof(unsigned char));
		memcpy(msg, &charData[index + 4], pbSize);

		ReceivedMessage receivedMessage;
		receivedMessage.data = msg;
		receivedMessage.size = pbSize;

		coordMessages.push_back(receivedMessage);

		index += 4 + pbSize;
	}

	coordMessageMutex.unlock();

	return UCS_OK;
}

// Progress the request until it completes.
ucs_status_t ucx_client::request_progress(void *request, test_req_t* req)
{
	ucs_status_t status;

    /* if operation was completed immediately */
    if (request == NULL) {
        return UCS_OK;
    }

    if (UCS_PTR_IS_ERR(request)) {
        return UCS_PTR_STATUS(request);
    }


    while (!req->complete) {
		ucp_worker_progress(myWorker);
        usleep(10);
    }
    
	status = UCS_PTR_STATUS(request);

	free(req);
    ucp_request_free(request);

    return status;
}

int ucx_client::init_conn(std::string sendAddress, int sendPort) {
	//UCP objects
	ucp_params_t ucp_params;
	ucs_status_t status;

	memset(&ucp_params, 0, sizeof(ucp_params));

	// UCP initialization
	ucp_params.field_mask = UCP_PARAM_FIELD_FEATURES;
	ucp_params.features = UCP_FEATURE_AM;

	status = ucp_init(&ucp_params, NULL, &myContext);
	if (status != UCS_OK) {
		fprintf(stderr, "failed to ucp_init (%s)\n", ucs_status_string(status));
		return -1;
	}

	ucp_worker_params_t worker_params;
    memset(&worker_params, 0, sizeof(worker_params));
    worker_params.field_mask  = UCP_WORKER_PARAM_FIELD_THREAD_MODE;
    worker_params.thread_mode = UCS_THREAD_MODE_SINGLE;

    status = ucp_worker_create(myContext, &worker_params, &myWorker);
	if (status != UCS_OK) {
		fprintf(stderr, "failed to init_worker (%s)\n", ucs_status_string(status));
		ucp_cleanup(myContext);
		return -1;
	}

	ucp_am_handler_param_t handler_params;
    handler_params.field_mask = UCP_AM_HANDLER_PARAM_FIELD_ID |
                                UCP_AM_HANDLER_PARAM_FIELD_CB;
    handler_params.id = 0;
    handler_params.cb = receiveClientMessage;
    status = ucp_worker_set_am_recv_handler(myWorker, &handler_params);
	if (status != UCS_OK) {
		fprintf(stderr, "failed to set recv handler (%s)\n", ucs_status_string(status));
	}

	handler_params.id = 1;
    handler_params.cb = receivePeerMessage;
    status = ucp_worker_set_am_recv_handler(myWorker, &handler_params);
	if (status != UCS_OK) {
		fprintf(stderr, "failed to set recv handler (%s)\n", ucs_status_string(status));
	}

	handler_params.id = 2;
    handler_params.cb = receiveCoordMessage;
    status = ucp_worker_set_am_recv_handler(myWorker, &handler_params);
	if (status != UCS_OK) {
		fprintf(stderr, "failed to set recv handler (%s)\n", ucs_status_string(status));
	}

	ucp_ep_params_t ep_params;
	struct sockaddr_storage connect_addr;

	
	struct sockaddr_in *sa_in = (struct sockaddr_in*) &connect_addr;
	sa_in->sin_family = AF_INET;
	sa_in->sin_port   = htons(sendPort);

	// Convert IPv4 and IPv6 addresses from text to binary form
	if (inet_pton(AF_INET, sendAddress.c_str(), &sa_in->sin_addr) <= 0) {
		printf("\nInvalid address/ Address not supported \n");
		return -1;
	}

	ep_params.field_mask       =    UCP_EP_PARAM_FIELD_ERR_HANDLER |
									UCP_EP_PARAM_FIELD_FLAGS      |
									UCP_EP_PARAM_FIELD_SOCK_ADDR ;
	ep_params.err_handler.cb  = err_cb;
	ep_params.err_handler.arg = NULL;
	ep_params.flags            = UCP_EP_PARAMS_FLAGS_CLIENT_SERVER;
	ep_params.sockaddr.addr    = (struct sockaddr*)&connect_addr;
	ep_params.sockaddr.addrlen = sizeof(connect_addr);

	status = ucp_ep_create(myWorker, &ep_params, &myEndpoint);
	if (status != UCS_OK) {
		fprintf(stderr, "failed to connect to %s (%s)\n", sendAddress.c_str(), ucs_status_string(status));
		return -1;
	}

	std::thread(progress_worker).detach();

	return 0;
}

void ucx_client::send(std::string data, int messageId) {
	ucs_status_t status;
	ucs_status_ptr_t request;
    ucp_request_param_t params;

	sendMutex.lock();

	test_req_t* req = (test_req_t*) malloc(sizeof(test_req_t));
	req->complete = 0;

	params.op_attr_mask = 	UCP_OP_ATTR_FIELD_CALLBACK |
							UCP_OP_ATTR_FIELD_DATATYPE |
                          	UCP_OP_ATTR_FIELD_USER_DATA |
							UCP_OP_ATTR_FIELD_FLAGS;
	params.datatype = ucp_dt_make_contig(1);
	params.cb.send = (ucp_send_nbx_callback_t) send_cb;
	params.user_data = req;
	params.flags = UCP_AM_SEND_FLAG_EAGER;

	if(messageId < 2) {
		request = ucp_am_send_nbx(myEndpoint, messageId, data.c_str(), data.size(), NULL, 0, &params);
	} else {
		request = ucp_am_send_nbx(myEndpoint, messageId, NULL, 0, data.c_str(), data.size(), &params);
	}
	
	status = request_progress(request, req);
	if (status != UCS_OK) {
	    fprintf(stderr, "unable to send UCX message: %d (%s)\n", status, ucs_status_string(status));
    }

	sendMutex.unlock();
}

int ucx_client::getPeerMessageCount() {
	int size = peerMessages.size();

	if(size > 0) {
		peerMessageMutex.lock();

		if(peerMessageSizesPtr != NULL) {
			free(peerMessageSizesPtr);
			peerMessageSizesPtr = NULL;
		}
		if(peerMessagesPtr != NULL) {
			for(int i = 0; i < peerMessageCount; i++) {
				free(peerMessagesPtr[i]);
			}
			free(peerMessagesPtr);
			peerMessagesPtr = NULL;
		}
	}

	peerMessageCount = size;
	return size;
}

int ucx_client::getClientMessageCount() {
	int size = clientMessages.size();

	if(size > 0) {
		clientMessageMutex.lock();

		if(clientMessageSizesPtr != NULL) {
			free(clientMessageSizesPtr);
			clientMessageSizesPtr = NULL;
		}
		if(clientMessagesPtr != NULL) {
			for(int i = 0; i < clientMessageCount; i++) {
				free(clientMessagesPtr[i]);
			}
			free(clientMessagesPtr);
			clientMessagesPtr = NULL;
		}
	}

	clientMessageCount = size;
	return size;
}

int ucx_client::getCoordMessageCount() {
	int size = coordMessages.size();

	if(size > 0) {
		coordMessageMutex.lock();

		if(coordMessageSizesPtr != NULL) {
			free(coordMessageSizesPtr);
			coordMessageSizesPtr = NULL;
		}
		if(coordMessagesPtr != NULL) {
			for(int i = 0; i < coordMessageCount; i++) {
				free(coordMessagesPtr[i]);
			}
			free(coordMessagesPtr);
			coordMessagesPtr = NULL;
		}
	}

	coordMessageCount = size;
	return size;
}

size_t* ucx_client::getPeerMessageSizes(int messageCount) {
	size_t* sizeArr = (size_t*) malloc(sizeof(size_t) * messageCount);
	std::list<ReceivedMessage>::iterator it = peerMessages.begin();

	for(int i = 0; i < messageCount; i++) {
		sizeArr[i] = (*it).size;
		it++;
	}

	peerMessageSizesPtr = sizeArr;
	return sizeArr;
}

size_t* ucx_client::getClientMessageSizes(int messageCount) {
	size_t* sizeArr = (size_t*) malloc(sizeof(size_t) * messageCount);
	std::list<ReceivedMessage>::iterator it = clientMessages.begin();

	for(int i = 0; i < messageCount; i++) {
		sizeArr[i] = (*it).size;
		it++;
	}

	clientMessageSizesPtr = sizeArr;
	return sizeArr;
}

size_t* ucx_client::getCoordMessageSizes(int messageCount) {
	size_t* sizeArr = (size_t*) malloc(sizeof(size_t) * messageCount);
	std::list<ReceivedMessage>::iterator it = coordMessages.begin();

	for(int i = 0; i < messageCount; i++) {
		sizeArr[i] = (*it).size;
		it++;
	}

	coordMessageSizesPtr = sizeArr;
	return sizeArr;
}

unsigned char** ucx_client::getPeerMessages(int messageCount) {
	unsigned char** stringArr = (unsigned char**) malloc(sizeof(unsigned char*) * messageCount);
	
	for(int i = 0; i < messageCount; i++) {
		stringArr[i] = peerMessages.front().data;
		peerMessages.pop_front();
	}

	peerMessageMutex.unlock();

	peerMessagesPtr = stringArr;
	return stringArr;
}

unsigned char** ucx_client::getClientMessages(int messageCount) {
	unsigned char** stringArr = (unsigned char**) malloc(sizeof(unsigned char*) * messageCount);
	
	for(int i = 0; i < messageCount; i++) {
		stringArr[i] = clientMessages.front().data;
		clientMessages.pop_front();
	}
	
	clientMessageMutex.unlock();

	clientMessagesPtr = stringArr;
	return stringArr;
}

unsigned char** ucx_client::getCoordMessages(int messageCount) {
	unsigned char** stringArr = (unsigned char**) malloc(sizeof(unsigned char*) * messageCount);
	
	for(int i = 0; i < messageCount; i++) {
		stringArr[i] = coordMessages.front().data;
		coordMessages.pop_front();
	}

	coordMessageMutex.unlock();

	coordMessagesPtr = stringArr;
	return stringArr;
}

void ucx_client::cleanup() {
	stop = true;

	printf("Stopping UCX execution\n");

	ucs_status_ptr_t close_status_pointer;
    ucp_request_param_t params;
    params.op_attr_mask = 0;

	close_status_pointer = ucp_ep_close_nbx(myEndpoint, &params);

    ucs_status_t status;
    if(UCS_PTR_IS_PTR(close_status_pointer)) {
		do {
			ucp_worker_progress(myWorker);
			status = ucp_request_check_status(close_status_pointer);
		} while (status == UCS_INPROGRESS);
		ucp_request_free(close_status_pointer);
	} else {
		status = UCS_PTR_STATUS(close_status_pointer);
	}

	if (status != UCS_OK) {
		fprintf(stderr, "failed to close ep %p: %s\n", (void*)myEndpoint, ucs_status_string(status));
	}

    ucp_worker_destroy(myWorker);
	ucp_cleanup(myContext);
}
