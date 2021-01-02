[![Watch the video](Pictures/youtube.png)](https://youtu.be/cPiIlZ2P5Ac)

# SmartSpin2K

Hardware (3d Printed) and Software Project that uses an ESP32 and stepper motor to control spin bikes that have a resistance knob. 
The initial target was to use it with Zwift: stationary bike up hills and feel the burn.

## Background
I travel for a living and some hotels I stay at have spin bikes. I started carrying my own power meter pedals for the spin bikes so I could ride Zwift, but the experience just isn't the same without automatic resistance on the hills. Then one day I was looking down at that simple to turn knob and I had a thought - With the power meter pedals attached, almost everything is already in place on this $300 spin bike to turn it into the equivalent of a $3,000 computer controlled smart bike! And so here we are....

## Summary of function
The power meter pedals send cycling power and cadence to Zwift (or any other cycling app). SmartSpin2k then connects to the APP as a controllable trainer which simulates gradient by automatically turning the knob a set amount for each percent grade change on your virtual road. If you choose to pair your pedals directly to SmartSpin2k, ERG mode is also availiable which lets an app directly control your watt output reguardless of your cadence. In addition to adjusting the knob, SmartSpin2k also has shifter buttons to simulate virtual shifting. 

Configuration is accomplished via a web interface hosted by the SmartSpin2k. If you connect the unit to WiFi with an internet connection, it will also preform automatic software updates whenever they become availiable!  

### Recomended Hardware:

You'll need the quantity listed below of each item excep the driver board. An optional PCB is in process which will make the wiring much simpler! 

|qty   |         Part             |              Link                      |
|:-----:|--------------------------|---------------------------------------:|
|1      | ESP32 Dev Board          | https://amzn.to/2ZNyjQX                |
|1      | NEMA 17 Pancake Stepper  | https://amzn.to/37mKKHh                |
|1      | TMC2225                  | https://amzn.to/3kctdEQ                |
|1      | LM2596 Buck Converter    | https://amzn.to/33ofggY                |
|1      | 24V Power Supply         | https://amzn.to/3r4e1i0                |
|2      | Tactile Switches         | https://amzn.to/33ezmKx                |
|2      | 608 Skate Bearings       | https://amzn.to/3isBzrW                |

Optional equipment so you can actually use it for fitness:
|qty    |         Part             |              Link                      |
|:-----:|--------------------------|---------------------------------------:|
|1      |Assioma Pedals(or similar)| https://amzn.to/3ioSjk7                |                   
|1      |Cheap Spin Bike Check Ebay| https://amzn.to/33mPmKj                |


So for $700-$1000 you can build a fully functional smart bike! If you already have a Peloton bike collecting dust in the corner, this will work on that too!


### Full Documentation Soon
But if you're adventerious and handy you can probably take a look at my pictures below or the assembly video and start tinkering. Feel free to reach out in the meantime as I'd love to help someone get this working for themselves. There's also a brand new wiki at https://github.com/doudar/SmartSpin2k/wiki 

In short, the process is:

1. Install Microsoft Visual Code. https://code.visualstudio.com/
2. Install PlatformIO into Visual Code. https://platformio.org/platformio-ide
3. Open this project in PlatformIO.  
4. Compile the project and upload to an ESP32
5. Print all the hardware. You'll need 1 Bottom Case, 1 Knob Cup, Both Spur Gears, 1 Stepper Crossbar, 1 Top Cover and a knob insert that fits your spin bike. PETG works well.
6. If you're using the same switches for the shifters, you can optionally print the provided files. Previously I was using tape as a protective enclosure and it worked fine (if ugly).
7. Wire all of the connections outside the case. Use push on connectors if possible. 
8. Press both bearings into the recess.
9. Install the power supply, stepper driver and ESP32 into their brackets.
10. Install the power plug and shifter plug. 
11. Push the large gear down into the bearing. From the other side slide the knob cup onto it's shaft.  
12. Attach the stepper crossbar to the stepper motor.
13. Install the stepper motor. 
14. Put the cover on. 
15. Connect to the SmartSpin2k Wifi network for initial configuration. Default password is "password". If connecting via mobile, the configuration page should automatically pop up. 
16. Once configured on your local network, you can easily find the Smartspin2k by going to http://SmartSpin2K.local/

ummm that was easy...right? :)

### Esp32 Connection Diagram
<img src="Pictures/SmartSpin2k_Esp32_Connections.jpg" alt="esp32 connections" style="height: 300px; width: 100"/> 

### TMC2225 Connection Diagram
<img src="Pictures/SmartSpin2k_TMC_Connections.jpg" alt="esp32 connections" style="height: 200px; width: 100"/> 

*note, you can use an A4988 stepper driver also but you'll need to manually set the driver current with a screwdriver as well as possibly modify the output pin settings in /include/settings.h . If you do so, you'll aslo want to turn off automatic firmware updates. 


<img src="Pictures/CadPreview.JPG" alt="Cad Preview" style="height: 200px; width: 100"/>

The Finished assembly looks somthing like this prototype. 

<img src="Pictures/on_bench_with_shifters.jpg" alt="On bench with shifters" style="height: 200px; width: 100"/>

Here it is mounted on a bike (ignore the piece of wood, it's no longer needed with a different hard attachment strap). 

<img src="Pictures/Closeup.jpg" alt="Closeup.jpg" style="height: 200px; width: 100"/>

Installed on a random Schwinn bike at the Gym:
<img src="Pictures/Schwinn.jpg" alt="Installed On Schwinn" style="height: 200px; width: 100"/>

...And here it is on a cheap magnetic HMC trainer off Amazon:
<img src="Pictures/HMC.jpg" alt="Installed on HMC" style="height: 200px; width: 100"/>

Here is the earliest prototype mounted on a spin bike. (Cardboard wrapped in ducktape enclosure :) ) 

<img src="Pictures/prototype_on_spin_bike.jpg" alt="Assembled SideView" style="height: 200px; width: 100"/>

Any help or feedback is greatly appreciated.



