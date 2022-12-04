$fn=90;

module top() {
     translate([1,-21,5]) {
         minkowski() {
            cube([3, 42, 3], center=false);
            cylinder(1);
         }
     }      
}

module bottom() {
     translate([1,-18.5,0]) {
         minkowski() {
            cube([3, 37, 5], center=false);
            cylinder(1);
         }
     }      
}

module bracket() {
    union() {
        top();
        bottom();
    }
}

bracket();