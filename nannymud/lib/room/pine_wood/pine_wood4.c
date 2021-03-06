#include <std_mudlib.h>
inherit I_ROOM;

reset(arg) {
  if (!arg) {
    set_light(1);
    add_property("outdoors");
    add_property("authour", "brom");
    add_exit("north", "pine_wood7");
    add_exit("east",  "pine_wood5");
    add_exit("south", "pine_wood1");
    set_short("In the pine forest");
    set_long
      ("The trees here are over a hundred feet tall, making any climbing projects\n"+
       "impossible. The branches are very thick, and the crowns very dense. It is\n"+
       "chilly here.\n");
    add_item( ({ "tree", "trees", "pine", "pines" }),
	     "The pine trees reach for the heaven. From here it looks like they\n"+
	     "succeded in their grasping; you don't see the sky.\n");
    add_item( ({ "branch", "branches" }),
	     "Thick, and very high up.\n");
    add_item( ({ "crown", "crowns", "tree crown", "tree crowns" }),
	     "The tree crowns are very dense, putting this place into\n"+
	     "a chilly shadow, even at noon.\n");
  }
} /* reset */
