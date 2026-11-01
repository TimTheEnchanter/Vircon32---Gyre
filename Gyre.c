// =============================================================================
//   GYRE - a tube shooter for Vircon32, built with vector.h
// =============================================================================
//   Your claw rides the rim of a wireframe tube. Enemies climb the lanes
//   toward you; shoot them down the tube before they reach the rim.
//
//   Controls:  D-pad   move along the rim (toward the pressed direction)
//              B / A   fire (hold for auto-fire)
//              X       superzapper (once per level, then a weak second use)
//              Start   start / pause
// =============================================================================

#define VEC_MAX_LINES 1100
#define GYRE_VERSION "V0.1"

#include "video.h"
#include "math.h"
#include "time.h"
#include "input.h"
#include "audio.h"
#include "misc.h"
#include "vector.h"
#include "music_data.h"
#include "chipmusic.h"

// -----------------------------------------------------------------------------
//   SOUNDS AND CHANNELS (same order as Gyre.xml)
// -----------------------------------------------------------------------------

#define SND_FIRE         0
#define SND_ENEMY_DIE    1
#define SND_TANKER_SPLIT 2
#define SND_MOVE         3
#define SND_ZAPPER       4
#define SND_WARP         5
#define SND_DEATH        6
#define SND_SPIKE_HIT    7
#define SND_ENEMY_FIRE   8
#define SND_LEVEL_START  9
#define SND_EXTRA_LIFE   10
#define SND_GRAB         11
#define SND_INSTRUMENTS  12       // 8 music instrument samples (chipmusic.h)

#define CH_MUSIC      0           // music uses channels 0-6
#define CH_FIRST_SFX  7

int next_sfx_channel = CH_FIRST_SFX;

void sfx_pitched( int sound_id, float volume, float speed )
{
    stop_channel( next_sfx_channel );          // a busy channel ignores new sounds
    select_channel( next_sfx_channel );
    set_channel_volume( volume );
    set_channel_speed( speed );
    play_sound_in_channel( sound_id, next_sfx_channel );

    next_sfx_channel++;
    if( next_sfx_channel >= sound_channels ) next_sfx_channel = CH_FIRST_SFX;
}

void sfx( int sound_id, float volume )
{
    sfx_pitched( sound_id, volume, 1.0 );
}

// -----------------------------------------------------------------------------
//   RANDOM NUMBERS (xorshift, so play is reproducible from a seed)
// -----------------------------------------------------------------------------

int rng_state = 1;

int xorshift( int x )
{
    x = x ^ ( x << 13 );
    x = x ^ ( ( x >> 17 ) & 0x7FFF );
    x = x ^ ( x << 5 );
    return x;
}

int grand()
{
    rng_state = xorshift( rng_state );
    return rng_state & 0x7FFFFFFF;
}

float frand()
{
    return ( grand() % 10000 ) / 10000.0;
}

float frand_range( float a, float b )
{
    return a + ( b - a ) * frand();
}

// -----------------------------------------------------------------------------
//   GAME STATE
// -----------------------------------------------------------------------------

#define STATE_TITLE     0
#define STATE_PLAYING   1
#define STATE_GAMEOVER  2

int state = STATE_TITLE;
int state_timer = 0;
int frame = 0;
bool paused = false;

int score = 0;
int high_score = 10000;
int lives;
int level;
int next_extra_life;

// play phases within a level
#define PHASE_FLYIN   0           // camera flies in toward the new web
#define PHASE_FIGHT   1
#define PHASE_WARP    2           // diving down the tube to the next web
#define PHASE_DEAD    3           // claw destroyed, waiting to respawn

int phase;
int phase_timer;

float shake = 0;
float flash = 0;
int flash_color = 0xFFFFFFFF;

void add_shake( float amount ) { if( amount > shake ) shake = amount; }

void add_flash( float amount, int color )
{
    if( amount > flash )
    {
        flash = amount;
        flash_color = color;
    }
}

// -----------------------------------------------------------------------------
//   WEBS (the tubes)
// -----------------------------------------------------------------------------
//   A web is a ring (or open strip) of rim points at z = 0, extruded to
//   z = DEPTH. Lane i lies between points i and i + 1.

#define MAX_POINTS  16
#define DEPTH       4.0
#define WEB_COUNT   6

float[ MAX_POINTS ] web_x;
float[ MAX_POINTS ] web_y;
int web_points;
bool web_closed;
int lanes;
int web_shape;

float[ MAX_POINTS ] lane_cx;          // lane centers on the rim
float[ MAX_POINTS ] lane_cy;

int* web_names = "";

void set_point( int i, float x, float y )
{
    web_x[ i ] = x;
    web_y[ i ] = y;
}

void build_web( int shape )
{
    int i, k;
    float a, r, t;

    web_shape = shape;
    web_points = 16;
    web_closed = true;

    if( shape == 0 )            // circle
    {
        for( i = 0; i < 16; i++ )
        {
            a = -1.5707963 + i * 6.2831853 / 16 + 3.14159 / 16;
            set_point( i, cos( a ), sin( a ) );
        }
    }
    else if( shape == 1 )       // square, 4 lanes per side
    {
        for( i = 0; i < 4; i++ )
        {
            t = -0.9 + i * 0.45;
            set_point( i,       t,     -0.9 );
            set_point( 4 + i,   0.9,   t );
            set_point( 8 + i,   -t,    0.9 );
            set_point( 12 + i,  -0.9,  -t );
        }
    }
    else if( shape == 2 )       // plus sign
    {
        set_point( 0, -0.3, -1.0 );  set_point( 1, 0.0, -1.0 );   set_point( 2, 0.3, -1.0 );
        set_point( 3, 0.3, -0.3 );   set_point( 4, 1.0, -0.3 );   set_point( 5, 1.0, 0.0 );
        set_point( 6, 1.0, 0.3 );    set_point( 7, 0.3, 0.3 );    set_point( 8, 0.3, 1.0 );
        set_point( 9, 0.0, 1.0 );    set_point( 10, -0.3, 1.0 );  set_point( 11, -0.3, 0.3 );
        set_point( 12, -1.0, 0.3 );  set_point( 13, -1.0, 0.0 );  set_point( 14, -1.0, -0.3 );
        set_point( 15, -0.3, -0.3 );
    }
    else if( shape == 3 )       // open V
    {
        web_closed = false;
        for( i = 0; i < 16; i++ )
        {
            t = i / 15.0;
            set_point( i, -1.35 + 2.7 * t, -0.75 + 1.65 * ( 1.0 - fabs( 2.0 * t - 1.0 ) ) );
        }
    }
    else if( shape == 4 )       // eight-pointed star
    {
        for( i = 0; i < 16; i++ )
        {
            a = -1.5707963 + i * 6.2831853 / 16;
            r = 1.0;
            if( i % 2 ) r = 0.6;
            set_point( i, cos( a ) * r, sin( a ) * r );
        }
    }
    else                        // flat strip, seen from above the far end
    {
        web_closed = false;
        for( i = 0; i < 16; i++ )
          set_point( i, -1.45 + 2.9 * i / 15.0, 0.75 );
    }

    lanes = web_points;
    if( !web_closed ) lanes = web_points - 1;

    for( i = 0; i < lanes; i++ )
    {
        k = ( i + 1 ) % web_points;
        lane_cx[ i ] = ( web_x[ i ] + web_x[ k ] ) * 0.5;
        lane_cy[ i ] = ( web_y[ i ] + web_y[ k ] ) * 0.5;
    }
}

int lane_next_point( int lane )
{
    return ( lane + 1 ) % web_points;
}

// wraps or clamps a lane index for the current web
int lane_wrap( int lane )
{
    if( web_closed )
    {
        while( lane < 0 ) lane += lanes;
        while( lane >= lanes ) lane -= lanes;
        return lane;
    }
    if( lane < 0 ) return -1;
    if( lane >= lanes ) return -1;
    return lane;
}

// signed lane steps from a to b (shortest way round on closed webs)
int lane_delta( int a, int b )
{
    int d = b - a;
    if( web_closed )
    {
        if( d > lanes / 2 ) d -= lanes;
        if( d < -lanes / 2 ) d += lanes;
    }
    return d;
}

// -----------------------------------------------------------------------------
//   CAMERA
// -----------------------------------------------------------------------------

#define CAM_Z_NORMAL  -1.25
#define CAM_Y         -0.12
#define FOCAL         182

float cam_z = CAM_Z_NORMAL;
float web_spin = 0;                   // title screen rotates the web

void set_camera()
{
    vec_camera( 320, 172, FOCAL, 0, CAM_Y, cam_z );
}

// 3D point on lane edge `side` (0 = first point, 1 = second) at depth z
float edge_x( int lane, int side )
{
    if( side ) return web_x[ lane_next_point( lane ) ];
    return web_x[ lane ];
}

float edge_y( int lane, int side )
{
    if( side ) return web_y[ lane_next_point( lane ) ];
    return web_y[ lane ];
}

// -----------------------------------------------------------------------------
//   PLAYER CLAW
// -----------------------------------------------------------------------------

int claw_lane;
int claw_from;                        // lane the claw is sliding from
float claw_slide = 1;                 // 0..1 progress of that slide
float claw_z;                         // 0 on the rim; grows while warping
int move_timer;
int fire_cooldown;
int zapper_uses;                      // 0 = full, 1 = weak use left, 2 = empty
int zap_flash_timer;
int respawn_timer;

// moves the claw to the neighbouring lane that lies most in the
// pressed screen direction (so Down always looks like down)
void move_claw( float dx, float dy )
{
    int i, cand, best = -1;
    float vx, vy, dot, best_dot = 0.25;

    for( i = -1; i <= 1; i += 2 )
    {
        cand = lane_wrap( claw_lane + i );
        if( cand < 0 ) continue;

        vx = lane_cx[ cand ] - lane_cx[ claw_lane ];
        vy = lane_cy[ cand ] - lane_cy[ claw_lane ];
        dot = ( vx * dx + vy * dy ) / sqrt( vx * vx + vy * vy + 0.0001 );
        if( dot > best_dot )
        {
            best_dot = dot;
            best = cand;
        }
    }

    if( best >= 0 )
    {
        claw_from = claw_lane;
        claw_slide = 0;
        claw_lane = best;
        sfx( SND_MOVE, 0.25 );
    }
}

// -----------------------------------------------------------------------------
//   SHOTS
// -----------------------------------------------------------------------------

#define MAX_SHOTS    8
#define SHOT_SPEED   0.16

int[ MAX_SHOTS ] shot_lane;
float[ MAX_SHOTS ] shot_z;
bool[ MAX_SHOTS ] shot_active;

#define MAX_ESHOTS   8

int[ MAX_ESHOTS ] eshot_lane;
float[ MAX_ESHOTS ] eshot_z;
bool[ MAX_ESHOTS ] eshot_active;

void fire_shot()
{
    int i;
    for( i = 0; i < MAX_SHOTS; i++ )
      if( !shot_active[ i ] )
      {
          shot_active[ i ] = true;
          shot_lane[ i ] = claw_lane;
          shot_z[ i ] = claw_z;
          sfx_pitched( SND_FIRE, 0.28, frand_range( 0.95, 1.05 ) );
          return;
      }
}

void enemy_fire( int lane, float z )
{
    int i;
    for( i = 0; i < MAX_ESHOTS; i++ )
      if( !eshot_active[ i ] )
      {
          eshot_active[ i ] = true;
          eshot_lane[ i ] = lane;
          eshot_z[ i ] = z;
          sfx( SND_ENEMY_FIRE, 0.35 );
          return;
      }
}

// -----------------------------------------------------------------------------
//   SPIKES (left in lanes by spikers; deadly when warping)
// -----------------------------------------------------------------------------

float[ MAX_POINTS ] spike_top;        // DEPTH = no spike; the spike runs from here to the far end

void clear_spikes()
{
    int i;
    for( i = 0; i < MAX_POINTS; i++ ) spike_top[ i ] = DEPTH;
}

// -----------------------------------------------------------------------------
//   PARTICLES (screen space)
// -----------------------------------------------------------------------------

#define MAX_PARTS 90

float[ MAX_PARTS ] part_x;
float[ MAX_PARTS ] part_y;
float[ MAX_PARTS ] part_vx;
float[ MAX_PARTS ] part_vy;
float[ MAX_PARTS ] part_angle;
float[ MAX_PARTS ] part_len;
int[ MAX_PARTS ] part_life;
int[ MAX_PARTS ] part_max;
int[ MAX_PARTS ] part_color;
int next_part = 0;

void burst( float x, float y, int count, float speed, float length, int color )
{
    int k, i;
    float a, v;

    for( k = 0; k < count; k++ )
    {
        i = next_part;
        next_part = ( next_part + 1 ) % MAX_PARTS;
        a = frand() * 6.2831853;
        v = speed * frand_range( 0.3, 1.0 );
        part_x[ i ] = x;
        part_y[ i ] = y;
        part_vx[ i ] = cos( a ) * v;
        part_vy[ i ] = sin( a ) * v;
        part_angle[ i ] = frand() * 6.2831853;
        part_len[ i ] = length;
        part_life[ i ] = 25 + grand() % 25;
        part_max[ i ] = part_life[ i ];
        part_color[ i ] = color;
    }
}

// explosion at a lane position
void burst_at( int lane, float z, int count, float speed, int color )
{
    float sx, sy;
    if( vec_project( lane_cx[ lane ], lane_cy[ lane ], z, &sx, &sy ) )
      burst( sx, sy, count, speed, 5, color );
}

// -----------------------------------------------------------------------------
//   ENEMIES
// -----------------------------------------------------------------------------

#define EN_FLIPPER   0
#define EN_TANKER    1
#define EN_SPIKER    2

#define ES_CLIMB     0
#define ES_RIM       1                // flipper on the rim, hunting the claw
#define ES_RETREAT   2                // spiker going back down

#define MAX_ENEMIES  12

bool[ MAX_ENEMIES ] en_active;
bool quiet_kills = false;             // superzapper: one sound for many kills
int[ MAX_ENEMIES ] en_type;
int[ MAX_ENEMIES ] en_state;
int[ MAX_ENEMIES ] en_lane;
float[ MAX_ENEMIES ] en_z;
float[ MAX_ENEMIES ] en_speed;
float[ MAX_ENEMIES ] en_target;       // spiker: how high it climbs
int[ MAX_ENEMIES ] en_timer;
int[ MAX_ENEMIES ] en_flip_to;        // -1 when not flipping
float[ MAX_ENEMIES ] en_flip_t;

int spawn_total;                      // enemies this level
int spawned;
int spawn_timer;

int count_enemies()
{
    int i, n = 0;
    for( i = 0; i < MAX_ENEMIES; i++ )
      if( en_active[ i ] ) n++;
    return n;
}

float base_speed()
{
    return 0.010 + level * 0.0012;
}

int spawn_enemy( int type, int lane, float z )
{
    int i;
    for( i = 0; i < MAX_ENEMIES; i++ )
      if( !en_active[ i ] )
      {
          en_active[ i ] = true;
          en_type[ i ] = type;
          en_state[ i ] = ES_CLIMB;
          en_lane[ i ] = lane;
          en_z[ i ] = z;
          en_flip_to[ i ] = -1;
          en_flip_t[ i ] = 0;
          en_timer[ i ] = 60 + grand() % 120;
          en_speed[ i ] = base_speed();
          if( type == EN_TANKER ) en_speed[ i ] *= 0.7;
          if( type == EN_SPIKER ) en_speed[ i ] *= 1.3;
          en_target[ i ] = frand_range( 0.9, 2.3 );
          return i;
      }
    return -1;
}

// picks what kind of enemy comes next, by level
int pick_enemy_type()
{
    int r = grand() % 100;
    if( level >= 3 && r < 20 ) return EN_SPIKER;
    if( level >= 2 && r < 42 ) return EN_TANKER;
    return EN_FLIPPER;
}

void split_tanker( int i )
{
    int a = lane_wrap( en_lane[ i ] - 1 );
    int b = lane_wrap( en_lane[ i ] + 1 );
    float z = fmax( en_z[ i ], 0.05 );
    int k;

    en_active[ i ] = false;
    if( a < 0 ) a = en_lane[ i ];
    if( b < 0 ) b = en_lane[ i ];

    k = spawn_enemy( EN_FLIPPER, a, z );
    if( k >= 0 && z <= 0.05 ) en_state[ k ] = ES_RIM;
    k = spawn_enemy( EN_FLIPPER, b, z );
    if( k >= 0 && z <= 0.05 ) en_state[ k ] = ES_RIM;

    burst_at( en_lane[ i ], z, 10, 2.0, 0xFFFF60D0 );
    sfx( SND_TANKER_SPLIT, 0.6 );
}

void add_score( int points );

void kill_enemy( int i, bool split )
{
    if( en_type[ i ] == EN_TANKER && split )
    {
        add_score( 100 );
        split_tanker( i );
        return;
    }

    if( en_type[ i ] == EN_FLIPPER ) add_score( 150 );
    else if( en_type[ i ] == EN_SPIKER ) add_score( 50 );
    else add_score( 100 );

    burst_at( en_lane[ i ], en_z[ i ], 14, 2.5, 0xFFFFFFFF );
    if( !quiet_kills ) sfx_pitched( SND_ENEMY_DIE, 0.45, frand_range( 0.9, 1.1 ) );
    en_active[ i ] = false;
}

// lane an enemy counts as being in (mid-flip it switches halfway)
int enemy_lane( int i )
{
    if( en_flip_to[ i ] >= 0 && en_flip_t[ i ] > 0.5 ) return en_flip_to[ i ];
    return en_lane[ i ];
}

void start_flip( int i, int dir )
{
    int to = lane_wrap( en_lane[ i ] + dir );
    if( to < 0 ) to = lane_wrap( en_lane[ i ] - dir );
    if( to < 0 ) return;
    en_flip_to[ i ] = to;
    en_flip_t[ i ] = 0;
}

// -----------------------------------------------------------------------------
//   SCORING AND DEATH
// -----------------------------------------------------------------------------

void add_score( int points )
{
    score += points;
    if( score > high_score ) high_score = score;
    if( score >= next_extra_life )
    {
        next_extra_life += 20000;
        lives++;
        sfx( SND_EXTRA_LIFE, 0.7 );
    }
}

void kill_claw()
{
    int i;
    float sx, sy;

    if( phase == PHASE_DEAD ) return;

    if( vec_project( lane_cx[ claw_lane ], lane_cy[ claw_lane ], claw_z, &sx, &sy ) )
    {
        burst( sx, sy, 30, 4.0, 8, 0xFF60FFFF );
        burst( sx, sy, 20, 2.0, 4, 0xFFFFFFFF );
    }

    sfx( SND_DEATH, 0.75 );
    add_shake( 10 );
    add_flash( 0.5, 0xFFC0E0FF );
    lives--;

    // everything still in the tube comes back later in the level
    for( i = 0; i < MAX_ENEMIES; i++ )
      if( en_active[ i ] )
      {
          en_active[ i ] = false;
          spawned--;
      }
    if( spawned < 0 ) spawned = 0;
    for( i = 0; i < MAX_SHOTS; i++ ) shot_active[ i ] = false;
    for( i = 0; i < MAX_ESHOTS; i++ ) eshot_active[ i ] = false;

    phase = PHASE_DEAD;
    phase_timer = 0;
}

// -----------------------------------------------------------------------------
//   LEVELS
// -----------------------------------------------------------------------------

int web_color;
int web_glow;

void set_level_colors()
{
    int group = ( ( level - 1 ) / WEB_COUNT ) % 4;
    if( group == 0 )      { web_color = 0xFFFFA040; web_glow = 0xFF803010; }    // blue
    else if( group == 1 ) { web_color = 0xFF4060FF; web_glow = 0xFF101880; }    // red
    else if( group == 2 ) { web_color = 0xFF60FF60; web_glow = 0xFF108010; }    // green
    else                  { web_color = 0xFF40E0FF; web_glow = 0xFF106080; }    // yellow
}

void start_level()
{
    int i;

    build_web( ( level - 1 ) % WEB_COUNT );
    set_level_colors();
    clear_spikes();
    for( i = 0; i < MAX_ENEMIES; i++ ) en_active[ i ] = false;
    for( i = 0; i < MAX_SHOTS; i++ ) shot_active[ i ] = false;
    for( i = 0; i < MAX_ESHOTS; i++ ) eshot_active[ i ] = false;

    claw_lane = lanes / 2;
    if( web_closed ) claw_lane = lanes / 2;
    claw_from = claw_lane;
    claw_slide = 1;
    claw_z = 0;
    zapper_uses = 0;

    spawn_total = min( 8 + level * 2, 32 );
    spawned = 0;
    spawn_timer = 90;

    phase = PHASE_FLYIN;
    phase_timer = 0;
    cam_z = -7;
    sfx( SND_LEVEL_START, 0.6 );
}

void new_game()
{
    score = 0;
    lives = 3;
    level = 1;
    next_extra_life = 20000;
    paused = false;
    fire_cooldown = 0;
    move_timer = 0;
    rng_state = rand() % 2000000000 + 1;
    start_level();
    state = STATE_PLAYING;
    state_timer = 0;
    music_play( MUSIC_GAME, 0.5 );
}

// -----------------------------------------------------------------------------
//   UPDATE
// -----------------------------------------------------------------------------

void use_zapper();

void update_claw()
{
    float dx = 0, dy = 0;
    int held;

    // direction pressed (d-pad), with key repeat
    if( gamepad_left() > 0 ) dx = -1;
    if( gamepad_right() > 0 ) dx = 1;
    if( gamepad_up() > 0 ) dy = -1;
    if( gamepad_down() > 0 ) dy = 1;

    held = max( max( gamepad_left(), gamepad_right() ), max( gamepad_up(), gamepad_down() ) );
    if( dx != 0 || dy != 0 )
    {
        if( held == 1 || ( held > 10 && held % 4 == 0 ) ) move_claw( dx, dy );
    }

    // the drawn claw slides quickly to its new lane
    if( claw_slide < 1 ) claw_slide = fmin( 1, claw_slide + 0.34 );

    // fire
    if( fire_cooldown > 0 ) fire_cooldown--;
    if( ( gamepad_button_b() > 0 || gamepad_button_a() > 0 ) && fire_cooldown == 0 )
    {
        fire_shot();
        fire_cooldown = 6;
    }

    // superzapper
    if( gamepad_button_x() == 1 && zapper_uses < 2 && phase == PHASE_FIGHT ) use_zapper();
}

void update_shots()
{
    int s, e, el;
    float z_old;

    for( s = 0; s < MAX_SHOTS; s++ )
      if( shot_active[ s ] )
      {
          z_old = shot_z[ s ];
          shot_z[ s ] += SHOT_SPEED;

          // spikes: each hit shortens the spike from the top
          if( spike_top[ shot_lane[ s ] ] < DEPTH && shot_z[ s ] >= spike_top[ shot_lane[ s ] ] )
          {
              spike_top[ shot_lane[ s ] ] += 0.35;
              if( spike_top[ shot_lane[ s ] ] > DEPTH - 0.1 ) spike_top[ shot_lane[ s ] ] = DEPTH;
              shot_active[ s ] = false;
              add_score( 3 );
              sfx( SND_SPIKE_HIT, 0.4 );
              burst_at( shot_lane[ s ], shot_z[ s ], 3, 1.0, 0xFF60FF60 );
              continue;
          }

          // enemies: anything the shot passed through this frame
          for( e = 0; e < MAX_ENEMIES; e++ )
            if( en_active[ e ] && enemy_lane( e ) == shot_lane[ s ]
                && en_z[ e ] >= z_old - 0.12 && en_z[ e ] <= shot_z[ s ] + 0.12 )
            {
                shot_active[ s ] = false;
                kill_enemy( e, true );
                break;
            }
          if( !shot_active[ s ] ) continue;

          // enemy shots can be shot down
          for( e = 0; e < MAX_ESHOTS; e++ )
            if( eshot_active[ e ] && eshot_lane[ e ] == shot_lane[ s ]
                && eshot_z[ e ] >= z_old - 0.1 && eshot_z[ e ] <= shot_z[ s ] + 0.1 )
            {
                eshot_active[ e ] = false;
                shot_active[ s ] = false;
                burst_at( shot_lane[ s ], shot_z[ s ], 4, 1.2, 0xFFFFFFFF );
                break;
            }

          if( shot_z[ s ] > DEPTH + 0.2 ) shot_active[ s ] = false;
      }

    for( e = 0; e < MAX_ESHOTS; e++ )
      if( eshot_active[ e ] )
      {
          eshot_z[ e ] -= 0.035 + level * 0.002;
          if( eshot_z[ e ] <= 0.05 )
          {
              eshot_active[ e ] = false;
              if( eshot_lane[ e ] == claw_lane && phase == PHASE_FIGHT ) kill_claw();
          }
      }
}

void update_enemies()
{
    int i, d;

    // spawning from the far end
    if( spawned < spawn_total )
    {
        spawn_timer--;
        if( spawn_timer <= 0 && count_enemies() < 8 )
        {
            if( spawn_enemy( pick_enemy_type(), grand() % lanes, DEPTH ) >= 0 ) spawned++;
            spawn_timer = max( 75 - level * 4, 25 ) + grand() % 30;
        }
    }

    for( i = 0; i < MAX_ENEMIES; i++ )
    {
        if( !en_active[ i ] ) continue;

        // flipping between lanes (animated)
        if( en_flip_to[ i ] >= 0 )
        {
            en_flip_t[ i ] += 0.09 + level * 0.004;
            if( en_flip_t[ i ] >= 1.0 )
            {
                en_lane[ i ] = en_flip_to[ i ];
                en_flip_to[ i ] = -1;
            }
        }

        if( en_state[ i ] == ES_CLIMB )
        {
            en_z[ i ] -= en_speed[ i ];

            if( en_type[ i ] == EN_SPIKER )
            {
                // leaves a spike behind as it climbs
                if( en_z[ i ] < spike_top[ en_lane[ i ] ] ) spike_top[ en_lane[ i ] ] = en_z[ i ];
                if( en_z[ i ] <= en_target[ i ] ) en_state[ i ] = ES_RETREAT;
            }
            else if( en_type[ i ] == EN_FLIPPER )
            {
                // flips lanes now and then, and fires from a distance
                en_timer[ i ]--;
                if( en_timer[ i ] <= 0 && en_flip_to[ i ] < 0 )
                {
                    start_flip( i, 1 - 2 * ( grand() % 2 ) );
                    en_timer[ i ] = 70 + grand() % 110;
                }
                if( en_z[ i ] > 1.2 && grand() % 1000 < 4 + level ) enemy_fire( en_lane[ i ], en_z[ i ] );
            }

            if( en_z[ i ] <= 0 )
            {
                en_z[ i ] = 0;
                if( en_type[ i ] == EN_TANKER ) split_tanker( i );
                else if( en_type[ i ] == EN_FLIPPER )
                {
                    en_state[ i ] = ES_RIM;
                    en_timer[ i ] = 20;
                }
            }
        }
        else if( en_state[ i ] == ES_RETREAT )
        {
            en_z[ i ] += en_speed[ i ];
            if( en_z[ i ] >= DEPTH ) en_active[ i ] = false;      // gone, counts as done
        }
        else if( en_state[ i ] == ES_RIM )
        {
            // flips along the rim toward the claw
            en_timer[ i ]--;
            if( en_timer[ i ] <= 0 && en_flip_to[ i ] < 0 )
            {
                d = lane_delta( en_lane[ i ], claw_lane );
                if( d != 0 )
                {
                    if( d > 0 ) start_flip( i, 1 );
                    else start_flip( i, -1 );
                }
                en_timer[ i ] = max( 26 - level, 12 );
            }
        }

        // touching the claw on the rim: grabbed
        if( phase == PHASE_FIGHT && en_active[ i ] && en_z[ i ] < 0.15 && enemy_lane( i ) == claw_lane )
        {
            sfx( SND_GRAB, 0.6 );
            kill_claw();
            return;
        }
    }
}

void use_zapper()
{
    int i, k, n = 0;

    if( zapper_uses == 0 )
    {
        // first use: everything in the tube
        quiet_kills = true;
        for( i = 0; i < MAX_ENEMIES; i++ )
          if( en_active[ i ] ) kill_enemy( i, false );
        quiet_kills = false;
        sfx( SND_ENEMY_DIE, 0.5 );
        for( i = 0; i < MAX_ESHOTS; i++ ) eshot_active[ i ] = false;
        zap_flash_timer = 40;
        add_flash( 0.6, 0xFFFFFFFF );
    }
    else
    {
        // second use: just one enemy
        k = grand() % MAX_ENEMIES;
        for( i = 0; i < MAX_ENEMIES; i++ )
          if( en_active[ ( k + i ) % MAX_ENEMIES ] )
          {
              kill_enemy( ( k + i ) % MAX_ENEMIES, false );
              break;
          }
        zap_flash_timer = 15;
    }

    zapper_uses++;
    sfx( SND_ZAPPER, 0.6 );
}

void update_particles()
{
    int i;
    for( i = 0; i < MAX_PARTS; i++ )
      if( part_life[ i ] > 0 )
      {
          part_life[ i ]--;
          part_x[ i ] += part_vx[ i ];
          part_y[ i ] += part_vy[ i ];
          part_vx[ i ] *= 0.96;
          part_vy[ i ] *= 0.96;
          part_angle[ i ] += 0.15;
      }
}

void update_playing()
{
    int i;

    if( gamepad_button_start() == 1 )
    {
        paused = !paused;
        if( paused ) pause_all_channels();
        else resume_all_channels();
    }
    if( paused ) return;

    phase_timer++;
    if( zap_flash_timer > 0 ) zap_flash_timer--;

    if( phase == PHASE_FLYIN )
    {
        // camera eases in from far behind the web
        cam_z += ( CAM_Z_NORMAL - cam_z ) * 0.08;
        if( phase_timer > 70 )
        {
            cam_z = CAM_Z_NORMAL;
            phase = PHASE_FIGHT;
            phase_timer = 0;
        }
        update_claw();
    }
    else if( phase == PHASE_FIGHT )
    {
        update_claw();
        update_shots();
        update_enemies();

        // level cleared: dive down the tube
        if( phase == PHASE_FIGHT && spawned >= spawn_total && count_enemies() == 0 )
        {
            for( i = 0; i < MAX_ESHOTS; i++ ) eshot_active[ i ] = false;
            phase = PHASE_WARP;
            phase_timer = 0;
            sfx( SND_WARP, 0.55 );
        }
    }
    else if( phase == PHASE_WARP )
    {
        update_claw();
        update_shots();

        // accelerate down the tube; the camera follows the claw
        claw_z += 0.004 + phase_timer * 0.0009;
        cam_z = CAM_Z_NORMAL + claw_z;

        // a spike in the claw's lane is fatal
        if( spike_top[ claw_lane ] < DEPTH && claw_z >= spike_top[ claw_lane ] - 0.05 )
        {
            kill_claw();
        }
        else if( claw_z > DEPTH + 0.5 )
        {
            level++;
            add_score( 0 );
            start_level();
        }
    }
    else if( phase == PHASE_DEAD )
    {
        update_shots();
        if( phase_timer > 120 )
        {
            if( lives <= 0 )
            {
                state = STATE_GAMEOVER;
                state_timer = 0;
                music_stop();
                return;
            }

            // back on the rim; a death during the warp restarts the dive
            claw_z = 0;
            cam_z = CAM_Z_NORMAL;
            if( spawned >= spawn_total ) phase = PHASE_WARP;
            else phase = PHASE_FIGHT;
            phase_timer = 0;
            if( phase == PHASE_WARP ) clear_spikes();
        }
    }
}

// -----------------------------------------------------------------------------
//   DRAWING
// -----------------------------------------------------------------------------

#define STARS 70
float[ STARS ] star_x;
float[ STARS ] star_y;
float[ STARS ] star_p;

void draw_stars()
{
    int i, g;
    float b;
    vec_thickness( 1 );
    for( i = 0; i < STARS; i++ )
    {
        b = 0.5 + 0.5 * sin( frame * 0.04 + star_p[ i ] );
        g = 40 + 50 * b;
        vec_color( 0xFF000000 | ( ( g + 30 ) << 16 ) | ( g << 8 ) | g, 0 );
        vec_line_polar( star_x[ i ], star_y[ i ], 1, 0 );
    }
}

// rotated web point (only the title screen spins the web)
float wx( int i )
{
    if( web_spin == 0 ) return web_x[ i ];
    return web_x[ i ] * cos( web_spin ) - web_y[ i ] * sin( web_spin );
}

float wy( int i )
{
    if( web_spin == 0 ) return web_y[ i ];
    return web_x[ i ] * sin( web_spin ) + web_y[ i ] * cos( web_spin );
}

void draw_web()
{
    int i, k, n;
    float z0;

    vec_thickness( 2 );
    vec_glow_size( 16 );
    vec_color( web_color, web_glow );
    vec_depth_fade( 1.2, DEPTH + 3.5, 0.18 );

    n = web_points;
    if( !web_closed ) n = web_points - 1;

    for( i = 0; i < n; i++ )
    {
        k = ( i + 1 ) % web_points;
        vec_line_3d( wx( i ), wy( i ), 0, wx( k ), wy( k ), 0 );            // rim
        vec_line_3d( wx( i ), wy( i ), DEPTH, wx( k ), wy( k ), DEPTH );    // far end
    }
    for( i = 0; i < web_points; i++ )
      vec_line_3d( wx( i ), wy( i ), 0, wx( i ), wy( i ), DEPTH );          // lane edges

    // the claw's lane glows brighter
    if( state == STATE_PLAYING && phase != PHASE_DEAD )
    {
        vec_thickness( 2.5 );
        vec_color( 0xFFFFFFFF, web_glow );
        k = lane_next_point( claw_lane );
        z0 = claw_z;
        vec_line_3d( web_x[ claw_lane ], web_y[ claw_lane ], z0, web_x[ claw_lane ], web_y[ claw_lane ], DEPTH );
        vec_line_3d( web_x[ k ], web_y[ k ], z0, web_x[ k ], web_y[ k ], DEPTH );
    }

    vec_depth_fade( 0, 0, 1 );
}

// a lane-spanning point: side 0..1 across the lane, at depth z,
// optionally blended toward another lane (flipping enemies)
float lx( int lane, int to, float t, float side )
{
    float a = edge_x( lane, 0 ) + ( edge_x( lane, 1 ) - edge_x( lane, 0 ) ) * side;
    float b;
    if( to < 0 ) return a;
    b = edge_x( to, 0 ) + ( edge_x( to, 1 ) - edge_x( to, 0 ) ) * side;
    return a + ( b - a ) * t;
}

float ly( int lane, int to, float t, float side )
{
    float a = edge_y( lane, 0 ) + ( edge_y( lane, 1 ) - edge_y( lane, 0 ) ) * side;
    float b;
    if( to < 0 ) return a;
    b = edge_y( to, 0 ) + ( edge_y( to, 1 ) - edge_y( to, 0 ) ) * side;
    return a + ( b - a ) * t;
}

void draw_claw()
{
    int lane = claw_lane;
    float z = claw_z, x0, y0, x1, y1, cx, cy, pulse;

    if( phase == PHASE_DEAD ) return;

    if( claw_slide < 1 )
    {
        x0 = lx( claw_from, lane, claw_slide, 0.02 );  y0 = ly( claw_from, lane, claw_slide, 0.02 );
        x1 = lx( claw_from, lane, claw_slide, 0.98 );  y1 = ly( claw_from, lane, claw_slide, 0.98 );
    }
    else
    {
        x0 = lx( lane, -1, 0, 0.02 );  y0 = ly( lane, -1, 0, 0.02 );
        x1 = lx( lane, -1, 0, 0.98 );  y1 = ly( lane, -1, 0, 0.98 );
    }
    cx = ( x0 + x1 ) * 0.5;        cy = ( y0 + y1 ) * 0.5;
    pulse = 0.02 * sin( frame * 0.3 );

    vec_thickness( 2.5 );
    vec_glow_size( 20 );
    vec_color( 0xFF40FFFF, 0xFF108080 );

    // two prongs reaching toward the viewer, joined at the back
    vec_line_3d( x0, y0, z, x0 + ( cx - x0 ) * 0.45, y0 + ( cy - y0 ) * 0.45, z - 0.12 - pulse );
    vec_line_3d( x0 + ( cx - x0 ) * 0.45, y0 + ( cy - y0 ) * 0.45, z - 0.12 - pulse, cx, cy, z - 0.03 );
    vec_line_3d( cx, cy, z - 0.03, x1 + ( cx - x1 ) * 0.45, y1 + ( cy - y1 ) * 0.45, z - 0.12 - pulse );
    vec_line_3d( x1 + ( cx - x1 ) * 0.45, y1 + ( cy - y1 ) * 0.45, z - 0.12 - pulse, x1, y1, z );
    vec_line_3d( x0, y0, z, cx, cy, z + 0.1 );
    vec_line_3d( cx, cy, z + 0.1, x1, y1, z );
}

void draw_enemies()
{
    int i, to;
    float z, t, a0x, a0y, a1x, a1y, cx, cy, s, r, ang, px, py, qx, qy;

    for( i = 0; i < MAX_ENEMIES; i++ )
    {
        if( !en_active[ i ] ) continue;
        z = en_z[ i ];
        to = en_flip_to[ i ];
        t = en_flip_t[ i ];

        a0x = lx( en_lane[ i ], to, t, 0.1 );  a0y = ly( en_lane[ i ], to, t, 0.1 );
        a1x = lx( en_lane[ i ], to, t, 0.9 );  a1y = ly( en_lane[ i ], to, t, 0.9 );
        cx = ( a0x + a1x ) * 0.5;              cy = ( a0y + a1y ) * 0.5;

        // a flipping enemy arcs up out of the web
        if( to >= 0 ) z -= sin( t * 3.14159 ) * 0.08;

        if( en_type[ i ] == EN_FLIPPER )
        {
            // bow tie across the lane
            vec_thickness( 2 );
            vec_glow_size( 14 );
            vec_color( 0xFF5050FF, 0xFF181880 );
            vec_line_3d( a0x, a0y, z - 0.12, a1x, a1y, z + 0.12 );
            vec_line_3d( a0x, a0y, z + 0.12, a1x, a1y, z - 0.12 );
            vec_line_3d( a0x, a0y, z - 0.12, a0x, a0y, z + 0.12 );
            vec_line_3d( a1x, a1y, z - 0.12, a1x, a1y, z + 0.12 );
        }
        else if( en_type[ i ] == EN_TANKER )
        {
            // diamond inside a diamond
            vec_thickness( 2 );
            vec_glow_size( 14 );
            vec_color( 0xFFFF60D0, 0xFF701860 );
            vec_line_3d( a0x, a0y, z, cx, cy, z - 0.22 );
            vec_line_3d( cx, cy, z - 0.22, a1x, a1y, z );
            vec_line_3d( a1x, a1y, z, cx, cy, z + 0.22 );
            vec_line_3d( cx, cy, z + 0.22, a0x, a0y, z );
            vec_line_3d( ( a0x + cx ) * 0.5, ( a0y + cy ) * 0.5, z, cx, cy, z - 0.1 );
            vec_line_3d( cx, cy, z - 0.1, ( a1x + cx ) * 0.5, ( a1y + cy ) * 0.5, z );
            vec_line_3d( ( a1x + cx ) * 0.5, ( a1y + cy ) * 0.5, z, cx, cy, z + 0.1 );
            vec_line_3d( cx, cy, z + 0.1, ( a0x + cx ) * 0.5, ( a0y + cy ) * 0.5, z );
        }
        else
        {
            // spiker: a spinning spiral drawn on screen at its position
            if( vec_project( cx, cy, z, &px, &py ) )
            {
                s = FOCAL / ( z - cam_z ) * 0.16;
                vec_thickness( 2 );
                vec_glow_size( 12 );
                vec_color( 0xFF60FF60, 0xFF186018 );
                ang = frame * 0.25 + i;
                qx = px;
                qy = py;
                for( r = 0.15; r <= 1.0; r += 0.12 )
                {
                    ang += 0.9;
                    vec_line( qx, qy, px + cos( ang ) * s * r, py + sin( ang ) * s * r );
                    qx = px + cos( ang ) * s * r;
                    qy = py + sin( ang ) * s * r;
                }
            }
        }
    }
}

void draw_spikes_and_shots()
{
    int i;
    float px, py;

    // spikes: green lines from their top to the far end
    vec_thickness( 2 );
    vec_glow_size( 12 );
    vec_color( 0xFF60FF60, 0xFF186018 );
    for( i = 0; i < lanes; i++ )
      if( spike_top[ i ] < DEPTH )
      {
          vec_line_3d( lane_cx[ i ], lane_cy[ i ], spike_top[ i ], lane_cx[ i ], lane_cy[ i ], DEPTH );
          if( vec_project( lane_cx[ i ], lane_cy[ i ], spike_top[ i ], &px, &py ) )
          {
              vec_line( px - 2, py - 2, px + 2, py + 2 );
              vec_line( px - 2, py + 2, px + 2, py - 2 );
          }
      }

    // player shots: short bright streaks down the lane
    vec_thickness( 3 );
    vec_glow_size( 14 );
    vec_color( 0xFFC0FFFF, 0xFF208080 );
    for( i = 0; i < MAX_SHOTS; i++ )
      if( shot_active[ i ] )
        vec_line_3d( lane_cx[ shot_lane[ i ] ], lane_cy[ shot_lane[ i ] ], shot_z[ i ],
                     lane_cx[ shot_lane[ i ] ], lane_cy[ shot_lane[ i ] ], shot_z[ i ] + 0.18 );

    // enemy shots: small spinning crosses
    vec_thickness( 2 );
    vec_color( 0xFF80C0FF, 0xFF204080 );
    for( i = 0; i < MAX_ESHOTS; i++ )
      if( eshot_active[ i ] && vec_project( lane_cx[ eshot_lane[ i ] ], lane_cy[ eshot_lane[ i ] ], eshot_z[ i ], &px, &py ) )
      {
          vec_line( px + cos( frame * 0.3 ) * 4, py + sin( frame * 0.3 ) * 4, px - cos( frame * 0.3 ) * 4, py - sin( frame * 0.3 ) * 4 );
          vec_line( px + sin( frame * 0.3 ) * 4, py - cos( frame * 0.3 ) * 4, px - sin( frame * 0.3 ) * 4, py + cos( frame * 0.3 ) * 4 );
      }
}

void draw_particles()
{
    int i;
    float f, c, s;
    vec_thickness( 1.5 );
    vec_glow_size( 10 );
    for( i = 0; i < MAX_PARTS; i++ )
      if( part_life[ i ] > 0 )
      {
          f = part_life[ i ] / ( part_max[ i ] * 1.0 );
          vec_color( vec_scale_alpha( part_color[ i ], f ), vec_scale_rgb( part_color[ i ], 0.4 * f ) );
          c = cos( part_angle[ i ] ) * part_len[ i ] * 0.5;
          s = sin( part_angle[ i ] ) * part_len[ i ] * 0.5;
          vec_line( part_x[ i ] - c, part_y[ i ] - s, part_x[ i ] + c, part_y[ i ] + s );
      }
}

float number_width( int value, float size )
{
    int digits = 1;
    while( value >= 10 )
    {
        value /= 10;
        digits++;
    }
    return ( digits * 1.6 - 0.6 ) * size;
}

void draw_hud()
{
    int i;
    float x;

    vec_thickness( 2 );
    vec_glow_size( 12 );
    vec_color( 0xFFFFFFFF, 0xFF705050 );
    vec_number( score, 16, 10, 8 );

    // lives as small claw marks
    vec_color( 0xFF40FFFF, 0xFF108080 );
    for( i = 0; i < lives && i < 8; i++ )
    {
        x = 20 + i * 18;
        vec_line( x, 44, x + 6, 36 );
        vec_line( x + 6, 36, x + 12, 44 );
        vec_line( x + 3, 44, x + 6, 40 );
        vec_line( x + 6, 40, x + 9, 44 );
    }

    vec_thickness( 1.5 );
    vec_color( 0xFFC09696, 0 );
    vec_text( "HI", 282, 12, 5 );
    vec_number( high_score, 304, 12, 5 );
    vec_text( "LEVEL", 540, 12, 5 );
    vec_number( level, 540 + vec_text_width( "LEVEL ", 5 ), 12, 5 );

    // superzapper charge
    if( zapper_uses == 0 ) vec_color( 0xFF60FFFF, 0xFF205050 );
    else if( zapper_uses == 1 ) vec_color( 0xFF40A0C0, 0 );
    else vec_color( 0xFF504040, 0 );
    vec_text( "ZAP", 584, 32, 4 );

    if( phase == PHASE_FLYIN && phase_timer < 70 )
    {
        vec_thickness( 2.5 );
        vec_glow_size( 18 );
        vec_intensity( fmin( 1.0, fmin( phase_timer / 10.0, ( 70 - phase_timer ) / 15.0 ) ) );
        vec_color( 0xFFFFE0A0, 0xFF805020 );
        x = 320 - ( vec_text_width( "LEVEL ", 12 ) + number_width( level, 12 ) ) / 2;
        vec_text( "LEVEL", x, 150, 12 );
        vec_number( level, x + vec_text_width( "LEVEL ", 12 ), 150, 12 );
        vec_intensity( 1 );
    }

    if( phase == PHASE_WARP && phase_timer < 50 )
    {
        vec_thickness( 2 );
        vec_glow_size( 14 );
        vec_color( 0xFFFFE0A0, 0xFF805020 );
        vec_text_centered( "AVOID THE SPIKES", 320, 290, 7 );
    }

    if( paused )
    {
        vec_thickness( 3 );
        vec_glow_size( 20 );
        vec_color( 0xFFFFFFFF, 0xFF705050 );
        vec_text_centered( "PAUSED", 320, 150, 16 );
    }
}

void draw_title()
{
    int letter, g, r;
    float x, y, b, w;
    int[ 2 ] one;
    int* title = "GYRE";

    w = vec_text_width( title, 34 );
    x = 320 - w / 2;
    one[ 1 ] = 0;

    vec_thickness( 4 );
    vec_glow_size( 28 );
    for( letter = 0; title[ letter ]; letter++ )
    {
        y = 60 + 5 * sin( frame * 0.06 + letter * 0.9 );
        b = 0.5 + 0.5 * sin( frame * 0.05 + letter * 1.1 );
        g = 140 + 100 * b;
        r = 60 + 60 * b;
        vec_color( 0xFF000000 | ( 255 << 16 ) | ( g << 8 ) | r, 0xFF782814 );
        one[ 0 ] = title[ letter ];
        vec_text( one, x + letter * 1.6 * 34, y, 34 );
    }

    if( ( frame / 30 ) % 2 == 0 )
    {
        vec_thickness( 2 );
        vec_glow_size( 14 );
        vec_color( 0xFFFFFFFF, 0xFF705050 );
        vec_text_centered( "PRESS START", 320, 196, 10 );
    }

    vec_thickness( 1.5 );
    vec_glow_size( 10 );
    vec_color( 0xFFC09696, 0 );
    vec_text_centered( "D-PAD MOVE   B FIRE   X SUPERZAPPER", 320, 262, 5 );
    vec_text_centered( "SHOOT THEM BEFORE THEY REACH THE RIM", 320, 284, 5 );

    vec_color( 0xFFFFE0A0, 0xFF403010 );
    vec_text_centered( "HIGH SCORE", 300, 320, 6 );
    vec_number( high_score, 320 + vec_text_width( "HIGH SCORE", 6 ) / 2 - 4, 320, 6 );

    vec_thickness( 1 );
    vec_color( 0xFF806868, 0 );
    vec_text( GYRE_VERSION, 596, 344, 3.5 );
}

void draw_game_over()
{
    vec_thickness( 3 );
    vec_glow_size( 22 );
    vec_color( 0xFF8080FF, 0xFF202070 );
    vec_text_centered( "GAME OVER", 320, 130, 18 );
    if( state_timer > 90 && ( frame / 30 ) % 2 == 0 )
    {
        vec_thickness( 2 );
        vec_glow_size( 14 );
        vec_color( 0xFFFFFFFF, 0xFF705050 );
        vec_text_centered( "PRESS START", 320, 220, 8 );
    }
}

// -----------------------------------------------------------------------------
//   MAIN
// -----------------------------------------------------------------------------

void enter_title()
{
    level = 1;
    build_web( 0 );
    set_level_colors();
    clear_spikes();
    cam_z = -1.6;
    state = STATE_TITLE;
    state_timer = 0;
    music_play( MUSIC_GAME, 0.3 );
}

void main()
{
    int i;

    srand( get_time() + 1 );
    rng_state = 20261031;
    vec_init( 0, 0 );
    set_global_volume( 0.6 );     // headroom: the sound chip does not clamp its mix
    select_gamepad( 0 );
    music_init( SND_INSTRUMENTS, CH_MUSIC );

    for( i = 0; i < STARS; i++ )
    {
        star_x[ i ] = frand() * screen_width;
        star_y[ i ] = frand() * screen_height;
        star_p[ i ] = frand() * 6.2831853;
    }

    enter_title();

    while( true )
    {
        // --- update ---
        if( state == STATE_TITLE )
        {
            web_spin = frame * 0.004;
            if( gamepad_button_start() == 1 && state_timer > 20 )
            {
                web_spin = 0;
                new_game();
            }
        }
        else if( state == STATE_PLAYING ) update_playing();
        else
        {
            if( state_timer > 90 && gamepad_button_start() == 1 ) enter_title();
            else if( state_timer > 600 ) enter_title();
        }

        if( !paused )
        {
            music_update();
            update_particles();
            shake *= 0.86;
            flash *= 0.85;
        }
        state_timer++;

        // --- draw ---
        clear_screen( make_color_rgb( 2, 2, 8 ) );

        vec_offset( frand_range( -shake, shake ), frand_range( -shake, shake ) );
        set_camera();
        if( paused ) vec_intensity( 0.35 );
        if( state == STATE_TITLE ) vec_intensity( 0.55 );

        draw_stars();
        draw_web();
        if( state == STATE_PLAYING )
        {
            draw_spikes_and_shots();
            draw_enemies();
            draw_claw();
        }
        draw_particles();
        vec_intensity( 1 );
        vec_offset( 0, 0 );

        if( state == STATE_TITLE ) draw_title();
        else
        {
            draw_hud();
            if( state == STATE_GAMEOVER ) draw_game_over();
        }

        vec_flush();

        // superzapper: the whole screen pulses
        if( zap_flash_timer > 0 && ( zap_flash_timer / 3 ) % 2 == 0 )
          vec_fill_rect( 0, 0, screen_width, screen_height, 0x30FFFFFF, blending_add );
        if( flash > 0.01 )
          vec_fill_rect( 0, 0, screen_width, screen_height, vec_scale_alpha( flash_color, flash * 0.6 ), blending_add );

        frame++;
        end_frame();
    }
}
