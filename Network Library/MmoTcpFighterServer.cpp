#include <process.h>
#include "MmoTcpFighterServer.h"
#include "MmoTcpFighterNetworkMessageProtocol.h"
#include "Log.h"
#include "SerializationBuffer.h"
#include "MmoTcpFighterServer_network_message.h"

static const wchar_t* dirTable[8] = { L"LL", L"LU", L"UU", L"RU", L"RR", L"RD", L"DD", L"LD" };

MmoTcpFighterServer::MmoTcpFighterServer(uint16_t port)
	: mhThreadUpdate(0)
	, mServerEngine(port, this, 6, 6)
	, mIsUpdateThreadRunning(true)
	, mMonitorLoopCnt(0)
	, mMonitorFrameCnt(0)
{

}

MmoTcpFighterServer::~MmoTcpFighterServer()
{
	mIsUpdateThreadRunning = false;
	if (WaitForSingleObject(mhThreadUpdate, INFINITE) == WAIT_FAILED)
	{
		_Log(dfLOG_LEVEL_SYSTEM, "hThreadUpdate - WaitForSingleObject error code: %d", GetLastError());
	}

	CloseHandle(mhThreadUpdate);

	mCharacterManager.ReleaseAllCharacterInfo();
}

void MmoTcpFighterServer::Start()
{
	if (!InitTCPFighterContentThread())
	{
		_Log(dfLOG_LEVEL_SYSTEM, "MMO TCP Fighter Server Start failed");
		return;
	}

	mServerEngine.Start();
}

void MmoTcpFighterServer::Monitoring()
{
	static DWORD startMeasurementTime = timeGetTime();

	DWORD endMeasurementTime = timeGetTime();
	DWORD interval = endMeasurementTime - startMeasurementTime;
	static int printCnt = 0;
	uint64_t printRecvIOCntPerSec;
	uint64_t printSendIOCntPerSec;
	uint64_t printRecvPacketBytesPerSec;
	uint64_t printSendPacketBytesPerSec;

	uint64_t printRecvPacketBytesPerOneIO = 0;
	uint64_t printSendPacketBytesPerOneIO = 0;
	if (interval >= 1000)
	{
		printRecvPacketBytesPerSec = InterlockedExchange((ULONG64*)&mServerEngine.monitorRecvPacketBytes, 0);
		printSendPacketBytesPerSec = InterlockedExchange((ULONG64*)&mServerEngine.monitorSendPacketBytes, 0);
		printRecvIOCntPerSec = InterlockedExchange((ULONG64*)&mServerEngine.monitorCompleteRecvIOCnt, 0);
		printSendIOCntPerSec = InterlockedExchange((ULONG64*)&mServerEngine.monitorCompleteSendIOCnt, 0);
		if (printCnt == 11)
		{
			if (printRecvIOCntPerSec != 0)
			{
				printRecvPacketBytesPerOneIO = printRecvPacketBytesPerSec / printRecvIOCntPerSec;
			}
			if (printSendIOCntPerSec != 0)
			{
				printSendPacketBytesPerOneIO = printSendPacketBytesPerSec / printSendIOCntPerSec;
			}

			printCnt = 0;
			_Log(dfLOG_LEVEL_SYSTEM,
				"\n-----------------------------------------------\n"
				"Accept Threads Count: %d\n"
				"IOCPWorker Threads Count: %d\n"
				"-----------------------------------------------\n"
				"FPS: %lld\n"
				"Loop/sec: %lld\n"
				"-----------------------------------------------\n"
				"Complete Recv IO/sec: %lld\n"
				"Complete Send IO/sec: %lld\n"
				"Recv Packet Size/sec: %lld\n"
				"Send Packet Size/sec: %lld\n"
				"Avr. One Recv IO Packet Size: %lld\n"
				"Avr. One Send IO Packet Size: %lld\n"
				"-----------------------------------------------\n"
				"Session Count: %lld\n"
				"Character Count: %lld\n"
				"Sector Character Count: %lld\n\n"
				, mServerEngine.GetAcceptThreadsCnt(), mServerEngine.GetIOCPWorkerThreadsCnt()
				, mMonitorFrameCnt, mMonitorLoopCnt
				, printRecvIOCntPerSec, printSendIOCntPerSec
				, printRecvPacketBytesPerSec, printSendPacketBytesPerSec
				, printRecvPacketBytesPerOneIO, printSendPacketBytesPerOneIO
				, mServerEngine.GetSessionCnt(), mCharacterManager.GetCharacterCnt(), mSectorManager.GetSectorCharacterCnt());
		}

		startMeasurementTime = endMeasurementTime - (interval - 1000);
		mMonitorFrameCnt = 0;
		mMonitorLoopCnt = 0;
		printCnt += 1;

	}
}

void MmoTcpFighterServer::OnRecv(SESSIONID sessionID, SerializationBuffer& refSendPacket)
{
	PCharacterInfo ptrCharacter;
	AcquireSRWLockShared(mCharacterManager.GetCharacterContainerLock());
	mCharacterManager.FindCharacter(sessionID, &ptrCharacter);
	AcquireSRWLockExclusive(&ptrCharacter->srwCharacterLock);
	ReleaseSRWLockShared(mCharacterManager.GetCharacterContainerLock());
	ptrCharacter->lastRecvTime = timeGetTime();
	//_Log(dfLOG_LEVEL_SYSTEM, "update lastRecvTime sessionID: %lld", sessionID);
	ReleaseSRWLockExclusive(&ptrCharacter->srwCharacterLock);

	DispatchPacketToContents(sessionID, refSendPacket);
}

void MmoTcpFighterServer::OnAccept(SESSIONID sessionID)
{
	CharacterInfo* characInfo = new CharacterInfo;
	mCharacterManager.InitCharacterInfo(sessionID, characInfo);
	mSectorManager.InitCharactorSectorInfo(characInfo);

	SerializationBuffer sendPacket;
	TcpFighterMessage::MakePacketCreateMyCharacter(sendPacket, characInfo);
	mServerEngine.SendPacket(sessionID, sendPacket);

	TcpFighterMessage::ConvertPacketCreateMyCharaterToCreateOtherCharacter(sendPacket);
	AcquireSRWLockExclusive(mCharacterManager.GetCharacterContainerLock());
	mCharacterManager.AddCharacter(sessionID, characInfo);
	mSectorManager.Sector_AddCharacter(characInfo);
	SendPacketByAcceptEvent(characInfo, sendPacket);	
	ReleaseSRWLockExclusive(mCharacterManager.GetCharacterContainerLock());
}

void MmoTcpFighterServer::OnRelease(SESSIONID sessionID)
{
	CHARACTERID characterID;
	SerializationBuffer sendPacket;
	CharacterInfo* disconnectCharac;
	
	AcquireSRWLockExclusive(mCharacterManager.GetCharacterContainerLock());
	if (!mCharacterManager.FindCharacter(sessionID, &disconnectCharac))
	{
		ReleaseSRWLockExclusive(mCharacterManager.GetCharacterContainerLock());
		return;
	}
	characterID = disconnectCharac->characterID;
	// 섹터에서 제거
	mSectorManager.Sector_RemoveCharacter(disconnectCharac);
	mCharacterManager.EraseCharacter(sessionID);
	AcquireSRWLockExclusive(&disconnectCharac->srwCharacterLock);
	ReleaseSRWLockExclusive(mCharacterManager.GetCharacterContainerLock());
	ReleaseSRWLockExclusive(&disconnectCharac->srwCharacterLock);
	
	TcpFighterMessage::MakePacketDeleteCharacter(sendPacket, characterID);
	SendPacketToSectorAround(disconnectCharac, sendPacket, true);
	delete disconnectCharac;
}

bool MmoTcpFighterServer::InitTCPFighterContentThread()
{
	mIsUpdateThreadRunning = true;
	mhThreadUpdate = (HANDLE)_beginthreadex(nullptr, 0, (_beginthreadex_proc_type)UpdateThread, this, false, nullptr);
	if (mhThreadUpdate == nullptr)
	{
		_Log(dfLOG_LEVEL_SYSTEM, "Update Thread Create Error code %d", GetLastError());
		return false;
	}
	SetThreadPriority(mhThreadUpdate, THREAD_PRIORITY_IDLE);
	return true;
}

uint32_t WINAPI MmoTcpFighterServer::UpdateThread(MmoTcpFighterServer* ptrTcpFighterServer)
{
	_Log(dfLOG_LEVEL_SYSTEM, "UpdateThread 시작");
	bool isUpdate = false;
	DWORD startTime = timeGetTime();
	DWORD endTime;
	DWORD intervalTime;
	bool* ptrIsUpdateThreadRunning = &ptrTcpFighterServer->mIsUpdateThreadRunning;
	uint64_t* ptrMonitorLoopCnt = &ptrTcpFighterServer->mMonitorLoopCnt;
	uint64_t* ptrMonitorFrameCnt = &ptrTcpFighterServer->mMonitorFrameCnt;
	errno_t errSectorUpdate;

	AsyncNetworkServerEngine* ptrServerEngine = &ptrTcpFighterServer->mServerEngine;;
	MmoTcpFighterServerCharacter* ptrCharacterManager = &ptrTcpFighterServer->mCharacterManager;
	MmoTcpFighterServerSector* ptrSectorManager = &ptrTcpFighterServer->mSectorManager;

	while (*ptrIsUpdateThreadRunning)
	{
		++(*ptrMonitorLoopCnt);
		endTime = timeGetTime();
		intervalTime = endTime - startTime;
		if (intervalTime < INTERVAL_FPS(25))
		{
			Sleep(0);
			continue;
		}
		startTime = endTime - (intervalTime - INTERVAL_FPS(25));
		++(*ptrMonitorFrameCnt);

		Sleep(23000);
		CharacterInfo* ptrCharac;
		AcquireSRWLockShared(ptrCharacterManager->GetCharacterContainerLock());
		std::unordered_map<SESSIONID, PCharacterInfo> characterList 
			= ptrCharacterManager->GetCharacterContainer();
		std::unordered_map<SESSIONID, PCharacterInfo>::const_iterator iter
			= characterList.cbegin();
		for (; iter != characterList.cend();)
		{
			ptrCharac = iter->second;
			++iter;
			AcquireSRWLockExclusive(&ptrCharac->srwCharacterLock);
			if (ptrCharac->hp < 1)
			{
				ptrServerEngine->Disconnect(ptrCharac->sessionID);
			}
			else if (endTime > ptrCharac->lastRecvTime && endTime - ptrCharac->lastRecvTime > dfNETWORK_PACKET_RECV_TIMEOUT)
			{
				
				ptrServerEngine->Disconnect(ptrCharac->sessionID);
			}
			else if (ptrCharac->action != INVALID_ACTION)
			{
				int xPos = ptrCharac->xPos;
				int yPos = ptrCharac->yPos;

				switch (ptrCharac->action)
				{
				case dfPACKET_MOVE_DIR_LL:
					if (xPos - dfSPEED_PLAYER_X > dfRANGE_MOVE_LEFT)
					{
						ptrCharac->xPos = xPos - dfSPEED_PLAYER_X;
					}
					break;
				case dfPACKET_MOVE_DIR_LU:
					if (xPos - dfSPEED_PLAYER_X > dfRANGE_MOVE_LEFT && yPos - dfSPEED_PLAYER_Y > dfRANGE_MOVE_TOP)
					{
						ptrCharac->xPos = xPos - dfSPEED_PLAYER_X;
						ptrCharac->yPos = yPos - dfSPEED_PLAYER_Y;
					}
					break;
				case dfPACKET_MOVE_DIR_LD:
					if (xPos - dfSPEED_PLAYER_X > dfRANGE_MOVE_LEFT && yPos + dfSPEED_PLAYER_Y < dfRANGE_MOVE_BOTTOM)
					{
						ptrCharac->xPos = xPos - dfSPEED_PLAYER_X;
						ptrCharac->yPos = yPos + dfSPEED_PLAYER_Y;
					}
					break;
				case dfPACKET_MOVE_DIR_UU:
					if (yPos - dfSPEED_PLAYER_Y > dfRANGE_MOVE_TOP)
					{
						ptrCharac->yPos = yPos - dfSPEED_PLAYER_Y;
					}
					break;
				case dfPACKET_MOVE_DIR_RU:
					if (xPos + dfSPEED_PLAYER_X < dfRANGE_MOVE_RIGHT && yPos - dfSPEED_PLAYER_Y > dfRANGE_MOVE_TOP)
					{
						ptrCharac->xPos = xPos + dfSPEED_PLAYER_X;
						ptrCharac->yPos = yPos - dfSPEED_PLAYER_Y;
					}
					break;
				case dfPACKET_MOVE_DIR_RR:
					if (xPos + dfSPEED_PLAYER_X < dfRANGE_MOVE_RIGHT)
					{
						ptrCharac->xPos = xPos + dfSPEED_PLAYER_X;
					}
					break;
				case dfPACKET_MOVE_DIR_RD:
					if (xPos + dfSPEED_PLAYER_X < dfRANGE_MOVE_RIGHT && yPos + dfSPEED_PLAYER_Y < dfRANGE_MOVE_BOTTOM)
					{
						ptrCharac->xPos = xPos + dfSPEED_PLAYER_X;
						ptrCharac->yPos = yPos + dfSPEED_PLAYER_Y;
					}
					break;
				case dfPACKET_MOVE_DIR_DD:
					if (yPos + dfSPEED_PLAYER_Y < dfRANGE_MOVE_BOTTOM)
					{
						ptrCharac->yPos = yPos + dfSPEED_PLAYER_Y;
					}
					break;
				}

				// 섹터에 정보 업데이트
				isUpdate = ptrSectorManager->Sector_UpdateCharacter(ptrCharac, &errSectorUpdate);
				ReleaseSRWLockExclusive(&ptrCharac->srwCharacterLock);
				if (isUpdate)
				{
					ptrTcpFighterServer->SendPacketBySectorUpdate(ptrCharac);
				}
				else if (errSectorUpdate == OUT_OF_RANGE_SECTOR)
				{
					ptrServerEngine->Disconnect(ptrCharac->sessionID);
				}
				continue;
			}
			ReleaseSRWLockExclusive(&ptrCharac->srwCharacterLock);
		}
		ReleaseSRWLockShared(ptrCharacterManager->GetCharacterContainerLock());
	}

	_Log(dfLOG_LEVEL_SYSTEM, "UpdateThread 종료");
	return 0;
}

void MmoTcpFighterServer::SendPacketToSector(SectorPos target, const SerializationBuffer& sendPacket, DWORD excludeCharacterID)
{
	if (excludeCharacterID != INVALID_CHARACTER_ID)
	{
		AcquireSRWLockShared(mSectorManager.GetLockOnSector(target));
		std::unordered_map<CHARACTERID, CharacterInfo*> characterList = mSectorManager.GetCharacterListOnSector(target);
		std::unordered_map<CHARACTERID, CharacterInfo*>::const_iterator iter = characterList.cbegin();
		for (; iter != characterList.end(); ++iter)
		{
			if (excludeCharacterID != iter->first)
			{
				mServerEngine.SendPacket(iter->second->sessionID, sendPacket);
			}
		}
		ReleaseSRWLockShared(mSectorManager.GetLockOnSector(target));
		return;
	}

	AcquireSRWLockShared(mSectorManager.GetLockOnSector(target));
	std::unordered_map<CHARACTERID, CharacterInfo*> characterList = mSectorManager.GetCharacterListOnSector(target);
	std::unordered_map<CHARACTERID, CharacterInfo*>::const_iterator iter = characterList.cbegin();
	for (; iter != characterList.end(); ++iter)
	{
		mServerEngine.SendPacket(iter->second->sessionID, sendPacket);
	}
	ReleaseSRWLockShared(mSectorManager.GetLockOnSector(target));
}

void MmoTcpFighterServer::SendNPacketToSector(SectorPos target, const SerializationBuffer** sendPacket, int numberOfPacket, DWORD excludeCharacterID)
{
	if (excludeCharacterID != INVALID_CHARACTER_ID)
	{
		AcquireSRWLockShared(mSectorManager.GetLockOnSector(target));
		std::unordered_map<CHARACTERID, CharacterInfo*> characterList = mSectorManager.GetCharacterListOnSector(target);
		std::unordered_map<CHARACTERID, CharacterInfo*>::const_iterator iter = characterList.cbegin();
		for (; iter != characterList.cend(); ++iter)
		{
			if (excludeCharacterID != iter->first)
			{
				for (int i = 0; i < numberOfPacket; ++i)
				{
					mServerEngine.SendPacket(iter->second->sessionID, *(sendPacket[i]));
				}
			}
		}
		ReleaseSRWLockShared(mSectorManager.GetLockOnSector(target));
		return;
	}

	AcquireSRWLockShared(mSectorManager.GetLockOnSector(target));
	std::unordered_map<CHARACTERID, CharacterInfo*> characterList = mSectorManager.GetCharacterListOnSector(target);
	std::unordered_map<CHARACTERID, CharacterInfo*>::const_iterator iter = characterList.cbegin();
	for (; iter != characterList.cend(); ++iter)
	{
		for (int i = 0; i < numberOfPacket; ++i)
		{
			mServerEngine.SendPacket(iter->second->sessionID, *(sendPacket[i]));
		}
	}
	ReleaseSRWLockShared(mSectorManager.GetLockOnSector(target));
}

void MmoTcpFighterServer::SendPacketToSectorAround(PCharacterInfo ptrCharac, const SerializationBuffer& sendPacket, bool includeMe)
{
	SectorAround sendTargetSectors;
	mSectorManager.GetSectorAround(ptrCharac->curPos, &sendTargetSectors, false);

	int idxSectors;
	for (idxSectors = 0; idxSectors < sendTargetSectors.cnt; ++idxSectors)
	{
		SendPacketToSector(sendTargetSectors.around[idxSectors], sendPacket);
	}

	if (includeMe == false)
	{
		SendPacketToSector(ptrCharac->curPos, sendPacket, ptrCharac->characterID);
	}
	else
	{
		SendPacketToSector(ptrCharac->curPos, sendPacket);
	}
}

void MmoTcpFighterServer::SendPacketByAcceptEvent(PCharacterInfo ptrCharac, const SerializationBuffer& myCharacInfoPacket)
{
	SerializationBuffer otherCharacInfoPacket;
	SectorAround sendTargetSectors;
	CharacterInfo* ptrOtherCharac;
	mSectorManager.GetSectorAround(ptrCharac->curPos, &sendTargetSectors, true);

	// 내정보를 주변 섹터에 뿌린다.
	int idxSectors;
	for (idxSectors = 0; idxSectors < sendTargetSectors.cnt; ++idxSectors)
	{
		SendPacketToSector(sendTargetSectors.around[idxSectors], myCharacInfoPacket);
	}

	for (idxSectors = 0; idxSectors < sendTargetSectors.cnt; ++idxSectors)
	{
		AcquireSRWLockShared(mSectorManager.GetLockOnSector(sendTargetSectors.around[idxSectors]));
		std::unordered_map<CHARACTERID, CharacterInfo*>& characterList 
			= mSectorManager.GetCharacterListOnSector(sendTargetSectors.around[idxSectors]);
		std::unordered_map<CHARACTERID, CharacterInfo*>::const_iterator iter = characterList.cbegin();
		for (; iter != characterList.cend(); ++iter)
		{
			ptrOtherCharac = iter->second;
			AcquireSRWLockShared(&ptrOtherCharac->srwCharacterLock);
			TcpFighterMessage::MakePacketCreateOtherCharacter(otherCharacInfoPacket, ptrOtherCharac);
			mServerEngine.SendPacket(ptrCharac->sessionID, otherCharacInfoPacket);
			otherCharacInfoPacket.ClearBuffer();
			if (ptrOtherCharac->action != INVALID_ACTION)
			{
				TcpFighterMessage::MakePacketMoveStart(otherCharacInfoPacket, ptrOtherCharac);
				mServerEngine.SendPacket(ptrCharac->sessionID, otherCharacInfoPacket);
				otherCharacInfoPacket.ClearBuffer();
			}
			ReleaseSRWLockShared(&ptrOtherCharac->srwCharacterLock);
		}
		ReleaseSRWLockShared(mSectorManager.GetLockOnSector(sendTargetSectors.around[idxSectors]));
	}
}

void MmoTcpFighterServer::SendPacketBySectorUpdate(PCharacterInfo ptrCharac)
{
	SerializationBuffer packetBuf;
	SerializationBuffer packetBuf2;

	SectorAround removeSectors;
	SectorAround addSectors;

	int idxRemoveSectors;
	int idxAddSectors;

	CharacterInfo* addSecterCharac;

	mSectorManager.GetUpdateSectorAround(ptrCharac, &removeSectors, &addSectors);
	CHARACTERID myCharacterID = ptrCharac->characterID;
	/*
		removeSectors에 존재하는 캐릭터들에게,
		ptrCharac의 정보를 화면에서 지우라고 함
	*/
	TcpFighterMessage::MakePacketDeleteCharacter(packetBuf, myCharacterID);
	for (idxRemoveSectors = 0; idxRemoveSectors < removeSectors.cnt; ++idxRemoveSectors)
	{
		SendPacketToSector(removeSectors.around[idxRemoveSectors], packetBuf, myCharacterID);
	}

	/*
		ptrCharac에개 존재하는 캐릭터들에게,
		removeSectors의 정보를 화면에서 지우라고 함
	*/
	for (idxRemoveSectors = 0; idxRemoveSectors < removeSectors.cnt; ++idxRemoveSectors)
	{
		AcquireSRWLockShared(mSectorManager.GetLockOnSector(removeSectors.around[idxRemoveSectors]));
		std::unordered_map<CHARACTERID, PCharacterInfo>& removeCharacterMap 
			= mSectorManager.GetCharacterListOnSector(removeSectors.around[idxRemoveSectors]);
		std::unordered_map<CHARACTERID, PCharacterInfo>::const_iterator iter = removeCharacterMap.cbegin();
		for (; iter != removeCharacterMap.cend(); ++iter)
		{
			if (iter->first == myCharacterID)
			{
				continue;
			}
			packetBuf.ClearBuffer();
			TcpFighterMessage::MakePacketDeleteCharacter(packetBuf, iter->first);
			mServerEngine.SendPacket(ptrCharac->sessionID, packetBuf);
		}
		ReleaseSRWLockShared(mSectorManager.GetLockOnSector(removeSectors.around[idxRemoveSectors]));
	}

	/*
		이동 중 새로 보이는 시야에 존재하는 캐릭터들에게
		이동 하는 캐릭터의 정보를 전송;
	*/
	packetBuf.ClearBuffer();
	TcpFighterMessage::MakePacketCreateOtherCharacter(packetBuf, ptrCharac);
	TcpFighterMessage::MakePacketMoveStart(packetBuf2, ptrCharac);
	const SerializationBuffer* bufferArr[] = { &packetBuf, &packetBuf2 };
	for (idxAddSectors = 0; idxAddSectors < addSectors.cnt; ++idxAddSectors)
	{
		SendNPacketToSector(addSectors.around[idxAddSectors], bufferArr, 2, myCharacterID);
	}

	/*
		새로 보이는 시야에 있는 캐릭터 들의 정보를 내게 가져온다.
		이동 하는 캐릭터의 정보를 전송;
	*/
	for (idxAddSectors = 0; idxAddSectors < addSectors.cnt; ++idxAddSectors)
	{
		AcquireSRWLockShared(mSectorManager.GetLockOnSector(addSectors.around[idxAddSectors]));
		std::unordered_map<CHARACTERID, PCharacterInfo>& addCharacterMap 
			= mSectorManager.GetCharacterListOnSector(addSectors.around[idxAddSectors]);
		std::unordered_map<CHARACTERID, PCharacterInfo>::const_iterator iter = addCharacterMap.cbegin();
		for (; iter != addCharacterMap.cend(); ++iter)
		{
			if (iter->first == myCharacterID)
			{
				continue;
			}
			addSecterCharac = iter->second;
			packetBuf.ClearBuffer();
			TcpFighterMessage::MakePacketCreateOtherCharacter(packetBuf, addSecterCharac);
			mServerEngine.SendPacket(ptrCharac->sessionID, packetBuf);
			switch (addSecterCharac->action)
			{
			case dfPACKET_MOVE_DIR_LL:
			case dfPACKET_MOVE_DIR_LU:
			case dfPACKET_MOVE_DIR_UU:
			case dfPACKET_MOVE_DIR_RU:
			case dfPACKET_MOVE_DIR_RR:
			case dfPACKET_MOVE_DIR_RD:
			case dfPACKET_MOVE_DIR_DD:
			case dfPACKET_MOVE_DIR_LD:
				packetBuf2.ClearBuffer();
				TcpFighterMessage::MakePacketMoveStart(packetBuf2, addSecterCharac);
				mServerEngine.SendPacket(ptrCharac->sessionID, packetBuf2);
			}
		}
		ReleaseSRWLockShared(mSectorManager.GetLockOnSector(addSectors.around[idxAddSectors]));
	}
}

bool MmoTcpFighterServer::SearchCollisionOnSectors(int attackXRange, int attackYRange, const CharacterInfo* characterOnAttack, CharacterInfo** outCharacterIDOnDamage)
{
	PCharacterInfo charac;
	SectorPos posOnAttacker = characterOnAttack->curPos;
	int attackerXPos = characterOnAttack->xPos;
	int attackerYPos = characterOnAttack->yPos;
	BYTE attackerStop2Dir = characterOnAttack->stop2Dir;
	DWORD attackerCharacID = characterOnAttack->characterID;

	int tmpDistanceX;
	int tmpDistanceY;
	int minDistanceX = attackXRange;
	int minDistanceY = attackYRange;

	short tmpX;
	int cnt;
	CharacterInfo* targetCharacter = nullptr;

	if (attackerStop2Dir == dfPACKET_MOVE_DIR_LL)
	{
		for (tmpX = posOnAttacker.xPos, cnt = 2; 
			targetCharacter == nullptr && tmpX > -1 && cnt != 0; 
			--tmpX, --cnt)
		{
			AcquireSRWLockShared(mSectorManager.GetLockOnSector(tmpX, posOnAttacker.yPos));
			std::unordered_map<CHARACTERID, PCharacterInfo> characterList 
				= mSectorManager.GetCharacterListOnSector(tmpX, posOnAttacker.yPos);
			std::unordered_map<CHARACTERID, PCharacterInfo>::const_iterator iter = characterList.cbegin();
			for (; iter != characterList.cend(); ++iter)
			{
				charac = iter->second;
				if (charac->characterID == attackerCharacID)
				{
					continue;
				}
				tmpDistanceX = attackerXPos - charac->xPos;
				tmpDistanceY = abs(charac->yPos - attackerYPos);

				if ((tmpDistanceX >= 0 && tmpDistanceX <= attackXRange) && tmpDistanceY <= attackYRange)
				{
					if ((tmpDistanceX < minDistanceX)
						|| (tmpDistanceX == minDistanceX && tmpDistanceY < minDistanceY))
					{
						minDistanceX = tmpDistanceX;
						minDistanceY = tmpDistanceY;
						targetCharacter = charac;
					}
				}
			}
			ReleaseSRWLockShared(mSectorManager.GetLockOnSector(tmpX, posOnAttacker.yPos));

			if (targetCharacter != nullptr)
			{
				*outCharacterIDOnDamage = targetCharacter;
				return true;
			}
		}

		int tmpYUp = ((attackerYPos - attackYRange) / dfSECTOR_PIXEL_HEIGHT);
		int tmpYDown = ((attackerYPos + attackYRange) / dfSECTOR_PIXEL_HEIGHT);
		int searchEndY = -1;

		if (tmpYUp > -1 && tmpYUp == posOnAttacker.yPos - 1)
		{
			searchEndY = tmpYUp;
		}
		else if (tmpYDown < dfWORLD_SECTOR_HEIGHT && tmpYDown == posOnAttacker.yPos + 1)
		{
			searchEndY = tmpYDown;
		}

		if (searchEndY != -1)
		{
			for (tmpX = posOnAttacker.xPos, cnt = 2; targetCharacter == nullptr && tmpX > -1 && cnt != 0; --tmpX, --cnt)
			{
				AcquireSRWLockShared(mSectorManager.GetLockOnSector(tmpX, searchEndY));
				std::unordered_map<CHARACTERID, PCharacterInfo> characterList
					= mSectorManager.GetCharacterListOnSector(tmpX, searchEndY);
				std::unordered_map<CHARACTERID, PCharacterInfo>::const_iterator iter = characterList.cbegin();
				for (; iter != characterList.cend(); ++iter)
				{
					charac = iter->second;
					if (charac->characterID == attackerCharacID)
					{
						continue;
					}
					tmpDistanceX = attackerXPos - charac->xPos;
					tmpDistanceY = abs(charac->yPos - attackerYPos);

					if ((tmpDistanceX >= 0 && tmpDistanceX <= attackXRange) && tmpDistanceY <= attackYRange)
					{
						if ((tmpDistanceX < minDistanceX)
							|| (tmpDistanceX == minDistanceX && tmpDistanceY < minDistanceY))
						{
							minDistanceX = tmpDistanceX;
							minDistanceY = tmpDistanceY;
							targetCharacter = charac;
						}
					}
				}
				ReleaseSRWLockShared(mSectorManager.GetLockOnSector(tmpX, searchEndY));

				if (targetCharacter != nullptr)
				{
					*outCharacterIDOnDamage = targetCharacter;
					return true;
				}
			}
		}

		return false;
	}
	else if (attackerStop2Dir == dfPACKET_MOVE_DIR_RR)
	{
		for (tmpX = posOnAttacker.xPos, cnt = 2; tmpX < dfWORLD_SECTOR_WIDTH && cnt != 0; ++tmpX, --cnt)
		{
			AcquireSRWLockShared(mSectorManager.GetLockOnSector(tmpX, posOnAttacker.yPos));
			std::unordered_map<CHARACTERID, PCharacterInfo> characterList
				= mSectorManager.GetCharacterListOnSector(tmpX, posOnAttacker.yPos);
			std::unordered_map<CHARACTERID, PCharacterInfo>::const_iterator iter = characterList.cbegin();
			for (; iter != characterList.cend(); ++iter)
			{
				charac = iter->second;
				if (charac->characterID == attackerCharacID)
				{
					continue;
				}

				tmpDistanceX = charac->xPos - attackerXPos;
				tmpDistanceY = abs(charac->yPos - attackerYPos);

				if ((tmpDistanceX >= 0 && tmpDistanceX <= attackXRange) && tmpDistanceY <= attackYRange)
				{
					if ((tmpDistanceX < minDistanceX)
						|| (tmpDistanceX == minDistanceX && tmpDistanceY < minDistanceY))
					{
						minDistanceX = tmpDistanceX;
						minDistanceY = tmpDistanceY;
						targetCharacter = charac;
					}
				}
			}
			ReleaseSRWLockShared(mSectorManager.GetLockOnSector(tmpX, posOnAttacker.yPos));

			if (targetCharacter != nullptr)
			{
				*outCharacterIDOnDamage = targetCharacter;
				return true;
			}
		}

		int tmpYUp = ((attackerYPos - attackYRange) / dfSECTOR_PIXEL_HEIGHT);
		int tmpYDown = ((attackerYPos + attackYRange) / dfSECTOR_PIXEL_HEIGHT);
		int searchEndY = -1;

		if (tmpYUp > -1 && tmpYUp == posOnAttacker.yPos - 1)
		{
			searchEndY = tmpYUp;
		}
		else if (tmpYDown < dfWORLD_SECTOR_HEIGHT && tmpYDown == posOnAttacker.yPos + 1)
		{
			searchEndY = tmpYDown;
		}

		if (searchEndY != -1)
		{
			for (tmpX = posOnAttacker.xPos, cnt = 2; tmpX < dfWORLD_SECTOR_WIDTH && cnt != 0; ++tmpX, --cnt)
			{
				AcquireSRWLockShared(mSectorManager.GetLockOnSector(tmpX, searchEndY));
				std::unordered_map<CHARACTERID, PCharacterInfo> characterList
					= mSectorManager.GetCharacterListOnSector(tmpX, searchEndY);
				std::unordered_map<CHARACTERID, PCharacterInfo>::const_iterator iter = characterList.cbegin();
				for (; iter != characterList.cend(); ++iter)
				{
					charac = iter->second;
					if (charac->characterID == attackerCharacID)
					{
						continue;
					}
					tmpDistanceX = charac->xPos - attackerXPos;
					tmpDistanceY = abs(charac->yPos - attackerYPos);

					if ((tmpDistanceX >= 0 && tmpDistanceX <= attackXRange) && tmpDistanceY <= attackYRange)
					{
						if ((tmpDistanceX < minDistanceX)
							|| (tmpDistanceX == minDistanceX && tmpDistanceY < minDistanceY))
						{
							minDistanceX = tmpDistanceX;
							minDistanceY = tmpDistanceY;
							targetCharacter = charac;
						}
					}
				}
				ReleaseSRWLockShared(mSectorManager.GetLockOnSector(tmpX, searchEndY));

				if (targetCharacter != nullptr)
				{
					*outCharacterIDOnDamage = targetCharacter;
					return true;
				}
			}
		}

		return false;
	}

	return false;
}

bool MmoTcpFighterServer::DispatchPacketToContents(SESSIONID sessionID, SerializationBuffer& refRecvPacket)
{
	WORD	byType;
	refRecvPacket >> byType;
	switch (byType)
	{
	case dfPACKET_CS_MOVE_START:
		return ProcessPacketMoveStart(sessionID, refRecvPacket);
	case dfPACKET_CS_MOVE_STOP:
		return ProcessPacketMoveStop(sessionID, refRecvPacket);
	case dfPACKET_CS_ATTACK1:
		return ProcessPacketAttack1(sessionID, refRecvPacket);
	case dfPACKET_CS_ATTACK2:
		return ProcessPacketAttack2(sessionID, refRecvPacket);
	case dfPACKET_CS_ATTACK3:
		return ProcessPacketAttack3(sessionID, refRecvPacket);
	case dfPACKET_CS_ECHO:
		return ProcessPacketEcho(sessionID, refRecvPacket);
	}
	return false;
}

bool MmoTcpFighterServer::ProcessPacketMoveStart(SESSIONID sessionID, SerializationBuffer& refRecvPacket)
{
	SerializationBuffer packetBuf;
	BYTE move8Dir;
	WORD clientXpos;
	WORD clientYpos;
	PCharacterInfo ptrCharacter;

	refRecvPacket >> move8Dir >> clientXpos >> clientYpos;
	AcquireSRWLockShared(mCharacterManager.GetCharacterContainerLock());
	mCharacterManager.FindCharacter(sessionID, &ptrCharacter);
	AcquireSRWLockExclusive(&ptrCharacter->srwCharacterLock);
	ReleaseSRWLockShared(mCharacterManager.GetCharacterContainerLock());
	_Log(dfLOG_LEVEL_DEBUG, "CHARACTER_ID[%d] PACKET MOVE_START [DIR: %s/X: %d/Y: %d]"
		, ptrCharacter->characterID, dirTable[move8Dir], clientXpos, clientYpos);

	if (abs(ptrCharacter->xPos - clientXpos) > dfERROR_RANGE
		|| abs(ptrCharacter->yPos - clientYpos) > dfERROR_RANGE)
	{
		clientXpos = ptrCharacter->xPos;
		clientYpos = ptrCharacter->yPos;

		TcpFighterMessage::MakePacketSyncXYPos(packetBuf, ptrCharacter->characterID, clientXpos, clientYpos);
		SendPacketToSectorAround(ptrCharacter, packetBuf, true);
		packetBuf.ClearBuffer();
	}
	else
	{
		ptrCharacter->xPos = clientXpos;
		ptrCharacter->yPos = clientYpos;
	}

	ptrCharacter->action = move8Dir;
	ptrCharacter->move8Dir = move8Dir;

	switch (move8Dir)
	{
	case dfPACKET_MOVE_DIR_LL:
	case dfPACKET_MOVE_DIR_LU:
		ptrCharacter->stop2Dir = dfPACKET_MOVE_DIR_LL;
		break;
	case dfPACKET_MOVE_DIR_RU:
	case dfPACKET_MOVE_DIR_RR:
	case dfPACKET_MOVE_DIR_RD:
		ptrCharacter->stop2Dir = dfPACKET_MOVE_DIR_RR;
		break;
	case dfPACKET_MOVE_DIR_LD:
		ptrCharacter->stop2Dir = dfPACKET_MOVE_DIR_LL;
		break;
	}

	TcpFighterMessage::MakePacketMoveStart(packetBuf, ptrCharacter->characterID, move8Dir, clientXpos, clientYpos);
	SendPacketToSectorAround(ptrCharacter, packetBuf, false);
	ReleaseSRWLockExclusive(&ptrCharacter->srwCharacterLock);

	return true;
}

bool MmoTcpFighterServer::ProcessPacketMoveStop(SESSIONID sessionID, SerializationBuffer& refRecvPacket)
{
	SerializationBuffer packetBuf;
	BYTE stop2Dir;
	WORD clientXpos;
	WORD clientYpos;
	PCharacterInfo ptrCharacter;

	refRecvPacket >> stop2Dir >> clientXpos >> clientYpos;
	AcquireSRWLockShared(mCharacterManager.GetCharacterContainerLock());
	mCharacterManager.FindCharacter(sessionID, &ptrCharacter);
	AcquireSRWLockExclusive(&ptrCharacter->srwCharacterLock);
	ReleaseSRWLockShared(mCharacterManager.GetCharacterContainerLock());
	_Log(dfLOG_LEVEL_DEBUG, "CHARACTER_ID[%d] PACKET MOVE_STOP [DIR: %s/X: %d/Y: %d]"
		, ptrCharacter->characterID, dirTable[stop2Dir], clientXpos, clientYpos);

	if (abs(ptrCharacter->xPos - clientXpos) > dfERROR_RANGE
		|| abs(ptrCharacter->yPos - clientYpos) > dfERROR_RANGE)
	{
		clientXpos = ptrCharacter->xPos;
		clientYpos = ptrCharacter->yPos;

		TcpFighterMessage::MakePacketSyncXYPos(packetBuf, ptrCharacter->characterID, clientXpos, clientYpos);
		SendPacketToSectorAround(ptrCharacter, packetBuf, true);
		packetBuf.ClearBuffer();
	}
	else
	{
		ptrCharacter->xPos = clientXpos;
		ptrCharacter->yPos = clientYpos;
	}

	ptrCharacter->action = INVALID_ACTION;
	ptrCharacter->stop2Dir = stop2Dir;

	TcpFighterMessage::MakePacketMoveStop(packetBuf, ptrCharacter->characterID, stop2Dir, clientXpos, clientYpos);
	SendPacketToSectorAround(ptrCharacter, packetBuf, false);
	ReleaseSRWLockExclusive(&ptrCharacter->srwCharacterLock);

	return true;
}

bool MmoTcpFighterServer::ProcessPacketAttack1(SESSIONID sessionID, SerializationBuffer& refRecvPacket)
{
	SerializationBuffer packetBuf;
	BYTE stop2Dir;
	WORD clientXpos;
	WORD clientYpos;
	PCharacterInfo ptrCharacter;

	refRecvPacket >> stop2Dir >> clientXpos >> clientYpos;
	AcquireSRWLockShared(mCharacterManager.GetCharacterContainerLock());
	mCharacterManager.FindCharacter(sessionID, &ptrCharacter);
	AcquireSRWLockExclusive(&ptrCharacter->srwCharacterLock);
	ReleaseSRWLockShared(mCharacterManager.GetCharacterContainerLock());
	if (abs(ptrCharacter->xPos - clientXpos) > dfERROR_RANGE
		|| abs(ptrCharacter->yPos - clientYpos) > dfERROR_RANGE)
	{
		clientXpos = ptrCharacter->xPos;
		clientYpos = ptrCharacter->yPos;

		TcpFighterMessage::MakePacketSyncXYPos(packetBuf, ptrCharacter->characterID, clientXpos, clientYpos);
		SendPacketToSectorAround(ptrCharacter, packetBuf, true);
		packetBuf.ClearBuffer();
	}
	else
	{
		ptrCharacter->xPos = clientXpos;
		ptrCharacter->yPos = clientYpos;
	}

	ptrCharacter->stop2Dir = stop2Dir;

	TcpFighterMessage::MakePacketAttack1(packetBuf, ptrCharacter->characterID, stop2Dir, clientXpos, clientYpos);
	SendPacketToSectorAround(ptrCharacter, packetBuf, false);

	CharacterInfo* damagedCharacter;
	if (SearchCollisionOnSectors(dfATTACK1_RANGE_X, dfATTACK1_RANGE_Y, ptrCharacter, &damagedCharacter))
	{
		packetBuf.ClearBuffer();
		TcpFighterMessage::MakePacketDamage(packetBuf, ptrCharacter->characterID, damagedCharacter->characterID, damagedCharacter->hp -= dfATTACK1_DAMAGE);
		SendPacketToSectorAround(damagedCharacter, packetBuf, true);
	}
	ReleaseSRWLockExclusive(&ptrCharacter->srwCharacterLock);

	return true;
}

bool MmoTcpFighterServer::ProcessPacketAttack2(SESSIONID sessionID, SerializationBuffer& refRecvPacket)
{
	SerializationBuffer packetBuf;
	BYTE stop2Dir;
	WORD clientXpos;
	WORD clientYpos;
	PCharacterInfo ptrCharacter;

	refRecvPacket >> stop2Dir >> clientXpos >> clientYpos;
	AcquireSRWLockShared(mCharacterManager.GetCharacterContainerLock());
	mCharacterManager.FindCharacter(sessionID, &ptrCharacter);
	AcquireSRWLockExclusive(&ptrCharacter->srwCharacterLock);
	ReleaseSRWLockShared(mCharacterManager.GetCharacterContainerLock());
	if (abs(ptrCharacter->xPos - clientXpos) > dfERROR_RANGE
		|| abs(ptrCharacter->yPos - clientYpos) > dfERROR_RANGE)
	{
		clientXpos = ptrCharacter->xPos;
		clientYpos = ptrCharacter->yPos;

		TcpFighterMessage::MakePacketSyncXYPos(packetBuf, ptrCharacter->characterID, clientXpos, clientYpos);
		SendPacketToSectorAround(ptrCharacter, packetBuf, true);
		packetBuf.ClearBuffer();
	}
	else
	{
		ptrCharacter->xPos = clientXpos;
		ptrCharacter->yPos = clientYpos;
	}

	ptrCharacter->stop2Dir = stop2Dir;

	TcpFighterMessage::MakePacketAttack2(packetBuf, ptrCharacter->characterID, stop2Dir, clientXpos, clientYpos);
	SendPacketToSectorAround(ptrCharacter, packetBuf, false);

	CharacterInfo* damagedCharacter;
	if (SearchCollisionOnSectors(dfATTACK2_RANGE_X, dfATTACK2_RANGE_Y, ptrCharacter, &damagedCharacter))
	{
		packetBuf.ClearBuffer();
		TcpFighterMessage::MakePacketDamage(packetBuf, ptrCharacter->characterID, damagedCharacter->characterID, damagedCharacter->hp -= dfATTACK2_DAMAGE);
		SendPacketToSectorAround(damagedCharacter, packetBuf, true);
	}
	ReleaseSRWLockExclusive(&ptrCharacter->srwCharacterLock);

	return true;
}

bool MmoTcpFighterServer::ProcessPacketAttack3(SESSIONID sessionID, SerializationBuffer& refRecvPacket)
{
	SerializationBuffer packetBuf;
	BYTE stop2Dir;
	WORD clientXpos;
	WORD clientYpos;
	PCharacterInfo ptrCharacter;

	refRecvPacket >> stop2Dir >> clientXpos >> clientYpos;
	AcquireSRWLockShared(mCharacterManager.GetCharacterContainerLock());
	mCharacterManager.FindCharacter(sessionID, &ptrCharacter);
	AcquireSRWLockExclusive(&ptrCharacter->srwCharacterLock);
	ReleaseSRWLockShared(mCharacterManager.GetCharacterContainerLock());
	if (abs(ptrCharacter->xPos - clientXpos) > dfERROR_RANGE
		|| abs(ptrCharacter->yPos - clientYpos) > dfERROR_RANGE)
	{
		clientXpos = ptrCharacter->xPos;
		clientYpos = ptrCharacter->yPos;

		TcpFighterMessage::MakePacketSyncXYPos(packetBuf, ptrCharacter->characterID, clientXpos, clientYpos);
		SendPacketToSectorAround(ptrCharacter, packetBuf, true);
		packetBuf.ClearBuffer();
	}
	else
	{
		ptrCharacter->xPos = clientXpos;
		ptrCharacter->yPos = clientYpos;
	}

	ptrCharacter->stop2Dir = stop2Dir;

	TcpFighterMessage::MakePacketAttack3(packetBuf, ptrCharacter->characterID, stop2Dir, clientXpos, clientYpos);
	SendPacketToSectorAround(ptrCharacter, packetBuf, false);

	CharacterInfo* damagedCharacter;
	if (SearchCollisionOnSectors(dfATTACK3_RANGE_X, dfATTACK3_RANGE_Y, ptrCharacter, &damagedCharacter))
	{
		Sleep(60000);
		int aaa = 3000;
		while (aaa--)
		{
			YieldProcessor();
		}
		Sleep(60000);
		packetBuf.ClearBuffer();
		TcpFighterMessage::MakePacketDamage(packetBuf, ptrCharacter->characterID, damagedCharacter->characterID, damagedCharacter->hp -= dfATTACK3_DAMAGE);
		SendPacketToSectorAround(damagedCharacter, packetBuf, true);
	}
	ReleaseSRWLockExclusive(&ptrCharacter->srwCharacterLock);

	return true;
}

bool MmoTcpFighterServer::ProcessPacketEcho(SESSIONID sessionID, SerializationBuffer& refRecvPacket)
{
	SerializationBuffer packetBuf;
	DWORD time;
	refRecvPacket >> time;
	TcpFighterMessage::MakePacketEcho(packetBuf, time);
	mServerEngine.SendPacket(sessionID, packetBuf);
	return true;
}
