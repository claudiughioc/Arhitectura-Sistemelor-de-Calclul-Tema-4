DIRS=ppu spu
include ${CELL_TOP}/buildutils/make.footer

run:
	./ppu/ppu_tema4 input/satellite_shuffled.ppm 224 150 150
