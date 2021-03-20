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
{          // getc pokazivac na kbdgetc()
	int c, doprocdump = 0;
	// static int ctrl_e_set = 0;

	acquire(&cons.lock);
	while((c = getc()) >= 0){
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
		case A('C'):
		case A('L'):
		case A('O'):
			consputc('l');
			break;
		default:
			if(c != 0 && input.e-input.r < INPUT_BUF){ // da l' ima mesta u baferu (< 128)
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

