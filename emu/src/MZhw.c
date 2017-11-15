//----------------------------------------------------------------------------
// File:MZhw.c
// MZ-80 Emulator mz80rpi for Raspberry Pi
// mz80rpi:Hardware Emulation Module based on MZ700WIN
// (c) Nibbles Lab./Oh!Ishi 2017
//
// 'mz700win' by Takeshi Maruyama, based on Russell Marks's 'mz700em'.
// Z80 emulation from 'Z80em' Copyright (C) Marcel de Kogel 1996,1997
//----------------------------------------------------------------------------

#define MZHW_H_

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "mz80rpi.h"

#include "z80.h"
#include "Z80Codes.h"

#include "defkey.h"
#include "MZhw.h"

#include "mzmain.h"
#include "mzscrn.h"

#include "mzbeep.h"

#define TEMPO_STROBE_TIME		14								/* テンポビット生成間隔 */
#define PIT_DEBUG 0

/*** for z80.c ***/
int IFreq = 60;
int CpuSpeed=100;
int Z80_IRQ;

/* for memories of MZ */
UINT8	*mem;													/* Main Memory */
UINT8	*junk;													/* to give mmio somewhere to point at */

uint16_t font[256][8][8];										// 256chars*8lines*8dots
uint16_t pcg8000_font[256][8][8];								/* PCG8000 font (2K) */

int rom1_mode;													/* ROM-1 判別用フラグ */
int rom2_mode;													/* ROM-2 判別用フラグ */

/* HARDWARE STATUS WORK */
THW700_STAT		hw700;
T700_TS			ts700;

/* 8253関連ワーク */
T8253_DAT		_8253_dat;
T8253_STAT		_8253_stat[3];

/* for Keyboard Matrix */
UINT8 keyports[10] ={ 0,0,0,0,0, 0,0,0,0,0 };

// MZTフォーマット展開用
UINT32 *mzt_buf;
int mzt_size;
unsigned int pwmTable[256][3];
unsigned int mzt_leader1[450];
unsigned int mzt_leader2[437];

// MZTデータの読み込み
void set_mztData(char *mztFile)
{
	FILE *fd;
	unsigned char tmp_buf[128];
	int bsize, bb, mzt_buf_tmp;
	unsigned int sum;

	fd = fopen(mztFile, "r");
	if(fd == NULL)
	{
		// MZTファイルが開けなかった時はイジェクト
		ts700.mzt_settape = 0;
		return;
	}

	mzt_buf_tmp = 0;
	for(;;)
	{
		// ヘッダ
		for(int i = 0; i < 450; i++)
		{
			mzt_buf[mzt_buf_tmp++] = mzt_leader1[i];
		}
		// 読み込み
		if(fread(tmp_buf, sizeof(unsigned char), 128, fd) < 128)
		{
			break;
		}
		// PWMへの変換とチェックサム
		sum = 0;
		for(int i = 0; i < 128; i++)
		{
			mzt_buf[mzt_buf_tmp++] = pwmTable[tmp_buf[i]][1];
			sum += pwmTable[tmp_buf[i]][2];
		}
		printf("sum=%X, pos=%d\n",sum,mzt_buf_tmp);
		mzt_buf[mzt_buf_tmp++] = pwmTable[(sum >> 8) & 0xff][1];
		mzt_buf[mzt_buf_tmp++] = pwmTable[sum & 0xff][1];
/*		mzt_buf[mzt_buf_tmp++] = 0xD5555540;
		mzt_buf[mzt_buf_tmp++] = 0xAAAAAAA0;
		mzt_buf[mzt_buf_tmp++] = 0x55555540;
		mzt_buf[mzt_buf_tmp++] = 0xAAAAAAA0;
		mzt_buf[mzt_buf_tmp++] = 0x55555540;
		mzt_buf[mzt_buf_tmp++] = 0xAAAAAAA0;
		mzt_buf[mzt_buf_tmp++] = 0x55555540;
		mzt_buf[mzt_buf_tmp++] = 0xAAAAAAA0;
		mzt_buf[mzt_buf_tmp++] = 0x55555540;
		mzt_buf[mzt_buf_tmp++] = 0xAAAAAAA0;
		mzt_buf[mzt_buf_tmp++] = 0x55555540;
		mzt_buf[mzt_buf_tmp++] = 0xAAAAAAA0;
		mzt_buf[mzt_buf_tmp++] = 0x55555540;
		mzt_buf[mzt_buf_tmp++] = 0xAAAAAAA0;
		mzt_buf[mzt_buf_tmp++] = 0x55555540;
		mzt_buf[mzt_buf_tmp++] = 0xAAAAAAA0;
		mzt_buf[mzt_buf_tmp++] = 0x55555540;
		mzt_buf[mzt_buf_tmp++] = 0xAAAAAAA0;
		mzt_buf[mzt_buf_tmp++] = 0x55555540;
		mzt_buf[mzt_buf_tmp++] = 0x80000000;
		sum = 0;
		for(int i = 0; i < 128; i++)
		{
			mzt_buf[mzt_buf_tmp++] = pwmTable[tmp_buf[i]][1];
			sum += pwmTable[tmp_buf[i]][2];
		}
		printf("sum=%X, pos=%d\n",sum,mzt_buf_tmp);
		mzt_buf[mzt_buf_tmp++] = pwmTable[(sum >> 8) & 0xff][1];
		mzt_buf[mzt_buf_tmp++] = pwmTable[sum & 0xff][1];*/
		mzt_buf[mzt_buf_tmp++] = 0xC0000000;
		bsize = tmp_buf[0x12] + (tmp_buf[0x13] << 8);

		// ボディ
		for(int i = 0; i < 437; i++)
		{
			mzt_buf[mzt_buf_tmp++] = mzt_leader2[i];
		}
		// PWMへの変換とチェックサム
		sum = 0;
		for(int i = 0; i < bsize; i++)
		{
			bb = fgetc(fd);
			mzt_buf[mzt_buf_tmp++] = pwmTable[bb][1];
			sum += pwmTable[bb][2];
		}
		mzt_buf[mzt_buf_tmp++] = pwmTable[(sum >> 8) & 0xff][1];
		mzt_buf[mzt_buf_tmp++] = pwmTable[sum & 0xff][1];
		mzt_buf[mzt_buf_tmp++] = 0xC0000000;
	}
	ts700.mzt_bsize = mzt_buf_tmp;
	printf("MZT size:%d\n", ts700.mzt_bsize);
	ts700.mzt_elapse = 0;
	ts700.mzt_settape = 1;
	fclose(fd);
}

//
void vblnk_start(void)
{
	ts700.vsync_tstates = ts700.cpu_tstates;
}

// Keyport Buffer Clear
void mz_keyport_init(void)
{
	memset(keyports, 0xFF, 10);
}

///////////////
// WM_KEYDOWN
///////////////
void mz_keydown_sub(UINT8 code)
{
	int n,b;
	
	n = code >> 4;
	b = code & 0x0F;
	keyports[n] &= (~(1 << b));
}

void mz_keydown(int code)
{
//	UINT8 *kptr = get_keymattbl_ptr() + (menu.keytype << 8);
	UINT8 *kptr = get_keymattbl_ptr();

	if (kptr[code]==0xFF) return;

#if _DEBUG	
	if ( Z80_Trace ) return;
#endif

	mz_keydown_sub(kptr[code]);
#if _DEBUG
//	printf("keydown : kptr = %02x idx%d bit%d\n", code, n, b);
#endif

}

////////////
// WM_KEYUP
////////////
void mz_keyup_sub(UINT8 code)
{
	int n,b;
	
	n = code >> 4;
	b = code & 0x0F;
	keyports[n] |= (1 << b);
}

void mz_keyup(int code)
{
//	UINT8 *kptr = get_keymattbl_ptr() + (menu.keytype << 8);
	UINT8 *kptr = get_keymattbl_ptr();

	if (kptr[code]==0xFF) return;

#if _DEBUG	
	if ( Z80_Trace ) return;
#endif	

	mz_keyup_sub(kptr[code]);
#if _DEBUG
//	printf("keyup : kptr = %02x idx%d bit%d\n", code, n, b);
#endif
}

/* ＭＺをリセットする */
void mz_reset(void)
{
//	int i, a;
//	TMEMBANK *mp;
	Z80_Regs StartRegs;

	/* ８２５３ワークの初期化 */
	memset(&_8253_stat, 0, sizeof(_8253_stat));
	memset(&_8253_dat, 0, sizeof(_8253_dat));

	/* ＭＺワークの初期化 */
	memset(&hw700, 0, sizeof(hw700));
	memset(&ts700, 0, sizeof(ts700));

	/* ＰＩＴ初期化 */
	pit_init();

	/* Ｚ８０レジスタの初期化 */
	Z80_SetRegs(&StartRegs);

	/* キーポートの初期化 */
	mz_keyport_init();

	Z80_intflag=0;
	
	hw700.pcg8000_mode = 0;
	hw700.tempo_strobe = 1;

	hw700.retrace = 1;
	hw700.motor = 0;											/* カセットモーター */
	hw700.vgate = 1;
	ts700.cmt_tstates = 0;
	ts700.cmt_play = 0;

	/* Ｚ８０のリセット */
	Z80_Reset();

}

/////////////////////////////////////////////////////////////////
// 8253 PIT
/////////////////////////////////////////////////////////////////

//////////////
// PIT 初期化
//////////////
void pit_init(void)
{
	T8253_STAT *st;
	int i;

	for (i=0;i<2;i++)
	{
		st = &_8253_stat[i];
		st->rl = st->rl_bak = 3;
		st->counter_base = st->counter = st->counter_lat = 0x0000;
		st->counter_out = 0;
		st->lat_flag = 0;
	}

//	_8253_dat.int_mask = 0;
	
	_8253_dat.bcd = 0;
	_8253_dat.m = 0;
	_8253_dat.rl = 3;
	_8253_dat.sc = 0;
}

////////////////////////////
// 8253 control word write
////////////////////////////
void write_8253_cw(int cw)
{
	T8253_STAT *st;
	
	_8253_dat.bcd = cw & 1;
	_8253_dat.m = (cw >> 1) & 7;
	_8253_dat.rl = (cw >> 4) & 3;
	_8253_dat.sc = (cw >> 6) & 3;

	st = &_8253_stat[_8253_dat.sc];

//	if (_8253_dat.sc==2) st->counter_out = 0;								/* count#2,then clr */
	st->counter_out = 0;								/* count clr */

	/* カウンタ・ラッチ・オペレーション */
	if (_8253_dat.rl==0)
	{
		st->counter_lat = st->counter;
		st->lat_flag = 1;
#if PIT_DEBUG	
//	dprintf("Count%d latch\n",_8253_dat.sc);
#endif
		return;
	}

	st->bit_hl = 0;
	st->lat_flag = 0;
	st->running = 0;
	st->rl = _8253_dat.rl;
	st->bcd = _8253_dat.bcd;
	st->mode = _8253_dat.m;

#if PIT_DEBUG
//	dprintf("Count%d mode=%d rl=%d bcd=%d\n",_8253_dat.sc,_8253_dat.m,
//		_8253_dat.rl,_8253_dat.bcd);
#endif
	
}

//
int pitcount_job(int no,int cou)
{
	T8253_STAT *st;
	int out;
	
	st = &_8253_stat[no];
	out = 0;
	if (!st->running) return 0;

	switch (st->mode)
	{
		/* mode 0 */
	case 0:
		if (st->counter <= 0)
		{
			out=1;
			st->counter = 0;
		}
		else
		{
			st->counter-=cou;
		}
		break;

		/* mode 2 */
	case 2:
		st->counter-=cou;
		if (st->counter<=0)
		{
			/* カウンタ初期値セット */
			st->counter = (int) st->counter_base;
//			do{
//			st->counter += (int) st->counter_base;
//			}while (st->counter<0);
			out=1;														/* out pulse */
		}

		break;
		
	case 3:
		break;	

		/* mode 4 */
	case 4:
		st->counter-=cou;
		if (st->counter<=0)
		{
			/* カウンタ初期値セット */
			st->counter = -1;
			out=1;														/* out pulse (1 time only) */
		}
		break;

	default:
		break;
	}

	st->counter_out = out;
	return out;
}

//
int pit_count(void)
{
	int ret = 0;

	/* カウンタ１を進める */
	if (pitcount_job(1,1))
	{
		/* カウンタ２を進める */
		ret = pitcount_job(2,1);
	}

	return ret;
}

/////////////////////////////////////////////////////////////////
// 8253 SOUND
/////////////////////////////////////////////////////////////////
//
void play8253(void)
{
	int freq2,freqtmp;

	// サウンドを鳴らす
	freqtmp = _8253_stat[0].counter_base;		
	if (_8253_dat.makesound == 0) {
	  _8253_dat.setsound = 0;
		mzbeep_stop();
	}
	else
	if (freqtmp>=0) {
		// play
		//freq2 = (1000000 / freqtmp);
		_8253_dat.setsound = 1;
		mzbeep_setFreq(freqtmp);
	}
	else
	{
		// stop
		mzbeep_stop();
	}
}

///////////////////////////////
// MZ700,1500 sound initiailze
///////////////////////////////
void mzsnd_init(void)
{
//	mzbeep_stop();
}

// PWMパターンの作成
void makePWM(void)
{
	int pat;

	// マークデータ1
	for(int i = 0; i < 441; i+=2)
	{
		mzt_leader1[i] = 0xAAAAAAA0;
		mzt_leader1[i+1] = 0x55555540;
	}
	mzt_leader1[442] = 0xAAADB6C0;
	mzt_leader1[443] = 0xDB6DB6C0;
	mzt_leader1[444] = 0xDB6DB6C0;
	mzt_leader1[445] = 0xDB6DB6C0;
	mzt_leader1[446] = 0x6DB6DB40;
	mzt_leader1[447] = 0xAAAAAAA0;
	mzt_leader1[448] = 0x55555540;
	mzt_leader1[449] = 0xAAAAAAC0;
	// マークデータ2
	for(int i = 0; i < 432; i+=2)
	{
		mzt_leader2[i] = 0xAAAAAAA0;
		mzt_leader2[i+1] = 0x55555540;
	}
	mzt_leader2[432] = 0xAAAAAAA0;
	mzt_leader2[433] = 0x56DB6DA0;
	mzt_leader2[434] = 0xB6DB6DA0;
	mzt_leader2[435] = 0xB6D55540;
	mzt_leader2[436] = 0xAAAAAAC0;
	
	// 8bitデータの変換テーブル
	for(int i = 0; i < 256; i++)
	{
		// データ生成のための初期値
		pwmTable[i][0] = 3;	// ビット長
		pwmTable[i][1] = 0;	// PWMパターン
		pwmTable[i][2] = 0;	// チェックサム

		// データ本体
		pat = i;
		// スタートビット
		pwmTable[i][1] = 0x000000006;
		pwmTable[i][0] = 3;
		for(int j = 0; j < 8; j++)
		{
			if(pat&0x80)
			{
				pwmTable[i][1] = 0x00000006 | (pwmTable[i][1] << 3);
				pwmTable[i][0] += 3;
				pwmTable[i][2]++;
			}
			else
			{
				pwmTable[i][1] = 0x00000002 | (pwmTable[i][1] << 2);
				pwmTable[i][0] += 2;
			}
			pat <<= 1;
		}
		pwmTable[i][1] <<= (32 - pwmTable[i][0]);
	}
}

// SHARP PWMフォーマットにもとづくテープリード信号
int cmt_read(void)
{
	int elapsed, tword, tbit;

	// テープ停止時はゼロを返す
	if(ts700.cmt_tstates == 0)
	{
		return 0;
	}

	// 時間経過からデータの位置を割り出す
	elapsed = ts700.cpu_tstates - ts700.mzt_start;
	elapsed += ts700.mzt_elapse;
	tword = elapsed / (27*428);
	tbit = (elapsed % (27*428)) / 428;
	//printf("%d %d %d ", elapsed, tword, tbit);
	// データエンドの先ならゼロを返す
	if(tword >= ts700.mzt_bsize)
	{
		//printf("0\n");
		ts700.cmt_play = 0;
		ts700.cmt_tstates = 0;
		setup_cpuspeed(1);
		return 0;
	}
	// データ位置からビットの状態を取り出す
	if((mzt_buf[tword] << tbit) & 0x80000000)
	{
		//printf("1\n");
		return 0x20;
	}
	else
	{
		//printf("0\n");
		return 0;
	}
}

////////////////////////////////////////////////////////////
// Memory Access
////////////////////////////////////////////////////////////
// READ
unsigned Z80_RDMEM(dword x)
{
	uint32_t addr = x & 0xFFFF;

	if(addr > 0xDFFF && addr < 0xE013)
	{
		return mmio_in(addr);
	}
	else
	{
		return mem[addr];
	}
}

// WRITE
void Z80_WRMEM(dword x, dword y)
{
	uint32_t addr = x & 0xFFFF;
		  
	if(addr > 0xDFFF && addr < 0xE013)
	{
		mmio_out(x,y);
	}
	else if(addr > 0x0FFF && addr < 0xD400)
	{
		mem[addr] = (BYTE) y;
	}
}

////////////////////////////////////////////////////////////
// Memory-mapped I/O Access
////////////////////////////////////////////////////////////
// IN
int mmio_in(int addr)
{
	int tmp;
	T8253_STAT *st;

	switch(addr)
	{
	case 0xE000:
		return 0xff;
		
	case 0xE001:
		/* read keyboard */
		return keyports[hw700.pb_select];

	case 0xE002:
		/* bit 4 - motor (on=1)
		 * bit 5 - tape data
		 * bit 6 - cursor blink timer
		 * bit 7 - vertical blanking signal (retrace?)
		 */
		tmp=((hw700.cursor_cou%25>15) ? 0x40:0);						/* カーソル点滅タイマー */
		tmp|=cmt_read();
		tmp=(((hw700.retrace^1)<<7)|(ts700.cmt_tstates<<4)|tmp|0x0F);
		return tmp;

	case 0xE003:
		return 0xff;

		/* ＰＩＴ関連 */
	case 0xE004:
	case 0xE005:
	case 0xE006:
		st = &_8253_stat[addr-0xE004];

		if (st->lat_flag)
		{
			/* カウンタラッチオペレーション */
			switch (st->rl)
			{
			case 1:
				/* 下位８ビット呼び出し */
				st->lat_flag = 0;						// counter latch opelation: off
				return (st->counter_lat & 255);
			case 2:
				/* 上位８ビット呼び出し */
				st->lat_flag = 0;						// counter latch opelation: off
				return (st->counter_lat >> 8);
			case 3:
				/* カウンタ・ラッチ・オペレーション */
				if((st->bit_hl^=1)) return (st->counter_lat & 255);			/* 下位 */
				else
				{
					st->lat_flag = 0;					// counter latch opelation: off
					return (st->counter_lat>>8); 	/* 上位 */
				}

				break;
			default:
				return 0x7f;
			}
		}
		else
		{
			switch (st->rl)
			{
			case 1:
				/* 下位８ビット呼び出し */
				return (st->counter & 255);
			case 2:
				/* 上位８ビット呼び出し */
				return (st->counter>>8);
			case 3:
				/* 下位・上位 */
				if((st->bit_hl^=1)) return (st->counter & 255);
				else return (st->counter>>8);
			default:
				return 0xff;
			}
		}
		break;

	case 0xE008:
		/* 音を鳴らすためのタイミングビット生成 */
		return (0x7e | hw700.tempo_strobe);
		
	default:
		/* all unused ones seem to give 7Eh */
		return 0x7e;
	}
}

// OUT
void mmio_out(int addr,int val)
{
	T8253_STAT *st;
	int i;
	int dcode, line, bit;
	int ch;
	uint16_t color;
	const uint16_t c_bright = 0x07e0, c_dark = 0x0000;

	switch(addr)
	{
	case 0xE000:
		if (!(val & 0x80)) hw700.cursor_cou = 0;	// cursor blink timer reset

		hw700.pb_select = (val & 15);
		if (hw700.pb_select>9) hw700.pb_select=9;
		break;
		
	case 0xE001:
	  break;
  
	case 0xE002:
		/* bit 0 - v-gate
		 * bit 1 - cassete write data
		 * bit 2 - led
		 * bit 3 - motor on
		 */
		hw700.vgate = val & 0x01;
		hw700.wdata = val & 0x02;
		hw700.led = val & 0x04;
		if(hw700.motor == 0 && (val & 0x08) != 0)	// 0->1変化時のみ有効
		{
			if(ts700.cmt_tstates == 0)
			{
				if(ts700.cmt_play != 0)
				{
					ts700.mzt_start = ts700.cpu_tstates;
					ts700.cmt_tstates = 1;
					setup_cpuspeed(5);
				}
			}
			else
			{
				// ポーズ
				ts700.cmt_tstates = 0;
				setup_cpuspeed(1);
				ts700.mzt_elapse += (ts700.cpu_tstates - ts700.mzt_start);
			}
		}
		hw700.motor = val & 0x08;
#if _DEBUG
//		dprintf("write beep_mask=%d int_mask=%d\n",_8253_dat.beep_mask,_8253_dat.int_mask);
#endif
		//play8253();
		break;
		
	case 0xE003:
		// bit0:   1=Set / 0=Reset
		// Bit1-3: PC0-7
		i = (val >> 1) & 7;
		if ((val & 1))
		{
			// SET
			switch (i)
			{
			case 0:
				hw700.vgate = 1;
				break;

			case 1:
				// Cassete
				// 1 byte: long:stop bit/ 1=long 0=short *8
				hw700.wdata = 0x02;
#if _DEBUG
//				dprintf("----\n");
#endif
				break;

			case 2:
				hw700.led = 4;
				break;

			case 3:
				if(hw700.motor == 0)	// 0->1変化時のみ有効
				{
					if(ts700.cmt_tstates == 0)
					{
						if(ts700.cmt_play != 0)
						{
							ts700.mzt_start = ts700.cpu_tstates;
							ts700.cmt_tstates = 1;
							setup_cpuspeed(5);
						}
					}
					else
					{
						// ポーズ
						ts700.cmt_tstates = 0;
						setup_cpuspeed(1);
						ts700.mzt_elapse += (ts700.cpu_tstates - ts700.mzt_start);
					}
				}
				hw700.motor = 8;
				break;
			}
		}
		else
		{
			// RESET
			switch (i)
			{
			case 0:
				hw700.vgate = 0;
				break;

				// Cassete
				// 1 byte: long:stop bit/ 1=long 0=short *8
			case 1:
				hw700.wdata = 0;
#if _DEBUG
//				dprintf("cmt_tstates=%d\n",ts700.cmt_tstates);		// short=650,long=1252 ?
#endif
				break;

			case 2:
				hw700.led = 0;
				break;

			case 3:
				hw700.motor = 0;
				break;
			}
		}
//		if (i==0 || i==2)
//		{
//#if _DEBUG
//			dprintf("E003:beep_mask=%d int_mask=%d\n",_8253_dat.beep_mask,_8253_dat.int_mask);
//#endif
//			play8253();
//		}

		break;
		
	case 0xE004:
	case 0xE005:
	case 0xE006:
		/* カウンタに書き込み */
		st = &_8253_stat[addr-0xE004];
		if (st->mode == 0)												/* モード０だったら、outをクリア */
		{
			st->counter_out = 0;
		}
		st->lat_flag = 0;						// counter latch opelation: off

		switch(st->rl)
		{
		case 1:
			/* 下位８ビット書き込み */
			st->counter_base = (val&0x00FF);
			if (!st->running || (st->mode != 2 && st->mode != 3)) {
				st->counter = st->counter_base;
			}
#if PIT_DEBUG
//			dprintf("Count%d=$%04X\n",addr-0xe004,st->counter);
#endif
			st->bit_hl=0;
			st->running = 1;
			break;
		case 2:
			/* 上位８ビット書き込み */
			st->counter_base = (val << 8)&0xFF00;
			if (!st->running || (st->mode != 2 && st->mode != 3)) {
				st->counter = st->counter_base;
			}
#if PIT_DEBUG
//			dprintf("Count%d=$%04X\n",addr-0xe004,st->counter);
#endif
			st->bit_hl=0;
			st->running = 1;
			break;
			
		case 3:
			if (st->bit_hl)
			{
				st->counter_base = ((st->counter_base & 0xFF)|(val << 8))&0xFFFF;
				if (!st->running || (st->mode != 2 && st->mode != 3)) {
					st->counter = st->counter_base;
				}
#if PIT_DEBUG
//				dprintf("H:Count%d=$%04X\n",addr-0xe004,st->counter);
#endif
				st->running = 1;
			}
			else
			{
				st->counter_base = (val&0x00FF);
				if (!st->running || (st->mode != 2 && st->mode != 3)) {
					st->counter = st->counter_base;
				}
#if PIT_DEBUG
//				dprintf("L:Count%d=$%04X\n",addr-0xe004,st->counter);
#endif
			}
			st->bit_hl^=1;
			break;
		}

		/* サウンド用のカウンタの場合 */
		if (addr==0xE004)
		{
			if(!st->bit_hl)
			{
//				dprintf("freqtmp=$%04x makesound=%d\n",freqtmp,_8253_dat.makesound);
				play8253();
			}
		}
		break;
		
	case 0xE007:
#if _DEBUG
//		dprintf("$E007=$%02X\n",val);
#endif
		/* PIT CONTROL WORD */
		write_8253_cw(val);
		break;
		
	case 0xE008:
#if _DEBUG
//		dprintf("$E008=$%02X\n",val);
#endif
		/* bit 0: sound on/off */
		_8253_dat.makesound=(val&1);
		play8253();
		break;

		/* PCG8000 Control */
		/* Data */
	case 0xE010:
		hw700.pcg8000_data = val;
		break;

		/* Addr-L */
	case 0xE011:
		hw700.pcg8000_addr &= 0xFF00;
		hw700.pcg8000_addr |= val;
		break;

		/* Addr-H / Control */
	case 0xE012:
		hw700.pcg8000_addr &= 0x00FF;
		hw700.pcg8000_addr |= (val<<8);
		if ((val & (16+32))==(16+32))
		{
			/* ＣＧ→ＰＣＧコピー */
			i = (hw700.pcg8000_addr & 0x3FF) | 0x400;
			dcode = i >> 3;
			line = i & 0x007;

			memcpy(pcg8000_font[dcode][line], font[dcode][line], sizeof(uint16_t) * 8);
		}
		else
		if (val & 16)
		{
			// アドレス->コード変換
			i = (hw700.pcg8000_addr & 0x3FF) | 0x400;
			dcode = i >> 3;
			line = i & 0x007;

			ch = hw700.pcg8000_data;
			/* ＰＣＧ定義 */
			for(bit = 0; bit < 8; bit++)
			{
				color = (ch & 0x80) == 0 ? c_dark : c_bright;
				pcg8000_font[dcode][line][bit] = color;
				ch <<= 1;
			}
		}

		/* ＰＣＧ選択 */
//		hw700.pcg8000_mode = val & 8;
		break;
		
		
	default:;
		break;
	}
}

////////////////////////////////////////////////////////////
// I/O Port Access
////////////////////////////////////////////////////////////
// IN
byte Z80_In (word Port)
{
	int r=255;
	int iPort = Port & 0x00FF;

	switch (iPort)
	{

#ifdef ENABLE_FDC
		// FDC
	case 0xd8:
	case 0xd9:
	case 0xda:
	case 0xdb:
	case 0xdc:
	case 0xdd:
		r = mz_fdc_r(iPort);
//		dprintf("FDC_R %02x = %02x\n", iPort, r);
#endif //ENABLE_FDC
		break;

	case 0xfe:
		/* z80 pio Port-A DATA Reg.*/
//		r = 0x10;														/* int0からのわりこみ？ */
//		r = 0x20;														/* int1からのわりこみ？ */
//		r = Z80PIO_stat[0].pin;
//		Z80PIO_stat[0].pin = 0xC0;										/* 読み込まれるデータの初期化 */
		r = 0x04;
		break;
	case 0xff:
		/* z80 pio Port-B DATA Reg.*/
		r = 0xff;
		break;
	}
	
	return r;
}

// OUT
void Z80_Out(word Port,word Value)
{
	int iPort = (int) (Port & 0xFF);
	int iVal =(int) (Value & 0xFF);

//	TMEMBANK *mp;


	switch (iPort) {
		// FDC 
	case 0xd8:
	case 0xd9:
	case 0xda:
	case 0xdb:
	case 0xdc:
	case 0xdd:
#ifdef ENABLE_FDC
//		dprintf("FDC_W %02x, %02x\n", iPort, iVal);
//		mz_fdc_w(iPort, iVal);
#endif
		break;

	case 0xfe:
		// z80 pio Port-A DATA Reg.
//		mz_z80pio_data(0,iVal);
		break;
	}
	
	return;
}

/* Called when RETN occurs */
void Z80_Reti (void)
{
}

/* Called when RETN occurs */
void Z80_Retn (void)
{
}

#define cval (CPU_SPEED_BASE / 31250)

/* Interrupt Timer */
int Z80_Interrupt(void)
{
	int ret = 0;

	// Pit Interrupt
	if ( (ts700.cpu_tstates - ts700.pit_tstates) >= cval)
	{
		ts700.pit_tstates += cval;
		if (pit_count())
		{
			if (_8253_stat[2].counter_out)
			{
//				if ( _8253_dat.int_mask )
//				{
					Z80_intflag |= 1;
					Interrupt(0);
//				}
		 	}
		}
	}

	return ret;
}

//--------
// NO Use
//--------
void Z80_Patch (Z80_Regs *Regs)
{

}

//----------------
// CARRY flag set
//----------------
void Z80_set_carry(Z80_Regs *Regs, int sw)
{
	if (sw)	{
		Regs->AF.B.l |= (C_FLAG);
	} else {
		Regs->AF.B.l &= (~C_FLAG);
	}
}

//----------------
// CARRY flag get
//----------------
int Z80_get_carry(Z80_Regs *Regs)
{
	return ((Regs->AF.B.l & C_FLAG) ? 1 : 0);
}

//---------------
// ZERO flag get
//---------------
void Z80_set_zero(Z80_Regs *Regs, int sw)
{
	if (sw)	{
		Regs->AF.B.l |= (Z_FLAG);
	} else {
		Regs->AF.B.l &= (~Z_FLAG);
	}
}

//---------------
// ZERO flag get
//---------------
int Z80_get_zero(Z80_Regs *Regs)
{
	return ((Regs->AF.B.l & Z_FLAG) ? 1 : 0);
}