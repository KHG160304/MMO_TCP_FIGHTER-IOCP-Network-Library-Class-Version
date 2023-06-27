#pragma comment(lib, "ws2_32")
#pragma comment(lib, "winmm")
#include <winsock2.h>
#include <ws2tcpip.h>
#include <process.h>
#include <locale.h>
#include "AsyncNetworkServerEngine.h"
#include "Log.h"
#include "RingBuffer.h"
#include "SerializationBuffer.h"

typedef struct NET_PACKET_HEADER
{
	unsigned short		len;	// 메시지 길이
} NET_PACKET_HEADER, *PNET_PACKET_HEADER;

struct WsaOverlappedEX
{
	WSAOVERLAPPED overlapped;			//32
	PSession ptrSession;				//8
};

struct Session
{
	SOCKET socketForWSACall;			//8
	SOCKET socketForReleaseSession;		//16
	SOCKADDR_IN clientAddr;				//32
	SESSIONID sessionID;				//40
	WsaOverlappedEX sendOverlapped;		//80
	WsaOverlappedEX recvOverlapped;		//120
	RingBuffer sendRingBuffer;			//144
	RingBuffer recvRingBuffer;			//168
	SRWLOCK	sessionSRWLock;				//176
	uint32_t overlappedIOCnt;			//180
	BOOL bWaitSend;						//184

	Session(SOCKET sock, const SOCKADDR_IN* addr, SESSIONID id, uint32_t sessionRecvBufferSize, uint32_t sessionSendBufferSize)
		: socketForWSACall(sock)
		, socketForReleaseSession(sock)
		, clientAddr(*addr)
		, sessionID(id)
		, recvRingBuffer(sessionRecvBufferSize)
		, sendRingBuffer(sessionSendBufferSize)
		, sessionSRWLock(SRWLOCK_INIT)
		, overlappedIOCnt(0)
		, bWaitSend(false)
	{
		sendOverlapped.ptrSession = this;
		recvOverlapped.ptrSession = this;
	}
};

constexpr int FAILED_SEND_PACKET = -5;

AsyncNetworkServerEngine::AsyncNetworkServerEngine(uint16_t port, ISessionEventHandler* handler
	, uint32_t numberOfCreateIOCPWorkerThreads, uint32_t numberOfConcurrentIOCPWorkerThreads
	, uint32_t sessionRecvBufferSize, uint32_t sessionSendBufferSize)
	: mPort(port)
	, mWstrIPv4Addr{ 0 }
	, mIpv4AddrStrLen(0)
	, mListenSock(INVALID_SOCKET)
	, mServerAddr{ 0 }
	, mNumberOfCreateAcceptThreads(1)
	, mNumberOfCreateIOCPWorkerThreads(numberOfCreateIOCPWorkerThreads)
	, mNumberOfConcurrentIOCPWorkerThreads(numberOfConcurrentIOCPWorkerThreads)
	, mSessionRecvBufferSize(sessionRecvBufferSize)
	, mSessionSendBufferSize(sessionSendBufferSize)
	, mPtrSessionEventHandler(handler)
	, mhIOCP(INVALID_HANDLE_VALUE)
	, mhThreadAccept(INVALID_HANDLE_VALUE)
	, mhArrThreadIOCPWorker(nullptr)
	, mSessionIdCounter(0)
	, mSessionMapSRWLock(RTL_SRWLOCK_INIT)
{	
}

AsyncNetworkServerEngine::AsyncNetworkServerEngine(uint16_t port, const wchar_t wstrIPv4Addr[17], ISessionEventHandler* handler
	, uint32_t numberOfCreateIOCPWorkerThreads, uint32_t numberOfConcurrentIOCPWorkerThreads
	, uint32_t sessionRecvBufferSize, uint32_t sessionSendBufferSize)
	: mPort(port)
	, mListenSock(INVALID_SOCKET)
	, mServerAddr{ 0 }
	, mNumberOfCreateAcceptThreads(1)
	, mNumberOfCreateIOCPWorkerThreads(numberOfCreateIOCPWorkerThreads)
	, mNumberOfConcurrentIOCPWorkerThreads(numberOfConcurrentIOCPWorkerThreads)
	, mSessionRecvBufferSize(sessionRecvBufferSize)
	, mSessionSendBufferSize(sessionSendBufferSize)
	, mPtrSessionEventHandler(handler)
	, mhIOCP(INVALID_HANDLE_VALUE)
	, mhThreadAccept(INVALID_HANDLE_VALUE)
	, mhArrThreadIOCPWorker(nullptr)
	, mSessionIdCounter(0)
	, mSessionMapSRWLock(RTL_SRWLOCK_INIT)
{
	if ((mIpv4AddrStrLen = wcsnlen_s(wstrIPv4Addr, 17)) == 17)
	{
		_Log(dfLOG_LEVEL_SYSTEM, L"잘못된 길이의 주소를 입력하셨습니다.");
		// 크래시
		*((char*)(nullptr)) = 0;
	}

	wcsncpy_s(mWstrIPv4Addr, wstrIPv4Addr, 17);
}

AsyncNetworkServerEngine::~AsyncNetworkServerEngine()
{
	ExitAllThread();
	ReleaseAllSession();

	CloseHandle(mhIOCP);
	CloseHandle(mhThreadAccept);
	for (uint32_t i = 0; i < mNumberOfCreateIOCPWorkerThreads; ++i)
	{
		CloseHandle(mhArrThreadIOCPWorker[i]);
	}
	
	delete[] mhArrThreadIOCPWorker;
	_Log(dfLOG_LEVEL_SYSTEM, "서버 종료 처리 완료");
	timeEndPeriod(1);
}

uint32_t AsyncNetworkServerEngine::GetAcceptThreadsCnt()
{
	return mNumberOfCreateAcceptThreads;
}
uint32_t AsyncNetworkServerEngine::GetIOCPWorkerThreadsCnt()
{
	return mNumberOfCreateIOCPWorkerThreads;
}

void AsyncNetworkServerEngine::ExitAllThread()
{
	closesocket(mListenSock); //Accept Thread 종료를 위해서 리슨 소켓 종료;
	for (DWORD i = 0; i < mNumberOfCreateIOCPWorkerThreads; ++i)
	{
		PostQueuedCompletionStatus(mhIOCP, 0, 0, nullptr);
	}

	if (WaitForSingleObject(mhThreadAccept, INFINITE) == WAIT_FAILED)
	{
		_Log(dfLOG_LEVEL_SYSTEM, "WaitForSingleObject failed: %d\n", GetLastError());
		_Log(dfLOG_LEVEL_SYSTEM, "accept thread exit faild\n");
	}
	else
	{
		_Log(dfLOG_LEVEL_SYSTEM, "accept thread 종료");
	}

	if (WaitForMultipleObjects(mNumberOfCreateIOCPWorkerThreads, mhArrThreadIOCPWorker, true, INFINITE) == WAIT_FAILED)
	{
		_Log(dfLOG_LEVEL_SYSTEM, "WaitForMultipleObjects failed: %d\n", GetLastError());
		_Log(dfLOG_LEVEL_SYSTEM, "iocp worker thread exit faild\n");
	}
	else
	{
		_Log(dfLOG_LEVEL_SYSTEM, "iocp worker thread 종료");
	}

	_Log(dfLOG_LEVEL_SYSTEM, "스레드 종료 완료");
}

void AsyncNetworkServerEngine::ReleaseAllSession()
{
	_Log(dfLOG_LEVEL_SYSTEM, "all session release 시작");
	Session* ptrSession;
	std::unordered_map<SESSIONID, Session*>::iterator iter = mSessionMap.begin();
	while (iter != mSessionMap.end())
	{
		ptrSession = iter->second;
		++iter;
		ReleaseSession(ptrSession);
	}
	_Log(dfLOG_LEVEL_SYSTEM, "all session release 완료");
}

void AsyncNetworkServerEngine::Start()
{
	timeBeginPeriod(1);
	_wsetlocale(LC_ALL, L"korean");

	if (!OpenServerPort())
	{
		timeEndPeriod(1);
		_Log(dfLOG_LEVEL_SYSTEM, L"Server Start failed - Port Open failed");
		return;
	}

	if (!InitNetworkIOThread())
	{
		timeEndPeriod(1);
		_Log(dfLOG_LEVEL_SYSTEM, L"Server Start failed - Init Network IOThread faild");
		return;
	}
}

bool AsyncNetworkServerEngine::OpenServerPort()
{
	WSADATA wsa;
	WCHAR wstrIp[17] = { 0, };
	SOCKET tmpListenSock;
	SOCKADDR_IN tmpServerAddr;

	if (WSAStartup(MAKEWORD(2, 2), &wsa) != NO_ERROR)
	{
		_Log(dfLOG_LEVEL_SYSTEM, L"WSAStartup() failed: %d", WSAGetLastError());
		return false;
	}

	tmpListenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (tmpListenSock == INVALID_SOCKET)
	{
		_Log(dfLOG_LEVEL_SYSTEM, L"listen socket() failed: %d", WSAGetLastError());
		return false;
	}

	linger optLinger = { 1, 0 };
	setsockopt(tmpListenSock, SOL_SOCKET, SO_LINGER, (char*)&optLinger, sizeof(linger));

	DWORD soSndBUfSize = 0;
	setsockopt(tmpListenSock, SOL_SOCKET, SO_SNDBUF, (char*)&soSndBUfSize, sizeof(DWORD));

	tmpServerAddr.sin_family = AF_INET;
	tmpServerAddr.sin_port = htons(mPort);
	if (mIpv4AddrStrLen == 0)
	{
		tmpServerAddr.sin_addr.s_addr = INADDR_ANY;
	}
	else
	{
		IN_ADDR inAddr;
		if (!InetPton(AF_INET, mWstrIPv4Addr, &inAddr))
		{
			_Log(dfLOG_LEVEL_SYSTEM, L"InetPton() failed: %d", WSAGetLastError());
			goto InitError;
		}
		tmpServerAddr.sin_addr = inAddr;
	}

	if (bind(tmpListenSock, (SOCKADDR*)&tmpServerAddr, sizeof(tmpServerAddr)) == SOCKET_ERROR)
	{
		_Log(dfLOG_LEVEL_SYSTEM, L"bind() failed: %d", WSAGetLastError());
		goto InitError;
	}

	if (listen(tmpListenSock, SOMAXCONN) == SOCKET_ERROR)
	{
		_Log(dfLOG_LEVEL_SYSTEM, L"listen() failed: %d", WSAGetLastError());
		goto InitError;
	}

	_Log(dfLOG_LEVEL_SYSTEM, L"Server Port Open [%d/%ws] ", mPort, InetNtopW(AF_INET, &tmpServerAddr.sin_addr, wstrIp, 17));

	mListenSock = tmpListenSock;
	mServerAddr = tmpServerAddr;
	return true;

InitError:
	closesocket(tmpListenSock);
	return false;
}

bool AsyncNetworkServerEngine::InitNetworkIOThread()
{
	SYSTEM_INFO systemInfo;
	int idx;
	int concurrentThreadCnt;
	int createIOCPWorkerThreadCnt;
	if (mNumberOfCreateIOCPWorkerThreads != 0 && mNumberOfConcurrentIOCPWorkerThreads != 0)
	{
		concurrentThreadCnt = mNumberOfConcurrentIOCPWorkerThreads;
		createIOCPWorkerThreadCnt = mNumberOfCreateIOCPWorkerThreads;
	}
	else
	{
		GetSystemInfo(&systemInfo);
		concurrentThreadCnt = (systemInfo.dwNumberOfProcessors / 2);
		createIOCPWorkerThreadCnt = 0;

		if (systemInfo.dwNumberOfProcessors > 5)
		{
			createIOCPWorkerThreadCnt = concurrentThreadCnt + 1;
		}
		else if (systemInfo.dwNumberOfProcessors > 1)
		{
			createIOCPWorkerThreadCnt = concurrentThreadCnt;
		}
		else
		{
			_Log(dfLOG_LEVEL_SYSTEM, "현재 서버의 논리코어가 1개만 존재합니다. 시스템 구동이 불가능 합니다.");
			CloseHandle(mhIOCP);
			return false;
		}
	}

	mhIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, concurrentThreadCnt);
	if (mhIOCP == nullptr)
	{
		_Log(dfLOG_LEVEL_SYSTEM, "CreateIoCompletionPort() failed: %d", GetLastError());
		return false;
	}

	this->mhArrThreadIOCPWorker = new HANDLE[createIOCPWorkerThreadCnt];
	for (idx = 0; idx < createIOCPWorkerThreadCnt; ++idx)
	{
		this->mhArrThreadIOCPWorker[idx] = (HANDLE)_beginthreadex(nullptr, 0, (_beginthreadex_proc_type)AsyncNetworkServerEngine::IOCPWorkerThread, this, 0, nullptr);
		if (this->mhArrThreadIOCPWorker[idx] == nullptr)
		{
			closesocket(mListenSock);
			_Log(dfLOG_LEVEL_SYSTEM, "_beginthreadex(IOCPWorkerThread) failed: %d", GetLastError());
			for (int j = 0; j < idx; ++j)
			{
				PostQueuedCompletionStatus(mhIOCP, 0, 0, nullptr);
			}
			CloseHandle(mhIOCP);
			delete[] mhArrThreadIOCPWorker;
			return false;
		}
		SetThreadPriority(this->mhArrThreadIOCPWorker[idx], THREAD_PRIORITY_TIME_CRITICAL);
	}

	mhThreadAccept = (HANDLE)_beginthreadex(nullptr, 0, (_beginthreadex_proc_type)AsyncNetworkServerEngine::AcceptThread, this, 0, nullptr);
	if (mhThreadAccept == nullptr)
	{
		closesocket(mListenSock);
		_Log(dfLOG_LEVEL_SYSTEM, "_beginthreadex(AcceptThread) failed: %d", GetLastError());
		for (idx = 0; idx < createIOCPWorkerThreadCnt; ++idx)
		{
			PostQueuedCompletionStatus(mhIOCP, 0, 0, nullptr);
		}
		CloseHandle(mhIOCP);
		delete[] mhArrThreadIOCPWorker;
		return false;
	}
	mNumberOfConcurrentIOCPWorkerThreads = concurrentThreadCnt;
	mNumberOfCreateIOCPWorkerThreads = createIOCPWorkerThreadCnt;

	return true;
}

uint32_t WINAPI AsyncNetworkServerEngine::IOCPWorkerThread(AsyncNetworkServerEngine* ptrServerEngine)
{
	int retvalGQCS;
	HANDLE hTmpIOCP = ptrServerEngine->mhIOCP;
	ISessionEventHandler* ptrSessionEventHandler = ptrServerEngine->mPtrSessionEventHandler;
	DWORD numberOfBytesTransferred = 0;
	SESSIONID sessionID = 0;
	WsaOverlappedEX* overlapped = 0;
	PSession ptrSession;
	RingBuffer* ptrRecvRingBuffer;
	RingBuffer* ptrSendRingBuffer;
	
	int recvQueueUseSize;
	SerializationBuffer recvPacket;
	NET_PACKET_HEADER recvPacketHeader;

	uint64_t* ptrMonitorCompleteRecvIOCnt = &ptrServerEngine->monitorCompleteRecvIOCnt;
	uint64_t* ptrMonitorCompleteSendIOCnt = &ptrServerEngine->monitorCompleteSendIOCnt;
	uint64_t* ptrMonitorRecvPacketBytes = &ptrServerEngine->monitorRecvPacketBytes;
	uint64_t* ptrMonitorSendPacketBytes = &ptrServerEngine->monitorSendPacketBytes;
	for (;;)
	{
		numberOfBytesTransferred = 0;
		sessionID = 0;
		overlapped = 0;
		retvalGQCS = GetQueuedCompletionStatus(hTmpIOCP, &numberOfBytesTransferred, &sessionID, (LPOVERLAPPED*)&overlapped, INFINITE);
		if (numberOfBytesTransferred == 0 && sessionID == 0 && overlapped == nullptr)
		{
			_Log(dfLOG_LEVEL_SYSTEM, "[No.%d] IOCPWorkerThread Exit", GetCurrentThreadId());
			return 0;
		}

		ptrSession = overlapped->ptrSession;
		if (retvalGQCS == FALSE)
		{
			goto FIN_COMPLETION_IO_PROCESS;
		}

		if (&ptrSession->recvOverlapped == overlapped)
		{
			if (numberOfBytesTransferred == 0)
			{
				goto FIN_COMPLETION_IO_PROCESS;
			}

			InterlockedExchangeAdd64((LONG64*)ptrMonitorRecvPacketBytes, numberOfBytesTransferred);
			InterlockedIncrement(ptrMonitorCompleteRecvIOCnt);
			ptrRecvRingBuffer = &ptrSession->recvRingBuffer;
			ptrRecvRingBuffer->MoveRear(numberOfBytesTransferred);
			for (;;)
			{
				//  이시점에 clear 필요 언제 break 해서 빠져 나갈지 모르기때문에
				recvPacket.ClearBuffer();
				recvQueueUseSize = ptrRecvRingBuffer->GetUseSize();
				if (recvQueueUseSize <= sizeof(recvPacketHeader))
				{
					break;
				}

				ptrRecvRingBuffer->Peek((char*)&recvPacketHeader, sizeof(recvPacketHeader));
				if (recvQueueUseSize < (sizeof(recvPacketHeader) + recvPacketHeader.len))
				{
					break;
				}
				ptrRecvRingBuffer->MoveFront(sizeof(recvPacketHeader));
				ptrRecvRingBuffer->Dequeue(recvPacket.GetRearBufferPtr(), recvPacketHeader.len);
				recvPacket.MoveRear(recvPacketHeader.len);

				ptrSessionEventHandler->OnRecv(sessionID, recvPacket);
			}

			ptrServerEngine->PostRecv(ptrSession);
		}
		else if (&ptrSession->sendOverlapped == overlapped && numberOfBytesTransferred != FAILED_SEND_PACKET)
		{
			InterlockedExchangeAdd64((LONG64*)ptrMonitorSendPacketBytes, numberOfBytesTransferred);
			InterlockedIncrement(ptrMonitorCompleteSendIOCnt);
			ptrSendRingBuffer = &ptrSession->sendRingBuffer;
			ptrSession->sendRingBuffer.MoveFront(numberOfBytesTransferred);
			// InterLocked 함수 호출로 인해서, MoveFront의 결과가 확실하게
			// CPU 캐시라인에 반영되기를 기대한다.
			InterlockedExchange((LONG*)&ptrSession->bWaitSend, false);
			if (ptrSession->sendRingBuffer.GetUseSize() > 0)
			{
				ptrServerEngine->PostSend(ptrSession);
			}
		}

		FIN_COMPLETION_IO_PROCESS:
		if (InterlockedDecrement((LONG*)&ptrSession->overlappedIOCnt) == 0)
		{
			//세션 삭제
			ptrServerEngine->ReleaseSession(ptrSession);
		}
	}
}

uint32_t WINAPI AsyncNetworkServerEngine::AcceptThread(AsyncNetworkServerEngine* ptrServerEngine)
{
	ISessionEventHandler* ptrSessionEventHandler = ptrServerEngine->mPtrSessionEventHandler;
	SOCKET tmpListenSock = ptrServerEngine->mListenSock;
	SOCKET clientSock;
	SOCKADDR_IN clientAddr;
	int addrlen = sizeof(clientAddr);
	int acceptErrorCode;
	PSession ptrSession;
	for (;;)
	{
		clientSock = accept(tmpListenSock, (SOCKADDR*)&clientAddr, &addrlen);
		if (clientSock == INVALID_SOCKET)
		{
			acceptErrorCode = WSAGetLastError();
			// listen 소켓을 닫은 경우
			if (acceptErrorCode == WSAENOTSOCK || acceptErrorCode == WSAEINTR)
			{
				_Log(dfLOG_LEVEL_SYSTEM, "AcceptThread Exit");
				return 0;
			}

			_Log(dfLOG_LEVEL_SYSTEM, "accept() failed: %d", acceptErrorCode);
			closesocket(clientSock);
			continue;
		}

		ptrSession = ptrServerEngine->CreateSession(clientSock, &clientAddr);

		InterlockedIncrement((LONG*)&ptrSession->overlappedIOCnt);
		ptrSessionEventHandler->OnAccept(ptrSession->sessionID);
		ptrServerEngine->PostRecv(ptrSession);

		if (InterlockedDecrement((LONG*)&ptrSession->overlappedIOCnt) == 0)
		{
			ptrServerEngine->ReleaseSession(ptrSession);
		}
	}
}

void AsyncNetworkServerEngine::PostRecv(PSession ptrSession)
{
	RingBuffer* ptrRecvRingBuffer = &ptrSession->recvRingBuffer;
	WSABUF wsabuf[2];
	int wsabufCnt = 1;
	int recvRingBufferFreeSize;
	DWORD flag = 0;
	int wsaRecvErrorCode;

	wsabuf[0].buf = ptrRecvRingBuffer->GetRearBufferPtr();
	wsabuf[0].len = ptrRecvRingBuffer->GetDirectEnqueueSize();
	recvRingBufferFreeSize = ptrRecvRingBuffer->GetFreeSize();
	if (recvRingBufferFreeSize > (int)wsabuf[0].len)
	{
		wsabuf[1].buf = ptrRecvRingBuffer->GetInternalBufferPtr();
		wsabuf[1].len = recvRingBufferFreeSize - (int)wsabuf[0].len;
		++wsabufCnt;
	}

	ZeroMemory(&ptrSession->recvOverlapped, sizeof(WSAOVERLAPPED));
	InterlockedIncrement((LONG*)&ptrSession->overlappedIOCnt);
	if (WSARecv(ptrSession->socketForWSACall, wsabuf, wsabufCnt, nullptr, &flag
		, (LPWSAOVERLAPPED)&ptrSession->recvOverlapped, nullptr) == SOCKET_ERROR
		&& (wsaRecvErrorCode = WSAGetLastError()) != WSA_IO_PENDING)
	{
		if (wsaRecvErrorCode != 10054 && wsaRecvErrorCode != 10038)
		{
			_Log(dfLOG_LEVEL_SYSTEM, "WSARecv error code: %d", wsaRecvErrorCode);
		}

		if (InterlockedDecrement((LONG*)&ptrSession->overlappedIOCnt) == 0)
		{
			ReleaseSession(ptrSession);
		}
	}
}

void AsyncNetworkServerEngine::PostSend(PSession ptrSession)
{
	if (InterlockedExchange((LONG*)&ptrSession->bWaitSend, true))
	{
		return;
	}

	RingBuffer* ptrSendRingBuffer = &ptrSession->sendRingBuffer;
	WSABUF wsabuf[2];
	int wsabufCnt;
	int wsaSendErrorCode;
	char* ptrRear;
	_int64 distanceOfRearToFront;

	wsabuf[0].buf = ptrSendRingBuffer->GetFrontBufferPtr();

	ptrRear = ptrSendRingBuffer->GetRearBufferPtr();
	distanceOfRearToFront = (ptrRear - wsabuf[0].buf);

	if (distanceOfRearToFront > 0)
	{
		wsabuf[0].len = ptrSendRingBuffer->GetDirectDequeueSize();
		wsabufCnt = 1;
	}
	else if (distanceOfRearToFront < 0)
	{
		wsabuf[0].len = ptrSendRingBuffer->GetDirectDequeueSize();

		wsabuf[1].buf = ptrSendRingBuffer->GetInternalBufferPtr();
		wsabuf[1].len = (ULONG)(ptrSendRingBuffer->GetRearBufferPtr() - wsabuf[1].buf);
		wsabufCnt = 2;
	}
	else
	{
		InterlockedExchange((LONG*)&ptrSession->bWaitSend, false);
		return;
	}

	ZeroMemory(&ptrSession->sendOverlapped, sizeof(WSAOVERLAPPED));
	if (InterlockedIncrement((LONG*)&ptrSession->overlappedIOCnt) == 1)
	{
		InterlockedExchange((LONG*)&ptrSession->overlappedIOCnt, 0);
		return;
	}

	if (WSASend(ptrSession->socketForWSACall, wsabuf, wsabufCnt, nullptr, 0
		, (LPWSAOVERLAPPED)&ptrSession->sendOverlapped, nullptr) == SOCKET_ERROR
		&& (wsaSendErrorCode = WSAGetLastError()) != WSA_IO_PENDING)
	{
		if (wsaSendErrorCode != 10054 && wsaSendErrorCode != 10038)
		{
			_Log(dfLOG_LEVEL_SYSTEM, "WSASend failed: %d", wsaSendErrorCode);
		}

		// 여기서 세션을 삭제하는 코드를 넣지 않아서 ,힙이 계속 해제가 되지 않는 문제가 발생
		if (InterlockedDecrement((LONG*)&ptrSession->overlappedIOCnt) == 0)
		{
			ReleaseSession(ptrSession);
		}
	}
}

bool AsyncNetworkServerEngine::PostSendForSendPacket(PSession ptrSession)
{
	if (InterlockedExchange((LONG*)&ptrSession->bWaitSend, true))
	{
		return true;
	}

	if (InterlockedIncrement((LONG*)&ptrSession->overlappedIOCnt) == 1)
	{
		InterlockedExchange((LONG*)&ptrSession->overlappedIOCnt, 0);
		return false;
	}

	RingBuffer* ptrSendRingBuffer = &ptrSession->sendRingBuffer;
	WSABUF wsabuf[2];
	int wsabufCnt;
	int wsaSendErrorCode;
	char* ptrRear;
	_int64 distanceOfRearToFront;

	wsabuf[0].buf = ptrSendRingBuffer->GetFrontBufferPtr();

	ptrRear = ptrSendRingBuffer->GetRearBufferPtr();
	distanceOfRearToFront = (ptrRear - wsabuf[0].buf);

	if (distanceOfRearToFront > 0)
	{
		wsabuf[0].len = ptrSendRingBuffer->GetDirectDequeueSize();
		wsabufCnt = 1;
	}
	else if (distanceOfRearToFront < 0)
	{
		wsabuf[0].len = ptrSendRingBuffer->GetDirectDequeueSize();

		wsabuf[1].buf = ptrSendRingBuffer->GetInternalBufferPtr();
		wsabuf[1].len = (ULONG)(ptrSendRingBuffer->GetRearBufferPtr() - wsabuf[1].buf);
		wsabufCnt = 2;
	}
	else
	{
		InterlockedExchange((LONG*)&ptrSession->bWaitSend, false);
		PostQueuedCompletionStatus(mhIOCP, FAILED_SEND_PACKET, ptrSession->sessionID, (LPOVERLAPPED)&ptrSession->sendOverlapped);
		return false;
	}

	ZeroMemory(&ptrSession->sendOverlapped, sizeof(WSAOVERLAPPED));
	if (WSASend(ptrSession->socketForWSACall, wsabuf, wsabufCnt, nullptr, 0
		, (LPWSAOVERLAPPED)&ptrSession->sendOverlapped, nullptr) == SOCKET_ERROR
		&& (wsaSendErrorCode = WSAGetLastError()) != WSA_IO_PENDING)
	{
		if (wsaSendErrorCode != 10054 && wsaSendErrorCode != 10038)
		{
			_Log(dfLOG_LEVEL_SYSTEM, "WSASend failed: %d", wsaSendErrorCode);
		}

		PostQueuedCompletionStatus(mhIOCP, FAILED_SEND_PACKET, ptrSession->sessionID, (LPOVERLAPPED)&ptrSession->sendOverlapped);
		return false;
	}

	return true;
}

PSession AsyncNetworkServerEngine::CreateSession(SOCKET clientSock, const SOCKADDR_IN* clientAddr)
{
	CreateIoCompletionPort((HANDLE)clientSock, mhIOCP, mSessionIdCounter, 0);
	Session* ptrNewSession = new Session(clientSock, clientAddr
		, mSessionIdCounter, mSessionRecvBufferSize, mSessionSendBufferSize);
	AcquireSRWLockExclusive(&mSessionMapSRWLock);
	mSessionMap.insert({ mSessionIdCounter, ptrNewSession });
	ReleaseSRWLockExclusive(&mSessionMapSRWLock);
	mSessionIdCounter = (mSessionIdCounter + 1) % INVALID_SESSION_ID;
	return ptrNewSession;
}

void AsyncNetworkServerEngine::ReleaseSession(PSession ptrSession)
{
	//세션 삭제
	closesocket(ptrSession->socketForReleaseSession);
	AcquireSRWLockExclusive(&mSessionMapSRWLock);
	mSessionMap.erase(ptrSession->sessionID);
	AcquireSRWLockExclusive(&ptrSession->sessionSRWLock);
	ReleaseSRWLockExclusive(&mSessionMapSRWLock);
	mPtrSessionEventHandler->OnRelease(ptrSession->sessionID);
	ReleaseSRWLockExclusive(&ptrSession->sessionSRWLock);
	delete ptrSession;
}

bool AsyncNetworkServerEngine::FindSession(SESSIONID sessionID, PSession* outSession)
{
	std::unordered_map<SESSIONID, PSession>::iterator iter = mSessionMap.find(sessionID);
	if (iter != mSessionMap.end())
	{
		*outSession = iter->second;
		return true;
	}

	return false;
}

size_t AsyncNetworkServerEngine::GetSessionCnt()
{
	size_t size;
	AcquireSRWLockShared(&mSessionMapSRWLock);
	size = mSessionMap.size();
	ReleaseSRWLockShared(&mSessionMapSRWLock);
	return size;
}

bool AsyncNetworkServerEngine::SendPacket(SESSIONID sessionID, const SerializationBuffer& refSendPacket)
{
	PSession ptrSession;
	RingBuffer* ptrSendRingBuffer;
	NET_PACKET_HEADER netPacketHeader;
	bool isSessionOpen;

	AcquireSRWLockExclusive(&mSessionMapSRWLock);
	if (!FindSession(sessionID, &ptrSession))
	{
		ReleaseSRWLockExclusive(&mSessionMapSRWLock);
		return false;
	}
	AcquireSRWLockExclusive(&ptrSession->sessionSRWLock);
	ReleaseSRWLockExclusive(&mSessionMapSRWLock);

	if ((netPacketHeader.len = refSendPacket.GetUseSize()) < 1)
	{
		_Log(dfLOG_LEVEL_SYSTEM, "송신패킷 0 size");
		ReleaseSRWLockExclusive(&ptrSession->sessionSRWLock);
		return false;
	}

	ptrSendRingBuffer = &ptrSession->sendRingBuffer;
	if (ptrSendRingBuffer->GetFreeSize() < (netPacketHeader.len + sizeof(netPacketHeader)))
	{
		_Log(dfLOG_LEVEL_SYSTEM, "송신패킷 SendRingBuffer Enqueue 실패 크기: %lld", (netPacketHeader.len + sizeof(netPacketHeader)));
		ReleaseSRWLockExclusive(&ptrSession->sessionSRWLock);
		return false;
	}
	ptrSendRingBuffer->Enqueue((char*)&netPacketHeader, sizeof(netPacketHeader));
	ptrSendRingBuffer->Enqueue(refSendPacket.GetFrontBufferPtr(), netPacketHeader.len);

	isSessionOpen = PostSendForSendPacket(ptrSession);
	ReleaseSRWLockExclusive(&ptrSession->sessionSRWLock);
	return isSessionOpen;
}

void AsyncNetworkServerEngine::Disconnect(SESSIONID sessionID)
{
	PSession ptrSession;
	AcquireSRWLockExclusive(&mSessionMapSRWLock);
	if (!FindSession(sessionID, &ptrSession))
	{
		ReleaseSRWLockExclusive(&mSessionMapSRWLock);
		return;
	}
	AcquireSRWLockExclusive(&ptrSession->sessionSRWLock);
	ReleaseSRWLockExclusive(&mSessionMapSRWLock);

	if (InterlockedExchange((SOCKET*)&ptrSession->socketForWSACall, INVALID_SOCKET) == INVALID_SOCKET)
	{
		ReleaseSRWLockExclusive(&ptrSession->sessionSRWLock);
		return;
	}

	CancelIoEx((HANDLE)ptrSession->socketForReleaseSession, nullptr);
	ReleaseSRWLockExclusive(&ptrSession->sessionSRWLock);
}