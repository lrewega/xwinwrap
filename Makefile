all: all64 all32

all64:
	gcc -Wall -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -Wredundant-decls -lX11 -lXext -lXrender xwinwrap.c -o xwinwrap
	-mkdir x86_64
	mv ./xwinwrap ./x86_64

all32:
	gcc -m32 -Wall -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -Wredundant-decls -lX11 -lXext -lXrender xwinwrap.c -o xwinwrap
	-mkdir i386
	mv ./xwinwrap ./i386

install64:
	cp x86_64/xwinwrap /usr/bin

install32:
	cp i386/xwinwrap /usr/bin

clean:
	-rm -rf x86_64/ i386/


