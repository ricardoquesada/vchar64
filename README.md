# VChar64

[![discord](https://img.shields.io/discord/775177861665521725.svg)](https://discord.gg/r5aMn6Cw5q)
[![Build status](https://ci.appveyor.com/api/projects/status/q5euvgygdmqf67oj/branch/master?svg=true)](https://ci.appveyor.com/project/ricardoquesada/vchar64/branch/master)

<img src="https://lh3.googleusercontent.com/-iE0eqQymBDk/Vl9f_NOGrII/AAAAAAABcow/0sRHClMkr4U/s400-Ic42/Screen%252520Shot%2525202015-12-02%252520at%2525201.16.32%252520PM.png">

## About

An editor for the Commodore 64/128

Tailored for my own needs, but feel free to send patches, to open bugs, etc.


## Features

* Char editing:
    * Cut, Copy & Paste
    * Clear
    * Invert
    * Flip Horizontally, Vertically
    * Rotate
    * Shift Up, Down, Left, Right
    * Undo, Redo
* Tile support:
    * From 1x1 to 8x8
    * Custom Distance in chars between tiles
* Map support
* Imports CharPad 1.8 and 2.0 projects
* Imports Koala image files: [Demo Video](https://www.youtube.com/watch?v=wIBTINBCngs)
* Imports VICE snapshot images
* Exports to Assembly, Raw and Prg
* Emulates different palettes: VICE, Pepto, Frodo, etc...
* Two Live Preview modes:
    * [TCP/IP server](https://github.com/ricardoquesada/vchar64/blob/master/server/README.md) support. [Demo Video](https://www.youtube.com/watch?v=yNCK_wZbo40)
    * [xlink](http://henning-bekel.de/xlink/) support. [Demo Video](https://www.youtube.com/watch?v=ZaSR_mxRfmo)
* Keyboard shortcuts for almost all actions

## Roadmap

In no particular order:

* Convert to PETSCII
* Tile: add Color Per Char (currently it supports Color Per Tile or Global)
* Tile: arbitrary composition of tiles
* Character / Tile animation support
* Sprite
   * Sprite editing
   * Sprite animation
* Level editor

## How to compile it

### Install Qt

* Download [Qt Creator Community Edition 5.6](http://www.qt.io/download/) or newer (doesn't work with older versions)

### Command Line

```
$ git clone https://github.com/ricardoquesada/vchar64.git
$ cd vchar64
$ mkdir build
$ cd build
$ cmake ..
$ make
```

To update the translations do:

```
# To update the entries in the .ts file
$ cmake --build . --target update_translations

# To trigger the generation of the .qm files manually
$ cmake --build . --target release_translations
```

### Using Qt Creator

* Open `vchar64.pro` file with Qt Creator
* Configure the project for "Desktop"
* Build & Run

## Supported platforms

* Linux (tested on Ubuntu 16.04)
* Mac (tested on v10.11)
* Windows (tested on Windows XP and 10)

## Download binaries

* [vchar64-v0.2.4 for win32](https://github.com/ricardoquesada/vchar64/releases/download/0.2.4/vchar64-0.2.4.win32.zip)
* [vchar64-v0.2.4 for mac](https://github.com/ricardoquesada/vchar64/releases/download/0.2.4/vchar64-0.2.4.mac.dmg)
* [vchar64-v0.2.4 for OpenPandora](https://github.com/ricardoquesada/vchar64/releases/download/0.2.4/vchar64-0.2.4.pnd)

## License

* [Apache v2](http://www.apache.org/licenses/LICENSE-2.0)

