[![Watch the video](Pictures/youtube.png)](https://youtu.be/cPiIlZ2P5Ac)

# SmartSpin2K

Hardware (3d Printed) and Software Project that uses an ESP32 and servo motor to control spin bikes that have a resistance knob. 
The initial target was to use it with Zwift: stationary bike up hills and feel the burn.

## Background
I travel for a living and most hotels have spin bikes. I started carrying my own power meter pedals for the spin bikes so I could ride Zwift but the experience just isn't the same without automatic resistance on the hills. Then one day I was looking down at that simple to turn knob and I had a thought - With the power meter pedals attached, almost everything is already in place on this $300 spin bike to turn it into the equivalent of a $3,000 computer controlled smart bike! And so here we are....

## Summary of function
The power meter pedals send cycling power and cadence to Zwift (or any other cycling app). Smartbike 2k connects to the APP as a controllable trainer which then simulates gradient by automatically turning the knob a set amount for each percent grade change on your virtual road. Very soon the pedals will be able to connect directly to SmartBike2k (with the data relayed to your app of choice) which will enable ERG mode (where SmartBike2k adjusts the knob to make you maintain a steady watt output no matter what your cadence is) on the spin bike. In addition to adjusting the knob, SmartBike2k also has shifter buttons to simulate virtual shifting. 

### Hardware:

|qty   |         Part             |              Link                      |
|:-----:|--------------------------|---------------------------------------:|
|1      | ESP32 Dev Board          | https://amzn.to/2ZNyjQX  |
|1      | NEMA 17 Pancake Stepper  | https://amzn.to/35tNiCW |
|1      | A4988 Driver Board       | https://amzn.to/35w2EqA |
|1      | LM2596 Buck Converter    | https://amzn.to/33ofggY  |
|1      | 12V Power Supply         | https://amzn.to/3hqrTNw  |
|2      | Tactile Switches         | https://amzn.to/33ezmKx |
|2      | 608 Skate Bearings       | https://amzn.to/3isBzrW  |

Optional equipment so you can actually use it for fitness:
|qty    |         Part             |              Link                      |
|:-----:|--------------------------|---------------------------------------:|
|1      |Assioma Pedals(or similar)| https://amzn.to/3ioSjk7|                   
|1      |Cheap Spin Bike Check Ebay| https://amzn.to/33mPmKj |


So for $700-$1000 you can build a fully functional smart bike! If you already have a Peloton bike collecting dust in the corner, this will work on that too!


### Full Documentation Soon
But if you're adventerious and handy you can probably take a look at my pictures below or the assembly video and start tinkering. Feel free to reach out in the meantime as I'd love to help someone get this working for themselves. In short, the process is:

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

ummm that was easy...right? :)


### Esp32 Connection Diagram
<img src="Pictures/SmartBike2k_Esp32_Connections.png" alt="esp32 connections" style="height: 300px; width: 100"/> 

### A4988 Connection Diagram
<img src="Pictures/SmartBike2k_A4988_Connections.png" alt="esp32 connections" style="height: 200px; width: 100"/> 

The hardware mentioned above mounts into a 3d printed enclosure which then easily attaches to a spin bike.


<img src="Pictures/CadPreview.JPG" alt="Cad Preview" style="height: 200px; width: 100"/>

The Finished assembly looks somthing like this prototype. 

<img src="Pictures/AssembledSideView.jpg" alt="Assembled SideView" style="height: 200px; width: 100"/>

Here is the earliest prototype mounted on a spin bike. (Cardboard wrapped in ducktape enclosure :) ) 

<img src="Pictures/prototype_on_spin_bike.jpg" alt="Assembled SideView" style="height: 200px; width: 100"/>


This is my first GitHub project and one of my first (recent) large coding projects, so any help or feedback is greatly appreciated.



