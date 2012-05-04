DIRS=ppu spu
include ${CELL_TOP}/buildutils/make.footer

run:
	./ppu/ppu_tema4 input/rainbow_shuffled.ppm
