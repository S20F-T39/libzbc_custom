all : zbc_read

zbc_read : custom_read_zone.o
	gcc -L/usr/include/libzbc -o zbc_read custom_read_zone.o -lzbc

custom_read_zone.o : custom_read_zone.c
	gcc -c -o custom_read_zone.o custom_read_zone.c

clean :
	rm *.o zbc_read

