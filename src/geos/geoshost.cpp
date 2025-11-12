#define CPP_MODULE

#include "dosbox.h"
#include "inout.h"
#include "control.h"
#include "mouse.h"
#include "cpu.h"
#include "timer.h"
#include "sdlmain.h"
#include "regs.h"
#include "mem.h"

#include "../ints/int10.h"
#include <SDL.h>
#include <SDL_net.h>

#include <SDL3_net/SDL_net.h>

#include "tlse.h"

#if C_GEOSHOST

#define USE_SDL3

#define MAX_ASYNC_OP_SLOTS	16

#define	HIF_API_HOST	0
#define HIF_API_VIDEO	1
#define HIF_API_SSL		2
#define HIF_API_SOCKET	3

#define HIF_SLOT_AX		0
#define HIF_SLOT_SI		1
#define HIF_SLOT_BX		2
#define HIF_SLOT_CX		3
#define HIF_SLOT_DX		4
#define HIF_SLOT_DI		5

	#define HIF_NOTIFY_DISPLAY_SIZE_CHANGE	1
#define HIF_NOTIFY_SOCKET_STATE_CHANGE	2

#define HIF_CHECK_API			98
#define HIF_SET_VIDEO_PARAMS	4
#define HIF_SET_EVENT_INTERRUPT 5
#define HIF_EVENT_ASYNC_END		7
#define HIF_GET_VIDEO_PARAMS	8
#define HIF_GET_EVENT			9

#define HIF_OK					0
#define HIF_NOT_FOUND			1
#define HIF_NO_MEMORY			2
#define HIF_ASYNC_OP			3
#define HIF_TABLE_FULL			4
#define HIF_PENDING				5
#define HIF_EVENT_NOTIFICATION	6
#define HIF_FAILED				7

#define HIF_NETWORKING_BASE     1000
#define HIF_NC_RESOLVE_ADDR     HIF_NETWORKING_BASE
#define HIF_NC_ALLOC_CONNECTION HIF_NETWORKING_BASE + 1
#define HIF_NC_CONNECT_REQUEST  HIF_NETWORKING_BASE + 2
#define HIF_NC_SEND_DATA        HIF_NETWORKING_BASE + 3
#define HIF_NC_NEXT_RECV_SIZE   HIF_NETWORKING_BASE + 4
#define HIF_NC_RECV_NEXT        HIF_NETWORKING_BASE + 5
#define HIF_NC_RECV_NEXT_CLOSE  HIF_NETWORKING_BASE + 6
#define HIF_NC_CLOSE            HIF_NETWORKING_BASE + 7
#define HIF_NC_DISCONNECT       HIF_NETWORKING_BASE + 8
#define HIF_NETWORKING_END      1199

#define HIF_SSL_BASE				 1200
#define HIF_SSL_V2_GET_CLIENT_METHOD HIF_SSL_BASE
#define HIF_SSL_SSLEAY_ADD_SSL_ALGO  HIF_SSL_BASE + 1
#define HIF_SSL_CTX_NEW              HIF_SSL_BASE + 2
#define HIF_SSL_CTX_FREE             HIF_SSL_BASE + 3
#define HIF_SSL_NEW                  HIF_SSL_BASE + 4
#define HIF_SSL_FREE                 HIF_SSL_BASE + 5
#define HIF_SSL_SET_FD               HIF_SSL_BASE + 6
#define HIF_SSL_CONNECT              HIF_SSL_BASE + 7
#define HIF_SSL_SHUTDOWN             HIF_SSL_BASE + 8
#define HIF_SSL_READ                 HIF_SSL_BASE + 9
#define HIF_SSL_WRITE                HIF_SSL_BASE + 10
#define HIF_SSL_V23_CLIENT_METHOD    HIF_SSL_BASE + 11
#define HIF_SSL_V3_CLIENT_METHOD     HIF_SSL_BASE + 12
#define HIF_SSL_GET_SSL_METHOD       HIF_SSL_BASE + 13
#define HIF_SSL_SET_CALLBACK         HIF_SSL_BASE + 14
#define HIF_SSL_SET_TLSEXT_HOST_NAME HIF_SSL_BASE + 15
#define HIF_SSL_END                  1299


const static char G_baseboxID[] = "XOBESAB2";
static uint8_t G_baseboxIDOffset = 1;

static uint8_t G_commandOffset = 0;
static uint16_t G_commandBuffer[6];
static uint8_t G_responseOffset = 0;
static uint16_t G_responseBuffer[6];
static uint8_t G_eventInterrupt = 0;
SDL_mutex* G_eventQueueMutex = NULL;
bool G_recheckEventInterrupt = false;
bool G_protectedOpMode = false;
static uint16_t G_nextAsyncID = 1;

class EventRecord {
private:
	volatile uint16_t m_Payload[6];
	EventRecord* m_Next;

public:
	EventRecord(uint16_t* eventRecord, EventRecord* next) {
		m_Next = next;
		uint8_t loopCount = 0;
		while (loopCount < 6) {
			m_Payload[loopCount] = eventRecord[loopCount];
			loopCount++;
		}
	}
	~EventRecord() {
	}

public:
	void SetNext(EventRecord* nextEvent) {
		m_Next = nextEvent;
	};
	EventRecord* GetNext() {
		return m_Next;
	}
	void GetRecordData(uint16_t* recordBuf) {

		uint8_t loopCount = 0;
		while (loopCount < 6) {
			recordBuf[loopCount] = m_Payload[loopCount];
			loopCount++;
		}
	}
};

#define UNALLOC_ASYNC_OP_ID	0

class AsyncOp {
private:
	AsyncOp* m_Next;
	uint16_t m_ID;
	SDL_Thread* m_thread;
	bool m_EventSent;

protected:
	bool m_RunOwnThread;

protected:
	uint16_t m_Result[6];

public:
	AsyncOp(AsyncOp* next)
	{
		m_Next = next;
		m_Result[0] = HIF_PENDING;
		m_thread    = NULL;
		m_EventSent = false;
		m_RunOwnThread = true;
	}
	virtual ~AsyncOp() {};

public:
	virtual uint16_t Init(uint16_t* cmdRec);

	AsyncOp* GetNext()
	{
		return m_Next;
	}
	uint16_t GetID() {
		return m_ID;
	}
	AsyncOp* Cleanup();


public:
	virtual uint16_t RunAsync()
	{
		return 0;
	 };
	virtual uint16_t PollStatus()
	{
		return 0;
	};
	void HandleCompletion();
};

class AsyncSocketResolveAddr : public AsyncOp 
{
private:
	char m_hostname[256];
#ifdef USE_SDL3
	NET_Address* m_Addr;
#endif
public:
	AsyncSocketResolveAddr(AsyncOp* next) : AsyncOp(next) {
#ifdef USE_SDL3
		m_Addr = NULL;
#endif

	};
	~AsyncSocketResolveAddr() {};

public:
	uint16_t Init(uint16_t* cmdRec);

public:
	uint16_t RunAsync();
	uint16_t PollStatus();
};

class AsyncSocketConnect : public AsyncOp {
private:
	enum State { IDLE, RESOLVING, CONNECTING, CONNECTED, DONE };

private:
	IPaddress m_ipAddr;
	uint16_t m_socketHandle;
#ifdef USE_SDL3
	NET_Address* m_Addr;
	Uint16 m_Port;
	State m_State;
#endif
public:
	AsyncSocketConnect(AsyncOp* next) : AsyncOp(next) {
#ifdef USE_SDL3
		m_Addr = NULL;
		m_State = IDLE;
	#endif
	};
	~AsyncSocketConnect() {};

public:
	uint16_t Init(uint16_t* cmdRec);

public:
	uint16_t RunAsync();
	uint16_t PollStatus();
};

class AsyncSocketSend : public AsyncOp {
private:

private:
	uint16_t m_socketHandle;
public:
	AsyncSocketSend(AsyncOp* next) : AsyncOp(next) {};
	~AsyncSocketSend() {};

public:
	uint16_t Init(uint16_t* cmdRec);

public:
	uint16_t RunAsync();
	uint16_t PollStatus();
};

struct SocketState {
	volatile bool used;
	volatile bool open;
	volatile bool blocking;
	TCPsocket socket;
	//NET_StreamSocket socketSet;
#ifdef USE_SDL3
	NET_StreamSocket* stream;
#endif
	char* recvBuf;
	volatile int recvBufUsed;
	volatile bool receiveDone;
	volatile bool done;
	volatile bool ssl;
	volatile bool sslInitialEnd;

	SocketState()
	        : used(false),
	          recvBuf(NULL),
	          recvBufUsed(0),
	          receiveDone(false),
	          done(false),
	          ssl(false),
	          sslInitialEnd(false)
	{}
};

static const int MaxSockets = 256;

static SocketState NetSockets[MaxSockets];

EventRecord* G_eventRecords = NULL;
AsyncOp* G_opRecords = NULL;

void GeosHost_NotifySocketChange();

static void PollSockets() {

	for (int i = 1; i < MaxSockets; i++) {
		if (NetSockets[i].used && NetSockets[i].open &&
		    !NetSockets[i].receiveDone &&
		    !NetSockets[i].done && (NetSockets[i].recvBufUsed <= 0)) {
			if (NetSockets[i].recvBuf == NULL) {
				LOG_MSG("\nReceived new buf");
				NetSockets[i].recvBuf = new char[8192];
			}

			int result = NET_ReadFromStreamSocket(NetSockets[i].stream,
			                                      NetSockets[i].recvBuf,
			                                      8192);
			if (result < 0) {
				// handle error situation
				NetSockets[i].receiveDone = true;
				NetSockets[i].sslInitialEnd = true;
				GeosHost_NotifySocketChange();
			} else if (result > 0) {
				NetSockets[i].recvBufUsed = result;
				LOG_MSG("\nSet sock->recvBufUsed %d", result);

				//				SDL_mutexP(G_callbackMutex);
				//				G_callbackPending
				//= true; 				SDL_mutexV(G_callbackMutex);
				// PIC_ActivateIRQ(5);
				GeosHost_NotifySocketChange();

				LOG_MSG("\nReceived data passed");
			}
		}
	}
}

static void GeosHost_TickHandler(void) {

	// Check for AsyncOp completion
	AsyncOp* nextOp = G_opRecords;
	while (nextOp) {
	
        nextOp->PollStatus();
		nextOp->HandleCompletion();
		nextOp = nextOp->GetNext();
	}
	if (G_opRecords) {
		G_opRecords = G_opRecords->Cleanup();
	}
#ifdef USE_SDL3
	PollSockets();
#endif

	// if in the matching operation mode: real mode or protected mode
	if (G_eventInterrupt && (G_protectedOpMode == cpu.pmode)) {
		// if event interrupt is requested
		if (G_recheckEventInterrupt && (reg_flags & FLAG_IF)) {

			static int intCounter = 0;

			SDL_mutexP(G_eventQueueMutex);
			G_recheckEventInterrupt = false;
			SDL_mutexV(G_eventQueueMutex);
			// issue software interrupt
			int thisIntCount = intCounter++;

			LOG_INFO("GEOSHOST: Trigger event interrupt (#%d)",
			         thisIntCount);

			CPU_SW_Interrupt(G_eventInterrupt, reg_eip);
			//DOSBOX_RunMachine();
			LOG_INFO("GEOSHOST: Event interrupt done (#%d)", thisIntCount);
		} 
	}
}

void GeosHost_GeneralNotification() {

	// Send general notification event
}

void GeosHost_AsyncCompletion() {

	// Send async completion event
}

void GeosHost_SendEvent(uint16_t* eventRecord)
{
	// Create Event Object
	EventRecord* newRecord = new EventRecord(eventRecord, NULL);
	
	// Add Event to list
	SDL_mutexP(G_eventQueueMutex);
	if (G_eventRecords == NULL) {
		G_recheckEventInterrupt = true;
	}
	newRecord->SetNext(G_eventRecords);
	G_eventRecords = newRecord;
	SDL_mutexV(G_eventQueueMutex);
}

uint16_t AsyncSocketSend::Init(uint16_t* cmdRec) {

	m_socketHandle = cmdRec[1];

	LOG_MSG("AsyncSocketSend::Init: %d %x:%x %d",
	        cmdRec[4],
	        cmdRec[2],
	        cmdRec[3],
	        m_socketHandle);

	if (m_socketHandle < 0 || m_socketHandle >= MaxSockets) {
		return HIF_FAILED;
	} else {
		SocketState& sock = NetSockets[m_socketHandle];

		uint32_t dosBuff = static_cast<uint32_t>(G_commandBuffer[2] << 4) +
		                   G_commandBuffer[3];
		int size = cmdRec[4];
		LOG_MSG("NetSendData data size: %d", size);

		char* buffer = new char[size + 1];
		for (int i = 0; i < size; i++) {
			buffer[i] = mem_readb(dosBuff + i);
		}
		buffer[size] = 0;

		bool result = NET_WriteToStreamSocket(sock.stream,
		                                      buffer,
		                                      size);
		delete[] buffer;
		if (!result) {
			return HIF_FAILED;
		}
	}
	return HIF_OK;
}

uint16_t AsyncSocketSend::RunAsync() {
	return 0;
}

uint16_t AsyncSocketSend::PollStatus() {
	SocketState& sock = NetSockets[m_socketHandle];
	if (sock.stream) {
		int pendingCount = NET_GetStreamSocketPendingWrites(sock.stream);
		if (pendingCount == 0) {
		
			// successfully sent
			m_Result[0] = HIF_OK;
		} else {
			// ended with error
			m_Result[0] = HIF_FAILED;
		}
	}
	return 0;
}


uint16_t AsyncSocketResolveAddr::Init(uint16_t* cmdRec)
{
	// si:bx	= host name address
	// cx = address name len
	LOG_MSG("\nAsyncSocketResolveAddr::Init: %x %x\n", cmdRec[1], cmdRec[2]);
	MEM_StrCopy(static_cast<uint32_t>(cmdRec[1] << 4) + cmdRec[2],
	            m_hostname,
	            cmdRec[3]); // 1024 toasts the
	                     // stack
	m_hostname[cmdRec[3]] = 0;

#ifdef USE_SDL3
	m_RunOwnThread = false;
	m_Addr = NET_ResolveHostname(m_hostname);
	if (m_Addr == NULL) {
		return HIF_FAILED;
	}
#endif
	return AsyncOp::Init(cmdRec);
}

uint16_t 
AsyncSocketConnect::Init(uint16_t* cmdRec) {

	m_ipAddr.host = (((uint32_t)cmdRec[1]) << 16) | cmdRec[2];
	m_ipAddr.port = ((cmdRec[3] & 0xFF) << 8) | ((cmdRec[3] >> 8) & 0xFF);

	m_socketHandle = cmdRec[4];
#ifdef USE_SDL3
	m_RunOwnThread = false;
	char hostname[20];
	sprintf(hostname,
	        "%d.%d.%d.%d",
	        (cmdRec[1] >> 8) & 0xFF,
	        cmdRec[1]  & 0xFF,
	        (cmdRec[2] >> 8) & 0xFF,
	        cmdRec[2] & 0xFF);
	LOG_MSG("\nAsyncSocketConnect::Init: %s\n", hostname);
	m_Port = cmdRec[3] /* ((cmdRec[3] & 0xFF) << 8) |
	         ((cmdRec[3] >> 8) & 0xFF)*/;
	m_Addr = NET_ResolveHostname(hostname);
	if (m_Addr == NULL) {
		return HIF_FAILED;
	}
	m_State = RESOLVING;
	#endif
	return AsyncOp::Init(cmdRec);
}

static int ReceiveThread(void* sockPtr)
{
	LOG_MSG("\nReceive thread started...");

	SocketState* sock = (SocketState*)sockPtr;
	do {
		// wait for data buffer to be supplied by dos request, or cancelled
		if (((SocketState*)sock)->ssl) {
			//((SocketState *)sock)->receiveDone = true;
			((SocketState*)sock)->sslInitialEnd = true;

			LOG_MSG("\nSSL initial receive done %x", sock);
			return 0;
		}

		if ((sock->recvBufUsed <= 0) /* && !G_receiveCallActive*/) {

			LOG_MSG("\nReceived get");
			if (sock->recvBuf == NULL) {
				LOG_MSG("\nReceived new buf");
				sock->recvBuf = new char[8192];
			}

			int result = -1;
			if (!sock->done) {
				result = SDLNet_TCP_Recv(((SocketState*)sock)->socket,
				                         sock->recvBuf,
				                         8192);
			}
			LOG_MSG("\nReceived data %d", result);
			if ((!sock->done) && (result > 0)) {

				// pass data to DOS
				sock->recvBufUsed = result;
				LOG_MSG("\nSet sock->recvBufUsed %d", result);

				//				SDL_mutexP(G_callbackMutex);
//				G_callbackPending = true;
//				SDL_mutexV(G_callbackMutex);
				// PIC_ActivateIRQ(5);
				GeosHost_NotifySocketChange();
			
				LOG_MSG("\nReceived data passed");
			} else {

				// handle receive error
				LOG_MSG("\nReceived done");
				// SDL_Delay(5000);
				((SocketState*)sock)->receiveDone   = true;
				((SocketState*)sock)->sslInitialEnd = true;
				GeosHost_NotifySocketChange();
				return 0;
			}
		} else {

			// pending, wait some time and retry
			SDL_Delay(50);
		}

	} while (true);
}

static void NetStartReceiver(int handle)
{

	// start receiver thread
	SDL_Thread* thread;
	int threadReturnValue;

	LOG_MSG("\nSimple SDL_CreateThread test:");

	SocketState& sock = NetSockets[handle];

	// Simply create a thread
	thread = SDL_CreateThread(ReceiveThread, "ReceiveThread", (void*)&sock);

	if (NULL == thread) {
		LOG_MSG("\nSDL_CreateThread failed: %s\n", SDL_GetError());
	} else {
		// SDL_WaitThread(thread, &threadReturnValue);
		// printf("\nThread returned value: %d", threadReturnValue);
		SDL_DetachThread(thread);
	}
}

uint16_t 
AsyncSocketConnect::RunAsync() {

	LOG_INFO("Connect %x %x %u\n", m_ipAddr.host, m_ipAddr.host, m_socketHandle);


	SocketState& sock = NetSockets[m_socketHandle];


	if (!(sock.socket = SDLNet_TCP_Open(&m_ipAddr))) {
		//__android_log_print(ANDROID_LOG_DEBUG, "GeosHost", "TCP Open
		// failed %x\n", ip.host); if (!sock.blocking) {
		//	SDLNet_FreeSocketSet(sock.socketSet);
		//}
		LOG_MSG("NetConnectRequest failed");
		m_Result[0] = HIF_FAILED;

	} else {

		sock.open = true;
		LOG_MSG("NetConnectRequest success");
		m_Result[0] = HIF_OK;

		// start receiver thread
		NetStartReceiver(m_socketHandle);
	}

	return 0;
}


uint16_t AsyncSocketConnect::PollStatus() {

	switch (m_State) {
	case RESOLVING: 
		if (m_Addr) {
			NET_Status status = NET_GetAddressStatus(m_Addr);
			if (status != NET_WAITING) {
				if (status == NET_SUCCESS) {
					m_State = CONNECTING;

					// move on to connecint state
					NetSockets[m_socketHandle].stream = NET_CreateClient(m_Addr, m_Port);
				} else {
					// failed
					m_State     = DONE;
					m_Result[0] = HIF_FAILED;
				}
			NET_UnrefAddress(m_Addr);
			}
		}
		break;
	case CONNECTING:
		if (NetSockets[m_socketHandle].stream) {
			NET_Status status = NET_GetConnectionStatus(NetSockets[m_socketHandle].stream);
			if (status != NET_WAITING) {
				if (status == NET_SUCCESS) {
					m_State = CONNECTED;
					NetSockets[m_socketHandle].open = true;
					LOG_MSG("NetConnectRequest success");
					m_Result[0] = HIF_OK;
					m_State     = DONE;
				} 
				else {
					m_Result[0] = HIF_FAILED;
					m_State     = DONE;
				}
			}
		}
	default: 
		break;
	}
	return 0;
}


static int InitOpThread(void* paramsPtr)
{
	((AsyncOp*)paramsPtr)->RunAsync();
	return 0;
}

uint16_t 
AsyncOp::Init(uint16_t* cmdRec)
{
	// allocate ID
	m_ID        = G_nextAsyncID;
	G_nextAsyncID++;

	uint16_t checkSlot = 0;
	while (checkSlot < MAX_ASYNC_OP_SLOTS) {

		AsyncOp *nextOp = G_opRecords;
		while (nextOp) {
		
			if (nextOp->GetID() == checkSlot) {
				break;
			}
			nextOp = nextOp->GetNext();
		}

		if (!nextOp) {
			// not found, so we are free to use the slot
			break;
		}

		checkSlot++;
	}

	if (checkSlot == MAX_ASYNC_OP_SLOTS) {
	
		// error, all slots full
		return HIF_FAILED;
	}

	m_ID = checkSlot;

	// Simply create a thread
	if (m_RunOwnThread) {
		m_thread = SDL_CreateThread(InitOpThread, "InitOpThread", (void*)this);

		if (NULL == m_thread) {
			LOG_MSG("\nSDL_CreateThread failed: %s\n", SDL_GetError());
			return HIF_NO_MEMORY;
		}
		SDL_DetachThread(m_thread);
	}

	return HIF_PENDING | (m_ID << 8);
}

void 
AsyncOp::HandleCompletion() 
{
	if (!m_EventSent && m_Result[0] != HIF_PENDING) {
	
		m_Result[0] |= m_ID << 8;
		GeosHost_SendEvent(m_Result);

		m_EventSent = true;
	}
}

AsyncOp* AsyncOp::Cleanup()
{
	
	AsyncOp* nextOp = this->GetNext();
	AsyncOp* thisOp = this; 
	while (nextOp) {
	
		nextOp = nextOp->GetNext();
		if (nextOp->m_EventSent) {
			AsyncOp* deleteOp = nextOp;
			thisOp->m_Next    = nextOp->GetNext();
			delete deleteOp;
		}
		thisOp = GetNext();
		if (thisOp) {
			nextOp = thisOp->GetNext();
		} else {
			nextOp = NULL;
		}
	}

	m_Next = nextOp;

	if (this->m_EventSent) {
		delete this;
		return nextOp;
	}
	return this;
}

uint16_t
AsyncSocketResolveAddr::RunAsync()
{
	LOG_INFO("NetResolveAddr %s", m_hostname);

	IPaddress ipaddress;
	int result = SDLNet_ResolveHost(&ipaddress, m_hostname, 1234);
	if (result == 0) {

		m_Result[1] = (ipaddress.host >> 16) & 0xFFFF;
		m_Result[2] = ipaddress.host & 0xFFFF;

		LOG_MSG("NetResolveAddr success %x", ipaddress.host);
		m_Result[0] = HIF_OK;
		return 0;
	}

	m_Result[0] = HIF_FAILED;

	return 0;
}

#ifdef USE_SDL3
uint16_t 
AsyncSocketResolveAddr::PollStatus() {

	NET_Status status = NET_GetAddressStatus(m_Addr);
	if (m_Addr && (status != NET_WAITING)) {
		if (status == NET_SUCCESS) {
			m_Result[0] = HIF_OK;
			long addr_host   = NET_GetIP4Address(m_Addr);
			LOG_MSG("NetResolveAddr success %x", addr_host);
			m_Result[2] = (((addr_host >> 16) & 0xFF) << 8) | (((addr_host >> 16) & 0xFF00) >> 8);
			m_Result[1] = ((addr_host & 0xFF) << 8) | ((addr_host & 0xFF00) >> 8);
		} else {
			m_Result[0] = HIF_FAILED;		
		}
		NET_UnrefAddress(m_Addr);
		m_Addr = NULL;
	}
	return 0;
}
#endif

static uint16_t read_baseboxid(io_port_t, io_width_t)
{
	uint16_t result = 0;
	if (G_responseOffset > 0) {
		G_responseOffset--;
		result = G_responseBuffer[G_responseOffset];
	}
	G_commandOffset = 0;
	return result;
}

void GeosHost_NotifyVideoChange()
{
	// send event to GEOS client
	static uint16_t eventRecord[6];
	eventRecord[0] = HIF_EVENT_NOTIFICATION;
	eventRecord[1] = HIF_NOTIFY_DISPLAY_SIZE_CHANGE;

	GeosHost_SendEvent(eventRecord);
}

void GeosHost_NotifySocketChange()
{
	// send event to GEOS client
	static uint16_t eventRecord[6];
	eventRecord[0] = HIF_EVENT_NOTIFICATION;
	eventRecord[1] = HIF_NOTIFY_SOCKET_STATE_CHANGE;

	GeosHost_SendEvent(eventRecord);
}

#define MAX_HANDLES 20

static void* handles[MAX_HANDLES];

static int AllocHandle(void* ptr)
{

	int handle = 0;
	while (handle < MAX_HANDLES) {

		if (handles[handle] == NULL) {

			handles[handle] = ptr;
			return handle + 1;
		}
		handle++;
	}

	return 0;
}

int SSLSocketRecv(int socket, void* buffer, size_t length, int flags)
{
	LOG_MSG("!!!SSLSocketRecv");
	SocketState& sock = NetSockets[socket];

	LOG_MSG("\n!!!SSLSocketRecv start wait %x", &sock);
	while (!sock.sslInitialEnd) {
	};
	LOG_MSG("\n!!!SSLSocketRecv done wait");

	if (sock.recvBufUsed) {
		int recvSize = sock.recvBufUsed;
		memcpy(buffer, sock.recvBuf, recvSize);
		sock.recvBufUsed = 0;
		return recvSize;
	}

	return SDLNet_TCP_Recv(sock.socket, buffer, length);
}

int SSLSocketSend(int socket, const void* buffer, size_t length, int flags)
{
	LOG_MSG("!!!SSLSocketSend");
	SocketState& sock = NetSockets[socket];

	sock.ssl = true;

	return SDLNet_TCP_Send(sock.socket, buffer, length);
}

static void write_baseboxcmd(io_port_t, io_val_t command, io_width_t)
{
	// we receive command bytes on this io port, works like a
	// interrupt but keeps the overall processor state
	// 
	// parameters are fetched from register contents as needed
	// result are fed into registers as well, depending on the command
	// potentially referenced memory is read or written

	if (G_commandOffset < (sizeof(G_commandBuffer) / sizeof(uint16_t))) {
		G_commandBuffer[G_commandOffset] = (uint16_t)command;
		G_commandOffset++;

		if (G_commandOffset == (sizeof(G_commandBuffer) / sizeof(uint16_t))) {
			if (G_commandBuffer[0] == HIF_SET_VIDEO_PARAMS) {

				MOUSEDOS_BeforeNewVideoMode();
				uint16_t newWidth  = G_commandBuffer[1];
				uint16_t newHeight = G_commandBuffer[2];
				VESA_SetBaseboxMode(newWidth, newHeight);
				LOG_INFO("GEOSHOST:Set basebox video mode");
				MOUSEDOS_AfterNewVideoMode(false);
				
				G_responseBuffer[0] = HIF_OK;
				G_responseBuffer[2] = 0x89A;
				G_responseOffset    = 6;

			} else if (G_commandBuffer[0] == HIF_CHECK_API) {

				uint16_t resultVersion = 0;

				/* full response buffer */
				G_responseBuffer[0] = HIF_OK;
				G_responseBuffer[1] = (((uint16_t)G_baseboxID[1])
				                       << 8) |
				                      G_baseboxID[0];
				G_responseBuffer[2] = (((uint16_t)G_baseboxID[3])
				                       << 8) |
				                      G_baseboxID[2];
				G_responseBuffer[3] = (((uint16_t)G_baseboxID[5])
				                       << 8) |
				                      G_baseboxID[4];
				G_responseBuffer[4] = (((uint16_t)G_baseboxID[7])
				                       << 8) |
				                      G_baseboxID[6];
				/* Return API specific version, 0 if API is not supported */
				switch (G_commandBuffer[1]) {
					case HIF_API_HOST: 
						resultVersion = 1;
						break;
					case HIF_API_VIDEO:
						resultVersion = 1;
						break;
				    case HIF_API_SOCKET:
					    resultVersion = 1;
					    break;
				    case HIF_API_SSL:
						//resultVersion = 1;
						//break;
				    default: 
						resultVersion = 0; /* any other unsupported API: not supported */
				}
				G_responseBuffer[5] = resultVersion; /* minor (compatibility)
				                            version */
				G_responseOffset = 6;

			} else if (G_commandBuffer[0] == HIF_SET_EVENT_INTERRUPT) {
				G_eventInterrupt =
				        0xA0 /* G_commandBuffer[1] & 0xFF*/;
			} else if (G_commandBuffer[0] == HIF_GET_VIDEO_PARAMS) {

				float ddpi, hdpi, vdpi;

				int displayIndex = SDL_GetWindowDisplayIndex(sdl.window);

				int width, height;
				SDL_GL_GetDrawableSize(sdl.window, &width, &height);

				G_responseBuffer[0] = HIF_OK;
				//G_responseBuffer[1] = sdl.draw_rect_px.w; /* native width */
				//G_responseBuffer[2] = sdl.draw_rect_px.h; /* native height */
				G_responseBuffer[1] = width; /* native width */ 
				G_responseBuffer[2] = height; /* native height */

				// Get the DPI of the first display (index 0)
				if (SDL_GetDisplayDPI(displayIndex, &ddpi, &hdpi, &vdpi) ==
				    0) {
					G_responseBuffer[3] = hdpi; /* horizontal dpi */
					G_responseBuffer[4] = vdpi; /* vertical dpi */
				} else {
					/* error getting the dpi */
					G_responseBuffer[3] = 0; /* horizontal dpi */
					G_responseBuffer[4] = 0; /* vertical dpi */
				}
				G_responseBuffer[5] = 0; /* unused */
				G_responseOffset    = 6;
			
			} else if (G_commandBuffer[0] == HIF_GET_EVENT) {
			
				// Add Event to list
				SDL_mutexP(G_eventQueueMutex);
				if (G_eventRecords == NULL) {
					G_responseBuffer[0] = HIF_NOT_FOUND;

				} else {

					EventRecord* thisRecord = G_eventRecords;
					thisRecord->GetRecordData(G_responseBuffer);
					G_eventRecords = thisRecord->GetNext();
					delete thisRecord;
				}
				G_responseOffset = 6;
				SDL_mutexV(G_eventQueueMutex);
			} else if (G_commandBuffer[0] == HIF_NC_RESOLVE_ADDR) {

				// allocate async op
				AsyncOp* newOp = new AsyncSocketResolveAddr(G_opRecords);
				if (newOp) {

					G_responseBuffer[0] = newOp->Init(
					        G_commandBuffer);

					if ((G_responseBuffer[0] & 0xFF) !=
					    HIF_PENDING) {

						delete newOp;

					} else {

						// add op to queue
						G_opRecords = newOp;
					}
				} else {

					G_responseBuffer[0] = HIF_NO_MEMORY;			
				}
				G_responseOffset    = 6;

			} else if (G_commandBuffer[0] == HIF_NC_ALLOC_CONNECTION) {

				int socketHandle = -1;

				//__android_log_print(ANDROID_LOG_DEBUG,
				//"GeosHost",
				//"NetAllocConnection");

				for (int i = 1; i < MaxSockets; i++) {
					if (!NetSockets[i].used) {
						socketHandle = i;
						break;
					}
				}

				if (socketHandle < 0) { // no free sockets
					LOG_MSG("ERROR No free sockets");
					G_responseBuffer[0] = HIF_TABLE_FULL;
					return;
				}

				LOG_MSG("Opening socket handle %d\n", socketHandle);
				SocketState& sock = NetSockets[socketHandle];

				sock.used          = true;
				sock.done          = false;
				sock.open          = false;
				sock.blocking      = false;
				sock.ssl           = false;
				sock.sslInitialEnd = false;
				sock.receiveDone   = false;

				G_responseBuffer[0] = HIF_OK;
				G_responseBuffer[1] = (uint16_t)socketHandle;

				G_responseOffset = 6;

			} else if (G_commandBuffer[0] == HIF_NC_CONNECT_REQUEST) {

				// allocate async op
				AsyncOp* newOp = new AsyncSocketConnect(G_opRecords);
				if (newOp) {

					G_responseBuffer[0] = newOp->Init(
					        G_commandBuffer);

					if ((G_responseBuffer[0] & 0xFF) !=
					    HIF_PENDING) {

						delete newOp;

					} else {

						// add op to queue
						G_opRecords = newOp;
					}
				} else {

					G_responseBuffer[0] = HIF_NO_MEMORY;
				}
				G_responseOffset = 6;

			} else if (G_commandBuffer[0] == HIF_NC_SEND_DATA) {

#ifdef USE_SDL3
				// allocate async op
				AsyncOp* newOp = new AsyncSocketSend(G_opRecords);
				if (newOp) {

					G_responseBuffer[0] = newOp->Init(
					        G_commandBuffer);

					if ((G_responseBuffer[0] & 0xFF) !=
					    HIF_PENDING) {

						delete newOp;

					} else {

						// add op to queue
						G_opRecords = newOp;
					}
				} else {

					G_responseBuffer[0] = HIF_NO_MEMORY;
				}
				G_responseOffset = 6;

	#else
				int socketHandle = G_commandBuffer[1];
				LOG_MSG("NetSendData: %d %x:%x %d",
				        G_commandBuffer[4],
				        G_commandBuffer[2],
				        G_commandBuffer[3],
				        socketHandle);

				if (socketHandle < 0 || socketHandle >= MaxSockets) {
					G_responseBuffer[0] = HIF_FAILED;
				} else {
					SocketState& sock = NetSockets[socketHandle];

					uint32_t dosBuff = static_cast<uint32_t>(
					                           G_commandBuffer[2]
					                           << 4) +
					                   G_commandBuffer[3];
					int size = G_commandBuffer[4];
					LOG_MSG("NetSendData data size: %d", size);

					char* buffer = new char[size + 1];
					for (int i = 0; i < size; i++) {
						buffer[i] = mem_readb(dosBuff + i);
					}
					buffer[size] = 0;

					int sent = SDLNet_TCP_Send(sock.socket,
					                           buffer,
					                           size);
					if (sent < size) {

						LOG_MSG("NetSendData send failed: %d %d",
						        sent,
						        socketHandle);
						G_responseBuffer[0] = HIF_FAILED;
					} else {

						LOG_MSG("NetSendData send success");
						G_responseBuffer[0] = HIF_OK;
					}

					delete[] buffer;
				}
#endif
				G_responseOffset = 6;
			} else if (G_commandBuffer[0] == HIF_NC_RECV_NEXT_CLOSE) {

				G_responseBuffer[0] = HIF_OK;
				for (int i = 0; i < MaxSockets; i++) {

					if (NetSockets[i].used) {

						if (!NetSockets[i].done) {
							if (NetSockets[i].receiveDone) {

								NetSockets[i].done = true;
								G_responseBuffer[1] = i;
								if (i > 0) {
									LOG_MSG("NetRecvNextClosed: %d",
									        i);
								}
								G_responseOffset = 6;
								return;
							}
						}
					}
				}
				G_responseBuffer[1] = 0;
				G_responseOffset    = 6;
			} else if (G_commandBuffer[0] == HIF_NC_NEXT_RECV_SIZE) {

				// init, return value with no bug pending
				G_responseBuffer[0] = HIF_OK;
				G_responseBuffer[1] = 0;

				for (int i = 0; i < MaxSockets; i++) {

					if (NetSockets[i].used) {

						if (!NetSockets[i].done) {
							// check if received
							// data pending
							if (NetSockets[i].recvBufUsed >
							    0) {

								// return size;
								G_responseBuffer[1] =
								        NetSockets[i]
								                 .recvBufUsed;
								G_responseBuffer[2] = i;
								break;
							}
						}
					}
				}
				if (G_responseBuffer[1] > 0) {
					LOG_MSG("NetNextRecvSize: %d %d",
					        G_responseBuffer[1],
					        G_responseBuffer[2]);
				}
				G_responseOffset = 6;
			} else if (G_commandBuffer[0] == HIF_NC_RECV_NEXT) {

				// find available socket with
				for (int i = 0; i < MaxSockets; i++) {

					if (NetSockets[i].used) {

						if (!NetSockets[i].done) {
							// check if recived data
							// pending
							LOG_MSG("RECEIVENEXT: %d %d (%u)",
							        NetSockets[i].recvBufUsed,
							        G_commandBuffer[1],
							        i);

							if (NetSockets[i].recvBufUsed ==
							    G_commandBuffer[1]) {

								// found buffer
								// of right size
								// copy it
								// PhysPt
								// dosBuff =
								//        SegPhys(es)
								//        +
								//        reg_di;
								int size = G_commandBuffer[1];

								LOG_MSG("RECEIVENEXT: %x %x",
								        G_commandBuffer[2],
								        G_commandBuffer[3]);
								for (int i2 = 0;
								     i2 < size;
								     i2++) {
									mem_writeb(
									        static_cast<uint32_t>(
									                G_commandBuffer[2]
									                << 4) +
									                G_commandBuffer[3] +
									                i2,
									        NetSockets[i]
									                .recvBuf[i2]);
								}

								// mark unused,
								// so continue
								// receiving
								NetSockets[i].recvBufUsed = 0;

								G_responseBuffer[1] = i;
								break;
							}
						}
					}
					G_responseOffset = 6;
				}
			} else if (G_commandBuffer[0] == HIF_NC_DISCONNECT) {
				// actually close
				LOG_MSG("NetDisconnect: %d", G_commandBuffer[1]);
				SocketState& sock = NetSockets[G_commandBuffer[1]];

				if (sock.used) {
					sock.done = true;
					if (sock.stream) {
						NET_DestroyStreamSocket(
						        sock.stream);						
						sock.stream  = NULL;
					}
					sock.used = false;
				}
				G_responseBuffer[0] = HIF_OK;
				G_responseOffset    = 6;

			} else if (G_commandBuffer[0] == HIF_SSL_CTX_NEW) {
				LOG_MSG("!!!SSLContextNew");

				int method = 0; // client method

				int handle = AllocHandle(
				        tls_create_context(method, TLS_V12));

				SSL_set_io(reinterpret_cast<struct TLSContext*>(
				                   handles[handle - 1]),
				           (void*)SSLSocketRecv,
				           (void*)SSLSocketSend);

				//reg_ax = handle & 0xFFFF;
				//reg_dx = (handle >> 16) & 0xFFFF;
				LOG_MSG("!!!SSLContextNew context %x", handle);
				G_responseBuffer[HIF_SLOT_AX] = HIF_OK;
				G_responseBuffer[HIF_SLOT_DX] = handle & 0xFFFF;
				G_responseOffset    = 6;

			} else {

				LOG_INFO("GEOSHOST: Unhandled request code: %d",
				         G_commandBuffer[0]);			
			}
		}
	}
}

void geoshost_init(Section* /*sec*/) {

	IO_RegisterReadHandler(0x38FF, read_baseboxid, io_width_t::word);
	IO_RegisterWriteHandler(0x38FF, write_baseboxcmd, io_width_t::word);

	G_eventQueueMutex = SDL_CreateMutex();

	TIMER_AddTickHandler(GeosHost_TickHandler);

	LOG_INFO("GEOSHOST:Initialized");
}

void GeosHost_AddConfigSection(const ConfigPtr& conf)
{
	assert(conf);

	Section_prop* sec = conf->AddSection_prop("geoshost",
	                                          &geoshost_init);
	assert(sec);
}

#endif // C_GEOSHOST



