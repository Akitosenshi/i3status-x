#ifndef ERROR_H
#define ERROR_H

#include <stdio.h>

//don't use this, use PRINT_ERROR() macro instead
inline void print_error(char* message, char* filename, int line, int usePerror){
	fprintf(stderr, "File %s, at line %i:\n", filename, line);
	if(usePerror){
		perror(message);
	}else{
		fprintf(stderr, "%s", message);
	}
}

#define PRINT_ERROR(MESSAGE) print_error(MESSAGE, __FILE__, __LINE__, 1)
#define PRINT_ERROR_NO_PERROR(MESSAGE) print_error(MESSAGE, __FILE__, __LINE__, 0)

#endif
