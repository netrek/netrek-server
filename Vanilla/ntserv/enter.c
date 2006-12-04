/*
 * enter.c
 */
#include "copyright.h"

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <sys/types.h>
#include <errno.h>
#include <pwd.h>
#include <ctype.h>
#include <time.h>		/* 7/16/91 TC */
#include <netinet/in.h>
#include "defs.h"
#include "struct.h"
#include "data.h"
#include "packets.h"
#include "proto.h"

/* file scope prototypes */

static void auto_peace(void);
static void placeIndependent(void);

/* Enter the game */

#if defined (ONCHECK) && !defined(ROBOT)
static int first_time = 1;
#endif

#ifdef STURGEON
extern void initspecial();
#endif

/* change 5/10/91 TC -- if tno == 4, independent */
/*                         tno == 5, Terminator  */
/* change 3/25/93 NBT -- added promote string    */
/* change 9/29/94 MJK -- enter no longer uses global variables */
/*                       this allows one robot to enter several ships */

/*ARGSUSED*/
void enter(int tno, int disp, int pno, int s_type, char *name)
     /* tno  0=fed, 1=rom, 2=kli, 3=ori, indep, termie */
{
    static int lastteam= -1;
    static int lastrank= -1;
    char addrbuf[10];
    int startplanet;
    int i;

    /* Use local variables, not the globals */
    struct player *me = &players[pno], *j;
    struct ship *myship = &me->p_ship;

#if defined (ONCHECK) && !defined(ROBOT)
/* produces a file for xbiff to watch daemon start */
    if (first_time) {
	char buf[80];
	sprintf(buf,"echo on >> %s",On_File);
	system(buf);
	first_time = 0;
    }
#endif

    STRNCPY(me->p_name, name, NAME_LEN);
    me->p_name[NAME_LEN - 1] = '\0';
    getship(myship, s_type);

    /* Alert client about new ship stats */
    sndShipCap();

    /* Limit client updates if needed */
#ifndef ROBOT
#ifdef FASTER_SB_MAXUPDATES
    if (s_type != STARBASE)   /* Allow SB's 10 ups/sec regardless of minskip */
      if (me->p_timerdelay < minskip)  me->p_timerdelay = minskip;
    if (me->p_timerdelay > maxskip) me->p_timerdelay = maxskip;

#ifdef SHORT_THRESHOLD
    /* I need the number of updates for the threshold handling  HW 93 */
    numupdates = (int) (10 / me->p_timerdelay);
    if ( send_threshold != 0) { /* We need to recompute the threshold */
        actual_threshold = send_threshold / numupdates;
        if ( actual_threshold < 60 ) { /* my low value */
             actual_threshold = 60;
             /* means: 1 SP_S_PLAYER+SP_S_YOU+36 bytes */
             /*new_warning(UNDEF,
                         "Threshold set to %d .  %d / Update(Server limit!)",
                         numupdates * 60, 60);
                         */
        }
        /*
        else {
             new_warning(UNDEF,
                         "Threshold set to %d .  %d / Update.",
                         send_threshold , actual_threshold);
        }
        */
    }
#endif

#endif /* of FASTER_SB_MAXUPDATES */
#endif /* of #ifndef ROBOT */

#ifdef STURGEON
    if (sturgeon) {
      char buf[80];
      /* Remove upgrades for bases */
      if (s_type == STARBASE) {
        me->p_upgrades = 0.0;
        for (i = 0; i < NUMUPGRADES; i++)
            me->p_upgradelist[i] = 0;
      }
      /* If non-base ship hasn't spent it's base rank credit upgrades, leave them on */
      /* And in the case of genocide/conquer, leave them on too */
      else if (me->p_upgrades > (float) me->p_stats.st_rank
               && me->p_whydead != KGENOCIDE
               && me->p_whydead != KWINNER) {
        if (sturgeon_lite) {
          /* Lose most costly upgrade */
          double highestcost = 0.0, tempcost = 0.0;
          int highestupgrade = 0;
          for (i = 1; i < NUMUPGRADES; i++) {
            if (me->p_upgradelist[i] > 0) {
              tempcost = baseupgradecost[i] + (me->p_upgradelist[i] - 1.0) * adderupgradecost[i];
              if (tempcost > highestcost) {
                highestupgrade = i;
                highestcost = tempcost;
              }
            }
          }
          /* Only remove it if rank credit insufficient to cover all upgrades */
          if (me->p_upgrades > (float) me->p_stats.st_rank) {
            me->p_upgradelist[highestupgrade]--;
            me->p_upgrades -= highestcost;
          }
        }
        else {
          /* Strip off upgrades until total upgrade amount is less than or equal
             to rank credit.  Run through normal upgrades first, then strip off
             the one time upgrades last, only if necessary. */
          for (i = 1; i < NUMUPGRADES; i++) {
            if (me->p_upgradelist[i] > 0) {
              while (me->p_upgradelist[i] != 0) {
                me->p_upgradelist[i]--;
                me->p_upgrades -= baseupgradecost[i] + me->p_upgradelist[i] * adderupgradecost[i];
                if (me->p_upgrades <= (float) me->p_stats.st_rank)
                  break;
              }
            }
            if (me->p_upgrades <= (float) me->p_stats.st_rank)
              break;
          }
          /* If for some reason we still are over, just set upgrades to zero.  Should
             never happen, but here to be safe */
          if (me->p_upgrades > (float) me->p_stats.st_rank)
            me->p_upgrades = 0;
        }
      }
      /* Calculate new rank credit */
      me->p_rankcredit = (float) me->p_stats.st_rank - me->p_upgrades;
      if (me->p_rankcredit < 0.0)
         me->p_rankcredit = 0.0;
      /* Now we need to go through the upgrade list and reapply all upgrades
         that are left, as default ship settings have been reset */
      for (i = 1; i < NUMUPGRADES; i++) {
        if (me->p_upgradelist[i] > 0)
          apply_upgrade(i, me, me->p_upgradelist[i]);
      }
      me->p_special = -1;
      initspecial(me);
      /* AS get unlimited mines */
      if (s_type == ASSAULT) {
        me->p_weapons[11].sw_number = -1;
        me->p_special = 11;
      }
      me->p_team = (1 << tno);
      /* SB gets unlimited pseudoplasma, type 5 plasma, and suicide drones */
      if (s_type == STARBASE) {
        me->p_weapons[0].sw_number = -1;
        me->p_weapons[5].sw_number = -1;
        me->p_weapons[10].sw_number = -1;
        me->p_special = 10;
        sprintf(buf, "%s (%c%c) is now a Starbase",
                me->p_name, teamlet[me->p_team], shipnos[me->p_no]);
        strcpy(addrbuf, "GOD->ALL");
        pmessage(0, MALL, addrbuf, buf);
      }
      /* Galaxy class gets unlimited pseudoplasma */
      if (s_type == SGALAXY) {
        me->p_weapons[0].sw_number = -1;
        me->p_special = 0;
      }
      /* ATT gets unlimited everything */
      if (s_type == ATT) {
        for (i = 0; i <= 10; i++)
            me->p_weapons[i].sw_number = -1;
        me->p_special = 10;
      }
    }
#endif

    /* Check if can use plasma torpedoes */
    if (s_type != STARBASE && s_type != ATT && (
#ifdef STURGEON  /* force plasmas -> upgrade feature */
        !sturgeon &&
#endif
        plkills > 0))
	me->p_ship.s_plasmacost = -1;
    me->p_updates = 0;
    me->p_flags = PFSHIELD|PFGREEN;
    if (s_type == STARBASE) {
	me->p_flags |= PFDOCKOK; /* allow docking by default */
	for(i = 0; i < MAXPLAYER; i++)
	    if (players[i].p_team == me->p_team)
	        players[i].p_candock = 1; /* give team permission to dock */
    }
    me->p_transwarp = PFGREEN|PFYELLOW|PFRED;
    me->p_dir = 0;
    me->p_desdir = 0;
    me->p_speed = 0;
    me->p_desspeed = 0;
    me->p_subspeed = 0;
    if ((tno == 4) || (tno == 5) || (me->w_queue == QU_GOD_OBS) ) { 
      /* change 5/10/91 TC new case, indep */
      /* change 1/05/00 CYV new case, god obs */
	me->p_team = 0;
	placeIndependent();	/* place away from others 1/23/92 TC */
    }
    else {
	me->p_team = (1 << tno);
	for (;;) {
	    startplanet=tno*10 + random() % 10;
	    if (startplanets[startplanet]) break;
	}
	me->p_x = planets[startplanet].pl_x + (random() % 10000) - 5000;
	me->p_y = planets[startplanet].pl_y + (random() % 10000) - 5000;
	if (me->p_x < 0) me->p_x=0;
	if (me->p_y < 0) me->p_y=0;
	if (me->p_x > GWIDTH) me->p_x = GWIDTH;
	if (me->p_y > GWIDTH) me->p_y = GWIDTH;
    }
    me->p_ntorp = 0;
    me->p_cloakphase = 0;
    me->p_nplasmatorp = 0;
    me->p_damage = 0;
    me->p_subdamage = 0;
    me->p_etemp = 0;
    me->p_etime = 0;
    me->p_wtemp = 0;
    me->p_wtime = 0;
    me->p_shield = me->p_ship.s_maxshield;
    me->p_subshield = 0;
    me->p_swar = 0;
    me->p_lastseenby = VACANT;
    me->p_kills = 0.0;
    me->p_armies = 0;
    bay_init(me);
    me->p_dock_with = 0;
    me->p_dock_bay = 0;
/*  if (!keeppeace) me->p_hostile = (FED|ROM|KLI|ORI); */
    if( !(me->p_flags & PFROBOT) && (me->p_team == NOBODY) ) {
      me->p_hostile = NOBODY;
      me->p_war = NOBODY;
      me->p_swar = NOBODY;
    } else {
      if (!keeppeace) auto_peace();
      me->p_hostile &= ~me->p_team;
      me->p_war = me->p_hostile;
    }

    /* reset eject voting to avoid inheriting this slot's last occupant's */
    /* escaped fate just in case the last vote comes through after the    */
    /* old guy quit and the new guy joined  -Villalpando req. by Cameron  */
    for (i = 0, j = &players[i]; i < MAXPLAYER; i++, j++) 
      j->voting[me->p_no] = -1;

    /* join message stuff */
    sprintf(me->p_mapchars,"%c%c",teamlet[me->p_team], shipnos[me->p_no]);
    sprintf(me->p_longname, "%s (%s)", me->p_name, me->p_mapchars);	    

    if (lastteam != tno || lastrank != mystats->st_rank) {

	/* Are joining a new team?  take care of a few things if so */

	if ((lastteam < NUMTEAM) && (lastteam != tno)) {
#ifdef LTD_STATS
	  setEnemy(tno, me);
#endif /*LTD_STATS*/
        /* If switching, become hostile to former team 6/29/92 TC */
	  if (lastteam >= 0)
	    declare_war(1<<lastteam, 0);
	}
	if (lastrank==-1) lastrank=mystats->st_rank;	/* NBT patch */
	lastteam=tno;
	sprintf(addrbuf, " %s->ALL", me->p_mapchars);
	if ((tno == 4) && (strcmp(me->p_monitor, "Nowhere") == 0)) {
	    /* change 5/10/91, 8/28/91 TC */
	    time_t curtime;
	    struct tm *tmstruct;
	    int hour;

	    time(&curtime);
	    tmstruct = localtime(&curtime);
	    if (!(hour = tmstruct->tm_hour%12)) hour = 12;
	    if ( queues[QU_PICKUP].count > 0){
	        pmessage2(0, MALL, addrbuf, me->p_no,
		    "It's %d:%02d%s, time to die.  Approximate queue size: %d.",
		    hour, 
		    tmstruct->tm_min,
                    tmstruct->tm_hour >= 12 ? "pm" : "am", 
		    queues[QU_PICKUP].count);
	    }
	    else
		pmessage2(0, MALL, addrbuf, me->p_no,
		    "It's %d:%02d%s, time to die.", 
		    hour,
		    tmstruct->tm_min,
                    tmstruct->tm_hour >= 12 ? "pm" : "am");
	}
	else if (tno == 5) {		/* change 7/27/91 TC */
	    if (players[disp].p_status != PFREE) {
		pmessage2(0, MALL, addrbuf, me->p_no,
		    "%c%c has been targeted for termination.",
                    teamlet[players[disp].p_team],
                    shipnos[players[disp].p_no]);
	    }
	}

	/* NBT added...nicer output I think */

	if (lastrank != mystats->st_rank) {
	  pmessage2(0, MALL | MJOIN, addrbuf, me->p_no,
	        "%.16s (%2.2s) promoted to %s (%.16s@%.32s)",
		me->p_name,
                me->p_mapchars,
                ranks[me->p_stats.st_rank].name,
                me->p_login,
		me->p_full_hostname);
	} else {
	/* new-style join message 4/13/92 TC */
	  pmessage2(0, MALL | MJOIN, addrbuf, me->p_no,
	        "%s %.16s is now %2.2s (%.16s@%.32s)", 
		ranks[me->p_stats.st_rank].name, 
		me->p_name, 
		me->p_mapchars, 
		me->p_login,
		me->p_full_hostname);
	}

	lastrank = mystats->st_rank;
    }

    me->p_fuel = myship->s_maxfuel;
    delay = 0;
}

/* my idea of avoiding using the war window 8/16/91 TC */
static void auto_peace(void)
{
    int i, num[MAXTEAM+1];	/* corrected 6/22/92 TC */
    struct player	*p;

    num[0] = num[FED] = num[ROM] = num[KLI] = num[ORI] = 0;
    for (i = 0, p = players; i < MAXPLAYER; i++, p++)
	if (p->p_status != PFREE)
	    num[p->p_team]++;
    if (status->tourn)
	me->p_hostile = 
	    ((FED * (num[FED] >= tournplayers)) |
	     (ROM * (num[ROM] >= tournplayers)) |
	     (KLI * (num[KLI] >= tournplayers)) |
	     (ORI * (num[ORI] >= tournplayers)));
    else
	me->p_hostile = FED|ROM|KLI|ORI;
}

/* code to avoid placing bots on top of others 1/23/92 TC */

#define INDEP (GWIDTH/3)	/* width of region they can enter */

static void placeIndependent(void)
{
    int i;
    struct player	*p;
    int good, failures;

    failures = 0;
    while (failures < 10) {	/* don't try too hard to get them in */
	me->p_x = GWIDTH/2 + (random() % INDEP) - INDEP/2; /* middle 9th of */
	me->p_y = GWIDTH/2 + (random() % INDEP) - INDEP/2; /* galaxy */
	good = 1;
	for (i = 0, p = players; i < MAXPLAYER; i++, p++)
	    if ((p->p_status != PFREE) && (p != me)) {

		/* was 5 tries, was < TRACTDIST 7/20/92 TC */
		if ((ABS(p->p_x - me->p_x) < 2*TRACTDIST) && /* arbitrary def */
		    (ABS(p->p_y - me->p_y) < 2*TRACTDIST)) { /* of too close */
		    failures++;
		    good = 0;
		    break;
		}
	    }
	if (good) return;
    }
    ERROR(2,("Couldn't place the bot successfully.\n"));
}

#ifdef STURGEON
void initspecial(struct player *me)
{
    int i;
    for (i = 0; i < NUMSPECIAL; i++)
        me->p_weapons[i].sw_number = 0;
    strcpy (me->p_weapons[0].sw_name, "Pseudoplasma");
    me->p_weapons[0].sw_type = SPECPLAS;
    me->p_weapons[0].sw_fuelcost = 0;
    me->p_weapons[0].sw_damage = 0;
    me->p_weapons[0].sw_fuse = 30;
    me->p_weapons[0].sw_speed = 15;
    me->p_weapons[0].sw_turns = 1;
    for (i = 1; i <= 5; i++)
    {
        sprintf(me->p_weapons[i].sw_name, "Type %d Plasma", i);
        me->p_weapons[i].sw_type = SPECPLAS;
        me->p_weapons[i].sw_fuelcost = 0;
        me->p_weapons[i].sw_damage = 25 * (i + 1);
        me->p_weapons[i].sw_fuse = 30;
        me->p_weapons[i].sw_speed = 15;
        me->p_weapons[i].sw_turns = 1;
    }
    strcpy(me->p_weapons[6].sw_name, "10 megaton warhead");
    me->p_weapons[6].sw_damage = 2;
    strcpy(me->p_weapons[7].sw_name, "20 megaton warhead");
    me->p_weapons[7].sw_damage = 5;
    strcpy(me->p_weapons[8].sw_name, "50 megaton warhead");
    me->p_weapons[8].sw_damage = 10;
    strcpy(me->p_weapons[9].sw_name, "100 megaton warhead");
    me->p_weapons[9].sw_damage = 25;
    for (i = 6; i <= 9; i++)
    {
        me->p_weapons[i].sw_type = SPECBOMB;
        me->p_weapons[i].sw_fuelcost = 0;
        me->p_weapons[i].sw_fuse = 1;
        me->p_weapons[i].sw_speed = 0;
        me->p_weapons[i].sw_turns = 0;
    }
    strcpy(me->p_weapons[10].sw_name, "Suicide Drones");
    me->p_weapons[10].sw_type = SPECTORP;
    me->p_weapons[10].sw_fuelcost = 500;
    me->p_weapons[10].sw_damage = 60;
    me->p_weapons[10].sw_speed = 5;
    me->p_weapons[10].sw_fuse = 150;
    me->p_weapons[10].sw_turns = 2;
    strcpy(me->p_weapons[11].sw_name, "Mines");
    me->p_weapons[11].sw_type = SPECMINE;
    me->p_weapons[11].sw_fuelcost = 250;
    me->p_weapons[11].sw_damage = 200;
    me->p_weapons[11].sw_speed = 2;
    me->p_weapons[11].sw_fuse = 600;
    me->p_weapons[11].sw_turns = 0;
}
#endif
