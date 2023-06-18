#pragma once
#ifndef __MMO_TCP_FIGHTER_CONTENT_PROCESS_PROTOCOL_H__
#define __MMO_TCP_FIGHTER_CONTENT_PROCESS_PROTOCOL_H__
//-----------------------------------------------------------------
// 캐릭터 액션 정지 상태
//-----------------------------------------------------------------
#define INVALID_ACTION	0xff


//-----------------------------------------------------------------
// 유효하지 않은 캐릭터 ID
//-----------------------------------------------------------------
#define INVALID_CHARACTER_ID  (DWORD)(~0)


//-----------------------------------------------------------------
// FPS 계산식
//-----------------------------------------------------------------
#define	INTERVAL_FPS(FRAME_COUNT)	(1000 / FRAME_COUNT)


//-----------------------------------------------------------------
// 이동 8방향
//-----------------------------------------------------------------
#define dfPACKET_MOVE_DIR_LL					0
#define dfPACKET_MOVE_DIR_LU					1
#define dfPACKET_MOVE_DIR_UU					2
#define dfPACKET_MOVE_DIR_RU					3
#define dfPACKET_MOVE_DIR_RR					4
#define dfPACKET_MOVE_DIR_RD					5
#define dfPACKET_MOVE_DIR_DD					6
#define dfPACKET_MOVE_DIR_LD					7


#endif // !__MMO_TCP_FIGHTER_CONTENT_PROCESS_PROTOCOL__

