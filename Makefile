all: mlbinstall

mlb.bin: mlb.asm
	nasm -o $@ $<

mlb_bin.h: mlb.bin
	xxd -i $< $@

mlbinstall: mlbinstall.c mlb_bin.h
	gcc -std=c99 -pedantic -Wall -o $@ $<

clean:
	rm mlbinstall mlb.bin mlb_bin.h
