# 2-in-1 strap
This mod was a design exercise to bring tactile feedback to the shifters.  The stock design uses a lot of TPU material above the momentary switch, resulting in mushiness and lost tactility.  This design also merges two shifters into one housing to  order to save plastic and reduce the travel movements necessary.


Fusion 360 exports and generic STEP files are both provided

![render of remote](screenshot.png)


## Assembly
- In case you need to flip the orientation of the shifter, you can easily do so in the SmartSpin2K User Interface.  Navigate to [http://SmartSpin2K.local/](http://SmartSpin2K.local).  Here, you can toggle the Shifter Direction button until the device shifts in the right direction and save your settings.  You can verify your settings in the [Web Shifter](http://smartspin2k.local/shift.html)

## Printing instructions
The bridging layers on this print are tricky to get right with TPU.  It is important to minimize holes in the bridging and ensure it is well sealed against sweat and water.  These tips have worked very well for me with this print in Superslicer.  With good cooling and a slow print speed, this print is very achievable. 

If you are unsure, print test_housing.stl - this will print much more quickly than the full strap and allow you to dial in the print using less material and time.

- Cooling fan at 75% (this is different for every printer and duct - I'm using a single 5015 fan)
- 0.2mm layer height with 0.4mm nozzle
- 3 perimeters
- Supports disabled
- 20% infill
- Monotonic top, solid, and bottom infills
- Infill Angle - 90 Degrees (You want the lines following the length of the strap)
- ![close up of strap](strap_close-up.png)
- Add a height range modifier on the strap from 8.85-13.70mm with the following settings.  The combination of ironing and concentric infill help ensure a watertight finish.
-- Solid Infill pattern: Ironing
-- Top Infill Pattern: Concentric
-- ![layer settings screenshot](layer_settings.png)

## [Test Print]
- test_housing.stl

## [Full Set]
- shifter_set.stl

## [Strap]
- strap.stl

## [Plug]
- plug.stl - x2

## [Retainer]
- retainer.stl

