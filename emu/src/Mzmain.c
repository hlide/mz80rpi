//----------------------------------------------------------------------------
// File:MZmain.c
// MZ-80 Emulator mz80rpi for Raspberry Pi
// mz80rpi:Main Program Module based on MZ700WIN
// (c) Nibbles Lab./Oh!Ishi 2017
//
// mz700win by Takeshi Maruyama, based on Russell Marks's 'mz700em'.
// MZ700 emulator 'mz700em' for VGA PCs running Linux (C) 1996 Russell Marks.
// Z80 emulation from 'Z80em' Copyright (C) Marcel de Kogel 1996,1997
//----------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/input.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <ctype.h>
#include <errno.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <libgen.h>
#include "mz80rpi.h"

#include "z80.h"
#include "Z80Codes.h"

#include "defkey.h"

#include "mzmain.h"
#include "MZhw.h"
#include "mzscrn.h"
#include "mzbeep.h"

static bool intFlag = false;

static pthread_t scrn_thread_id;
void *scrn_thread(void *arg);
static pthread_t keyin_thread_id;
void *keyin_thread(void *arg);

#define	FIFO_i_PATH	"/tmp/cmdxfer"
#define	FIFO_o_PATH	"/tmp/stsxfer"
int fifo_i_fd, fifo_o_fd;

SYS_STATUS sysst;
int xferFlag = 0;
int wFifoOpenFlag = 0;

#define SyncTime	16667000									/* 1/60 sec. */

int q_kbd;
typedef struct KBDMSG_t {
	long mtype;
	char len;
	unsigned char msg[80];
} KBDMSG;
const unsigned char ak_tbl[] =
{
	0xff, 0xff, 0xff, 0x80, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x84, 0xff, 0xff,
	0xff, 0x92, 0x80, 0x83, 0x80, 0x90, 0x80, 0x81, 0x80, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0x91, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x73, 0x05, 0x64, 0x74,
	0x14, 0x00, 0x10, 0x01, 0x11, 0x02, 0x12, 0x03, 0x13, 0x04, 0x80, 0x54, 0x80, 0x25, 0x80, 0x80,
	0x80, 0x40, 0x62, 0x61, 0x41, 0x21, 0x51, 0x42, 0x52, 0x33, 0x43, 0x53, 0x44, 0x63, 0x72, 0x24,
	0x34, 0x20, 0x31, 0x50, 0x22, 0x23, 0x71, 0x30, 0x70, 0x32, 0x60, 0x80, 0x80, 0x80, 0xff, 0xff,
	0xff, 0x40, 0x62, 0x61, 0x41, 0x21, 0x51, 0x42, 0x52, 0x33, 0x43, 0x53, 0x44, 0x63, 0x72, 0x24,
	0x34, 0x20, 0x31, 0x50, 0x22, 0x23, 0x71, 0x30, 0x70, 0x32, 0x60, 0xff, 0xff, 0xff, 0xff, 0xff,
};
const unsigned char ak_tbl_s[] =
{
	0xff, 0xff, 0xff, 0x93, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0x92, 0xff, 0x83, 0xff, 0x90, 0xff, 0x81, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0x00, 0x10, 0x01, 0x11, 0x02, 0x12, 0x03, 0x13, 0x04, 0x25, 0x05, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x24, 0xff, 0x20, 0xff, 0x30, 0x33,
	0x23, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x31, 0x32, 0x22, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

#define MAX_PATH 256
char PROGRAM_PATH[MAX_PATH];
char FDROM_PATH[MAX_PATH];
char MONROM_PATH[MAX_PATH];
char CGROM_PATH[MAX_PATH];

extern uint16_t c_bright;

//------------------------------------------------
// Memory Allocation for MZ
//------------------------------------------------
int mz_alloc_mem(void)
{
	int result = 0;

	/* Main Memory */
	mem = malloc(64*1024);
	if(mem == NULL)
	{
		return -1;
	}

	/* Junk(Dummy) Memory */
	junk = malloc(4096);
	if(junk == NULL)
	{
		return -1;
	}

	/* MZT file Memory */
	mzt_buf = malloc(4*64*1024);
	if(mzt_buf == NULL)
	{
		return -1;
	}

	/* ROM FONT */
//	font = malloc(ROMFONT_SIZE);
//	if(font == NULL)
//	{
//		result = -1;
//	}

	/* PCG-8000 FONT */
//	pcg8000_font = malloc(PCG8000_SIZE);
//	if(pcg8000_font == NULL)
//	{
//		result = -1;
//	}

	return result;
}

//------------------------------------------------
// Release Memory for MZ
//------------------------------------------------
void mz_free_mem(void)
{
//	if(pcg8000_font)
//	{
//		free(pcg8000_font);
//	}

//	if(font)
//	{
//		free(font);
//	}

	if(mzt_buf)
	{
		free(mzt_buf);
	}

	if(junk)
	{
		free(junk);
	}

	if(mem)
	{
		free(mem);
	}
}

//--------------------------------------------------------------
// ＲＯＭモニタを読み込む
//--------------------------------------------------------------
void monrom_load(void)
{
	FILE *in;

	if((in = fopen(MONROM_PATH, "r")) != NULL)
	{
		fread(mem, sizeof(unsigned char), 4096, in);
		fclose(in);
	}
}

void fdrom_load(void)
{
	FILE *in;

	if((in = fopen(FDROM_PATH, "r")) != NULL)
	{
		fread(mem + ROM2_START, sizeof(unsigned char), 1024, in);
		fclose(in);
	}
}

//--------------------------------------------------------------
// ＭＺのモニタＲＯＭのセットアップ
//--------------------------------------------------------------
void mz_mon_setup(void)
{
	memset(mem, 0xFF, 64*1024);
	memset(mem+RAM_START, 0, 48*1024);
	memset(mem+VID_START, 0, 1024);
}

//--------------------------------------------------------------
// FIFOの準備
//--------------------------------------------------------------
static int setupXfer(void)
{
	// 外部プログラムから入力
	mkfifo(FIFO_i_PATH, 0777);
	fifo_i_fd = open(FIFO_i_PATH, O_RDONLY|O_NONBLOCK);
	if(fifo_i_fd == -1)
	{
		perror("FIFO(i) open");
		return -1;
	}

	// 外部プログラムへ出力
	mkfifo(FIFO_o_PATH, 0777);

	return 0;
}

//--------------------------------------------------------------
// 外部プログラムへのステータス送信処理
//--------------------------------------------------------------
static int statusXfer(void)
{
	char sdata[160];
	int pos = 0;

	if(xferFlag & SYST_CMT)
	{
		sprintf(&sdata[pos], "CP%3d", sysst.tape);
		pos = 6;
		xferFlag &= ~SYST_CMT;
	}
	if(xferFlag & SYST_MOTOR)
	{
		if(pos != 0)
		{
			sdata[pos - 1] = ',';
		}
		sprintf(&sdata[pos], "CM%1d", sysst.motor);
		pos += 4;
		xferFlag &= ~SYST_MOTOR;
	}
	if(xferFlag & SYST_LED)
	{
		if(pos != 0)
		{
			sdata[pos - 1] = ',';
		}
		sprintf(&sdata[pos], "LM%1d", sysst.led);
		pos += 4;
		xferFlag &= ~SYST_LED;
	}
	if(xferFlag & SYST_BOOTUP)
	{
		if(pos != 0)
		{
			sdata[pos - 1] = ',';
		}
		sprintf(&sdata[pos], "BU");
		pos += 3;
		xferFlag &= ~SYST_BOOTUP;
	}
	if(xferFlag & SYST_PCG)
	{
		if(pos != 0)
		{
			sdata[pos - 1] = ',';
		}
		sprintf(&sdata[pos], "GM%1d", hw700.pcg8000_mode);
		pos += 4;
		xferFlag &= ~SYST_PCG;
	}

	if(pos != 0)
	{
		// 初回送信時にオープン
		if(wFifoOpenFlag == 0)
		{
			fifo_o_fd = open(FIFO_o_PATH, O_WRONLY);
			if(fifo_o_fd == -1)
			{
				perror("FIFO(o) open");
				return -1;
			}
			wFifoOpenFlag = 1;
		}

		write(fifo_o_fd, sdata, pos);

		// クローズしない
		//close(fifo_o_fd);
	}
	return 0;
}

//--------------------------------------------------------------
// 外部プログラムからのコマンド処理
//--------------------------------------------------------------
static void processXfer(void)
{
	int num;
	unsigned char ch, cmd[80];
	KBDMSG kbdm;
	static int runmode = 0;
	char str[MAX_PATH];

REPEAT:
	// 予約されたステータス送信処理
	statusXfer();

	// 1行受信
	ch = 0x01;
	num = 1;
	memset(cmd, 0, 80);
	if(read(fifo_i_fd, &cmd[0], 1) <= 0)	// 受信データチェック
	{
		goto EXIT;
	}
	if(cmd[0] == 0)	// 先頭がゼロなら無効行
	{
		goto EXIT;
	}
	while(ch != 0)
	{
		if(read(fifo_i_fd, &ch, 1) == 0)
		{
			usleep(100);
			continue;
		}
		cmd[num++] = ch;
	}
	//printf("recieved:%s\n", cmd);
	num = strlen((char *)cmd);

	switch(cmd[0])
	{
	case 'R':	// Reset MZ
		mz_reset();
		break;
	case 'C':	// Casette Tape
		switch(cmd[1])
		{
		case 'T':	// Set Tape
			if(ts700.cmt_tstates == 0)
			{
				sprintf(str, "%s/%s", PROGRAM_PATH, (char *)&cmd[2]);
				set_mztData(str);
				ts700.cmt_play = 0;
			}
			break;
		case 'P':	// Play Tape
			if(ts700.mzt_settape != 0)
			{
				ts700.cmt_play = 1;
				ts700.mzt_start = ts700.cpu_tstates;
				ts700.cmt_tstates = 1;
				setup_cpuspeed(5);
				sysst.motor = 1;
				xferFlag |= SYST_MOTOR;
			}
			break;
		case 'S':	// Stop Tape
			if(ts700.cmt_tstates != 0)
			{
				ts700.cmt_play = 0;
				ts700.cmt_tstates = 0;
				setup_cpuspeed(1);
				ts700.mzt_elapse += (ts700.cpu_tstates - ts700.mzt_start);
			}
			break;
		case 'E':	// Eject Tape
			if(ts700.cmt_tstates == 0)
			{
				ts700.mzt_settape = 0;
			}
			break;
		default:
			break;
		}
		break;
	case 'P':	// PCG
		if(cmd[1] == '0')	// OFF
		{
			hw700.pcg8000_mode = 0;
		}
		else if(cmd[1] == '1')	// ON
		{
			hw700.pcg8000_mode = 1;
		}
		break;
	case 'K':	// Key in
		kbdm.mtype = 1;
		kbdm.len = num;
		memcpy(kbdm.msg, &cmd[1], num);
		msgsnd(q_kbd, &kbdm, 81, IPC_NOWAIT);
		break;
	case 'S':	// Set/Echo status
		switch(cmd[1])
		{
		case 'R':	// Run
			runmode = 1;
			break;
		case 'S':	// Stop
			runmode = 0;
			break;
		case 'M':	// Monitor ROM
			sprintf(MONROM_PATH, "%s/%s", PROGRAM_PATH, (char *)&cmd[2]);
			monrom_load();
			break;
		case 'F':	// Font ROM
			sprintf(CGROM_PATH, "%s/%s", PROGRAM_PATH, (char *)&cmd[2]);
			font_load(CGROM_PATH);
			break;
		case 'C':	// CRT color
			if(cmd[2] == '0')
			{
				c_bright = 0xf7df;	// WHITE
			}
			else if(cmd[2] == '1')
			{
				c_bright = 0x07e0;	// GREEN
			}
			break;
		case 'E':	// Echo
			xferFlag |= SYST_PCG|SYST_LED|SYST_CMT|SYST_MOTOR;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

EXIT:
	if(runmode == 0)
	{
		usleep(3000);
		goto REPEAT;
	}
}

//--------------------------------------------------------------
// シグナル関連
//--------------------------------------------------------------
void sighandler(int act)
{
	intFlag = true;
}

bool intByUser(void)
{
	return intFlag;
}

void mz_exit(int arg)
{
	sighandler(0);
}

//--------------------------------------------------------------
// メイン部
//--------------------------------------------------------------
int main(int argc, char *argv[])
{
	struct sigaction sa;
	char tmpPathStr[MAX_PATH];

	sigaction(SIGINT, NULL, &sa);
	sa.sa_handler = sighandler;
	sa.sa_flags = SA_NODEFER;
	sigaction(SIGINT, &sa, NULL);

	readlink("/proc/self/exe", tmpPathStr, sizeof(tmpPathStr));
	sprintf(PROGRAM_PATH, "%s", dirname(tmpPathStr));

	mz_screen_init();
	mz_alloc_mem();
	init_defkey();
	read_defkey();
	makePWM();

	// キー1行入力用メッセージキューの準備
	q_kbd = msgget(QID_KBD, IPC_CREAT);

	// 外部プログラムとの通信の準備
	setupXfer();

	// ＭＺのモニタのセットアップ
	mz_mon_setup();

	// メインループ実行
	mainloop();

	// 終了
	mzbeep_clean();
	end_defkey();
	mz_free_mem();
	mz_screen_finish();

	return 0;

}


////-------------------------------------------------------------
////  mz700win MAINLOOP
////-------------------------------------------------------------
void mainloop(void)
{
	int _synctime = SyncTime;
	struct timespec timetmp, w, synctmp;

	mzbeep_init(44100);

	/* スレッド　開始 */
	start_thread();
	
	// Reset MZ
	mz_reset();

	setup_cpuspeed(1);
	Z80_IRQ = 0;
//	Z80_Trap = 0x0556;

	w.tv_sec = 0;
	xferFlag |= SYST_BOOTUP;

	// start the CPU emulation
	while(!intByUser())
	{
		processXfer();	// 外部プログラムからのコマンド処理

		clock_gettime(CLOCK_MONOTONIC_RAW, &timetmp);
		if (!Z80_Execute()) break;
		clock_gettime(CLOCK_MONOTONIC_RAW, &synctmp);
		if(synctmp.tv_nsec < timetmp.tv_nsec)
		{
			w.tv_nsec = _synctime - (1000000000 + synctmp.tv_nsec - timetmp.tv_nsec);
		}
		else
		{
			w.tv_nsec = _synctime - (synctmp.tv_nsec - timetmp.tv_nsec);
		}
		if(w.tv_nsec > 0)
		{
			nanosleep(&w, NULL);
		}

#if _DEBUG
		if (Z80_Trace)
		{
			usleep(1000000);
		}
#endif
	}
//	
}

//------------------------------------------------------------
// CPU速度を設定 (10-100)
//------------------------------------------------------------
void setup_cpuspeed(int mul) {
	int _iperiod;
	//int a;

	_iperiod = (CPU_SPEED*CpuSpeed*mul)/(100*IFreq);

	//a = (per * 256) / 100;

	_iperiod *= 256;
	_iperiod >>= 8;

	Z80_IPeriod = _iperiod;
	Z80_ICount = _iperiod;

}

//--------------------------------------------------------------
// スレッドの準備
//--------------------------------------------------------------
int create_thread(void)
{
	return 0;
}

//--------------------------------------------------------------
// スレッドの開始
//--------------------------------------------------------------
void start_thread(void)
{
	int st;

	st = pthread_create(&scrn_thread_id, NULL, scrn_thread, NULL);
	if(st != 0)
	{
		perror("update_scrn_thread");
		return;
	}
	pthread_detach(scrn_thread_id);

	st = pthread_create(&keyin_thread_id, NULL, keyin_thread, NULL);
	if(st != 0)
	{
		perror("keyin_thread");
		return;
	}
	pthread_detach(keyin_thread_id);

}

//--------------------------------------------------------------
// スレッドの後始末
//--------------------------------------------------------------
int end_thread(void)
{
	return 0;
}

//--------------------------------------------------------------
// 画面描画スレッド 
//--------------------------------------------------------------
void * scrn_thread(void *arg)
{
	struct timespec vsynctmp, timetmp, vsyncwait;
	
	vsyncwait.tv_sec = 0;

	while(!intByUser())
	{
		// 画面更新処理
		hw700.retrace = 1;											/* retrace = 0 : in v-blnk */
		vblnk_start();

		clock_gettime(CLOCK_MONOTONIC_RAW, &timetmp);
		update_scrn();												/* 画面描画 */

		clock_gettime(CLOCK_MONOTONIC_RAW, &vsynctmp);
		if(vsynctmp.tv_nsec < timetmp.tv_nsec)
		{
			vsyncwait.tv_nsec = SyncTime - (1000000000 + vsynctmp.tv_nsec - timetmp.tv_nsec);
		}
		else
		{
			vsyncwait.tv_nsec = SyncTime - (vsynctmp.tv_nsec - timetmp.tv_nsec);
		}
		if(vsyncwait.tv_nsec > 0)
		{
			nanosleep(&vsyncwait, NULL);
		}

	}
	return NULL;
}

//--------------------------------------------------------------
// キーボード入力スレッド
//--------------------------------------------------------------
void * keyin_thread(void *arg)
{
	int fd = 0, fd2, key_active = 0, i;
	struct input_event event;
	KBDMSG kbdm;

	fd2 = open("/dev/input/mice", O_RDONLY);
	if(fd2 != -1)
	{
		ioctl(fd2, EVIOCGRAB, 1);
	}

	while(!intByUser())
	{
		// キー入力
		if(key_active == 0)
		{
			fd = open("/dev/input/event0", O_RDONLY);
			if(fd != -1)
			{
				ioctl(fd, EVIOCGRAB, 1);
				key_active = 1;	// キーデバイス確認、次回はキー入力
			}
		}
		else
		{
			if(read(fd, &event, sizeof(event)) == sizeof(event))
			{
				if(event.type == EV_KEY)
				{
					switch(event.value)
					{
					case 0:
						mz_keyup(event.code);
						break;
					case 1:
						mz_keydown(event.code);
						break;
					default:
						break;
					}
					//printf("code = %d, value = %d\n", event.code, event.value);
				}
			}
			else
			{
				key_active = 0;	// キーデバイス消失、次回はデバイスオープンを再試行
			}
		}

		// 外部プログラムからの1行入力
		if(msgrcv(q_kbd, &kbdm, 81, 0, IPC_NOWAIT) != -1)
		{
			for(i = 0; i < kbdm.len; i++)
			{
				if(ak_tbl[kbdm.msg[i]] == 0xff)
				{
					continue;
				}
				else if(ak_tbl[kbdm.msg[i]] == 0x80)
				{
					mz_keydown_sub(ak_tbl[kbdm.msg[i]]);
					usleep(60000);
					mz_keydown_sub(ak_tbl_s[kbdm.msg[i]]);
					usleep(60000);
					mz_keyup_sub(ak_tbl_s[kbdm.msg[i]]);
					mz_keyup_sub(ak_tbl[kbdm.msg[i]]);
					usleep(60000);
				}
				else
				{
					mz_keydown_sub(ak_tbl[kbdm.msg[i]]);
					usleep(60000);
					mz_keyup_sub(ak_tbl[kbdm.msg[i]]);
					usleep(60000);
				}
			}
		}
		usleep(10000);
	}

	ioctl(fd, EVIOCGRAB, 0);
	close(fd);
	ioctl(fd2, EVIOCGRAB, 0);
	close(fd2);
	return NULL;
}
