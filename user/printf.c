#include "kernel/types.h"
#include "kernel/stat.h"
#include "user.h"

#include <stdarg.h>

static char digits[] = "0123456789ABCDEF";

static void
putc(int fd, char c)
{
	write(fd, &c, 1); // na fd pise niz duzine 1 (salje se 1 karakter - niz karaktera duzine 1)
}

static void
printint(int fd, int xx, int base, int sgn)
{
	char buf[16];
	int i, neg;
	uint x;

	neg = 0;
	if(sgn && xx < 0){
		neg = 1;
		x = -xx;
	} else {
		x = xx;
	}

	i = 0;
	do{
		buf[i++] = digits[x % base];
	}while((x /= base) != 0);
	if(neg)
		buf[i++] = '-';

	while(--i >= 0)
		putc(fd, buf[i]);
}

// Print to the given fd. Only understands %d, %x, %p, %s.
void
vprintf(int fd, const char *fmt, va_list ap)
{
	char *s;
	int c, i, state;

	state = 0;
	for(i = 0; fmt[i]; i++){ // prolazi kroz format
		c = fmt[i] & 0xff;
		if(state == 0){
			if(c == '%'){
				state = '%'; // menja se stanje
			} else {
				putc(fd, c); // putc
			}
		} else if(state == '%'){
			if(c == 'd'){
				printint(fd, va_arg(ap, int), 10, 1);
			} else if(c == 'x' || c == 'p') {
				printint(fd, va_arg(ap, int), 16, 0);
			} else if(c == 's'){
				s = va_arg(ap, char*);
				if(s == 0)
					s = "(null)";
				while(*s != 0){
					putc(fd, *s);
					s++;
				}
			} else if(c == 'c'){
				putc(fd, va_arg(ap, uint));
			} else if(c == '%'){
				putc(fd, c);
			} else {
				// Unknown % sequence.  Print it to draw attention.
				putc(fd, '%');
				putc(fd, c);
			}
			state = 0;
		}
	}
}

void
fprintf(int fd, const char *fmt, ...) // fileDescriptor, format, argumenti
{
	va_list ap;

	va_start(ap, fmt);
	vprintf(fd, fmt, ap);
}

void
printf(const char *fmt, ...) // format, proizvoljno argumenata
{
	va_list ap;

	va_start(ap, fmt); // pristupam kao niz svim argumentima nakon formata
	vprintf(1, fmt, ap); // (1 -> file sa rednim brojem 1 (stdout tj. /dev/console ))
}
