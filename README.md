# Gameboy DMG Emulator

This is an emulator written in C just for fun during summer, mainly as a way to learn more about hardware emulation.

It’s not fully functional yet — it’s still missing some core components such as serial data transfer, some memory bank controller (only MBC1 works), 
and audio emulation. Furthermore, there are still some rendering issues in smaller games like Alleyway.

## Build
Make sure to have cmake on your machine. By now other libraries ar compiled from source code inside `external` folder, so you don't need anything else.

```bash
# Create build directory
mkdir build

cd build
cmake ..
```
If you are on windows open the VS solution generated, compile it and run.

On any unix-like machine you can do `make`.

## Usage
In order to play some games with this emulator you have to download you game of choice from the internet (.gb file)
and then you can run the emulator with the following command from the build directory.

```bash 
./gameboy <PATH_TO_ROM>
```

You will see some infos about the cartridge that you are using on the terminal.

**Warning** If you read something different from `ROM ONLY` or `MBC1` be careful because the actual emulator implementation doesn't support
all the feature required in order to run correctly that game.

## Images
[!Logo](.images/logo.png)
[!Kirby](.images/kirby.png)
[!DrMario](.images/DrMario.png)


## Contribution
If you want to contribute to this project, you are welcome!
