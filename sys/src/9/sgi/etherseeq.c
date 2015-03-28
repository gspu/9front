#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/netif.h"
#include "etherif.h"

typedef struct Hio Hio;
typedef struct Desc Desc;
typedef struct Ring Ring;
typedef struct Ctlr Ctlr;

/*
 * SEEQ 8003 interfaced to HPC3 (very different from IP20)
 */
struct Hio
{
	ulong	unused0[20480];
	ulong	crbp;		/* current receive buf desc ptr */
	ulong	nrbdp;		/* next receive buf desc ptr */
	ulong	unused1[1022];
	ulong	rbc;		/* receive byte count */
	ulong	rstat;		/* receiver status */
	ulong	rgio;		/* receive gio fifo ptr */
	ulong	rdev;		/* receive device fifo ptr */
	ulong	unused2;
	ulong	ctl;		/* interrupt, channel reset, buf oflow */
	ulong	dmacfg;		/* dma configuration */
	ulong	piocfg;		/* pio configuration */
	ulong 	unused3[1016];
	ulong	cxbdp;		/* current xmit buf desc ptr */
	ulong	nxbdp;		/* next xmit buffer desc. pointer */
	ulong	unused4[1022];
	ulong	xbc;		/* xmit byte count */
	ulong	xstat;
	ulong	xgio;		/* xmit gio fifo ptr */
	ulong	xdev;		/* xmit device fifo ptr */
	ulong	unused5[1020];
	ulong	crbdp;		/* current receive descriptor ptr */
	ulong	unused6[2047];
	ulong	cpfxbdp;	/* current/previous packet 1st xmit */
	ulong	ppfxbdp;	/* desc ptr */
	ulong	unused7[59390];
	ulong	eaddr[6];	/* seeq station address wo */
	ulong	csr;		/* seeq receiver cmd/status reg */
	ulong	csx;		/* seeq transmitter cmd/status reg */
};

enum
{			/* ctl */
	Cover=	0x08,		/* receive buffer overflow */
	Cnormal=0x00,		/* 1=normal, 0=loopback */
	Cint=	0x02,		/* interrupt (write 1 to clear) */
	Creset=	0x01,		/* ethernet channel reset */

			/* xstat */
	Xdma=	0x200,		/* dma active */
	Xold=	0x080,		/* register has been read */
	Xok=	0x008,		/* transmission was successful */
	Xmaxtry=0x004,		/* transmission failed after 16 attempts */
	Xcoll=	0x002,		/* transmission collided */
	Xunder=	0x001,		/* transmitter underflowed */

			/* csx */
	Xreg0=	0x00,		/* access reg bank 0 incl station addr */
	XIok=	0x08,
	XImaxtry=0x04,
	XIcoll=	0x02,
	XIunder=0x01,

			/* rstat */
	Rlshort=0x800,		/* [small len in received frame] */
	Rdma=	0x200,		/* dma active */
	Rold=	0x80,		/* register has been read */
	Rok=	0x20,		/* received good frame */
	Rend=	0x10,		/* received end of frame */
	Rshort=	0x08,		/* received short frame */
	Rdrbl=	0x04,		/* dribble error */
	Rcrc=	0x02,		/* CRC error */
	Rover=	0x01,		/* overflow error */

			/* csr */
	Rsmb=	0xc0,		/* receive station/broadcast/multicast frames */
	Rsb=	0x80,		/* receive station/broadcast frames */
	Rprom=	0x40,		/* receive all frames */
	RIok=	0x20,	 	/* interrupt on good frame */
	RIend=	0x10,		/* interrupt on end of frame */
	RIshort=0x08,		/* interrupt on short frame */
	RIdrbl=	0x04,		/* interrupt on dribble error */
	RIcrc=	0x02,		/* interrupt on CRC error */
	RIover=	0x01,		/* interrupt on overflow error */

	HPC_MODNORM=	0x0,	/* mode: 0=normal, 1=loopback */
	HPC_FIX_INTR=	0x8000,	/* start timeout counter after */
	HPC_FIX_EOP=	0x4000,	/* rcv_eop_intr/eop_in_chip is set */ 
	HPC_FIX_RXDC=	0x2000,	/* clear eop status upon rxdc */
};

struct Desc
{
	ulong	addr;		/* addr */
	ulong	count;		/* eox / eop / busy / xie / count:13 */
	ulong	next;
	uchar*	base;
};

struct Ring
{
	Rendez;
	int	size;
	uchar*	base;
	Desc*	head;
	Desc*	tail;
};

enum
{
	Eor=	1<<31,		/* end of ring */
	Eop=	1<<30,
	Ie=	1<<29,
	Busy=	1<<24,
	Empty=	1<<14,		/* no data here */
};

enum {
	Rbsize = ETHERMAXTU+3,
};

struct Ctlr
{
	int	attach;

	Hio	*io;

	Ring	rx;
	Ring	tx;
};

static ulong dummy;

static void
interrupt(Ureg *, void *arg)
{
	Ether *edev;
	Ctlr *ctlr;
	Hio *io;
	uint s;

	edev = arg;
	ctlr = edev->ctlr;
	io = ctlr->io;
	s = io->ctl;
	if(s & Cover)
		io->ctl = Cnormal | Cover;
	if(s & Cint) {
		io->ctl = Cnormal | Cint;
		wakeup(&ctlr->rx);
	}
}

static int
notempty(void *arg)
{
	Ctlr *ctlr = arg;
	Hio *io;

	io = ctlr->io;
	dummy = io->piocfg;
	if((io->rstat & Rdma) == 0)
		return 1;
	return (IO(Desc, ctlr->rx.head->next)->count & Empty) == 0;
}

static void
rxproc(void *arg)
{
	Ether *edev = arg;
	Ctlr *ctlr;
	Hio *io;
	Block *b;
	Desc *p;
	int n;

	while(waserror())
		;

	ctlr = edev->ctlr;
	io = ctlr->io;
	for(p = IO(Desc, ctlr->rx.head->next);; p = IO(Desc, p->next)){
		while((p->count & Empty) != 0){
			io->rstat = Rdma;
			tsleep(&ctlr->rx, notempty, ctlr, 500);
		}
		n = Rbsize - (p->count & 0x3fff)-3;
		if(n >= ETHERMINTU){
			if((p->base[n+2] & Rok) != 0){
				b = allocb(n);
				b->wp += n;
				memmove(b->rp, p->base+2, n);
				etheriq(edev, b, 1);
			}
		}
		p->addr = PADDR(p->base);
		p->count = Ie|Empty|Rbsize;
		ctlr->rx.head = p;
	}
}

static void
txproc(void *arg)
{
	Ether *edev = arg;
	Ctlr *ctlr;
	Hio *io;
	Block *b;
	Desc *p;
	int clean, n;

	while(waserror())
		;

	ctlr = edev->ctlr;
	io = ctlr->io;
	clean = ctlr->tx.size / 2;
	for(p = IO(Desc, ctlr->tx.tail->next); (b = qbread(edev->oq, 1000000)) != nil; p = IO(Desc, p->next)){
		while(!clean){
			splhi();
			p = ctlr->tx.head;
			dummy = io->piocfg;
			ctlr->tx.head = IO(Desc, io->nxbdp & ~0xf);
			spllo();
			while(p != ctlr->tx.head){
				if((p->count & Busy) == 0)
					break;
				clean++;
				p->count = Eor|Eop;
				p = IO(Desc, p->next);
			}

			p = IO(Desc, ctlr->tx.tail->next);
			if(clean)
				break;

			io->xstat = Xdma;
			tsleep(&ctlr->tx, return0, nil, 10);
		}
		clean--;

		n = BLEN(b);
		if(n > ETHERMAXTU)
			n = ETHERMAXTU;
		memmove(p->base, b->rp, n);

		p->addr = PADDR(p->base);
		p->count = Eor|Eop|Busy|n;

		ctlr->tx.tail->count &= ~Eor;
		ctlr->tx.tail = p;

		io->xstat = Xdma;

		freeb(b);
	}
}

static void
allocring(Ring *r, int n)
{
	uchar *b;
	Desc *p;
	int m;

	r->size = n;
	
	m = n*BY2PG/2;
	b = xspanalloc(m, BY2PG, 0);
	dcflush(b, m);
	b = IO(uchar, b);
	memset(b, 0, m);
	r->base = b;

	m = n*sizeof(Desc);
	p = xspanalloc(m, BY2PG, 0);
	dcflush(p, m);
	p = IO(Desc, p);
	memset(p, 0, m);
	r->head = r->tail = p;

	for(m=0; m<n; m++, p++, b += (BY2PG/2)){
		p->base = b;
		p->next = PADDR(p+1);
	}
	p[-1].next = PADDR(r->head);
}

static int
init(Ether *edev)
{
	Ctlr *ctlr;
	Desc *p;
	Hio *io;
	int i;

	io = IO(Hio, edev->port);
	ctlr = edev->ctlr;
	ctlr->io = io;

	io->csx = Xreg0;
	allocring(&ctlr->rx, 256);
	allocring(&ctlr->tx, 64);

	io->rstat = 0;
	io->xstat = 0;
	io->ctl = Cnormal | Creset | Cint;
	delay(10);
	io->ctl = Cnormal;
	io->csx = 0;
	io->csr = 0;

	io->dmacfg |= HPC_FIX_INTR | HPC_FIX_EOP | HPC_FIX_RXDC;

	p = ctlr->rx.head;
	do {
		p->addr = PADDR(p->base);
		p->count = Ie|Empty|Rbsize;
		p = IO(Desc, p->next);
	} while(p != ctlr->rx.head);
	io->crbdp = PADDR(p);
	io->nrbdp = p->next;

	p = ctlr->tx.tail;
	do {
		p->addr = 0;
		p->count = Eor|Eop;
		p = IO(Desc, p->next);
	} while(p != ctlr->tx.tail);
	ctlr->tx.head = IO(Desc, p->next);
	io->cxbdp = PADDR(p);
	io->nxbdp = p->next;

	for(i=0; i<6; i++)
		io->eaddr[i] = edev->ea[i];

	io->csx = 0; /* XIok | XImaxtry | XIcoll | XIunder; -- no interrupts needed */
	io->csr = Rprom | RIok|RIend|RIshort|RIdrbl|RIcrc;

	return 0;
}

/*
 * do nothing for promiscuous() and multicast() as we
 * are always in promisc mode.
 */
static void
promiscuous(void*, int)
{
}
static void
multicast(void*, uchar*, int)
{
}

static void
attach(Ether *edev)
{
	Ctlr *ctlr;

	ctlr = edev->ctlr;
	if(ctlr->attach)
		return;
	ctlr->attach = 1;
	kproc("#0rx", rxproc, edev);
	kproc("#0tx", txproc, edev);
}

static int
pnp(Ether *edev)
{
	static Ctlr ct;
	char *s;

	/* only one controller */
	if(edev->ctlrno != 0)
		return -1;

	/* get mac address from nvram */
	if((s = getconf("eaddr")) != nil)
		parseether(edev->ea, s);

	edev->ctlr = &ct;
	edev->port = HPC3_ETHER;
	edev->irq = IRQENET;
	edev->irqlevel = hpc3irqlevel(edev->irq);
	edev->ctlr = &ct;
	edev->promiscuous = promiscuous;
	edev->multicast = multicast;
	edev->interrupt = interrupt;
	edev->attach = attach;
	edev->arg = edev;
	edev->mbps = 10;
	edev->link = 1;
	if(init(edev) < 0){
		edev->ctlr = nil;
		return -1;
	}
	return 0;
}

void
etherseeqlink(void)
{
	addethercard("seeq", pnp);
}
