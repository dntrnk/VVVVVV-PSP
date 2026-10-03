How to Build
------------

Install [PSPSDK](https://pspdev.github.io/installation.html) and make sure `$PSPDEV` is set up in your environment.

Run `git clone https://github.com/dntrnk/VVVVVV-PSP`

After cloning, run `git submodule update --init` inside of `VVVVVV-PSP` folder to set submodules up.
You can also use this command whenever the submodules need to be updated.

To build the Make and Play edition of the game, uncomment `#define MAKEANDPLAY`
in `MakeAndPlay.h`.

Open `desktop_version` folder in terminal and run `make clean && make`

Including data.zip and music
------------
This port uses a modified data.zip and a converted version of the original
soundtrack (`.at3` files), neither of which is included in this repository.
They cannot be redistributed without permission from the original authors.

There is no information about data.zip or music yet - stay tuned for updates.