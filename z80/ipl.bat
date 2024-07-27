asl /L ipl_8080.asm
p2bin ipl_8080.p ipl_8080.bin
python bin2hexsrc.py ipl_8080.bin > ipl_8080.inc

asl /L bios_8080.asm
p2bin bios_8080.p bios_8080.bin
python bin2hexsrc.py bios_8080.bin > bios_8080.inc