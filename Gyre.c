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
#define GYRE_VERSION "V0.3"

#include "video.h"
#include "math.h"
#include "time.h"
#include "input.h"
#include "audio.h"
#include "misc.h"
#include "memcard.h"
#define VEC_MAX_LINES 1200     // menus draw their text over the web
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
#define SND_PULSE        12
#define SND_INSTRUMENTS  13       // 8 music instrument samples (chipmusic.h)

#define CH_MUSIC      0           // music uses channels 0-6
#define CH_FIRST_SFX  7

int next_sfx_channel = CH_FIRST_SFX;
bool sfx_muted = false;               // attract demo: effects off, music stays

// player options (saved on the memory card)
#define DIFF_NORMAL  0
#define DIFF_EASY    1
#define DIFF_HARD    2
int opt_music = 10;                   // 0-10
int opt_sfx = 10;                     // 0-10
int opt_difficulty = DIFF_NORMAL;
bool opt_shake = true;
bool opt_flash = true;

float music_base = 0.5;               // volume the current track asked for

float sfx_level()
{
    return opt_sfx / 10.0;
}

void play_track( int track, float volume )
{
    music_base = volume;
    music_play( track, volume * opt_music / 10.0 );
}

void apply_music_volume()
{
    music_set_volume( music_base * opt_music / 10.0 );
}

// stops every effect channel, leaving the music alone
void stop_sfx_channels()
{
    int ch;
    for( ch = CH_FIRST_SFX; ch < sound_channels; ch++ ) stop_channel( ch );
}

void sfx_pitched( int sound_id, float volume, float speed )
{
    if( sfx_muted ) return;
    stop_channel( next_sfx_channel );          // a busy channel ignores new sounds
    select_channel( next_sfx_channel );
    set_channel_volume( volume * sfx_level() );
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

// effects-only generator (screen shake): drawing also runs while paused,
// so it must never touch the gameplay generator
int fx_state = 1;

float fx_range( float a, float b )
{
    fx_state = xorshift( fx_state );
    return a + ( b - a ) * ( ( fx_state & 0x7FFFFFFF ) % 10000 ) / 10000.0;
}

float frand_range( float a, float b )
{
    return a + ( b - a ) * frand();
}

// -----------------------------------------------------------------------------
//   INPUT (live or replayed) AND RECORDING
// -----------------------------------------------------------------------------
//   Game logic never reads the gamepad directly: each logic frame gets a
//   6-bit input word, from the gamepad (and recorded) or from a recording.
//   With the same seed and inputs a game replays exactly.

#define IN_LEFT    1
#define IN_RIGHT   2
#define IN_UP      4
#define IN_DOWN    8
#define IN_FIRE    16
#define IN_ZAP     32

#define REC_MAX_FRAMES  3600      // 60 s of gameplay
#define REC_WORDS       720       // 5 frames of 6 bits per word
#define DEMO_MAX_PLAY   2700      // attract demo: 45 s at most
#define DEMO_MIN_FRAMES 600       // shorter games never become the demo

int in_bits = 0;
int[ 6 ] in_held;                 // frames each input has been held (1 = just pressed)
bool input_lock = false;          // after the pause menu: ignore B/A until released
int game_frame = 0;
int game_seed = 1;

int[ REC_WORDS ] rec_data;
int rec_frames = 0;

bool demo_mode = false;
int demo_pos = 0;
int demo_seed = 0;
int demo_frames = 0;
int demo_score = 0;
int demo_difficulty = 0;
int[ REC_WORDS ] demo_data;

int read_gamepad_bits()
{
    int bits = 0;
    bool fire = gamepad_button_b() > 0 || gamepad_button_a() > 0;

    if( input_lock && !fire ) input_lock = false;
    if( input_lock ) fire = false;

    if( gamepad_left() > 0 )  bits = bits | IN_LEFT;
    if( gamepad_right() > 0 ) bits = bits | IN_RIGHT;
    if( gamepad_up() > 0 )    bits = bits | IN_UP;
    if( gamepad_down() > 0 )  bits = bits | IN_DOWN;
    if( fire )                bits = bits | IN_FIRE;
    if( gamepad_button_x() > 0 ) bits = bits | IN_ZAP;
    return bits;
}

void rec_add( int bits )
{
    if( rec_frames >= REC_MAX_FRAMES ) return;
    rec_data[ rec_frames / 5 ] = rec_data[ rec_frames / 5 ] | ( bits << ( ( rec_frames % 5 ) * 6 ) );
    rec_frames++;
}

int rec_get( int* data, int position )
{
    return ( data[ position / 5 ] >> ( ( position % 5 ) * 6 ) ) & 63;
}

void apply_input( int bits )
{
    int i;
    in_bits = bits;
    for( i = 0; i < 6; i++ )
    {
        if( bits & ( 1 << i ) ) in_held[ i ]++;
        else in_held[ i ] = 0;
    }
}

// -----------------------------------------------------------------------------
//   GAME STATE
// -----------------------------------------------------------------------------

#define STATE_TITLE     0
#define STATE_PLAYING   1
#define STATE_GAMEOVER  2
#define STATE_DEMO      3     // attract mode: replaying a recorded game
#define STATE_ENTRY     4     // entering initials
#define STATE_OPTIONS   5

int state = STATE_TITLE;
int state_timer = 0;
int frame = 0;
int anim = 0;                 // animation clock: game-relative in play, so replays match
bool paused = false;

int score = 0;
int lives;
int level;
int next_extra_life;
int save_message = 0;

// difficulty of the game being played (a demo uses the one it was recorded on)
int game_difficulty = DIFF_NORMAL;

int diff_pick( int easy, int normal, int hard )
{
    if( game_difficulty == DIFF_EASY ) return easy;
    if( game_difficulty == DIFF_HARD ) return hard;
    return normal;
}

float diff_pickf( float easy, float normal, float hard )
{
    if( game_difficulty == DIFF_EASY ) return easy;
    if( game_difficulty == DIFF_HARD ) return hard;
    return normal;
}

bool options_from_pause = false;

bool in_game()
{
    return state == STATE_PLAYING || state == STATE_DEMO
           || ( state == STATE_OPTIONS && options_from_pause );
}

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

void add_shake( float amount )
{
    if( opt_shake && amount > shake ) shake = amount;
}

void add_flash( float amount, int color )
{
    if( !opt_flash ) return;
    if( amount > flash )
    {
        flash = amount;
        flash_color = color;
    }
}

// -----------------------------------------------------------------------------
//   HIGH SCORE TABLE
// -----------------------------------------------------------------------------

#define HS_COUNT 5

int[ HS_COUNT ] hs_score;
int[ HS_COUNT ] hs_name;              // 3 characters, 8 bits each; difficulty in the top byte

int pack_name( int* text )
{
    return text[ 0 ] | ( text[ 1 ] << 8 ) | ( text[ 2 ] << 16 );
}

void unpack_name( int name, int* text )
{
    text[ 0 ] = name & 255;
    text[ 1 ] = ( name >> 8 ) & 255;
    text[ 2 ] = ( name >> 16 ) & 255;
    text[ 3 ] = 0;
}

void init_table()
{
    hs_score[ 0 ] = 10000;  hs_name[ 0 ] = pack_name( "TUB" );
    hs_score[ 1 ] = 8000;   hs_name[ 1 ] = pack_name( "WEB" );
    hs_score[ 2 ] = 6000;   hs_name[ 2 ] = pack_name( "ZAP" );
    hs_score[ 3 ] = 4000;   hs_name[ 3 ] = pack_name( "RIM" );
    hs_score[ 4 ] = 2000;   hs_name[ 4 ] = pack_name( "SPK" );
}

int table_rank( int value )
{
    int i;
    for( i = 0; i < HS_COUNT; i++ )
      if( value > hs_score[ i ] ) return i;
    return -1;
}

void table_insert( int value, int name, int rank )
{
    int i;
    if( rank < 0 || rank >= HS_COUNT ) return;
    for( i = HS_COUNT - 1; i > rank; i-- )
    {
        hs_score[ i ] = hs_score[ i - 1 ];
        hs_name[ i ] = hs_name[ i - 1 ];
    }
    hs_score[ rank ] = value;
    hs_name[ rank ] = name;
}

void table_merge( int value, int name )
{
    int i;
    for( i = 0; i < HS_COUNT; i++ )
      if( hs_score[ i ] == value && hs_name[ i ] == name ) return;
    table_insert( value, name, table_rank( value ) );
}

// -----------------------------------------------------------------------------
//   MEMORY CARD
// -----------------------------------------------------------------------------
//   Same layout as Rockfield, under Gyre's own signature (word offsets):
//     0-19 signature   20 format version (2; format 1 loads without its demo)   21-25 scores   26-30 names
//     31-33 demo seed, length, score   34 checksum of 21-33
//     35 checksum of the demo   36 options   37 checksum of 36 and 38
//     38 demo difficulty   40- demo recording (REC_WORDS words)
//   A card belonging to another game is never written to.

#define CARD_NONE      0
#define CARD_OURS      1
#define CARD_EMPTY     2
#define CARD_OTHER     3

#define SAVE_OFFSET     20
#define SAVE_HEADER     16
#define SAVE_EXTRA_AT   36
#define SAVE_DEMO_AT    40
#define SAVE_VERSION    2         // 2: fuseballs and pulsars (v1 demos are dropped)
#define SAVE_KEY        0x2F6A9B3D

#define SAVE_MSG_NONE      0
#define SAVE_MSG_SAVED     1
#define SAVE_MSG_NO_CARD   2
#define SAVE_MSG_OTHER     3

int* save_signature_text = "GYRE TUBE SHOOTER V1";   // exactly 20 characters
int card_state = CARD_NONE;
bool card_loaded = false;
int[ SAVE_HEADER ] save_header;
int[ 3 ] save_extra;
int[ REC_WORDS ] card_demo;

int checksum( int* data, int count )
{
    int h = SAVE_KEY, i;
    for( i = 0; i < count; i++ )
      h = ( ( h << 5 ) | ( ( h >> 27 ) & 31 ) ) ^ data[ i ];
    return h;
}

int pack_options()
{
    int f = 0, h = 0;
    if( opt_shake ) f = 1;
    if( opt_flash ) h = 1;
    return opt_music | ( opt_sfx << 4 ) | ( opt_difficulty << 8 ) | ( f << 10 ) | ( h << 11 ) | ( 1 << 12 );
}

void unpack_options( int word )
{
    opt_music = min( word & 15, 10 );
    opt_sfx = min( ( word >> 4 ) & 15, 10 );
    opt_difficulty = ( word >> 8 ) & 3;
    if( opt_difficulty > DIFF_HARD ) opt_difficulty = DIFF_NORMAL;
    opt_shake = ( ( word >> 10 ) & 1 ) != 0;
    opt_flash = ( ( word >> 11 ) & 1 ) != 0;
}

int extra_checksum()
{
    int[ 2 ] w;
    w[ 0 ] = save_extra[ 0 ];
    w[ 1 ] = save_extra[ 2 ];
    return checksum( w, 2 );
}

void load_save()
{
    int i;
    bool extra_ok;

    card_read_data( save_header, SAVE_OFFSET, SAVE_HEADER );
    if( save_header[ 0 ] != SAVE_VERSION && save_header[ 0 ] != 1 ) return;
    if( save_header[ 14 ] != checksum( &save_header[ 1 ], 13 ) ) return;

    card_read_data( save_extra, SAVE_EXTRA_AT, 3 );
    extra_ok = ( save_extra[ 1 ] == extra_checksum() && ( save_extra[ 0 ] & ( 1 << 12 ) ) );
    if( extra_ok )
    {
        unpack_options( save_extra[ 0 ] );
        apply_music_volume();
    }

    for( i = 0; i < HS_COUNT; i++ )
      table_merge( save_header[ 1 + i ], save_header[ 6 + i ] );

    // a format 1 demo was recorded before fuseballs and pulsars and might not replay
    if( save_header[ 0 ] == SAVE_VERSION && save_header[ 13 ] > demo_score
        && save_header[ 12 ] > 0 && save_header[ 12 ] <= REC_MAX_FRAMES )
    {
        card_read_data( card_demo, SAVE_DEMO_AT, REC_WORDS );
        if( save_header[ 15 ] == checksum( card_demo, REC_WORDS ) )
        {
            memcpy( demo_data, card_demo, REC_WORDS );
            demo_seed = save_header[ 11 ];
            demo_frames = save_header[ 12 ];
            demo_score = save_header[ 13 ];
            demo_difficulty = DIFF_NORMAL;
            if( extra_ok ) demo_difficulty = save_extra[ 2 ] & 3;
        }
    }
}

int high_score;

void refresh_card()
{
    if( !card_is_connected() )
    {
        card_state = CARD_NONE;
        card_loaded = false;
        return;
    }

    if( card_signature_matches( (game_signature*)save_signature_text ) ) card_state = CARD_OURS;
    else if( card_is_empty() ) card_state = CARD_EMPTY;
    else card_state = CARD_OTHER;

    if( card_state == CARD_OURS && !card_loaded )
    {
        load_save();
        high_score = hs_score[ 0 ];
    }
    card_loaded = ( card_state != CARD_NONE );
}

int save_all()
{
    int i;
    refresh_card();

    if( card_state == CARD_NONE ) return SAVE_MSG_NO_CARD;
    if( card_state == CARD_OTHER ) return SAVE_MSG_OTHER;
    if( card_state == CARD_EMPTY ) card_write_signature( (game_signature*)save_signature_text );

    save_header[ 0 ] = SAVE_VERSION;
    for( i = 0; i < HS_COUNT; i++ )
    {
        save_header[ 1 + i ] = hs_score[ i ];
        save_header[ 6 + i ] = hs_name[ i ];
    }
    save_header[ 11 ] = demo_seed;
    save_header[ 12 ] = demo_frames;
    save_header[ 13 ] = demo_score;
    save_header[ 14 ] = checksum( &save_header[ 1 ], 13 );
    save_header[ 15 ] = checksum( demo_data, REC_WORDS );

    card_write_data( demo_data, SAVE_DEMO_AT, REC_WORDS );
    card_write_data( save_header, SAVE_OFFSET, SAVE_HEADER );

    save_extra[ 0 ] = pack_options();
    save_extra[ 2 ] = demo_difficulty;
    save_extra[ 1 ] = extra_checksum();
    card_write_data( save_extra, SAVE_EXTRA_AT, 3 );

    card_state = CARD_OURS;
    return SAVE_MSG_SAVED;
}

#include "demo_builtin.h"

void load_builtin_demo()
{
    demo_frames = 0;
    demo_score = 0;
    if( builtin_demo_frames <= 0 ) return;
    memcpy( demo_data, builtin_demo_data, REC_WORDS );
    demo_seed = builtin_demo_seed;
    demo_frames = builtin_demo_frames;
    demo_score = builtin_demo_score;
    demo_difficulty = DIFF_NORMAL;
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
          sfx_pitched( SND_FIRE, 0.28, fx_range( 0.95, 1.05 ) );
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
        a = fx_range( 0, 6.2831853 );
        v = speed * fx_range( 0.3, 1.0 );
        part_x[ i ] = x;
        part_y[ i ] = y;
        part_vx[ i ] = cos( a ) * v;
        part_vy[ i ] = sin( a ) * v;
        part_angle[ i ] = fx_range( 0, 6.2831853 );
        part_len[ i ] = length;
        part_life[ i ] = fx_range( 25, 50 );
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
#define EN_FUSEBALL  3                // rides the lane edges; hittable only while crossing a lane
#define EN_PULSAR    4                // electrifies its lane every few seconds

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
float[ MAX_ENEMIES ] en_side;         // fuseball: 0 = left edge of its lane, 1 = right edge
int[ MAX_ENEMIES ] en_dir;            // fuseball: crossing direction (0 = resting on an edge)
float[ MAX_ENEMIES ] en_zdir;         // fuseball: climbing (+) or backing off (-)
int[ MAX_ENEMIES ] en_pulse;          // pulsar: countdown to its next pulse

#define PULSE_FRAMES 24
int hint_timer = 0;                   // "new enemy" note under the level banner

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
    return ( 0.010 + level * 0.0012 ) * diff_pickf( 0.8, 1.0, 1.2 );
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
          en_side[ i ] = 0;
          en_dir[ i ] = 0;
          en_zdir[ i ] = 1;
          en_pulse[ i ] = 0;
          // the new types draw extra random numbers only for themselves, so
          // levels without them play out exactly as before
          if( type == EN_PULSAR )
          {
              en_speed[ i ] *= 0.6;
              en_target[ i ] = 0.4 + ( en_target[ i ] - 0.9 ) * 0.3;     // hunts from 0.4-0.82 deep
              en_pulse[ i ] = 100 + grand() % 60;
          }
          if( type == EN_FUSEBALL ) en_timer[ i ] = 20 + grand() % 40;
          return i;
      }
    return -1;
}

// picks what kind of enemy comes next, by level
int pick_enemy_type()
{
    int r = grand() % 100;
    if( level >= 5 && r >= 88 ) return EN_PULSAR;
    if( level >= 4 && r >= 76 ) return EN_FUSEBALL;
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
    else if( en_type[ i ] == EN_PULSAR ) add_score( 200 );
    else if( en_type[ i ] == EN_FUSEBALL )
    {
        // worth more the deeper it is
        if( en_z[ i ] > 2.6 ) add_score( 750 );
        else if( en_z[ i ] > 1.3 ) add_score( 500 );
        else add_score( 250 );
    }
    else add_score( 100 );

    burst_at( en_lane[ i ], en_z[ i ], 14, 2.5, 0xFFFFFFFF );
    if( en_type[ i ] == EN_FUSEBALL ) burst_at( en_lane[ i ], en_z[ i ], 10, 1.6, 0xFF40FF40 );
    if( en_type[ i ] == EN_PULSAR ) burst_at( en_lane[ i ], en_z[ i ], 10, 1.6, 0xFF2090FF );
    if( !quiet_kills ) sfx_pitched( SND_ENEMY_DIE, 0.45, fx_range( 0.9, 1.1 ) );
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
    hint_timer = 0;
    if( level == 4 || level == 5 ) hint_timer = 330;
    spawned = 0;
    spawn_timer = 90;

    phase = PHASE_FLYIN;
    phase_timer = 0;
    cam_z = -7;
    sfx( SND_LEVEL_START, 0.6 );
}

// resets every piece of gameplay state, then seeds the gameplay RNG:
// a live game and its replay must start out bit-for-bit identical
void begin_game( int seed, bool demo )
{
    int i;

    game_difficulty = opt_difficulty;
    if( demo ) game_difficulty = demo_difficulty;

    game_seed = seed;
    rng_state = seed;
    fx_state = seed ^ 0x2545F491;
    if( fx_state == 0 ) fx_state = 1;

    demo_mode = demo;
    sfx_muted = demo;
    demo_pos = 0;
    game_frame = 0;
    rec_frames = 0;
    for( i = 0; i < REC_WORDS; i++ ) rec_data[ i ] = 0;
    in_bits = 0;
    for( i = 0; i < 6; i++ ) in_held[ i ] = 0;
    input_lock = false;

    score = 0;
    lives = diff_pick( 5, 3, 2 );
    level = 1;
    next_extra_life = 20000;
    paused = false;
    fire_cooldown = 0;
    move_timer = 0;
    zap_flash_timer = 0;
    quiet_kills = false;
    shake = 0;
    flash = 0;
    web_spin = 0;
    save_message = SAVE_MSG_NONE;
    high_score = hs_score[ 0 ];
    for( i = 0; i < MAX_PARTS; i++ ) part_life[ i ] = 0;
    next_part = 0;

    stop_sfx_channels();
    start_level();
    state_timer = 0;

    if( demo )
    {
        state = STATE_DEMO;
        play_track( MUSIC_GAME, 0.3 );
    }
    else
    {
        state = STATE_PLAYING;
        play_track( MUSIC_GAME, 0.5 );
    }
}

void new_game()
{
    // the hardware RNG (seeded from the clock) picks the game's seed
    begin_game( rand() % 2000000000 + 1, false );
}

// -----------------------------------------------------------------------------
//   TITLE PAGES, GAME END, INITIALS ENTRY
// -----------------------------------------------------------------------------

#define PAGE_LOGO    0
#define PAGE_SCORES  1
int title_page = PAGE_LOGO;
int highlight_rank = -1;

void enter_title_page( int page )
{
    int i;
    level = 1;
    build_web( 0 );
    set_level_colors();
    clear_spikes();
    for( i = 0; i < MAX_ENEMIES; i++ ) en_active[ i ] = false;
    cam_z = -1.6;
    demo_mode = false;
    sfx_muted = false;
    paused = false;
    shake = 0;
    flash = 0;
    zap_flash_timer = 0;
    stop_sfx_channels();
    title_page = page;
    state = STATE_TITLE;
    state_timer = 0;
    play_track( MUSIC_GAME, 0.3 );
}

void enter_title()
{
    highlight_rank = -1;
    enter_title_page( PAGE_LOGO );
}

int* entry_charset = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789- ";
#define ENTRY_CHARS 38
int[ 3 ] entry_index;
int entry_cursor;
int entry_rank;

bool pressed_or_repeat( int held_frames )
{
    return held_frames == 1 || ( held_frames > 18 && held_frames % 5 == 0 );
}

// a live game ended: keep its recording if it is the best so far,
// then ask for initials or show the game over screen
void finish_game()
{
    bool demo_changed = false;

    if( score > demo_score && rec_frames >= DEMO_MIN_FRAMES )
    {
        memcpy( demo_data, rec_data, REC_WORDS );
        demo_seed = game_seed;
        demo_frames = rec_frames;
        demo_score = score;
        demo_difficulty = game_difficulty;
        demo_changed = true;
    }

    play_track( MUSIC_GAME, 0.3 );

    if( table_rank( score ) >= 0 )
    {
        entry_index[ 0 ] = 0;
        entry_index[ 1 ] = 0;
        entry_index[ 2 ] = 0;
        entry_cursor = 0;
        entry_rank = table_rank( score );
        sfx( SND_EXTRA_LIFE, 0.6 );
        state = STATE_ENTRY;
        state_timer = 0;
        return;
    }

    save_message = SAVE_MSG_NONE;
    if( demo_changed ) save_message = save_all();
    state = STATE_GAMEOVER;
    state_timer = 0;
}

void finish_entry()
{
    int[ 4 ] name;
    name[ 0 ] = entry_charset[ entry_index[ 0 ] ];
    name[ 1 ] = entry_charset[ entry_index[ 1 ] ];
    name[ 2 ] = entry_charset[ entry_index[ 2 ] ];
    name[ 3 ] = 0;

    table_insert( score, pack_name( name ) | ( game_difficulty << 24 ), entry_rank );
    high_score = hs_score[ 0 ];
    save_message = save_all();
    sfx( SND_LEVEL_START, 0.6 );
    highlight_rank = entry_rank;
    enter_title_page( PAGE_SCORES );
}

void update_entry()
{
    int i = entry_cursor;

    if( pressed_or_repeat( gamepad_up() ) )
    {
        entry_index[ i ] = ( entry_index[ i ] + 1 ) % ENTRY_CHARS;
        sfx( SND_MOVE, 0.4 );
    }
    if( pressed_or_repeat( gamepad_down() ) )
    {
        entry_index[ i ] = ( entry_index[ i ] + ENTRY_CHARS - 1 ) % ENTRY_CHARS;
        sfx( SND_MOVE, 0.4 );
    }
    if( gamepad_left() == 1 && entry_cursor > 0 ) entry_cursor--;
    if( gamepad_right() == 1 && entry_cursor < 2 ) entry_cursor++;

    if( gamepad_button_b() == 1 || gamepad_button_a() == 1 )
    {
        if( entry_cursor < 2 )
        {
            entry_cursor++;
            sfx( SND_SPIKE_HIT, 0.5 );
        }
        else
        {
            finish_entry();
            return;
        }
    }

    if( gamepad_button_start() == 1 || state_timer > 1800 ) finish_entry();
}

// -----------------------------------------------------------------------------
//   OPTIONS SCREEN
// -----------------------------------------------------------------------------

#define OPT_MUSIC    0
#define OPT_SFX      1
#define OPT_DIFF     2
#define OPT_SHAKE    3
#define OPT_FLASH    4
#define OPT_RESET    5
#define OPT_BACK     6
#define OPT_ROWS     7

int opt_cursor = 0;
bool opt_confirm_reset = false;
int opt_note_timer = 0;

void enter_options()
{
    opt_cursor = 0;
    opt_confirm_reset = false;
    opt_note_timer = 0;
    state = STATE_OPTIONS;
    state_timer = 0;
}

void leave_options()
{
    refresh_card();
    if( card_state == CARD_OURS || card_state == CARD_EMPTY ) save_all();

    if( options_from_pause )
    {
        options_from_pause = false;
        state = STATE_PLAYING;
        state_timer = 0;
        return;
    }
    enter_title();
}

void reset_high_scores()
{
    init_table();
    load_builtin_demo();
    high_score = hs_score[ 0 ];
    refresh_card();
    if( card_state == CARD_OURS || card_state == CARD_EMPTY ) save_all();
    opt_note_timer = 150;
    sfx( SND_LEVEL_START, 0.6 );
}

void update_options()
{
    int change = 0;

    if( opt_note_timer > 0 ) opt_note_timer--;

    if( pressed_or_repeat( gamepad_up() ) )
    {
        opt_cursor = ( opt_cursor + OPT_ROWS - 1 ) % OPT_ROWS;
        opt_confirm_reset = false;
        sfx( SND_MOVE, 0.4 );
    }
    if( pressed_or_repeat( gamepad_down() ) )
    {
        opt_cursor = ( opt_cursor + 1 ) % OPT_ROWS;
        opt_confirm_reset = false;
        sfx( SND_MOVE, 0.4 );
    }

    if( pressed_or_repeat( gamepad_left() ) ) change = -1;
    if( pressed_or_repeat( gamepad_right() ) ) change = 1;

    if( change != 0 )
    {
        if( opt_cursor == OPT_MUSIC )
        {
            opt_music = max( 0, min( 10, opt_music + change ) );
            apply_music_volume();
        }
        else if( opt_cursor == OPT_SFX )
        {
            opt_sfx = max( 0, min( 10, opt_sfx + change ) );
            sfx( SND_ENEMY_DIE, 0.5 );            // hear the new level
        }
        else if( opt_cursor == OPT_DIFF )
        {
            // shown in the order easy, normal, hard
            if( opt_difficulty == DIFF_EASY && change > 0 ) opt_difficulty = DIFF_NORMAL;
            else if( opt_difficulty == DIFF_NORMAL && change > 0 ) opt_difficulty = DIFF_HARD;
            else if( opt_difficulty == DIFF_HARD && change < 0 ) opt_difficulty = DIFF_NORMAL;
            else if( opt_difficulty == DIFF_NORMAL && change < 0 ) opt_difficulty = DIFF_EASY;
            sfx( SND_SPIKE_HIT, 0.5 );
        }
        else if( opt_cursor == OPT_SHAKE )
        {
            opt_shake = !opt_shake;
            sfx( SND_SPIKE_HIT, 0.5 );
            add_shake( 6 );
        }
        else if( opt_cursor == OPT_FLASH )
        {
            opt_flash = !opt_flash;
            sfx( SND_SPIKE_HIT, 0.5 );
            add_flash( 0.3, 0xFFFFE0C0 );
        }
    }

    if( gamepad_button_b() == 1 || gamepad_button_a() == 1 )
    {
        if( opt_cursor == OPT_RESET )
        {
            if( !opt_confirm_reset )
            {
                opt_confirm_reset = true;
                sfx( SND_GRAB, 0.4 );
            }
            else
            {
                opt_confirm_reset = false;
                reset_high_scores();
            }
        }
        else if( opt_cursor == OPT_BACK )
        {
            leave_options();
            return;
        }
    }

    if( gamepad_button_start() == 1 || gamepad_button_y() == 1 ) leave_options();
}

// -----------------------------------------------------------------------------
//   PAUSE MENU
// -----------------------------------------------------------------------------

#define PAUSE_RESUME   0
#define PAUSE_OPTIONS  1
#define PAUSE_QUIT     2

int pause_cursor = 0;
bool pause_confirm_quit = false;

void pause_game()
{
    paused = true;
    pause_cursor = PAUSE_RESUME;
    pause_confirm_quit = false;
    pause_all_channels();
    sfx( SND_SPIKE_HIT, 0.5 );
}

void resume_game()
{
    paused = false;
    resume_all_channels();
    sfx( SND_SPIKE_HIT, 0.5 );
    input_lock = true;
}

void update_pause_menu()
{
    if( pressed_or_repeat( gamepad_up() ) )
    {
        pause_cursor = ( pause_cursor + 2 ) % 3;
        pause_confirm_quit = false;
        sfx( SND_MOVE, 0.4 );
    }
    if( pressed_or_repeat( gamepad_down() ) )
    {
        pause_cursor = ( pause_cursor + 1 ) % 3;
        pause_confirm_quit = false;
        sfx( SND_MOVE, 0.4 );
    }

    if( gamepad_button_start() == 1 )
    {
        resume_game();
        return;
    }

    if( gamepad_button_b() == 1 || gamepad_button_a() == 1 )
    {
        if( pause_cursor == PAUSE_RESUME ) resume_game();
        else if( pause_cursor == PAUSE_OPTIONS )
        {
            options_from_pause = true;
            enter_options();
        }
        else if( !pause_confirm_quit )
        {
            pause_confirm_quit = true;
            sfx( SND_GRAB, 0.4 );
        }
        else
        {
            // abandon the game: not scored, never becomes the demo
            paused = false;
            enter_title();
        }
    }
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
    if( in_bits & IN_LEFT ) dx = -1;
    if( in_bits & IN_RIGHT ) dx = 1;
    if( in_bits & IN_UP ) dy = -1;
    if( in_bits & IN_DOWN ) dy = 1;

    held = max( max( in_held[ 0 ], in_held[ 1 ] ), max( in_held[ 2 ], in_held[ 3 ] ) );
    if( dx != 0 || dy != 0 )
    {
        if( held == 1 || ( held > 10 && held % 4 == 0 ) ) move_claw( dx, dy );
    }

    // the drawn claw slides quickly to its new lane
    if( claw_slide < 1 ) claw_slide = fmin( 1, claw_slide + 0.34 );

    // fire
    if( fire_cooldown > 0 ) fire_cooldown--;
    if( ( in_bits & IN_FIRE ) && fire_cooldown == 0 )
    {
        fire_shot();
        fire_cooldown = 6;
    }

    // superzapper
    if( in_held[ 5 ] == 1 && zapper_uses < 2 && phase == PHASE_FIGHT ) use_zapper();
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
                && en_z[ e ] >= z_old - 0.12 && en_z[ e ] <= shot_z[ s ] + 0.12
                && ( en_type[ e ] != EN_FUSEBALL || ( en_side[ e ] > 0.1 && en_side[ e ] < 0.9 ) ) )
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
          eshot_z[ e ] -= ( 0.035 + level * 0.002 ) * diff_pickf( 0.85, 1.0, 1.15 );
          if( eshot_z[ e ] <= 0.05 )
          {
              eshot_active[ e ] = false;
              if( eshot_lane[ e ] == claw_lane && phase == PHASE_FIGHT ) kill_claw();
          }
      }
}

// starts a fuseball crossing a lane; dir +1 = toward higher lanes
void start_fuse_hop( int i, int dir )
{
    int other;
    if( dir > 0 && en_side[ i ] >= 1 )
    {
        other = lane_wrap( en_lane[ i ] + 1 );
        if( other < 0 ) dir = -1;                   // end of an open web: turn back
        else { en_lane[ i ] = other; en_side[ i ] = 0; }
    }
    else if( dir < 0 && en_side[ i ] <= 0 )
    {
        other = lane_wrap( en_lane[ i ] - 1 );
        if( other < 0 ) dir = 1;
        else { en_lane[ i ] = other; en_side[ i ] = 1; }
    }
    en_dir[ i ] = dir;
}

void update_fuseball( int i )
{
    int d;

    if( en_dir[ i ] != 0 )
    {
        // crossing a lane: the only time it can be shot. On the rim it rolls
        // slowly enough that auto-fire (a shot every 6 frames) always gets a
        // chance at it before it reaches the middle of the claw's lane
        if( en_state[ i ] == ES_RIM ) en_side[ i ] += en_dir[ i ] * 0.04;
        else en_side[ i ] += en_dir[ i ] * ( 0.05 + level * 0.002 );
        if( en_side[ i ] >= 1 ) { en_side[ i ] = 1; en_dir[ i ] = 0; }
        if( en_side[ i ] <= 0 ) { en_side[ i ] = 0; en_dir[ i ] = 0; }
        return;
    }

    en_timer[ i ]--;

    if( en_state[ i ] == ES_CLIMB )
    {
        // climbs in fits and starts, sometimes backing off
        en_z[ i ] -= en_speed[ i ] * en_zdir[ i ];
        if( en_z[ i ] > DEPTH ) { en_z[ i ] = DEPTH; en_zdir[ i ] = 1.2; }

        if( en_timer[ i ] <= 0 )
        {
            d = grand() % 100;
            en_timer[ i ] = 25 + grand() % 50;
            en_zdir[ i ] = 1.2;
            if( d < 55 ) start_fuse_hop( i, 1 - 2 * ( grand() % 2 ) );
            else if( d < 75 ) en_zdir[ i ] = -0.7;
        }

        if( en_z[ i ] <= 0 )
        {
            en_z[ i ] = 0;
            en_state[ i ] = ES_RIM;
            en_timer[ i ] = 15;
        }
    }
    else
    {
        // on the rim: rolls from edge to edge toward the claw
        if( en_timer[ i ] <= 0 )
        {
            d = lane_delta( en_lane[ i ], claw_lane );
            if( ( en_side[ i ] <= 0 && d >= 0 ) || ( en_side[ i ] >= 1 && d > 0 ) ) start_fuse_hop( i, 1 );
            else start_fuse_hop( i, -1 );
            en_timer[ i ] = diff_pick( 26, 18, 12 ) + grand() % 10;
        }
    }
}

// returns true when its pulse destroyed the claw
bool update_pulsar( int i )
{
    int d, charge = diff_pick( 50, 40, 32 );

    // climbs slowly to its hunting depth, flipping now and then
    if( en_z[ i ] > en_target[ i ] )
    {
        en_z[ i ] -= en_speed[ i ];
        if( en_z[ i ] < en_target[ i ] ) en_z[ i ] = en_target[ i ];
    }

    en_timer[ i ]--;
    if( en_timer[ i ] <= 0 && en_flip_to[ i ] < 0 && en_pulse[ i ] > charge )
    {
        if( en_z[ i ] <= en_target[ i ] )
        {
            d = lane_delta( en_lane[ i ], claw_lane );
            if( d > 0 ) start_flip( i, 1 );
            else if( d < 0 ) start_flip( i, -1 );
            en_timer[ i ] = 50 + grand() % 40;
        }
        else
        {
            start_flip( i, 1 - 2 * ( grand() % 2 ) );
            en_timer[ i ] = 90 + grand() % 70;
        }
    }

    // pulse cycle: charge (the lane flickers), then the lane is live
    en_pulse[ i ]--;
    if( en_pulse[ i ] == 0 ) sfx( SND_PULSE, 0.45 );
    if( en_pulse[ i ] <= -PULSE_FRAMES ) en_pulse[ i ] = diff_pick( 190, 150, 120 );

    if( en_pulse[ i ] <= 0 && phase == PHASE_FIGHT && enemy_lane( i ) == claw_lane )
    {
        kill_claw();
        return true;
    }
    return false;
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
            spawn_timer = ( max( 75 - level * 4, 25 ) + grand() % 30 ) * diff_pickf( 1.3, 1.0, 0.8 );
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

        if( en_type[ i ] == EN_FUSEBALL ) update_fuseball( i );
        else if( en_type[ i ] == EN_PULSAR )
        {
            if( update_pulsar( i ) ) return;
        }
        else if( en_state[ i ] == ES_CLIMB )
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
                if( en_z[ i ] > 1.2 && grand() % 1000 < diff_pick( 2, 4, 6 ) + level ) enemy_fire( en_lane[ i ], en_z[ i ] );
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
                en_timer[ i ] = max( 26 - level, 12 ) + diff_pick( 8, 0, -4 );
            }
        }

        // touching the claw on the rim: grabbed
        if( phase == PHASE_FIGHT && en_active[ i ] && en_z[ i ] < 0.15 && enemy_lane( i ) == claw_lane
            && ( en_type[ i ] != EN_FUSEBALL || ( en_side[ i ] > 0.4 && en_side[ i ] < 0.6 ) ) )
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
    int i, bits;

    if( !demo_mode )
    {
        if( paused )
        {
            update_pause_menu();
            if( paused || state != STATE_PLAYING ) return;
        }
        else if( gamepad_button_start() == 1 )
        {
            pause_game();
            return;
        }
    }

    // logic projects explosions through the camera: aim it first
    set_camera();

    if( hint_timer > 0 ) hint_timer--;

    // this logic frame's input: replayed, or read live and recorded
    if( demo_mode )
    {
        bits = rec_get( demo_data, demo_pos );
        demo_pos++;
    }
    else
    {
        bits = read_gamepad_bits();
        rec_add( bits );
    }
    apply_input( bits );
    game_frame++;

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
                if( demo_mode ) enter_title();
                else finish_game();
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
        b = 0.5 + 0.5 * sin( anim * 0.04 + star_p[ i ] );
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
    if( in_game() && state != STATE_GAMEOVER && phase != PHASE_DEAD )
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
    pulse = 0.02 * sin( anim * 0.3 );

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

// fuseball: five curly tendrils in different colors, spinning, sitting on a lane edge
void draw_fuseball( int i )
{
    int k;
    float x, y, px, py, s, a, bx, by;
    int[ 5 ] colors;

    colors[ 0 ] = 0xFF4040FF;  colors[ 1 ] = 0xFF40FFFF;  colors[ 2 ] = 0xFF40FF40;
    colors[ 3 ] = 0xFFFFFF40;  colors[ 4 ] = 0xFFFF40FF;

    x = lx( en_lane[ i ], -1, 0, en_side[ i ] );
    y = ly( en_lane[ i ], -1, 0, en_side[ i ] );
    if( !vec_project( x, y, en_z[ i ], &px, &py ) ) return;

    s = FOCAL / ( en_z[ i ] - cam_z ) * 0.13;
    vec_thickness( 2 );
    vec_glow_size( 10 );
    for( k = 0; k < 5; k++ )
    {
        a = anim * 0.18 + k * 1.2566;
        bx = px + cos( a ) * s * 0.55;
        by = py + sin( a ) * s * 0.55;
        vec_color( colors[ k ], vec_scale_rgb( colors[ k ], 0.4 ) );
        vec_line( px, py, bx, by );
        vec_line( bx, by, px + cos( a + 0.7 ) * s, py + sin( a + 0.7 ) * s );
    }
}

// pulsar: a zigzag across its lane that swells while charging; while it
// pulses, its whole lane up to the rim is live
void draw_pulsar( int i, float a0x, float a0y, float a1x, float a1y, float z )
{
    int k, lane, nxt, charge = diff_pick( 50, 40, 32 );
    float amp = 0.09, t0, t1, z0, z1, j;
    bool charging = en_pulse[ i ] > 0 && en_pulse[ i ] <= charge;
    bool live = en_pulse[ i ] <= 0;

    if( charging ) amp = 0.09 + 0.1 * ( 1 - en_pulse[ i ] / (float)charge );
    if( live ) amp = 0.21;

    vec_thickness( 2.5 );
    vec_glow_size( 14 );
    if( live ) vec_color( 0xFFFFFFFF, 0xFF2090FF );
    else vec_color( 0xFF2090FF, 0xFF103060 );

    for( k = 0; k < 6; k++ )
    {
        t0 = k / 6.0;
        t1 = ( k + 1 ) / 6.0;
        z0 = z + amp;  z1 = z - amp;
        if( k % 2 ) { z0 = z - amp; z1 = z + amp; }
        vec_line_3d( a0x + ( a1x - a0x ) * t0, a0y + ( a1y - a0y ) * t0, z0,
                     a0x + ( a1x - a0x ) * t1, a0y + ( a1y - a0y ) * t1, z1 );
    }

    if( en_flip_to[ i ] >= 0 ) return;
    lane = en_lane[ i ];
    nxt = lane_next_point( lane );

    if( charging && ( anim / 3 ) % 2 == 0 )
    {
        // warning: the lane edges flicker
        vec_thickness( 2 );
        vec_glow_size( 12 );
        vec_color( 0xFF2080FF, 0xFF102040 );
        vec_line_3d( web_x[ lane ], web_y[ lane ], 0, web_x[ lane ], web_y[ lane ], z );
        vec_line_3d( web_x[ nxt ], web_y[ nxt ], 0, web_x[ nxt ], web_y[ nxt ], z );
    }
    else if( live )
    {
        // the lane is live: bright edges and a crackle down the middle
        vec_thickness( 3 );
        vec_glow_size( 18 );
        vec_color( 0xFFC0F0FF, 0xFF2090FF );
        vec_line_3d( web_x[ lane ], web_y[ lane ], 0, web_x[ lane ], web_y[ lane ], z );
        vec_line_3d( web_x[ nxt ], web_y[ nxt ], 0, web_x[ nxt ], web_y[ nxt ], z );
        vec_thickness( 1.5 );
        t0 = 0.5;
        z0 = 0;
        for( k = 1; k <= 6; k++ )
        {
            z1 = z * k / 6.0;
            j = fx_range( 0.25, 0.75 );
            if( k == 6 ) j = 0.5;
            vec_line_3d( lx( lane, -1, 0, t0 ), ly( lane, -1, 0, t0 ), z0, lx( lane, -1, 0, j ), ly( lane, -1, 0, j ), z1 );
            t0 = j;
            z0 = z1;
        }
    }
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
        else if( en_type[ i ] == EN_FUSEBALL )
        {
            draw_fuseball( i );
        }
        else if( en_type[ i ] == EN_PULSAR )
        {
            draw_pulsar( i, a0x, a0y, a1x, a1y, z );
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
                ang = anim * 0.25 + i;
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
          vec_line( px + cos( anim * 0.3 ) * 4, py + sin( anim * 0.3 ) * 4, px - cos( anim * 0.3 ) * 4, py - sin( anim * 0.3 ) * 4 );
          vec_line( px + sin( anim * 0.3 ) * 4, py - cos( anim * 0.3 ) * 4, px - sin( anim * 0.3 ) * 4, py + cos( anim * 0.3 ) * 4 );
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

#define DIM_TEXT    0xFFC09696
#define TEXT_CORE   0xFFFFFFFF
#define TEXT_GLOW   0xFF705050

void draw_pause_menu()
{
    int i;
    float y, x;
    int* label;

    for( i = 0; i <= PAUSE_QUIT; i++ )
    {
        y = 170 + i * 30;
        label = "RESUME";
        if( i == PAUSE_OPTIONS ) label = "OPTIONS";
        if( i == PAUSE_QUIT ) label = "QUIT TO TITLE";
        if( i == PAUSE_QUIT && pause_confirm_quit ) label = "PRESS B AGAIN TO QUIT";

        vec_thickness( 2 );
        vec_glow_size( 14 );
        if( i == pause_cursor )
        {
            if( i == PAUSE_QUIT && pause_confirm_quit ) vec_color( 0xFF8080FF, 0xFF202070 );
            else vec_color( 0xFFFFFF60, 0xFF606020 );
            x = 320 - vec_text_width( label, 7 ) / 2 - 20 + 3 * sin( frame * 0.2 );
            vec_line( x, y + 1, x + 9, y + 7 );
            vec_line( x, y + 13, x + 9, y + 7 );
        }
        else vec_color( DIM_TEXT, 0 );

        vec_text_centered( label, 320, y, 7 );
    }

    vec_thickness( 1.2 );
    vec_color( DIM_TEXT, 0 );
    vec_text_centered( "UP DOWN SELECT   B CHOOSE   START RESUME", 320, 280, 4.2 );
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

    if( hint_timer > 0 )
    {
        vec_thickness( 1.8 );
        vec_glow_size( 12 );
        vec_intensity( fmin( 1.0, fmin( ( 330 - hint_timer ) / 15.0, hint_timer / 30.0 ) ) );
        if( level == 4 )
        {
            vec_color( 0xFF60FF60, 0xFF186018 );
            vec_text_centered( "FUSEBALLS - SHOOT THEM AS THEY CROSS A LANE", 320, 343, 5 );
        }
        else
        {
            vec_color( 0xFF2090FF, 0xFF103060 );
            vec_text_centered( "PULSARS - STAY OUT OF A FLICKERING LANE", 320, 343, 5 );
        }
        vec_intensity( 1 );
    }

    if( phase == PHASE_WARP && phase_timer < 50 )
    {
        vec_thickness( 2 );
        vec_glow_size( 14 );
        vec_color( 0xFFFFE0A0, 0xFF805020 );
        vec_text_centered( "AVOID THE SPIKES", 320, 290, 7 );
    }

    if( paused && state == STATE_PLAYING )
    {
        vec_thickness( 3 );
        vec_glow_size( 20 );
        vec_color( 0xFFFFFFFF, 0xFF705050 );
        vec_text_centered( "PAUSED", 320, 110, 14 );
        draw_pause_menu();
    }
}

void draw_card_status()
{
    vec_thickness( 1.2 );
    vec_glow_size( 8 );
    vec_color( 0xFF907070, 0 );
    if( card_state == CARD_OURS )       vec_text_centered( "HIGH SCORES SAVED ON MEMORY CARD", 320, 342, 4.2 );
    else if( card_state == CARD_EMPTY ) vec_text_centered( "MEMORY CARD READY", 320, 342, 4.2 );
    else if( card_state == CARD_OTHER ) vec_text_centered( "MEMORY CARD HAS ANOTHER GAME - NOT SAVING", 320, 342, 4.2 );
    else                                vec_text_centered( "NO MEMORY CARD - HIGH SCORES WILL NOT BE SAVED", 320, 342, 4.2 );
}

void draw_save_message( float y )
{
    vec_thickness( 1.5 );
    vec_glow_size( 10 );
    vec_color( 0xFFC0A0A0, 0 );
    if( save_message == SAVE_MSG_SAVED )        vec_text_centered( "SAVED TO MEMORY CARD", 320, y, 5 );
    else if( save_message == SAVE_MSG_NO_CARD ) vec_text_centered( "NO MEMORY CARD - NOT SAVED", 320, y, 5 );
    else                                        vec_text_centered( "CARD HAS ANOTHER GAME - NOT SAVED", 320, y, 5 );
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
    vec_color( 0xFFFFB090, 0xFF603020 );
    vec_text_centered( "Y  OPTIONS", 320, 226, 6 );

    vec_color( 0xFFC09696, 0 );
    vec_text_centered( "D-PAD MOVE   B FIRE   X SUPERZAPPER", 320, 262, 5 );
    vec_text_centered( "START PAUSE", 320, 284, 5 );

    vec_color( 0xFFFFE0A0, 0xFF403010 );
    vec_text( "HIGH SCORE", 230, 314, 6 );
    vec_number( high_score, 230 + vec_text_width( "HIGH SCORE ", 6 ), 314, 6 );

    draw_card_status();

    vec_thickness( 1 );
    vec_color( 0xFF806868, 0 );
    vec_text( GYRE_VERSION, 596, 344, 3.5 );
}

void draw_game_over()
{
    float t = fmin( 1.0, state_timer / 30.0 );

    vec_intensity( t );
    vec_thickness( 3 );
    vec_glow_size( 22 );
    vec_color( 0xFF8080FF, 0xFF202070 );
    vec_text_centered( "GAME OVER", 320, 120 - 20 * ( 1 - t ), 18 );

    if( save_message != SAVE_MSG_NONE ) draw_save_message( 262 );

    if( state_timer > 90 && ( frame / 30 ) % 2 == 0 )
    {
        vec_thickness( 2 );
        vec_glow_size( 14 );
        vec_color( TEXT_CORE, TEXT_GLOW );
        vec_text_centered( "PRESS START", 320, 220, 8 );
    }
    vec_intensity( 1 );
}

void draw_scores_page()
{
    int i, k;
    float y, t;
    int[ 4 ] name;
    int[ 2 ] rank_text;

    t = fmin( 1.0, state_timer / 30.0 );
    vec_intensity( t );

    vec_thickness( 3 );
    vec_glow_size( 22 );
    vec_color( 0xFFFFE0A0, 0xFF805020 );
    vec_text_centered( "HIGH SCORES", 320, 34, 15 );

    vec_thickness( 1.5 );
    vec_glow_size( 10 );
    vec_color( 0xFFFFB090, 0xFF603020 );
    vec_dashed_line( 170, 82, 470, 82, 8, 6 );

    rank_text[ 1 ] = 0;

    for( i = 0; i < HS_COUNT; i++ )
    {
        y = 104 + i * 40;
        vec_intensity( t * fmin( 1.0, fmax( 0.0, ( state_timer - i * 8 ) / 20.0 ) ) );

        if( i == highlight_rank )
        {
            vec_thickness( 2.5 );
            vec_glow_size( 18 );
            if( ( frame / 10 ) % 2 ) vec_color( 0xFF60FFFF, 0xFF208080 );
            else vec_color( 0xFFFFFFFF, 0xFF406060 );
        }
        else
        {
            vec_thickness( 2 );
            vec_glow_size( 14 );
            if( i == 0 ) vec_color( 0xFFFFF0B0, 0xFF805020 );
            else vec_color( 0xFFF0D8E8, 0xFF603848 );
        }

        rank_text[ 0 ] = '1' + i;
        vec_text( rank_text, 180, y, 9 );
        unpack_name( hs_name[ i ], name );
        vec_text( name, 230, y, 9 );
        vec_number( hs_score[ i ], 460 - number_width( hs_score[ i ], 9 ), y, 9 );

        k = ( hs_name[ i ] >> 24 ) & 3;
        if( k != DIFF_NORMAL )
        {
            vec_thickness( 1.5 );
            vec_glow_size( 8 );
            if( k == DIFF_EASY ) { vec_color( 0xFF90E090, 0 ); vec_char( 'E', 474, y + 6, 4 ); }
            else                 { vec_color( 0xFF8080FF, 0 ); vec_char( 'H', 474, y + 6, 4 ); }
        }
    }

    vec_intensity( t );

    if( save_message != SAVE_MSG_NONE && highlight_rank >= 0 ) draw_save_message( 312 );
    else if( ( frame / 30 ) % 2 == 0 )
    {
        vec_thickness( 2 );
        vec_glow_size( 14 );
        vec_color( TEXT_CORE, TEXT_GLOW );
        vec_text_centered( "PRESS START", 320, 308, 8 );
    }

    draw_card_status();
    vec_intensity( 1 );
}

void draw_entry()
{
    int i;
    float x, w, t;
    int[ 2 ] letter;

    t = fmin( 1.0, state_timer / 25.0 );
    vec_intensity( t );

    vec_thickness( 3 );
    vec_glow_size( 22 );
    if( entry_rank == 0 )
    {
        vec_color( 0xFF60FFFF, 0xFF205050 );
        vec_text_centered( "NEW HIGH SCORE", 320, 30, 14 );
    }
    else
    {
        vec_color( 0xFFFFE0A0, 0xFF805020 );
        vec_text_centered( "TOP 5 SCORE", 320, 30, 14 );
    }

    vec_thickness( 2.5 );
    vec_glow_size( 16 );
    vec_color( TEXT_CORE, TEXT_GLOW );
    w = number_width( score, 12 );
    vec_number( score, 320 - w / 2, 84, 12 );

    vec_thickness( 1.5 );
    vec_glow_size( 10 );
    vec_color( DIM_TEXT, 0 );
    vec_text_centered( "ENTER YOUR INITIALS", 320, 136, 6 );

    letter[ 1 ] = 0;
    for( i = 0; i < 3; i++ )
    {
        x = 320 - 90 + i * 70;
        letter[ 0 ] = entry_charset[ entry_index[ i ] ];

        if( i == entry_cursor )
        {
            vec_thickness( 3.5 );
            vec_glow_size( 26 );
            vec_color( 0xFF60FFFF, 0xFF208080 );
            vec_text( letter, x, 170 + 3 * sin( frame * 0.2 ), 20 );
            if( ( frame / 15 ) % 2 == 0 ) vec_line( x - 4, 226, x + 24, 226 );
        }
        else
        {
            vec_thickness( 3 );
            vec_glow_size( 18 );
            vec_color( 0xFFF0D8E8, 0xFF603848 );
            vec_text( letter, x, 170, 20 );
            vec_line( x - 4, 226, x + 24, 226 );
        }

        if( letter[ 0 ] == ' ' )
        {
            vec_thickness( 1.5 );
            vec_dashed_line( x, 210, x + 20, 210, 3, 3 );
        }
    }

    vec_thickness( 1.5 );
    vec_glow_size( 10 );
    vec_color( DIM_TEXT, 0 );
    vec_text_centered( "UP DOWN  CHANGE     LEFT RIGHT  MOVE", 320, 268, 5 );
    vec_text_centered( "B  NEXT     START  DONE", 320, 290, 5 );

    vec_intensity( 1 );
}

void draw_demo_overlay()
{
    vec_thickness( 2.5 );
    vec_glow_size( 16 );
    if( ( frame / 40 ) % 2 == 0 )
    {
        vec_color( 0xFFFFE0A0, 0xFF805020 );
        vec_text_centered( "DEMO", 320, 64, 10 );
    }

    vec_thickness( 2 );
    vec_glow_size( 14 );
    vec_color( TEXT_CORE, TEXT_GLOW );
    vec_text_centered( "PRESS START", 320, 322, 8 );
}

void draw_value_arrows( float x_left, float x_right, float y )
{
    vec_line( x_left, y + 6, x_left + 6, y );
    vec_line( x_left, y + 6, x_left + 6, y + 12 );
    vec_line( x_right, y + 6, x_right - 6, y );
    vec_line( x_right, y + 6, x_right - 6, y + 12 );
}

void draw_options()
{
    int i, k, amount;
    float y, t, x;
    bool selected;
    int* value;

    t = fmin( 1.0, state_timer / 25.0 );
    vec_intensity( t );

    vec_thickness( 3 );
    vec_glow_size( 22 );
    vec_color( 0xFFFFE0A0, 0xFF805020 );
    vec_text_centered( "OPTIONS", 320, 24, 15 );

    for( i = 0; i < OPT_ROWS; i++ )
    {
        y = 86 + i * 30;
        if( i == OPT_RESET ) y += 10;
        if( i == OPT_BACK ) y += 14;
        selected = ( i == opt_cursor );

        vec_thickness( 1.8 );
        vec_glow_size( 12 );
        if( selected ) vec_color( 0xFFFFFF60, 0xFF606020 );
        else vec_color( DIM_TEXT, 0 );

        if( i == OPT_MUSIC ) vec_text( "MUSIC VOLUME", 110, y, 6 );
        else if( i == OPT_SFX ) vec_text( "EFFECTS VOLUME", 110, y, 6 );
        else if( i == OPT_DIFF ) vec_text( "DIFFICULTY", 110, y, 6 );
        else if( i == OPT_SHAKE ) vec_text( "SCREEN SHAKE", 110, y, 6 );
        else if( i == OPT_FLASH ) vec_text( "SCREEN FLASHES", 110, y, 6 );
        else if( i == OPT_RESET ) vec_text( "RESET HIGH SCORES", 110, y, 6 );
        else vec_text( "BACK", 110, y, 6 );

        if( selected )
        {
            x = 92 + 3 * sin( frame * 0.2 );
            vec_line( x, y + 1, x + 8, y + 6 );
            vec_line( x, y + 11, x + 8, y + 6 );
        }

        if( i == OPT_MUSIC || i == OPT_SFX )
        {
            amount = opt_music;
            if( i == OPT_SFX ) amount = opt_sfx;
            vec_thickness( 6 );
            vec_glow_size( 12 );
            for( k = 0; k < 10; k++ )
            {
                if( k < amount ) vec_color( 0xFFFFE060, 0xFF806020 );
                else vec_color( 0xFF503838, 0 );
                vec_line( 380 + k * 14, y + 6, 389 + k * 14, y + 6 );
            }
        }
        else if( i == OPT_DIFF || i == OPT_SHAKE || i == OPT_FLASH )
        {
            if( i == OPT_DIFF )
            {
                value = "NORMAL";
                if( opt_difficulty == DIFF_EASY ) value = "EASY";
                if( opt_difficulty == DIFF_HARD ) value = "HARD";
            }
            else
            {
                value = "OFF";
                if( i == OPT_SHAKE && opt_shake ) value = "ON";
                if( i == OPT_FLASH && opt_flash ) value = "ON";
            }

            vec_thickness( 2 );
            vec_glow_size( 14 );
            if( selected ) vec_color( TEXT_CORE, TEXT_GLOW );
            else vec_color( 0xFFE0D0D0, 0 );
            vec_text_centered( value, 449, y, 6 );
            if( selected ) draw_value_arrows( 384, 514, y );
        }
        else if( i == OPT_RESET && opt_confirm_reset && ( frame / 10 ) % 2 == 0 )
        {
            vec_thickness( 2 );
            vec_glow_size( 14 );
            vec_color( 0xFF8080FF, 0xFF202070 );
            vec_text_centered( "PRESS B AGAIN", 449, y, 5 );
        }
    }

    vec_thickness( 1.5 );
    vec_glow_size( 10 );
    vec_color( 0xFFFFB090, 0 );
    if( opt_cursor == OPT_DIFF )
    {
        if( options_from_pause )               vec_text_centered( "TAKES EFFECT IN THE NEXT GAME", 320, 316, 4.5 );
        else if( opt_difficulty == DIFF_EASY ) vec_text_centered( "5 LIVES - SLOWER ENEMIES - FEWER SHOTS", 320, 316, 4.5 );
        else if( opt_difficulty == DIFF_HARD ) vec_text_centered( "2 LIVES - FASTER ENEMIES - MORE SHOTS", 320, 316, 4.5 );
        else                                   vec_text_centered( "3 LIVES - THE STANDARD GAME", 320, 316, 4.5 );
    }
    else if( opt_note_timer > 0 )
    {
        vec_color( 0xFF60FFFF, 0 );
        vec_text_centered( "HIGH SCORES RESET", 320, 316, 5 );
    }

    vec_color( DIM_TEXT, 0 );
    vec_thickness( 1.2 );
    vec_text_centered( "UP DOWN SELECT   LEFT RIGHT CHANGE   START DONE", 320, 340, 4.2 );

    vec_intensity( 1 );
}

// -----------------------------------------------------------------------------
//   MAIN
// -----------------------------------------------------------------------------

void main()
{
    int i;
    bool menu_screen;

    srand( get_time() + 1 );
    vec_init( 0, 0 );
    set_global_volume( 0.6 );     // headroom: the sound chip does not clamp its mix
    select_gamepad( 0 );
    music_init( SND_INSTRUMENTS, CH_MUSIC );
    init_table();
    load_builtin_demo();

    // fixed seed: the starfield is the same on every boot
    rng_state = 20261031;
    for( i = 0; i < STARS; i++ )
    {
        star_x[ i ] = frand() * screen_width;
        star_y[ i ] = frand() * screen_height;
        star_p[ i ] = frand() * 6.2831853;
    }

    refresh_card();
    high_score = hs_score[ 0 ];
    enter_title();

    while( true )
    {
        // --- update ---
        if( state == STATE_TITLE )
        {
            refresh_card();            // notices cards inserted on the title screen
            web_spin = frame * 0.004;

            if( gamepad_button_start() == 1 && state_timer > 20 ) new_game();
            else if( gamepad_button_y() == 1 && state_timer > 20 ) enter_options();

            // logo -> high scores -> demo -> logo ...
            else if( title_page == PAGE_LOGO && state_timer > 480 )
              enter_title_page( PAGE_SCORES );
            else if( title_page == PAGE_SCORES && state_timer > 360 )
            {
                highlight_rank = -1;
                if( demo_frames > 0 ) begin_game( demo_seed, true );
                else enter_title_page( PAGE_LOGO );
            }
        }
        else if( state == STATE_PLAYING ) update_playing();
        else if( state == STATE_DEMO )
        {
            if( gamepad_button_start() == 1 ) new_game();
            else
            {
                update_playing();
                if( state == STATE_DEMO && ( demo_pos >= demo_frames || demo_pos >= DEMO_MAX_PLAY ) )
                  enter_title();
            }
        }
        else if( state == STATE_ENTRY )
        {
            web_spin = frame * 0.004;
            update_entry();
        }
        else if( state == STATE_OPTIONS )
        {
            if( !options_from_pause ) web_spin = frame * 0.004;
            update_options();
        }
        else
        {
            if( state_timer > 90 && gamepad_button_start() == 1 ) new_game();
            else if( state_timer > 900 ) enter_title();
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
        anim = frame;
        if( in_game() ) anim = game_frame;

        clear_screen( make_color_rgb( 2, 2, 8 ) );

        vec_offset( fx_range( -shake, shake ), fx_range( -shake, shake ) );
        set_camera();

        menu_screen = ( state == STATE_TITLE || state == STATE_ENTRY
                        || ( state == STATE_OPTIONS && !options_from_pause ) );
        if( paused ) vec_intensity( 0.35 );
        if( menu_screen ) vec_intensity( 0.55 );
        if( state == STATE_OPTIONS || state == STATE_ENTRY ) vec_intensity( 0.3 );
        if( state == STATE_TITLE && title_page == PAGE_SCORES ) vec_intensity( 0.3 );

        draw_stars();
        if( state != STATE_OPTIONS ) draw_web();      // the options text needs the line budget
        if( !menu_screen && state != STATE_OPTIONS )
        {
            draw_spikes_and_shots();
            draw_enemies();
            if( state != STATE_GAMEOVER ) draw_claw();
        }
        draw_particles();
        vec_intensity( 1 );
        vec_offset( 0, 0 );

        if( state == STATE_TITLE )
        {
            if( title_page == PAGE_LOGO ) draw_title();
            else draw_scores_page();
        }
        else if( state == STATE_ENTRY ) draw_entry();
        else if( state == STATE_OPTIONS ) draw_options();
        else
        {
            draw_hud();
            if( state == STATE_GAMEOVER ) draw_game_over();
            if( state == STATE_DEMO ) draw_demo_overlay();
        }

        vec_flush();

        // superzapper: the whole screen pulses (a flash, so it follows that option)
        if( opt_flash && zap_flash_timer > 0 && ( zap_flash_timer / 3 ) % 2 == 0 )
          vec_fill_rect( 0, 0, screen_width, screen_height, 0x30FFFFFF, blending_add );
        if( flash > 0.01 )
          vec_fill_rect( 0, 0, screen_width, screen_height, vec_scale_alpha( flash_color, flash * 0.6 ), blending_add );

        frame++;
        end_frame();
    }
}
