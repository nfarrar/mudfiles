#include <sys/types.h>
#include <sys/stat.h>
/* #include <sys/dir.h> */
#include <fcntl.h>
#include <setjmp.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <memory.h>
#include <ctype.h>
#if defined(sun)
#include <alloca.h>
#endif
#if defined(M_UNIX) || defined(__sun__) || defined(__linux__)
#include <dirent.h>
#endif

#include "lint.h"
#include "config.h"
#include "stdio.h"
#include "lang.h"
#include "interpret.h"
#include "object.h"
#include "sent.h"
#include "wiz_list.h"
#include "exec.h"
#include "comm.h"

#include "acl.h"
extern int cfg_use_acls;

extern int errno;
extern int comp_flag;
extern int current_time;
extern int eval_cost;

extern int num_arrays;

char *inherit_file;

#if 0
extern int readlink PROT((char *, char *, int));
extern int symlink PROT((char *, char *));
#ifndef MSDOS
extern int lstat PROT((char *, struct stat *));
#else
#define lstat stat
#endif
extern int fchmod PROT((int, int));
#endif

char *last_verb;

extern int special_parse PROT((char *)),
    set_call PROT((struct object *, struct sentence *, int)),
    legal_path PROT((char *));

void pre_compile PROT((char *)),
    remove_interactive PROT((struct object *)),
    add_light PROT((struct object *, int)),
    add_action PROT((char *, char *, int)),
    add_verb PROT((char *, int)),
    print_local_commands(), ipc_remove(),
    show_info_about PROT((char *, char *, struct interactive *)),
    set_snoop PROT((struct object *, struct object *)),
    print_lnode_status PROT((int)),
    remove_all_players(), start_new_file PROT((FILE *)), end_new_file(),
    move_or_destruct PROT((struct object *, struct object *)),
#if TIME_TO_SWAP > 0
    load_ob_from_swap PROT((struct object *)),
#endif
    dump_malloc_data(),
    print_svalue PROT((struct svalue *)),
    debug_message_value(), show_free_list(),
    destruct2();

extern int d_flag;
extern int num_player;

struct object *obj_list, *obj_list_destruct, *master_ob;

extern struct wiz_list *back_bone_uid;

struct object *current_object;      /* The object interpreting a function. */
struct object *command_giver;       /* Where the current command came from. */
struct object *current_interactive; /* The user who caused this execution */

int num_parse_error;		/* Number of errors in the parser. */

void shutdowngame();

extern void flush_all_player_mess();

void omove_or_destruct(struct object *, struct object *, int);
void odestruct_object(struct svalue *, int);

struct variable *find_status(str, must_find)
    char *str;
    int must_find;
{
    int i;

    for (i=0; i < current_object->prog->num_variables; i++) {
	if (strcmp(current_object->prog->variable_names[i].name, str) == 0)
	    return &current_object->prog->variable_names[i];
    }
    if (!must_find)
	return 0;
    error("--Status %s not found in prog for %s\n", str,
	   current_object->name);
    return 0;
}

/*
 * Give the correct uid and euid to a created object.
 */
int give_uid_to_object(ob)
    struct object *ob;
{
    struct object *tmp_ob;
#ifdef COMPAT_MODE
    char wiz_name[100];
#else
    struct svalue *ret;
    char *creator_name;
#endif

    if (master_ob == 0)
	tmp_ob = ob;
    else {
	assert_master_ob_loaded();
	tmp_ob = master_ob;
    }
    
#ifdef COMPAT_MODE
    /* Is this object wizard defined ? */
    if (sscanf(ob->name, "players/%s", wiz_name) == 1)
    {
      char *np;
      np = strchr(wiz_name, '/');
      if (np)
	*np = '\0';
      ob->user = add_name(wiz_name);
    }
    else if (sscanf(ob->name, "guilds/%s", wiz_name) == 1)
    {
      /* Make guilds appear as wizards with capitalized name 
       * Profezzorn
       */
      
      char *np;
      np = strchr(wiz_name, '/');
      if (np)
	*np = '\0';
      *wiz_name=toupper(*wiz_name); /* capitalize */
      ob->user = add_name(wiz_name);
    } else {
      ob->user = 0;
    }
    ob->eff_user = ob->user;	/* Initial state */
    return 1;
#else

    if (!current_object || !current_object->user) {
	/*
	 * Only for the master and void object. Note that
	 * back_bone_uid is not defined when master.c is being loaded.
	 */
	ob->user = add_name("NONAME");
	ob->eff_user = 0;
	return 1;
    }

    /*
     * Ask master.c who the creator of this object is.
     */
    push_string(ob->name, STRING_CONSTANT);
    ret = apply("creator_file", tmp_ob, 1);
    if (!ret)
	error("No function 'creator_file' in master.c!\n");
    if (ret->type != T_STRING) {
	struct svalue arg;
	/* This can be the case for objects in /ftp and /open. */
	arg.type = T_OBJECT;
	arg.u.ob = ob;
	destruct_object(&arg);
	error("Illegal object to load.\n");
    }
    creator_name = ret->u.string;
    /*
     * Now we are sure that we have a creator name.
     * Do not call apply() again, because creator_name will be lost !
     */
    if (strcmp(current_object->user->name, creator_name) == 0) {
	/* 
	 * The loaded object has the same uid as the loader.
	 */
	ob->user = current_object->eff_user;
	ob->eff_user = current_object->eff_user;
	return 1;
    }

    if (strcmp(back_bone_uid->name, creator_name) == 0) {
	/*
	 * The object is loaded from backbone. This is trusted, so we
	 * let it inherit the value of eff_user.
	 */
	ob->user = current_object->eff_user;
	ob->eff_user = current_object->eff_user;
	return 1;
    }

    /*
     * The object is not loaded from backbone, nor from 
     * from the loading objects path. That should be an object
     * defined by another wizard. It can't be trusted, so we give it the
     * same uid as the creator. Also give it eff_user 0, which means that
     * player 'a' can't use objects from player 'b' to load new objects nor
     * modify files owned by player 'b'.
     *
     * If this effect is wanted, player 'b' must let his object do
     * 'seteuid()' to himself. That is the case for most rooms.
     */
    ob->user = add_name(creator_name);
    ob->eff_user = (struct wiz_list *)0;
    return 1;
#endif /* COMPAT_MODE */
}

static int legal_object_name(char *path)
{
   char *p;
   int i;
   if(!(p = strrchr(path, '#')) || !isdigit(p[1]))
      return 1;
   for(i = 2; p[i]; i++)
   {
      if(!isdigit(p[i]))
	 return 1;
   }
   return 0;
}

/*
 * Load an object definition from file. If the object wants to inherit
 * from an object that is not loaded, discard all, load the inherited object,
 * and reload again.
 *
 * In mudlib3.0 when loading inherited objects, their reset() is not called.
 *
 * Save the command_giver, because reset() in the new object might change
 * it.
 *
 * 
 */
struct object *load_object(lname, dont_reset,inherit_level)
    char *lname;
    int dont_reset,inherit_level;
{
    FILE *f;
    extern int total_lines;
    extern int approved_object;

    struct object *ob, *save_command_giver = command_giver;
    extern struct program *prog;
    extern char *current_file;
    struct stat c_st;
    int name_length;
    char real_name[200], name[200];

#ifndef COMPAT_MODE
    if (current_object && current_object->eff_user == 0)
	error("Can't load objects when no effective user.\n");
#endif

    if(inherit_level>15)
    {
      error("Too deep inherit level (probably a recursive inherit).\n");
    }
    /* Truncate possible .c in the object name. */
    /* Remove leading '/' if any. */
    while(lname[0] == '/')
	lname++;
    strncpy(name, lname, sizeof(name) - 1);
    name[sizeof name - 1] = '\0';
    name_length = strlen(name);
    if (name_length > sizeof name - 4)
	name_length = sizeof name - 4;
    name[name_length] = '\0';
    if (name[name_length-2] == '.' && name[name_length-1] == 'c') {
	name[name_length-2] = '\0';
	name_length -= 2;
    }
    /* Real name, that is including '.c' */
    (void)strcpy(real_name, name);
    (void)strcat(real_name, ".c");
    /*
     * Check if it's a legal name.
     */
    if(!legal_path(real_name) || !legal_object_name(name))
    {
       fprintf(stderr, "Illegal pathname: %s\n", real_name);
       error("Illegal path name.\n");
    }
    /*
     * First check that the c-file exists.
     */
    if(stat(real_name, &c_st) == -1)
    {
#ifdef O_VIRTUAL
       struct svalue *ret;
       /* Check for a virtual object */
       push_string(name, STRING_CONSTANT);
       if((ret = apply_master_ob("get_vobject", 1)) &&
	  ret->type == T_OBJECT && !(ret->u.ob->flags & O_DESTRUCTED))
       {
	  ob = ret->u.ob;
	  if(!(ob->flags & O_CLONE))
	  {
	     fprintf(stderr, "Bad virtual object: %s\n", ob->name);
	     error("Bad virtual object, not a clone.\n");
	  }
	  remove_object_hash(ob);
	  ob->flags &= ~O_CLONE;
	  ob->flags |= O_VIRTUAL;
	  free(ob->name);
	  ob->name = NULL;
	  if(dont_reset != 2)
	  {
	    ob->name = string_copy(name);
	    enter_object_hash(ob);
	    if(give_uid_to_object(ob) && !dont_reset)
	       reset_object(ob, -1);   /* Run reset(-1) but not __INIT() */
	  }
          if(d_flag > 1)
	     debug_message("--virtual object %s created\n", name);
	  command_giver = save_command_giver;
	  return ob;
       }
#endif
       fprintf(stderr, "Could not load descr for %s\n", real_name);
       error("Failed to load file.\n");
    }
    if (comp_flag)
	fprintf(stderr, " compiling %s ...", real_name);
    f = fopen(real_name, "r");
    if (f == 0) {
	perror(real_name);
	error("Could not read the file.\n");
    }
    start_new_file(f);
    current_file = string_copy(real_name);	/* This one is freed below */
    compile_file();
    end_new_file();
    if (comp_flag)
        fprintf(stderr, " done\n");
    update_compile_av(total_lines);
    total_lines = 0;
    (void)fclose(f);
    free(current_file);
    current_file = 0;
    /* Sorry, can't handle objects without programs yet. */
    if (inherit_file == 0 && (num_parse_error > 0 || prog == 0)) {
	if (prog)
	    free_prog(prog, 1);
	if (num_parse_error == 0 && prog == 0)
	    error("No program in object !\n");
	error("Error in loading object\n");
    }
    /*
     * This is an iterative process. If this object wants to inherit an
     * unloaded object, then discard current object, load the object to be
     * inherited and reload the current object again. The global variable
     * "inherit_file" will be set by lang.y to point to a file name.
     */
    if (inherit_file) {
	char *tmp = inherit_file;
	if (prog) {
	    free_prog(prog, 1);
	    prog = 0;
	}
	if (strcmp(inherit_file, name) == 0) {
	    free(inherit_file);
	    inherit_file = 0;
	    error("Illegal to inherit self.\n");
	}
	inherit_file = 0;
#if 1 /* MUDLIB3_NEED, It's very awkard to have to have a debug3 /JnA */
#ifdef COMPAT_MODE
	load_object(tmp, 0,0);
#else	
	load_object(tmp, 1,0);
#endif
#else
	load_object(tmp, 0,0);		/* Remove this feature for now */
#endif
        if(!find_object2(tmp)) {         /* Oros -- 920129 */
	  free(tmp);
	  error("Illegal to destruct inherited object in reset.\n");
	}
	free(tmp);
	if(dont_reset==2) /* Virtual ? */
	{
	  ob = load_object(name, dont_reset,0);
	}else{
	  ob = find_object(name);
	}
	if(!ob)
	   error("Hey! Where did that object go?\n");
	return ob;
    }
    ob = get_empty_object(prog->num_variables);
    /*
     * Can we approve of this object ?
     */
    if (approved_object || strcmp(prog->name, "std/object.c") == 0)
	ob->flags |= O_APPROVED;
    ob->prog = prog;
    if(dont_reset!=2)
    {
      ob->time_of_load = current_time;
      ob->next_all = obj_list;
#ifndef NO_BACKLINK_OBJS
      if(obj_list)
	 obj_list->prev_all = ob;
#endif
      obj_list = ob;
      ob->name = string_copy(name); /* Shared string is no good here */
      enter_object_hash(ob);	/* add name to fast object lookup table */

      if (give_uid_to_object(ob) && !dont_reset)
	reset_object(ob, 0);
#if TIME_TO_CLEAN_UP > 0
      if (!(ob->flags & O_DESTRUCTED) && function_exists("clean_up",ob)) {
	ob->flags |= O_WILL_CLEAN_UP;
      }
#endif
    }
    command_giver = save_command_giver;
    if (d_flag > 1 && ob)
	debug_message("--%s loaded\n", ob->name);
    return ob;
}

char *make_new_name(str)
    char *str;
{
    static int i;
    char *p = xalloc(strlen(str) + 10);

    (void)sprintf(p, "%s#%d", str, i);
    i++;
    return p;
}
    

/*
 * Save the command_giver, because reset() in the new object might change
 * it.
 */
struct object *clone_object(str1, copyvars)
    char *str1;
    int copyvars;
{
  struct object *ob, *new_ob;
  struct object *save_command_giver = command_giver;

#if 1
  if(copyvars)
     printf("DEBUG: clone_object(\"%s\", %d);\n", str1, copyvars);
#endif

  while(*str1=='/') str1++;
#ifndef COMPAT_MODE
  if (current_object && current_object->eff_user == 0)
    error("Illegal to call clone_object() with effective user 0\n");
#endif
  ob = find_object(str1);
  /*
   * If the object self-destructed...
   */
  if (ob == 0)
    return 0;
  if (/* ob->super || */ (ob->flags & O_CLONE))
    error("Cloning a bad object !\n");
    
  /* We do not want the heart beat to be running for unused copied objects */

  if (ob->flags & O_HEART_BEAT) 
    (void)set_heart_beat(ob, 0);

  if(ob->flags & O_PROGRAM_THROWN)
  {
#if 0
    struct stat c_st;
#endif
    new_ob=load_object(str1,2,0);
    new_ob->flags |= O_CLONE;
    new_ob->name = make_new_name(ob->name);

#if 0
    if (stat(new_ob->prog->name, &c_st) != -1)
    {
      if(c_st.st_mtime<ob->time_of_load)
      {
	/* We make a hack to put the program _back_ again */
	reference_prog(new_ob->prog,"put program back (clone_object)");
	free_prog(ob->prog,"put program back (clone_object)");
	ob->prog=new_ob->prog;
	ob->flags&=~O_PROGRAM_THROWN;
      }
    }
#endif
  }else{
    new_ob = get_empty_object(ob->prog->num_variables);
    new_ob->flags |= O_CLONE | (ob->flags & ( O_APPROVED | O_WILL_CLEAN_UP )) ;
    new_ob->name = make_new_name(ob->name);
#if 0
    if (ob->flags & O_APPROVED)
      new_ob->flags |= O_APPROVED;
#endif
    new_ob->prog = ob->prog;
    reference_prog (ob->prog, "clone_object");

  }
#ifdef COMPAT_MODE
  if (current_object && current_object->user && !ob->user)
    new_ob->user = current_object->user;
  else
    new_ob->user = ob->user;	/* Possibly a null pointer */
  new_ob->eff_user = new_ob->user; /* Init state */
#else 
  if (!current_object)
    fatal("clone_object() from no current_object !\n");
    
  give_uid_to_object(new_ob);

#endif
  /* There is a slight possibility it might already be linked */
  /* But only when virtual and toss code collides */
  if(!new_ob->next_all)
  {
    new_ob->next_all = obj_list;
#ifndef NO_BACKLINK_OBJS
    if(obj_list)
       obj_list->prev_all = new_ob;
#endif
    obj_list = new_ob;
    new_ob->time_of_load = current_time;
  }
  enter_object_hash(new_ob);	/* Add name to fast object lookup table */
#if 1
  if(copyvars)
  {

    int i;
    for(i = 0; i < new_ob->prog->num_variables && i < ob->prog->num_variables;
	i++)
    {
       assign_svalue(&new_ob->variables[i], &ob->variables[i]);
    }
  }
#endif
  reset_object(new_ob, 0); 
  command_giver = save_command_giver;
  /* Never know what can happen ! :-( */
  if (new_ob->flags & O_DESTRUCTED)
    return 0;
  return new_ob;
}



struct object *environment(arg)
    struct svalue *arg;
{
    struct object *ob = current_object;

    if (arg && arg->type == T_OBJECT)
	ob = arg->u.ob;
    else if (arg && arg->type == T_STRING)
	ob = find_object2(arg->u.string);
    if (ob == 0 || ob->super == 0 || (ob->super->flags & O_DESTRUCTED))
	return 0;
    if (ob->flags & O_DESTRUCTED)
	error("environment() off destructed object.\n");
    return ob->super;
}

#ifdef F_REPLACE
char *string_replace(struct svalue *str, struct svalue *del,struct svalue *to)
{
  char *str_s,*del_s,*to_s;
  int str_l,del_l,to_l;
  int e,num;
  char *buff,*q;
  
  /* find number of delimeters */

  str_s=str->u.string;
  del_s=del->u.string;
  to_s=to->u.string;


  str_l=strlen(str_s);
  del_l=strlen(del_s);
  to_l=strlen(to_s);

  if(!del_l || !str_l)  return string_copy(str_s);

  for (num=e=0; e<str_l;)
  {
    if (e+del_l<=str_l && !memcmp(str_s+e, del_s, del_l))
    {
      num++;
      e+=del_l;
    }else{
      e++;
    }
  }
  if(!num) 
    return string_copy(str_s);

  q = buff = (char *)xalloc(str_l+num*(to_l-del_l)+1);

  for (e=0;e<str_l;)
  {
    if(e+del_l<=str_l && !memcmp(str_s+e, del_s, del_l))
    {
      memcpy(q,to_s,to_l);
      e += del_l;
      q += to_l;
    } else {
      *(q++)=str_s[e++];
    }
  }
  buff[str_l + num * (to_l-del_l) ]='\0';
  return buff;
}
#endif

/*
 * Execute a command for an object. Copy the command into a
 * new buffer, because 'parse_command()' can modify the command.
 * If the object is not current object, static functions will not
 * be executed. This will prevent forcing players to do illegal things.
 *
 * Return cost of the command executed if success (> 0).
 * When failure, return 0.
 */
int command_for_object(str, ob)
    char *str;
    struct object *ob;
{
    struct svalue *ret;
    char buff[1000];
    int save_eval_cost = eval_cost - 1000;

    if (strlen(str) > sizeof(buff) - 1)
	error("Too long command.\n");
    if (ob == 0)
	ob = current_object;
    else if (ob->flags & O_DESTRUCTED)
	return 0;
    strncpy(buff, str, sizeof buff);
    buff[sizeof buff - 1] = '\0';

    if (command_giver && !(command_giver->flags & O_DESTRUCTED))
      push_object(command_giver);
    else
      push_number(0);
    
    push_object(ob);
    
    ret = apply_master_ob("valid_command", 2);
    if (ret && ret->type == T_NUMBER && ret->u.number == 0)
        return 0;
    
    if (parse_command(buff, ob))
	return eval_cost - save_eval_cost;
    else
	return 0;
}

/*
 * To find if an object is present, we have to look in two inventory
 * lists. The first list is the inventory of the current object.
 * The second list is all things that have the same ->super as
 * current_object.
 * Also test the environment.
 * If the second argument 'ob' is non zero, only search in the
 * inventory of 'ob'. The argument 'ob' will be mandatory, later.
 */

static struct object *object_present2 PROT((char *, struct object *));

struct object *object_present(v, ob)
    struct svalue *v;
    struct object *ob;
{
    struct svalue *ret;
    struct object *ret_ob;
    int specific = 0;

    if (ob == 0)
	ob = current_object;
    else
	specific = 1;
    if (ob->flags & O_DESTRUCTED)
	return 0;
    if (v->type == T_OBJECT) {
      if (specific) {
	if (v->u.ob->super == ob)
	  return v->u.ob;
	else
	  return 0;
      }
      if (v->u.ob->super == ob ||
	  (v->u.ob->super == ob->super && ob->super != 0))
	return v->u.ob->super;
      return 0;
    }
    ret_ob = object_present2(v->u.string, ob->contains);
    if (ret_ob)
	return ret_ob;
    if (specific)
	return 0;
    if (ob->super) {
	push_string(v->u.string, STRING_CONSTANT);
	ret = apply("id", ob->super, 1);
	if (ob->super->flags & O_DESTRUCTED)
	    return 0;
	if (ret && !(ret->type == T_NUMBER && ret->u.number == 0))
	    return ob->super;
	return object_present2(v->u.string, ob->super->contains);
    }
    return 0;
}


/* Supporting function for efun: _isclone(), 920612 pen@lysator.liu.se */
int is_cloned_object(v)
  struct svalue *v;
{
  struct object *ob;

  if (v->type == T_OBJECT)
    ob = v->u.ob;
  else
  {
    ob = find_object2(v->u.string);
    if (ob == 0)
      error("_isclone: Could not find %s\n", v->u.string);
  }

  if (ob->flags & O_DESTRUCTED)
    error("_isclone: Accessing destructed object\n");

  return ob->flags & O_CLONE;
}

static struct object *object_present2(str, ob)
    char *str;
    struct object *ob;
{
  extern void my_free();
  int onerror_num;
  struct svalue *ret;
  char *p;
  int count = 0, length;
  char *item;

  length = strlen(str);
  item = (char *) malloc(length+1);
  memcpy(item, str, length+1);
  p = item + length - 1;
  if (*p >= '0' && *p <= '9') {
    while(p > item && *p >= '0' && *p <= '9')
      p--;
    if (p > item && *p == ' ') {
      count = atoi(p+1) - 1;
      *p = '\0';
      length = p - item;	/* This is never used again ! */
    }
  }

  onerror_num = onerror(my_free, item);
  for (; ob; ob = ob->next_inv)
  {
    eval_cost++;
    push_string(item, STRING_CONSTANT);
    ret = apply("id", ob, 1);
    if (ob->flags & O_DESTRUCTED)
    {
      ob=0;
      break;
    }
    if (ret == 0 || (ret->type == T_NUMBER && ret->u.number == 0)) continue;
    if (count-- > 0)  continue;
    break;
  }
  remove_onerror(onerror_num);
  free(item);
  return ob;
}

/*
 * Remove an object. It is first moved into the destruct list, and
 * not really destructed until later. (see destruct2()).
 */
void destruct_object(v)
    struct svalue *v;
{
  odestruct_object(v, 0);
}

void sdestruct_object(v)
    struct svalue *v;
{
  odestruct_object(v, 1);
}


static int clear_indestflag(ob, msg)
  struct object *ob;
  char *msg;
{
  ob->flags &= ~O_IN_DESTRUCT;
  return 0;
}

/* pen 920112 - added "f" flag, if set don't call "exit", "query_weight" etc
   in object before destructing it */
void odestruct_object(v, f)
    struct svalue *v;
    int f;
{
    struct object *ob, *super;
    struct object **pp;
    int eid;
#ifdef NO_BACKLINK_OBJS
    int removed;
#endif

    if (v->type == T_OBJECT)
	ob = v->u.ob;
    else {
	ob = find_object2(v->u.string);
	if (ob == 0)
	    error("destruct_object: Could not find %s\n", v->u.string);
    }

    /*
    ** Added 930111 by pen@lysator.liu.se to prevent recursive destructs
    */
    if (ob->flags & O_IN_DESTRUCT && !f)
      return;

    /*
     * check if object has an efun socket referencing it for
     * a callback. if so, close the efun socket.
     */
#ifdef SOCKET
    if (ob->flags & O_EFUN_SOCKET) {
      close_referencing_sockets(ob);
    }
#endif /* SOCKET */

    if (ob->flags & O_DESTRUCTED)
      return;
    
    eid = onerror(clear_indestflag, ob);
    ob->flags |= O_IN_DESTRUCT;

#if TIME_TO_SWAP > 0
    if (ob->flags & O_SWAPPED)
	load_ob_from_swap(ob);
#endif
    remove_object_from_stack(ob);

    
    /*
     * If this is the first object being shadowed by another object, then
     * destruct the whole list of shadows.
     */
    if (ob->shadowed && !ob->shadowing)
    {
      struct svalue svp;
      struct object *ob2;
      
      ob2=ob->shadowed;
      ob->shadowed=0;
      ob2->shadowing=0;
      svp.type = T_OBJECT;
      svp.u.ob = ob2;
      odestruct_object(&svp,f);
    }
    
    /*
     * The chain of shadows is a double linked list. Take care to update
     * it correctly.
     */
    if (ob->shadowing)
	ob->shadowing->shadowed = ob->shadowed;
    if (ob->shadowed)
	ob->shadowed->shadowing = ob->shadowing;
    ob->shadowing = 0;
    ob->shadowed = 0;

    if (d_flag > 1)
	debug_message("Destruct object %s (ref %d)\n", ob->name, ob->ref);

    /*
    ** Added 930111 by pen@lysator.liu.se so we can have a way to
    ** react when being destructed
    */
    if (!f)
      (void)apply("_exit",ob,0);
    
    super = ob->super;
    if (super && !f) {
#ifdef COMPAT_MODE
	struct svalue *weight;
	/* Call exit in current room, if player or npc not in mudlib 3.0 */
	if((ob->flags & O_ENABLE_COMMANDS))
	{
	    push_object(ob);
	    (void)apply("exit",super,1);
	}
	
	weight = apply("query_weight", ob, 0);
	if (weight && weight->type == T_NUMBER)
	{
	    push_number(-weight->u.number);
	    (void)apply("add_weight", super, 1);
	}
#endif
    }
    
    if( f )     /* No wimpy mode for _destruct /Wing 920513 */
    {
      struct svalue svp;
      svp.type = T_OBJECT;
      while(ob->contains)
      {
	svp.u.ob = ob->contains;	    
	odestruct_object( &svp, f );
      }
    }
    else if (super == 0 || (super->flags & O_IN_DESTRUCT))
    {
      /*
       * There is nowhere to move the objects.
       */
      struct svalue svp;
      svp.type = T_OBJECT;
      while(ob->contains)
      {
	svp.u.ob = ob->contains;
	push_object(ob->contains);
	/* An error here will not leave destruct() in an inconsistent
	 * stage.
	 */
	apply_master_ob("destruct_environment_of",1);
	if (svp.u.ob == ob->contains)
	  odestruct_object(&svp, f);
      }
    } else
    {
      while(ob->contains)
	omove_or_destruct(ob->contains, super, f);
    }
    
    if ( ob->interactive )
    {
      struct object *save=command_giver;

	command_giver=ob;
	if (ob->interactive->ed_buffer) {
	    extern void save_ed_buffer();

	    save_ed_buffer();
	}
	flush_all_player_mess();
	command_giver=save;
    }
    set_heart_beat(ob, 0);
    /*
     * Remove us out of this current room (if any).
     * Remove all sentences defined by this object from all objects here.
     */
    if (ob->super)
    {
      if (ob->super->flags & O_ENABLE_COMMANDS)
	remove_sent(ob, ob->super);
      add_light(ob->super, - ob->total_light);
      for (pp = &ob->super->contains; *pp;) {
	if ((*pp)->flags & O_ENABLE_COMMANDS)
	  remove_sent(ob, *pp);
	if (*pp != ob)
	  pp = &(*pp)->next_inv;
	else
	  *pp = (*pp)->next_inv;
      }
    }

    /*
     * Now remove us out of the list of all objects.
     * This must be done last, because an error in the above code would
     * halt execution.
     */
#ifndef NO_BACKLINK_OBJS
    if(obj_list == ob)
       obj_list = ob->next_all;
    if(ob->next_all)
       ob->next_all->prev_all = ob->prev_all;
    if(ob->prev_all)
       ob->prev_all->next_all = ob->next_all;
    remove_object_hash(ob);
#else
    for(removed = 0, pp = &obj_list; *pp; pp = &(*pp)->next_all)
    {
      if(*pp != ob)
	continue;
      *pp = (*pp)->next_all;
      removed = 1;
      remove_object_hash(ob);
      break;
    }
    if(!removed)
        fatal("Failed to delete object.\n");
#endif

    if (ob->living_name)
	remove_living_name(ob);
    
    ob->super = 0;
    ob->next_inv = 0;
    ob->contains = 0;
    ob->flags &= ~O_IN_DESTRUCT;
    ob->flags &= ~O_ENABLE_COMMANDS;
    ob->next_all = obj_list_destruct;
#ifndef NO_BACKLINK_OBJS
    if(obj_list_destruct)
       obj_list_destruct->prev_all = ob;
#endif
    obj_list_destruct = ob;
    ob->flags |= O_DESTRUCTED;
    remove_onerror(eid);
}

/*
 * This one is called when no program is executing from the main loop.
 */
void destruct2(ob)
    struct object *ob;
{
    if (d_flag > 1) {
	debug_message("Destruct-2 object %s (ref %d)\n", ob->name, ob->ref);
    }
    if (ob->interactive)
	remove_interactive(ob);
    /*
     * We must deallocate variables here, not in 'free_object()'.
     * That is because one of the local variables may point to this object,
     * and deallocation of this pointer will also decrease the reference
     * count of this object. Otherwise, an object with a variable pointing
     * to itself, would never be freed.
     * Just in case the program in this object would continue to
     * execute, change string and object variables into the number 0.
     */
    if (ob->prog->num_variables > 0) {
	/*
	 * Deallocate variables in this object.
	 * The space of the variables are not deallocated until
	 * the object structure is freed in free_object().
	 */
	int i;
	for (i=0; i<ob->prog->num_variables; i++) {
	    free_svalue(&ob->variables[i]);
	    ob->variables[i].type = T_NUMBER;
	    ob->variables[i].u.number = 0;
	}
    }
    free_object(ob, "destruct_object");
}

#ifdef F_CREATE_WIZARD
/*
 * This is the efun create_wizard(). Create a home dir for a wizard,
 * and other files he may want.
 *
 * The real job is done by the master.c object, so the call could as well
 * have been done to it directly. But, this function remains for
 * compatibility.
 *
 * It should be replaced by a function in simul_efun.c !
 */
char *create_wizard(owner, domain)
    char *owner;
    char *domain;
{
    struct svalue *ret;
#if 0
    struct stat st;
    FILE *f;
    char cmd[200], lbuf[128];
    static char name[200], name2[200];	/* Ugly fix /Lars (static) */
    struct object *owner_obj;
#endif

    /*
     * Let the master object do the job.
     */
    push_constant_string(owner);
    push_constant_string(domain);
    push_object(current_object);
    ret = apply_master_ob("master_create_wizard", 3);
    if (ret && ret->type == T_STRING)
	return ret->u.string;
    return 0;
#if 0
    /*
     * Verify that it is a valid call of create_wizard(). This is done
     * by a function in master.c. It will take the calling object as
     * argument, and must return a non-zero value.
     */
    push_object(current_object);
    ret = apply_master_ob("verify_create_wizard", 1);
    if (ret == 0)
	error("No wizards allowed ! (because no verify_create_wizard() in master.c)\n");
    if (ret->type == T_NUMBER && ret->u.number == 0)
	error("Illegal use of create_wizard() !\n");

    /*
     * Even if the object that called create_wizard() is trusted, we won't
     * allow it to use funny names for the owner.
     */
    if (!legal_path(owner))
	error("Bad name to create_wizard: %s\n", owner);

    owner_obj = find_living_object(owner, 1);
    if (owner_obj == 0) {
	fprintf(stderr,
		"create_wizard: Could not find living object %s.\n", owner);
	return 0;
    }

    /*
     * Create the path to wizards home directory.
     */
    (void)sprintf(name, "%s/%s", PLAYER_DIR, owner);

    /*
     * If we are using domains, make a path to the domain.
     */
    if(domain) {
	(void)sprintf(name2, "%s/%s/%s", DOMAIN_DIR, domain, owner);
	fprintf(stderr, "name = %s, name2 =  %s\n", name, name2);

	/*
	 * If the directory already exists, we move it to the domain.
	 */
	if (stat(name, &st) == 0) {
	    if((st.st_mode & S_IFMT) == S_IFDIR) {
		rename(name, name2);
	    }
	} else {
	    if (mkdir(name2, 0777) == -1) {
		perror(name2);
		error("Could not mkdir %s\n", name2);
	    }
	}
    } else {
	if (stat(name, &st) == 0)
		error("Player %s already has a castle!\n", owner);

	else
	    if (mkdir(name, 0777) == -1) {
		perror(name);
		error("Could not mkdir %s\n", name);
	    }
    }

    /* add castle */
    if(domain) {
	(void)sprintf(name, "%s/%s/common/domain.c", DOMAIN_DIR, domain);
    } else {
	(void)sprintf(name, "%s/%s/%s", PLAYER_DIR, owner, "castle.c");
    }
    if(stat(name, &st) == 0) {
	fprintf(stderr, "castle file %s already exists.\n", name);
    } else {
	f = fopen(name, "w");
	if (f == NULL)
	    error("Could not create a castle file %s!\n", name);
	(void)fprintf(f, "#define NAME \"%s\"\n", domain ? domain : owner);
#ifdef CASTLE_ROOM
	(void)fprintf(f, "#define DEST \"%s\"\n", CASTLE_ROOM);
#else
	(void)fprintf(f, "#define DEST \"%s\"\n",
		      current_object->super->name);
#endif
	(void)fclose(f);
	(void)sprintf(cmd, "cat %s >> %s", DEFAULT_CASTLE, name);
	(void)system(cmd);
    }
    
    /*
     * Add this castle name to the list of files to be loaded.
     */
    f = fopen(INIT_FILE, "a");
    if (f == NULL)
	error("Could not add the new castle to the %s\n", INIT_FILE);
    (void)fprintf(f, "%s\n", name);
    (void)fclose(f);
    return name;
#endif
}
#endif /* F_CREATE_WIZARD */

/*
 * A message from an object will reach
 * all objects in the inventory,
 * all objects in the same environment and
 * the surrounding object.
 * Non interactive objects gets no messages.
 *
 * There are two cases to take care of. If this routine is called from
 * a player (indirectly), then the message goes to all in his
 * environment. Otherwise, the message goes to all in the current_object's
 * environment (as the case when called from a heart_beat()).
 *
 * If there is a second argument 'avoid_ob', then do not send the message
 * to that object.
 */

void say(v, avoid)
    struct svalue *v;
    struct vector *avoid;
{
    extern struct vector *order_alist PROT((struct vector *));
    struct vector *vtmpp;
    static struct vector vtmp = {
#ifndef NO_ARRAY_LINKS
       NULL,
       NULL,
#endif
       1, 0, 1,
#ifdef DEBUG
       1,
#endif
       (struct wiz_list *)NULL,
       { { T_POINTER } }
    };

    extern int assoc PROT((struct svalue *key, struct vector *));
    struct object *ob, *save_command_giver = command_giver;
    struct object *origin;
    char buff[256];
#define INITIAL_MAX_RECIPIENTS 50
    int max_recipients = INITIAL_MAX_RECIPIENTS;
    struct object *first_recipients[INITIAL_MAX_RECIPIENTS];
    struct object **recipients = first_recipients;
    struct object **curr_recipient = first_recipients;
    struct object **last_recipients =
	&first_recipients[INITIAL_MAX_RECIPIENTS-1];
#ifdef USE_OLD_MESSAGES
    struct object *save_again;
#endif
    static struct svalue stmp = { T_OBJECT };

    if (current_object->flags & O_ENABLE_COMMANDS)
	command_giver = current_object;
    else if (current_object->shadowing)
	command_giver = current_object->shadowing;
    if (command_giver) {
	origin = command_giver;
        if (avoid->item[0].type == T_NUMBER) {
            avoid->item[0].type = T_OBJECT;
            avoid->item[0].u.ob = command_giver;
            add_ref(command_giver, "ass to var");
        }
    } else
	origin = current_object;
    vtmp.item[0].u.vec = avoid;
    vtmpp = order_alist(&vtmp);
    avoid = vtmpp->item[0].u.vec;
    if (ob = origin->super) {
	if (ob->flags & O_ENABLE_COMMANDS || ob->interactive) {
	    *curr_recipient++ = ob;
	}
	for (ob = origin->super->contains; ob; ob = ob->next_inv) {
            if (ob->flags & O_ENABLE_COMMANDS || ob->interactive) {
                if (curr_recipient >= last_recipients) {
                    max_recipients <<= 1;
                    curr_recipient = (struct object **)
		      alloca(max_recipients * sizeof(struct object *));
                    memcpy((char*)curr_recipient, (char*)recipients,
                      max_recipients * sizeof(struct object *)>>1);
                    recipients = curr_recipient;
                    last_recipients = &recipients[max_recipients-1];
		    curr_recipient += (max_recipients>>1) - 1;
                }
                *curr_recipient++ = ob;
            }
	}
    }
    for (ob = origin->contains; ob; ob = ob->next_inv) {
	if (ob->flags & O_ENABLE_COMMANDS || ob->interactive) {
	    if (curr_recipient >= last_recipients) {
		max_recipients <<= 1;
		curr_recipient = (struct object **)alloca(max_recipients);
		memcpy((char*)curr_recipient, (char*)recipients,
		  max_recipients * sizeof(struct object *)>>1);
		recipients = curr_recipient;
		last_recipients = &recipients[max_recipients-1];
		curr_recipient += (max_recipients>>1) - 1;
	    }
	    *curr_recipient++ = ob;
	}
    }
    *curr_recipient = (struct object *)0;
    switch(v->type) {
    case T_STRING:
	strncpy(buff, v->u.string, sizeof buff);
	buff[sizeof buff - 1] = '\0';
	break;
    case T_OBJECT:
	strncpy(buff, v->u.ob->name, sizeof buff);
	buff[sizeof buff - 1] = '\0';
	break;
    case T_NUMBER:
	sprintf(buff, "%d", v->u.number);
	break;
    case T_POINTER:
	for (curr_recipient = recipients; ob = *curr_recipient++; ) {
	    extern void push_vector PROT((struct vector *));

	    if (ob->flags & O_DESTRUCTED) continue;
	    stmp.u.ob = ob;
	    if (assoc(&stmp, avoid) >= 0) continue;
	    push_vector(v->u.vec);
	    push_object(command_giver);
	    apply("catch_msg", ob, 2);
	}
	break;
    default:
	error("Invalid argument %d to say()\n", v->type);
    }
#ifdef USE_OLD_MESSAGES
    save_again = command_giver;
#endif
    for (curr_recipient = recipients; ob = *curr_recipient++; ) {
        if (ob->flags & O_DESTRUCTED) continue;
	stmp.u.ob = ob;
	if(assoc(&stmp, avoid) >= 0)
	   continue;
#ifndef USE_OLD_MESSAGES
	tell_object_low(ob, buff);
#else
	if (ob->interactive == 0) {
	    tell_npc(ob, buff);
	    continue;
	}
	command_giver = ob;
	add_message("%s", buff);
	command_giver = save_again;
#endif
    }
    free_vector(vtmpp);
    command_giver = save_command_giver;
}

/*
 * Send a message to all objects inside an object.
 * Non interactive objects gets no messages.
 * Compare with say().
 */

void tell_room(room, v, avoid)
    struct object *room;
    struct svalue *v;
    struct vector *avoid; /* has to be in alist order */
{
#ifdef USE_OLD_MESSAGES
    struct object *save_command_giver = command_giver;
#endif
    struct object *ob;
    char buff[4096];

    switch(v->type) {
    case T_STRING:
	strncpy(buff, v->u.string, sizeof buff);
	buff[sizeof buff - 1] = '\0';
	break;
    case T_OBJECT:
	strncpy(buff, v->u.ob->name, sizeof buff);
	buff[sizeof buff - 1] = '\0';
	break;
    case T_NUMBER:
	sprintf(buff, "%d", v->u.number);
	break;
    default:
	error("Invalid argument %d to tell_room()\n", v->type);
    }
    for (ob = room->contains; ob; ob = ob->next_inv) {
        int assoc PROT((struct svalue *key, struct vector *));
	static struct svalue stmp = { T_OBJECT, } ;

	stmp.u.ob = ob;
	if(assoc(&stmp, avoid) >= 0)
	   continue;
#ifndef USE_OLD_MESSAGES
	tell_object_low(ob, buff);
#else
	if(ob->interactive == 0) {
	    if (ob->flags & O_ENABLE_COMMANDS) {
		/*
		 * We want the monster code to have a correct this_player()
		 */
		command_giver = save_command_giver;
		tell_npc(ob, buff);
	    }
	    if (ob->flags & O_DESTRUCTED)
		break;
	    continue;
	}
	command_giver = ob;
	add_message("%s", buff);
#endif
    }
#ifdef USE_OLD_MESSAGES
    command_giver = save_command_giver;
#endif
}

void shout_string(str)
    char *str;
{
  struct object *save_command_giver = command_giver;
  int i;
  for (i = 0; i < num_player; i++) 
  {
#ifndef USE_OLD_MESSAGES
    tell_object_low(get_interactive_object(i), str);
#else
    command_giver = get_interactive_object(i);
    add_message("%s", str);
#endif
  }
  command_giver = save_command_giver;
}

struct object *first_inventory(arg)
    struct svalue *arg;
{
    struct object *ob;

    if (arg->type == T_STRING)
	ob = find_object(arg->u.string);
    else
	ob = arg->u.ob;
    if (ob == 0)
	error("No object to first_inventory()");
    if (ob->contains == 0)
	return 0;
    return ob->contains;
}

/*
 * This will enable an object to use commands normally only
 * accessible by interactive players.
 * Also check if the player is a wizard. Wizards must not affect the
 * value of the wizlist ranking.
 */
	/*  Enable_commands() handles both the efuns enable_commands() and
    disable_commands(). This version patches disable_commands() so
    that it only sets command_giver to 0 if it's equal to 
    current_object and also frees all actions. The old version
    crashed for disable_commands() because it always set command_giver
    to 0. Exchange the one in simulate.c
		/Gwendolyn@nannymud.lysator.liu.se 2000
 */



void enable_commands(int num)
{
   struct sentence *sen, *tmp;
   if(current_object->flags & O_DESTRUCTED)
      return;
   if(d_flag > 1)
   {
      debug_message("Enable commands %s (ref %d)\n",
	            current_object->name, current_object->ref);
   }
   if(num)
   {
      current_object->flags |= O_ENABLE_COMMANDS;
      command_giver = current_object;
   }
   else
   {
      current_object->flags &= ~O_ENABLE_COMMANDS;
      sen = current_object->sent;
      current_object->sent = (struct sentence *) 0;
      while(sen)
      {
         tmp = sen->next;
         free_sentence(sen);
         sen = tmp;
      }
      if(current_object == command_giver)
         command_giver = 0;
   }
}



/*
 * Set up a function in this object to be called with the next
 * user input string.
 */
int input_to(fun, flag, obj)
    char *fun;
    int flag;
#if 1
    struct object *obj;
#endif
{
    struct sentence *s;

    if (!command_giver || command_giver->flags & O_DESTRUCTED)
	return 0;
    s = alloc_sentence();
    if (set_call(command_giver, s, flag)) 
    {
      s->function = make_shared_string(fun);
#if 1
      s->ob = obj;
      add_ref(obj, "input_to");
#else
      s->ob = current_object;
      add_ref(current_object, "input_to");
#endif
      s->super=current_object;
      return 1;
    }
    free_sentence(s);
    return 0;
}

#define MAX_LINES 50

/*
 * This one is used by qsort in get_dir().
 */
static int pstrcmp(p1, p2)
    struct svalue *p1, *p2;
{
    return strcmp(p1->u.string, p2->u.string);
}

/*
 * List files in directory. This function do same as standard list_files did,
 * but instead writing files right away to player this returns an array
 * containing those files. Actually most of code is copied from list_files()
 * function.
 * Differences with list_files:
 *
 *   - file_list("/w"); returns ({ "w" })
 *
 *   - file_list("/w/"); and file_list("/w/."); return contents of directory
 *     "/w"
 *
 *   - file_list("/");, file_list("."); and file_list("/."); return contents
 *     of directory "/"
 */
struct vector *get_dir(path)
    char *path;
{
    struct vector *v;
    int i, count = 0;
    DIR *dirp;
    int namelen, do_match = 0;
#if defined(_AIX) || defined(M_UNIX) || defined(__sun__) || defined(__linux__)
    struct dirent *de;
#else
    struct direct *de;
#endif
    struct stat st;
    char *temppath;
    char *p, *tmp17;
    char *regexp = 0;

    if (!path)
	return 0;

    tmp17 = alloca(strlen(path)+1);
    strcpy(tmp17, path);
    if (cfg_use_acls)
    {
	p = strrchr(tmp17, '/');
	if (p && (strchr(p+1, '*') || strchr(p+1, '?')))
	{
	    /* We have wildcards */
	    *p++ = '\0';
	    path = acl_checkrights(tmp17, ACL_L, 0, 0);
	}
	else
	{
	    path = acl_checkrights(tmp17, ACL_L, 0, 0);
	    p = 0;
	}
    }
    else
    {
	p = 0;
#ifdef COMPAT_MODE
	path = check_file_name(path, 0);
#else
	path = check_valid_path(path, current_object->eff_user, "get_dir", 0);
#endif
    }

    if (path == 0)
	return 0;

    /*
     * We need to modify the returned path, and thus to make a
     * writeable copy.
     * The path "" needs 2 bytes to store ".\0".
     */

    temppath = (char *)alloca(strlen(path)+2+(p ? strlen(p)+1 : 0));
    if (strlen(path)<2)
    {
	temppath[0] = path[0] ? path[0] : '.';
	temppath[1] = '\0';
	if (p)
	{
	    strcat(temppath, "/");
	    strcat(temppath, p);
	}
	p = temppath;
    } else
    {
	strcpy(temppath, path);
	if (p)
	{
	    strcat(temppath, "/");
	    strcat(temppath, p);
	}
	
	/*
	 * If path ends with '/' or "/." remove it
	 */
	if ((p = strrchr(temppath, '/')) == 0)
	    p = temppath;
	
	if (p[0] == '/' && p[1] == '.' && p[2] == '\0' || 
	    p[0] == '/' && p[1] == '\0')
	    *p = '\0';
    }
    
    if (stat(temppath, &st) < 0)
    {
	if (*p == '\0')
	    return 0;
	regexp = (char *)alloca(strlen(p)+2);
	if (p != temppath) {
	    strcpy(regexp, p + 1);
	    *p = '\0';
	} else {
	    strcpy(regexp, p);
	    strcpy(temppath, ".");
	}
	do_match = 1;
    } else if (!S_ISDIR(st.st_mode))
    {
	if (*p == '/' && *(p + 1) != '\0')
	    p++;
	v = allocate_array(1);
	v->item[0].type = T_STRING;
	v->item[0].string_type = STRING_MALLOC;
	v->item[0].u.string = string_copy(p);

	return v;
    }

    if ((dirp = opendir(temppath)) == 0)
	return 0;

    /*
     *  Count files
     */
    while(de = readdir(dirp))
    {
#if defined(M_UNIX) || (defined(__sun__) && defined(__svr4__)) || defined(__linux__)
	namelen = strlen(de->d_name);
#else
	namelen = de->d_namlen;
#endif
	eval_cost++;
	if (!do_match && (strcmp(de->d_name, ".") == 0 ||
			  strcmp(de->d_name, "..") == 0))
	    continue;
	if (do_match && !match_string(regexp, de->d_name))
	    continue;
	count++;
	if ( count >= MAX_ARRAY_SIZE)
	    break;
    }
    /*
     * Make array and put files on it.
     */
    v = allocate_array(count);
    if (count == 0) {
	/* This is the easy case :-) */
	closedir(dirp);
	return v;
    }
    rewinddir(dirp);
    for(i = 0, de = readdir(dirp); i < count; de = readdir(dirp)) {
#if defined(M_UNIX) || (defined(__sun__) && defined(__svr4__)) || defined(__linux__)
        namelen = strlen(de->d_name);
#else
	namelen = de->d_namlen;
#endif
	if (!do_match && (strcmp(de->d_name, ".") == 0 ||
			  strcmp(de->d_name, "..") == 0))
	    continue;
	if (do_match && !match_string(regexp, de->d_name))
	    continue;
	de->d_name[namelen] = '\0';
	v->item[i].type = T_STRING;
	v->item[i].string_type = STRING_MALLOC;
	v->item[i].u.string = string_copy(de->d_name);
	i++;
    }
    closedir(dirp);
    /* Sort the names. */
    qsort((char *)v->item, count, sizeof v->item[0], pstrcmp);
    return v;
}

int tail(path)
    char *path;
{
    char buff[1000];
    FILE *f;
    struct stat st;
    int offset;

    if (cfg_use_acls)
    {
	path = acl_checkrights(path, ACL_R, 0, 0);
    }
    else
    {
#ifdef COMPAT_MODE
	path = check_file_name(path, 0);
#else
	path = check_valid_path(path, current_object->eff_user, "tail", 0);
#endif
    }

    if (path == 0)
        return 0;
    f = fopen(path, "r");
    if (f == 0)
	return 0;
    if (fstat(fileno(f), &st) == -1)
	fatal("Could not stat an open file.\n");
    offset = st.st_size - 54 * 20;
    if (offset < 0)
	offset = 0;
    if (fseek(f, offset, 0) == -1)
	fatal("Could not seek.\n");
    /* Throw away the first incomplete line. */
    if (offset > 0)
	(void)fgets(buff, sizeof buff, f);
    while(fgets(buff, sizeof buff, f)) {
	add_message("%s", buff);
    }
    fclose(f);
    return 1;
}

int print_file(path, start, len)
    char *path;
    int start, len;
{
    char buff[1000];
    FILE *f;
    int i;

    if (len < 0)
	return 0;

    if (cfg_use_acls)
    {
	path = acl_checkrights(path, ACL_R, 0, 0);
    }
    else
    {
#ifdef COMPAT_MODE
	path = check_file_name(path, 0);
#else
	path = check_valid_path(path, current_object->eff_user, "print_file", 0);
#endif
    }

    if (path == 0)
        return 0;
    if (start < 0)
	return 0;
    f = fopen(path, "r");
    if (f == 0)
	return 0;
    if (len == 0)
	len = MAX_LINES;
    if (len > MAX_LINES)
	len = MAX_LINES;
    if (start == 0)
	start = 1;
    for (i=1; i < start + len; i++) {
	if (fgets(buff, sizeof buff, f) == 0)
	    break;
	if (i >= start)
	    add_message("%s", buff);
    }
    fclose(f);
    if (i <= start)
	return 0;
    if (i == MAX_LINES + start)
	add_message("*****TRUNCATED****\n");
    return i-start;
}

int remove_file(path)
    char *path;
{
    if (path == 0)
        return 0;

    if (cfg_use_acls)
    {
	path = acl_checkrights(path, ACL_D, 1, 0);
    }
    else
    {
#ifdef COMPAT_MODE
	path = check_file_name(path, 1);
#else
	path = check_valid_path(path, current_object->eff_user, "remove_file", 1);
#endif
    }

    if (unlink(path) == -1)
        return 0;
    return 1;
}

void log_file(file, str)
    char *file, *str;
{
  int len;
  FILE *f;
  char file_name[100];
  struct stat st;

  if (strchr(file, '/') || file[0] == '.' || strlen(file) > 30)
    error("Illegal file name to log_file(%s)\n", file);
  sprintf(file_name, "log/%s", file);
#ifdef MAX_LOG_SIZE
  if (stat(file_name, &st) != -1 && st.st_size > MAX_LOG_SIZE) {
    char file_name2[sizeof file_name + 5];
    sprintf(file_name2, "%s.old", file_name);
    rename(file_name, file_name2); /* No panic if failure */
  }
#endif
  f = fopen(file_name, "a");
  if (f == 0)
    error("Cannot open file: log_file(%s)\n", file);
  len=strlen(str);
  eval_cost += len >> 10;
  fwrite(str, len, 1, f);
  fclose(f);
}

void print_svalue(arg)
    struct svalue *arg;
{
#ifndef USE_OLD_MESSAGES
   if(arg == 0)
      tell_object_low(command_giver, "<NULL>");
   else switch(arg->type)
   {
      case T_NUMBER:
      {
	 char psbuf[64];
	 sprintf(psbuf, "%d", arg->u.number);
	 tell_object_low(command_giver, psbuf);
	 break;
      }
      case T_STRING:
	 tell_object_low(command_giver, arg->u.string);
	 break;
      case T_OBJECT:
      {
	 char psbuf[2048];
	 if(strlen(arg->u.ob->name) > sizeof(psbuf) - 16)
	    strcpy(psbuf, "OBJ(...)");
	 else
	    sprintf(psbuf, "OBJ(%s)", arg->u.ob->name);
	 tell_object_low(command_giver, psbuf);
	 break;
      }
      case T_POINTER:
	 tell_object_low(command_giver, "<ARRAY>");
	 break;
      case T_MAPPING:
	 tell_object_low(command_giver, "<MAPPING>");
	 break;
      case T_LVALUE:
	 tell_object_low(command_giver, "<LVALUE>");
	 break;
      case T_INDEX_TYPE:
	 tell_object_low(command_giver, "<INDEX_TYPE>");
	 break;
      default:
	 tell_object_low(command_giver, "<UNKNOWN>");
	 break;
   }
#else
  if (arg == 0)
    add_message("<NULL>");
  else if (arg->type == T_STRING) {
    if (strlen(arg->u.string) > 9500) /* Not pretty */
      error("Too long string.\n");
    /* Strings sent to monsters are now delivered */
    if (command_giver && (command_giver->flags & O_ENABLE_COMMANDS) &&
	!command_giver->interactive)
      tell_npc(command_giver, arg->u.string);
    else
      add_message("%s", arg->u.string);
  } else if (arg->type == T_OBJECT)
    add_message("OBJ(%s)", arg->u.ob->name);
  else if (arg->type == T_NUMBER)
    add_message("%d", arg->u.number);
  else if (arg->type == T_POINTER)
    add_message("<ARRAY>");
  else
    add_message("<UNKNOWN>");
#endif
}

void do_write(arg)
    struct svalue *arg;
{
  struct object *save_command_giver = command_giver;
  if (command_giver == 0 && current_object->shadowing)
    command_giver = current_object;
  if (command_giver) {
    /* Send the message to the first object in the shadow list */
    while (command_giver->shadowing)
      command_giver = command_giver->shadowing;
  }
  print_svalue(arg);
  command_giver = save_command_giver;
}

/* Find an object. If not loaded, load it !
 * The object may selfdestruct, which is the only case when 0 will be
 * returned.
 */

struct object *find_object(str)
    char *str;
{
  struct object *ob;

  /* Remove leading '/' if any. */
  while(str[0] == '/')
    str++;
  ob = find_object2(str);
  if (ob)
    return ob;
  ob = load_object(str, 0,0);
  if (ob->flags & O_DESTRUCTED)	/* *sigh* */
    return 0;
#if TIME_TO_SWAP > 0
  if (ob && ob->flags & O_SWAPPED)
    load_ob_from_swap(ob);
#endif
  return ob;
}

/* Look for a loaded object. Return 0 if non found. */
struct object *find_object2(str)
    char *str;
{
  register struct object *ob;
  register int length;

  /* Remove leading '/' if any. */
  while(str[0] == '/')
    str++;
  /* Truncate possible .c in the object name. */
  length = strlen(str);
  if (length > 1 && str[length-2] == '.' && str[length-1] == 'c') {
    /* A new writreable copy of the name is needed. */
    char *p;
    p = (char *)alloca(strlen(str)+1);
    strcpy(p, str);
    str = p;
    str[length-2] = '\0';
  }
  if (ob = lookup_object_hash(str)) {

#if 0
    if (ob->flags & O_SUPER_INVIS) {
      struct svalue *arg1;
      if (!command_giver || !command_giver->interactive)
	return 0;
      arg1 = apply("query_level_sec", command_giver, 0);
      if (!arg1 || arg1->type != T_NUMBER || arg1->u.number < 22)
	return 0;
    }
#endif
#if TIME_TO_SWAP > 0
    if (ob->flags & O_SWAPPED)
      load_ob_from_swap(ob);
#endif
    return ob;
  }
  return 0;
}

struct object *find_and_load_object(str)
     char *str;
{
  struct object *ob;

  while(str[0] == '/')
    str++;

  ob = find_object2(str);

  if(ob == NULL)
  { 
    struct stat buf;
    int length;
    char *p;

    length = strlen(str);
    if (str[length-2] != '.' || str[length-1] != 'c') 
    {
      p = (char *)alloca(strlen(str)+3);
      strcpy(p, str);
      p[length] = '.';
      p[length+1] = 'c';
      p[length+2] = '\0';
    }
    else
      p = str;
    if(stat(p, &buf) == -1)
      ob = NULL;
    else
      ob = find_object(str);
  }
  return ob;
}


#if 0

void apply_command(com)
    char *com;
{
    struct value *ret;

    if (command_giver == 0)
	error("command_giver == 0 !\n");
    ret = apply(com, command_giver->super, 0);
    if (ret != 0) {
	add_message("Result:");
	if (ret->type == T_STRING)
	    add_message("%s\n", ret->u.string);
	if (ret->type == T_NUMBER)
	    add_message("%d\n", ret->u.number);
    } else {
	add_message("Error apply_command: function %s not found.\n", com);
    }
}
#endif /* 0 */

/*
 * Transfer an object.
 * The object has to be taken from one inventory list and added to another.
 * The main work is to update all command definitions, depending on what is
 * living or not. Note that all objects in the same inventory are affected.
 *
 * There are some boring compatibility to handle. When -o flag is specified,
 * several functions are called in some objects. This is dangerous, as
 * object might self-destruct when called.
 */
void move_object(item, dest)
    struct object *item, *dest;
{
    struct object **pp, *ob, *next_ob;
    struct object *save_cmd = command_giver;

#ifndef COMPAT_MODE
    if (item != current_object)
	error("Illegal to move other object than this_object()\n");
#endif

    /* Added 930111 pen@lysator.liu.se, this way we stop stupid
       wizards from catching a destruct and then moving people or
       things into the object being destructed (and thereby
       causing them to be destructed too */
    if (dest->flags & O_IN_DESTRUCT)
      return;

    if (dest->flags & O_OBJECT_LOCKED)
    {
	error("Target object is locked, can't move object\n");
	return;
    }
    
    /* Recursive moves are not allowed. */
    for (ob = dest; ob; ob = ob->super)
	if (ob == item)
	    error("Can't move object inside itself.\n");
    if (item->shadowing)
	error("Can't move an object that is shadowing.\n");
    
#if 0 /* Not now /Lars */
    /*
     * Objects must have inherited std/object if they are to be allowed to
     * be moved.
     */
#ifndef COMPAT_MODE
    if (!(item->flags & O_APPROVED) ||
		    !(dest->flags & O_APPROVED)) {
	error("Trying to move object where src or dest not inherit std/object\n");
	return;
    }
#endif    
#endif
#ifdef COMPAT_MODE
	/* This is only needed in -o mode. Otherwise, objects can only move
	 * themselves.
	 */
	dest->flags &= ~O_RESET_STATE;
	item->flags &= ~O_RESET_STATE;
#endif
    if (item->super) {
	int okey = 0;
		
	if (item->flags & O_ENABLE_COMMANDS) {
#ifdef COMPAT_MODE
		command_giver = item;
		push_object(item);
		(void)apply("exit", item->super, 1);
		if (item->flags & O_DESTRUCTED || dest->flags & O_DESTRUCTED)
		    return;	/* Give up */
#endif
	    remove_sent(item->super, item);
	}
	if (item->super->flags & O_ENABLE_COMMANDS)
	    remove_sent(item, item->super);
	add_light(item->super, - item->total_light);
	for (pp = &item->super->contains; *pp;) {
	    if (*pp != item) {
		if ((*pp)->flags & O_ENABLE_COMMANDS)
		    remove_sent(item, *pp);
		if (item->flags & O_ENABLE_COMMANDS)
		    remove_sent(*pp, item);
		pp = &(*pp)->next_inv;
		continue;
	    }
	    *pp = item->next_inv;
	    okey = 1;
	}
	if (!okey)
	    fatal("Failed to find object %s in super list of %s.\n",
		  item->name, item->super->name);
    }
    add_light(dest, item->total_light);
    item->next_inv = dest->contains;
    dest->contains = item;
    item->super = dest;
    /*
     * Setup the new commands. The order is very important, as commands
     * in the room should override commands defined by the room.
     * Beware that init() in the room may have moved 'item' !
     *
     * The call of init() should really be done by the object itself
     * (except in the -o mode). It might be too slow, though :-(
     */
    if (item->flags & O_ENABLE_COMMANDS) {
	command_giver = item;
	(void)apply("init", dest, 0);
	if ((dest->flags & O_DESTRUCTED) || item->super != dest) {
	    command_giver = save_cmd; /* marion */
	    return;
	}
    }
    /*
     * Run init of the item once for every present player, and
     * for the environment (which can be a player).
     */
    for (ob = dest->contains; ob; ob=next_ob) {
      eval_cost++;
      next_ob = ob->next_inv;
      if (ob == item)
	continue;
      if (ob->flags & O_DESTRUCTED)
	error("An object was destructed at call of init()\n");
      if (ob->flags & O_ENABLE_COMMANDS) {
	command_giver = ob;
	(void)apply("init", item, 0);
	if (dest != item->super) {
	  command_giver = save_cmd; /* marion */
	  return;
	}
      }
      if (item->flags & O_DESTRUCTED) /* marion */
	error("The object to be moved was destructed at call of init()\n");
      if((item->flags & O_ENABLE_COMMANDS) && !(ob->flags & O_DESTRUCTED))
      {
	command_giver = item;
	(void)apply("init", ob, 0);
	if (dest != item->super) {
	  command_giver = save_cmd; /* marion */
	  return;
	}
      }
    }
    if (dest->flags & O_DESTRUCTED) /* marion */
      error("The destination to move to was destructed at call of init()\n");
    if (dest->flags & O_ENABLE_COMMANDS) {
      command_giver = dest;
      (void)apply("init", item, 0);
    }
    command_giver = save_cmd;
}

/*
 * Every object as a count of number of light sources it contains.
 * Update this.
 */

void add_light(p, n)
    struct object *p;
    int n;
{
  if (n == 0)
    return;
  p->total_light += n;
  if (p->super)
    add_light(p->super, n);
}

struct sentence *sent_free = 0;
int tot_alloc_sentence;

#define SENTENCE_CHUNK_SIZE 1024
struct sentence_chunk
{
  struct sentence_chunk *next;
  struct sentence sentences[SENTENCE_CHUNK_SIZE];
};

static struct sentence_chunk *sentence_chunks=0;

struct sentence *alloc_sentence()
{
  struct sentence *p;

  if (!sent_free)
  {
    struct sentence_chunk *s;
    int e;
    s=(struct sentence_chunk *)xalloc(sizeof(struct sentence_chunk));
    s->next=sentence_chunks;
    sentence_chunks=s;
    sent_free=s->sentences;
    for(e=0;e<SENTENCE_CHUNK_SIZE-1;e++)
      s->sentences[e].next=& s->sentences[e+1];
    s->sentences[SENTENCE_CHUNK_SIZE-1].next=0;
    tot_alloc_sentence+=SENTENCE_CHUNK_SIZE;
  }
  p = sent_free;
  sent_free = sent_free->next;
  p->super = 0;
  p->verb = 0;
  p->function = 0;
  p->next = 0;
  return p;
}

#ifdef DEALLOCATE_MEMORY_AT_SHUTDOWN
void free_all_sent()
{
  struct sentence_chunk *s;
  for (;sentence_chunks;sentence_chunks=s) {
    s = sentence_chunks->next;
    free((char *)sentence_chunks);
  }
}
#endif

void free_sentence(p)
    struct sentence *p;
{
    if (p->function)
	free_string(p->function);
    p->function = 0;
    p->super = 0;
    if (p->verb)
	free_string(p->verb);
    p->verb = 0;
    p->next = sent_free;
    sent_free = p;
}

/*
 * Find the sentence for a command from the player.
 * Return success status.
 */
int player_parser(buff)
    char *buff;
{
  extern char* findstring PROT((char*));
  struct sentence *s;
  char *p, *quick;
  int length;
  struct object *save_current_object = current_object;
  struct object *current_super;
  char verb_copy[100];

  if (d_flag > 1)
    debug_message("cmd [%s]: %s\n", command_giver->name, buff);

  /* strip leading spaces /Profezzorn */
  while(*buff==' ') buff++;

  /* strip trailing spaces. */
  for (p = buff + strlen(buff) - 1; p >= buff; p--) {
    if (*p != ' ')
      break;
    *p = '\0';
  }
  if (buff[0] == '\0')  return 0;
  if (special_parse(buff))  return 1;

  p = strchr(buff, ' ');
  if (p == 0)
  {
    quick=findstring(buff);
    length = strlen(buff);
  }else{
    length = p - buff;
    
    /* don't look /Profezzorn */
    *p=0;
    quick=findstring(buff);
    *p=' ';
  }
  clear_notify();
  for (s=command_giver->sent; s; s = s->next)
  {
    struct svalue *ret;
    int len;
    struct object *command_object;
	
    if (s->verb == 0)
      error("An 'action' did something, but returned 0 or had an undefined verb.\n");
    if (s->no_space) {
      if(strncmp(buff, s->verb,strlen(s->verb)) != 0)
	continue;
    } else if (s->short_verb) {
      if (strncmp(s->verb, buff, strlen(s->verb)) != 0)
	continue;
    } else {
#if 0
      if (len != length) continue;
      if (strncmp(buff, s->verb, length))
	continue;
#else
      /* Shared string comparison /Profezzorn */
      if(s->verb != quick) continue;
#endif
    }
    /*
     * Now we have found a special sentence !
     */
    if (d_flag > 1)
      debug_message("Local command %s on %s\n", s->function, s->ob->name);
    if (length >= sizeof verb_copy)
      len = sizeof verb_copy - 1;
    else
      len = length;
    strncpy(verb_copy, buff, len);
    verb_copy[len] = '\0';
    last_verb = verb_copy;
    /*
     * If the function is static and not defined by current object,
     * then it will fail. If this is called directly from player input,
     * then we set current_object so that static functions are allowed.
     * current_object is reset just after the call to apply().
     */
    if (current_object == 0)
      current_object = s->ob;
    /*
     * Remember the object, to update score.
     */
    current_super = s->super;
    command_object = s->ob;
    if(s->no_space) {
      push_constant_string(&buff[strlen(s->verb)]);
      ret = apply(s->function,s->ob, 1);
    } else if (buff[length] == ' ') {
      push_constant_string(&buff[length+1]);
      ret = apply(s->function, s->ob, 1);
    } else {
      ret = apply(s->function, s->ob, 0);
    }
    if (current_object->flags & O_DESTRUCTED) {
      /* If disable_commands() were called, then there is no
       * command_giver any longer.
       */
      if (command_giver == 0 || (command_giver->flags & O_DESTRUCTED))
	return 1;
      s = command_giver->sent;	/* Restart :-( */
    }
    if(s && (s->super != current_super))  {
      if (command_giver == 0)
	return 1;
      s = command_giver->sent;	/* Restart :-( */
    }
    current_object = save_current_object;
    last_verb = 0;
    /* If we get fail from the call, it was wrong second argument. */
    if (ret && ret->type == T_NUMBER && ret->u.number == 0)
      continue;
    if(s && command_object->user && command_giver && command_giver->interactive)
       command_object->user->score++;
    if (ret == 0)
      add_message("Error: function %s not found.\n", s->function);
    break;
  }
  if (s == 0) {
    notify_no_command();
    return 0;
  }
  return 1;
}

/*
 * Associate a command with function in this object.
 * The optional second argument is the command name. If the command name
 * is not given here, it should be given with add_verb().
 *
 * The optinal third argument is a flag that will state that the verb should
 * only match against leading characters.
 *
 * The object must be near the command giver, so that we ensure that the
 * sentence is removed when the command giver leaves.
 *
 * If the call is from a shadow, make it look like it is really from
 * the shadowed object.
 */
void add_action(str, cmd, flag)
    char *str, *cmd;
    int flag;
{
  struct sentence *p;
  struct object *ob;

  if (str[0] == ':')
    error("Illegal function name: %s\n", str);
  if (current_object->flags & O_DESTRUCTED)
    return;
  ob = current_object;
  while(ob->shadowing)
    ob = ob->shadowing;
  if (command_giver == 0 || (command_giver->flags & O_DESTRUCTED))
    return;

  if (ob != command_giver && ob->super != command_giver &&
      ob->super != command_giver->super && ob != command_giver->super)
    error("add_action from object that was not present.\n");

  if (d_flag > 1)
    debug_message("--Add action %s\n", str);

#ifdef COMPAT_MODE
  if (strcmp(str, "exit") == 0)
    error("Illegal to define a command to the exit() function.\n");
#endif

  p = alloc_sentence();
  p->function = make_shared_string(str);
  p->ob = ob;
  p->super = command_giver;
  p->next = command_giver->sent;
  p->short_verb = flag;
  p->no_space = 0;
  if (cmd)
  {
    p->verb = make_shared_string(cmd);
  }else{
    p->verb = 0;
  }
  command_giver->sent = p;
}

void add_verb(str, no_space)
    char *str;
    int no_space;
{
  if (command_giver == 0 || (command_giver->flags & O_DESTRUCTED))
    return;
  if (command_giver->sent == 0)
    error("No add_action().\n");
  if (command_giver->sent->verb != 0)
    error("Tried to set verb again.\n");
  command_giver->sent->verb = make_shared_string(str);
  command_giver->sent->no_space = no_space;
  if (d_flag > 1)
    debug_message("--Adding verb %s to action %s\n", str,
		  command_giver->sent->function);
}

/*
 * Remove all commands (sentences) defined by object 'ob' in object
 * 'player'
 */
void remove_sent(ob, player)
    struct object *ob, *player;
{
  struct sentence **s;

  for (s= &player->sent; *s;) {
    struct sentence *tmp;
    if ((*s)->ob == ob) {
      if (d_flag > 1)
	debug_message("--Unlinking sentence %s\n", (*s)->function);
      tmp = *s;
      *s = tmp->next;
      free_sentence(tmp);
    } else {
      s = &((*s)->next);
    }
  }
}

char debug_parse_buff[50]; /* Used for debugging */

/*
 * Hard coded commands, that will be available to all players. They can not
 * be redefined, so the command name should be something obscure, not likely
 * to be used in the game.
 */
int special_parse(buff)
    char *buff;
{
#ifdef DEBUG
  strncpy(debug_parse_buff, buff, sizeof debug_parse_buff);
  debug_parse_buff[sizeof debug_parse_buff - 1] = '\0';
#endif
  switch(buff[0])
  {
  case 'm':
    if (strcmp(buff, "malloc showfreelist") == 0) {
#if defined(MALLOC_smalloc)
      show_free_list();
      add_message("Large free list dumped on lpmud.log.\n");
#endif
      return 1;
    }
    if (strcmp(buff, "malloc") == 0) {
#if defined(MALLOC_smalloc)
      dump_malloc_data();
#endif
#ifdef MALLOC_gmalloc
      add_message("Using Gnu malloc.\n");
#endif
#ifdef MALLOC_sysmalloc
      add_message("Usage system standard malloc.\n");
#endif
      return 1;
    }
    break;

  case 'd':
    if(buff[1]==0)
    {
      (void)strcpy(buff, "down");
      return 0;
    }
    if (strcmp(buff, "dumpallobj") == 0)
    {
      if(command_giver &&
	 !(command_giver->flags & O_DESTRUCTED))
      {
	if(command_giver->interactive)
	{
	  struct svalue *ret;
	  ret=apply("query_level",command_giver,0);
	  if(ret && ret->type == T_NUMBER && ret->u.number > 20)
	  {
	    dumpstat();
	    return 1;
	  }
	}else{
	  dumpstat();
	  return 1;
	}
      }
    }

#if defined(MALLOC_malloc) || defined(MALLOC_smalloc)
    if (strcmp(buff, "debugmalloc") == 0)
    {
      extern int debugmalloc;
      debugmalloc = !debugmalloc;
      if (debugmalloc)
	add_message("On.\n");
      else
	add_message("Off.\n");
      return 1;
    }
#endif
    break;

  case 's':
    if (buff[1]==0)
    {
      (void)strcpy(buff, "south");
      return 0;
    }

    if (strcmp(buff, "sw") == 0) {
      (void)strcpy(buff, "southwest");
      return 0;
    }
    if (strcmp(buff, "se") == 0) {
      (void)strcpy(buff, "southeast");
      return 0;
    }
#ifndef NO_PROGRAM_STATUS
    if(!strcmp(buff, "status programs"))
    {
       extern int total_num_prog_blocks, total_prog_block_size;

       extern int arg_bytes_won;

       extern int progstruct_bytes;
       extern int byte_code_bytes;
       extern int function_bytes;
       extern int strings_bytes;
       extern int variables_bytes;
       extern int line_number_bytes;
       extern int inherits_bytes;
#ifdef DO_SAVE_ARG_TYPES
       extern int arg_types_bytes;
       extern int arg_index_bytes;
#endif

       add_message("Programs:\t%d\n", total_num_prog_blocks);
       add_message("   Program structs:     %5d Kb\n",
		   progstruct_bytes / 1024);
       add_message("   Bytecode blocks:     %5d Kb\n",
		   byte_code_bytes / 1024);
       add_message("   Function blocks:     %5d Kb\n",
		   function_bytes / 1024);
       add_message("   String blocks:       %5d Kb\n",
		   strings_bytes / 1024);
       add_message("   Variable blocks:     %5d Kb\n",
		   variables_bytes / 1024);
       add_message("   Linenumber blocks:   %5d Kb\n",
		   line_number_bytes / 1024);
       add_message("   Inherit blocks:      %5d Kb\n",
		   inherits_bytes / 1024);
#ifdef DO_SAVE_ARG_TYPES
       add_message("   Argtype blocks:      %5d Kb\n",
		   arg_types_bytes / 1024);
       add_message("   Argindex blocks:     %5d Kb\n",
		   arg_index_bytes / 1024);
#endif
       add_message("                        -----\n");
       add_message("   Total:               %5d Kb\n",
		   total_prog_block_size / 1024);

       add_message("\n   Won from args:       %5d Kb\n",
		   arg_bytes_won / 1024);
       return 1;
    }
#endif
    if (strcmp(buff, "status") == 0 || strcmp(buff, "status tables") == 0)
    {
      int tot, res, verbose = 0;
      extern char *reserved_area;
      extern int tot_alloc_sentence, tot_alloc_object,
      tot_alloc_object_size,
#if TIME_TO_SWAP > 0
      num_swapped, total_bytes_swapped,
#endif
      total_array_size;
      extern int total_num_prog_blocks, total_prog_block_size;

#ifdef COMM_STAT
      extern int add_message_calls,inet_packets,inet_volume;
#endif
      extern int bytes_won;
      extern int bytes_optimized;
      extern int bytes_thrown;

      if (strcmp(buff, "status tables") == 0)
	verbose = 1;
      if (reserved_area)
	res = RESERVED_SIZE;
      else
	res = 0;
#ifdef COMM_STAT
      if (verbose)
	add_message("Calls to add_message: %d  Packets: %d  Average packet size: %f\n\n",add_message_calls,inet_packets,(float)inet_volume/inet_packets);
#endif
      if(verbose)
      {
	add_message("Bytes won by peephole optimizer : %d\n",bytes_won);
	add_message("Bytes won by other optimizations: %d\n",bytes_optimized);
	add_message("Bytes thrown away in null progs.: %d\n\n",bytes_thrown);
      }

      if (!verbose) {
	add_message("Sentences:\t\t\t%8d %8d\n", tot_alloc_sentence,
		    tot_alloc_sentence * sizeof (struct sentence));
	add_message("Objects:\t\t\t%8d %8d\n",
		    tot_alloc_object, tot_alloc_object_size);
	add_message("Arrays:\t\t\t\t%8d %8d\n", num_arrays,
		    total_array_size);
	add_message("Prog blocks:\t\t\t%8d %8d (%d swapped, %d Kbytes)\n",
		    total_num_prog_blocks, total_prog_block_size,
#if TIME_TO_SWAP > 0
		    num_swapped, total_bytes_swapped / 1024
#else
		    0, 0
#endif
		    );
	add_message("Memory reserved:\t\t\t %8d\n", res);
      }
      if (verbose)
	stat_living_objects();
      tot = total_prog_block_size +
	total_array_size +
	  tot_alloc_object_size+
	    show_otable_status(verbose) +
	      heart_beat_status(verbose) +
		add_string_status(verbose) +
		  print_call_out_usage(verbose) +
		    res;

      if (!verbose) {
	add_message("\t\t\t\t\t --------\n");
	add_message("Total:\t\t\t\t\t %8d\n", tot);
      }
      return 1;
    }

    break;


  case 'e':
    if(buff[1]==0)
    {
      (void)strcpy(buff, "east");
      return 0;
    }
    break;

  case 'w':
    if(buff[1]==0)
    {
      (void)strcpy(buff, "west");
      return 0;
    }
    break;

  case 'n':
    if(buff[1]==0)
    {
      (void)strcpy(buff, "north");
      return 0;
    }
    if (strcmp(buff, "nw") == 0) {
      (void)strcpy(buff, "northwest");
      return 0;
    }
    if (strcmp(buff, "ne") == 0) {
      (void)strcpy(buff, "northeast");
      return 0;
    }
    break;

  case 'u':
    if(buff[1]==0)
    {
      (void)strcpy(buff, "up");
      return 0;
    }
    break;
  }
  return 0;
}


void print_local_commands() {
    struct sentence *s;

    add_message("Current local commands:\n");
    for (s = command_giver->sent; s; s = s->next)
	add_message("%s ", s->verb);
    add_message("\n");
}

struct vector *get_action(ob, verb)
  struct object *ob;
  char *verb;
{
  struct vector *v;
  struct sentence *s;

  for (s = ob->sent; s; s = s->next)
    if (strcmp(verb, s->verb) == 0)
      break;

  if (!s)
    return NULL;

  v = allocate_array(3);
  v->item[0].type = T_NUMBER;
  v->item[0].u.number = s->short_verb;

  v->item[1].type = T_STRING;
  v->item[1].string_type = STRING_MALLOC;
  v->item[1].u.string = string_copy(s->function);
  
  v->item[2].type = T_OBJECT;
  v->item[2].u.ob = s->ob;
  add_ref(s->ob, "get_action");

  return v;
}

struct vector *get_all_actions(ob)
  struct object *ob;
{
  struct vector *v;
  struct sentence *s;
  int num;

  num = 0;
  for (s = ob->sent; s; s = s->next)
    num++;

  if (num == 0)
    return NULL;
  
  v = allocate_array(num);
  num = 0;
  for (s = ob->sent; s; s = s->next)
  {
    v->item[num].type = T_STRING;
    v->item[num].string_type = STRING_MALLOC;
    v->item[num].u.string = string_copy(s->verb);
    num++;
  }
    
  return v;  
}


/*VARARGS1*/
void fatal(fmt, a, b, c, d, e, f, g, h)
    char *fmt;
    int a, b, c, d, e, f, g, h;
{
    static int in_fatal = 0;
    /* Prevent double fatal. */
    if (in_fatal)
	abort();
    in_fatal = 1;
    (void)fprintf(stderr, fmt, a, b, c, d, e, f, g, h);
    fflush(stderr);
    if (current_object)
	(void)fprintf(stderr, "Current object was %s\n",
		      current_object->name);
    debug_message(fmt, a, b, c, d, e, f, g, h);
    if (current_object)
	debug_message("Current object was %s\n", current_object->name);
    debug_message("Dump of variables:\n");
    (void)dump_trace(1);
    abort();
}

int num_error = 0;

/*
 * Error() has been "fixed" so that users can catch and throw them.
 * To catch them nicely, we really have to provide decent error information.
 * Hence, all errors that are to be caught
 * (error_recovery_context_exists == 2) construct a string containing
 * the error message, which is returned as the
 * thrown value.  Users can throw their own error values however they choose.
 */

/*
 * This is here because throw constructs its own return value; we dont
 * want to replace it with the system's error string.
 */

void throw_error() {
    extern int error_recovery_context_exists;
    extern jmp_buf error_recovery_context;
    if (error_recovery_context_exists > 1) {
	longjmp(error_recovery_context, 1);
	fatal("Throw_error failed!");
    }
    error("Throw with no catch.\n");
}

static char emsg_buf[2000];

#define NUM_ONERRORS 1000
int n_onerrhandlers = 0;
int onerror_level=0;
static int (*onerrhandler[NUM_ONERRORS])();
static void *onerrextra[NUM_ONERRORS];

/*
** Set up an error handler
*/
int onerror(handler, extra)
  int (*handler)();
  void *extra;
{
  if (n_onerrhandlers >= NUM_ONERRORS)
    error("onerror: Out of onerror slots (%d)!\n", NUM_ONERRORS);

  onerrhandler[n_onerrhandlers] = handler;
  onerrextra[n_onerrhandlers] = extra;

  return n_onerrhandlers++;
}


/*
** Remove all error handlers up to (and including) the "eid" one
*/
int remove_onerror(eid)
  int eid;
{
  n_onerrhandlers = eid;
}

void reset_onerror()
{
  n_onerrhandlers = 0;
  onerror_level=0;
}  


/*VARARGS1*/
void error(fmt, a, b, c, d, e, f, g, h)
    char *fmt;
    int a, b, c, d, e, f, g, h;
{
    extern int error_recovery_context_exists;
    extern jmp_buf error_recovery_context;
    extern struct object *current_heart_beat;
    extern struct svalue catch_value;
    extern int eval_cost;
    char *object_name;

    sprintf(emsg_buf+1, fmt, a, b, c, d, e, f, g, h);
    emsg_buf[0] = '*';	/* all system errors get a * at the start */

    /* Call all the error handlers, last first */
    while (--n_onerrhandlers >= onerror_level)
      (*onerrhandler[n_onerrhandlers])(onerrextra[n_onerrhandlers], emsg_buf);
    
    if (error_recovery_context_exists > 1) { /* user catches this error */
	struct svalue v;
	v.type = T_STRING;
	v.u.string = emsg_buf;
	v.string_type = STRING_MALLOC;	/* Always reallocate */
	assign_svalue(&catch_value, &v);
   	longjmp(error_recovery_context, 1);
   	fatal("Catch() longjump failed");
    }
    eval_cost=0;
    num_error++;
    if (num_error > 1)
	fatal("Too many simultaneous errors.\n");
    debug_message("%s", emsg_buf+1);
    if (current_object) {
	debug_message("program: %s, object: %s line %d\n",
		    current_prog ? current_prog->name : "",
		    current_object->name,
		    get_line_number_if_any());
	if (current_prog)
	    save_error(emsg_buf, current_prog->name,
		       get_line_number_if_any());
    }
    object_name = dump_trace(0);
    fflush(stdout);
    if (object_name) {
	struct object *ob;
	ob = find_object2(object_name);
	if (!ob) {
	    if (command_giver)
		add_message("error when executing program in destroyed object %s\n",
			    object_name);
	    debug_message("error when executing program in destroyed object %s\n",
			object_name);
	}
    }
    if (command_giver && command_giver->interactive) {
	struct svalue *v = 0;

	num_error--;
	/* 
	 * The stack must be brought in a usable state. After the
	 * call to reset_machine(), all arguments to error() are invalid,
	 * and may not be used any more. The reason is that some strings
	 * may have been on the stack machine stack, and has been deallocated.
	 */
	reset_machine (0);
	push_constant_string("error messages");
	v = apply_master_ob("query_player_level", 1);
	num_error++;
	if (v && v->u.number != 0) {
	    add_message("%s", emsg_buf+1);
	    if (current_object)
		add_message("program: %s, object: %s line %d\n",
			    current_prog ? current_prog->name : "",
			    current_object->name,
			    get_line_number_if_any());
	} else {
	    add_message("Your sensitive mind notices a wrongness in the fabric of space.\n");
	}
    }
    if (current_heart_beat) {
	set_heart_beat(current_heart_beat, 0);
	debug_message("Heart beat in %s turned off.\n",
		      current_heart_beat->name);
	if (current_heart_beat->interactive) {
	    struct object *save_cmd = command_giver;
	    command_giver = current_heart_beat;
	    add_message("Game driver tells you: You have no heart beat !\n");
	    command_giver = save_cmd;
	}
	current_heart_beat = 0;
    }
    num_error--;
    if (error_recovery_context_exists)
	longjmp(error_recovery_context, 1);
    abort();
}

/*
 * Check that it is an legal path. No '..' are allowed.
 */
int legal_path(path)
    char *path;
{
    char *p;

    if (path == NULL || strchr(path, ' '))
	return 0;
    if (path[0] == '/')
        return 0;
#ifdef MSDOS
    if (!valid_msdos(path)) return(0);
#endif
    for(p = strchr(path, '.'); p; p = strchr(p+1, '.')) {
	if (p[1] == '.')
	    return 0;
    }
    return 1;
}

/*
 * There is an error in a specific file. Ask the game driver to log the
 * message somewhere.
 */
void smart_log(error_file, line, what)
     char *error_file, *what;
     int line;
{
    char buff[500];

    if (error_file == 0)
	return;
    if (strlen(what) + strlen(error_file) > sizeof buff - 100)
	what = "...[too long error message]...";
    if (strlen(what) + strlen(error_file) > sizeof buff - 100)
	error_file = "...[too long filename]...";
    sprintf(buff, "%s line %d:%s\n", error_file, line, what);
    push_constant_string(error_file);
    push_constant_string(buff);
    apply_master_ob("log_error", 2);
}

/*
 * Check that a file name is valid for read or write.
 * Also change the name as if the current directory was at the players
 * own directory.
 * This is done by functions in the player object.
 *
 * The master object (master.c) always have permissions to access
 * any file in the mudlib hierarchy, but only inside the mudlib.
 *
 * WARNING: The string returned will (mostly) be deallocated at next
 * call of apply().
 */
#ifdef COMPAT_MODE
char debug_check_file[50];

char *check_file_name(file, writeflg)
    char *file;
    int writeflg;
{
    struct svalue *ret;
    char *str;

    if (current_object != master_ob) {
	if (!command_giver || !command_giver->interactive ||
	    (command_giver->flags & O_DESTRUCTED))
	  return 0;
	push_constant_string(file);
	if (writeflg)
	  ret = apply("valid_write", command_giver, 1);
	else
	  ret = apply("valid_read",  command_giver, 1);
	if (command_giver->flags & O_DESTRUCTED)
	  return 0;
	if (ret == 0 || ret->type != T_STRING) {
/*	    add_message("Bad file name.\n");  */
	    return 0;
	}
	str = ret->u.string;
    } else {
	/* Master object can read/write anywhere ! */
	if (file[0] == '/')
	  str = file + 1;
	else
	  str = file;
    }
    strncpy(debug_check_file, str, sizeof debug_check_file);
    debug_check_file[sizeof debug_check_file - 1] = '\0';
    if (!legal_path(str))
      error("Illegal path: %s\n", str);
    /* The string "/" will be converted to "". */
    if (str[0] == '\0')
      return ".";
    return str;
}
#endif /* COMPAT_MODE */

/*
 * Check that a path to a file is valid for read or write.
 * This is done by functions in the master object.
 * The path is always treated as an absolute path, and is returned without
 * a leading '/'.
 * If the path was '/', then '.' is returned.
 * The returned string may or may not be residing inside the argument 'path',
 * so don't deallocate arg 'path' until the returned result is used no longer.
 * Otherwise, the returned path is temporarily allocated by apply(), which
 * means it will be dealocated at next apply().
 */
char *check_valid_path(path, eff_user, call_fun, writeflg)
    char *path;
    struct wiz_list *eff_user;
    char *call_fun;
    int writeflg;
{
    struct svalue *v;

    if (eff_user == 0)
	return 0;
    push_string(path, STRING_MALLOC);
    push_string(eff_user->name, STRING_CONSTANT);
    push_string(call_fun, STRING_CONSTANT);
    if (writeflg)
	v = apply_master_ob("valid_write", 3);
    else
	v = apply_master_ob("valid_read", 3);
    if (v && v->type == T_NUMBER && v->u.number == 0)
	return 0;
    if (path[0] == '/')
	path++;
    if (path[0] == '\0')
	path = ".";
    if (legal_path(path))
	return path;
    return 0;
}

/*
 * This one is called from HUP.
 */
int game_is_being_shut_down;

#if !defined(_AIX) && !defined(__linux__) && !defined(__svr4__)
void startshutdowngame() {
    game_is_being_shut_down = 1;
}
#else
void startshutdowngame(int arg) {
    game_is_being_shut_down = 1;
}
#endif

/*
 * This one is called from the command "shutdown".
 * We don't call it directly from HUP, because it is dangerous when being
 * in an interrupt.
 */
void shutdowngame()
{
  extern void free_all_call_outs(void);
  extern int total_num_prog_blocks;
  extern int tot_alloc_object;
  extern char *reserved_area;

  shout_string("Game driver shouts: LPmud shutting down immediately.\n");
  apply_master_ob("shut_down_game",0);
  save_wiz_file();
  ipc_remove();
  remove_all_players();
#if TIME_TO_SWAP > 0
  unlink_swap_file();
#endif
#ifdef DEALLOCATE_MEMORY_AT_SHUTDOWN
  free_all_call_outs();
  free_simul_efuns();
  remove_all_objects();
  free_all_sent();
  free_inc_list();
  free_object(master_ob, "shutdowngame");
  if(obj_list)
    fprintf(stderr,"Couldn't destructe all objects. (%s)\n",obj_list->name);
  if(obj_list_destruct)
    fprintf(stderr,"Couldn't free all destructed objects. (%s)\n",obj_list_destruct->name);
  if(num_arrays)
    fprintf(stderr, "Arrays left: %d\n", num_arrays);
  if(total_num_prog_blocks)
    fprintf(stderr, "Program blocks left: %d\n", total_num_prog_blocks);
  if(tot_alloc_object)
    fprintf(stderr, "Objects not freed: %d\n", tot_alloc_object);
  remove_wiz_list();
  free_all_strings();
  if(reserved_area != NULL)
    free(reserved_area);
#ifdef MALLOC_smalloc
  dump_malloc_data();
  show_free_list();
#endif
#endif
#ifdef OPCPROF
  opcdump();
#endif
  exit(0);
}

/*
 * Transfer an object from an object to an object.
 * Call add_weight(), drop(), get(), prevent_insert(), add_weight(),
 * and can_put_and_get() where needed.
 * Return 0 on success, and special code on failure:
 *
 * 1: To heavy for destination.
 * 2: Can't be dropped.
 * 3: Can't take it out of it's container.
 * 4: The object can't be inserted into bags etc.
 * 5: The destination doesn't allow insertions of objects.
 * 6: The object can't be picked up.
 */
#ifdef F_TRANSFER
int transfer_object(ob, to)
    struct object *ob, *to;
{
  return otransfer_object(ob, to, 0);
}

int otransfer_object(ob, to, force_flag)
    struct object *ob, *to;
    int force_flag;
{
  struct svalue *v_weight, *ret;
  int weight;
  struct object *from = ob->super;

  /*
   * Get the weight of the object
   */
  v_weight = 0;
  if (!force_flag)
    v_weight = apply("query_weight", ob, 0);

  if (v_weight && v_weight->type == T_NUMBER)
    weight = v_weight->u.number;
  else
    weight = 0;
  if (ob->flags & O_DESTRUCTED)
    return 3;
  /*
   * If the original place of the object is a living object,
   * then we must call drop() to check that the object can be dropped.
   */
  if (from && (from->flags & O_ENABLE_COMMANDS)) 
  {
    ret = apply("drop", ob, 0);
    if (ret && (ret->type != T_NUMBER || ret->u.number != 0))
      return 2;
    /* This shold not happen, but we can not trust LPC hackers. :-) */
    if (ob->flags & O_DESTRUCTED)
      return 2;
  }
  /*
   * If 'from' is not a room and not a player, check that we may
   * remove things out of it.
   */
  if (from && from->super && !(from->flags & O_ENABLE_COMMANDS)) 
  {
    ret = apply("can_put_and_get", from, 0);
    if (!ret || (ret->type == T_NUMBER && ret->u.number == 0) ||
	(from->flags & O_DESTRUCTED))
      return 3;
  }
  /*
   * If the destination is not a room, and not a player,
   * Then we must test 'prevent_insert', and 'can_put_and_get'.
   */
  if (to->super && !(to->flags & O_ENABLE_COMMANDS)) 
  {
    ret = apply("prevent_insert", ob, 0);
    if (ret && (ret->type != T_NUMBER || ret->u.number != 0))
      return 4;
    ret = apply("can_put_and_get", to, 0);
    if (!ret || (ret->type != T_NUMBER && ret->type != 0) ||
	(to->flags & O_DESTRUCTED) || (ob->flags & O_DESTRUCTED))
      return 5;
  }
  /*
   * If the destination is a player, check that he can pick it up.
   */
  if (to->flags & O_ENABLE_COMMANDS)
  {
    ret = apply("get", ob, 0);
    if (!ret || (ret->type == T_NUMBER && ret->u.number == 0) ||
	(ob->flags & O_DESTRUCTED))
      return 6;
  }
  /*
   * If it is not a room, correct the total weight in the destination.
   */
  if (to->super && weight) 
  {
    /*
     * Check if the destination can carry that much.
     */
    push_number(weight);
    ret = apply("add_weight", to, 1);
    if ((ret && ret->type == T_NUMBER && ret->u.number == 0) ||
	(to->flags & O_DESTRUCTED))
      return 1;
  }
  /*
   * If it is not a room, correct the weight in the 'from' object.
   */
  if (from && from->super && weight)
  {
    push_number(-weight);
    (void)apply("add_weight", from, 1);
  }
  move_object(ob, to);
  return 0;
}
#endif /* F_TRANSFER */

/*
 * Move or destruct one object.
 */
void move_or_destruct(what, to)
    struct object *what, *to;
{
  omove_or_destruct(what, to, 0);
}

void omove_or_destruct(what, to, force_flag)
    struct object *what, *to;
    int force_flag;
{
  struct svalue v;
  int res;
  res = otransfer_object(what, to, force_flag);
  if (res == 0 || (what->flags & O_DESTRUCTED))
    return;
  if (res == 1 || res == 4 || res == 5) {
    if (to->super) {
      omove_or_destruct(what, to->super, force_flag);
      return;
    }
  }
  /*
   * Failed to move the object. Then, it is destroyed.
   */
  v.type = T_OBJECT;
  v.u.ob = what;
  odestruct_object(&v, force_flag);
}

/*
 * Call this one when there is only little memory left. It will start
 * Armageddon.
 */
void slow_shut_down(minutes)
    int minutes;
{
    struct object *ob;

    /*
     * Swap out objects, and free some memory.
     */
    ob = find_object("obj/shut");
    if (!ob) {
	struct object *save_current = current_object,
	              *save_command = command_giver;
	command_giver = 0;
	current_object = 0;
	shout_string("Game driver shouts: Out of memory.\n");
	command_giver = save_command;
	current_object = save_current;
#if !defined(_AIX) && !defined(__linux__) && !defined(__svr4__)
        startshutdowngame();
#else
        startshutdowngame(1);
#endif
	return;
    }
    shout_string("Game driver shouts: The memory is getting low !\n");
    push_number(minutes);
    (void)apply("shut", ob, 1);
}

int match_string(match, str)
    char *match, *str;
{
    int i;

 again:
    if (*str == '\0' && *match == '\0')
	return 1;
    switch(*match) {
    case '?':
	if (*str == '\0')
	    return 0;
	str++;
	match++;
	goto again;
    case '*':
	match++;
	if (*match == '\0')
	    return 1;
	for (i=0; str[i] != '\0'; i++)
	    if (match_string(match, str+i))
		return 1;
	return 0;
    case '\0':
	return 0;
    case '\\':
	match++;
	if (*match == '\0')
	    return 0;
	/* Fall through ! */
    default:
	if (*match == *str) {
	    match++;
	    str++;
	    goto again;
	}
	return 0;
    }
}

/*
 * Credits for some of the code below goes to Free Software Foundation
 * Copyright (C) 1990 Free Software Foundation, Inc.
 * See the GNU General Public License for more details.
 */
#ifndef S_ISDIR
#define	S_ISDIR(m)	(((m)&S_IFMT) == S_IFDIR)
#endif

#ifndef S_ISREG
#define	S_ISREG(m)	(((m)&S_IFMT) == S_IFREG)
#endif

int
isdir (path)
     char *path;
{
  struct stat stats;

  return stat (path, &stats) == 0 && S_ISDIR (stats.st_mode);
}

void
strip_trailing_slashes (path)
     char *path;
{
  int last;

  last = strlen (path) - 1;
  while (last > 0 && path[last] == '/')
    path[last--] = '\0';
}

struct stat to_stats, from_stats;

int
copy (from, to)
     char *from, *to;
{
  int ifd;
  int ofd;
  char buf[1024 * 8];
  int len;			/* Number of bytes read into `buf'. */
  
  if (!S_ISREG (from_stats.st_mode))
    {

      error ("cannot move `%s' across filesystems: Not a regular file\n", from);
      return 1;
    }
  
  if (unlink (to) && errno != ENOENT)
    {
      error ("cannot remove `%s'\n", to);
      return 1;
    }

  ifd = open (from, O_RDONLY, 0);
  if (ifd < 0)
    {
      error ("%s: open failed\n", from);
      return errno;
    }
  ofd = open (to, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (ofd < 0)
    {
      error ("%s: open failed\n", to);
      close (ifd);
      return 1;
    }
#ifndef FCHMOD_MISSING
  if (fchmod (ofd, from_stats.st_mode & 0777))
    {
      error ("%s: fchmod failed\n", to);
      close (ifd);
      close (ofd);
      unlink (to);
      return 1;
    }
#endif
  
  while ((len = read (ifd, buf, sizeof (buf))) > 0)
    {
      int wrote = 0;
      char *bp = buf;
      eval_cost ++;
      do
	{
	  wrote = write (ofd, bp, len);
	  if (wrote < 0)
	    {
	      error ("%s: write failed\n", to);
	      close (ifd);
	      close (ofd);
	      unlink (to);
	      return 1;
	    }
	  bp += wrote;
	  len -= wrote;
	} while (len > 0);
    }
  if (len < 0)
    {
      error ("%s: read failed\n", from);
      close (ifd);
      close (ofd);
      unlink (to);
      return 1;
    }

  if (close (ifd) < 0)
    {
      error ("%s: close failed", from);
      close (ofd);
      return 1;
    }
  if (close (ofd) < 0)
    {
      error ("%s: close failed", to);
      return 1;
    }
  
#ifdef FCHMOD_MISSING
  if (chmod (to, from_stats.st_mode & 0777))
    {
      error ("%s: chmod failed\n", to);
      return 1;
    }
#endif

  return 0;
}

/* Move FROM onto TO.  Handles cross-filesystem moves.
   If TO is a directory, FROM must be also.
   Return 0 if successful, 1 if an error occurred.  */

int
do_move (from, to)
     char *from;
     char *to;
{
  if (lstat (from, &from_stats) != 0)
    {
      error ("%s: lstat failed\n", from);
      return 1;
    }

  if (lstat (to, &to_stats) == 0)
    {
#ifndef MSDOS
      if (from_stats.st_dev == to_stats.st_dev
	  && from_stats.st_ino == to_stats.st_ino)
#else
      if (same_file(from,to))
#endif
	{
	  error ("`%s' and `%s' are the same file", from, to);
	  return 1;
	}

      if (S_ISDIR (to_stats.st_mode))
	{
	  error ("%s: cannot overwrite directory", to);
	  return 1;
	}

    }
  else if (errno != ENOENT)
    {
      error ("%s: unknown error\n", to);
      return 1;
    }
#ifdef SYSV
  if (isdir(from)) {
      char cmd_buf[100];
      sprintf(cmd_buf, "/usr/lib/mv_dir %s %s", from, to);
      return system(cmd_buf);
  } else
#endif /* SYSV */      
  if (rename (from, to) == 0)
    {
      return 0;
    }

  if (errno != EXDEV)
    {
      error ("cannot move `%s' to `%s'", from, to);
      return 1;
    }

  /* rename failed on cross-filesystem link.  Copy the file instead. */

  if (copy (from, to))
      return 1;
  
  if (unlink (from))
    {
      error ("cannot remove `%s'", from);
      return 1;
    }

  return 0;

}
    
/*
 * do_rename is used by the efun rename. It is basically a combination
 * of the unix system call rename and the unix command mv. Please shoot
 * the people at ATT who made Sys V.
 */

#ifdef F_RENAME
int
do_rename(fr, t)
    char *fr, *t;
{
    char *from, *to;
    
    from = check_valid_path(fr, current_object->eff_user, "do_rename", 1);
    if(!from)
	return 1;
    to = check_valid_path(t, current_object->eff_user, "do_rename", 1);
    if(!to)
	return 1;
    if(!strlen(to) && !strcmp(t, "/")) {
	to = (char *)alloca(3);
	sprintf(to, "./");
    }
    strip_trailing_slashes (from);
    if (isdir (to))
	{
	    /* Target is a directory; build full target filename. */
	    char *cp;
	    char *newto;
	    
	    cp = strrchr (from, '/');
	    if (cp)
		cp++;
	    else
		cp = from;
	    
	    newto = (char *) alloca (strlen (to) + 1 + strlen (cp) + 1);
	    sprintf (newto, "%s/%s", to, cp);
	    return do_move (from, newto);
	}
    else
	return do_move (from, to);
}
#endif /* F_RENAME */

/*
 * Fuzzymatch code:
 * original code by Stig S�ther Bakken (Auronthas) 940715
 * Less original code: Profezzorn
 */

/* The fuzziness between two strings, STRING1 and STRING2, is calculated by
 * finding the position in STRING2 of a prefix of STRING1.  The first character
 * of STRING1 is found in STRING2.  If we find it, we continue matching
 * successive characters from STRING1 at successive STRING2 positions.  When we
 * have found the longest prefix of STRING1 in STRING2, we decide whether it is
 * a match.  It is considered a match if the length of the prefix is greater or
 * equal to the offset of the beginning of the prefix of STRING1 in STRING2.
 * This means that "food" will match "barfoo" because "foo" (the prefix)
 * matches "foo" in "barfoo" with an offset and length of 3.  However, "food"
 * will not be considered to match "barfu", since the length is 1 while the
 * offset is 3.  The fuzz value of the match is the length of the prefix.  If
 * we find a match, we take the prefix off STRING1 and the string upto the end
 * of the match in STRING2.  If we do not find a match, we take off the first
 * character in STRING1.  Then we try and find the next prefix.
 *
 * So, to walk through an example:
 *
 * (FM-matchiness "pigface" "pigsfly"):
 *
 * STRING1		STRING2		MATCH LENGTH	OFFSET		FUZZ
 * pigface		pigsfly		3		0		3
 * face			sfly		1		1		1
 * ace			ly		0		0		0
 * ce			ly		0		0		0
 * c			ly		0		0		0
 *
 *      => 4
 * 
 * (FM-matchiness "begining-of-l" "beginning-of-l"):
 * 
 * STRING1		STRING2		MATCH LENGTH	OFFSET		FUZZ
 * begining-of-l	beginning-of-l	5		0		5
 * ing-of-l		ning-of-l	8		1		8
 *
 *      => 13
 */
#ifdef F__FUZZYMATCH
int fuzzymatch(char *str1,char *str2)
{
  int fuzz;
  fuzz=0;
  while(*str1 && *str2)
  {
    char *tmp1,*tmp2;
    int offset,length;
    eval_cost++;

    /* Now we will look for the first character of tmp1 in tmp2 */
    if(tmp2=strchr(str2,str1[0]))
    {
      /* Ok, so we have found one character, let's check how many more */
      tmp1 = str1;
      offset = tmp2 - str2;
      length = 1;
      while (*(++tmp1)==*(++tmp2) && *tmp1) length++;

      /* We consider this a hit only if the length of the match is
	 bigger than or equal to the offset */
      if(length>=offset)
      {
	fuzz += length;
	str1 += length;
	str2 += length + offset;
	continue;
      }
    }
    str1++;
  }
  return fuzz;
}
#endif
