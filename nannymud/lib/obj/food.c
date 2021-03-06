/*
  
   This object is a standard food object and works
   like /obj/alco_drink.c or /obj/armour.c
  
   To use this you can do:
     inherit "obj/food";
   ......
   or,
   object ob;
   ob = clone_object("obj/food");
   ob->set_name("apple pie");
  
*  For more documentation look at /doc/build/food


   These functions are defined:

	   set_name(string)	To set the name of the item. For use in id().

   Two alternative names can be set with the calls:

	   set_alias(string) and set_alt_name(string)

	   set_short(string)	To set the short description.

	   set_long(string)	To set the long description.

	   set_value(int)	To set the value of the item.

	   set_weight(int)	To set the weight of the item.

	   set_strength(int)	To set the stuffness power of the item. If you
				don't wish the item to have stuffness powers
				just set this value to 0.

	   set_heal(int)	To set the healing power of the item. If you
				don't wish the item to have healing powers
				just set this value to 0.

	   set_eater_mess(string)
				To set the message that is written to the 
				player when he eats the item.

	   set_eating_mess(string)
				To set the message given to the surrounding
				players when this object is eaten.


	For an example of the use of this object, please read:
*	/doc/examples/apple_pie.c

*/

inherit "/obj/property";

string name, short, long, eating_mess, eater_mess, alias, alt_name;
int value, strength, weight, heal;

init()
{
	add_action("eat", "eat");
}

reset(arg)
{
	if (arg)
		return;

	weight = 1; 
	eater_mess = "Yum yum yum.\n"; 
	add_property("eatable");
	add_property("special_item");
}

prevent_insert()
{
  write("You don't want to ruin " + short + ".\n");
  return 1;
}

id(str)
{
  return  str == name || str == alt_name || str == alias || str == "fd2";
}

query_name()
{
  return name;
}

short()
{ 
	if(!short)
	    return name;

	return short;
}

long() 
{
	if(!long)
		write(short() + ".\n");
	else
		write(long);
}

get()
{
	return 1;
}

eat(str)
{
	object tp;

	tp = this_player();

	if (!str || !id(str))
		return 0;

	if(tp->query_level() * 8 < strength)
	{
		write("You realize even before trying that you'll never be able to eat all this.\n");
		return 1;
	}

	if (!tp->eat_food(strength))
		return 1;

        if (!heal) tp->heal_self(strength); /*This will be removed, I think.*/
	tp->heal_self(heal);
	write(eater_mess);
	if (eating_mess)
		say(capitalize(this_player()->query_name()) + eating_mess);
	else
		say(capitalize(this_player()->query_name()) + " eats " + short + ".\n");
	destruct(this_object());
	return 1;
}

query_heal() { return heal||strength; }
query_full() { return 1; }


min_cost()
{
	return 4 * strength + (strength * strength) / 10;
}

set_name(n) 
{ 
	name = n; 
}

set_short(s) 
{ 
	short = s; 
}

set_long(l) 
{ 
	long = l; 
}

set_value(v) 
{ 
	value = v; 
}

set_weight(w) 
{ 
	weight = w; 
}

set_heal(h) 
{
        heal = h;
}

set_strength(s) 
{
	strength = s; 
}

set_alias(a) 
{ 
	alias = a; 
}

set_alt_name(an) 
{ 
	alt_name = an; 
}

set_eating_mess(em) 
{ 
	eating_mess = em; 
}

set_eater_mess(em) 
{ 
	eater_mess = em; 
}

/*
 * Things that other objects might want to know.
 */

query_value() 
{ 
	if (value)
		return value; 
	else
		return min_cost();
}

query_weight() 
{ 
	return weight; 
}
