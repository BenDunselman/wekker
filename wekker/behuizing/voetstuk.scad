$fn=90;
// maten in centimeter
breedte_vlak=11.5;
breedte_randje=0.3;
breedte_overall=breedte_vlak+2*breedte_randje;
breedte_hoek=1.0;
breedte_ondervlak=breedte_vlak-2*breedte_hoek;
diepte_vlak=1.0;
diepte_randje=0.3;
diepte_overall=diepte_vlak+diepte_randje;
hoogte_vlak=4.0;
hoogte_randje=0.3;
hoogte_overall=hoogte_vlak+hoogte_randje;
hoogte_ondervlak=3.5;
dikte_vlak=hoogte_vlak-hoogte_ondervlak;
boog_straal_boven=hoogte_ondervlak;
boog_straal_onder=hoogte_ondervlak-1;
boog1_x=breedte_overall/2+boog_straal_boven-1;
boog1_y=hoogte_randje;
boog1_z=-boog_straal_boven-dikte_vlak;
boog2_x=-breedte_overall/2-boog_straal_boven+1;
boog2_y=boog1_y;
boog2_z=boog1_z;
voetstuk_breedte=2.0;
voetstuk_diepte=diepte_overall;
voetstuk_hoogte=1.0;
voetstuk1_x=boog1_x-voetstuk_breedte;
voetstuk1_y=boog_straal_boven-voetstuk_hoogte;
voetstuk1_z=-boog_straal_boven;
voetstuk2_x=voetstuk_breedte;
voetstuk2_y=voetstuk1_y;
bovenvoetstuk1_x=breedte_overall/2-voetstuk_breedte;
bovenvoetstuk1_y=-diepte_overall/2;
bovenvoetstuk1_z=-voetstuk_hoogte;

module vlak() {
    translate([-breedte_overall/2,-diepte_overall/2,-dikte_vlak]) {
        cube([breedte_overall, diepte_overall, dikte_vlak], center=false);
    }    
}

module front_rand() {
    translate([-breedte_overall/2,-diepte_overall/2,0]) {
        cube([breedte_overall, diepte_randje, hoogte_randje], center=false);
    }    
}

module zij_rand_links() {
    translate([-breedte_overall/2,-diepte_overall/2,0]) {
        cube([breedte_randje, diepte_overall, hoogte_randje], center=false);
    }    
}

module zij_rand_rechts() {
    mirror() zij_rand_links(); 
}

// snippet    translate([0,0,0]) rotate(180,[1,0,1]) 

module boog_outer() {
    translate([0,0,0]) // rotate(180,[1,0,1]) 
        linear_extrude(diepte_overall) intersection(){square(boog_straal_boven);circle(boog_straal_boven);}
}

module boog_inner() {
    translate([0,0,0]) // rotate(180,[1,0,1]) 
        linear_extrude(diepte_overall) intersection(){square(boog_straal_onder);circle(boog_straal_onder);}
}

module boog1() {
    translate([boog1_x,diepte_overall/2,0]) rotate(-180,[0,1,0]) rotate(90,[1,0,0]) difference() {
        boog_outer();
        boog_inner();
    }
}
        
module boog2() {
    translate([boog2_x,-diepte_overall/2,0])   rotate(-90,[1,0,0]) difference() {
        boog_outer();
        boog_inner();
    }
}

module voet1() {
    translate([voetstuk1_x,-voetstuk_diepte/2,voetstuk1_z]) {
        cube([voetstuk_breedte, voetstuk_diepte, voetstuk_hoogte], center=false);
    }    
}

module voet2() {
    mirror() voet1();    
}

module bovenvoet1() {
    translate([bovenvoetstuk1_x,bovenvoetstuk1_y,bovenvoetstuk1_z]) {
        cube([voetstuk_breedte, voetstuk_diepte, voetstuk_hoogte], center=false);
    }    
}

module bovenvoet2() {
    mirror() bovenvoet1();    
}

module boorgat1() {
    union() {
        translate([breedte_vlak/2-0.5,diepte_randje/2,-1]) {
            cylinder(h=4,d=0.45, center=false);
        } 
        translate([breedte_vlak/2-0.5,diepte_randje/2,-2.7]) {
            cylinder(h=2,d=0.75, center=false);
        } 
    }
}

module boorgat2() { mirror() boorgat1(); }

module voetstuk() {
    union() {
        vlak();
        front_rand();
        zij_rand_links();
        zij_rand_rechts();
        boog1();
        boog2();
        voet1();
        voet2();
        bovenvoet1();
        bovenvoet2();
    }
}

difference() {
    voetstuk();
    boorgat1();
    boorgat2();
}