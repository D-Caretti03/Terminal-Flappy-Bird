/** includes **/

#include <stdio.h>
#include <termios.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <time.h>
#include <string.h>
#include <sys/select.h>

/** defines **/

#define START_POS_X (Game.winW / 6)   // x start position for the player
#define START_POS_Y (Game.winH / 2)   // y start position for the player
#define GRAVITY 1                     // constant for gravity
#define SPACE_POWER -8                // power of the jump of the bird
#define PIPES 3                       // number of pipes
#define PIPE_POS_X 65                 // spacing between the pipes
#define GAP 12                        // gap between the pipe
#define PIPE_W 8                      // width of the pipe
#define PIPE_VEL 1                    // speed of the pipes
#define CLOUDS_VEL 3                  // speed of the clouds
#define CLOUDS 5                      // number of clouds
#define CLOUDS_POS_X 40               // spacing between the clouds
#define BIG_CLOUDS 3                  // number of big clouds
#define BIG_CLOUDS_VEL 2              // speed of big clouds
#define BIG_CLOUDS_POS_X 70           // spacing between big clouds
#define ABUF_INIT {NULL, 0}           // initialization of ab struct
#define DELAY 50000                   // delay for drawing 

enum keys{
  SPACE = 1000, // key 1000 for pressing space
  QUIT          // key 1001 for pressing q
};

/** structs **/

/**
 *  struct for 2 dimensional vector
 */
struct vec2{
  int x;  // x component
  int y;  // y component
};

/**
 *  struct for the player
 */
struct player{
  int is_dead;      // 1 if player is dead (not used yet)
  struct vec2 *pos; // position of the player
};

/**
 *  struct for the pipe
 */
struct pipe{
  int pipeH;        // height of the pipe
  struct vec2 *pos; // position of the pipe
};

struct cloud{
  struct vec2 *pos;
};

/**
 *  struct for the output buffer
 */
struct abuf{
  char *b;  // string to write
  int len;  // length of the string
};

/**
 *  struct for the game
 */
struct game{
  int is_running;                     // should the game continue?
  int key_press;                      // currently pressed key
  int winW;                           // width of the terminal
  int winH;                           // height of the terminal
  int score;                          // score of the player
  struct termios orig_termios;        // struct termios to save the original attributes of the terminal
  struct player *Player;              // the player
  struct pipe Pipe[PIPES];            // array containing the pipes
  struct cloud Cloud[CLOUDS];         // array containing the clouds
  struct cloud BigCloud[BIG_CLOUDS];  //array containing the big clouds
};

typedef struct game game_t;
game_t Game;

/** ASCII to draw the bird **/
/*
 * __()>
 * \__)
 *
 */
char *bird = { "\x1b[1m\x1b[93m__()>\n\b\b\b\b\b\\__)\x1b[39m\x1b[22m"};
size_t bird_len;

/** ASCII to draw the clouds **/
/*   __
 *  (__)_
 * (_____)
 */
char *cloud = { "\x1b[1m__\n\b\b\b(__)_\n\b\b\b\b\b\b(_____)\x1b[22m" };
size_t cloud_len;

/** ASCII to draw big clouds **/
/*   ____
 *  (____)
 * (___)__)_
 * (______)_)
 */
char *big_cloud = { "\x1b[1m____\n\b\b\b\b\b(____)\n\b\b\b\b\b\b\b(___)__)_\n\b\b\b\b\b\b\b\b\b(______)_)\x1b[22m" };
size_t big_cloud_len;

/**
 *  append a string to the buffer
 *
 *  ab: the buffer to append the string
 *  s: the string to append
 *  len: the length of the string
 */
void ab_append(struct abuf *ab, const char *s, int len){
  char *new = realloc(ab->b, ab->len + len);

  if (new == NULL) return;
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

/**
 *  free the memory allocated for the string in the buffer
 *
 *  ab: the buffer
 */
void ab_free(struct abuf *ab){
  free(ab->b);
}

/**
 *  clear the screen before drawing
 */
void clear_screen(){
  // clear screen
  write(STDOUT_FILENO, "\x1b[2J", 4);
  // reposition cursor
  write(STDOUT_FILENO, "\x1b[H", 3);
}

void free_memory(){
  free(Game.Player->pos);
  free(Game.Player);
  for(int i = 0; i < PIPES; ++i){
    free(Game.Pipe[i].pos);
  } 
}

/**
 *  function to handle errors
 *
 *  s: string to pass to perror
 */
void flappy_die(const char *s){
  clear_screen();

  //make cursor visible again
  write(STDOUT_FILENO, "\x1b[?25h", 6);

  free_memory();
  
  perror(s);

  exit(1);
}

/**
 *  disable raw mode when exiting the game
 */
void disable_raw_mode(){
  //reset the old terminal attributes
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &Game.orig_termios) == 1) flappy_die("tcsetattr");
  clear_screen();
  //make cursor visible again
  write(STDOUT_FILENO, "\x1b[?25h", 6);
}

/**
 *  enable raw mode inside the game
 */
void enable_raw_mode(){
  if (tcgetattr(STDIN_FILENO, &Game.orig_termios) == -1) flappy_die("tcgetattr");

  //do this function when exiting the program
  atexit(disable_raw_mode);

  //set the termios struct attributes to enter raw mode
  struct termios raw = Game.orig_termios;

  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 1;
  raw.c_cc[VTIME] = 0;

  //set attributes to the terminal
  if (tcsetattr(STDOUT_FILENO, TCSAFLUSH, &raw) == -1) flappy_die("tcsetattr");
}

/**
 *  function to handle the correct exit of the game
 */
void flappy_exit(){
  Game.is_running = 0;
  clear_screen();
  //make cursor visible again
  write(STDOUT_FILENO, "\x1b[?25h", 6);
}

/**
 *  read a character from the input, if timeout sends 0, to prevent blocking on input
 */
void flappy_read_char(){
  int nread;  //number of bytes read
  char c;     //character read

  // struct for timeout 0, on blocking
  struct timeval tv = {0, 0};

  // setting the stdin
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(STDIN_FILENO, &fds);

  if (select (STDIN_FILENO+1, &fds, NULL, NULL, &tv) > 0){
    nread = read(STDIN_FILENO, &c, 1);
  }

  if (c == ' ') Game.key_press = SPACE;
  else if (c == 'q') Game.key_press = QUIT;
  else if (nread == 0) Game.key_press = 0;
  else if (nread == -1) flappy_die("read");
  else Game.key_press = -1;
}

/**
 *  handles the input char of the player, determining what to do with the given char
 */
void flappy_input(){
  flappy_read_char();

  int c = Game.key_press;

  switch (c) {
  case QUIT:
    flappy_exit();
    break;
  case SPACE:
    break;
  case -1:
    break;
  }
}

/**
 *  update the player position, and check for world bounds
 *
 *  p: pointer to the player struct
 *  x: value x to add to the player's one
 *  y: value y to add to the player's one
 */
void flappy_update_player_pos(struct player* p, int x, int y){
  //update the position of the player
  p->pos->x += x;
  p->pos->y += y;

  //keep player inside screen bounds
  if (p->pos->y < 0) p->pos->y = 0;
  else if (p->pos->y > Game.winH) p->pos->y = Game.winH;
}

/**
 *  update function for the entire game: 
 *  apply gravity to the player;
 *  check the key for the jump;
 *  move the pipes to the left and wrap them around when they reach the end of the screen;
 *  check for collision with the pipes
 *  add 1 to the score when player passes through one pipe
 */
void flappy_update(){
  struct player *p = Game.Player;
  struct vec2 *p_pos = p->pos;
  int p_pos_x = p_pos->x;
  int p_pos_y = p_pos->y;

  //applying gravity
  flappy_update_player_pos(p, 0, GRAVITY);

  //apply jump if space was pressed this frame
  if (Game.key_press == SPACE)
    flappy_update_player_pos(p, 0, SPACE_POWER);

  //prevents scoring the same pipe multiple times
  static int scored[PIPES] = {0};
  for (int i = 0; i < PIPES; ++i){
    struct pipe *pipe = &Game.Pipe[i];
    int *pipe_x = &pipe->pos->x;
    int *pipe_h = &pipe->pipeH;

    *pipe_x -= PIPE_VEL;
    if (*pipe_x < 0){
      *pipe_h = (rand() % 30) + 5;
      *pipe_x = Game.winW;
    }

    //add 1 to the score if passing through a pipe
    if (p_pos_x > *pipe_x + PIPE_W && !scored[i]){
      Game.score++;
      scored[i] = 1;
    }

    //allow scoring again when pipe comes around next time
    if (p_pos_x < *pipe_x) scored[i] = 0;

    //check for collision with the pipes
    if (p_pos_x >= *pipe_x && p_pos_x <= *pipe_x + PIPE_W){
      if (p_pos_y <= *pipe_h || p_pos_y >= *pipe_h + GAP) Game.is_running = 0;
    }
  }
  
  // update the clouds
  for (int i = 0; i < CLOUDS; ++i) {
    struct cloud *c = &Game.Cloud[i];
    int *cloud_x = &c->pos->x;

    *cloud_x -= CLOUDS_VEL;

    if (*cloud_x < 0)
      *cloud_x = Game.winW;
  }

  // update big clouds
  for (int i = 0; i < BIG_CLOUDS; ++i){
    struct cloud *bc = &Game.BigCloud[i];
    int *cloud_x = &bc->pos->x;

    *cloud_x -= BIG_CLOUDS_VEL;

    if (*cloud_x < 0) *cloud_x = Game.winW;
  }

  //reset the key press
  Game.key_press = 0;
}

/**
 *  draw a pipe
 *
 *  ab: the buffer for the write
 *  idx: index of the pipe to draw
 */
void draw_pipe(struct abuf *ab, int idx){
  //values to help drawing the pipe
  int y = 0;
  int x = Game.Pipe[idx].pos->x;
  int w = PIPE_W;
  int h = Game.Pipe[idx].pipeH;

  //positioning the cursor on the right coordinates of the pipe
  char pos[16];
  int poslen = snprintf(pos, sizeof(pos), "\x1b[%d;%dH", ++y, ++x);
  ab_append(ab, pos, poslen);
  
  //draw the upper part of the pipe
  for (int i = 0; i < h; ++i){
    for (int j = 0; j < w; ++j){
      ab_append(ab, "H", 1);
    }
    poslen = snprintf(pos, sizeof(pos), "\x1b[%d;%dH", ++y, x);
    ab_append(ab, pos, poslen);
  }

  //position the cursor below the gap
  y+=GAP;
  poslen = snprintf(pos, sizeof(pos), "\x1b[%d;%dH", y, x);
  ab_append(ab, pos, poslen);

  //draw the bottom part of the pipe
  for (int i = 0; i < Game.winH - h - GAP - 1; ++i){
    for (int j = 0; j < w; ++j){
      ab_append(ab, "H", 1);
    }
    poslen = snprintf(pos, sizeof(pos), "\x1b[%d;%dH", ++y, x);
    ab_append(ab, pos, poslen);
  }
  
}

/**
 *  draw all the pipes in the game passing the index to draw_pipe
 *
 *  ab: buffer for the write
 */
void draw_pipes(struct abuf *ab){
  //set bold text and color green
  ab_append(ab, "\x1b[1m", 4);
  ab_append(ab, "\x1b[92m", 5);
  for (int i = 0; i < PIPES; ++i){
    draw_pipe(ab, i);
  }
  //reset text and color
  ab_append(ab, "\x1b[39m", 5);
  ab_append(ab, "\x1b[22m", 6);
}

/**
 * draw the clouds on the background
 *
 * ab: buffer for the write
 * idx: index of the cloud to draw
 */
void draw_cloud(struct abuf *ab, int idx){
  int x = Game.Cloud[idx].pos->x;
  int y = Game.Cloud[idx].pos->y;

  char pos[16];
  int plen = snprintf(pos, sizeof(pos), "\x1b[%d;%dH", y+1, x+1);
  ab_append(ab, pos, plen);
  ab_append(ab, cloud, cloud_len);
}

/**
 * draw the big clouds on the background
 *
 * ab: buffer for the write
 * idx: index of the cloud to draw
 */
void draw_big_clouds(struct abuf *ab, int idx){
  int x = Game.BigCloud[idx].pos->x;
  int y = Game.BigCloud[idx].pos->y;
  
  char pos[16];
  int plen = snprintf(pos, sizeof(pos), "\x1b[%d;%dH", y + 1, x + 1);
  ab_append(ab, pos, plen);
  ab_append(ab, big_cloud, big_cloud_len);
}

/**
 *  draw the backround of the game
 */
void draw_background(struct abuf *ab){
  for (int i = 0; i < BIG_CLOUDS; ++i){
    draw_big_clouds(ab, i);
  }
  for (int i = 0; i < CLOUDS; ++i){
    draw_cloud(ab, i);
  }
}

/**
 *  draw the entire scene of the game
 */
void flappy_draw(){
  //clear the screen
  clear_screen();

  //initialize the buffer for the write
  struct abuf ab = ABUF_INIT;

  //draw the background
  draw_background(&ab);

  //draw the pipes
  draw_pipes(&ab);

  //draw the bird
  //set the cursor on the position of the player
  char buf_pos[16];
  int wlen = snprintf(buf_pos, sizeof(buf_pos), "\x1b[%d;%dH", Game.Player->pos->y, Game.Player->pos->x);
  ab_append(&ab, buf_pos, wlen);
  ab_append(&ab, bird, bird_len);  

  //draw the score in the top left corner
  char score[32];
  int slen = snprintf(score, sizeof(score), "\x1b[1m\x1b[91m\x1b[2;4H%d\x1b[22m\x1b[39m", Game.score);
  ab_append(&ab, score, slen);

  //write all the contents of the buffer in the terminal
  write(STDOUT_FILENO, ab.b, ab.len);
  
  //free the string of the buffer
  ab_free(&ab);
}

/**
 *  main loop of the game
 */
void flappy_game_loop(){
  while (Game.is_running){
    flappy_input();
    usleep(DELAY);
    flappy_update();
    flappy_draw();
  }  
}

/**
 *  get the size of the terminal
 */ 
int getWindowSize(){
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) || ws.ws_col == 0) return -1;

  Game.winW = ws.ws_col;
  Game.winH = ws.ws_row;
  return 0;
}

/**
 *  initialize the game, set up the attributes of the struct and enable raw mode
 */
void init_game(){
  enable_raw_mode();
  Game.is_running = 1;
  Game.score = 0;

  Game.Player = malloc(sizeof(struct player));
  Game.Player->is_dead = 0;

  Game.Player->pos = malloc(sizeof(struct vec2));
  if (getWindowSize() == -1) flappy_die("getWindowSize");
  Game.Player->pos->x = START_POS_X;
  Game.Player->pos->y = START_POS_Y;

  for (int i = 0; i < PIPES; ++i){
    Game.Pipe[i].pos = malloc(sizeof(struct vec2));    
    Game.Pipe[i].pos->x = (i+1) * PIPE_POS_X;
    Game.Pipe[i].pos->y = 0; 
    Game.Pipe[i].pipeH = (rand() % 30) + 5;
  }

  for (int i = 0; i < CLOUDS; ++i) {
    Game.Cloud[i].pos = malloc(sizeof(struct vec2));
    Game.Cloud[i].pos->y = (rand() % 10) + 5;
    Game.Cloud[i].pos->x = (i+1) * CLOUDS_POS_X;
  }

  for (int i = 0; i < BIG_CLOUDS; ++i){
    Game.BigCloud[i].pos = malloc(sizeof(struct vec2));
    Game.BigCloud[i].pos->y = (rand() % 10) + 5;
    Game.BigCloud[i].pos->x = (i+1) * BIG_CLOUDS_POS_X;
  }

  bird_len = strlen(bird);
  cloud_len = strlen(cloud);
  big_cloud_len = strlen(big_cloud);
  
  write(STDOUT_FILENO, "\x1b[?25l", 6);
}

int main(void){
  srand(time(NULL));
  clear_screen();
  init_game();

  flappy_game_loop();

  free_memory();
}
