del *.o /s
del *.d /s
del *.dol
del *.elf
make --jobs=9
elf2dol cubemusic.elf cubemusic.dol
d0lz cubemusic.dol cubemusic-lz.dol -m
pause