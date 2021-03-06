enum
{
	LANY = 0,
	LBOL,
	LCLASS,
	LEND,
	LEOL,
	LLPAR,
	LOR,
	LREP,
	LRPAR,
	LRUNE,

	TANY = 0,
	TBOL,
	TCAT,
	TCLASS,
	TEOL,
	TNOTNL,
	TOR,
	TPLUS,
	TQUES,
	TRUNE,
	TSTAR,
	TSUB,

	NSUBEXPM = 32
};

typedef struct Parselex Parselex;
typedef struct Renode Renode;

struct Parselex
{
	/* Parse */
	Renode *next;
	Renode *nodes;
	int sub;
	int instrs;
	jmp_buf exitenv;
	/* Lex */
	void (*getnextr)(Parselex*);
	char *rawexp;
	char *orig;
	Rune rune;
	Rune peek;
	int peeklex;
	int done;
	int literal;
	Rune cpairs[400+2];
	int nc;
};
struct Renode
{
	int op;
	Renode *left;
	Rune r;
	union
	{
		Rune r1;
		int sub;
		Renode *right;
	};
	int nclass;
};
struct Rethread
{
	Reinst *pc;
	Resub sem[NSUBEXPM];
	int pri;
	Rethread *next;
};
struct Reinst
{
	char op;
	int gen;
	Reinst *a;
	union
	{
		Rune r;
		int sub;
	};
	union
	{
		Rune r1;
		Reinst *b;
	};
};

static int lex(Parselex*);
static void getnextr(Parselex*);
static void getnextrlit(Parselex*);
static void getclass(Parselex*);
static Renode *e0(Parselex*);
static Renode *e1(Parselex*);
static Renode *e2(Parselex*);
static Renode *e3(Parselex*);
static Renode *buildclass(Parselex*);
static Renode *buildclassn(Parselex*);
static int pcmp(void*, void*);
static Reprog *regcomp1(char*, int, int);
static Reinst *compile(Renode*, Reprog*, int);
static Reinst *compile1(Renode*, Reinst*, int*, int);
static void prtree(Renode*, int, int);
