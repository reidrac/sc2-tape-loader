TOOLS=bin/rasm bin/apultra

all: loader/loader.bin

loader/loader.bin: $(TOOLS) loader/loader.z80 loader/stage2.z80 data/screen.sc2
	make -C loader

bin/rasm:
	make -C tools/rasm
	cp tools/rasm/rasm $@

bin/apultra:
	make -C tools/apultra
	cp tools/apultra/apultra $@

clean:
	rm -f $(TOOLS)
	make -C tools/rasm clean
	make -C tools/apultra clean
	make -C loader clean

