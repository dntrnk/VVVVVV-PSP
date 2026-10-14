How to Build
------------
Install [PSPSDK](https://pspdev.github.io/installation.html) and make sure `$PSPDEV` is set up in your environment.

Run `git clone https://github.com/dntrnk/VVVVVV-PSP`

After cloning, run `git submodule update --init` inside of `VVVVVV-PSP` folder to set submodules up.
You can also use this command whenever the submodules need to be updated.

To build the Make and Play edition of the game, uncomment `#define MAKEANDPLAY`
in `MakeAndPlay.h`.

Open `desktop_version` folder in terminal and run `make clean && make`

Including data.zip, music, fonts and lang
------------
The data.zip file and the music, fonts and lang folders are all included in the ready-to-use build available on the [Releases](https://github.com/dntrnk/VVVVVV-PSP/releases) page.
Put them next to EBOOT.PBP and the game should run.