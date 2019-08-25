# steg-png
`steg-png` is a simple C-based commandline application that can be used to embed string data in Portable Network Graphics (PNG) images. This is known as steganography, and is useful for concealing hidden messages in otherwise unassuming files.

![](screenshot.png)

## How it works
Portable Network Graphics (PNG) images have a pretty simple format. They are composed of an 8-byte file header and a number of data chunks.

Data chunks within the file are split into two groups, known as `critical` and `ancillary`. Critical chunks are necessary for the decoder to be able to decode and display the image. This includes information like color data and dimensions. Ancillary chunks are optional chunks of data that decoders will ignore if unknown to them. There are a number of standard ancillary chunks, but steg-png uses a custom "stEG" type chunk.

Chunks have the following byte structure:
| Length  | Chunk Type | Chunk Data   | CRC     |
|---------|------------|--------------|---------|
| 4 bytes | 4 bytes    | Length bytes | 4 bytes |

steg-png builds a custom chunk from the message given, then creates a copy of the input file but with the custom chunk inserted before the IEND chunk.

You can read more on the specifics of PNG in [informational RFC 2083](https://tools.ietf.org/html/rfc2083);

## Building and Installing
By default, steg-png is installed into your user's ~/bin directory. To install, from the project root run:
```
$ mkdir build
$ cd build
$ cmake ..
$ make install

$ ~/bin/steg-png --help
```

For a global install, run from the project root:
```
$ mkdir build
$ cd build
$ cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
$ make install

$ steg-png --help
```

## Usage
Using the tool is simple.a

```
usage: steg-png [--embed] (--message | -m) <message> <file>
   or: steg-png --extract <file>

    --embed             embed a message in a png image
    -m, --message <message>
                        specify the message to embed in the png image
    --extract           extract a message in a png image
    -h, --help          show help and exit
```

## License
This project is free software and is available under the [MIT License](https://opensource.org/licenses/MIT).
