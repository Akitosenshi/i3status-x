
all : i3status-x

i3status-x : main.c main.h
	gcc -pthread -O2 -Wno-unused-result -Wno-discarded-qualifiers -o build/i3status-x main.c
	chmod +x build/i3status-x

debug : main.c main.h
	gcc -pthread -g3 -Wno-unused-result -Wno-discarded-qualifiers -o build/i3status-x main.c
	chmod +x build/i3status-x

clean :
	rm -f i3status-x
