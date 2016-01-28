all:
	cl65 -g -d -Ln loader.sym -u __EXEHDR__ -t c64 -C c64-asm.cfg -o loader.prg main.s

run: all
	x64 -moncommands loader.sym loader.prg

clean:
	rm *.o *.prg *.sym
