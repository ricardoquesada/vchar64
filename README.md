# VChar64

<img src="https://lh6.googleusercontent.com/-Oi4KQjCYhKY/VNpNGmcVlnI/AAAAAAABQc8/Lp3oHhWBKso/s640/Screen%2520Shot%25202015-02-10%2520at%25209.18.16%2520AM.png" width=640>

## About

An editor for the Commodore 64/128

Tailored for my own needs, but feel free to send patches, to open bugs, etc.

## Features

* Char editing: 
    * Clear, Copy & Paste
    * Invert
    * Flip Horizontally, Vertically
    * Rotate
    * Shift Up, Down, Left, Right
    * Undo, Redo
* Tile support:
    * From 1x1 to 8x8
    * Custom Distance in chars between tiles
* Imports CharPad 1.8 and 2.0 projects
* Exports to Raw and Prg
* [xlink](http://henning-bekel.de/xlink/) support. Video showing xlink in action: https://www.youtube.com/watch?v=ZaSR_mxRfmo

## Roadmap

In no particular order:

* MDI: copy & paste chars from one document to another one
* Images support
   * Import images and convert them to charset + color
* Tilemap editor (similar to CharPad but with Tiled look & feel)
   * Tile free
   * Map + tile editing
   * Character / Tile animation support
   * Add mini-character / mini-tile view
* Color palettes
   * PAL, NTSC, VICE, Monochrome
* Sprite
   * Sprite editing
   * Sprite animation
* Level editor

## How to compile it

* Clone the repo:

```
$ git clone https://github.com/ricardoquesada/vchar64.git
```

* Download [Qt Creator Community Edition 5.3](http://www.qt.io/download/) or newer (might work on older versions but not tested)
* Open `vchar64.pro` file with Qt Creator
* Configure the project for "Desktop"
* Build & Run

## Supported platforms

* Linux (tested on Ubuntu 14.10)
* Mac (tested on v10.10)
* Windows (tested on Windows 8.1)

## Download binaries

No binaries available yet, so:

* Just clone the github repository
* Or download the source code from [here](https://github.com/ricardoquesada/vchar64/releases)

## License

* [Apache v2](http://www.apache.org/licenses/LICENSE-2.0)
