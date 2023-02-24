/*All in one yoke*/

// Length of yoke
yokeLength = 55;

// Width of the yoke opening
yokeWidth = 32;

// Thickness of the yoke arms
yokeThickness = 5.5;

// YokeHeight
yokeHeight = 30;

// Strap Slot Thickness
strapThickness = 2;

// Strap Slot Height
strapHeight = 15;

///Expert Options Below///
// height of the arm
armHeight = 15;

// Overall width of the attachment arm
armAttachWidth = 20;

// bolt diameter
boltDiameter = 5.5;

// armSlotWidth
armSlotWidth = 9.2;
///////////////////////////
module strapHolder()
{
    translate([ -yokeLength / 2, yokeWidth / 2 + yokeThickness / 2, 0 ])
    {
        difference()
        {
            hull()
            {
                translate([ -yokeThickness / 2, yokeThickness * 1.5, 0 ]) cylinder(yokeHeight, yokeThickness / 2, yokeThickness / 2);
                cylinder(yokeHeight, yokeThickness / 2, yokeThickness / 2);
            }
            rotate([ 0, 0, 30 ])
            {
                translate([ -yokeThickness * 2, yokeThickness / 1.3, yokeHeight / 2 - strapHeight / 2 ])
                {
                    cube([ yokeLength, strapThickness, strapHeight ]);
                }
            }
        }
    }
}

module ovalTube()
{
    difference()
    {
        hull()
        {
            cylinder(yokeHeight, yokeWidth / 2 + yokeThickness, yokeWidth / 2 + yokeThickness);
            translate([ -yokeWidth + yokeThickness, 0, yokeHeight / 2 ])
            {
                cube([ yokeLength, yokeWidth + yokeThickness * 2, yokeHeight ], true);
            }
        }
        // middle of yoke
        cylinder(yokeHeight + 5, yokeWidth / 2, yokeWidth / 2);
        translate([ -yokeLength / 2, 0, yokeHeight / 2 ])
        {
            cube([ yokeLength, yokeWidth, yokeHeight + 5 ], true);
        }
        // end cut
        translate([ -yokeLength, 0, yokeHeight / 2 ])
        {
            cube([ yokeLength, yokeWidth * 2, yokeHeight + 5 ], true);
        }
    }
}

difference()
{
    union()
    {
        // main attach point
        translate([ 0, 0, 0 ])
        {
            cube([ armAttachWidth * 1.5, armAttachWidth, armHeight ], true);
            // radius above hook
        }
        // yoke
        translate([ -armAttachWidth - yokeWidth / 2 + yokeThickness / 2, 0, -armHeight / 2 ])
        {
            ovalTube();
            strapHolder();
            mirror([ 0, 1, 0 ]) strapHolder();
        }
    }

    // bolt hole
    translate([ armAttachWidth / 3, 0, 0 ])
    {
        rotate([ 0, 90, 90 ])
        {
            cylinder(armHeight * 4, boltDiameter / 2, boltDiameter / 2, true);
            // head recess
            translate([ 0, 0, -armAttachWidth / 1.3 ])
            {
                cylinder(armHeight, boltDiameter, boltDiameter, true);
            }
            // Nut recess
            translate([ 0, 0, armAttachWidth / 1.3 ])
            {
                cube([ 5.2, 8.9, boltDiameter * 2.7 ], true);
                rotate([ 00, 00, 60 ])
                {
                    cube([ 5.2, 8.9, boltDiameter * 2.7 ], true);
                }
                rotate([ 0, 0, 120 ])
                {
                    cube([ 5.2, 8.9, boltDiameter * 2.7 ], true);
                }
            }
        }
    }

    // hook hole
    translate([ -armSlotWidth / 4, 0, 0 ])
    {
        cylinder(armHeight + 2, armSlotWidth / 2, armSlotWidth / 2, true);
    }

    // hook slot
    translate([ armAttachWidth / 1.5, 0, 0 ])
    {
        cube([ armAttachWidth + armSlotWidth, armSlotWidth, armHeight + 2 ], true);
    }
}
