# 2 stage tape loader for MSX

This is a simple 2 stage tape loader for MSX 8-bit microcomputers.

 - **Stage 1**: loader with an APLIB compressed SC2 image.
 - **Stage 2**: the actual program to load.

It can handle up to 48K (it is a bit less, actually; see limitations).

# License

 - rasm: MIT "expat" licensed
 - apultra: CC0 licensed
 - mkcas: MIT licensed
 - aplib.z80 PD (distributed by apultra)

Check each project source code for further details.

Everything else is PD by Juan J. Martinez (@reidrac).

## Requirements

Common tools:

 - Python 3
 - GNU make (or any `make` tool, probably)
 - gcc (optional if you're going to build rasm and apultra)

Less common tools:

 - rasm assembler: http://www.roudoudou.com/rasm/index.php
 - apultra compressor: https://github.com/emmanuel-marty/apultra
 - mkcas: https://github.com/reidrac/mkcas

All three are open source and a copy is included in here for your convenience.

## Limitations

The loading screen must be an image in SC2 format.

The final program, if 32K:
 - load address: 0x8000
 - exec address: 0x8000
 - max length: 29468 bytes

For 48K:
 - load address: 0x4000
 - exec address: 0x4000
 - max length: 45852 bytes

The stage 2 loader is currently limited to 100 bytes.

## Usage

Add an `screen.sc2` file to the `data` directory and run `make`.

Then build your `cas` file, for example for 32K:

```
tools/mkcas/mkcas.py --name mygame mygame.cas ascii loader/loader.bas
tools/mkcas/mkcas.py --add --name loading --addr 0x8000 --exec 0x8000 mygame.cas binary loader/loader.bin
tools/mkcas/mkcas.py --add --addr 0x8000 mygame.cas custom-header mygame.bin
```

For 48K, use `--addr 0x4000` for your game (last line in the example).

You can try that `cas` file with any MSX emulator.

