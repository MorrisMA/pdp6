#include "pdp6.h"
#include "args.h"

#define MAXOPS 20

static char *lp;
static int numops;
static char *ops[MAXOPS];

/* current apr */
static Apr *apr;

static void
skipwhite(void)
{
	while(isspace(*lp))
		lp++;
}

static int
isdelim(char c)
{
	return c && strchr(" \t\n;'\"", c) != nil;
}

/* tokenize lp into ops[numops] */
static void
splitops(void)
{
	char delim;
	char *p;

	numops = 0;
	for(; *lp; lp++){
		skipwhite();
		if(*lp == ';' || *lp == '\0')
			return;
		if(numops == MAXOPS){
			fprintf(stderr, "Too many arguments, ignored <%s>\n", lp);
			return;
		}
		if(*lp == '"' || *lp == '\''){
			delim = *lp++;
			ops[numops++] = p = lp;
			while(*lp && *lp != delim){
				if(*lp == '\\')
					lp++;
				*p++ = *lp++;
			}
			*p = '\0';
		}else{
			ops[numops++] = p = lp;
			while(!isdelim(*lp)){
				if(*lp == '\\')
					lp++;
				*p++ = *lp++;
			}
			*p = '\0';
		}
	}
}

/* Parse an octal, decimal or hexadecimal number.
 *  0nnnn  is an octal number.
 *   nnnn. is a decimal number.
 * 0xnnnn  is a hexadecimal number.
 * returns ~0 on error.
 */
word
parsen(char **sp)
{
	char *s;
	word dec, oct, hex;
	char c;
	int base;

	s = *sp;
	base = 0;
	dec = oct = hex = 0;
	if(s[0] == '0'){
		s++;
		base = 8;
		if(*s == 'x' || *s == 'X'){
			s++;
			base = 16;
		}
	}
	while(c = *s++){
		if(c >= '0' && c <= '9')
			c -= '0';
		else if(c >= 'A' && c <= 'F')
			c -= 'A' + 10;
		else if(c >= 'a' && c <= 'f')
			c -= 'a' + 10;
		else if(c == '.'){
			/* decimal number, but only if . is at the end */
			if(base <= 10 && *s == '\0'){
				base = 10;
				break;
			}else
				goto fail;
		}else
			break;
		oct = oct*8 + c;
		dec = dec*10 + c;
		hex = hex*16 + c;
		if(c >= 8){
			if(base < 8)
				base = 8;
			else
				goto fail;
		}
		if(c >= 10){
			if(base < 10)
				base = 10;
			else
				goto fail;
		}
		if(c >= 16){
			if(base < 16)
				base = 16;
			else
				goto fail;
		}
	}
	*sp = s-1;
	if(base == 16) return hex;
	if(base == 10) return dec;
	return oct;
fail:
	printf("error: number format\n");
	return ~0;
}

static int
parserange(char *s, word *start, word *end)
{
	*start = *end = ~0;

	*start = parsen(&s);
	if(*start == ~0) return 1;

	if(*s == '\0'){
		*end = *start;
		return 0;
	}else if(*s == '-' || *s == ':'){
		s++;
		*end = parsen(&s);
		if(*end == ~0) return 1;
	}
	if(*s){
		*start = *end = ~0;
		printf("error: address format\n");
		return 1;
	}
	return 0;
}

enum
{
	FmtOct, FmtMnem
};

static void
exam(word addr, int fastmem, int format)
{
	word *p;
	p = getmemref(&apr->membus, addr, fastmem);
	if(p == nil){
		printf("Non existent memory\n");
		return;
	}
	printf("%06lo: ", addr);
	switch(format){
	case FmtOct:
	default:
		printf("%012lo\n", *p);
		break;

	case FmtMnem:
		printf("%s\n", disasm(*p));
		break;
	}
}

static void
dep(word addr, int fastmem, word w)
{
	word *p;
	p = getmemref(&apr->membus, addr, fastmem);
	if(p == nil){
		printf("Non existent memory\n");
		return;
	}
	*p = w;
}

/* Commands */

typedef struct DevDef DevDef;
struct DevDef
{
	char *type;
	Device *(*make)(int argc, char *argv[]);
};
DevDef definitions[] = {
	{ FMEM_IDENT, makefmem },
	{ CMEM_IDENT, makecmem },
	{ APR_IDENT, makeapr },
	{ TTY_IDENT, maketty },
	{ PTR_IDENT, makeptr },
	{ PTP_IDENT, makeptp },
	{ nil, nil }
};

static void
c_mkdev(int argc, char *argv[])
{
	Device *dev;
	DevDef *def;

	if(argc < 3){
		printf("Not enough arguments\n");
		return;
	}
	for(def = definitions; def->type; def++)
		if(strcmp(def->type, argv[2]) == 0){
			dev = def->make(argc-3, argv+3);
			goto found;
		}
	printf("No such device type: %s\n", argv[2]);
	return;
found:
	dev->name = strdup(argv[1]);
	adddevice(dev);
}

static void
c_attach(int argc, char *argv[])
{
	Device *dev;

	if(argc < 3){
		printf("Not enough arguments\n");
		return;
	}
	dev = getdevice(argv[1]);
	if(dev == nil){
		printf("No device: %s\n", argv[1]);
		return;
	}
	if(dev->attach == nil){
		printf("No attach for device type %s\n", dev->type);
		return;
	}
	dev->attach(dev, argv[2]);
}

/* ioconnect dev busdev */
static void
c_ioconnect(int argc, char *argv[])
{
	Device *dev, *busdev;

	if(argc < 3){
		printf("Not enough arguments\n");
		return;
	}
	dev = getdevice(argv[1]);
	if(dev == nil){
		printf("No device: %s\n", argv[1]);
		return;
	}
	busdev = getdevice(argv[2]);
	if(busdev == nil){
		printf("No device: %s\n", argv[2]);
		return;
	}
	if(busdev->type != apr_ident){
		printf("No bus device: %s\n", busdev->type);
		return;
	}
	if(dev->ioconnect == nil){
		printf("No ioconnect for device type %s\n", dev->type);
		return;
	}
	dev->ioconnect(dev, &((Apr*)busdev)->iobus);
}

/* memconnect dev proc busdev addr */
static void
c_memconnect(int argc, char *argv[])
{
	int proc, addr;
	Device *dev, *busdev;

	if(argc < 5){
		printf("Not enough arguments\n");
		return;
	}
	dev = getdevice(argv[1]);
	if(dev == nil){
		printf("No device: %s\n", argv[1]);
		return;
	}
	proc = atoi(argv[2]);
	busdev = getdevice(argv[3]);
	if(busdev == nil){
		printf("No device: %s\n", argv[3]);
		return;
	}
	if(busdev->type != apr_ident){
		printf("No bus device: %s\n", argv[3]);
		return;
	}
	addr = strtol(argv[4], nil, 8);
	attachmem((Mem*)dev, proc, &((Apr*)busdev)->membus, addr);
}

static void
c_ex(int argc, char *argv[])
{
	char *argv0 = nil;
	word start, end;
	int format, fastmem;

	format = FmtOct;
	fastmem = 1;
	ARGBEGIN{
	case 's':	/* shadow mode */
		fastmem = 0;
		break;
	case 'm':
		format = FmtMnem;
		break;
	}ARGEND;
	if(argc < 1)
		return;
	if(parserange(argv[0], &start, &end))
		return;
	for(; start <= end; start++)
		exam(start, fastmem, format);
}

static void
c_dep(int argc, char *argv[])
{
	char *argv0 = nil;
	word start, end;
	word w;
	int fastmem;

	fastmem = 1;
	ARGBEGIN{
	case 's':
		fastmem = 0;
		break;
	}ARGEND;
	if(argc < 2)
		return;
	if(parserange(argv[0], &start, &end))
		return;
	w = parsen(&argv[1]);
	if(w == ~0)
		return;
	for(; start <= end; start++)
		dep(start, fastmem, w);
}

enum
{
	FmtUnk, FmtSav
};

static void
loadsav(FILE *fp)
{
	word iowd;
	word w;
	while(w = readwbak(fp), w != ~0){
		if(w >> 27 == 0254){
			apr->pc = right(w);
			return;
		}
		iowd = w;
		while(left(iowd) != 0){
			iowd += 01000001;
			w = readwbak(fp);
			if(w == ~0)
				return;
			dep(right(iowd), 0, w);
		}
	}
}

static void
c_load(int argc, char *argv[])
{
	char *argv0 = nil;
	FILE *fp;
	int fmt;

	fmt = FmtSav;	// assume SAV for now
	ARGBEGIN{
	case 's':
		fmt = FmtSav;
		break;
	}ARGEND;
	if(argc < 1)
		return;

	fp = fopen(argv[0], "rb");
	if(fp == nil){
		printf("couldn't open file: %s\n", argv[0]);
		return;
	}
	if(fmt == FmtSav)
		loadsav(fp);
	fclose(fp);
}

static void
c_show(int argc, char *argv[])
{
	printf("Memory:\n");
	showmem(&apr->membus);
}

static void
c_quit(int argc, char *argv[])
{
	quit(0);
}

struct {
	char *cmd;
	void (*f)(int, char **);
} cmdtab[] = {
	{ "mkdev", c_mkdev },
	{ "attach", c_attach },
	{ "ioconnect", c_ioconnect },
	{ "memconnect", c_memconnect },
	{ "examine", c_ex },
	{ "deposit", c_dep },
	{ "load", c_load },
	{ "show", c_show },
	{ "quit", c_quit },
	{ "", nil}
};

void
commandline(char *line)
{
	int i, n;
	int nfound;
	size_t l;
	char *cmd;

	lp = line;
	splitops();

	if(numops && ops[0][0] != '#'){
		nfound = 0;
		for(i = 0; cmdtab[i].cmd[0]; i++){
			cmd = cmdtab[i].cmd;
			l = strlen(ops[0]);
			if(strncmp(ops[0], cmd, l) == 0){
				n = i;
				nfound++;
			}
		}
		if(nfound == 0)
			printf("%s: not found\n", ops[0]);
		else if(nfound == 1)
			cmdtab[n].f(numops, ops);
		else{
			printf("Ambiguous command: %s\n", ops[0]);
			for(i = 0; cmdtab[i].cmd[0]; i++){
				cmd = cmdtab[i].cmd;
				l = strlen(ops[0]);
				if(strncmp(ops[0], cmd, l) == 0)
					printf("	%s\n", cmd);
			}
		}
	}
}

void
cli(FILE *in)
{
	Device *dev;
	size_t len;
	char *line;

	apr = nil;
	for(dev = devlist; dev; dev = dev->next)
		if(dev->type == apr_ident){
			apr = (Apr*)dev;
			break;
		}
	for(;;){
		line = nil;
		len = 0;
		if(in == stdin)
			printf("> ");
		if(getline(&line, &len, in) < 0)
			return;
		commandline(line);
		free(line);
	}
}

void*
cmdthread(void *p)
{
	cli(stdin);
	quit(0);
	return nil;
}
