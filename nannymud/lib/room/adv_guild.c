/*  /room/adv_guild - the Adventurers' Guild
 *
 *  You will probably want to inherit this room if you build your own guild.
 *  WARNING: If you change this room, be careful - many objects
 *           inherit or call it, so you can easily break things.
 *
 * 960219 Brom        Made the join command work again after changes to /std.
 * 950904 Rohan       Changed to /std/room, real properties.
 * 950221 Taren       Modified to fit Adventurers Guild.
 *
 */


#define LB(X) sprintf("%-=78s\n", X)
#include <tune.h>

static inherit "/std/room";

reset(arg) 
{
  object ob;    

  if (arg) return;

  set_light(1);
  set_short("The Adventurers' Guild");
  set_long(LB("This is the Adventurers' Guild.\n" +
	      "Here the guildless players and the members of the adventurer's guild "+
	      "come to 'advance' in level and stats. If you want to become a member "+
	      "of the Adventurers Guild, type 'join guild'. You can also 'list' the quests "+
	      "and get some hints about the quests you have yet to solve. "+
	      "On the board players can put up notes about NannyMUD. If you want to "+
	      "read, type 'enter board'. "+
	      "There is an opening to the south, with some glimmering blue light "+
	      "in the doorway. To the west you can see a small chamber and there are "+
	      "some stairs leading up to the next floor."));
  add_exit("north","/room/vill_road2");
  add_exit("up","/room/adv_mud_board");
  add_exit("west","/room/holiday_board");
  add_exit("south","/room/adv_inner",0,"check_wiz");

  add_property("no_fight"); /* Replaces old query_haven() */
  add_property("no_magic");
  add_property("indoors");

  ob = clone_object("obj/quest_obj");
  ob->set_name("orc_slayer");
  ob->set_hint("Retrieve the Orc slayer from the evil orc shaman, and give it to Leo.\n");
  ob->set_short("Give the Orc slayer to Leo");
  ob->set_points(10);		/* Nanny-type quest points. */
  ob->set_other_wiz("banshee");
  move_object(ob, "room/quest_room");
    
  move_object("obj/chainbook", this_object());
    
  move_object(clone_object("obj/party"), this_object());
#if 1
  ob=clone_object("obj/newboard");
  ob->set_name("advguild");
#else
  ob=clone_object("obj/bboard");
  ob->set_name("bboard/advguild", 50);
  ob->set_write_level(3);
  ob->set_read_level(1);
#endif
  move_object(ob,this_object()); 

  add_command("join %s","@join()");
  add_command("cost","@cost_for_level()");
} /* reset */

init() 
{
  ::init();
  add_action("advance", "advance");
  add_action("advance", "wiz"); /* Milamber. */
  add_action("banish", "banish"); 
  add_action("list_quests", "list");

} /* init */

/*---------------------------------------------------------------------------*/

join(foo, str)
{
  object ob;

  if (!stringp(str) || sscanf(str, "%*sguild%*s") != 2)
  {
    notify_fail("But what do you wish to join?\n");
    return;
  }
  if(present("guild_mark",this_player())) 
    return write("You can not join the Adventurers Guild because you are already\nin another guild.\n"),1;
      
  ob=clone_object("/guilds/adv_guild/adv_guild");
  transfer(ob,this_player());
  ob->join_real2();
  write("You have joined the Adventurers guild.\n");
  say(this_player()->query_name()+" has joined the Adventurers guild.\n");
  return 1;
}
/*---------------------------------------------------------------------------*/

/* Made static to make banishfiles smaller: */

static int next_level;
static int next_exp;
string banished_by;
string title;         /* now with arrays. :) */
int level;
static int exp;
static object player_ob;

/* some minor changes by Iggy. */
/* get level asks get_next_exp() and  get_next_title() */

get_level(str)
{
  level = str;
  
  next_exp   = get_next_exp(level);
  next_level = level + 1 ;   
  title      = get_new_title(level);
}    

static string title_str; /* set by get_new_title */

get_new_title(level_minus_one)
{
  if (!title_str) {
    
    title_str = allocate(20);
    
    title_str[19]="the apprentice Wizard";
    title_str[18]="the adventurer";
    title_str[17]="the adventurer";
    title_str[16]="the adventurer";
    title_str[15]="the adventurer";
    title_str[14]="the adventurer";
    title_str[13]="the adventurer";
    title_str[12]="the adventurer";
    title_str[11]="the adventurer";
    title_str[10]="the adventurer";
    title_str[9]="the adventurer";
    title_str[8]="the small adventurer";
    title_str[7]="the inexperienced adventurer";
    title_str[6]="the experienced journeyman";
    title_str[5]="the journeyman";
    title_str[4]="the experienced wanderer";
    title_str[3]="the wanderer";
    title_str[2]="the simple wanderer";
    title_str[1]="the beginner";
    title_str[0]="the utter novice";
  }
  return title_str[level_minus_one];
} /* get_new_title */

/* made static to make banishfiles smaller */

static int exp_str;

/*  returns the next_exp. */
get_next_exp(str) 
{
/*  log_file("foo",str+"...\n");  */
  if(!exp_str){
    exp_str = allocate(23);
    
    exp_str[19]	= 1000000;
    exp_str[18]	=  666666; 
    exp_str[17]	=  444444;
    exp_str[16]	=  296296;
    exp_str[15]	=  197530;
    exp_str[14]	=  131687;
    exp_str[13]	=   97791;
    exp_str[12]	=   77791;
    exp_str[11]	=   58527;
    exp_str[10]	=   39018;
    exp_str[9]	=   26012;
    exp_str[8]	=   17341;
    exp_str[7]	=   11561;
    exp_str[6]	=    7707;
    exp_str[5]	=    5138;
    exp_str[4]	=    3425;
    exp_str[3]	=    2283;
    exp_str[2]	=    1522;
    exp_str[1]	=    1014;
    exp_str[0]	=     676;
  }
  return exp_str[str];
}

/*
 * This routine is called by monster, to calculate how much they are worth.
 * This value should not depend on the tuning.
 */
query_cost(l) {
  player_ob = this_player();
  level = l;
  if (level >= 20)
    return 1000000;
  get_level(level);
  return next_exp;
}

/*
 * Special function for other guilds to call. Arguments are current level
 * and experience points.
 */
query_cost_for_level(l, e) 
{
  level = l;
  exp = e;
  get_level(level);
  if (next_exp <= exp)
    return 0;
  return (next_exp - exp) * 1000 / EXP_COST;
}

cost_for_level()
{
  int cost, current_max;

  /*    
    if (this_player()->query_level() > 19) 
      return write("You don't need any stats immortal.\n");
  */
  player_ob = this_player();
  level = player_ob->query_level();
  
  cost = raise_cost(player_ob->query_str());
  if (cost)
    write("Str: " + cost + " experience points.\n");
  else
    write("Str: Not possible.\n");
  
  cost = raise_cost(player_ob->query_con());
  if (cost)
    write("Con: " + cost + " experience points.\n");
  else
    write("Con: Not possible.\n");
  
  cost = raise_cost(player_ob->query_dex());
  if (cost)
    write("Dex: " + cost + " experience points.\n");
  else
    write("Dex: Not possible.\n");
  
  cost = raise_cost(player_ob->query_int());
  if (cost)
    write("Int: " + cost + " experience points.\n");
  else
    write("Int: Not possible.\n");
  
  if (level >= 20) {
    write("You will have to seek other ways.\n");
    return 1;
  }
  
  /* If a player has solved ALL the quests, she, he (or it) can 
   * become a wizard! 
   * This works only if the quest point max is at least 1000. 
   */
  current_max = "room/quest_room"->count_all();
  if (player_ob->query_quest_points() >= current_max && current_max >= 1000) {
    write("Since you have solved all the quests, you can now advance to level 20\nby typing 'wiz'.\n");
    return 1;
  }
  
  exp = call_other(player_ob, "query_exp", 0);
  logging(player_ob,exp);
  get_level(level);
  if (next_exp <= exp) {
    write("It will cost you nothing to be promoted.\n");
    return 1;
  }
  write("It will cost you "); write((next_exp - exp) * 1000 / EXP_COST);
  write(" gold coins to advance to level "); write(next_level);
  write(".\n");
  return 1;
}

advance(arg)
{
  object guild_ob;
  int next_level;
  string name_of_player, title;
  
  if(!arg) arg="";
  if ((guild_ob = present("guild_mark",this_player())) && 
      !(guild_ob->query_allow_advance())
      && this_player()->query_level()<20)
    {
      write("Please advance in your own guild.\n");
      return 1;
    }				/* Mats 920826 */
  
  switch(arg)
    {
    case "con": raise_con(); return 1; 
    case "dex": raise_dex(); return 1;
    case "int": raise_int(); return 1;
    case "str": raise_str(); return 1;
    default:
      notify_fail("Advance what?\n");
      return 0;
      
    case "":
    case "level":
    }
  next_level=raise_level();
  title=get_new_title(next_level);
  player_ob->set_title(title);
  name_of_player=this_player()->query_name();
  if (next_level) say(name_of_player+ " is now level " + next_level + ".\n");
  if (next_level==0) return 1;
  
  if (next_level < 7) {
    write("You are now " + name_of_player + " " + title +
	  " (level " + next_level + ").\n");
    return 1;
  }
  if (next_level < 14) {
    write("Well done, " + name_of_player + " " + title +
	  " (level " + next_level + ").\n");
    return 1;
  }
  if (next_level < 20)  {
    write("Welcome to your new class, mighty one.\n" +
	  "You are now " + title + " (level " + next_level + ").\n");
  }
  if (next_level == 20) {
    write("A new Wizard has been born.\n");
    shout("A new Wizard has been born.\n");
    return 1;
  }
  return 1;
}

raise_level() {
  string name_of_player;
  int cost, current_max,qp_left;

  player_ob = this_player();
  name_of_player = player_ob->query_name();
  level = player_ob->query_level();

  if (level == -1) level = 0;

  exp = player_ob->query_exp();
  title = player_ob->query_title();
  if (level >= 20)
  {
    write("You are still "+title+"\n");
    return 0;
  }
  get_level(level);
  if (qp_left="room/quest_room"->count(1))
  {
    write("You have not solved enough quests to advance.\n");
    write("You need "+qp_left+" more questpoint"+(qp_left==1?"":"s")+" to advance to next level.\n");
    return 0;
  }
  if (level == 0)
    next_exp = exp;
  cost = (next_exp - exp) * 1000 / EXP_COST;
  
  /* If a player has solved ALL the quests, he (she, it) can become 
   * a wizard! 
   * This works only if the quest point max is at least 1000. 
   */
  current_max = "room/quest_room"->count_all();

  if ((query_verb() == "wiz") 
    && (player_ob->query_quest_points() >= current_max && current_max >= 1000))
  {
    write("Since you have solved all the quests, you are ready to become a wizard!\n");
    next_level = 20;
    cost = 0;
  }

  if (next_exp > exp)
  {
    if (player_ob->query_money() < cost)
    {
      write("You don't have enough gold coins.\n");
      return 0;
    }
    player_ob->add_money(- cost);
  }
  player_ob->set_level(next_level);
  if (exp < next_exp)
    player_ob->add_exp(next_exp - exp);

  logging(player_ob,exp);
  return next_level;
} /* raise_level */

raise_con()
{
  int lvl;
  
  if (too_high_average())
    return;
  lvl = this_player()->query_con();
  if (lvl >= 20) {
    alas("tough and endurable");
    return;
  }
  if (raise_cost(lvl, 1))
  {
    this_player()->set_con(lvl + 1);
    write("Ok.\n");
  }
  else
    write("You don't have enough experience.\n");
}

raise_dex()
{
  int lvl;
  
  if (too_high_average())
    return;
  lvl = this_player()->query_dex();
  if (lvl >= 20) {
    alas("skilled and vigorous");
    return;
  }
  if (raise_cost(lvl, 1))
  {
    this_player()->set_dex(lvl + 1);
    write("Ok.\n");
  }
  else
    write("You don't have enough experience.\n");
}

raise_int()
{
  int lvl;
  
  if (too_high_average())
    return;
  lvl = this_player()->query_int();
  if (lvl >= 20) {
    alas("knowledgeable and wise");
    return;
  }
  if (raise_cost(lvl, 1))
  {
    this_player()->set_int(lvl + 1);
    write("Ok.\n");
  }
  else
    write("You don't have enough experience.\n");
}

raise_str()
{
  int lvl;
  
  if (too_high_average())
    return;
  lvl = this_player()->query_str();
  if (lvl >= 20) {
    alas("strong and powerful");
    return;
  }
  if (raise_cost(lvl, 1))
  {
    this_player()->set_str(lvl + 1);
    write("Ok.\n");
  }
  else
    write("You don't have enough experience.\n");
}

/* 
* Compute cost for raising a stat one level. 'base' is the level that
* you have now, but never less than 1.
*/
raise_cost(base, action)
{
  int cost, saldo;
  
  if (base >= 20)
    return 0;
  cost = (get_next_exp(base) - get_next_exp(base - 1)) / STAT_COST;
  saldo = this_player()->query_exp() -
    get_next_exp(this_player()->query_level()- 1);
  if (!action)
    return cost;
  if (saldo < cost)
    return 0;
  this_player()->add_exp(-cost);
  return cost;
}

/*
* Banish a monster name from being used.
*/
banish(who) {
  level = call_other(this_player(), "query_level");
  if (level < 21)
    return 0;
  if (!who) {
    write("Who ?\n");
    return 1;
  }
  if (!call_other(this_player(), "valid_name", who))
    return 1;
  if ("/obj/daemon/fingerd"->playerp(who))
  {
    write("That name is already used.\n");
    return 1;
  }
  if (restore_object("banish/" + who)) {
    write("That name is already banished.\n");
    return 1;
  }
  banished_by = call_other(this_player(), "query_name");
  title = call_other(this_player(), "query_title");
  if (banished_by == "Someone") {
    write("You must not be invisible!\n");
    return 1;
  }
  save_object("banish/" + who);
  return 1;
}

check_wiz() {
  if (call_other(this_player(), "query_level", 0) < 20) {
    write("A strong magic force stops you.\n");
    say(call_other(this_player(), "query_name", 0) +
	" tries to go through the field, but fails.\n");
    return 1;
  }
  write("You wriggle through the force field...\n");
  return 0;
}

old_list_quests(num)
{
  int qnumber;
  if (num && (sscanf(num, "%d", qnumber) == 1))
    "room/quest_room"->list(qnumber);
  else
    "room/quest_room"->count(0);
  return 1;
}

list_quests(str)
{
  int qnumber;
  
  if (str)
    str = lower_case(str);
  if (!str || str == "all" || str == "all quests") {
    /* List all solved quests */
    call_other("room/quest_room", "more_list_solved", 0);
    call_other("room/quest_room", "more_count", 0);
  }
  else if (   str == "quests" || str == "unsolved" || str == "unsolved quests"
	   || str == "all unsolved" || str == "all unsolved quests") {
    /* List all unsolved quests */
    call_other("room/quest_room", "more_count", 0);
  }
  else if (   str == "solved" || str == "solved quests"
	   || str == "all solved" || str == "all solved quests") {
    /* List all solved quests */
    call_other("room/quest_room", "more_list_solved", 0);
  }
  else if (   sscanf(str, "%d", qnumber) == 1
	   || sscanf(str, "quest %d", qnumber) == 1
	   || sscanf(str, "number %d", qnumber) == 1
	   || sscanf(str, "quest number %d", qnumber) == 1
	   || sscanf(str, "unsolved %d", qnumber) == 1
	   || sscanf(str, "unsolved quest %d", qnumber) == 1
	   || sscanf(str, "unsolved quest number %d", qnumber) == 1) {
    /* Show info about one of the solved quests */
    call_other("room/quest_room", "more_list", qnumber);
  }
  else if (   sscanf(str, "solved %d", qnumber) == 1
	   || sscanf(str, "solved number %d", qnumber) == 1
	   || sscanf(str, "solved quest %d", qnumber) == 1
	   || sscanf(str, "solved quest number %d", qnumber) == 1) {
    /* Show info about one of the solved quests */
    call_other("room/quest_room", "more_list_solved", qnumber);
  }
  else {
    notify_fail("Which quest or quests do you want to list?\n" +
		"Use for example 'list', 'list unsolved' or 'list unsolved 2'.\n");
    return 0;
  }
  this_player()->flush();
  return 1;
}				/* list_quests */

int inorout=1;

alas(what) {
  write("Sorry " + gnd_prn() + ", but you are already as " + what +
	"\nas any");
  if (this_player()->query_gender() == 0)
    write("thing could possibly hope to get.\n");
  else
    write("one could possibly hope to get.\n");
}

/*
* Check that the player does not have too high average of the stats.
*/
too_high_average() {
  if ((this_player()->query_con() + this_player()->query_str() +
       this_player()->query_int() + this_player()->query_dex()) / 4 >=
      this_player()->query_level() + 3) {
    write("Sorry " + gnd_prn() +
	  ", but you are not of high enough level.\n");
    return 1;
  }
  return 0;
}

gnd_prn() {
  int gnd;
  
  gnd = this_player()->query_gender();
  if (gnd == 1) 
    return "sir";
  else if (gnd == 2)
    return "madam";
  else 
    return "best creature";
}

logging(p,e) {
  log_file("PLAYERS", p->query_name()+" ("+p->query_level()+") /"+
	   p->query_str()+","+
	   p->query_con()+","+
	   p->query_dex()+","+
	   p->query_int()+
	   "/: "+e+" xp, "+
	   p->query_money()+" gc, " +
	   p->query_quest_points() + " qp\n");
}

