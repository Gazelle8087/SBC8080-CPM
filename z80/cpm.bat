asl /L CPM22_CCP_BDOS.asm
p2bin CPM22_CCP_BDOS.p CPM22_CCP_BDOS.bin
python bin2hexsrc.py CPM22_CCP_BDOS.bin > CPM22_CCP_BDOS.inc
