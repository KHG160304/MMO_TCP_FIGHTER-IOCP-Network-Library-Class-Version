#pragma once
#ifndef __MMO_TCP_FIGHTER_SERVER_NETWORK_MESSAGE_H__
#define __MMO_TCP_FIGHTER_SERVER_NETWORK_MESSAGE_H__

typedef unsigned long DWORD;
typedef unsigned char BYTE;
typedef unsigned short WORD;

class SerializationBuffer;
struct CharacterInfo;

class MmoTcpFigheterServerNetworkMessage
{
public:
	static void MakePacketEcho(SerializationBuffer& packetBuf, DWORD time);
	static void MakePacketSyncXYPos(SerializationBuffer& packetBuf, DWORD id, WORD xPos, WORD yPos);
	static void MakePacketCreateMyCharacter(SerializationBuffer& packetBuf, DWORD id, BYTE stop2Dir, WORD xPos, WORD yPos, BYTE hp);
	static void MakePacketCreateMyCharacter(SerializationBuffer& packetBuf, CharacterInfo* charac);
	static void MakePacketCreateOtherCharacter(SerializationBuffer& packetBuf, DWORD id, BYTE stop2Dir, WORD xPos, WORD yPos, BYTE hp);
	static void MakePacketCreateOtherCharacter(SerializationBuffer& packetBuf, CharacterInfo* charac);
	static void MakePacketDeleteCharacter(SerializationBuffer& packetBuf, DWORD id);
	static void MakePacketMoveStart(SerializationBuffer& packetBuf, DWORD id, BYTE move8Dir, WORD xPos, WORD yPos);
	static void MakePacketMoveStart(SerializationBuffer& packetBuf, CharacterInfo* charac);
	static void MakePacketMoveStop(SerializationBuffer& packetBuf, DWORD id, BYTE dir, WORD xPos, WORD yPos);
	static void MakePacketAttack1(SerializationBuffer& packetBuf, DWORD id, BYTE dir, WORD xPos, WORD yPos);
	static void MakePacketAttack2(SerializationBuffer& packetBuf, DWORD id, BYTE dir, WORD xPos, WORD yPos);
	static void MakePacketAttack3(SerializationBuffer& packetBuf, DWORD id, BYTE dir, WORD xPos, WORD yPos);
	static void MakePacketDamage(SerializationBuffer& packetBuf, DWORD attackerID, DWORD damagedID, BYTE damageHP);

	static void ConvertPacketCreateMyCharaterToCreateOtherCharacter(SerializationBuffer& packetBuf);
private:
	MmoTcpFigheterServerNetworkMessage() {};
};

typedef	MmoTcpFigheterServerNetworkMessage TcpFighterMessage;
#endif // !__MMO_TCP_FIGHTER_SERVER_NETWORK_MESSAGE

