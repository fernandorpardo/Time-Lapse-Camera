/**************************************************************************************************
 * Time Lapse Camera
 * some support functions
 * 
 **************************************************************************************************
*/
#include <iostream> 	// isdigit
#include <unistd.h> 	// STDIN_FILENO
#include <string.h> 	// strlen
#include <termios.h> 	// tcgetattr(
#include <fcntl.h>  	// fcntl
#include "glib.h"

bool isNumber(char *s)
{
	unsigned int i=0;
	if(s[i]!='\0' && (s[i]=='-' || s[i]=='+')) i++;
	for (; i<strlen(s) && isdigit(s[i]); i++);
	return !(i<strlen(s));
}
double CPUtemperature(void)
{
	double T;
	FILE *temperatureFile = fopen ("/sys/class/thermal/thermal_zone0/temp", "r");
	if (temperatureFile == NULL) return 0;
	fscanf (temperatureFile, "%lf", &T);
	T /= 1000;
	fclose (temperatureFile);
	return T;
}
/* -------------------------------------------  KEYBOARD  ---------------------------------------------- */
struct termios term_flags;	// keep original values to restore on exit
int term_ctrl;
int termios_init()
{
	/* get the original state */
	tcgetattr(STDIN_FILENO, &term_flags);
	term_ctrl = fcntl(STDIN_FILENO, F_GETFL, 0);
	return 0;
}
int termios_restore()
{
	tcsetattr(STDIN_FILENO, TCSANOW, &term_flags);
	fcntl(STDIN_FILENO, F_SETFL, term_ctrl);
	return 0;
}
int kbhit(void)
{
	struct termios newtio, oldtio;
	int oldf;

    if (tcgetattr(STDIN_FILENO, &oldtio) < 0) /* get the original state */
        return -1;
    newtio = oldtio;
	/* echo off, canonical mode off */
    newtio.c_lflag &= ~(ECHO | ICANON );  
	tcsetattr(STDIN_FILENO, TCSANOW, &newtio);
 	oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
	fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);

	int ch = getchar();
 	if(ch != EOF)
	{
		ungetc(ch, stdin);
		return 1;
	}
 	tcsetattr(STDIN_FILENO, TCSANOW, &oldtio);
	fcntl(STDIN_FILENO, F_SETFL, oldf);
	return 0;
}

/* END OF FILE */
