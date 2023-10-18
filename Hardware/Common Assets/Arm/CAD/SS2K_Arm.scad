
/*All in one Arm*/

//Length of arm
armLength=61.5; // 


//////////////////////////
//Width of the arm
armWidth=9;

//Height of the arm
armHeight = 12;

//bolt diameter
boltDiameter = 5.25;

//hook diameter
hookDiameter = 5.2;
///////////////////////////

//Required to add roundness to parts
$fa = 1;
$fs = 0.5;

difference(){
union(){ 
    //round part at the end
    translate([0,-armLength/2, 0]){
    cylinder(armWidth, armHeight/2, armHeight/2, true);
    }
    //main arm
    cube([armHeight, armLength, armWidth], true);
    translate([0,(armLength/2)-(armHeight/2), 0]){
    cube([armHeight*1.5, armHeight, armWidth], true);   
    //radius above hook   
    translate([-armHeight + armHeight/2,-armHeight/2, 0]){
    cylinder(armWidth, armHeight/4, armHeight/4, true);
    }
    //Radius below hook
    translate([armHeight - armHeight/2,-armHeight/2, 0]){
    cylinder(armWidth, armHeight/4, armHeight/4, true);
    } 
        
   }
}

// bolt hole
translate([0,-armLength/2, 0]){
cylinder(armWidth +2, boltDiameter/2, boltDiameter/2, true);
}

// hook hole
translate([-hookDiameter/4,(armLength/2)-(armHeight/2), 0]){
cylinder(armWidth +2, hookDiameter/2, hookDiameter/2, true);
}

//hook slot
translate([armHeight/1.5,(armLength/2)-(armHeight/2), 0]){    
    cube([armHeight+hookDiameter, hookDiameter, armWidth +2], true);
}

//size text
//hook slot
translate([0,0,armWidth/2-.5]){  
rotate([0,0,90]){ 
    linear_extrude(2){
    text(str(armLength),  size = armHeight-3, halign = "right", valign = "center");
}}}

}



