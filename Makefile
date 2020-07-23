
all : i3status-x

i3status-x : main.c main.h
	gcc -pthread -g0 -o i3status-x main.c
	chmod +x i3status-x

debug : main.c main.h
	gcc -pthread -g3 -o i3status-x main.c
	chmod +x i3status-x

clean :
	rm -f i3status-x
