
release i3status-expansion : main.c main.h
	gcc -pthread -g0 -o i3status-expansion main.c
	chmod +x i3status-expansion

debug : main.c main.h
	gcc -pthread -g3 -o i3status-expansion main.c
	chmod +x i3status-expansion
