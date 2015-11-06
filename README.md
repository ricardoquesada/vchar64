# VChar64

<img src="https://lh3.googleusercontent.com/-ek5qP9Qnq7w/VjqZJIf3PbI/AAAAAAABUAA/MumlfIxHGk0/s640-Ic42/Screen%252520Shot%2525202015-11-04%252520at%2525203.42.49%252520PM.png" width=640>
[![Build Status](https://api.travis-ci.org/ricardoquesada/vchar64.svg)]


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
* Emulates different palettes: VICE, Pepto, Frodo, etc...
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
* Windows (tested on Windows XP, 8.1 and 10)

## Download binaries

* [vchar64-v0.0.8 for win32](https://www.dropbox.com/s/i0fni0xzy6cp0ef/vchar64-0.0.8.zip?dl=1)
* [vchar64-v0.0.8 for mac](https://www.dropbox.com/s/nkeporn3nox6x4n/vchar64-0.0.8.dmg?dl=1)

## License

* [Apache v2](http://www.apache.org/licenses/LICENSE-2.0)
