
This repository has code I have made for ESP32 boards to control different types of PTZ cameras and my own camera platform software.

All code is for platformio framework. Open each folder in platformio-enabled IDE.

## Configure 

Add `secrets_config.ini` file to root of this repository with contents

```ini
[common]
build_flags = 
    -DWIFI_SSID="\"your ssid\""
    -DWIFI_PASS="\"your wifi password\""
```

## My own PTZ software

Based on Visca control commands and running stepper motors for pan, tilt and zoom features. Code at [ESP32-control](ESP32-control)
Blog post: [PanTiltZoom Camera Head ](https://blog.kiisu.club/posts/dlsr-ptz-controller/)



## Joystick for Panasonic and Visca

Control both visca and panasonic ptz cameras with one joystick. Code at [ESP32-joystick-panasonic](ESP32-joystick-panasonic)
Blog post: [Remote PTZ controller for Visca and Panasonic cameras](https://blog.kiisu.club/posts/ptz-remote-controller/)

## Visca ESP32 adapter

Make RS-232 Visca cameras wifi-enabled with this simple code. Requires max2323 or similar converter. Can be found in [Sony-visca-udp-adapter](Sony-visca-udp-adapter)