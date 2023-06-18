#pragma once
#ifndef __MMO_TCP_FIGHTER_CONTENT_SETTINGS_H__
#define __MMO_TCP_FIGHTER_CONTENT_SETTINGS_H__
//-----------------------------------------------------------------
// 캐릭터 기본 hp
//-----------------------------------------------------------------
#define	defCHARACTER_DEFAULT_HP	100


//-----------------------------------------------------------------
// 30초 이상이 되도록 아무런 메시지 수신도 없는경우 접속 끊음.
//-----------------------------------------------------------------
#define dfNETWORK_PACKET_RECV_TIMEOUT	30000


//-----------------------------------------------------------------
// 화면 이동 범위.
//-----------------------------------------------------------------
#define dfRANGE_MOVE_TOP	0
#define dfRANGE_MOVE_LEFT	0
#define dfRANGE_MOVE_RIGHT	6400
#define dfRANGE_MOVE_BOTTOM	6400


//-----------------------------------------------------------------
// 섹터 하나의 높이, 너비
//-----------------------------------------------------------------
#define dfSECTOR_PIXEL_WIDTH	(64 * 3)
#define dfSECTOR_PIXEL_HEIGHT	(64 * 2)

#define dfWORLD_SECTOR_WIDTH	(dfRANGE_MOVE_RIGHT / dfSECTOR_PIXEL_WIDTH + 1)
#define dfWORLD_SECTOR_HEIGHT	(dfRANGE_MOVE_BOTTOM / dfSECTOR_PIXEL_HEIGHT + 1)

//-----------------------------------------------------------------
// 유저 시야 범위(섹터)
//-----------------------------------------------------------------
#define	dfUSER_VISIBLE_SECTOR_WIDTH		3;
#define	dfUSER_VISIBLE_SECTOR_HEIGHT	3;


//---------------------------------------------------------------
// 공격범위.
//---------------------------------------------------------------
#define dfATTACK1_RANGE_X		80
#define dfATTACK2_RANGE_X		90
#define dfATTACK3_RANGE_X		100
#define dfATTACK1_RANGE_Y		10
#define dfATTACK2_RANGE_Y		10
#define dfATTACK3_RANGE_Y		20


//---------------------------------------------------------------
// 공격 데미지.
//---------------------------------------------------------------
#define dfATTACK1_DAMAGE		1
#define dfATTACK2_DAMAGE		2
#define dfATTACK3_DAMAGE		3


//-----------------------------------------------------------------
// 캐릭터 이동 속도   // 25fps 기준 이동속도
//-----------------------------------------------------------------
#define dfSPEED_PLAYER_X	6	// 25fps
#define dfSPEED_PLAYER_Y	4	// 25fps
//#define dfSPEED_PLAYER_X	3   //50fps
//#define dfSPEED_PLAYER_Y	2   //50fps


//-----------------------------------------------------------------
// 이동 오류체크 범위
//-----------------------------------------------------------------
#define dfERROR_RANGE		50


#endif // !__MMO_TCP_FIGHTER_CONTENT_SETTINGS

