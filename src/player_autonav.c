/*
 * See Licensing and Copyright notice in naev.h
 */

/**
 * @file player_autonav.c
 *
 * @brief Contains all the player autonav related stuff.
 */


#include "player.h"

#include "naev.h"

#include "toolkit.h"
#include "pause.h"
#include "space.h"


extern double player_acc; /**< Player acceleration. */


/*
 * Prototypes.
 */
static void player_autonav (void);
static int player_autonavApproach( Vector2d *pos );
static int player_autonavBrake (void);


/**
 * @brief Starts autonav.
 */
void player_autonavStart (void)
{
   /* Not under manual control. */
   if (pilot_isFlag( player.p, PILOT_MANUAL_CONTROL ))
      return;

   if (player.p->nav_hyperspace == -1)
      return;

   if (player.p->fuel < HYPERSPACE_FUEL) {
      player_message("\erNot enough fuel to jump for autonav.");
      return;
   }

   player_message("\epAutonav initialized.");
   player_setFlag(PLAYER_AUTONAV);
   player.autonav = AUTONAV_JUMP_APPROACH;
}


/**
 * @brief Starts autonav and closes the window.
 */
void player_autonavStartWindow( unsigned int wid, char *str)
{
   (void) str;

   if (player.p->nav_hyperspace == -1)
      return;

   if (player.p->fuel < HYPERSPACE_FUEL) {
      player_message("\erNot enough fuel to jump for autonav.");
      return;
   }

   player_message("\epAutonav initialized.");
   player_setFlag(PLAYER_AUTONAV);

   window_destroy( wid );
}


/**
 * @brief Starts autonav with a local position destination.
 */
void player_autonavPos( double x, double y )
{
   player.autonav    = AUTONAV_POS_APPROACH;
   vect_cset( &player.autonav_pos, x, y );
   player_message("\epAutonav initialized.");
   player_setFlag(PLAYER_AUTONAV);
}


/**
 * @brief Aborts autonav.
 */
void player_autonavAbort( char *reason )
{
   /* No point if player is beyond aborting. */
   if ((player.p==NULL) || ((player.p != NULL) && pilot_isFlag(player.p, PILOT_HYPERSPACE)))
      return;

   if (player_isFlag(PLAYER_AUTONAV)) {
      if (reason != NULL)
         player_message("\erAutonav aborted: %s!", reason);
      else
         player_message("\erAutonav aborted!");
      player_rmFlag(PLAYER_AUTONAV);

      /* Get rid of acceleration. */
      player_accelOver();

      /* Drop out of possible different speed modes. */
      if (dt_mod != 1.)
         pause_compressEnd();

      /* Break possible hyperspacing. */
      if (pilot_isFlag(player.p, PILOT_HYP_PREP)) {
         pilot_hyperspaceAbort(player.p);
         player_message("\epAborting hyperspace sequence.");
      }
   }
}


/**
 * @brief Handles the autonavigation process for the player.
 */
static void player_autonav (void)
{
   JumpPoint *jp;
   int ret;

   switch (player.autonav) {
      case AUTONAV_JUMP_APPROACH:
         /* Target jump. */
         jp    = &cur_system->jumps[ player.p->nav_hyperspace ];
         ret   = player_autonavApproach( &jp->pos );
         if (ret)
            player.autonav = AUTONAV_JUMP_BRAKE;
         break;

      case AUTONAV_JUMP_BRAKE:
         /* Target jump. */
         jp    = &cur_system->jumps[ player.p->nav_hyperspace ];
         ret   = player_autonavBrake();
         /* Try to jump or see if braked. */
         if (space_canHyperspace(player.p)) {
            player.autonav = AUTONAV_JUMP_APPROACH;
            player_accelOver();
            player_jump();
         }
         else if (ret)
            player.autonav = AUTONAV_JUMP_APPROACH;
         break;
   
      case AUTONAV_POS_APPROACH:
         ret = player_autonavApproach( &player.autonav_pos );
         if (ret) {
            player_rmFlag( PLAYER_AUTONAV );
            player_message( "\epAutonav arrived at position." );
         }
         break;
   }
}


/**
 * @brief Handles approaching a position with autonav.
 *
 *    @param pos Position to go to.
 *    @return 1 on completion.
 */
static int player_autonavApproach( Vector2d *pos )
{
   double d, time, vel, dist;

   /* Only accelerate if facing move dir. */
   d = pilot_face( player.p, vect_angle( &player.p->solid->pos, pos ) );
   if (FABS(d) < MIN_DIR_ERR) {
      if (player_acc < 1.)
         player_accel( 1. );
   }
   else if (player_acc > 0.)
      player_accelOver();

   /* Get current time to reach target. */
   time  = MIN( 1.5*player.p->speed, VMOD(player.p->solid->vel) ) /
      (player.p->thrust / player.p->solid->mass);

   /* Get velocity. */
   vel   = MIN( player.p->speed, VMOD(player.p->solid->vel) );

   /* Get distance. */
   dist  = vel*(time+1.1*180./player.p->turn) -
      0.5*(player.p->thrust/player.p->solid->mass)*time*time;

   /* See if should start braking. */
   if (dist*dist > vect_dist2( pos, &player.p->solid->pos )) {
      player_accelOver();
      return 1;
   }
   return 0;
}


/**
 * @brief Handles the autonav braking.
 *
 *    @return 1 on completion.
 */
static int player_autonavBrake (void)
{
   double d;

   /* Braking procedure. */
   d = pilot_face( player.p, VANGLE(player.p->solid->vel) + M_PI );
   if (FABS(d) < MIN_DIR_ERR) {
      if (player_acc < 1.)
         player_accel( 1. );
   }
   else if (player_acc > 0.)
      player_accelOver();

   if (VMOD(player.p->solid->vel) < MIN_VEL_ERR) {
      player_accelOver();
      return 1;
   }
   return 0;
}


/**
 * @brief Handles autonav thinking.
 *
 *    @param pplayer Player doing the thinking.
 */
void player_thinkAutonav( Pilot *pplayer )
{
   /* Abort if lockons detected. */
   if (pplayer->lockons > 0)
      player_autonavAbort("Missile Lockon Detected");
   else if ((player.autonav == AUTONAV_JUMP_APPROACH) ||
         (player.autonav == AUTONAV_JUMP_BRAKE)) {
      /* If we're already at the target. */
      if (player.p->nav_hyperspace == -1)
         player_autonavAbort("Target changed to current system");

      /* Need fuel. */
      else if (pplayer->fuel < HYPERSPACE_FUEL)
         player_autonavAbort("Not enough fuel for autonav to continue");

      else
         player_autonav();
   }

   /* Keep on moving. */
   else
      player_autonav();
}


