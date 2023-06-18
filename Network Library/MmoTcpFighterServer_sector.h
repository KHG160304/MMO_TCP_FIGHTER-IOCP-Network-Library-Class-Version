#pragma once
#ifndef __MMO_TCP_FIGHTER_SERVER_SECTOR_H__
#define __MMO_TCP_FIGHTER_SERVER_SECTOR_H__

#include "MmoTcpFighterServer_character.h"
#include "MmoTcpFighterContentProcessProtocol.h"
#include "MmoTcpFighterContentSettings.h"

class SerializationBuffer;

struct SectorAround
{
	int cnt;
	SectorPos around[9];
};

class MmoTcpFighterServerSector
{
public:
	static SectorPos ConvertWorldPosToSectorPos(short worldXPos, short worldYPos);

	size_t GetSectorCharacterCnt();
	std::unordered_map<CHARACTERID, CharacterInfo*>& GetCharacterListOnSector(SectorPos targetSector);
	std::unordered_map<CHARACTERID, CharacterInfo*>& GetCharacterListOnSector(short sectorXpos, short sectorYpos);
	SRWLOCK* GetLockOnSector(SectorPos targetSector);
	SRWLOCK* GetLockOnSector(short sectorXpos, short sectorYpos);

	void InitCharactorSectorInfo(PCharacterInfo charac);
	void Sector_AddCharacter(PCharacterInfo charac);
	void Sector_RemoveCharacter(PCharacterInfo charac);
	bool Sector_UpdateCharacter(PCharacterInfo charac, errno_t* err);
	void GetSectorAround(SectorPos sourceSector, SectorAround* pSectorAround, bool includeSourceSector);
	void GetUpdateSectorAround(PCharacterInfo ptrCharacSectorInfo, SectorAround* ptrRemoveSectors, SectorAround* ptrAddSectors);
private:
	std::unordered_map<CHARACTERID, CharacterInfo*> mSectorList[dfWORLD_SECTOR_HEIGHT + 1][dfWORLD_SECTOR_WIDTH + 1];
	SRWLOCK mSectorLockList[dfWORLD_SECTOR_HEIGHT + 1][dfWORLD_SECTOR_WIDTH + 1] = { SRWLOCK_INIT, };
};
#endif // !__MMO_TCP_FIGHTER_SERVER_SECTOR__

