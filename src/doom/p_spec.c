//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//	Implements special effects:
//	Texture animation, height or lighting changes
//	 according to adjacent sectors, respective
//	 utility functions, etc.
//	Line Tag handling. Line and Sector triggers.
//


#include <stdlib.h>

#include "doomdef.h"
#include "doomstat.h"

#include "deh_main.h"
#include "i_system.h"
#include "i_swap.h" // [crispy] LONG()
#include "z_zone.h"
#include "m_argv.h"
#include "m_misc.h"
#include "m_random.h"
#include "w_wad.h"

#include "r_local.h"
#include "p_local.h"

#include "g_game.h"

#include "s_sound.h"

// State.
#include "r_state.h"

// Data.
#include "sounds.h"

#include "d_englsh.h"

#define HUSTR_SECRETFOUND	"A secret is revealed!"

//
//      source animation definition
//
// [crispy] change istexture type from int to char and
// add PACKEDATTR for reading ANIMATED lumps from memory
typedef PACKED_STRUCT (
{
    signed char	istexture;	// if false, it is a flat
    char	endname[9];
    char	startname[9];
    int		speed;
}) animdef_t;



#define MAXANIMS                32

// [crispy] remove MAXANIMS limit
extern anim_t*	anims;
extern anim_t*	lastanim;

//
// P_InitPicAnims
//

// Floor/ceiling animation sequences,
//  defined by first and last frame,
//  i.e. the flat (64x64 tile) name to
//  be used.
// The full animation sequence is given
//  using all the flats between the start
//  and end entry, in the order found in
//  the WAD file.
//
// [crispy] add support for ANIMATED lumps
animdef_t		animdefs_vanilla[] =
{
    {false,	"NUKAGE3",	"NUKAGE1",	8},
    {false,	"FWATER4",	"FWATER1",	8},
    {false,	"SWATER4",	"SWATER1", 	8},
    {false,	"LAVA4",	"LAVA1",	8},
    {false,	"BLOOD3",	"BLOOD1",	8},

    // DOOM II flat animations.
    {false,	"RROCK08",	"RROCK05",	8},		
    {false,	"SLIME04",	"SLIME01",	8},
    {false,	"SLIME08",	"SLIME05",	8},
    {false,	"SLIME12",	"SLIME09",	8},

    {true,	"BLODGR4",	"BLODGR1",	8},
    {true,	"SLADRIP3",	"SLADRIP1",	8},

    {true,	"BLODRIP4",	"BLODRIP1",	8},
    {true,	"FIREWALL",	"FIREWALA",	8},
    {true,	"GSTFONT3",	"GSTFONT1",	8},
    {true,	"FIRELAVA",	"FIRELAV3",	8},
    {true,	"FIREMAG3",	"FIREMAG1",	8},
    {true,	"FIREBLU2",	"FIREBLU1",	8},
    {true,	"ROCKRED3",	"ROCKRED1",	8},

    {true,	"BFALL4",	"BFALL1",	8},
    {true,	"SFALL4",	"SFALL1",	8},
    {true,	"WFALL4",	"WFALL1",	8},
    {true,	"DBRAIN4",	"DBRAIN1",	8},
	
    {-1,        "",             "",             0},
};

// [crispy] remove MAXANIMS limit
anim_t*		anims;
anim_t*		lastanim;
static size_t	maxanims;


//
//      Animating line specials
//
#define MAXLINEANIMS            64*256

extern  short	numlinespecials;
extern  line_t*	linespeciallist[MAXLINEANIMS];



void P_InitPicAnims (void)
{
    int		i;

    // [crispy] add support for ANIMATED lumps
    animdef_t *animdefs;
    const boolean from_lump = (W_CheckNumForName("ANIMATED") != -1);

    if (from_lump)
    {
	animdefs = W_CacheLumpName("ANIMATED", PU_STATIC);
    }
    else
    {
	animdefs = animdefs_vanilla;
    }
    
    //	Init animation
    lastanim = anims;
    for (i=0 ; animdefs[i].istexture != -1 ; i++)
    {
        const char *startname, *endname;

	// [crispy] remove MAXANIMS limit
	if (lastanim >= anims + maxanims)
	{
	    size_t newmax = maxanims ? 2 * maxanims : MAXANIMS;
	    anims = I_Realloc(anims, newmax * sizeof(*anims));
	    lastanim = anims + maxanims;
	    maxanims = newmax;
	}

        startname = DEH_String(animdefs[i].startname);
        endname = DEH_String(animdefs[i].endname);

	if (animdefs[i].istexture)
	{
	    // different episode ?
	    if (R_CheckTextureNumForName(startname) == -1)
		continue;	

	    lastanim->picnum = R_TextureNumForName(endname);
	    lastanim->basepic = R_TextureNumForName(startname);
	}
	else
	{
	    if (W_CheckNumForName(startname) == -1)
		continue;

	    lastanim->picnum = R_FlatNumForName(endname);
	    lastanim->basepic = R_FlatNumForName(startname);
	}

	lastanim->istexture = animdefs[i].istexture;
	lastanim->numpics = lastanim->picnum - lastanim->basepic + 1;

	if (lastanim->numpics < 2)
	    I_Error ("P_InitPicAnims: bad cycle from %s to %s",
		     startname, endname);
	
	lastanim->speed = from_lump ? LONG(animdefs[i].speed) : animdefs[i].speed;
	lastanim++;
    }
	
    if (from_lump)
    {
	Z_ChangeTag(animdefs, PU_CACHE);
    }
}



//
// UTILITIES
//



//
// getSide()
// Will return a side_t*
//  given the number of the current sector,
//  the line number, and the side (0/1) that you want.
//
side_t*
getSide
( int		currentSector,
  int		line,
  int		side )
{
    return &sides[ (sectors[currentSector].lines[line])->sidenum[side] ];
}


//
// getSector()
// Will return a sector_t*
//  given the number of the current sector,
//  the line number and the side (0/1) that you want.
//
sector_t*
getSector
( int		currentSector,
  int		line,
  int		side )
{
    return sides[ (sectors[currentSector].lines[line])->sidenum[side] ].sector;
}


//
// twoSided()
// Given the sector number and the line number,
//  it will tell you whether the line is two-sided or not.
//
int
twoSided
( int	sector,
  int	line )
{
    return (sectors[sector].lines[line])->flags & ML_TWOSIDED;
}




//
// getNextSector()
// Return sector_t * of sector next to current.
// NULL if not two-sided line
//
sector_t*
getNextSector
( line_t*	line,
  sector_t*	sec )
{
    if (!(line->flags & ML_TWOSIDED))
	return NULL;
		
    if (line->frontsector == sec)
	return line->backsector;
	
    return line->frontsector;
}



//
// P_FindLowestFloorSurrounding()
// FIND LOWEST FLOOR HEIGHT IN SURROUNDING SECTORS
//
fixed_t	P_FindLowestFloorSurrounding(sector_t* sec)
{
    int			i;
    line_t*		check;
    sector_t*		other;
    fixed_t		floor = sec->floorheight;
	
    for (i=0 ;i < sec->linecount ; i++)
    {
	check = sec->lines[i];
	other = getNextSector(check,sec);

	if (!other)
	    continue;
	
	if (other->floorheight < floor)
	    floor = other->floorheight;
    }
    return floor;
}



//
// P_FindHighestFloorSurrounding()
// FIND HIGHEST FLOOR HEIGHT IN SURROUNDING SECTORS
//
fixed_t	P_FindHighestFloorSurrounding(sector_t *sec)
{
    int			i;
    line_t*		check;
    sector_t*		other;
    fixed_t		floor = -500*FRACUNIT;
	
    for (i=0 ;i < sec->linecount ; i++)
    {
	check = sec->lines[i];
	other = getNextSector(check,sec);
	
	if (!other)
	    continue;
	
	if (other->floorheight > floor)
	    floor = other->floorheight;
    }
    return floor;
}



//
// P_FindNextHighestFloor
// FIND NEXT HIGHEST FLOOR IN SURROUNDING SECTORS
// Note: this should be doable w/o a fixed array.

// Thanks to entryway for the Vanilla overflow emulation.

// 20 adjoining sectors max!
#define MAX_ADJOINING_SECTORS     20

fixed_t
P_FindNextHighestFloor
( sector_t* sec,
  int       currentheight )
{
    int         i;
    int         h;
    int         min;
    line_t*     check;
    sector_t*   other;
    fixed_t     height = currentheight;
    static fixed_t *heightlist = NULL;
    static int heightlist_size = 0;

    // [crispy] remove MAX_ADJOINING_SECTORS Vanilla limit
    // from prboom-plus/src/p_spec.c:404-411
    if (sec->linecount > heightlist_size)
    {
	do
	{
	    heightlist_size = heightlist_size ? 2 * heightlist_size : MAX_ADJOINING_SECTORS;
	} while (sec->linecount > heightlist_size);
	heightlist = I_Realloc(heightlist, heightlist_size * sizeof(*heightlist));
    }

    for (i=0, h=0; i < sec->linecount; i++)
    {
        check = sec->lines[i];
        other = getNextSector(check,sec);

        if (!other)
            continue;
        
        if (other->floorheight > height)
        {
            // Emulation of memory (stack) overflow
            if (h == MAX_ADJOINING_SECTORS + 1)
            {
                height = other->floorheight;
            }
            else if (h == MAX_ADJOINING_SECTORS + 2)
            {
                // Fatal overflow: game crashes at 22 sectors
                fprintf(stderr, "Sector with more than 22 adjoining sectors. "
                        "Vanilla will crash here\n");
            }

            heightlist[h++] = other->floorheight;
        }
    }
    
    // Find lowest height in list
    if (!h)
    {
        return currentheight;
    }
        
    min = heightlist[0];
    
    // Range checking? 
    for (i = 1; i < h; i++)
    {
        if (heightlist[i] < min)
        {
            min = heightlist[i];
        }
    }

    return min;
}

//
// FIND LOWEST CEILING IN THE SURROUNDING SECTORS
//
fixed_t
P_FindLowestCeilingSurrounding(sector_t* sec)
{
    int			i;
    line_t*		check;
    sector_t*		other;
    fixed_t		height = INT_MAX;
	
    for (i=0 ;i < sec->linecount ; i++)
    {
	check = sec->lines[i];
	other = getNextSector(check,sec);

	if (!other)
	    continue;

	if (other->ceilingheight < height)
	    height = other->ceilingheight;
    }
    return height;
}


//
// FIND HIGHEST CEILING IN THE SURROUNDING SECTORS
//
fixed_t	P_FindHighestCeilingSurrounding(sector_t* sec)
{
    int		i;
    line_t*	check;
    sector_t*	other;
    fixed_t	height = 0;
	
    for (i=0 ;i < sec->linecount ; i++)
    {
	check = sec->lines[i];
	other = getNextSector(check,sec);

	if (!other)
	    continue;

	if (other->ceilingheight > height)
	    height = other->ceilingheight;
    }
    return height;
}



//
// RETURN NEXT SECTOR # THAT LINE TAG REFERS TO
//
int
P_FindSectorFromLineTag
( line_t*	line,
  int		start )
{
    int	i;
	
    // [crispy] linedefs without tags apply locally
    if (!line->tag)
    {
    for (i=start+1;i<numsectors;i++)
	if (&sectors[i] == line->backsector)
	{
	    // const long linedef = line - lines;
	    // fprintf(stderr, "P_FindSectorFromLineTag: Linedef %ld without tag applied to sector %d\n", linedef, i);
	    return i;
	}
    }
    else
    for (i=start+1;i<numsectors;i++)
	if (sectors[i].tag == line->tag)
	    return i;
    
    return -1;
}




//
// Find minimum light from an adjacent sector
//
int
P_FindMinSurroundingLight
( sector_t*	sector,
  int		max )
{
    int		i;
    int		min;
    line_t*	line;
    sector_t*	check;
	
    min = max;
    for (i=0 ; i < sector->linecount ; i++)
    {
	line = sector->lines[i];
	check = getNextSector(line,sector);

	if (!check)
	    continue;

	if (check->lightlevel < min)
	    min = check->lightlevel;
    }
    return min;
}



//
// EVENTS
// Events are operations triggered by using, crossing,
// or shooting special lines, or by timed thinkers.
//

//
// P_CrossSpecialLine - TRIGGER
// Called every time a thing origin is about
//  to cross a line with a non 0 special.
//
void
P_CrossSpecialLine
( int		linenum,
  int		side,
  mobj_t*	thing )
{
    return P_CrossSpecialLinePtr(&lines[linenum], side, thing);
}

// [crispy] more MBF code pointers
void
P_CrossSpecialLinePtr
( line_t*	line,
  int		side,
  mobj_t*	thing )
{
//  line_t*	line;
    int		ok;

//  line = &lines[linenum];
    
    //	Triggers that other things can activate
    if (!thing->player)
    {
	// Things that should NOT trigger specials...
	switch(thing->type)
	{
	  case MT_ROCKET:
	  case MT_PLASMA:
	  case MT_BFG:
	  case MT_TROOPSHOT:
	  case MT_HEADSHOT:
	  case MT_BRUISERSHOT:
	    return;
	    break;
	    
	  default: break;
	}
	}

	// pointer to line function is NULL by default, set non-null if
    // line special is walkover generalized linedef type
    int (*linefunc)(line_t *line)=NULL;

    // check each range of generalized linedefs
    if ((unsigned)line->special >= GenEnd)
    {
      // Out of range for GenFloors
    }
    else if ((unsigned)line->special >= GenFloorBase)
    {
      if (!thing->player)
        if ((line->special & FloorChange) || !(line->special & FloorModel))
          return; // FloorModel is "Allow Monsters" if FloorChange is 0
      if (/*!comperr(comperr_zerotag) &&*/ !line->tag && ((line->special&6)!=6)) //e6y //jff 2/27/98 all non-manual
        return;                         // generalized types require tag
      linefunc = EV_DoGenFloor;
    }
    else if ((unsigned)line->special >= GenCeilingBase)
    {
      if (!thing->player)
        if ((line->special & CeilingChange) || !(line->special & CeilingModel))
          return;   // CeilingModel is "Allow Monsters" if CeilingChange is 0
      if (/*!comperr(comperr_zerotag) &&*/ !line->tag && ((line->special&6)!=6)) //e6y //jff 2/27/98 all non-manual
        return;                         // generalized types require tag
      linefunc = EV_DoGenCeiling;
    }
    else if ((unsigned)line->special >= GenDoorBase)
    {
      if (!thing->player)
      {
        if (!(line->special & DoorMonster))
          return;   // monsters disallowed from this door
        if (line->flags & ML_SECRET) // they can't open secret doors either
          return;
      }
      if (/*!comperr(comperr_zerotag) &&*/ !line->tag && ((line->special&6)!=6)) //e6y //jff 3/2/98 all non-manual
        return;                         // generalized types require tag
      linefunc = EV_DoGenDoor;
    }
    else if ((unsigned)line->special >= GenLockedBase)
    {
      if (!thing->player)
        return;   // monsters disallowed from unlocking doors
      if (!P_CanUnlockGenDoor(line,thing->player))
        return;
      if (/*!comperr(comperr_zerotag) &&*/ !line->tag && ((line->special&6)!=6)) //e6y //jff 2/27/98 all non-manual
        return;                         // generalized types require tag

      linefunc = EV_DoGenLockedDoor;
    }
    else if ((unsigned)line->special >= GenLiftBase)
    {
      if (!thing->player)
        if (!(line->special & LiftMonster))
          return; // monsters disallowed
      if (/*!comperr(comperr_zerotag) &&*/ !line->tag && ((line->special&6)!=6)) //e6y //jff 2/27/98 all non-manual
        return;                         // generalized types require tag
      linefunc = EV_DoGenLift;
    }
    else if ((unsigned)line->special >= GenStairsBase)
    {
      if (!thing->player)
        if (!(line->special & StairMonster))
          return; // monsters disallowed
      if (/*!comperr(comperr_zerotag) &&*/ !line->tag && ((line->special&6)!=6)) //e6y //jff 2/27/98 all non-manual
        return;                         // generalized types require tag
      linefunc = EV_DoGenStairs;
    }
    else if ((unsigned)line->special >= GenCrusherBase)
    {
      if (!thing->player)
        if (!(line->special & CrusherMonster))
          return; // monsters disallowed
      if (/*!comperr(comperr_zerotag) &&*/ !line->tag && ((line->special&6)!=6)) //e6y //jff 2/27/98 all non-manual
        return;                         // generalized types require tag
      linefunc = EV_DoGenCrusher;
    }

    if (linefunc) // if it was a valid generalized type
      switch((line->special & TriggerType) >> TriggerTypeShift)
      {
        case WalkOnce:
          if (linefunc(line))
            line->special = 0;    // clear special if a walk once type
          return;
        case WalkMany:
          linefunc(line);
          return;
        default:                  // if not a walk type, do nothing here
          return;
      }
		
	if (!thing->player)
    {
	ok = 0;
	switch(line->special)
	{
	  case 39:	// TELEPORT TRIGGER
	  case 97:	// TELEPORT RETRIGGER
	  case 125:	// TELEPORT MONSTERONLY TRIGGER
	  case 126:	// TELEPORT MONSTERONLY RETRIGGER
	  case 4:	// RAISE DOOR
	  case 10:	// PLAT DOWN-WAIT-UP-STAY TRIGGER
	  case 88:	// PLAT DOWN-WAIT-UP-STAY RETRIGGER
		//jff 3/5/98 add ability of monsters etc. to use teleporters
      case 208:     //silent thing teleporters
      case 207:
      case 243:     //silent line-line teleporter
      case 244:     //jff 3/6/98 make fit within DCK's 256 linedef types
      case 262:     //jff 4/14/98 add monster only
      case 263:     //jff 4/14/98 silent thing,line,line rev types
      case 264:     //jff 4/14/98 plus player/monster silent line
      case 265:     //            reversed types
      case 266:
      case 267:
      case 268:
      case 269:
	    ok = 1;
	  	break;
	}
	if (!ok)
	    return;
    }

    
    // Note: could use some const's here.
    switch (line->special)
    {
	// TRIGGERS.
	// All from here to RETRIGGERS.
      case 2:
	// Open Door
	EV_DoDoor(line,openDoor);
	line->special = 0;
	break;

      case 3:
	// Close Door
	EV_DoDoor(line,closeDoor);
	line->special = 0;
	break;

      case 4:
	// Raise Door
	EV_DoDoor(line,normal);
	line->special = 0;
	break;
	
      case 5:
	// Raise Floor
	EV_DoFloor(line,raiseFloor);
	line->special = 0;
	break;
	
      case 6:
	// Fast Ceiling Crush & Raise
	EV_DoCeiling(line,fastCrushAndRaise);
	line->special = 0;
	break;
	
      case 8:
	// Build Stairs
	EV_BuildStairs(line,build8);
	line->special = 0;
	break;
	
      case 10:
	// PlatDownWaitUp
	EV_DoPlat(line,downWaitUpStay,0);
	line->special = 0;
	break;
	
      case 12:
	// Light Turn On - brightest near
	EV_LightTurnOn(line,0);
	line->special = 0;
	break;
	
      case 13:
	// Light Turn On 255
	EV_LightTurnOn(line,255);
	line->special = 0;
	break;
	
      case 16:
	// Close Door 30
	EV_DoDoor(line,close30ThenOpen);
	line->special = 0;
	break;
	
      case 17:
	// Start Light Strobing
	EV_StartLightStrobing(line);
	line->special = 0;
	break;
	
      case 19:
	// Lower Floor
	EV_DoFloor(line,lowerFloor);
	line->special = 0;
	break;
	
      case 22:
	// Raise floor to nearest height and change texture
	EV_DoPlat(line,raiseToNearestAndChange,0);
	line->special = 0;
	break;
	
      case 25:
	// Ceiling Crush and Raise
	EV_DoCeiling(line,crushAndRaise);
	line->special = 0;
	break;
	
      case 30:
	// Raise floor to shortest texture height
	//  on either side of lines.
	EV_DoFloor(line,raiseToTexture);
	line->special = 0;
	break;
	
      case 35:
	// Lights Very Dark
	EV_LightTurnOn(line,35);
	line->special = 0;
	break;
	
      case 36:
	// Lower Floor (TURBO)
	EV_DoFloor(line,turboLower);
	line->special = 0;
	break;
	
      case 37:
	// LowerAndChange
	EV_DoFloor(line,lowerAndChange);
	line->special = 0;
	break;
	
      case 38:
	// Lower Floor To Lowest
	EV_DoFloor( line, lowerFloorToLowest );
	line->special = 0;
	break;
	
      case 39:
	// TELEPORT!
	EV_Teleport( line, side, thing );
	line->special = 0;
	break;

      case 40:
	// RaiseCeilingLowerFloor
	EV_DoCeiling( line, raiseToHighest );
	EV_DoFloor( line, lowerFloorToLowest );
	line->special = 0;
	break;
	
      case 44:
	// Ceiling Crush
	EV_DoCeiling( line, lowerAndCrush );
	line->special = 0;
	break;
	
      case 52:
	// EXIT!
	G_ExitLevel ();
	break;
	
      case 53:
	// Perpetual Platform Raise
	EV_DoPlat(line,perpetualRaise,0);
	line->special = 0;
	break;
	
      case 54:
	// Platform Stop
	EV_StopPlat(line);
	line->special = 0;
	break;

      case 56:
	// Raise Floor Crush
	EV_DoFloor(line,raiseFloorCrush);
	line->special = 0;
	break;

      case 57:
	// Ceiling Crush Stop
	EV_CeilingCrushStop(line);
	line->special = 0;
	break;
	
      case 58:
	// Raise Floor 24
	EV_DoFloor(line,raiseFloor24);
	line->special = 0;
	break;

      case 59:
	// Raise Floor 24 And Change
	EV_DoFloor(line,raiseFloor24AndChange);
	line->special = 0;
	break;

	  case 100:
	// Build Stairs Turbo 16
	EV_BuildStairs(line,turbo16);
	line->special = 0;
	break;
	
      case 104:
	// Turn lights off in sector(tag)
	EV_TurnTagLightsOff(line);
	line->special = 0;
	break;
	
      case 108:
	// Blazing Door Raise (faster than TURBO!)
	EV_DoDoor (line,blazeRaise);
	line->special = 0;
	break;
	
      case 109:
	// Blazing Door Open (faster than TURBO!)
	EV_DoDoor (line,blazeOpen);
	line->special = 0;
	break;
	
      case 110:
	// Blazing Door Close (faster than TURBO!)
	EV_DoDoor (line,blazeClose);
	line->special = 0;
	break;

      case 119:
	// Raise floor to nearest surr. floor
	EV_DoFloor(line,raiseFloorToNearest);
	line->special = 0;
	break;
	
      case 121:
	// Blazing PlatDownWaitUpStay
	EV_DoPlat(line,blazeDWUS,0);
	line->special = 0;
	break;
	
      case 124:
	// Secret EXIT
	G_SecretExitLevel ();
	break;
		
      case 125:
	// TELEPORT MonsterONLY
	if (!thing->player)
	{
	    EV_Teleport( line, side, thing );
	    line->special = 0;
	}
	break;
	
      case 130:
	// Raise Floor Turbo
	EV_DoFloor(line,raiseFloorTurbo);
	line->special = 0;
	break;
	
      case 141:
	// Silent Ceiling Crush & Raise
	EV_DoCeiling(line,silentCrushAndRaise);
	line->special = 0;
	break;
	
	// RETRIGGERS.  All from here till end.
      case 72:
	// Ceiling Crush
	EV_DoCeiling( line, lowerAndCrush );
	break;

      case 73:
	// Ceiling Crush and Raise
	EV_DoCeiling(line,crushAndRaise);
	break;

      case 74:
	// Ceiling Crush Stop
	EV_CeilingCrushStop(line);
	break;
	
      case 75:
	// Close Door
	EV_DoDoor(line,closeDoor);
	break;
	
      case 76:
	// Close Door 30
	EV_DoDoor(line,close30ThenOpen);
	break;
	
      case 77:
	// Fast Ceiling Crush & Raise
	EV_DoCeiling(line,fastCrushAndRaise);
	break;
	
      case 79:
	// Lights Very Dark
	EV_LightTurnOn(line,35);
	break;
	
      case 80:
	// Light Turn On - brightest near
	EV_LightTurnOn(line,0);
	break;
	
      case 81:
	// Light Turn On 255
	EV_LightTurnOn(line,255);
	break;
	
      case 82:
	// Lower Floor To Lowest
	EV_DoFloor( line, lowerFloorToLowest );
	break;
	
      case 83:
	// Lower Floor
	EV_DoFloor(line,lowerFloor);
	break;

      case 84:
	// LowerAndChange
	EV_DoFloor(line,lowerAndChange);
	break;

      case 86:
	// Open Door
	EV_DoDoor(line,openDoor);
	break;
	
      case 87:
	// Perpetual Platform Raise
	EV_DoPlat(line,perpetualRaise,0);
	break;
	
      case 88:
	// PlatDownWaitUp
	EV_DoPlat(line,downWaitUpStay,0);
	break;
	
      case 89:
	// Platform Stop
	EV_StopPlat(line);
	break;
	
      case 90:
	// Raise Door
	EV_DoDoor(line,normal);
	break;
	
      case 91:
	// Raise Floor
	EV_DoFloor(line,raiseFloor);
	break;
	
      case 92:
	// Raise Floor 24
	EV_DoFloor(line,raiseFloor24);
	break;
	
      case 93:
	// Raise Floor 24 And Change
	EV_DoFloor(line,raiseFloor24AndChange);
	break;
	
      case 94:
	// Raise Floor Crush
	EV_DoFloor(line,raiseFloorCrush);
	break;
	
      case 95:
	// Raise floor to nearest height
	// and change texture.
	EV_DoPlat(line,raiseToNearestAndChange,0);
	break;
	
      case 96:
	// Raise floor to shortest texture height
	// on either side of lines.
	EV_DoFloor(line,raiseToTexture);
	break;
	
      case 97:
	// TELEPORT!
	EV_Teleport( line, side, thing );
	break;
	
      case 98:
	// Lower Floor (TURBO)
	EV_DoFloor(line,turboLower);
	break;

      case 105:
	// Blazing Door Raise (faster than TURBO!)
	EV_DoDoor (line,blazeRaise);
	break;
	
      case 106:
	// Blazing Door Open (faster than TURBO!)
	EV_DoDoor (line,blazeOpen);
	break;

      case 107:
	// Blazing Door Close (faster than TURBO!)
	EV_DoDoor (line,blazeClose);
	break;

      case 120:
	// Blazing PlatDownWaitUpStay.
	EV_DoPlat(line,blazeDWUS,0);
	break;
	
      case 126:
	// TELEPORT MonsterONLY.
	if (!thing->player)
	    EV_Teleport( line, side, thing );
	break;
	
      case 128:
	// Raise To Nearest Floor
	EV_DoFloor(line,raiseFloorToNearest);
	break;
	
      case 129:
	// Raise Floor Turbo
	EV_DoFloor(line,raiseFloorTurbo);
	break;

		// Extended walk triggers

      // jff 1/29/98 added new linedef types to fill all functions out so that
      // all have varieties SR, S1, WR, W1

      // killough 1/31/98: "factor out" compatibility test, by
      // adding inner switch qualified by compatibility flag.
      // relax test to demo_compatibility

      // killough 2/16/98: Fix problems with W1 types being cleared too early

	  // Extended walk once triggers

          case 142:
            // Raise Floor 512
            // 142 W1  EV_DoFloor(raiseFloor512)
            if (EV_DoFloor(line,raiseFloor512))
              line->special = 0;
            break;

          case 143:
            // Raise Floor 24 and change
            // 143 W1  EV_DoPlat(raiseAndChange,24)
            if (EV_DoPlat(line,raiseAndChange,24))
              line->special = 0;
            break;

          case 144:
            // Raise Floor 32 and change
            // 144 W1  EV_DoPlat(raiseAndChange,32)
            if (EV_DoPlat(line,raiseAndChange,32))
              line->special = 0;
            break;

          case 145:
            // Lower Ceiling to Floor
            // 145 W1  EV_DoCeiling(lowerToFloor)
            if (EV_DoCeiling( line, lowerToFloor ))
              line->special = 0;
            break;

          case 146:
            // Lower Pillar, Raise Donut
            // 146 W1  EV_DoDonut()
            if (EV_DoDonut(line))
              line->special = 0;
            break;

          case 199:
            // Lower ceiling to lowest surrounding ceiling
            // 199 W1 EV_DoCeiling(lowerToLowest)
            if (EV_DoCeiling(line,lowerToLowest))
              line->special = 0;
            break;

          case 200:
            // Lower ceiling to highest surrounding floor
            // 200 W1 EV_DoCeiling(lowerToMaxFloor)
            if (EV_DoCeiling(line,lowerToMaxFloor))
              line->special = 0;
            break;

          case 207:
            // killough 2/16/98: W1 silent teleporter (normal kind)
            if (EV_SilentTeleport(line, side, thing))
              line->special = 0;
            break;

            //jff 3/16/98 renumber 215->153
          case 153: //jff 3/15/98 create texture change no motion type
            // Texture/Type Change Only (Trig)
            // 153 W1 Change Texture/Type Only
            if (EV_DoChange(line,trigChangeOnly))
              line->special = 0;
            break;

          case 239: //jff 3/15/98 create texture change no motion type
            // Texture/Type Change Only (Numeric)
            // 239 W1 Change Texture/Type Only
            if (EV_DoChange(line,numChangeOnly))
              line->special = 0;
            break;

          case 219:
            // Lower floor to next lower neighbor
            // 219 W1 Lower Floor Next Lower Neighbor
            if (EV_DoFloor(line,lowerFloorToNearest))
              line->special = 0;
            break;

          case 227:
            // Raise elevator next floor
            // 227 W1 Raise Elevator next floor
            if (EV_DoElevator(line,elevateUp))
              line->special = 0;
            break;

          case 231:
            // Lower elevator next floor
            // 231 W1 Lower Elevator next floor
            if (EV_DoElevator(line,elevateDown))
              line->special = 0;
            break;

          case 235:
            // Elevator to current floor
            // 235 W1 Elevator to current floor
            if (EV_DoElevator(line,elevateCurrent))
              line->special = 0;
            break;

          case 243: //jff 3/6/98 make fit within DCK's 256 linedef types
            // killough 2/16/98: W1 silent teleporter (linedef-linedef kind)
            if (EV_SilentLineTeleport(line, side, thing, false))
              line->special = 0;
            break;

          case 262: //jff 4/14/98 add silent line-line reversed
            if (EV_SilentLineTeleport(line, side, thing, true))
              line->special = 0;
            break;

          case 264: //jff 4/14/98 add monster-only silent line-line reversed
            if (!thing->player &&
                EV_SilentLineTeleport(line, side, thing, true))
              line->special = 0;
            break;

          case 266: //jff 4/14/98 add monster-only silent line-line
            if (!thing->player &&
                EV_SilentLineTeleport(line, side, thing, false))
              line->special = 0;
            break;

          case 268: //jff 4/14/98 add monster-only silent
            if (!thing->player && EV_SilentTeleport(line, side, thing))
              line->special = 0;
            break;

          //jff 1/29/98 end of added W1 linedef types

          // Extended walk many retriggerable

          //jff 1/29/98 added new linedef types to fill all functions
          //out so that all have varieties SR, S1, WR, W1

          case 147:
            // Raise Floor 512
            // 147 WR  EV_DoFloor(raiseFloor512)
            EV_DoFloor(line,raiseFloor512);
            break;

          case 148:
            // Raise Floor 24 and Change
            // 148 WR  EV_DoPlat(raiseAndChange,24)
            EV_DoPlat(line,raiseAndChange,24);
            break;

          case 149:
            // Raise Floor 32 and Change
            // 149 WR  EV_DoPlat(raiseAndChange,32)
            EV_DoPlat(line,raiseAndChange,32);
            break;

          case 150:
            // Start slow silent crusher
            // 150 WR  EV_DoCeiling(silentCrushAndRaise)
            EV_DoCeiling(line,silentCrushAndRaise);
            break;

          case 151:
            // RaiseCeilingLowerFloor
            // 151 WR  EV_DoCeiling(raiseToHighest),
            //         EV_DoFloor(lowerFloortoLowest)
            EV_DoCeiling( line, raiseToHighest );
            EV_DoFloor( line, lowerFloorToLowest );
            break;

          case 152:
            // Lower Ceiling to Floor
            // 152 WR  EV_DoCeiling(lowerToFloor)
            EV_DoCeiling( line, lowerToFloor );
            break;

            //jff 3/16/98 renumber 153->256
          case 256:
            // Build stairs, step 8
            // 256 WR EV_BuildStairs(build8)
            EV_BuildStairs(line,build8);
            break;

            //jff 3/16/98 renumber 154->257
          case 257:
            // Build stairs, step 16
            // 257 WR EV_BuildStairs(turbo16)
            EV_BuildStairs(line,turbo16);
            break;

          case 155:
            // Lower Pillar, Raise Donut
            // 155 WR  EV_DoDonut()
            EV_DoDonut(line);
            break;

          case 156:
            // Start lights strobing
            // 156 WR Lights EV_StartLightStrobing()
            EV_StartLightStrobing(line);
            break;

          case 157:
            // Lights to dimmest near
            // 157 WR Lights EV_TurnTagLightsOff()
            EV_TurnTagLightsOff(line);
            break;

          case 201:
            // Lower ceiling to lowest surrounding ceiling
            // 201 WR EV_DoCeiling(lowerToLowest)
            EV_DoCeiling(line,lowerToLowest);
            break;

          case 202:
            // Lower ceiling to highest surrounding floor
            // 202 WR EV_DoCeiling(lowerToMaxFloor)
            EV_DoCeiling(line,lowerToMaxFloor);
            break;

          case 208:
            // killough 2/16/98: WR silent teleporter (normal kind)
            EV_SilentTeleport(line, side, thing);
            break;

          case 212: //jff 3/14/98 create instant toggle floor type
            // Toggle floor between C and F instantly
            // 212 WR Instant Toggle Floor
            EV_DoPlat(line,toggleUpDn,0);
            break;

          //jff 3/16/98 renumber 216->154
          case 154: //jff 3/15/98 create texture change no motion type
            // Texture/Type Change Only (Trigger)
            // 154 WR Change Texture/Type Only
            EV_DoChange(line,trigChangeOnly);
            break;

          case 240: //jff 3/15/98 create texture change no motion type
            // Texture/Type Change Only (Numeric)
            // 240 WR Change Texture/Type Only
            EV_DoChange(line,numChangeOnly);
            break;

          case 220:
            // Lower floor to next lower neighbor
            // 220 WR Lower Floor Next Lower Neighbor
            EV_DoFloor(line,lowerFloorToNearest);
            break;

          case 228:
            // Raise elevator next floor
            // 228 WR Raise Elevator next floor
            EV_DoElevator(line,elevateUp);
            break;

          case 232:
            // Lower elevator next floor
            // 232 WR Lower Elevator next floor
            EV_DoElevator(line,elevateDown);
            break;

          case 236:
            // Elevator to current floor
            // 236 WR Elevator to current floor
            EV_DoElevator(line,elevateCurrent);
            break;

          case 244: //jff 3/6/98 make fit within DCK's 256 linedef types
            // killough 2/16/98: WR silent teleporter (linedef-linedef kind)
            EV_SilentLineTeleport(line, side, thing, false);
            break;

          case 263: //jff 4/14/98 add silent line-line reversed
            EV_SilentLineTeleport(line, side, thing, true);
            break;

          case 265: //jff 4/14/98 add monster-only silent line-line reversed
            if (!thing->player)
              EV_SilentLineTeleport(line, side, thing, true);
            break;

          case 267: //jff 4/14/98 add monster-only silent line-line
            if (!thing->player)
              EV_SilentLineTeleport(line, side, thing, false);
            break;

          case 269: //jff 4/14/98 add monster-only silent
            if (!thing->player)
              EV_SilentTeleport(line, side, thing);
            break;

            //jff 1/29/98 end of added WR linedef types
    }
}



//
// P_ShootSpecialLine - IMPACT SPECIALS
// Called when a thing shoots a special line.
//
void
P_ShootSpecialLine
( mobj_t*	thing,
  line_t*	line )
{

	// pointer to line function is NULL by default, set non-null if
    // line special is walkover generalized linedef type
    int (*linefunc)(line_t *line)=NULL;

    // check each range of generalized linedefs
    if ((unsigned)line->special >= GenEnd)
    {
      // Out of range for GenFloors
    }
    else if ((unsigned)line->special >= GenFloorBase)
    {
      if (!thing->player)
        if ((line->special & FloorChange) || !(line->special & FloorModel))
          return; // FloorModel is "Allow Monsters" if FloorChange is 0
      if (/*!comperr(comperr_zerotag) &&*/ !line->tag && ((line->special&6)!=6)) //e6y //jff 2/27/98 all non-manual
        return;                         // generalized types require tag
      linefunc = EV_DoGenFloor;
    }
    else if ((unsigned)line->special >= GenCeilingBase)
    {
      if (!thing->player)
        if ((line->special & CeilingChange) || !(line->special & CeilingModel))
          return;   // CeilingModel is "Allow Monsters" if CeilingChange is 0
      if (/*!comperr(comperr_zerotag) &&*/ !line->tag && ((line->special&6)!=6)) //e6y //jff 2/27/98 all non-manual
        return;                         // generalized types require tag
      linefunc = EV_DoGenCeiling;
    }
    else if ((unsigned)line->special >= GenDoorBase)
    {
      if (!thing->player)
      {
        if (!(line->special & DoorMonster))
          return;   // monsters disallowed from this door
        if (line->flags & ML_SECRET) // they can't open secret doors either
          return;
      }
      if (/*!comperr(comperr_zerotag) &&*/ !line->tag && ((line->special&6)!=6)) //e6y //jff 3/2/98 all non-manual
        return;                         // generalized types require tag
      linefunc = EV_DoGenDoor;
    }
    else if ((unsigned)line->special >= GenLockedBase)
    {
      if (!thing->player)
        return;   // monsters disallowed from unlocking doors
      if (!P_CanUnlockGenDoor(line,thing->player))
        return;
      if (/*!comperr(comperr_zerotag) &&*/ !line->tag && ((line->special&6)!=6)) //e6y //jff 2/27/98 all non-manual
        return;                         // generalized types require tag

      linefunc = EV_DoGenLockedDoor;
    }
    else if ((unsigned)line->special >= GenLiftBase)
    {
      if (!thing->player)
        if (!(line->special & LiftMonster))
          return; // monsters disallowed
      if (/*!comperr(comperr_zerotag) &&*/ !line->tag && ((line->special&6)!=6)) //e6y //jff 2/27/98 all non-manual
        return;                         // generalized types require tag
      linefunc = EV_DoGenLift;
    }
    else if ((unsigned)line->special >= GenStairsBase)
    {
      if (!thing->player)
        if (!(line->special & StairMonster))
          return; // monsters disallowed
      if (/*!comperr(comperr_zerotag) &&*/ !line->tag && ((line->special&6)!=6)) //e6y //jff 2/27/98 all non-manual
        return;                         // generalized types require tag
      linefunc = EV_DoGenStairs;
    }
    else if ((unsigned)line->special >= GenCrusherBase)
    {
      if (!thing->player)
        if (!(line->special & CrusherMonster))
          return; // monsters disallowed
      if (/*!comperr(comperr_zerotag) &&*/ !line->tag && ((line->special&6)!=6)) //e6y //jff 2/27/98 all non-manual
        return;                         // generalized types require tag
      linefunc = EV_DoGenCrusher;
    }

    if (linefunc)
      switch((line->special & TriggerType) >> TriggerTypeShift)
      {
        case GunOnce:
          if (linefunc(line))
            P_ChangeSwitchTexture(line,0);
          return;
        case GunMany:
          if (linefunc(line))
            P_ChangeSwitchTexture(line,1);
          return;
        default:  // if not a gun type, do nothing here
          return;
      }

    int		ok;
    
    //	Impacts that other things can activate.
    if (!thing->player)
    {
	ok = 0;
	switch(line->special)
	{
	  case 46:
	    // OPEN DOOR IMPACT
	    ok = 1;
	    break;
	}
	if (!ok)
	    return;
    }

    switch(line->special)
    {
      case 24:
	// RAISE FLOOR
	EV_DoFloor(line,raiseFloor);
	P_ChangeSwitchTexture(line,0);
	break;
	
      case 46:
	// OPEN DOOR
	EV_DoDoor(line,openDoor);
	P_ChangeSwitchTexture(line,1);
	break;
	
      case 47:
	// RAISE FLOOR NEAR AND CHANGE
	EV_DoPlat(line,raiseToNearestAndChange,0);
	P_ChangeSwitchTexture(line,0);
	break;

	//jff 1/30/98 added new gun linedefs here
    // killough 1/31/98: added demo_compatibility check, added inner switch

	case 197:
	// Exit to next level
	// killough 10/98: prevent zombies from exiting levels
	if(thing->player && thing->player->health<=0)
		break;
	P_ChangeSwitchTexture(line,0);
	G_ExitLevel();
	break;

	case 198:
	// Exit to secret level
	// killough 10/98: prevent zombies from exiting levels
	if(thing->player && thing->player->health<=0)
		break;
	P_ChangeSwitchTexture(line,0);
	G_SecretExitLevel();
	break;
	//jff end addition of new gun linedefs
    }
}



//
// P_PlayerInSpecialSector
// Called every tic frame
//  that the player origin is in a special sector
//
void P_PlayerInSpecialSector (player_t* player)
{
    sector_t*	sector;
    extern int showMessages;
    static sector_t*	error;
	
    sector = player->mo->subsector->sector;

    // Falling, not all the way down yet?
    if (player->mo->z != sector->floorheight)
	return;	

    // Has hitten ground.
    switch (sector->special)
    {
      case 5:
	// HELLSLIME DAMAGE
	// [crispy] no nukage damage with NOCLIP cheat
	if (!player->powers[pw_ironfeet] && !(player->mo->flags & MF_NOCLIP))
	    if (!(leveltime&0x1f))
		P_DamageMobj (player->mo, NULL, NULL, 10);
	break;
	
      case 7:
	// NUKAGE DAMAGE
	// [crispy] no nukage damage with NOCLIP cheat
	if (!player->powers[pw_ironfeet] && !(player->mo->flags & MF_NOCLIP))
	    if (!(leveltime&0x1f))
		P_DamageMobj (player->mo, NULL, NULL, 5);
	break;
	
      case 16:
	// SUPER HELLSLIME DAMAGE
      case 4:
	// STROBE HURT
	// [crispy] no nukage damage with NOCLIP cheat
	if ((!player->powers[pw_ironfeet]
	    || (P_Random()<5) ) && !(player->mo->flags & MF_NOCLIP))
	{
	    if (!(leveltime&0x1f))
		P_DamageMobj (player->mo, NULL, NULL, 20);
	}
	break;
			
      case 9:
	// SECRET SECTOR
	player->secretcount++;
	sector->special = 0;
	break;
			
      case 11:
	// EXIT SUPER DAMAGE! (for E1M8 finale)
	player->cheats &= ~CF_GODMODE;

	if (!(leveltime&0x1f))
	    P_DamageMobj (player->mo, NULL, NULL, 20);

	if (player->health <= 10)
	    G_ExitLevel();
	break;
			
      default:
	// [crispy] ignore unknown special sectors
	if (error != sector)
	{
	error = sector;
	fprintf (stderr, "P_PlayerInSpecialSector: "
		 "unknown special %i\n",
		 sector->special);
	}
	break;
    };
}




//
// P_UpdateSpecials
// Animate planes, scroll walls, etc.
//
boolean		levelTimer;
int		levelTimeCount;

void P_UpdateSpecials (void)
{
    anim_t*	anim;
    int		pic;
    int		i;
    line_t*	line;

    
    //	LEVEL TIMER
    if (levelTimer == true)
    {
	levelTimeCount--;
	if (!levelTimeCount)
	    G_ExitLevel();
    }
    
    //	ANIMATE FLATS AND TEXTURES GLOBALLY
    for (anim = anims ; anim < lastanim ; anim++)
    {
	for (i=anim->basepic ; i<anim->basepic+anim->numpics ; i++)
	{
	    pic = anim->basepic + ( (leveltime/anim->speed + i)%anim->numpics );
	    if (anim->istexture)
		texturetranslation[i] = pic;
	    else
	    {
		// [crispy] add support for SMMU swirling flats
		if (anim->speed > 65535 || anim->numpics == 1)
		{
		    flattranslation[i] = -1;
		}
		else
		flattranslation[i] = pic;
	    }
	}
    }

    
    //	ANIMATE LINE SPECIALS
    for (i = 0; i < numlinespecials; i++)
    {
	line = linespeciallist[i];
	switch(line->special)
	{
	  case 48:
	    // EFFECT FIRSTCOL SCROLL +
	    // [crispy] smooth texture scrolling
	    sides[line->sidenum[0]].basetextureoffset += FRACUNIT;
	    sides[line->sidenum[0]].textureoffset =
	    sides[line->sidenum[0]].basetextureoffset;
	    break;
	  case 85:
	    // [JN] (Boom) Scroll Texture Right
	    // [crispy] smooth texture scrolling
	    sides[line->sidenum[0]].basetextureoffset -= FRACUNIT;
	    sides[line->sidenum[0]].textureoffset =
	    sides[line->sidenum[0]].basetextureoffset;
	    break;
	  case 255:
	  	// killough 3/2/98: scroll according to sidedef offsets
	    sides[line->sidenum[0]].textureoffset -= sides[line->sidenum[0]].basetextureoffset;
		sides[line->sidenum[0]].rowoffset +=  sides[line->sidenum[0]].baserowoffset;
	    break;
    case 252:
    case 253:
    {
      mobj_t* thing;
      fixed_t dx = line->dx >> SCROLL_SHIFT;  // direction and speed of scrolling
      fixed_t dy = line->dy >> SCROLL_SHIFT;
      dx = FixedMul(dx,CARRYFACTOR);
      dy = FixedMul(dy,CARRYFACTOR);
      int s;
      for (s = -1; (s = P_FindSectorFromLineTag(line,s)) >= 0;)
      {
      sector_t* sec = &sectors[s];
      fixed_t height = sec->floorheight;
      fixed_t waterheight = sec->heightsec != -1 &&
        sectors[sec->heightsec].floorheight > height ?
        sectors[sec->heightsec].floorheight : INT_MIN;

      // Handle all things in sector.
      for (thing = sec->thinglist ; thing ; thing = thing->snext)
        if (!((thing->flags & MF_NOCLIP) &&
            (!(thing->flags & MF_NOGRAVITY || thing->z > height) ||
             thing->z < waterheight)))
        {
          // Move objects only if on floor or underwater,
          // non-floating, and clipped.
          thing->momx += dx;
          thing->momy += dy;
        }
      }
    }
      break;
	}
    }

    
    //	DO BUTTONS
    for (i = 0; i < maxbuttons; i++)
	if (buttonlist[i].btimer)
	{
	    buttonlist[i].btimer--;
	    if (!buttonlist[i].btimer)
	    {
		switch(buttonlist[i].where)
		{
		  case top:
		    sides[buttonlist[i].line->sidenum[0]].toptexture =
			buttonlist[i].btexture;
		    break;
		    
		  case middle:
		    sides[buttonlist[i].line->sidenum[0]].midtexture =
			buttonlist[i].btexture;
		    break;
		    
		  case bottom:
		    sides[buttonlist[i].line->sidenum[0]].bottomtexture =
			buttonlist[i].btexture;
		    break;
		}
		// [crispy] & [JN] Logically proper sound behavior.
		// Do not play second "sfx_swtchn" on two-sided linedefs that attached to special sectors,
		// and always play second sound on single-sided linedefs.
		// if (!buttonlist[i].line->backsector /*|| !buttonlist[i].line->backsector->specialdata*/)
		// {
		// 	S_StartSoundOnce(buttonlist[i].soundorg,sfx_swtchn);
		// }

		S_StartSoundOnce(&buttonlist[i].soundorg,sfx_swtchn);
		memset(&buttonlist[i],0,sizeof(button_t));
	    }
	}

    // [crispy] draw fuzz effect independent of rendering frame rate
    // R_SetFuzzPosTic();
}

// [crispy] smooth texture scrolling
void R_InterpolateTextureOffsets (void)
{
	// if (crispy->uncapped && leveltime > oldleveltime)
	// {
	// 	int i;

	// 	for (i = 0; i < numlinespecials; i++)
	// 	{
	// 		const line_t *const line = linespeciallist[i];
	// 		side_t *const side = &sides[line->sidenum[0]];

	// 		if (line->special == 48)
	// 		{
	// 			side->textureoffset = side->basetextureoffset + fractionaltic;
	// 		}
	// 		else
	// 		if (line->special == 85)
	// 		{
	// 			side->textureoffset = side->basetextureoffset - fractionaltic;
	// 		}
	// 	}
	// }
}

//
// Donut overrun emulation
//
// Derived from the code from PrBoom+.  Thanks go to Andrey Budko (entryway)
// as usual :-)
//

#define DONUT_FLOORHEIGHT_DEFAULT 0x00000000
#define DONUT_FLOORPIC_DEFAULT 0x16

static void DonutOverrun(fixed_t *s3_floorheight, short *s3_floorpic,
                         line_t *line, sector_t *pillar_sector)
{
    static int first = 1;
    static int tmp_s3_floorheight;
    static int tmp_s3_floorpic;

    extern int numflats;

    if (first)
    {
        int p;

        // This is the first time we have had an overrun.
        first = 0;

        // Default values
        tmp_s3_floorheight = DONUT_FLOORHEIGHT_DEFAULT;
        tmp_s3_floorpic = DONUT_FLOORPIC_DEFAULT;

        //!
        // @category compat
        // @arg <x> <y>
        //
        // Use the specified magic values when emulating behavior caused
        // by memory overruns from improperly constructed donuts.
        // In Vanilla Doom this can differ depending on the operating
        // system.  The default (if this option is not specified) is to
        // emulate the behavior when running under Windows 98.

        p = M_CheckParmWithArgs("-donut", 2);

        if (p > 0)
        {
            // Dump of needed memory: (fixed_t)0000:0000 and (short)0000:0008
            //
            // C:\>debug
            // -d 0:0
            //
            // DOS 6.22:
            // 0000:0000    (57 92 19 00) F4 06 70 00-(16 00)
            // DOS 7.1:
            // 0000:0000    (9E 0F C9 00) 65 04 70 00-(16 00)
            // Win98:
            // 0000:0000    (00 00 00 00) 65 04 70 00-(16 00)
            // DOSBox under XP:
            // 0000:0000    (00 00 00 F1) ?? ?? ?? 00-(07 00)

            M_StrToInt(myargv[p + 1], &tmp_s3_floorheight);
            M_StrToInt(myargv[p + 2], &tmp_s3_floorpic);

            if (tmp_s3_floorpic >= numflats)
            {
                fprintf(stderr,
                        "DonutOverrun: The second parameter for \"-donut\" "
                        "switch should be greater than 0 and less than number "
                        "of flats (%d). Using default value (%d) instead. \n",
                        numflats, DONUT_FLOORPIC_DEFAULT);
                tmp_s3_floorpic = DONUT_FLOORPIC_DEFAULT;
            }
        }
    }

    /*
    fprintf(stderr,
            "Linedef: %d; Sector: %d; "
            "New floor height: %d; New floor pic: %d\n",
            line->iLineID, pillar_sector->iSectorID,
            tmp_s3_floorheight >> 16, tmp_s3_floorpic);
     */

    *s3_floorheight = (fixed_t) tmp_s3_floorheight;
    *s3_floorpic = (short) tmp_s3_floorpic;
}


//
// Special Stuff that can not be categorized
//
int EV_DoDonut(line_t*	line)
{
    sector_t*		s1;
    sector_t*		s2;
    sector_t*		s3;
    int			secnum;
    int			rtn;
    int			i;
    floormove_t*	floor;
    fixed_t s3_floorheight;
    short s3_floorpic;

    secnum = -1;
    rtn = 0;
    while ((secnum = P_FindSectorFromLineTag(line,secnum)) >= 0)
    {
	s1 = &sectors[secnum];

	// ALREADY MOVING?  IF SO, KEEP GOING...
	if (s1->floordata)
	    continue;

	rtn = 1;
	s2 = getNextSector(s1->lines[0],s1);

        // Vanilla Doom does not check if the linedef is one sided.  The
        // game does not crash, but reads invalid memory and causes the
        // sector floor to move "down" to some unknown height.
        // DOSbox prints a warning about an invalid memory access.
        //
        // I'm not sure exactly what invalid memory is being read.  This
        // isn't something that should be done, anyway.
        // Just print a warning and return.

        if (s2 == NULL)
        {
            fprintf(stderr,
                    "EV_DoDonut: linedef had no second sidedef! "
                    "Unexpected behavior may occur in Vanilla Doom. \n");
	    break;
        }

	for (i = 0; i < s2->linecount; i++)
	{
	    s3 = s2->lines[i]->backsector;

	    if (s3 == s1)
		continue;

            if (s3 == NULL)
            {
                // e6y
                // s3 is NULL, so
                // s3->floorheight is an int at 0000:0000
                // s3->floorpic is a short at 0000:0008
                // Trying to emulate

                fprintf(stderr,
                        "EV_DoDonut: WARNING: emulating buffer overrun due to "
                        "NULL back sector. "
                        "Unexpected behavior may occur in Vanilla Doom.\n");

                DonutOverrun(&s3_floorheight, &s3_floorpic, line, s1);
            }
            else
            {
                s3_floorheight = s3->floorheight;
                s3_floorpic = s3->floorpic;
            }

	    //	Spawn rising slime
	    floor = Z_Malloc (sizeof(*floor), PU_LEVSPEC, 0);
	    P_AddThinker (&floor->thinker);
	    s2->floordata = floor;
	    floor->thinker.function.acp1 = (actionf_p1) T_MoveFloor;
	    floor->type = donutRaise;
	    floor->crush = false;
	    floor->direction = 1;
	    floor->sector = s2;
	    floor->speed = FLOORSPEED / 2;
	    floor->texture = s3_floorpic;
	    floor->newspecial = 0;
	    floor->floordestheight = s3_floorheight;
	    
	    //	Spawn lowering donut-hole
	    floor = Z_Malloc (sizeof(*floor), PU_LEVSPEC, 0);
	    P_AddThinker (&floor->thinker);
	    s1->floordata = floor;
	    floor->thinker.function.acp1 = (actionf_p1) T_MoveFloor;
	    floor->type = lowerFloor;
	    floor->crush = false;
	    floor->direction = -1;
	    floor->sector = s1;
	    floor->speed = FLOORSPEED / 2;
	    floor->floordestheight = s3_floorheight;
	    break;
	}
    }
    return rtn;
}

// Hash the sector tags across the sectors and linedefs.
static void P_InitTagLists(void)
{
  register int i;

  for (i=numsectors; --i>=0; )        // Initially make all slots empty.
    sectors[i].firsttag = -1;
  for (i=numsectors; --i>=0; )        // Proceed from last to first sector
    {                                 // so that lower sectors appear first
      int j = (unsigned) sectors[i].tag % (unsigned) numsectors; // Hash func
      sectors[i].nexttag = sectors[j].firsttag;   // Prepend sector to chain
      sectors[j].firsttag = i;
    }

  // killough 4/17/98: same thing, only for linedefs

  for (i=numlines; --i>=0; )        // Initially make all slots empty.
    lines[i].firsttag = -1;
  for (i=numlines; --i>=0; )        // Proceed from last to first linedef
    {                               // so that lower linedefs appear first
      int j = (unsigned) lines[i].tag % (unsigned) numlines; // Hash func
      lines[i].nexttag = lines[j].firsttag;   // Prepend linedef to chain
      lines[j].firsttag = i;
    }
}

//
// SPECIAL SPAWNING
//

//
// P_SpawnSpecials
// After the map has been loaded, scan for specials
//  that spawn thinkers
//
short		numlinespecials;
line_t*		linespeciallist[MAXLINEANIMS];


// Parses command line parameters.
void P_SpawnSpecials (void)
{
    sector_t*	sector;
    int		i;

    // See if -TIMER was specified.

    if (timelimit > 0 && deathmatch)
    {
        levelTimer = true;
        levelTimeCount = timelimit * 60 * TICRATE;
    }
    else
    {
	levelTimer = false;
    }

    //	Init special SECTORs.
    sector = sectors;
    for (i=0 ; i<numsectors ; i++, sector++)
    {
	if (!sector->special)
	    continue;
	
	switch (sector->special)
	{
	  case 1:
	    // FLICKERING LIGHTS
	    P_SpawnLightFlash (sector);
	    break;

	  case 2:
	    // STROBE FAST
	    P_SpawnStrobeFlash(sector,FASTDARK,0);
	    break;
	    
	  case 3:
	    // STROBE SLOW
	    P_SpawnStrobeFlash(sector,SLOWDARK,0);
	    break;
	    
	  case 4:
	    // STROBE FAST/DEATH SLIME
	    P_SpawnStrobeFlash(sector,FASTDARK,0);
	    sector->special = 4;
	    break;
	    
	  case 8:
	    // GLOWING LIGHT
	    P_SpawnGlowingLight(sector);
	    break;
	  case 9:
	    // SECRET SECTOR
	    totalsecret++;
	    break;
	    
	  case 10:
	    // DOOR CLOSE IN 30 SECONDS
	    P_SpawnDoorCloseIn30 (sector);
	    break;
	    
	  case 12:
	    // SYNC STROBE SLOW
	    P_SpawnStrobeFlash (sector, SLOWDARK, 1);
	    break;

	  case 13:
	    // SYNC STROBE FAST
	    P_SpawnStrobeFlash (sector, FASTDARK, 1);
	    break;

	  case 14:
	    // DOOR RAISE IN 5 MINUTES
	    P_SpawnDoorRaiseIn5Mins (sector, i);
	    break;
	    
	  case 17:
	    P_SpawnFireFlicker(sector);
	    break;
	}
    }

	// P_InitTagLists() must be called before P_FindSectorFromLineTag()
  	// or P_FindLineFromLineTag() can be called.

  	P_InitTagLists();   // killough 1/30/98: Create xref tables for tags
    
    //	Init line EFFECTs
    numlinespecials = 0;
    for (i = 0;i < numlines; i++)
    {
	switch(lines[i].special)
	{
	  case 48:
	  case 85:  // [crispy] [JN] (Boom) Scroll Texture Right
	  case 255: // killough 3/2/98: scroll according to sidedef offsets
    case 252: // carry things
    case 253: // and scroll floor
            if (numlinespecials >= MAXLINEANIMS)
            {
                I_Error("Too many scrolling wall linedefs! "
                        "(Vanilla limit is 64)");
            }
	    // EFFECT FIRSTCOL SCROLL+
	    linespeciallist[numlinespecials] = &lines[i];
	    numlinespecials++;
	    break;

    // killough 3/7/98:
    // support for drawn heights coming from different sector
    case 242:
    {
      sector_t* sec = sides[lines[i].sidenum[0]].sector;
      int s;
      for (s = -1; (s = P_FindSectorFromLineTag(&lines[i],s)) >= 0;)
        sectors[s].heightsec = sec->id;
    }
      break;

    case 213:
    {
      sector_t* sec = sides[lines[i].sidenum[0]].sector;
      int s;
      for (s = -1; (s = P_FindSectorFromLineTag(&lines[i],s)) >= 0;)
        sectors[s].floorlightsec = sec->id;
    }
      break;

    case 261:
    {
      sector_t* sec = sides[lines[i].sidenum[0]].sector;
      int s;
      for (s = -1; (s = P_FindSectorFromLineTag(&lines[i],s)) >= 0;)
        sectors[s].ceilinglightsec = sec->id;
    }
      break;

	  // [crispy] add support for MBF sky tranfers
	  case 271:
	  case 272:
	    {
		int secnum;

		for (secnum = 0; secnum < numsectors; secnum++)
		{
		    if (sectors[secnum].tag == lines[i].tag)
		    {
			sectors[secnum].sky = i | PL_SKYFLAT;
		    }
		}
	    }
	    break;
	}
    }

    
    //	Init other misc stuff
    for (i = 0;i < MAXCEILINGS;i++)
	activeceilings[i] = NULL;

    for (i = 0;i < MAXPLATS;i++)
	activeplats[i] = NULL;
    
    for (i = 0;i < maxbuttons;i++)
	memset(&buttonlist[i],0,sizeof(button_t));

    // UNUSED: no horizonal sliders.
    //	P_InitSlidingDoorFrames();
}

//
// P_SectorActive()
//
// Passed a linedef special class (floor, ceiling, lighting) and a sector
// returns whether the sector is already busy with a linedef special of the
// same class. If old demo compatibility true, all linedef special classes
// are the same.
//
// jff 2/23/98 added to prevent old demos from
//  succeeding in starting multiple specials on one sector
//
boolean P_SectorActive(special_e t, const sector_t *sec)
{
    switch (t) // return whether thinker of same type is active
    {
      case floor_special:
        return sec->floordata != NULL;
      case ceiling_special:
        return sec->ceilingdata != NULL;
      case lighting_special:
        return sec->lightingdata != NULL;
    }
  return true; // don't know which special, must be active, shouldn't be here
}

//
// P_FindNextLowestFloor()
//
// Passed a sector and a floor height, returns the fixed point value
// of the largest floor height in a surrounding sector smaller than
// the floor height passed. If no such height exists the floorheight
// passed is returned.
//
// jff 02/03/98 Twiddled Lee's P_FindNextHighestFloor to make this
//
fixed_t P_FindNextLowestFloor(sector_t *sec, int currentheight)
{
  sector_t *other;
  int i;

  for (i=0 ;i < sec->linecount ; i++)
    if ((other = getNextSector(sec->lines[i],sec)) &&
         other->floorheight < currentheight)
    {
      int height = other->floorheight;
      while (++i < sec->linecount)
        if ((other = getNextSector(sec->lines[i],sec)) &&
            other->floorheight > height &&
            other->floorheight < currentheight)
          height = other->floorheight;
      return height;
    }
  return currentheight;
}

//
// P_FindShortestTextureAround()
//
// Passed a sector number, returns the shortest lower texture on a
// linedef bounding the sector.
//
// Note: If no lower texture exists 32000*FRACUNIT is returned.
//       but if compatibility then INT_MAX is returned
//
// jff 02/03/98 Add routine to find shortest lower texture
//
fixed_t P_FindShortestTextureAround(int secnum)
{
  int minsize = INT_MAX;
  side_t*     side;
  int i;
  sector_t *sec = &sectors[secnum];

	minsize = 32000<<FRACBITS; //jff 3/13/98 prevent overflow in height calcs

  for (i = 0; i < sec->linecount; i++)
  {
    if (twoSided(secnum, i))
    {
      side = getSide(secnum,i,0);
      if (side->bottomtexture > 0)  //jff 8/14/98 texture 0 is a placeholder
        if (textureheight[side->bottomtexture] < minsize)
          minsize = textureheight[side->bottomtexture];
      side = getSide(secnum,i,1);
      if (side->bottomtexture > 0)  //jff 8/14/98 texture 0 is a placeholder
        if (textureheight[side->bottomtexture] < minsize)
          minsize = textureheight[side->bottomtexture];
    }
  }
  return minsize;
}

//
// P_FindShortestUpperAround()
//
// Passed a sector number, returns the shortest upper texture on a
// linedef bounding the sector.
//
// Note: If no upper texture exists 32000*FRACUNIT is returned.
//       but if compatibility then INT_MAX is returned
//
// jff 03/20/98 Add routine to find shortest upper texture
//
fixed_t P_FindShortestUpperAround(int secnum)
{
  int minsize = INT_MAX;
  side_t*     side;
  int i;
  sector_t *sec = &sectors[secnum];

  
  minsize = 32000<<FRACBITS; //jff 3/13/98 prevent overflow
                               // in height calcs
  for (i = 0; i < sec->linecount; i++)
  {
    if (twoSided(secnum, i))
    {
      side = getSide(secnum,i,0);
      if (side->toptexture > 0)     //jff 8/14/98 texture 0 is a placeholder
        if (textureheight[side->toptexture] < minsize)
          minsize = textureheight[side->toptexture];
      side = getSide(secnum,i,1);
      if (side->toptexture > 0)     //jff 8/14/98 texture 0 is a placeholder
        if (textureheight[side->toptexture] < minsize)
          minsize = textureheight[side->toptexture];
    }
  }
  return minsize;
}

//
// P_FindModelCeilingSector()
//
// Passed a ceiling height and a sector number, return a pointer to a
// a sector with that ceiling height across the lowest numbered two sided
// line surrounding the sector.
//
// Note: If no sector at that height bounds the sector passed, return NULL
//
// jff 02/03/98 Add routine to find numeric model ceiling
//  around a sector specified by sector number
//  used only from generalized ceiling types
// jff 3/14/98 change first parameter to plain height to allow call
//  from routine not using ceiling_t
//
sector_t *P_FindModelCeilingSector(fixed_t ceildestheight,int secnum)
{
  int i;
  sector_t *sec=NULL;
  int linecount;

  sec = &sectors[secnum]; //jff 3/2/98 woops! better do this
  //jff 5/23/98 don't disturb sec->linecount while searching
  // but allow early exit in old demos
  linecount = sec->linecount;
  for (i = 0; i < (/*demo_compatibility &&*/ sec->linecount<linecount?
                   sec->linecount : linecount); i++)
  {
    if ( twoSided(secnum, i) )
    {
      if (getSide(secnum,i,0)->sector - sectors == secnum)
          sec = getSector(secnum,i,1);
      else
          sec = getSector(secnum,i,0);

      if (sec->ceilingheight == ceildestheight)
        return sec;
    }
  }
  return NULL;
}

//
// P_FindModelFloorSector()
//
// Passed a floor height and a sector number, return a pointer to a
// a sector with that floor height across the lowest numbered two sided
// line surrounding the sector.
//
// Note: If no sector at that height bounds the sector passed, return NULL
//
// jff 02/03/98 Add routine to find numeric model floor
//  around a sector specified by sector number
// jff 3/14/98 change first parameter to plain height to allow call
//  from routine not using floormove_t
//
sector_t *P_FindModelFloorSector(fixed_t floordestheight,int secnum)
{
  int i;
  sector_t *sec=NULL;
  int linecount;

  sec = &sectors[secnum]; //jff 3/2/98 woops! better do this
  //jff 5/23/98 don't disturb sec->linecount while searching
  // but allow early exit in old demos
  linecount = sec->linecount;
  for (i = 0; i < (/*demo_compatibility &&*/ sec->linecount<linecount?
                   sec->linecount : linecount); i++)
  {
    if ( twoSided(secnum, i) )
    {
      if (getSide(secnum,i,0)->sector - sectors == secnum)
          sec = getSector(secnum,i,1);
      else
          sec = getSector(secnum,i,0);

      if (sec->floorheight == floordestheight)
        return sec;
    }
  }
  return NULL;
}

//
// P_FindNextLowestCeiling()
//
// Passed a sector and a ceiling height, returns the fixed point value
// of the largest ceiling height in a surrounding sector smaller than
// the ceiling height passed. If no such height exists the ceiling height
// passed is returned.
//
// jff 02/03/98 Twiddled Lee's P_FindNextHighestFloor to make this
//
fixed_t P_FindNextLowestCeiling(sector_t *sec, int currentheight)
{
  sector_t *other;
  int i;

  for (i=0 ;i < sec->linecount ; i++)
    if ((other = getNextSector(sec->lines[i],sec)) &&
        other->ceilingheight < currentheight)
    {
      int height = other->ceilingheight;
      while (++i < sec->linecount)
        if ((other = getNextSector(sec->lines[i],sec)) &&
            other->ceilingheight > height &&
            other->ceilingheight < currentheight)
          height = other->ceilingheight;
      return height;
    }
  return currentheight;
}

//
// P_FindNextHighestCeiling()
//
// Passed a sector and a ceiling height, returns the fixed point value
// of the smallest ceiling height in a surrounding sector larger than
// the ceiling height passed. If no such height exists the ceiling height
// passed is returned.
//
// jff 02/03/98 Twiddled Lee's P_FindNextHighestFloor to make this
//
fixed_t P_FindNextHighestCeiling(sector_t *sec, int currentheight)
{
  sector_t *other;
  int i;

  for (i=0 ;i < sec->linecount ; i++)
    if ((other = getNextSector(sec->lines[i],sec)) &&
         other->ceilingheight > currentheight)
    {
      int height = other->ceilingheight;
      while (++i < sec->linecount)
        if ((other = getNextSector(sec->lines[i],sec)) &&
            other->ceilingheight < height &&
            other->ceilingheight > currentheight)
          height = other->ceilingheight;
      return height;
    }
  return currentheight;
}

//
// P_CanUnlockGenDoor()
//
// Passed a generalized locked door linedef and a player, returns whether
// the player has the keys necessary to unlock that door.
//
// Note: The linedef passed MUST be a generalized locked door type
//       or results are undefined.
//
// jff 02/05/98 routine added to test for unlockability of
//  generalized locked doors
//
boolean P_CanUnlockGenDoor
( line_t* line,
  player_t* player)
{
  // does this line special distinguish between skulls and keys?
  int skulliscard = (line->special & LockedNKeys)>>LockedNKeysShift;

  // determine for each case of lock type if player's keys are adequate
  switch((line->special & LockedKey)>>LockedKeyShift)
  {
    case AnyKey:
      if
      (
        !player->cards[it_redcard] &&
        !player->cards[it_redskull] &&
        !player->cards[it_bluecard] &&
        !player->cards[it_blueskull] &&
        !player->cards[it_yellowcard] &&
        !player->cards[it_yellowskull]
      )
      {
        player->message = DEH_String(PD_ANY);; // Ty 03/27/98 - externalized
        S_StartSound(player->mo,sfx_oof);             // killough 3/20/98
        return false;
      }
      break;
    case RCard:
      if
      (
        !player->cards[it_redcard] &&
        (!skulliscard || !player->cards[it_redskull])
      )
      {
        player->message = DEH_String(PD_REDK);; // Ty 03/27/98 - externalized
        S_StartSound(player->mo,sfx_oof);             // killough 3/20/98
        return false;
      }
      break;
    case BCard:
      if
      (
        !player->cards[it_bluecard] &&
        (!skulliscard || !player->cards[it_blueskull])
      )
      {
        player->message = DEH_String(PD_BLUEK); // Ty 03/27/98 - externalized
        S_StartSound(player->mo,sfx_oof);             // killough 3/20/98
        return false;
      }
      break;
    case YCard:
      if
      (
        !player->cards[it_yellowcard] &&
        (!skulliscard || !player->cards[it_yellowskull])
      )
      {
        player->message = DEH_String(PD_YELLOWK); // Ty 03/27/98 - externalized
        S_StartSound(player->mo,sfx_oof);             // killough 3/20/98
        return false;
      }
      break;
    case RSkull:
      if
      (
        !player->cards[it_redskull] &&
        (!skulliscard || !player->cards[it_redcard])
      )
      {
        player->message = DEH_String(PD_REDK); // Ty 03/27/98 - externalized
        S_StartSound(player->mo,sfx_oof);             // killough 3/20/98
        return false;
      }
      break;
    case BSkull:
      if
      (
        !player->cards[it_blueskull] &&
        (!skulliscard || !player->cards[it_bluecard])
      )
      {
        player->message = DEH_String(PD_BLUEK); // Ty 03/27/98 - externalized
        S_StartSound(player->mo,sfx_oof);             // killough 3/20/98
        return false;
      }
      break;
    case YSkull:
      if
      (
        !player->cards[it_yellowskull] &&
        (!skulliscard || !player->cards[it_yellowcard])
      )
      {
        player->message = DEH_String(PD_YELLOWK); // Ty 03/27/98 - externalized
        S_StartSound(player->mo,sfx_oof);             // killough 3/20/98
        return false;
      }
      break;
    case AllKeys:
      if
      (
        !skulliscard &&
        (
          !player->cards[it_redcard] ||
          !player->cards[it_redskull] ||
          !player->cards[it_bluecard] ||
          !player->cards[it_blueskull] ||
          !player->cards[it_yellowcard] ||
          !player->cards[it_yellowskull]
        )
      )
      {
        player->message = DEH_String(PD_ALL6); // Ty 03/27/98 - externalized
        S_StartSound(player->mo,sfx_oof);             // killough 3/20/98
        return false;
      }
      if
      (
        skulliscard &&
        (
          (!player->cards[it_redcard] &&
            !player->cards[it_redskull]) ||
          (!player->cards[it_bluecard] &&
            !player->cards[it_blueskull]) ||
          (!player->cards[it_yellowcard] &&
            !player->cards[it_yellowskull])
        )
      )
      {
        player->message = DEH_String(PD_ALL3); // Ty 03/27/98 - externalized
        S_StartSound(player->mo,sfx_oof);             // killough 3/20/98
        return false;
      }
      break;
  }
  return true;
}

// killough 4/16/98: Same thing, only for linedefs

int P_FindLineFromLineTag(line_t *line, int start)
{
  start = start >= 0 ? lines[start].nexttag :
    lines[(unsigned) line->tag % (unsigned) numlines].firsttag;
  while (start >= 0 && lines[start].tag != line->tag)
    start = lines[start].nexttag;
  return start;
}
