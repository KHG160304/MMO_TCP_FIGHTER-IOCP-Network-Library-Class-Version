#pragma once
#ifndef __MMO_TCP_FIGHTER_SERVER_H__
#define __MMO_TCP_FIGHTER_SERVER_H__
#include "AsyncNetworkServerEngine.h"
#include "MmoTcpFighterServer_character.h"
#include "MmoTcpFighterServer_sector.h"

class MmoTcpFighterServer : protected ISessionEventHandler
{
public:
	MmoTcpFighterServer(uint16_t port);
	~MmoTcpFighterServer();

	void Start();

	void Monitoring();
protected:
	void OnRecv(SESSIONID sessionID, SerializationBuffer& refRecvPacket);
	void OnAccept(SESSIONID sessionID);
	void OnRelease(SESSIONID sessionID);
private:
	bool InitTCPFighterContentThread();

	void SendPacketToSector(SectorPos target, const SerializationBuffer& sendPacket, DWORD excludeCharacterID = INVALID_CHARACTER_ID);
	void SendNPacketToSector(SectorPos target, const SerializationBuffer** sendPacket, int numberOfPacket, DWORD excludeCharacterID);
	void SendPacketToSectorAround(PCharacterInfo ptrCharac, const SerializationBuffer& sendPacket, bool includeMe);
	void SendPacketByAcceptEvent(PCharacterInfo ptrCharac, const SerializationBuffer& myCharacInfoPacket);
	void SendPacketBySectorUpdate(PCharacterInfo ptrCharac);

	bool DispatchPacketToContents(SESSIONID sessionID, SerializationBuffer& refRecvPacket);
	bool ProcessPacketMoveStart(SESSIONID sessionID, SerializationBuffer& refRecvPacket);
	bool ProcessPacketMoveStop(SESSIONID sessionID, SerializationBuffer& refRecvPacket);
	bool ProcessPacketAttack1(SESSIONID sessionID, SerializationBuffer& refRecvPacket);
	bool ProcessPacketAttack2(SESSIONID sessionID, SerializationBuffer& refRecvPacket);
	bool ProcessPacketAttack3(SESSIONID sessionID, SerializationBuffer& refRecvPacket);
	bool ProcessPacketEcho(SESSIONID sessionID, SerializationBuffer& refRecvPacket);

	bool SearchCollisionOnSectors(int attackXRange, int attackYRange, const CharacterInfo* characterOnAttack, SESSIONID* outSessionIdForCharacterOnDamage);

	static uint32_t WINAPI UpdateThread(MmoTcpFighterServer* ptrServerEngine);
private:
	AsyncNetworkServerEngine mServerEngine;
	MmoTcpFighterServerCharacter mCharacterManager;
	MmoTcpFighterServerSector mSectorManager;

	HANDLE mhThreadUpdate;

	bool mIsUpdateThreadRunning;
	uint64_t mMonitorLoopCnt;
	uint64_t mMonitorFrameCnt;
};

#endif // !__MMO_TCP_FIGHTER_SERVER__
