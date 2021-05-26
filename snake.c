/*
A character-based surround-the-opponent game.
Reads from nametable RAM to determine collisions, and also
to help the AI avoid walls.
Siege Game used for reference
For more information, see "Making Arcade Games in C".
*/

#include <stdlib.h>
#include <string.h>
#include <nes.h>
#include <joystick.h>

#include "neslib.h"

// VRAM buffer module
#include "vrambuf.h"
//#link "vrambuf.c"

// link the pattern table into CHR ROM
//#link "chr_generic.s"


#define COLS 32
#define ROWS 27

// read a character from VRAM.
// this is tricky because we have to wait
// for VSYNC to start, then set the VRAM
// address to read, then set the VRAM address
// back to the start of the frame.
byte getchar(byte x, byte y) {
  // compute VRAM read address
  word addr = NTADR_A(x,y);
  // result goes into rd
  byte rd;
  // wait for VBLANK to start
  ppu_wait_nmi();
  // set vram address and read byte into rd
  vram_adr(addr);
  vram_read(&rd, 1);
  // scroll registers are corrupt
  // fix by setting vram address
  vram_adr(0x0);
  return rd;
}

void cputcxy(byte x, byte y, char ch) {
  vrambuf_put(NTADR_A(x,y), &ch, 1);
}

void cputsxy(byte x, byte y, const char* str) {
  vrambuf_put(NTADR_A(x,y), str, strlen(str));
}

void clrscr() {
  vrambuf_clear();
  ppu_off();
  vram_adr(0x2000);
  vram_fill(0, 32*28);
  vram_adr(0x0);
  ppu_on_bg();
}

////////// GAME DATA

typedef struct {
  byte x;
  byte y;
  bool active;
} BodyPart;

typedef struct {
  byte x;
  byte y;
  byte dir;
  word score;
  char head_attr;
  char tail_attr;
  int collided:1;
  int human:1;
  int num;
  BodyPart body[45];
  int length;
} Player;

typedef struct {
  byte x;
  byte y;
  char attr;
} Powop;

Player players[2];
Powop pow;

byte attract;
byte num_players;
byte gameover;
byte frames_per_move;
unsigned char seed;

#define START_SPEED 4
#define MAX_SPEED 2
#define MAX_SCORE 7

///////////

const char BOX_CHARS[8] = { 0xa5,0xa3,0xa0,0xa2,0xa4,0xa1,0xa6,0xa7 };

void draw_box(byte x, byte y, byte x2, byte y2, const char* chars) {
  byte x1 = x;
  cputcxy(x, y, chars[2]);
  cputcxy(x2, y, chars[3]);
  cputcxy(x, y2, chars[0]);
  cputcxy(x2, y2, chars[1]);
  while (++x < x2) {
    cputcxy(x, y, chars[5]);
    cputcxy(x, y2, chars[4]);
  }
  while (++y < y2) {
    cputcxy(x1, y, chars[6]);
    cputcxy(x2, y, chars[7]);
  }
}

void draw_playfield() {
  draw_box(1,3,COLS-2,ROWS-1,BOX_CHARS);
  if (attract) {
    //cputsxy(3,ROWS-1,"BATTLE SNAKE - PRESS ENTER");
    cputsxy(8,2,"___Slither.NES___");
    cputsxy(3,ROWS-1,"Press: A for 1P | B for 2P");
  } else {
    cputcxy(9,2,players[0].score+'0');
    cputcxy(28,2,players[1].score+'0');
    cputsxy(1,1,"PLYR1:");
    cputsxy(20,1,"PLYR2:");
  }
}

typedef enum { D_RIGHT, D_DOWN, D_LEFT, D_UP } dir_t;
const char DIR_X[4] = { 1, 0, -1, 0 };
const char DIR_Y[4] = { 0, 1, 0, -1 };

void rand_place() {
  set_rand(seed);
  seed += rand8();
  pow.x = (rand8() % (29 - 2 + 1)) + 2;
  pow.y = (rand8() % (23 - 3 + 1)) + 3;
  while(getchar(pow.x, pow.y) != 0)
  {
    pow.x = (rand8() % (29 - 2 + 1)) + 2;
    pow.y = (rand8() % (23 - 3 + 1)) + 3;
  }
}

void init_game() {
  memset(players, 0, sizeof(players));
  players[0].head_attr = 0xae;
  players[1].head_attr = 0xaf;
  players[0].tail_attr = 0x06;
  players[1].tail_attr = 0x07;
  players[0].length = 2;
  players[1].length = 2;
  
  pow.attr = 0xad;
  
  //players[0].body[0].active = true;
  frames_per_move = START_SPEED;
}

void reset_players() {
  byte i;
  players[0].x = players[0].y = 5;
  players[0].dir = D_RIGHT;
  players[1].x = COLS-6;
  players[1].y = ROWS-6;
  players[1].dir = D_LEFT;
  players[0].collided = players[1].collided = 0;
  
  players[0].length = players[1].length = 2;
  
  
  players[0].body[0].active = players[1].body[0].active = true;
  for(i = 1; i < 45 && (players[0].body[i].active || players[1].body[i].active); i++)
  {
    players[0].body[i].active = players[1].body[i].active = false;
  }
  
  frames_per_move = START_SPEED +(2 * num_players * num_players);
}

void draw_player(Player* p) {
  cputcxy(p->x, p->y, p->head_attr);
}

void draw_powop(Powop* p) {
  //cputcxy(p->x, p->y, p->attr);
  vrambuf_put(NTADR_A(p->x,p->y), &p->attr, 1);
}

void update_body(Player* p) {
  byte i;
  byte tx = p->x;
  byte ty = p->y;
  byte xx;
  byte yy;
  for(i = 0; i < p->length; i++)
  {
    xx = p->body[i].x;
    yy = p->body[i].y;
    p->body[i].x = tx;
    p->body[i].y = ty;
    tx = xx;
    ty = yy;
    if(!p->body[i].active)
    {
      cputcxy(p->body[i].x, p->body[i].y, 0);
      break;
    }
  }
}

void move_player(Player* p) {
  //cputcxy(p->x, p->y, p->tail_attr);
  draw_player(p);
  update_body(p);
  p->x += DIR_X[p->dir];
  p->y += DIR_Y[p->dir];
  if (getchar(p->x, p->y) != 0)
  {
    if(getchar(p->x, p->y) == pow.attr)
    {
      rand_place();
      draw_powop(&pow);
      p->body[p->length-1].active = true;
      p->length++;
      if ((frames_per_move > MAX_SPEED && p->human) || (frames_per_move > MAX_SPEED && attract)) frames_per_move--;
    }
    else
      p->collided = 1;
  }
  draw_player(p);
}

void human_control(Player* p) {
  byte dir = 0xff;
  byte pad;
  if(p->num == 2)
  {
    pad = pad_poll(1);
  }
  else
    pad = pad_poll(0);
  // start game if attract mode
  if (attract && (pad & PAD_A))
  {
    gameover = 1;
    num_players = 1;
  }
  else if (attract && (pad & PAD_B))
  {
    gameover = 1;
    num_players = 2;
  }
  // do not allow movement unless human player
  if (!p->human) return;
  if (pad & PAD_LEFT) dir = D_LEFT;
  if (pad & PAD_RIGHT) dir = D_RIGHT;
  if (pad & PAD_UP) dir = D_UP;
  if (pad & PAD_DOWN) dir = D_DOWN;
  // don't let the player reverse
  if (dir < 0x80 && dir != (p->dir ^ 2)) {
    p->dir = dir;
  }
  //seed++;
}

byte ai_try_dir(Player* p, dir_t dir, byte shift) {
  byte x,y;
  dir &= 3;
  x = p->x + (DIR_X[dir] << shift);
  y = p->y + (DIR_Y[dir] << shift);
  if (getchar(x, y) == pow.attr) {
    p->dir = dir;
    return 1;
  }
  if (x < COLS && y < ROWS && (getchar(x, y) == 0)) {
    p->dir = dir;
    return 1;
  } else {
    return 0;
  }
}

void ai_control(Player* p) {
  dir_t dir;
  if (p->human) return;
  
  if(p->x > pow.x)
  {
    if(p->y != pow.y && ((rand8() % (1 - 0 + 1)) + 0) > 0)
      if(p->y < pow.y)
        dir = D_DOWN;
      else
        dir = D_UP;
    else
      dir = D_LEFT;
  }
  else if(p->x < pow.x)
  {
    if(p->y != pow.y && ((rand8() % (10 - 0 + 1)) + 0) > 6)
      if(p->y < pow.y)
        dir = D_DOWN;
      else
        dir = D_UP;
    else
      dir = D_RIGHT;
  }
  else
  {
    if(p->y < pow.y)
      dir = D_DOWN;
    else
      dir = D_UP;
  }
  
  //dir = p->dir;
  if (!ai_try_dir(p, dir, 0)) {
    ai_try_dir(p, dir+1, 0);
    ai_try_dir(p, dir-1, 0);
  } else {
    //ai_try_dir(p, dir-1, 0) && ai_try_dir(p, dir-1, 1+(rand() & 3));
    //ai_try_dir(p, dir+1, 0) && ai_try_dir(p, dir+1, 1+(rand() & 3));
    //ai_try_dir(p, dir, rand() & 3);
    seed++;
  }
}

void flash_colliders() {
  byte i;
  // flash players that collided
  for (i=0; i<56; i++) {
    //cv_set_frequency(CV_SOUNDCHANNEL_0, 1000+i*8);
    //cv_set_attenuation(CV_SOUNDCHANNEL_0, i/2);
    if (players[0].collided && (i % 4) == 0) players[0].head_attr ^= 0x30;
    if (players[1].collided && (i % 4) == 0) players[1].head_attr ^= 0x30;
    vrambuf_flush();
    vrambuf_flush();
    draw_player(&players[0]);
    draw_player(&players[1]);
  }
  //cv_set_attenuation(CV_SOUNDCHANNEL_0, 28);
}

void make_move() {
  byte i;
  for (i=0; i<frames_per_move; i++) {
    human_control(&players[0]);
    human_control(&players[1]);
    vrambuf_flush();
  }
  ai_control(&players[0]);
  ai_control(&players[1]);
  // if players collide, 2nd player gets the point
  move_player(&players[1]);
  move_player(&players[0]);
}

void declare_winner(byte winner) {
  byte i;
  clrscr();
  for (i=0; i<ROWS/2-3; i++) {
    //draw_box(i,i,COLS-1-i,ROWS-1-i,BOX_CHARS);
    vrambuf_flush();
  }
  cputsxy(12,10,"WINNER:");
  cputsxy(12,13,"PLAYER ");
  cputcxy(12+7, 13, '1'+winner);
  vrambuf_flush();
  delay(75);
  gameover = 1;
  attract = 1;
}

#define AE(tl,tr,bl,br) (((tl)<<0)|((tr)<<2)|((bl)<<4)|((br)<<6))

// this is attribute table data, 
// each 2 bits defines a color palette
// for a 16x16 box
const unsigned char Attrib_Table[0x40]={
AE(3,3,1,1),AE(3,3,1,1),AE(3,3,1,1),AE(3,3,1,1), AE(2,2,1,1),AE(2,2,1,1),AE(2,2,1,1),AE(2,2,1,1),
AE(1,0,1,0),AE(0,0,0,0),AE(0,0,0,0),AE(0,0,0,0), AE(0,0,0,0),AE(0,0,0,0),AE(0,0,0,0),AE(0,1,0,1),
AE(1,0,1,0),AE(0,0,0,0),AE(0,0,0,0),AE(0,0,0,0), AE(0,0,0,0),AE(0,0,0,0),AE(0,0,0,0),AE(0,1,0,1),
AE(1,0,1,0),AE(0,0,0,0),AE(0,0,0,0),AE(0,0,0,0), AE(0,0,0,0),AE(0,0,0,0),AE(0,0,0,0),AE(0,1,0,1),
AE(1,0,1,0),AE(0,0,0,0),AE(0,0,0,0),AE(0,0,0,0), AE(0,0,0,0),AE(0,0,0,0),AE(0,0,0,0),AE(0,1,0,1),
AE(1,0,1,0),AE(0,0,0,0),AE(0,0,0,0),AE(0,0,0,0), AE(0,0,0,0),AE(0,0,0,0),AE(0,0,0,0),AE(0,1,0,1),
AE(1,0,1,1),AE(0,0,1,1),AE(0,0,1,1),AE(0,0,1,1), AE(0,0,1,1),AE(0,0,1,1),AE(0,0,1,1),AE(0,1,1,1),
AE(1,1,1,1),AE(1,1,1,1),AE(1,1,1,1),AE(1,1,1,1), AE(1,1,1,1),AE(1,1,1,1),AE(1,1,1,1),AE(1,1,1,1),
};

/*{pal:"nes",layout:"nes"}*/
const unsigned char Palette_Table[16]={ 
  0x00,
  0x26,0x28,0x31,0x00,
  0x04,0x24,0x34,0x00,
  0x06,0x17,0x26,0x00,
  0x12,0x13,0x31
};

// put 8x8 grid of palette entries into the PPU
void setup_attrib_table() {
  vram_adr(0x23c0);
  vram_write(Attrib_Table, 0x40);
}

void setup_palette() {
  int i;
  // only set palette entries 0-15 (background only)
  for (i=0; i<16; i++)
    pal_col(i, Palette_Table[i] ^ attract);
}

void play_round() {
  ppu_off();
  setup_attrib_table();
  setup_palette();
  clrscr();
  draw_playfield();
  reset_players();
  rand_place();
  draw_powop(&pow);
  while (1) {
    if (attract)
      seed++;
    make_move();
    if (gameover) return; // attract mode -> start
    if (players[0].collided || players[1].collided) break;
  }
  flash_colliders();
  // add scores to players that didn't collide
  if (players[0].collided) players[1].score++;
  if (players[1].collided) players[0].score++;
  // increase speed
  //if (frames_per_move > MAX_SPEED) frames_per_move--;
  // game over?
  if (players[0].score != players[1].score) {
    if (players[0].score >= MAX_SCORE)
      declare_winner(0);
    else if (players[1].score >= MAX_SCORE)
      declare_winner(1);
  }
}

void play_game() {
  gameover = 0;
  init_game();
  if (!attract)
  {
    players[0].human = 1;
    players[0].num = 1;
    if (num_players == 2)
    {
      players[1].human = 1;
      players[1].num = 2;
    }
  }
  while (!gameover) {
    play_round();
  }
}

void main() {
  joy_install (joy_static_stddrv);
  vrambuf_clear();
  set_vram_update(updbuf);
  seed = 0;
  while (1) {
    attract = 1;
    play_game();
    attract = 0;
    play_game();
  }
}
