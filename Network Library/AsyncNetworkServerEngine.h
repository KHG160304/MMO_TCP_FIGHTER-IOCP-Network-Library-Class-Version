#pragma once
#ifndef __ASYNC_NETWORK_SERVER_ENGINE__
#define	__ASYNC_NETWORK_SERVER_ENGINE__

#include <stdint.h>
#include <unordered_map>
#include <winsock2.h>
#include "ISessionEventHandler.h"

typedef struct Session Session, *PSession;

// null 터미네이터 포함x
constexpr int IPv4_MAX_STR_LEN = 16;
constexpr uint32_t DEFAULT_SESSION_BUFFER_SIZE = 4096;

class AsyncNetworkServerEngine
{
public:
	AsyncNetworkServerEngine(uint16_t port, ISessionEventHandler* handler
		, uint32_t numberOfCreateIOCPWorkerThreads = 0, uint32_t numberOfConcurrentIOCPWorkerThreads = 0
		, uint32_t sessionRecvBufferSize = DEFAULT_SESSION_BUFFER_SIZE, uint32_t sessionSendBufferSize = DEFAULT_SESSION_BUFFER_SIZE);
	AsyncNetworkServerEngine(uint16_t port, const wchar_t wstrIPv4Addr[17], ISessionEventHandler* handler
		, uint32_t numberOfCreateIOCPWorkerThreads = 0, uint32_t numberOfConcurrentIOCPWorkerThreads = 0
		, uint32_t sessionRecvBufferSize = DEFAULT_SESSION_BUFFER_SIZE, uint32_t sessionSendBufferSize = DEFAULT_SESSION_BUFFER_SIZE);

	~AsyncNetworkServerEngine();
	void Start();

	bool SendPacket(SESSIONID sessionID, const SerializationBuffer& refSendPacket);
	void Disconnect(SESSIONID);

	uint32_t GetAcceptThreadsCnt();
	uint32_t GetIOCPWorkerThreadsCnt();
	size_t GetSessionCnt();
private:
	void PostRecv(const PSession ptrSession);
	void PostSend(const PSession ptrSession);
	bool PostSendForSendPacket(const PSession ptrSession);

	PSession CreateSession(SOCKET clientSock, const SOCKADDR_IN* clientAddr);
	void ReleaseSession(const PSession ptrSession);
	bool FindSession(SESSIONID sessionID, PSession* outSession);

	static uint32_t WINAPI IOCPWorkerThread(AsyncNetworkServerEngine* args);
	static uint32_t WINAPI AcceptThread(AsyncNetworkServerEngine* args);
	
	bool OpenServerPort();
	bool InitNetworkIOThread();
	
	void ExitAllThread();
	void ReleaseAllSession();

	
private:
	ISessionEventHandler* mPtrSessionEventHandler;	//48

	uint32_t mPort;										//4
	wchar_t mWstrIPv4Addr[IPv4_MAX_STR_LEN + 4];		//24	
	size_t mIpv4AddrStrLen;								//32

	SOCKET mListenSock;				//40
	SOCKADDR_IN mServerAddr;		//56

	HANDLE mhIOCP;					//64
	HANDLE mhThreadAccept;			//8
	HANDLE* mhArrThreadIOCPWorker;	//16

	uint32_t mNumberOfCreateAcceptThreads;			//20
	uint32_t mNumberOfCreateIOCPWorkerThreads;		//24
	uint32_t mNumberOfConcurrentIOCPWorkerThreads;	//28

	uint32_t mSessionRecvBufferSize;				//32
	uint32_t mSessionSendBufferSize;				//36 + 4

	SESSIONID mSessionIdCounter;					//56
	std::unordered_map<SESSIONID, PSession> mSessionMap;
	SRWLOCK	mSessionMapSRWLock;

public:
	uint64_t monitorSessionCnt;
	uint64_t monitorCompleteRecvIOCnt;
	uint64_t monitorCompleteSendIOCnt;
};

#endif // !__NETWORK_SERVER_ENGINE__