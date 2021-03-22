#include "types.h"
#include "x86.h"
#include "defs.h"
#include "kbd.h"

int
kbdgetc(void) // scancode pretvara u ascii
{
	// uint -> uvek pozitivan
	static uint shift; // static cuva vrednost izmedju poziva
	static uchar *charcode[7] = { // pokazivaci na mape
		normalmap, shiftmap, ctlmap, ctlmap, altmap, altmap, altmap
	};
	volatile uint st, data, c;

	st = inb(KBSTATP); // port za citanje statusa tastature
	if((st & KBS_DIB) == 0) // da li zaista ima nesto za citanje (najnizi bit == 1)
		return -1;
	// data sadrzi scan code (1B)
	data = inb(KBDATAP); // 0x60 -> keyboard data port

	if(data == 0xE0){ // ako je esc
		shift |= E0ESC; // setuje 6. bit
		return 0;
	} else if(data & 0x80){ // da li data ima 1 na najjacem bitu (taster otpusten)
		// Key released
		data = (shift & E0ESC ? data : data & 0x7F); // resetuje se data
		shift &= ~(shiftcode[data] | E0ESC); // resetuje bit
		return 0;
	} else if(shift & E0ESC){
		// Last character was an E0 escape; or with 0x80
		data |= 0x80;
		shift &= ~E0ESC;
	}

	// shift + slovo zapravo dva poziva
	// shift = | - | ESC | scrollLock | numLock | capsLock | alt | ctl | shift | -> da li su pomocni tasteri pritisnuti
	shift |= shiftcode[data]; // setujem bitove na poziciji specijalnih tastera ako su pritisnuti (shift, alt, ctl)
	shift ^= togglecode[data]; // za lock-ove

	if(shift & ALT && data != 0x38 ) {
		st = 0; // shift  slovo -> ulazim u if (ne ulazi za shift)
	}

	c = charcode[shift & (ALT | CTL | SHIFT)][data]; // procitam ascii iz neke od mapa
	if(shift & CAPSLOCK){ // ako je capslock ukljucen konvertujem ascii iz malog u veliko ili obrnuto
		if('a' <= c && c <= 'z')
			c += 'A' - 'a';
		else if('A' <= c && c <= 'Z')
			c += 'a' - 'A';
	}

	return c;
}

void
kbdintr(void) // poziva se za prekid tastature
{
	consoleintr(kbdgetc);
}
