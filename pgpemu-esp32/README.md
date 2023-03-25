# Pokemon Go Plus Emulator for ESP32

## Hardware

Tested with:

- ESP32-WROOM-32 (AZDelivery)

## Build

You need ESP-IDF. I'm using v4.4 (stable) installed using the VSCode extension.
To install it use the [Get Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html).

Open the folder `pgpemu-esp32` in VSCode. Run "ESP-IDF Build, Flash and Monitor". That's it!

### Development

From VSCode's Command Palette run "ESP-IDF: Add vscode configuration folder" [as described here](https://github.com/espressif/vscode-esp-idf-extension/blob/master/docs/C_CPP_CONFIGURATION.md) to get working C/C++ IntelliSense.

## Credits

- <https://github.com/yohanes/pgpemu> - Original PGPEMU implementation

## References

- <https://www.reddit.com/r/pokemongodev/comments/5ovj04/pokemon_go_plus_reverse_engineering_write_up/>
- <https://www.reddit.com/r/pokemongodev/comments/8544ol/my_1_gotcha_is_connecting_and_spinning_catching_2/>
- <https://tinyhack.com/2018/11/21/reverse-engineering-pokemon-go-plus/>
- <https://tinyhack.com/2019/05/01/reverse-engineering-pokemon-go-plus-part-2-ota-signature-bypass/>
- <https://coderjesus.com/blog/pgp-suota/> - Most comprehensive write-up
- <https://github.com/Jesus805/Suota-Go-Plus> - Bootloader exploit + secret dumper
