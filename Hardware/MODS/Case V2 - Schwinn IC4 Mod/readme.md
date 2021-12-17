# Bowflex C6 / Schwinn IC4 mod
This mods the version2 case of SmartSpin2k to have a dedicated mount for a Schinn IC4 / Bowflex C6.  This should work with other bikes with minimal modification.  Bike mount.stl is intended for semi-permanent installation with zip ties or VHB tape.  This allows 1-handed installation and removal while offering a lot more rigidity than the original, highly portable design.

Fusion 360 exports and generic STEP files are both provided

![image](https://user-images.githubusercontent.com/24726844/144154082-946314dc-77e9-4369-8a4d-50c85c6263d2.png)
![image](https://user-images.githubusercontent.com/24726844/144154111-70023323-28be-45eb-903a-97bb79305001.png)


## Revisions from standard version 2 case:
- Minor revisions to motor mount to suit my stepper motor.
- Replace wood screws with M3 threaded inserts to accept m3 screws. Source from [Aliexpress](https://www.aliexpress.com/item/4000232858343.html?spm=a2g0s.9042311.0.0.21164c4dGhWHhY): M3 x 5 x 4 heat set inserts. (5mm diameter).
- Design to use a semi-permanent mount on Schwinn IC4/Bowflex C6, allowing one-handed installation and removal.
- opportunity to 

## BOM
- In addition to the electronics specced for the version 2 case, you will need:
- M3 x 5 x 4 heat set inserts. (5mm diameter).  Source from [Aliexpress](https://www.aliexpress.com/item/4000232858343.html?spm=a2g0s.9042311.0.0.21164c4dGhWHhY).  [Instructions for installation here](https://www.youtube.com/watch?v=cyof7fYFcuQ)
- 4xM3 screws - I think M3x8 or M3x10 should work. As long as it's long enough to go into the threaded inserts, the length doesn't matter.  There's plenty of clearance if it's too long
- 1 pc M5x12mm for the slide on the arm
- 2pc M5x30mm - 1 for the bike mount and another for the case
- 3pc M5 nuts

OPTIONAL (if using [hex bolt shaft mod](https://github.com/doudar/SmartSpin2k/discussions/267))
- 1 pc 5/16" x 1-1/2" hex head bolt
- 3pc 5/16" washers
- 2 pc 5/16" nuts

For the bike mount, you have two options: VHB tape or something similar OR if you want to protect the paint and avoid adhesives, you can use the zip tie and rubber sticker approach:

- 6x zip ties for the bike mount - what I did here is fish a zip tie from one end, around the square tube and through the other. Used a 2nd zip tie's ratchet bit to tighten the first zip tie. Trim off the excess. Between the rubber and zip ties, it's a pretty solid mount
- some sort of rubber or silicon non-skid stickers to put on the bike mount to prevent sliding.  [I'm using this](https://www.amazon.ca/gp/product/B00P5VQ7HE)

## Printing instructions
- 4 perimeters
- 40% infill
- 0.2mm layer height
- Supports enabled (from print bed only) for ic4-body-left.stl and ic4-body-right.stl

## [Bike Mount]
-bike mount.stl

## [Case]
- ic4-body-left.stl
- ic4-body-right.stl

## [Gears]
From main project folder (Case Version2)
- 40_Tooth_Mod1.STL - fully printed single piece including shaft
- Spur gear 1m 40T Hex.stl - to be used with an M8 or 5/16x1.5" bolt.  Requires matching knob cup (see info below)

Optionally available in this folder:
- Spur Gear 1M 40T HEX - large bolt mod.stl - Bolt spacing is intended for use with [these bolts](https://www.grainger.ca/en/product/TAP-BOLT%2CHX%2C5-16%22-18-X-1-1-2%22%2CUNC%2C100-PK/p/EBP41UC82) or bolts of a similar spec.  1/2" drive size, 7/32" head height.


## [Knob cup and insert]
MULTIPLE OPTIONS.  READ FIRST:
You have three options, each with various benefits:
- Stock cup and insert from the main project
  -  mix and match inserts to suit your bike.  Cup is available in steel bolt (HEX) or fully printed options.  [See here](https://github.com/doudar/SmartSpin2k/tree/develop/Hardware/KnobCups) and [here](https://github.com/doudar/SmartSpin2k/tree/develop/Hardware/Inserts)
- IC4-Optimized Cup-and-insert-combo.stl
  - This combines the stock knob fully printed knob cup and insert.  Works with stock 3d printed 40t gear
- ic4 HEX knob cup and insert combined.stl
  - This is to be used with a 5/16x1.5" hex head screw or an M8 with similar length

  
## [KnobCups]
- Knob_Cup_V2.STL

## [Inserts]
- The insert for your bike.