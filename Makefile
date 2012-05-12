DIRS=ppu spu
include ${CELL_TOP}/buildutils/make.footer

run:
	./ppu/ppu_tema4 input/aquarium_shuffled.ppm 180 100 100
