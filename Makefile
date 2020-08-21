zbc_write : custom_write_zone.o
	gcc -L/usr/include/libzbc -o zbc_write custom_write_zone.o -lzbc

custom_write_zone.o : custom_write_zone.c
	gcc -c -o custom_write_zone.o custom_write_zone.c

clean : 
	rm *.o zbc_write
