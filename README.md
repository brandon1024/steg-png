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

steg-png builds a custom chunk from the message or file given, then creates a copy of the input file but with the custom chunk inserted before the `IEND` chunk.

Here is a detailed description of the `stEG` chunk format:

| Length  | Chunk Type | Timestamp (unix) | Token | Chunk Data         | CRC     |
|---------|------------|------------------|-------|--------------------|---------|
| 4 bytes | 4 bytes    | 8 bytes          | 0x4c  | (Length - 9) bytes | 4 bytes |

You can read more on the specifics of the PNG format in [informational RFC 2083](https://tools.ietf.org/html/rfc2083).

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
Using the tool is simple.

```
usage: steg-png --embed (-m | --message <message>) [-o | --output <file>] <file>
   or: steg-png --embed (-f | --file <file>) [-o | --output <file>] <file>
   or: steg-png --embed (-h | --help)

    -m, --message <message>
                        specify the message to embed in the png image
    -f, --file <file>   specify a file to embed in the png image
    -o, --output <file>
                        output to a specific file
    -q, --quiet         suppress output to stdout
    -h, --help          show help and exit
```

## License
This project is free software and is available under the [MIT License](https://opensource.org/licenses/MIT).
