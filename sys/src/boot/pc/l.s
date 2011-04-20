#include "x16.h"
#include "mem.h"

#undef ORB

#define DATA32SEL SELECTOR(1, SELGDT, 0)
#define EXEC32SEL SELECTOR(2, SELGDT, 0)
#define DATA16SEL SELECTOR(3, SELGDT, 0)
#define EXEC16SEL SELECTOR(4, SELGDT, 0)

#define SEGSS BYTE $0x36
#define SEGES BYTE $0x26
#define FARRET BYTE $0xCB

TEXT origin(SB), $0
	CLI
	CLR(rCX)
	MTSR(rCX, rSS)
	OPSIZE; MOVL $origin(SB), SP
	PUSHA
	OPSIZE; ADSIZE; PUSHL SP
	OPSIZE; ADSIZE; PUSHL CX
	PUSHI(start(SB))

TEXT pmode32(SB), $0
	CLI

	/* disable nmi */
	PUSHA
	LWI(0x70, rDX)
	INB
	ANDB $0x7F, AL
	OUTB
	POPA

	/* get return pc */
	POPR(rDI)

	/* make sure stack is at 0000: */
	CLR(rCX)
	MTSR(rCX, rSS)
	OPSIZE; ANDL $0xFFFF, SP

	/* convert 16-bit return pc to far pointer */
	PUSHI(EXEC32SEL)
	PUSHR(rDI)

	/* load gdt */
	SEGSS; LGDT(tgdtptr(SB))

	/* enable protected mode */
	MFCR(rCR0, rCX)
	ORB $1, CL
	MTCR(rCX, rCR0)

	/* flush */
	FARJUMP16(EXEC16SEL, pmode32flush(SB));
TEXT pmode32flush(SB), $0

	/* load 32-bit protected mode data selector */
	LWI(DATA32SEL, rCX)

_segret:
	/* load all data segments */
	MTSR(rCX, rDS)
	MTSR(rCX, rES)
	MTSR(rCX, rFS)
	MTSR(rCX, rGS)
	MTSR(rCX, rSS)
	FARRET

TEXT rmode16(SB), $0
	/* setup farret to rmode16x */
	PUSHL $EXEC16SEL
	PUSHL $rmode16x(SB)

	/* load 16-bit protected mode data selector */
	MOVL $DATA16SEL, CX
	JMP _segret

TEXT rmode16x(SB), $0
	/* reload idt */
	SEGSS; LIDT(tidtptr(SB))

	/* disable protected mode */
	MFCR(rCR0, rCX)
	ANDB $0xfe, CL
	MTCR(rCX, rCR0)

	/* flush */
	FARJUMP16(0, rmode16flush(SB));
TEXT rmode16flush(SB), $0

	/*
	 * load 16-bit realmode data segment 0000: and
	 * return to 32 bit return pc interpreted
	 * as 16 bit far pointer.
	 */
	CLR(rCX)
	JMP _segret

TEXT tgdt(SB), $0 
	/* null descriptor */
	LONG $0
	LONG $0

	/* data segment descriptor for 4 gigabytes (PL 0) */
	LONG $(0xFFFF)
	LONG $(SEGG|SEGB|(0xF<<16)|SEGP|SEGPL(0)|SEGDATA|SEGW)

	/* exec segment descriptor for 4 gigabytes (PL 0) */
	LONG $(0xFFFF)
	LONG $(SEGG|SEGD|(0xF<<16)|SEGP|SEGPL(0)|SEGEXEC|SEGR)

	/* data segment descriptor for (PL 0) 16-bit */
	LONG $(0xFFFF)
	LONG $((0xF<<16)|SEGP|SEGPL(0)|SEGDATA|SEGW)

	/* exec segment descriptor for (PL 0) 16-bit */
	LONG $(0xFFFF)
	LONG $((0xF<<16)|SEGP|SEGPL(0)|SEGEXEC|SEGR)

TEXT tgdtptr(SB), $0 
         WORD $(5*8) 
         LONG $tgdt(SB)

TEXT tidtptr(SB), $0
	WORD $0x3ff
	LONG $0

TEXT jump(SB), $0
	MOVL 4(SP), AX
	JMP *AX

TEXT halt(SB), $0
_halt:
	JMP _halt

TEXT spllo(SB), $0
	/* enable nmi */
	PUSHA
	LWI(0x70, rDX)
	INB
	ORB $0x80, AL
	OUTB
	POPA

	STI
	RET

TEXT getc(SB), $0
	CALL rmode16(SB)
	CALL16(spllo(SB))
	MOVB $0x00, AH
	BIOSCALL(0x16)
_getcret:
	CALL16(pmode32(SB))
	ANDL $0xFF, AX
	RET

TEXT gotc(SB), $0
	CALL rmode16(SB)
	CALL16(spllo(SB))
	MOVB $0x01, AH
	BIOSCALL(0x16)
	JNZ _getcret
	CLR(rAX)
	JMP _getcret
	
TEXT putc(SB), $0
	MOVL 4(SP),AX
	CALL rmode16(SB)
	CALL16(spllo(SB))
	MOVB $0x0E, AH
	BIOSCALL(0x10)
_pret32:
	CALL16(pmode32(SB))
	ANDL $0xFFFF, AX
	RET

#ifdef PXE

TEXT pxecallret(SB), $0
	ADDI(6, rSP)
	JMP _pret32

TEXT pxecall(SB), $0
	MOVL op+4(SP),AX
	MOVL buf+8(SP),SI
	CALL rmode16(SB)

	CLR(rCX)
	PUSHR(rCX)
	PUSHR(rSI)

	/* opcode */
	PUSHR(rAX)

	/* farcall */
	PUSHR(rCX)
	PUSHI(pxecallret(SB))

	/* get pxe env structure in ES:BX */
	LWI(0x5650, rAX)
	BIOSCALL(0x1A)
	JC _farret

	/* !PXE or PXEENV+ signature */
	SEGES; LXW(0, xBX, rAX)
	CMPI((('!'<<0)|('P'<<8)), rAX)
	JEQ _getentry
	CMPI((('P'<<0)|('X'<<8)), rAX)
	JNE _farret

	SEGES; LXW(0x2A, xBX, rAX)
	SEGES; LXW(0x28, xBX, rBX)
	MTSR(rAX, rES)

_getentry:
	SEGES; LXW(0x12, xBX, rAX)
	PUSHR(rAX)
	SEGES; LXW(0x10, xBX, rAX)
	PUSHR(rAX)

	CALL16(spllo(SB))

	CLR(rAX)
	CLR(rBX)
	CLR(rCX)
	CLR(rDX)
	CLR(rDI)
	CLR(rSI)
_farret:
	FARRET

#else /* PXE */

/*
 * in:
 *	DL drive
 *	AX:BX lba32,
 *	0000:SI buffer
 */
TEXT readsect16(SB), $0
	PUSHA
	CLR(rCX)

	PUSHR(rCX)		/* qword lba */
	PUSHR(rCX)
	PUSHR(rBX)
	PUSHR(rAX)

	PUSHR(rCX)		/* dword buffer */
	PUSHR(rSI)

	INC(rCX)
	PUSHR(rCX)		/* word # of sectors */

	PUSHI(0x0010)		/* byte reserved, byte packet size */

	MW(rSP, rSI)
	LWI(0x4200, rAX)
	BIOSCALL(0x13)
	JCC _readok
	ADDI(0x10, rSP)
	POPA
	CLR(rAX)
	DEC(rAX)
	RET
_readok:
	ADDI(0x10, rSP)
	POPA
	CLR(rAX)
	RET

TEXT readsect(SB), $0
	MOVL 4(SP), DX
	MOVW 8(SP), AX
	MOVW 10(SP), BX
	MOVL 12(SP), SI 
	CALL rmode16(SB)
	CALL16(spllo(SB))
	CALL16(readsect16(SB))
	CALL16(pmode32(SB))
	ANDL $0xFFFF, AX
	RET

#endif

#ifdef ISO

TEXT bootname(SB), $0
	BYTE $'3'; BYTE $'8'; BYTE $'6'; BYTE $'/';
	BYTE $'9'; BYTE $'b'; BYTE $'o'; BYTE $'o';
	BYTE $'t'; BYTE $'i'; BYTE $'s'; BYTE $'o';
	BYTE $0

#endif

TEXT crnl(SB), $0
	BYTE $'\r'; BYTE $'\n'; BYTE $0

TEXT hex(SB), $0
	BYTE $'0'; BYTE $'1'; BYTE $'2'; BYTE $'3';
	BYTE $'4'; BYTE $'5'; BYTE $'6'; BYTE $'7';
	BYTE $'8'; BYTE $'9'; BYTE $'a'; BYTE $'b';
	BYTE $'c'; BYTE $'d'; BYTE $'e'; BYTE $'f'
