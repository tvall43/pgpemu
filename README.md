# Pokemon Go Plus Emulator for ESP32

Autocatcher for Pokemon Go.

**Note:** This repository doesn't contain the secret blobs needed to make a working emulator for a PGP!
You need to dump these secrets yourself from your own original device (see [References](#references) for some pointers).

## Features

This fork adds some features:

- connect up to 3 devices simultaneously
- parse LED patterns to detect Pokemon/Pokestops/bag full/box full/Pokeballs empty and press button only when needed
- randomized delay for pressing the button and press duration
- PGP secrets are stored in [ESP32 NVS](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_flash.html) instead of being compiled in
- settings menu using serial port
- secrets upload using serial port (see [Upload Secrets](#upload-secrets))
- setting: autocatch/autospin on/off
- setting: periodically turn on WiFi to waste some power so your powerbank doesn't turn off (depends on your powerbank if this works)
- setting: chose between PGP secrets if you have dumps from multiple PGPs
- store user settings in NVS
- why not use it together with [a fashionable case](https://www.thingiverse.com/thing:5892225)? :)

## Hardware

Tested with:

- ESP32-WROOM-32 (AZDelivery)

## Usage

### Build Firmware

You need ESP-IDF. I'm using v4.4 (stable) installed using the VSCode extension.
To install it use the [Get Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html).

Open the folder `pgpemu-esp32` in VSCode. Run "ESP-IDF Build, Flash and Monitor".

### Upload Secrets

Go to `./secrets`, rename `secrets.example.yaml` to `secrets.yaml` and edit it with your dumped PGP secrets.

You'll need Python 3.9+ and [Poetry](https://python-poetry.org/). Install dependencies:

```shell
poetry install --no-root
```

Then upload your secrets to your device using:

```shell
# Linux: (replace /dev/ttyUSB0 with your ESP's serial tty)
poetry run ./secrets_upload.py secrets.yaml /dev/ttyUSB0
```

### Configuration

Serial menu:

```text
I (61081) uart_events: Device: RedPGP
I (61081) uart_events: User Settings (lost on restart unless saved):
I (61091) uart_events: - s - toggle PGP autospin
I (61091) uart_events: - c - toggle PGP autocatch
I (61101) uart_events: - p - toggle powerbank ping
I (61101) uart_events: - l - toggle verbose logging
I (61111) uart_events: - S - save user settings permanently
I (61111) uart_events: Commands:
I (61121) uart_events: - h,? - help
I (61121) uart_events: - A - start BT advertising again
I (61131) uart_events: - X - edit secrets (select eg. slot 2 with 'X2!')
I (61131) uart_events: - t - show BT connection time
I (61141) uart_events: - T - show FreeRTOS task list
I (61151) uart_events: - R - restart
```

### Development

From VSCode's Command Palette run "ESP-IDF: Add vscode configuration folder" [as described here](https://github.com/espressif/vscode-esp-idf-extension/blob/master/docs/C_CPP_CONFIGURATION.md) to get working C/C++ IntelliSense.

## Show Case

Catching Pokemon on connection ID 1 while spinning a Stop on connection ID 0:

```text
I (715392) pgp_led: Pokemon in range!
I (715392) button_task: pressing button delay=1918 ms, duration=300 ms, conn_id=1
I (716602) pgp_led: Pokestop in range!
I (717312) button_task: pressing button delay=1595 ms, duration=500 ms, conn_id=0
I (718382) pgp_led: LED pattern total duration: 6450 ms
I (718392) pgp_led: Caught Pokemon after 3 ball shakes.
I (720112) pgp_led: Got items from Pokestop.
```

Not catching Pokemon:

```text
I (503732) pgp_led: Pokemon in range!
I (503732) button_task: pressing button delay=1198 ms, duration=250 ms, conn_id=1
I (506092) pgp_led: Pokemon fled after 3 ball shakes.
```

Spinning Pokestops:

```text
I (142841) pgp_led: Pokestop in range!
I (142841) button_task: pressing button delay=1895 ms, duration=300 ms, conn_id=0
I (145851) pgp_led: Got items from Pokestop.
```

Bag full detection:

```text
I (151681) pgp_led: Pokestop in range!
I (151681) button_task: pressing button delay=2288 ms, duration=350 ms, conn_id=0
W (155031) pgp_led: Can't spin Pokestop. Bag is full.
```

Changing PGP secrets slots:

```text
I (359801) config_secrets: - pgpsecret_0: device=RedPGP mac=7c:bb:8a:...
I (359811) config_secrets: - pgpsecret_1: device=BluePGP mac=7c:bb:8a:...
I (359811) config_secrets: - pgpsecret_2: device=YellowPGP mac=7c:bb:8a:...
I (359821) config_secrets: - pgpsecret_3: (none)
I (359821) config_secrets: - pgpsecret_4: (none)
I (359831) config_secrets: - pgpsecret_5: (none)
I (359831) config_secrets: - pgpsecret_6: (none)
I (359841) config_secrets: - pgpsecret_7: (none)
I (359841) config_secrets: - pgpsecret_8: (none)
I (359851) config_secrets: - pgpsecret_9: (none)
# after pressing '2!':
W (438551) uart_events: slot=2
W (439841) uart_events: SETTING SLOT=2 AND RESTARTING
I (439851) uart_events: closing nvs
I (439851) uart_events: restarting
...
I (1072) PGPEMU: Device: YellowPGP
I (1072) PGPEMU: MAC: 7c:bb:8a:...
I (1072) PGPEMU: Ready.
I (1092) pgp_bt_gap: advertising start successful
```

Connect a second phone:

```text
I (324182) uart_events: starting advertising
I (324192) pgp_bt_gap: advertising start successful
I (356572) pgp_bt_gatts: ESP_GATTS_CONNECT_EVT, conn_id=1, mac=...
...
I (363552) pgp_led: LED pattern total duration: 30000 ms
I (363552) pgp_led: Pokemon in range!
I (363552) button_task: pressing button delay=1217 ms, duration=450 ms, conn_id=1   # <- note that this conn_id=1 is from our second connection
I (364822) pgp_bt_gatts: ESP_GATTS_WRITE_EVT: CHAR_LED_VAL
I (364822) pgp_led: LED pattern total duration: 500 ms
I (365872) pgp_bt_gatts: ESP_GATTS_WRITE_EVT: CHAR_LED_VAL
I (365872) pgp_led: LED pattern total duration: 1800 ms
W (365882) pgp_led: Pokeballs are empty or Pokestop went out of range.
I (374702) pgp_bt_gatts: ESP_GATTS_WRITE_EVT: CHAR_LED_VAL
I (374712) pgp_led: LED pattern total duration: 30000 ms
I (374712) pgp_led: Pokestop in range!
I (374712) button_task: pressing button delay=1742 ms, duration=500 ms, conn_id=0   # <- all the while autocatch/spin from the first connection are still handled
```

## Credits

- <https://github.com/yohanes/pgpemu> - Original PGPEMU implementation

## References

- <https://www.reddit.com/r/pokemongodev/comments/5ovj04/pokemon_go_plus_reverse_engineering_write_up/>
- <https://www.reddit.com/r/pokemongodev/comments/8544ol/my_1_gotcha_is_connecting_and_spinning_catching_2/>
- <https://tinyhack.com/2018/11/21/reverse-engineering-pokemon-go-plus/>
- <https://tinyhack.com/2019/05/01/reverse-engineering-pokemon-go-plus-part-2-ota-signature-bypass/>
- <https://coderjesus.com/blog/pgp-suota/> - Most comprehensive write-up
- <https://github.com/Jesus805/Suota-Go-Plus> - Bootloader exploit + secrets dumper
