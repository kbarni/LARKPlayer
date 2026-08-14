LARK - Libre Audiobook Reader for Kindle
========================================

<a href='https://ko-fi.com/E1E71RAR86' target='_blank'><img height='36' style='border:0px;height:36px;' src='https://storage.ko-fi.com/cdn/kofi3.png?v=6' border='0' alt='Buy Me a Coffee at ko-fi.com' /></a> <-- All proceeds go to charities.

LARK is a free **M4B/MP3** audiobook reader for jailbroken Kindles. **Only Firmware <5.19 supported right now. Working on a solution for newer devices**

![Screenshot](assets/screenshot.png)

It supports audiobooks in MP3 format and M4B format/AAC encoding. When downloading audiobooks, make sure to choose these formats. AAC is the most common encoding for M4B audiobooks, however this format supports other encodings too.

Features
--------

- Support for MP3 files and AAC encoded M4B audiobooks
- Uses [KinAMP](https://github.com/kbarni/KinAMP)'s audio engine and [FAAD2](https://github.com/knik0/faad2) decoder library
- Optimized for e-book readers: minimum screen refreshes, backlight management
- Listening history
- Book metadata and chapter support
- Bookmark support
- Scriptlet and KUAL launcher included

Installation and usage
-----------------------

Grab the latest relase from the [Releases](https://github.com/kbarni/LARKPlayer/releases) page. Unzip it to the root of your Kindle.

Start it from the library using the provided scriptlet or from KUAL.

![Buttons](assets/buttons.png)

Getting free audio books
------------------------

[Librivox](https://librivox.org/search) has a good list of free public domain audiobooks created by the community. To get their books in M4B format, look for the *Download M4B" link at the bottom of the left column.

You can create your own M4B files from other audio files (like MP3) using the excellent [AudiobookConverter](https://github.com/yermak/AudioBookConverter) or [M4B-Tool](https://github.com/sandreas/m4b-tool/tree/master) converters.


Planned features
----------------

- AudioBookShelf support

Troubleshooting
---------------

If LARK player doesn't start, try to start it from `kterm` and submit an issue with the error you get.

```
cd LARK
./start_lark.sh
```
Building
--------

You must create your CMake toolchain file for your cross-compile toolchain.

You also need to get libfaad2 libraries (`.so`) and include (`.h`) files or [build them from source](https://github.com/knik0/faad2).

```
git clone 
cd LARK
mkdir build
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=armhf-toolchain.cmake
make
```

Changelog
---------

- Version 2.6 - 3/10/2026
    - Low resolution devices support
    - Closing bugs fixed
    - Disappearing database fixed
- Version 2.5 - 1/17/2026
    - MP3 format support
    - PW2 (soft-float) models support
- Version 2.0 - 1/10/2026
    - History stored in SQLite database
    - Chapter selection
    - Bookmark support
    - Bugfixes and cosmetic changes
- Version 1.0 - 1/3/2026
    - Initial release

License
-------

This program is free software: you can redistribute it and/or modify it under the terms of the **GNU General Public License** as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version. This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with this program. If not, see <http://www.gnu.org/licenses/>.
