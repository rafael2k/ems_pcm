#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h> //elks doesn't have this library? but crt0.S have "_exit"


//#include 	<conio.h> elks doesn't have this library
#include	<malloc.h>

//#include "arch/io.h"		//gcc-ia16用?
#include "arch/ports.h"

#define	COUNTAT	1193180L	/* PC/XT 8253 clock */
#define	COUNT5		2457600L	/* timer clock freq.[Hz] at 5/10/20MHz */
#define	COUNT8		1996800L	/* timer clock freq.[Hz] at 8/16MHz */

unsigned char far	*BeepBuf;		/* data buffer pointer */
static int			playflag;
static long			playcount;
static long			playsize;
static unsigned int	count;
static unsigned int	count0;
static unsigned int	divcount;

//#define outportb	outb
//#define inportb	inb
//#define disable	_disable
//#define enable	_enable


unsigned int OrgTimerVectSEG;
unsigned int OrgTimerVectOFF;
unsigned int lowmode = 0;
long OrgTimerVect;
unsigned int playcount2 = 0;

//void	unrealmodeset();
char table[0x40];
long PCMBUFFER = 0x110000;	//1MiB+64kB
char	cpufamily;
char	isPC98 = 0;
char	emscard;
char	EMJcard;

//#include	<arch/types.h>
typedef __u8                    byte_t;
typedef __u16                   word_t;
typedef __u32                   long_t;
typedef __u16                   seg_t;      /* segment value */
typedef __u16                   segext_t;   /* paragraph count */
typedef __u16                   segoff_t;   /* offset in segment */
typedef __u16                   flag_t;     /* CPU flag word */

typedef __u32                   addr_t;     /* linear 32-bit address */
#include	<linuxmt/memory.h>
#include	<linuxmt/mem.h>
#undef _FP_SEG
#undef _FP_OFF
#undef _MK_FP
#include 	<i86.h>


void copymem(long src, long dst, int size)
{
	if(cpufamily > 2){//on unreal mode
	printf("memcopy from %08lx to %08lx\n",src,dst);
			_asm{
.386
				cli
				mov	esi,src
				mov	edi,dst
				mov	cx,size
				shr	cx,2
cpmem:
				mov	eax,[fs:esi]
				mov	[gs:edi],eax
				add	esi,4
				add	edi,4
				loop	cpmem
				sti
			}
	}else{			//EMS or BANK memory
	int	se= src >> 4;
	int	dst2 = dst >> 4;
	printf("memcopy from %04x:0000 to EMS %04x\n",se,dst2);
			_asm{
				push	es
				push	ds
				cli
				mov	cx,size
				shr	cx,1
				cmp	cx,0
				jne	trans
				mov	cx,8000h
trans:				mov	ax,dst2
				mov	es,ax
				mov	ds,se
				xor	di,di
				xor	si,si
			rep	movsw
				pop	ds
				pop	es
				sti
			}
	}
}

static void disable(){
	_asm {
	cli
	}
}

static void enable(){
	_asm{
	sti
	}
}

void outportb(unsigned int ioport, unsigned char value){
	_asm{
	mov	dx,ioport
	mov	al,value
	out	dx,al
	}
}

unsigned char inportb(unsigned int ioport){
	unsigned char ret1;
	ret1= 0;//コンパイラがうるさい
	_asm{
	mov	dx,ioport
	in	al,dx
	mov	ret1,al
	}
	return ret1;
}

static long getvect(unsigned int intnumber){
	intnumber *= 4;
	unsigned int ret0,ret1;
	long ret;
	ret0 = ret1 = 0;//コンパイラがうるさい
	_asm{
	push	es
	mov	ax,0
	mov	es,ax
	mov	bx,intnumber
	mov	ax,[es:bx]
	add	bx,2
	mov	dx,[es:bx]
	pop	es
	mov	ret0,dx
	mov	ret1,ax
	}
	ret = ret0;
	ret = ret << 16;
	ret |= ret1;
	return ret;
}

static void setvect(unsigned int intnumber, long isr){
	intnumber *= 4;
	int isroff,isrseg;
	isroff = isr &0xffff;
	isrseg = isr >> 16;
	_asm{
	cli
	push	es
	mov	ax,0
	mov	es,ax
	mov	ax,isroff
	mov	bx,intnumber
	mov	es:[bx],ax
	mov	ax,isrseg
	add	bx,2
	mov	es:[bx],ax
	pop	es
	sti
	}
}

static void changeBank(char BANK)
{
	unsigned char	BANK0,BANK1;
	int	BANK2;
	switch(emscard){
	case 1:
	if(isPC98){				//PC-9801-53 or NekoProject2
		BANK0 = BANK >> 4;
		BANK1 = BANK & 0xf;
		BANK1 = BANK1 << 4;
		printf("BANK0 %x BANK1 %x\n",BANK0,BANK1);
		_asm {
			mov	dx,8e9h
			mov	al,BANK0
			out	dx,al
			mov	dx,8e1h
			mov	al,BANK1
			out	dx,al
			mov	dx,8e3h
			add	al,4
			out	dx,al
			mov	dx,8e5h
			add	al,4
			out	dx,al
			mov	dx,8e7h
			add	al,4
			out	dx,al
		}
	}else{				//pcem
		if(BANK == 0){
			_asm {
			mov	dx,20ah
			mov	al,18h
			out	dx,al
			mov	dx,209h
			xor	al,al
			out	dx,al
			mov	dx,20ah
			mov	al,19h
			out	dx,al
			mov	dx,209h
			xor	al,al
			out	dx,al
			mov	dx,20ah
			mov	al,1ah
			out	dx,al
			mov	dx,209h
			xor	al,al
			out	dx,al
			mov	dx,20ah
			mov	al,1bh
			out	dx,al
			mov	dx,209h
			xor	al,al
			out	dx,al
			}
		}else{
			BANK1 = BANK >> 6;
			BANK0 = BANK << 2;
			_asm {
			mov	dx,20ah
			mov	al,18h
			out	dx,al
			mov	dx,208h
			mov	al,BANK0
			out	dx,al
			mov	dx,209h
			mov	al,BANK1
			or	al,80h
			out	dx,al
			mov	dx,20ah
			mov	al,19h
			out	dx,al
			mov	dx,208h
			mov	al,BANK0
			add	al,1
			out	dx,al
			mov	dx,209h
			mov	al,BANK1
			adc	al,0
			or	al,80h
			out	dx,al
			mov	dx,20ah
			mov	al,1ah
			out	dx,al
			mov	dx,208h
			mov	al,BANK0
			add	al,2			
			out	dx,al
			mov	dx,209h
			mov	al,BANK1
			adc	al,0
			or	al,80h
			out	dx,al
			mov	dx,20ah
			mov	al,1bh
			out	dx,al
			mov	dx,208h
			mov	al,BANK0
			add	al,3
			out	dx,al
			out	dx,al
			mov	dx,209h
			mov	al,BANK1
			adc	al,0
			or	al,80h
			out	dx,al
			}
		}
	}
		break;
	case 2://EMJ
			BANK0 = BANK << 2;
		printf("BANK %x card %x\n",BANK0,EMJcard);
	if(BANK == 0){
		_asm {
			mov	dh,EMJcard		//ロータリースイッチ設定で相手する
			or	dh,60h		
			mov	dl,0eeh	//dx=60eeh
			mov	ax,0		//窓0
			out	dx,al
			or	dh,10h
			in	ax,dx
			and	ax,7fffh
			out	dx,ax

			mov	dh,EMJcard
			or	dh,60h		
			mov	dl,0eeh	//dx=60eeh
			mov	ax,1		//窓1
			out	dx,al
			or	dh,10h
			in	ax,dx
			and	ax,7fffh
			out	dx,ax

			mov	dh,EMJcard
			or	dh,60h		
			mov	dl,0eeh	//dx=60eeh
			mov	ax,2		//窓2
			out	dx,al
			or	dh,10h
			in	ax,dx
			and	ax,7fffh
			out	dx,ax

			mov	dh,EMJcard
			or	dh,60h		
			mov	dl,0eeh	//dx=60eeh
			mov	ax,3		//窓3
			out	dx,al
			or	dh,10h
			in	ax,dx
			and	ax,7fffh
			out	dx,ax
		}
	}else{
		_asm {
			mov	dh,EMJcard		//ロータリースイッチ設定で相手する
			or	dh,60h		
			mov	dl,0eeh	//dx=6xeeh
			mov	al,0		//窓0
			out	dx,al
			inc	dx		//dx=6xefh
			mov	al,0c0h	//出す場所C0000-C3FFF
			out	dx,al
			dec	dx
			or	dx,1000h	//dx=7xeeh
			mov	ah,80h
			mov	al,BANK0	//出すメモリ番号 0-255では4MiBまでしか無理
			out	dx,ax

			mov	dh,EMJcard
			or	dh,60h		
			mov	dl,0eeh	//dx=6xeeh
			mov	al,1		//窓1
			out	dx,al
			inc	dx		//dx=6xefh
			mov	al,0c4h	//出す場所C4000-C7FFF
			out	dx,al
			dec	dx
			or	dx,1000h	//dx=7xeeh
			mov	ah,80h
			mov	al,BANK0	//出すメモリ番号 0-255では4MiBまでしか無理
			add	al,1
			out	dx,ax

			mov	dh,EMJcard
			or	dh,60h		
			mov	dl,0eeh	//dx=6xeeh
			mov	al,2		//窓2
			out	dx,al
			inc	dx		//dx=6xefh
			mov	al,0c8h	//出す場所C8000-CBFFF
			out	dx,al
			dec	dx
			or	dx,1000h	//dx=7xeeh
			mov	ah,80h
			mov	al,BANK0	//出すメモリ番号 0-255では4MiBまでしか無理
			add	al,2
			out	dx,ax

			mov	dh,EMJcard
			or	dh,60h		
			mov	dl,0eeh	//dx=6xeeh
			mov	al,3		//窓3
			out	dx,al
			inc	dx		//dx=6xefh
			mov	al,0cfh	//出す場所CC000-CFFFF
			out	dx,al
			dec	dx
			or	dx,1000h	//dx=7xeeh
			mov	ah,80h
			mov	al,BANK0	//出すメモリ番号 0-255では4MiBまでしか無理
			add	al,3
			out	dx,ax
		}
	}
		break;
	case 3://PIO-PC34
			BANK2 = ((int)BANK) << 2;
	if(BANK == 0){
		_asm {
			mov	dx,0e8h
			mov	al,0c0h	//窓0
			out	dx,al
			inc	dx
			mov	al,0h		//出さない
			out	dx,al

			mov	dx,0e8h
			mov	al,0c1h	//窓1
			out	dx,al
			inc	dx
			mov	al,0h		//出さない
			out	dx,al

			mov	dx,0e8h
			mov	al,0c2h	//窓2
			out	dx,al
			inc	dx
			mov	al,0h		//出さない
			out	dx,al

			mov	dx,0e8h
			mov	al,0c3h	//窓3
			out	dx,al
			inc	dx
			mov	al,0h		//出さない
			out	dx,al
		}
	}else{
		_asm {
			mov	ah,0
			mov	dx,0e8h
			mov	al,0c0h	//窓0
			out	dx,al
			inc	dx
			mov	al,0c3h	//出す場所C0000-C3FFF
			out	dx,al
			inc	dx
			mov	ax,BANK2	//出すメモリ番号 0-255では4MiBまでしか無理
			out	dx,ax		//もっと上まで使えるけど面倒なので今はこれで

			mov	dx,0e8h
			mov	al,0c1h	//窓1
			out	dx,al
			inc	dx
			mov	al,0c7h	//出す場所C4000-C7FFF
			out	dx,al
			inc	dx
			mov	ax,BANK2	//出すメモリ番号
			add	ax,1
			out	dx,ax

			mov	dx,0e8h
			mov	al,0c2h	//窓2
			out	dx,al
			inc	dx
			mov	al,0cbh	//出す場所C8000-CBFFF
			out	dx,al
			inc	dx
			mov	ax,BANK2	//出すメモリ番号
			add	ax,2
			out	dx,ax

			mov	dx,0e8h
			mov	al,0c3h	//窓3
			out	dx,al
			inc	dx
			mov	al,0cfh	//出す場所CC000-CFFFF
			out	dx,al
			inc	dx
			mov	ax,BANK2	//出すメモリ番号
			add	ax,3
			out	dx,ax
		}
	}	
	default:
		break;
	}
}

static void interrupt NewTimerVectBeep(void)
{
	static unsigned int	d;
	if(cpufamily < 3){//286のときだけじゃなくて 16bit機の場合
		if((playsize >= 0x10000) &&
			 ((playcount & (0xffff-lowmode)) == (0xffff-lowmode))){//64kB毎
			changeBank(1+(PCMBUFFER + playcount)>>16);
		}
	}
	if(playcount < playsize){
		if ((cpufamily > 2) && (playsize >= 0x10000)){//32bitCPUの場合はハイメモリ直接読み出し
			_asm {
			.386
				mov	edi,playcount
				add	edi,PCMBUFFER		//elks-0.9
				mov	ah,[gs:edi]
				xor	al,al
				mov	d,ax;
			}
			playcount++;
			d /= (0x10000/count);//正規化
		}else{//通常はこの位置から読み出し
//			d=(unsigned int)(((unsigned int)BeepBuf[(playcount++) & 0xffff]*count)>>8)+1; //8bitPCM unsigned
			d=BeepBuf[playcount++]; //8bitPCM unsigned
			d <<=8;//16bit化
			d &= 0xff00;
			d /= (0x10000/count);//正規化
		}
 if(isPC98){
		outportb(0x3fdb, (d&0xff));//LSB	/* counter start */
		outportb(0x3fdb, (d>>8));//MSB	/* counter start */
 }else{
		outportb(0x42, (d & 0xff));	//LSB
		outportb(0x42, (d>>8));		//MSB
 }
		playcount += lowmode;
	}
	else
		playflag = 0;

 if(1){
	playcount2++;
	if(playcount2 == count0){//10ms 100Hz 元々の割り込み処理もやるべき
		playcount2 = 0;
		_asm{
			push	bp
			mov	ax,OrgTimerVectSEG
			push	ax
			mov	ax,OrgTimerVectOFF
			push	ax
			mov	bp,sp
			pushf
			call	dword ptr [bp]
			add	sp,4		;;レジスタだけ触った方がよいかも
//			pop	ax		;;memory access 遅い
//			pop	ax
			pop	bp
		}
	}
 }
//	outportb(0x00,0x20);//本物がやる場合に先取りするとダメ
 if(isPC98)
	outportb(0x00,0x60);//本物がやる場合に先取りするとダメ 0x40もつけたほうがよいのでは?
 else
	outportb(0x20,0x60);//ioport間違えすぎ!!
	if(playflag == 0){//途中でCTRL-C押してしまった場合にはタイマー割り込み処理が続けられる その終端処理
		setvect(0x08,OrgTimerVect);
		changeBank(0);
 if(isPC98){
		/*ｶｳﾝﾀ1(ﾋﾞｰﾌﾟ)の設定*/
		outportb(0x35,(inportb(0x35)|0x08));	/* beep off */
		outportb(0x77,0x76);					/* set timer #1 to mode 3 */
		d = (inportb (0x42) & 0x20)?0x80:0;
		d = (inportb (0x42) == 0xff)?0:d;
		if(d & 0x80){
			count=(unsigned int)((long)COUNT8/2000);
		}
		else{
			count=(unsigned int)((long)COUNT5/2000);
		}
		/*ｶｳﾝﾀ1(ﾋﾞｰﾌﾟ)のｶｳﾝﾄ値*/
		outportb(0x3fdb,(unsigned char)(count&0x00ff));
		outportb(0x3fdb,(unsigned char)(count>>8));		/* set freq 2,000Hz */
 }else{
		/*ｶｳﾝﾀ2(ﾋﾞｰﾌﾟ)の設定*/
		outportb(0x61,(inportb(0x61) & 0xd));	/* beep off */
		outportb(0x43,0xb6);					/* set timer #2 to mode 3 */
		count=(unsigned int)((long)COUNTAT/2000);
		/*ｶｳﾝﾀ2(ﾋﾞｰﾌﾟ)のｶｳﾝﾄ値*/
		outportb(0x42,(unsigned char)(count&0x00ff));
		outportb(0x42,(unsigned char)(count>>8));		/* set freq 2,000Hz */
 }
	}
}


static void interrupt NewTimerVectBeep16(void)
{
	static unsigned int d;

	if(cpufamily < 3){//286のときだけじゃなくて 16bit機の場合
		if( (playsize >= 0x10000) &&
		 ( ((playcount & (0xffff-lowmode)) == (0xffff-lowmode)) || ((playcount & (0xfffe-lowmode)) == (0xfffe-lowmode)) ) ){//64kB毎
			changeBank(1+(PCMBUFFER + playcount)>>16);
		}
	}

	if(playcount < playsize){
		if ((cpufamily > 2) && (playsize >= 0x10000)){//32bitCPUの場合はハイメモリ直接読み出し
			_asm {
			.386
				mov	edi,playcount
				add	edi,PCMBUFFER		//elks-0.9
				mov	ax,[gs:edi]
				mov	d,ax;
			}
		}else{//通常はこの位置から読み出し
			d = BeepBuf[(playcount+1) & 0xffff];	//16bit signed PCM Little endian upper
			d <<= 8;
			d |= BeepBuf[(playcount) & 0xffff];	//16bit signed PCM lower
		}
		d ^= 0x8000;//Unsigned化
		d /= divcount;//先に計算しておいた数値でやる(0x10000/count);//正規化
 if(isPC98){
		outportb(0x3fdb, (d & 0xff));	//LSB
		outportb(0x3fdb, (d>>8));		//MSB
 }else{
		outportb(0x42, (d & 0xff));	//LSB
		outportb(0x42, (d>>8));		//MSB
 }
	}
	else
		playflag = 0;

	playcount +=2;
	playcount += lowmode;
 if(1){
	playcount2++;
	if(playcount2 == count0){
		playcount2 = 0;
		_asm{
			push	bp
			mov	ax,OrgTimerVectSEG
			push	ax
			mov	ax,OrgTimerVectOFF
			push	ax
			mov	bp,sp
			pushf
			call	dword ptr [bp]
			add	sp,4
			pop	bp
		}
	}
 }


 if(isPC98)
	outportb(0x00,0x60);//本物がやる場合に先取りするとダメ 0x40もつけたほうがよいのでは?
 else
	outportb(0x20,0x60);

	if(playflag == 0){//途中でCTRL-C押してしまった場合に
		changeBank(0);
		setvect(0x08,OrgTimerVect);
 if(isPC98){
		/*ｶｳﾝﾀ1(ﾋﾞｰﾌﾟ)の設定*/
		outportb(0x35,(inportb(0x35)|0x08));	/* beep off */
		outportb(0x77,0x76);					/* set timer #1 to mode 3 */
		d = (inportb (0x42) & 0x20)?0x80:0;
		d = (inportb (0x42) == 0xff)?0:d;
		if(d & 0x80){
			count=(unsigned int)((long)COUNT8/2000);
		}
		else{
			count=(unsigned int)((long)COUNT5/2000);
		}
		/*ｶｳﾝﾀ1(ﾋﾞｰﾌﾟ)のｶｳﾝﾄ値*/
		outportb(0x3fdb,(unsigned char)(count&0x00ff));
		outportb(0x3fdb,(unsigned char)(count>>8));		/* set freq 2,000Hz */
 }else{
		/*ｶｳﾝﾀ2(ﾋﾞｰﾌﾟ)の設定*/
		outportb(0x61,(inportb(0x61) & 0xd));	/* beep off */
		outportb(0x43,0xb6);					/* set timer #2 to mode 3 */
		count=(unsigned int)((long)COUNTAT/2000);
		/*ｶｳﾝﾀ2(ﾋﾞｰﾌﾟ)のｶｳﾝﾄ値*/
		outportb(0x42,(unsigned char)(count&0x00ff));
		outportb(0x42,(unsigned char)(count>>8));		/* set freq 2,000Hz */

 }
	}
}

void cpucheck()
{
	_asm{
		cli
		mov	ax,0xf000
		push	ax
		popf
		pushf
		pop	ax
		and	ax,0xf000
		cmp	ax,0
		je	under286
		cmp	ax,0xf000
		je	v30_8086
/*A20判別も危なくなったのでやりません
		mov	al,isPC98
		cmp	al,1
		jne	pcat_a20
		mov	dx,0xf2
		out	dx,al		//A20アンマスク
		mov	dx,0xf6
		mov	al,3
		out	dx,al		//A20再マスク(のつもり)
		jmp	pc98_a20
pcat_a20:
		mov	dx,92h		//ioport A20gate quite dangerous
		in	al,dx
		or	al,0x2
		out	dx,al
		in	al,dx
		in	al,dx
		in	al,dx
		and	al,0xfd
		out	dx,al
pc98_a20:
		xor	ax,ax
		mov	es,ax
		mov	ax,[es:0]	//0000:0000
		mov	dx,0xffff
		mov	es,dx
		mov	dx,[es:10h]	//FFFF:0010
		cmp	dx,ax
		jne	over386_at16	//A20再マスクできなければ 286機のアクセラレータ搭載機種
*/
		mov	al,4		//本体も32bit対応機
		mov	cpufamily,al
		jmp	checkend
under286:
		mov	al,2
		mov	cpufamily,al
		jmp	checkend
v30_8086:
		mov	al,0
		mov	cpufamily,al
		jmp	checkend
over386_at16:
		mov	al,3		//A20マスクにCPU SHUTDOWNが必要 Int1FのAH=90がとっても遅いはず
		mov	cpufamily,al
checkend:
		sti
	}
	printf("cpufamily %x\n",cpufamily);
}

long LoadBF(char *wavename)
{
	FILE *fp;
	long fsize =0;
	int read;
	long Highaddress,Lowaddress;
	long fsize2;
	unsigned char emscard2 = 0xff;
	unsigned char EMJcard2 = 0;

	if((fp = fopen(wavename, "rb")) == NULL) {
		fprintf(stderr, "Error! file open failed.\n");
		return -1L;
	}

	Highaddress = PCMBUFFER;
	Lowaddress = FP_SEG(BeepBuf);
	Lowaddress = Lowaddress << 4;
	Lowaddress |= FP_OFF(BeepBuf);

	while((read=fgetc(fp))!=EOF){
		BeepBuf[fsize]=(char)read;
		fsize++;
		if(fsize == 65536)goto loadhigh;
	}
loadend:
	Highaddress = PCMBUFFER;//0x100000;//戻す

	fclose(fp);
	printf("LoadBF fsize %lu\n",fsize);

	return(fsize);

loadhigh:
	if(cpufamily < 3)goto emscheck;
	copymem(Lowaddress, Highaddress, 0x8000);
 	Highaddress += 0x8000;
	Lowaddress += 0x8000;//dosbox-x用
loadhigh2:
	copymem(Lowaddress, Highaddress, 0x8000);
	Highaddress += 0x8000;
	if(Highaddress > 0xe00000){//14.6MBの位置で止めよう
	//ほんとはここでメモリリミットより大きかったら止めるような安全装置が必要
	//メモリループしてる機種では割り込みテーブルへの上書き(だけではないけど)が発生する
		goto loadhigh;
	}

	fsize2 = 0x8000;
	while((read=fgetc(fp))!=EOF){
		BeepBuf[fsize2++]=(char)read;
		fsize++;
		if(fsize2 == 65536)goto loadhigh2;
	}
//最後もコピーしないと!
	copymem(Lowaddress, Highaddress, 0x8000);
	goto loadend;


emscheck:
 if(isPC98){
	_asm{
		cli
		push	es
		mov	dx,0c000h
		mov	es,dx
		mov	bx,1234h
		mov	ax,[es:bx]
		cmp	ax,0ffffh
		jne	onUMB
		mov	dx,60efh
		mov	cx,16
loopEMJ:
		in	al,dx
		cmp	al,0ffh
		jne	EMJari
		inc	dh
		loop	loopEMJ
		mov	cx,7ffh
		jmp	loopPC34
EMJari:
		mov	EMJcard2,dh
		mov	al,2
		jmp	emscheck_end
loopPC34:
		mov	ax,cx
		mov	dx,0eah
		out	dx,ax
		in	al,dx
		cmp	al,0ffh
		jne	PC34ari
		loop	loopPC34
		jmp	noPC34
PC34ari:
		mov	cx,0
PC34ari2:
		mov	ax,cx
		out	dx,ax
		in	al,dx
		cmp	al,0ffh
		jne	multimode
		inc	cx
		jmp	PC34ari2
multimode:
		mov	ax,cx
		mov	cl,4
		shr	ax,cl
		mov	EMJcard2,al	//EMSとしても使える先頭メモリ番号
		mov	al,3
		jmp	emscheck_end
noPC34:
		mov	dx,8e9h	//PC-9801-53 or NekoProject EMS
		mov	al,1		//100000h
		out	dx,al
		mov	dx,8e1h
		mov	al,0		//100000h とりあえずHMAと被ってる場所
		out	dx,al		//それでも使用中のxmsバッファの可能性はある
		mov	ax,[es:bx]	//いきなり読んだらパリティエラーを吐く可能性ありなので… 怖いっちゃ怖いが…
		cmp	ax,0ffffh
		je	noEMS
		mov	al,1
		jmp	emscheck_end
noEMS:
		mov	al,0xfe
onUMB:
		mov	al,0xff
emscheck_end:
		pop	es
		mov	emscard2,al
		sti
	}
 }else{
	_asm{
		cli
		push	es
		mov	dx,0d000h
		mov	es,dx
		mov	bx,1234h
		mov	ax,[es:bx]
		cmp	ax,0ffffh
		jne	onUMB
		mov	dx,20ah	//pcem award286
		mov	al,18h
		out	dx,al
		mov	dx,208h
		mov	al,40h		//100000h >> 14
		out	dx,al		//HMAが見える
		mov	dx,209h
		mov	al,80h
		out	dx,al
		mov	ax,[es:bx]	//いきなり読んだらパリティエラーを吐く可能性ありなので… 怖いっちゃ怖いが…
		cmp	ax,0ffffh
		je	noEMS
		mov	al,1
		jmp	emscheck_end
noEMS:
		mov	al,0xfe
onUMB:
		mov	al,0xff
emscheck_end:
		pop	es
		mov	emscard2,al
		sti
	}
 }	
	emscard = emscard2;		//うちのopenwatcomがこの数値をちゃんとやってくれないので面倒なことを一個はさんでいる
	printf("EMScard detect %x %x\n",emscard2,EMJcard2);
	if(emscard2 > 0xfd)goto loadend;
	if(emscard == 2){					//EMJがプロテクトメモリにもなってるはず?
		EMJcard = EMJcard2 & 0xf;
		if(PCMBUFFER > (long)EMJcard << 20)
			PCMBUFFER -= (long)EMJcard << 20;	//メモリ位置
		else PCMBUFFER = 0x10000;		//0をいれるとやっかいなのでここからにする
		printf("EMJcard detect %x address %lx\n",EMJcard,0+((PCMBUFFER+ fsize -65536) >> 16));
	}
	if(emscard == 3){
		EMJcard = EMJcard2;				//EMSとしての先頭をとりあえず保管
		if(EMJcard == 0)EMJcard  = 1;	//とりあえず00での先頭はやっかいなのでよける
		if(EMJcard < 4)			//EMSが1Mより下のアドレスを指してるならEMS専用モードかも
			PCMBUFFER = (long)EMJcard << 18;
		if(PCMBUFFER < (long)EMJcard << 18)	//EMS先頭よりもPCM先頭が下ならば上に引き上げる(2枚刺しとかの話…)
			PCMBUFFER = (long)EMJcard << 18;
		printf("PC34 detect %x address %lx\n",EMJcard,0+((PCMBUFFER+ fsize -65536) >> 16));
	}
	if(isPC98)Highaddress = 0xc0000;
	else	Highaddress = 0xd0000;
loadems:
	changeBank(0+((PCMBUFFER+ fsize -65536) >> 16));
	copymem(Lowaddress, Highaddress, 0);
	fsize2 = 0;
	while((read=fgetc(fp))!=EOF){
		BeepBuf[fsize2++]=(char)read;
		fsize++;
		if(fsize2 == 65536)goto loadems;
	}
	changeBank(0+((PCMBUFFER+ fsize) >> 16));
//	copymem(Lowaddress, Highaddress, fsize2);
	copymem(Lowaddress, Highaddress, 0);//全部のメモリに書き込みをやっておかないとパリティエラー吐かれる
	changeBank(PCMBUFFER >> 16);
	free(BeepBuf);
	if(isPC98)BeepBuf = (unsigned char far *)0xc0000000;//固定値
	else	BeepBuf = (unsigned char far *)0xd0000000;//固定値
	goto loadend;
}



void PlayWave(long Fsize,long khz)
{
//	long OrgTimerVect,isr;
	long isr;
	unsigned char imr_m;//, imr_s;
	char	BIOS_FLAG;
	unsigned int	count_o;


//	disable();								/* cli */
//	outportb(0x02,inportb(0x02)|0x01);		/* set timer mask bit */
//	enable();								/* sti */

 if(isPC98){
	outportb(0x2b,0);
	inportb(0x2b);
	inportb(0x2b);
	inportb(0x2b);
	if(inportb(0x2b) != 0xff){//PC-H98なら32bitDMAIOを持ってる
		BIOS_FLAG = 0x80;//8MHz系確定
	}else{//それ以外でもハイレゾだとこのIOを使えない
		BIOS_FLAG = (inportb (0x42) & 0x20)?0x80:0;
		BIOS_FLAG = (inportb (0x42) == 0xff)?0:BIOS_FLAG;
		_asm{
			xor	ax,ax
			mov	es,ax
			mov	al,[es:501h]
			test	al,8
			je	normal
			and	al,80h
			mov	BIOS_FLAG,al
normal:
		}
	}

	if(BIOS_FLAG & 0x80){				/* 8MHz/16MHz... */
		count=(unsigned int)((long)COUNT8/khz);
		count_o = (unsigned int)((long)COUNT8/100);
	}
	else{									/* 5MHz/10MHz... */
		count=(unsigned int)((long)COUNT5/khz);
		count_o = (unsigned int)((long)COUNT5/100);
	}
 }else{//PC/XT
		count=(unsigned int)((long)COUNTAT/khz);
		count_o = (unsigned int)((long)COUNTAT/100);
 }	
	count0 = khz / 100;//元の割り込み周期
	/*ｶｳﾝﾀ0(ｲﾝﾀｰﾊﾞﾙﾀｲﾏ)の設定*/
//	outportb(0x77,0x36);				/* counter mode 3 */
//	outportb(0x71,(unsigned char)(count&0x00ff));		/* rate LSB */
//	outportb(0x71,(unsigned char)(count>>8));			/* rate MSB */
//ぎりぎりまでタイマー側のIOは触らない

	playflag = 1;
	playsize = Fsize;
//	printf("Fsize playsize %lx %lx\n",Fsize ,playsize);

	OrgTimerVect = getvect(0x08);
	OrgTimerVectSEG = FP_SEG(OrgTimerVect);
	OrgTimerVectOFF = FP_OFF(OrgTimerVect);

	printf("Orginal vector8 %04x:%04x\n",FP_SEG(OrgTimerVect),FP_OFF(OrgTimerVect));

	if(BeepBuf[34] == 8){//unsigned 8bit PCM
	isr = 	FP_SEG(NewTimerVectBeep);
	isr = isr << 16;
	isr |= FP_OFF(NewTimerVectBeep);
	printf("new vector8 %04x:%04x\n",FP_SEG(NewTimerVectBeep),FP_OFF(NewTimerVectBeep));
	}else if(BeepBuf[34] == 16){//signed 16bit Little-Endian PCM
	lowmode <<= 1;
	divcount = 0x10000/count;
	isr = 	FP_SEG(NewTimerVectBeep16);
	isr = isr << 16;
	isr |= FP_OFF(NewTimerVectBeep16);
	printf("new vector8 %04x:%04x\n",FP_SEG(NewTimerVectBeep),FP_OFF(NewTimerVectBeep));
	}
 if(isPC98){
	/*ｶｳﾝﾀ0(ｲﾝﾀｰﾊﾞﾙﾀｲﾏ)の設定*/
	outportb(0x77,0x36);						/* counter0 mode 3 */
	outportb(0x71,(unsigned char)(count&0x00ff));		/* rate LSB */
	outportb(0x71,(unsigned char)(count>>8));		/* rate MSB */
	setvect(0x08,isr);
	/*ｶｳﾝﾀ1(ｽﾋﾟｰｶｰ)の設定*/
	outportb(0x77,0x70);		/* set timer #1 to mode 0 */
	outportb(0x35,(inportb(0x35)&(0xff-0x08)));	/* beep on */
 }else{//PC/XT
	/*ｶｳﾝﾀ0(ｲﾝﾀｰﾊﾞﾙﾀｲﾏ)の設定*/
	outportb(0x43,0x36);						/* counter0 mode 3 */
	outportb(0x40,(unsigned char)(count&0x00ff));		/* rate LSB */
	outportb(0x40,(unsigned char)(count>>8));		/* rate MSB */
	setvect(0x08,isr);
	/*ｶｳﾝﾀ2(ｽﾋﾟｰｶｰ)の設定*/
	outportb(0x43,0xb0);		/* set timer #2 to mode 0 LSB/MSB*/
	outportb(0x61,(inportb(0x61)|0x3)&0xf);	/* beep on */

 }
	disable();								/* cli */

 if(isPC98){
//	outportb(0x02,inportb(0x02)&(~0x01));	/* clear timer mask bit */
	outportb(0x02,(imr_m=(unsigned char)inportb(0x02))|(0x00));
//	outportb(0x0a,(imr_s=(unsigned char)inportb(0x0a))|(0x20));//IR13禁止 不要?
 }else{
	outportb(0x21,(imr_m=(unsigned char)inportb(0x21))|(0x00));
 }
	enable();								/* sti */

	while(playflag){
//		printf("playsize %lx playcount %lx\n",playsize,playcount);
	};

 if(isPC98){
	/*ｶｳﾝﾀ0(ｲﾝﾀｰﾊﾞﾙﾀｲﾏ)の設定戻す*/
	outportb(0x77,0x36);						/* counter mode 3 */
	outportb(0x71,(unsigned char)(count_o&0x00ff));		/* rate LSB */
	outportb(0x71,(unsigned char)(count_o>>8));		/* rate MSB */
 }else{//PC/XT
	/*ｶｳﾝﾀ0(ｲﾝﾀｰﾊﾞﾙﾀｲﾏ)の設定戻す*/
	outportb(0x43,0x36);						/* counter mode 3 */
	outportb(0x40,(unsigned char)(count_o&0x00ff));		/* rate LSB */
	outportb(0x40,(unsigned char)(count_o>>8));		/* rate MSB */

 }

	disable();								/* cli */
 if(isPC98){
	outportb(0x02,imr_m);
//	outportb(0x0a,imr_s);
//	outportb(0x02,inportb(0x02)|0x01);		/* set timer mask bit */
 }else{
	outportb(0x21,imr_m);
 }
	enable();								/* sti */

	setvect(0x08,OrgTimerVect);

 if(isPC98){
	/*ｶｳﾝﾀ1(ﾋﾞｰﾌﾟ)の設定*/
	outportb(0x35,(inportb(0x35)|0x08));	/* beep off */
	outportb(0x77,0x76);					/* set timer #1 to mode 3 */
	if(BIOS_FLAG & 0x80){
		count=(unsigned int)((long)COUNT8/2000);
	}
	else{
		count=(unsigned int)((long)COUNT5/2000);
	}
	/*ｶｳﾝﾀ1(ﾋﾞｰﾌﾟ)のｶｳﾝﾄ値*/
	outportb(0x3fdb,(unsigned char)(count&0x00ff));
	outportb(0x3fdb,(unsigned char)(count>>8));		/* set freq 2,000Hz */
 }else{
	/*ｶｳﾝﾀ2(ﾋﾞｰﾌﾟ)の設定*/
	outportb(0x61,(inportb(0x61) & 0xd));	/* beep off */
	outportb(0x43,0xb6);					/* set timer #2 to mode 3 */
	count=(unsigned int)((long)COUNTAT/2000);
	/*ｶｳﾝﾀ2(ﾋﾞｰﾌﾟ)のｶｳﾝﾄ値*/
	outportb(0x42,(unsigned char)(count&0x00ff));
	outportb(0x42,(unsigned char)(count>>8));		/* set freq 2,000Hz */

 }
//	disable();								/* cli */
//	outportb(0x02,inportb(0x02)&(~0x01));	/* clear timer mask bit */
//	outportb(0x02,inportb(0x02)|(01));	/* re-set timer mask bit */
//	enable();								/* sti */
}



/*
 *	main function
 *****************************************************************************/
int main(int argc, char *argv[])
{
	char wavename[80];
	long fsize,freq;

#if 1
#include <fcntl.h>
#include <sys/ioctl.h>
	int fd;
    struct mem_usage mu;
    if ((fd = open("/dev/kmem", O_RDONLY)) < 0) {
        perror("meminfo");
        return 1;
    }

    if (!ioctl(fd, MEM_GETUSAGE, &mu)) {
        /* note MEM_GETUSAGE amounts are floors, so total may display less by 1k than actual*/
        printf("  Main %d/%dK used, %dK free, ",
            mu.main_used, mu.main_used + mu.main_free, mu.main_free);
        printf("XMS %d/%dK used, %dK free\n",
            mu.xms_used, mu.xms_used + mu.xms_free, mu.xms_free);
    }
	PCMBUFFER = ((mu.xms_used) + 63);//16bit
	PCMBUFFER = PCMBUFFER << 10;//24bit
	PCMBUFFER += 0x110000;//最低ベース
	PCMBUFFER &=0xff0000;//64kB毎
#endif

	BeepBuf=(unsigned char far *)fmemalloc(65536);//セグメントルール内に収めた方がよいのではのサイズ?
	if(BeepBuf == NULL) exit(1);

	printf("malloc at %04x:%04x\n",FP_SEG(BeepBuf),FP_OFF(BeepBuf));
	printf("HIGH malloc at %08lx\n",PCMBUFFER);

/*
	if(peekl(0x1e*4,0) == 0x27ecfd80)//Int1Eh N88-BASIC(86)  BIOS Copyright NEC
		isPC98 = 1;
	else
*/	if(inportb(0x11) != 0xff)//PC-98 DMA status for 9801 Emulator etc...
		isPC98 = 1;
	else{
		isPC98 = 0;
		printf("This Machine is not NEC98\n");
	}

	cpucheck();

	if(argc>1){
		strcpy(wavename,argv[1]);
		if ((wavename[0] == '-')&&(wavename[1] == 'l')){
			lowmode = 2;strcpy(wavename,argv[argc-1]);
		}
	}else{
	 strcpy(wavename,"weskar.spk");
	 freq = 22050;
	}

	printf("Loading wave ... ");
	if((fsize=LoadBF(wavename)) < 0L) {
		changeBank(0);
		printf("size %lu\n",fsize);
		exit(1);
	}
	printf("size %lu ok \n",fsize);
	printf("Buf[0-3] %c %c %c %c\n",BeepBuf[0],BeepBuf[1],BeepBuf[2], BeepBuf[3]);
	if((BeepBuf[0] != 'R')||(BeepBuf[1] !='I')||(BeepBuf[2] != 'F')||(BeepBuf[3] != 'F')){
		changeBank(0);
		exit(1);
	}
	if(BeepBuf[20] != 1){		//PCM
		changeBank(0);
		exit(1);
	}

	if(cpufamily < 3)lowmode = 2;	//16bit機では必ず下げることにした

	freq = BeepBuf[25];
	freq = freq << 8;
	freq |= BeepBuf[24];
	printf("WAV sample %uHz ",freq);
	if (freq < 8001)lowmode = 0;
	if(lowmode){
	 if(cpufamily > 2)
		for(lowmode = 1;freq > 4000;lowmode<<=1){ //8kまでは下げることにしたがまだきつい ので4000まで下げる
			freq >>= 1;//周波数を半減していく
		}
	 }else{
		for(lowmode = 1;freq > 2000;lowmode<<=1){ //286未満では2kまで下げることにした
			freq >>= 1;//周波数を半減していく
		}
	 }
	printf("play with %uHz ",freq);
	}
	if(BeepBuf[34] == 8){//unsigned 8bit PCM
		printf("Unsigned 8bit ");
	}else if(BeepBuf[34] == 16){
		printf("Signed 16bit(LE) ");
	}
	if(BeepBuf[22]==2){//モノラルかステレオかしか判別不可 マルチチャンネルPCMとか無視
		printf("stereo\n");
		if(lowmode == 0)lowmode = 1;
		lowmode <<= 1;//stereoの場合は飛ばすサイズは倍 左だけ鳴らす 混ぜない
	}else printf("mono\n");
	playcount = 12;
datachk:
	if(playcount > 500){
		changeBank(0);
		exit(1);
	}
	if((BeepBuf[playcount] != 'd')||(BeepBuf[playcount+1] !='a')||(BeepBuf[playcount+2] != 't')||(BeepBuf[playcount+3] != 'a')){
		playcount += BeepBuf[playcount+4]+8;
		goto datachk;
	}
	playcount += 8;
	printf("OK(%ldbytes).\n",fsize);
	printf("Playing wave ... ");
	if(lowmode){
		lowmode--;
		printf("lowmode %d\n",lowmode);
	}

	if((cpufamily > 2) && (fsize > 0x10000))//386以上のときは上位メモリ使うから開放可
		free(BeepBuf);
//	else if((cpufamily < 3) && (fsize > 0x10000)){//16bitCPUのときはEMS使う
//		free(BeepBuf);
//		BeepBuf = (unsigned char far *)0xc0000000;//固定値
//		changeBank(PCMBUFFER>>16);
//	printf("EMS malloc at %04x:%04x\n",FP_SEG(BeepBuf),FP_OFF(BeepBuf));
//	printf("Buf[0-3] %c %c %c %c\n",BeepBuf[0],BeepBuf[1],BeepBuf[2], BeepBuf[3]);
//	}

	PlayWave(fsize,freq);
	changeBank(0);
	printf("END.\n");

	if(BeepBuf != NULL)
		free(BeepBuf);
}
