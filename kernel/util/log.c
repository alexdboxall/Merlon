
#include <common.h>

static void IntToStr(uint32_t i, char* output, int base)
{
	const char* digits = "0123456789ABCDEF";

    /*
    * Work out where the end of the string is (this is based on the number).
    * Using the do...while ensures that we always get at least one digit 
    * (i.e. ensures a 0 is printed if the input was 0).
    */
	uint32_t shifter = i;
	do {
		++output;
		shifter /= base;
	} while (shifter);

    /* Put in the null terminator. */
	*output = '\0';

    /*
    * Now fill in the digits back-to-front.
    */
	do {
		*--output = digits[i % base];
		i /= base;
	} while (i);
}

static void outb(uint16_t port, uint8_t value)
{
	asm volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static uint8_t inb(uint16_t port)
{
	uint8_t value;
	asm volatile ("inb %1, %0"
		: "=a"(value)
		: "Nd"(port));
	return value;
}

static void logcnv(char c)
{	
	while ((inb(0x3F8 + 5) & 0x20) == 0) {
		;
	}
	outb(0x3F8, c);
}

static void logsnv(char* a)
{
	while (*a) logcnv(*a++);
}

static void log_intnv(uint32_t i, int base)
{
	char str[12];
    IntToStr(i, str, base);
	logsnv(str);
}

static void LogWriteSerialVa(const char* format, va_list list) {
	if (format == NULL) {
		format = "NULL";
	}

	int i = 0;

	while (format[i]) {
		if (format[i] == '%') {
			switch (format[++i]) {
			case '%': 
				logcnv('%'); break;
			case 'c':
				logcnv(va_arg(list, int)); break;
			case 's': 
				logsnv(va_arg(list, char*)); break;
			case 'd': 
				log_intnv(va_arg(list, signed), 10); break;
			case 'x':
			case 'X': 
				log_intnv(va_arg(list, unsigned), 16); break;
			case 'l':
			case 'L': 
				log_intnv(va_arg(list, unsigned long long), 16); break;
			case 'u':
				log_intnv(va_arg(list, unsigned), 10); break;
			default: 
				logcnv('%'); 
				logcnv(format[i]);
				break;
			}
		} else {
			logcnv(format[i]);
		}
		i++;
	}
}

void LogWriteSerial(const char* format, ...)
{
	va_list list;
	va_start(list, format);
	LogWriteSerialVa(format, list);
	va_end(list);
}

void LogDeveloperWarning(char* format, ...) {
	va_list list;
	va_start(list, format);
	LogWriteSerial(">>> KERNEL DEVELOPER WARNING:\n    ");
	LogWriteSerialVa(format, list);
	va_end(list);
}