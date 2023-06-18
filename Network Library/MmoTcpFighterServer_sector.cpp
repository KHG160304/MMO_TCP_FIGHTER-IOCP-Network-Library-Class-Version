#include "MmoTcpFighterServer_sector.h"


SectorPos MmoTcpFighterServerSector::ConvertWorldPosToSectorPos(short worldXPos, short worldYPos)
{
	return {
		(short)(worldXPos / dfSECTOR_PIXEL_WIDTH),
		(short)(worldYPos / dfSECTOR_PIXEL_HEIGHT)
	};
}

size_t MmoTcpFighterServerSector::GetSectorCharacterCnt()
{
	size_t cnt = 0;
	for (int y = 0; y < dfWORLD_SECTOR_HEIGHT; ++y)
	{
		for (int x = 0; x < dfWORLD_SECTOR_WIDTH; ++x)
		{
			AcquireSRWLockShared(&mSectorLockList[y][x]);
			cnt += mSectorList[y][x].size();
			ReleaseSRWLockShared(&mSectorLockList[y][x]);
		}
	}
	return cnt;
}

std::unordered_map<CHARACTERID, CharacterInfo*>& MmoTcpFighterServerSector::GetCharacterListOnSector(SectorPos targetSector)
{
	return mSectorList[targetSector.yPos][targetSector.xPos];
}

std::unordered_map<CHARACTERID, CharacterInfo*>& MmoTcpFighterServerSector::GetCharacterListOnSector(short sectorXpos, short sectorYpos)
{
	return mSectorList[sectorYpos][sectorXpos];
}

SRWLOCK* MmoTcpFighterServerSector::GetLockOnSector(SectorPos targetSector)
{
	return &mSectorLockList[targetSector.yPos][targetSector.xPos];
}

SRWLOCK* MmoTcpFighterServerSector::GetLockOnSector(short sectorXpos, short sectorYpos)
{
	return &mSectorLockList[sectorYpos][sectorXpos];
}

void MmoTcpFighterServerSector::InitCharactorSectorInfo(PCharacterInfo charac)
{
	SectorPos characSectorPos = ConvertWorldPosToSectorPos(charac->xPos, charac->yPos);
	charac->curPos = characSectorPos;
	charac->oldPos = characSectorPos;
}

void MmoTcpFighterServerSector::Sector_AddCharacter(PCharacterInfo charac)
{
	SectorPos curPos = charac->curPos;
	AcquireSRWLockExclusive(&mSectorLockList[curPos.yPos][curPos.xPos]);
	mSectorList[curPos.yPos][curPos.xPos].insert({ charac->characterID, charac });
	ReleaseSRWLockExclusive(&mSectorLockList[curPos.yPos][curPos.xPos]);
}

void MmoTcpFighterServerSector::Sector_RemoveCharacter(PCharacterInfo charac)
{
	SectorPos curPos = charac->curPos;
	AcquireSRWLockExclusive(&mSectorLockList[curPos.yPos][curPos.xPos]);
	mSectorList[curPos.yPos][curPos.xPos].erase(charac->characterID);
	ReleaseSRWLockExclusive(&mSectorLockList[curPos.yPos][curPos.xPos]);
}

bool MmoTcpFighterServerSector::Sector_UpdateCharacter(PCharacterInfo charac, errno_t* err)
{
	SectorPos curPos = ConvertWorldPosToSectorPos(charac->xPos, charac->yPos);
	if (curPos.yPos >= dfWORLD_SECTOR_HEIGHT || curPos.xPos >= dfWORLD_SECTOR_WIDTH)
	{
		*err = OUT_OF_RANGE_SECTOR;
		return false;
	}

	*err = 0;
	if (*((DWORD*)&curPos) == *((DWORD*)&(charac->curPos)))
	{
		return false;
	}

	charac->oldPos = charac->curPos;
	Sector_RemoveCharacter(charac);

	charac->curPos = curPos;
	Sector_AddCharacter(charac);

	return true;
}

void MmoTcpFighterServerSector::GetSectorAround(SectorPos sourceSector, SectorAround* pSectorAround, bool includeSourceSector)
{
	short endYPos = sourceSector.yPos + 1;
	short endXPos = sourceSector.xPos + 1;
	short y, x;

	if (includeSourceSector == false)
	{
		int idx = 0;
		for (y = endYPos - 2; y <= endYPos; ++y)
		{
			if (y < 0 || y >= dfWORLD_SECTOR_HEIGHT)
			{
				continue;
			}
			for (x = endXPos - 2; x <= endXPos; ++x)
			{
				if (x < 0 || x >= dfWORLD_SECTOR_WIDTH)
				{
					continue;
				}

				if (sourceSector.xPos == x && sourceSector.yPos == y)
				{
					continue;
				}

				pSectorAround->around[idx++] = { x, y };
			}
		}
		pSectorAround->cnt = idx;

		return;
	}

	int idx = 0;
	for (y = endYPos - 2; y <= endYPos; ++y)
	{
		if (y < 0 || y >= dfWORLD_SECTOR_HEIGHT)
		{
			continue;
		}
		for (x = endXPos - 2; x <= endXPos; ++x)
		{
			if (x < 0 || x >= dfWORLD_SECTOR_WIDTH)
			{
				continue;
			}

			pSectorAround->around[idx++] = { x, y };
		}
	}

	pSectorAround->cnt = idx;
}

void MmoTcpFighterServerSector::GetUpdateSectorAround(PCharacterInfo ptrCharac, SectorAround* ptrRemoveSectors, SectorAround* ptrAddSectors)
{
	int idxOld;
	int idxCur;
	bool isFind;
	int removeSectorsCnt = 0;
	int addSectorsCnt = 0;

	SectorAround oldSectors;
	SectorAround curSectors;
	GetSectorAround(ptrCharac->oldPos, &oldSectors, true);
	GetSectorAround(ptrCharac->curPos, &curSectors, true);

	for (idxOld = 0; idxOld < oldSectors.cnt; ++idxOld)
	{
		isFind = false;
		for (idxCur = 0; idxCur < curSectors.cnt; ++idxCur)
		{
			if (*((DWORD*)(oldSectors.around + idxOld)) == *((DWORD*)(curSectors.around + idxCur)))
			{
				isFind = true;
				break;
			}
		}

		if (isFind == false)
		{
			ptrRemoveSectors->around[removeSectorsCnt++] = oldSectors.around[idxOld];
		}
	}
	ptrRemoveSectors->cnt = removeSectorsCnt;

	for (idxCur = 0; idxCur < curSectors.cnt; ++idxCur)
	{
		isFind = false;
		for (idxOld = 0; idxOld < oldSectors.cnt; ++idxOld)
		{
			if (*((DWORD*)(curSectors.around + idxCur)) == *((DWORD*)(oldSectors.around + idxOld)))
			{
				isFind = true;
				break;
			}
		}

		if (isFind == false)
		{
			ptrAddSectors->around[addSectorsCnt++] = curSectors.around[idxCur];
		}
	}
	ptrAddSectors->cnt = addSectorsCnt;
}