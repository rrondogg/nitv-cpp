# nitv-cpp

Read a NITF file, write a PNG of its image data.

Inspired by [nitf-viz](https://github.com/holmesv3/nitf-viz) but I don't know Rust that well.

For now its just an image viewer but maybe this can be branched out further (better image viewer? maybe throw in a processor here? something cool?)

Loosely the flow is:

parse the NITF headers
decide what kind of picture this is
memory-map each image segment's pixel data
address it as one logical image
resample to the requested output size
write a PNG

## Not supported

Compression of any kind, block masks (`IC` = `NM`/`M*`), pixel sizes other
than 1 byte for MONO / 3 for RGB / 8 for SICD, `IMODE` other than `P` for RGB,
blocked SICD (`IMODE=B`), images split along columns rather than rows, and
SICD stored as `AMP8I_PHS8I`.
