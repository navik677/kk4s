/* 'turn' トランジション用変形変換テーブル */
/* kk4s: upstream generated turntrans_table.cpp (422 KB) with
   maketable/mkturntranstable.pl; turntrans_table.cpp here computes the same
   table once at start-up (TVPInitTurnTransParams). */

#ifndef turntrans_table_H
#define turntrans_table_H

#pragma pack(push, 4)
struct tTurnTransParams
{
	int start;
	int len;
	int sx; // source start x ( * 65536 )
	int sy; // source start y ( * 65536 )
	int ex; // source end x ( * 65536 )
	int ey; // source end y ( * 65536 )
	int stepx; // source step x ( * 65536 )
	int stepy; // source step y ( * 65536 )
};
#pragma pack(pop)

extern tTurnTransParams TurnTransParams[64][64];
void TVPInitTurnTransParams(); // kk4s: fills TurnTransParams (idempotent)


#endif

