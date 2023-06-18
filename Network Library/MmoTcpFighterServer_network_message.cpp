#include "MmoTcpFighterServer_network_message.h"
#include "MmoTcpFighterNetworkMessageProtocol.h"
#include "SerializationBuffer.h"
#include "MmoTcpFighterServer_character.h"

void MmoTcpFigheterServerNetworkMessage::MakePacketEcho(SerializationBuffer& packetBuf, DWORD time)
{
	packetBuf << (WORD)dfPACKET_SC_ECHO << time;
}

void MmoTcpFigheterServerNetworkMessage::MakePacketSyncXYPos(SerializationBuffer& packetBuf, DWORD id, WORD xPos, WORD yPos)
{
	packetBuf << (WORD)dfPACKET_SC_SYNC << id << xPos << yPos;
}

void MmoTcpFigheterServerNetworkMessage::MakePacketCreateMyCharacter(SerializationBuffer& packetBuf, DWORD id, BYTE stop2Dir, WORD xPos, WORD yPos, BYTE hp)
{
	packetBuf << (WORD)dfPACKET_SC_CREATE_MY_CHARACTER << id << stop2Dir << xPos << yPos << hp;
}

void MmoTcpFigheterServerNetworkMessage::MakePacketCreateMyCharacter(SerializationBuffer& packetBuf, CharacterInfo* charac)
{
	packetBuf << (WORD)dfPACKET_SC_CREATE_MY_CHARACTER << charac->characterID << charac->stop2Dir << charac->xPos << charac->yPos << charac->hp;
}


void MmoTcpFigheterServerNetworkMessage::MakePacketCreateOtherCharacter(SerializationBuffer& packetBuf, DWORD id, BYTE stop2Dir, WORD xPos, WORD yPos, BYTE hp)
{
	packetBuf << (WORD)dfPACKET_SC_CREATE_OTHER_CHARACTER << id << stop2Dir << xPos << yPos << hp;
}

void MmoTcpFigheterServerNetworkMessage::MakePacketCreateOtherCharacter(SerializationBuffer& packetBuf, CharacterInfo* charac)
{
	packetBuf << (WORD)dfPACKET_SC_CREATE_OTHER_CHARACTER << charac->characterID << charac->stop2Dir << charac->xPos << charac->yPos << charac->hp;
}

void MmoTcpFigheterServerNetworkMessage::MakePacketDeleteCharacter(SerializationBuffer& packetBuf, DWORD id)
{
	packetBuf << (WORD)dfPACKET_SC_DELETE_CHARACTER << id;
}

void MmoTcpFigheterServerNetworkMessage::MakePacketMoveStart(SerializationBuffer& packetBuf, DWORD id, BYTE move8Dir, WORD xPos, WORD yPos)
{
	packetBuf << (WORD)dfPACKET_SC_MOVE_START << id << move8Dir << xPos << yPos;
}

void MmoTcpFigheterServerNetworkMessage::MakePacketMoveStart(SerializationBuffer& packetBuf, CharacterInfo* charac)
{
	packetBuf << (WORD)dfPACKET_SC_MOVE_START << charac->characterID << charac->move8Dir << charac->xPos << charac->yPos;
}

void MmoTcpFigheterServerNetworkMessage::MakePacketMoveStop(SerializationBuffer& packetBuf, DWORD id, BYTE dir, WORD xPos, WORD yPos)
{
	packetBuf << (WORD)dfPACKET_SC_MOVE_STOP << id << dir << xPos << yPos;
}

void MmoTcpFigheterServerNetworkMessage::MakePacketAttack1(SerializationBuffer& packetBuf, DWORD id, BYTE dir, WORD xPos, WORD yPos)
{
	packetBuf << (WORD)dfPACKET_SC_ATTACK1 << id << dir << xPos << yPos;
}

void MmoTcpFigheterServerNetworkMessage::MakePacketAttack2(SerializationBuffer& packetBuf, DWORD id, BYTE dir, WORD xPos, WORD yPos)
{
	packetBuf << (WORD)dfPACKET_SC_ATTACK2 << id << dir << xPos << yPos;
}

void MmoTcpFigheterServerNetworkMessage::MakePacketAttack3(SerializationBuffer& packetBuf, DWORD id, BYTE dir, WORD xPos, WORD yPos)
{
	packetBuf << (WORD)dfPACKET_SC_ATTACK3 << id << dir << xPos << yPos;
}

void MmoTcpFigheterServerNetworkMessage::MakePacketDamage(SerializationBuffer& packetBuf, DWORD attackerID, DWORD damagedID, BYTE damageHP)
{
	packetBuf << (WORD)dfPACKET_SC_DAMAGE << attackerID << damagedID << damageHP;
}

void MmoTcpFigheterServerNetworkMessage::ConvertPacketCreateMyCharaterToCreateOtherCharacter(SerializationBuffer& packetBuf)
{
	*((WORD*)packetBuf.GetFrontBufferPtr()) = (WORD)dfPACKET_SC_CREATE_OTHER_CHARACTER;
}
