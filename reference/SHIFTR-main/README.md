[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![CI](https://github.com/JuergenLeber/SHIFTR/actions/workflows/build.yml/badge.svg)](https://github.com/JuergenLeber/SHIFTR/actions/workflows/build.yml)
# SHIFTR 
A BLE to Direct Connect bridge for bike trainers using a WT32-ETH01 module based on ESP32. Increases connection stability and additionally adding Zwift "virtual shifting" functionality to any device supporting FE-C over BLE. Also adding FTMS emulation via DirCon for non-FE-C compatible software like MyWhoosh.

Currently tested with Garmin/Tacx NEO 2T and Vortex but should also work on other trainers supporting FE-C over BLE.

Tested bike trainer software so far:
- &#x2611; Zwift (Pass-through + virtual shifting mode)
- &#x2611; MyWhoosh (Pass-through mode only / TACX Neo 2T and Vortex only with FTMS emulation enabled) 

Disclaimer: Even if everything is working fine this project is still work in progress. Feel free to provide feedback if you encounter any issues.

## Overview
Bike trainers heavily evolved over the last few years. Tacx (acquired by Garmin some years ago) is one of the market leaders producing bike trainers of a very high quality with special features like e.g. "Road Feeling" to simulate different road surfaces. 
Other vendors like Zwift came up with cool new features like "virtual shifting" using the "Zwift Cog" and a controller device (Zwift Click/Play) to have a non-mechanical shifting solution which is setting the gears via software that adjusts the resistance in the trainer. 
Wahoo came up with a technology called "Direct Connect" that allows the trainer devices to be connected via Ethernet (or even WiFi) instead of bluetooth to have a more stable connection.

While other vendors like e.g. Elite or JetBlack are very fast in implementing things like virtual shifting to their newer models Garmin is very slow and unresponsive when it comes to such feature requests. Probably also because they have their own ecosystem that competes with Zwift. 

Using Zwift for many years and owning a "Tacx Vortex" and a "Tacx Neo 2T" bike trainer I have been jealous of features like virtual shifting and the possibility to connect the trainer device via ethernet. So the idea for this project, SHIFTR, was born by having a device between Zwift and the Tacx supporting all this cool new functionality.

After many days and nights of searching the internet for a specification of the protocols I was only able to find small parts for "Direct Connect" that have already been reverse engineered by Roberto Viola in his [QDomyos-Zwift](https://github.com/cagnulein/qdomyos-zwift) project. The code helped me a lot to understand the protocol.
Unfortunately there was absolutely no information about the "virtual shifting" to be found. So a very long session of decompiling Zwift apps and sniffing BLE traffic between Zwift and capable trainers (which fortunately a friend had one of) started. It took some weeks but I was able to reverse engineer the used protocols.

As I wanted to have a convenient solution that doesn't require a special program or app to be started to have that functionality I decided to implement everything on an ESP32 microcontroller that already features WiFi and Bluetooth onboard and also optionally ethernet. The perfect module for that was the [WT32-ETH01](https://en.wireless-tag.com/product-item-2.html) module that comes with everything I needed. The device will be placed very close to the trainer in a 3D printed case that also has a place for a step down converter to be able to use the existing power supply from the trainer.

Now I have a great setup with a Garmin/Tacx NEO 2T with a Zwift Cog installed and communicating to Zwift via ethernet while still having the "Road Feeling" feature: 
![The whole setup](images/tacx_neo_2t_and_zwift_cog_and_device.jpg)

This setup even works great with Zwift running on an Apple TV. Apple TV only supports two simultaneous bluetooth connections and those can now be used for the Zwift Play controllers while the trainer itself is being connected through ethernet.

## How it works
The SHIFTR is working in two modes, "Pass-through" and "Pass-through + virtual shifting":
### Pass-through mode
In this mode the SHIFTR just takes all services from the BLE trainer devices and provides them 1:1 via Direct Connect. It supports SIM and ERG mode as if the device would be connected via bluetooth.
### Pass-through + virtual shifting mode
This mode provides the pass through features as mentioned before and additionally offers a special Zwift service via Direct Connect that allows it to behave like a Zwift certified device offering virtual shifting, too. All necessary information like incline, bicycle and user weight, etc. are transmitted by Zwift and used for calculations. SIM and ERG mode are supported but of course virtual shifting only works in SIM mode.

Details about the virtual shifting function and calculations can be found [here](./VirtualShifting.md).

***Note***: Virtual shifting will be disabled if neither Zwift Play controllers nor a Zwift Click are connected which results in the standard SIM mode with a track resistance of 0%. Then you'd have to shift with your bike but with a Zwift Cog installed this doesn't make sense of course. If the controllers disconnect during a training then the fallback will be this normal SIM mode but as soon as the controller(s) are re-connected the virtual shifting will be enabled again.

### Pass-through + FTMS emulation
This mode provides all services from the BLE trainer and additionally offers a FTMS emulation that internally sends the commands to the BLE FE-C trainer. This makes software like MyWhoosh also working. Doesn't (at least not yet) work with virtual shifting as this is a Zwift-only feature at the moment.

## Needed hardware
- WT32-ETH01 ESP32 board (e.g. from [Amazon](https://www.amazon.de/WT32-ETH01-Embedded-Schnittstelle-Bluetooth-Entwicklungsplatine/dp/B0CW3DDWZ4))
- Step-Down converter 5-80V (the Tacx Neo supplies 48V) to 5V (e.g. from [Amazon](https://www.amazon.de/dp/B0CMC7Y3DJ))
- Y-Splitter for the Tacx power cable (e.g. from [Amazon](https://www.amazon.de/dp/B09FHHN9T5))
- Power connector for the case (e.g. from [Amazon](https://www.amazon.de/gp/product/B09PD6J4BN))
- 3D printed case that fits the WT32-ETH01. Luckily I was able to find a perfect match that also fits the step-down converter: [WT32-ETH01 Enclosure](https://www.thingiverse.com/thing:5621092)
- Two countersunk screws M2x7 to hold the lid on the case


## Hardware installation
- ***IMPORTANT***: Don't solder the pin headers that came with the WT32-ETH01. With the longer pins on the bottom side the module won't fit the case anymore.
- ***IMPORTANT #2***: Connect the step-down converter first to a power supply (e.g. the original one with 48V) and a multimeter and use a screwdriver to adjust the output voltage to 5V before connecting it to the WT32-ETH01 module!
- Use a standard USB<->TTL converter (e.g. with FTDI232 or similar). Make sure that it can provide **3,3V and not 5V** and connect as follows (see also pinout above):
  | Converter | -> | ETH01-EVO | 
  |-|-|-|
  | RX | -> | TXD |
  | TX | -> | RXD |
  | 3V3 | -> | 3V3 |
  | GND | -> | GND |

  It's best to use some breadboard jumper wires that can just be connected without soldering. Use a rubber band to hold them in contact.

  To start the WT32-ETH01 in boot mode it is necessary to connect "IO0" with GND and then to reset the board, shortly connect "EN" to GND for a quarter of a second.

## Software installation
- Open the project in [PlatformIO](https://platformio.org) and let it install the dependencies
- Connect the programmer and upload via the task wt32-eth01 -> Upload and Monitor
- Connect an ethernet cable and make sure a DHCP server exists in your network
- After a few seconds you should be able to see the IP address and the hostname (like e.g. "SHIFTR-123456.local") in the monitor log
- If you don't use the Ethernet interface you can also use the WiFi functionality. For that the device opens an AP with the device name (e.g. "SHIFTR-123456") and the password is the same but of course can be changed later. Please note that WiFi is provided by an external library called [IoTWebConf](https://github.com/prampec/IotWebConf) and is more or less untested
- After that you can configure the device and its network settings in the web interface on e.g. http://SHIFTR-123456.local
- The status page is accessible without credentials, the settings and firmware update pages need the username "admin" and the password is the configured AP password or just the device name (e.g. "SHIFTR-123456") as default. Note that both are case sensitive!
- Further updates can be made over ethernet or WiFi so you can disconnect the programming adapter now

## Finalization
- Finally you can solder some wires to the power connector to the step down module and from there to the WT32-ETH01. Then mount everything in the 3D printed case:

  ![Mounted adapter](images/wt32-eth01_case.jpg)

- Use the Y-Power-Splitter to connect the Tacx trainer and the device:
  
  ![Connected to Tacx](images/connected_to_tacx_neo_2t.jpg)

- Power the Trainer and go to the web interface
- Go to "Settings", select the trainer device and "Save"
- Make sure the "Virtual shifting" option is enabled if you want to use it. Virtual shifting only works in Zwift when a Zwift Click or Zwift Play controllers are connected. Otherwise it will fall back to the normal ERG and SIM modes
- In Zwift you need to connect to the "SHIFTR 123456" device instead of your old device now. Have fun!

## Thank you!
You've made it to the end, hopefully you'll have fun rebuilding the whole thing. Please tell me if you like it and if it works as expected.
In case of any questions feel free to contact me!

## Disclaimer
Please always check the documentation of the hardware you bought as there are sometimes small changes in pin assignments, voltage and so on. I don't take any liability for damages or injuries. Build this project at your own risk. The linked products and pages are for reference only. I don't get any money from the manufacturers or the (re)sellers.




