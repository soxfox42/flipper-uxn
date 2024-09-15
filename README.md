# Flipper Uxn

This repo contains a port of the [SDL Uxn emulator](https://git.sr.ht/~rabbits/uxn) to the [Flipper Zero](https://flipperzero.one/).

I don't intend to develop this further, but feel free to fork it and develop it further.

## Supported Functionality

- Loading ROMs
- Uxn core instructions
- Varvara Screen device
- Varvara Controller device (partial)

## Controller device

The Controller implementation does not support any form of keyboard input, and maps the Flipper Zero's 6 buttons as follows:

| Flipper Zero | Varvara |
| ------------ | ------- |
| Up           | Up      |
| Down         | Down    |
| Left         | Left    |
| Right        | Right   |
| Ok           | A       |
| Back         | B       |

Long pressing Back will immediately exit the emulator.
