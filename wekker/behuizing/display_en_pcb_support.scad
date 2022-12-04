$fn=90;


ring_thickness = 3.2 + 0.8;
support_thickness = 7;
support_height = 100;
support_width = 95;
display_translation = ring_thickness/2;

module ring1() {
    translate([0,0,display_translation]) {
        union() {
            difference() {
                cylinder(h=ring_thickness,d=65.5+2, center=true);
                cylinder(h=ring_thickness,d=52.3-2, center=true);
            }
            translate([0,0,-display_translation]) difference() {
                cylinder(h=support_thickness,d=65.5+2-4, center=true);
                cylinder(h=support_thickness,d=52.3-2+4, center=true);
            }
        }
    }
}

module ring2() {
    translate([0,0,display_translation]) {
        union() {
            difference() {
                cylinder(h=ring_thickness,d=36.85+2, center=true);
                cylinder(h=ring_thickness,d=23.3-2, center=true);
            }
            translate([0,0,-display_translation]) difference() {
                cylinder(h=support_thickness,d=36.85+2-4, center=true);
                cylinder(h=support_thickness,d=23.3-2+4, center=true);
            }
        }
    }
}

module jewel() {
    translate([0,0,display_translation]) {
        union() {
            cylinder(h=ring_thickness,d=23.3+1, center=true);
            translate([0,0,-display_translation]) 
                cylinder(h=support_thickness,d=15.3+1, center=true);
        }
    }
}

module display_top() {
    translate([0,0,0]) {
        cube([support_width, support_height, support_thickness], center=true);
    }
}

module uitsparing1(positie, diameter) {
    translate(positie) {
        cylinder(support_thickness+2, d=diameter, center=true);
    }
}

module uitsparing2(positie, hoogte, breedte, diameter) {
    translate(positie) {
        // union() {
            //cylinder(support_thickness+2, d=diameter, center=true);
            //cylinder(support_thickness+2, d=diameter, center=true);
            cube([ hoogte, breedte , support_thickness+2] , center=true);
        //}
    }
}

module brackets() {
    translate([0,0,0]) {
        bw = 4;
        bt = support_thickness/2;
        //bpx = support_width/2;
        //bpy = support_height/2;
        bpx = 24;
        bpy = 30;
        translate([-bpx-8, -bw/2, -bt]) cube([bpx, bw, bt ], center=false);
        mirror() translate([-bpx-8, -bw/2, -bt]) cube([bpx, bw, bt ], center=false);
        translate([-bw/2, -bpy-8, -bt]) cube([bw, bpy, bt ], center=false);
        mirror([0,1,0]) translate([-bw/2, -bpy-8, -bt]) cube([bw, bpy, bt ], center=false);
    }
}

module pcb_holes() {
    translate([0,0,0]) {
         translate([17,35,0]) cylinder(support_thickness+2, 2, 0, center=true);       
         mirror() translate([17,35,0]) cylinder(support_thickness+2, 2, 0, center=true);       
         translate([17,-35,0]) cylinder(support_thickness+2, 2, 0, center=true);       
         mirror() translate([17,-35,0]) cylinder(support_thickness+2, 2, 0, center=true);       
    }
}

module support() {
    union() {
        difference() {
            display_top();
            ring1();
            ring2();
            jewel();
            ur1 = 12;
            ur2 = ur1 + 2;
            uitsparing1([ support_width/2 - ur2,   support_height/2 - ur2,0], 2*ur1);
            uitsparing1([-support_width/2 + ur2,  support_height/2 - ur2,0],  2*ur1);
            uitsparing1([ support_width/2 - ur2, - support_height/2 + ur2,0], 2*ur1);
            uitsparing1([-support_width/2 + ur2,- support_height/2 + ur2,0], 2*ur1);
            uitsparing2([ 45,   0,0],   18, 34, 5);
            uitsparing2([-45,   0,0],   18, 34, 5);
            uitsparing2([   0,  50,0], 34,  18, 5);
            uitsparing2([   0, -50,0], 34,  18, 5);
            pcb_holes();
        }
        brackets();
    }
}

support();