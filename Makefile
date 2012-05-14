DIRS=ppu spu
include ${CELL_TOP}/buildutils/make.footer

run1:
	./ppu/ppu_tema4 input/rainbow_shuffled.ppm 32 80 80

run2:
	./ppu/ppu_tema4 input/aquarium_shuffled.ppm 180 100 100

run3:
	./ppu/ppu_tema4 input/satellite_shuffled.ppm 224 150 150
