all: loader.bin

export PATH:=../bin:$(PATH)

screen2.bin: ../data/screen.sc2
	apultra $< $@

stage2.bin: stage2.z80
	rasm $< -ob $@

loader.bin: loader.z80 screen2.bin stage2.bin
	rasm $< -ob $@

clean:
	rm -f *.bin

