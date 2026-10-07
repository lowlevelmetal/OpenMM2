# Room visibility (`city/<map>.cpvs`)

Parser: `src/city/Pvs.{h,cpp}`. Retail coverage: all 25 `.cpvs` files (the
two cities plus editor variants such as `london_16.cpvs`) parse and every row
decompresses within its size.

```
char[4] "PVS0"
u32     n                       = PSDL roomCount + 1
u32     offsets[n - 1]          one per room 1..roomCount-1, plus the end offset;
                                relative to the first byte after the table
u8      data[]                  RLE rows
```

Row *k* (from `offsets[k-1]` to `offsets[k]`) is the visibility of room *k*. After
decompression it is a bit array with **2 bits per room**, LSB first
(`(byte[r / 4] >> (2 * (r % 4))) & 3`). Trailing zero bytes are not stored. A full row is
`ceil(roomCount * 2 / 8)` bytes. Only values 0 and 3 occur, so the second bit's
purpose (perhaps LOD) is unknown.

RLE: control byte `c`. If `c & 0x80`, copy `(c & 0x7F) + 1` literal bytes.
Otherwise repeat the next byte `c` times.

Verification: decoding every row never exceeds `ceil(roomCount/4)` bytes.
Every room sees itself, except 1 room in London and 2 in SF that have no
geometry. The decoder was derived by testing RLE variants until every row
decoded within the row size.

`city/<map>.pvs` (4 MiB, uncompressed) and `.pvshist` are editor files and are
not used.
