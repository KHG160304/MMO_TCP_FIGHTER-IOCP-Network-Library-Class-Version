#include "MmoTcpFighterServer_character.h"
#include "MmoTcpFighterContentProcessProtocol.h"
#include "MmoTcpFighterContentSettings.h"

MmoTcpFighterServerCharacter::MmoTcpFighterServerCharacter()
	: mCharacterIdCounter(0)
	, mCharacterListSrwLock(SRWLOCK_INIT)
{
}

void MmoTcpFighterServerCharacter::ReleaseAllCharacterInfo()
{
	std::unordered_map<SESSIONID, PCharacterInfo>::iterator iter = mCharacterList.begin();
	while (iter != mCharacterList.end())
	{
		delete iter->second;
		iter = mCharacterList.erase(iter);
	}
}

void MmoTcpFighterServerCharacter::AddCharacter(SESSIONID sessionID, PCharacterInfo ptrCharacterInfo)
{
	mCharacterList.insert({ sessionID, ptrCharacterInfo });
}

void MmoTcpFighterServerCharacter::EraseCharacter(SESSIONID sessionID)
{
	mCharacterList.erase(sessionID);
}

bool MmoTcpFighterServerCharacter::FindCharacter(SESSIONID sessionID, PCharacterInfo* outCharacterInfo)
{
	std::unordered_map<SESSIONID, PCharacterInfo>::iterator iter = mCharacterList.find(sessionID);
	if (iter != mCharacterList.end())
	{
		*outCharacterInfo = iter->second;
		return true;
	}
	return false;
}

size_t MmoTcpFighterServerCharacter::GetCharacterCnt()
{
	AcquireSRWLockShared(&mCharacterListSrwLock);
	size_t size = mCharacterList.size();
	ReleaseSRWLockShared(&mCharacterListSrwLock);
	return size;
}

void MmoTcpFighterServerCharacter::InitCharacterInfo(SESSIONID sessionID, CharacterInfo* ptrCharacInfo)
{
	//CharacterInfo* characInfo = new CharacterInfo();
	ptrCharacInfo->sessionID = sessionID;
	ptrCharacInfo->characterID = mCharacterIdCounter;
	ptrCharacInfo->hp = defCHARACTER_DEFAULT_HP;
	ptrCharacInfo->action = INVALID_ACTION;
	ptrCharacInfo->move8Dir = dfPACKET_MOVE_DIR_LL;
	ptrCharacInfo->stop2Dir = dfPACKET_MOVE_DIR_LL;
	ptrCharacInfo->xPos = rand() % dfRANGE_MOVE_RIGHT;
	ptrCharacInfo->yPos = rand() % dfRANGE_MOVE_BOTTOM;

	ptrCharacInfo->dwActionTick = 0;

	ptrCharacInfo->xPos -= (ptrCharacInfo->xPos % dfSECTOR_PIXEL_WIDTH);
	ptrCharacInfo->yPos -= (ptrCharacInfo->yPos % dfSECTOR_PIXEL_HEIGHT);

	//SectorPos sectorPos = ConvertWorldPosToSectorPos(characInfo->xPos, characInfo->yPos);
	//characInfo->curPos = sectorPos;
	//characInfo->oldPos = sectorPos;
	ptrCharacInfo->lastRecvTime = timeGetTime();
	ptrCharacInfo->srwCharacterLock = SRWLOCK_INIT;

	mCharacterIdCounter = (mCharacterIdCounter + 1) % INVALID_CHARACTER_ID;
}

std::unordered_map<SESSIONID, PCharacterInfo>& MmoTcpFighterServerCharacter::GetCharacterContainer()
{
	return mCharacterList;
}

SRWLOCK* MmoTcpFighterServerCharacter::GetCharacterContainerLock()
{
	return &mCharacterListSrwLock;
}
