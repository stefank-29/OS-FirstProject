// Console input and output.
// Input is from the keyboard or serial port.
// Output is written to the screen and serial port.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "traps.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"

static void consputc(int);

static int panicked = 0;

static struct {
	struct spinlock lock;
	int locking;
} cons;

static void
printint(int xx, int base, int sign)
{
	static char digits[] = "0123456789abcdef";
	char buf[16];
	int i;
	uint x;

	if(sign && (sign = xx < 0))
		x = -xx;
	else
		x = xx;

	i = 0;
	do{
		buf[i++] = digits[x % base];
	}while((x /= base) != 0);

	if(sign)
		buf[i++] = '-';

	while(--i >= 0)
		consputc(buf[i]);
}

// Print to the console. only understands %d, %x, %p, %s.
void
cprintf(char *fmt, ...)
{
	int i, c, locking;
	uint *argp;
	char *s;

	locking = cons.locking;
	if(locking)
		acquire(&cons.lock);

	if (fmt == 0)
		panic("null fmt");

	argp = (uint*)(void*)(&fmt + 1);
	for(i = 0; (c = fmt[i] & 0xff) != 0; i++){
		if(c != '%'){
			consputc(c);
			continue;
		}
		c = fmt[++i] & 0xff;
		if(c == 0)
			break;
		switch(c){
		case 'd':
			printint(*argp++, 10, 1);
			break;
		case 'x':
		case 'p':
			printint(*argp++, 16, 0);
			break;
		case 's':
			if((s = (char*)*argp++) == 0)
				s = "(null)";
			for(; *s; s++)
				consputc(*s);
			break;
		case '%':
			consputc('%');
			break;
		default:
			// Print unknown % sequence to draw attention.
			consputc('%');
			consputc(c);
			break;
		}
	}

	if(locking)
		release(&cons.lock);
}

void
panic(char *s)
{
	int i;
	uint pcs[10];

	cli();
	cons.locking = 0;
	// use lapiccpunum so that we can call panic from mycpu()
	cprintf("lapicid %d: panic: ", lapicid());
	cprintf(s);
	cprintf("\n");
	getcallerpcs(&s, pcs);
	for(i=0; i<10; i++)
		cprintf(" %p", pcs[i]);
	panicked = 1; // freeze other CPU
	for(;;)
		;
}

#define BACKSPACE 0x100
#define CRTPORT 0x3d4
static ushort *crt = (ushort*)P2V(0xb8000);  // CGA memory (pokazivac na short(niz))


//**********************************
static void
cgaputc(int c) // ascii karakter (ispisuje char na ekran)
{
	int pos;

	// Cursor position: col + 80*row.
	outb(CRTPORT, 14); // (14 == 0x0E)
	pos = inb(CRTPORT+1) << 8;
	outb(CRTPORT, 15); // (15 == 0x0F)
	pos |= inb(CRTPORT+1); // pozicija(dvobajt)

	if(c == '\n')
		pos += 80 - pos%80; // pocetak sledeceg reda
	else if(c == BACKSPACE){
		if(pos > 0) --pos;
	} else
		crt[pos++] = (c&0xff) | 0x0700;  // white on black

	// if(c == '#'){
	// 	  crt[pos++] = (c&0xff) | 0xC400;
	// }

	if(pos < 0 || pos > 25*80)
			panic("pos under/overflow");

	if((pos/80) >= 24){  // Scroll up.  // pos/80 -> index trenutnog reda
		memmove(crt, crt+80, sizeof(crt[0])*23*80); // (to, from, moveBytes) // pomera od 2 do 24 reda na gore
 		pos -= 80; // vrati kursor gore
		memset(crt+pos, 0, sizeof(crt[0])*(24*80 - pos)); // (adresa, value, numBytes) // od kursora do kraja reda popuni nulama
	}
	// upisem poziciju kursora u crt kontoler
	outb(CRTPORT, 14);
	outb(CRTPORT+1, pos>>8); // saljem visi bajt
	outb(CRTPORT, 15);
	outb(CRTPORT+1, pos); // nizi bajt
	crt[pos] = ' ' | 0x0700;
}

void
consputc(int c)
{
	if(panicked){
		cli();
		for(;;)
			;
	}

	if(c == BACKSPACE){
		uartputc('\b'); uartputc(' '); uartputc('\b');
	} else
		uartputc(c);
	cgaputc(c);
}

//******************************************
static char table[10][23] = {
	"/---<FG>--- ---<BG>---\\",
	"|Black     |Black     |",
	"|Blue      |Blue      |",
	"|Green     |Green     |",
	"|Aqua      |Aqua      |",
	"|Red       |Red       |",
	"|Purple    |Purple    |",
	"|Yellow    |Yellow    |",
	"|White     |White     |",
	"\\---------------------/",

};

volatile static char *colors[8][2] = {
	{"Black     ", "Black     "},
	{"Blue      ", "Blue      "},
	{"Green     ", "Green     "},
	{"Aqua      ", "Aqua      "},
	{"Red       ", "Red       "},
	{"Purple    ", "Purple    "},
	{"Yellow    ", "Yellow    "},
	{"White     ", "White     "},
};

volatile static ushort colorsHex[8][2] = {
	{0x0000, 0x0000},
	{0x0100, 0x1000},
	{0x0200, 0x2000},
	{0x0300, 0x3000},
	{0x0400, 0x4000},
	{0x0500, 0x5000},
	{0x0600, 0x6000},
	{0x0700, 0x7000},

};

volatile static ushort hiddenConsole [10][23];

static int tableX = 0, tableY = 0;

static ushort currColor = 0x0700;


// TODO kad se nastavi kucanje u toj boji da bude

void
openTable(){
	int pos = 57;

	int x = 0, y = 0;

	for(int i = 0; i < 10; i++){
		for(int j = 57 + i*80; j < 80 + i*80; j++){
			hiddenConsole[x][y++] = crt[j];
		}
		x++;
		y = 0;
	}

	x = 0;
	y = 0;

	for(int i = 0; i < 10; i++){
		for(int j = 57 + i*80; j < 80 + i*80; j++){
			crt[j] = (table[x][y++] & 0xff) | 0x0f00; // belo na crno
		}
		x++;
		y = 0;
	}

	for(int i = (tableX+1)*80+58 + tableY*11, j=0; i <  (tableX+1)*80+68 + tableY*11; i++){
		crt[i] = (colors[tableX][tableY][j++] & 0xff) | 0xf000; // blackFG active
	}
}

void
closeTable(){
	// iz hiddenConsole ispisati u crt
	int x = 0, y = 0;
	static uint mask = 0xff00;
	for(int i = 0; i < 10; i++){
		for(int j = 57 + i*80; j < 80 + i*80; j++){
			crt[j] = hiddenConsole[x][y++];
			crt[j] =  (crt[j] & ~mask) | (currColor & mask); // set currColor
		}
		x++;
		y = 0;
	}

}

void
renderTable(){
	int x = 0, y = 0;

	for(int i = 0; i < 10; i++){
		for(int j = 57 + i*80; j < 80 + i*80; j++){
			crt[j] = (table[x][y++] & 0xff) | 0x0f00; // belo na crno
		}
		x++;
		y = 0;
	}

	for(int i = (tableX+1)*80+58 + tableY*11, j=0; i <  (tableX+1)*80+68 + tableY*11; i++){
		crt[i] = (colors[tableX][tableY][j++] & 0xff) | 0xf000; // blackFG active
	}
}



void
setCurrColor(int brighter){
	static uint mask;
	if (tableY == 0){ // slova
		mask = 0x0f00;
		currColor = (currColor & ~mask) | ((brighter ? colorsHex[tableX][tableY] | 0x0800 : colorsHex[tableX][tableY]) & mask);
	} else if (tableY == 1) { // pozadina
		mask = 0xf000;
		currColor = (currColor & ~mask) | ((brighter ? colorsHex[tableX][tableY] | 0x8000 : colorsHex[tableX][tableY]) & mask);
	}
}

void
renderConsole(int brighter){
	setCurrColor(brighter);
	static uint mask = 0xff00;
	for (int i = 0; i < 25; i++){
		for(int j = 0+ i*80; j < 80 + i*80; j++){
			if(!(i < 10 && j > i*80 + 56)){
				crt[j] =  (crt[j] & ~mask) | (currColor & mask); // set currColor
			}
		}
	}
}

#define INPUT_BUF 128
struct {
	char buf[INPUT_BUF];
	uint r;  // Read index
	uint w;  // Write index
	uint e;  // Edit index
} input;

#define C(x)  ((x)-'@')  // Control-x

#define A(x) ((x) + 'Z') // Alt-x

void
consoleintr(int (*getc)(void)) // upis u bafer (poziva se na klik dugmeta)
{
	int c, doprocdump = 0;
	static int alt_flags[3] = {0, 0, 0};
	static int table_open = 0;


	acquire(&cons.lock);
	while((c = getc()) >= 0){ // getc pokazivac na kbdgetc()


		if(table_open){
			switch(c){
				case 'w':
					tableX = (tableX==0 ? 7 : tableX-1);
					renderTable();
					break;
				case 's':
					tableX = (tableX + 1) % 8;
					renderTable();
					break;
				case 'a':
				case 'd':
					tableY = (tableY + 1) % 2;
					renderTable();
					break;
				case 'e':
					renderConsole(0);
					break;
				case 'r':
					renderConsole(1);
					break;
				default:
					break;
			}


		}

		if(c == A('A')){
			// consputc('a');
			continue;
		}else if((c == A('C') || c == A('O') || c==A('L'))){
			// consputc('a');
			switch(c){
				case A('C'):
					alt_flags[0] = 1;
					alt_flags[1] = 0;
					alt_flags[2] = 0;
					// consputc('a');
					break;
				case A('O'):
					if(alt_flags[0]==1 && alt_flags[1]==0){
						alt_flags[1] = 1;
					}else{
						alt_flags[0] = 0;
						alt_flags[1] = 0;
						alt_flags[2] = 0;
					}
					// consputc('a');
					break;
				case A('L'):
					if(alt_flags[0]==1 && alt_flags[1]==1){
						alt_flags[2] = 1;
					}else{
						alt_flags[0] = 0;
						alt_flags[1] = 0;
						alt_flags[2] = 0;
					}
					// consputc('a');
					break;
			}
			if(alt_flags[0]==1 && alt_flags[1]==1 && alt_flags[2]==1){
				// reset flag-ova
				alt_flags[0] = 0;
				alt_flags[1] = 0;
				alt_flags[2] = 0;
				// otvoriti tabelu ili zatvoriti
				table_open = !table_open;
				if(table_open){
					openTable();
				}else{
					closeTable();
				}
			}
			continue;
		}

		// resetovati flag
		if(c != 0){
			alt_flags[0] = 0;
			alt_flags[1] = 0;
			alt_flags[2] = 0;
		}

		switch(c){
		case C('P'):  // Process listing. // ctrl + p
			// procdump() locks cons.lock indirectly; invoke later
			doprocdump = 1;
			break;
		case C('U'):  // Kill line.
			while(input.e != input.w &&
			      input.buf[(input.e-1) % INPUT_BUF] != '\n'){
				input.e--; // vraca e do w
				consputc(BACKSPACE); // brise karakter na konzoli
			}
			break;
		case C('H'): case '\x7f':  // Backspace
			if(input.e != input.w){ // samo jednom vrati
				input.e--;
				consputc(BACKSPACE);
			}
			break;
		// case C('E'):
		// 	ctrl_e_set = !ctrl_e_set;
		// 	break;
		//! mozda i ne treba ovde
		case A('C'):
		case A('L'):
		case A('O'):

			break;
		default:
			if(c != 0 && input.e-input.r < INPUT_BUF && !table_open){ // nije otvorena tabela
				c = (c == '\r') ? '\n' : c; // \r u \n
				input.buf[input.e++ % INPUT_BUF] = c;
				// if(!ctrl_e_set)
				consputc(c); // echo (ispisuje pritisnute tastere na konzoli)
				if(c == '\n' || c == C('D') || input.e == input.r+INPUT_BUF){ // enter ili eof(crtl + D) ili popunjen bafer
					input.w = input.e;
					wakeup(&input.r); // budi proces koji sleep-ujem u while-u
				}
			}
			break;
		}
	}
	release(&cons.lock);
	if(doprocdump) {
		procdump();  // now call procdump() wo. cons.lock held
	}
}

int
consoleread(struct inode *ip, char *dst, int n) // dst -> niz u korisnickom prostoru gde upisem podatke | n -> duzina niza
{
	uint target;
	int c;

	iunlock(ip);
	target = n; // int n
	acquire(&cons.lock); // zakljucavanje kod vise procesa
	while(n > 0){ //* zavrsi se ako napunim dst ili naidjem na enter
		while(input.r == input.w){ // jednaki su ako nemam sta da citam (na enter w skoci na kraj tj. na e)
			if(myproc()->killed){
				release(&cons.lock);
				ilock(ip);
				return -1;
			}
			sleep(&input.r, &cons.lock); // blokira trenutni proces (prosledim broj koji je adresa od input.r)
		}
		// nakon entera nastavlja se proces
		c = input.buf[input.r++ % INPUT_BUF]; // citam char po char iz bafera
		if(c == C('D')){  // EOF
			if(n < target){
				// Save ^D for next time, to make sure
				// caller gets a 0-byte result.
				// ako sam procitao eof svaki naredni put dobijam eof
				input.r--;
			}
			break;
		}
		*dst++ = c; // upisivanje u niz iz korisnickog prostora
		--n;
		if(c == '\n')
			break;
	}
	//* r ne mora da bude == w na kraju f-je (ako je korisnicki bafer manji od input bafera) onda ponovo pozove read pa se citanje nastavlja od r indeksa
	release(&cons.lock);
	ilock(ip);

	return target - n; // koliko je karaktera procitano (da li da ponovo zove read)
}

int
consolewrite(struct inode *ip, char *buf, int n)
{
	int i;

	iunlock(ip);
	acquire(&cons.lock);
	for(i = 0; i < n; i++)
		consputc(buf[i] & 0xff);
	release(&cons.lock);
	ilock(ip);

	return n;
}

void
consoleinit(void)
{
	initlock(&cons.lock, "console");
	// setuju se pokazivaci na consolewrite i consolread
	devsw[CONSOLE].write = consolewrite; // dewsw[1] = funkcija (za novi uredjaj dodati u niz npr devsw[PRINTER] = printerwrite)
	devsw[CONSOLE].read = consoleread;
	cons.locking = 1;

	ioapicenable(IRQ_KBD, 0);
}

