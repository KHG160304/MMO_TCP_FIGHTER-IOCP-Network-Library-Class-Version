#pragma once
#ifndef __MMO_TCP_FIGHTER_SERVER_CHARACTER_H__
#define __MMO_TCP_FIGHTER_SERVER_CHARACTER_H__
#include <windows.h>
#include <unordered_map>

#define OUT_OF_RANGE_SECTOR -1

typedef	unsigned long long SESSIONID;
typedef	DWORD CHARACTERID;

struct SectorPos
{
	short xPos;
	short yPos;
};

typedef struct CharacterInfo
{
	SESSIONID sessionID;
	CHARACTERID	characterID;
	DWORD	dwActionTick;
	WORD	xPos;
	WORD	yPos;
	WORD	actionXpos;
	WORD	actionYpos;
	BYTE	stop2Dir;
	BYTE	move8Dir;
	BYTE	action;
	char	hp;

	DWORD lastRecvTime;
	SRWLOCK srwCharacterLock;

	SectorPos curPos;
	SectorPos oldPos;
} CharacterInfo, * PCharacterInfo;

class MmoTcpFighterServerCharacter
{
public:
	MmoTcpFighterServerCharacter();
	void ReleaseAllCharacterInfo();

	void AddCharacter(SESSIONID sessionID, PCharacterInfo ptrCharacterInfo);
	void EraseCharacter(SESSIONID sessionID);
	bool FindCharacter(SESSIONID sessionID, PCharacterInfo* outCharacterInfo);
	size_t GetCharacterCnt();
	void InitCharacterInfo(SESSIONID sessionID, CharacterInfo* ptrCharac);
	std::unordered_map<SESSIONID, PCharacterInfo>& GetCharacterContainer();
	SRWLOCK* GetCharacterContainerLock();
private:
	CHARACTERID mCharacterIdCounter;
	std::unordered_map<SESSIONID, PCharacterInfo> mCharacterList;
	SRWLOCK mCharacterListSrwLock = SRWLOCK_INIT;
};


#endif // !__MMO_TCP_FIGHTER_SERVER_CHARACTER__
