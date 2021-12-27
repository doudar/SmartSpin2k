# 2-in-1 strap mod
This mod was a design exercise to bring tactile feedback to the shifters.  The stock design uses a lot of TPU material above the momentary switch, resulting in mushiness and lost tactility.  While  I was doing this, I merged two shifters into one housing in order to save plastic and reduce the travel movements necessary.


Fusion 360 exports and generic STEP files are both provided

![render of remote](screenshot.png)


## Assembly
- Extra care must be taken when soldering since the orientation of the wires makes a different - You must decide up front whether you want the cables to exit  towards the rear or towards the front and wire the left/right channels appropriately.
- In case you need to flip the orientation up, you can swap the outer  on the shifter connector where it attatches to the PCB

## BOM
- Same as standard shifters

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


## [Strap]
-strap.stl

## [Test Print]
- test_housing.stl