#ifndef MAIN_H
#define MAIN_H

#undef __cplusplus
#define BUFFER_SIZE 1024 * 1024


int main(int, char**);
void threadFunc(void*);
int prependRate(char*, int);

#endif
