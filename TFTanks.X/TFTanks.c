/*
 *  File: TFTanks
 *  Morgan Cupp (mmc274), Stefen Pegels (sgp62), Maria Martucci (mlm423)
 *
 */
////////////////////////////////////
// clock AND protoThreads configure!
#include "config_1_3_2.h"
// threading library
#include "pt_cornell_1_3_2_python.h"
#include <math.h>
#include <stdfix.h>
#include <time.h>

////////////////////////////////////
// graphics libraries
// SPI channel 1 connections to TFT
#include "tft_master.h"
#include "tft_gfx.h"
#include <stdfix.h>
#define fix _Accum

//== Timer 2 interrupt handler ===========================================
#define Fs 44000
#define WAIT {}

#define RAD(angle) (double)(((angle)*M_PI)/180) // convert degrees to radians

// screen dimensions
#define TFT_HEIGHT 239
#define TFT_WIDTH  319

// tank dimensions
#define TANK_WIDTH 20
#define TANK_HEIGHT 10

#define STEP_SIZE 5 // step size for tank movements

unsigned int turn = 1; // which tank's turn it is (1 or 2)
volatile int fire = 0; // whether or not a projectile is in the air (0 or 1)
volatile fix power = 4.5; // scalar for projectile initial velocity (1 to 9)
volatile int land[320]; // array holding y positions of each land pixel
int land_height = 80; // initial default land height
const fix g = (fix) 0.1; // gravity constant
volatile int steps = 5; // number of steps per turn per player (5 to 0)
volatile int new_game = 0; // whether or not the new game button was pressed
volatile int end_screen =0; // whether or not we are on the end screen
volatile int explosion = 0; // turns to 1 when a projectile hits the ground/tank
volatile int start_screen = 1; // 1 until Start Game button is pressed
volatile int start_game = 0; // used to check state of game at start

char tft_str_buffer[60];
char buffer[60];


// structure to represent a tank
typedef struct tftank
{
    int x; // represents middle x position of tank
    int y; // represents bottom y position of tank
    int angle; // cannon angle (invariant 0..180)
    int width; // tank bottom width
    int height; // height of tank at midpoint
    int vertex_x; // x position of top of tank/vertex of cannon
    int vertex_y; // y position of top of tank/vertex of cannon
    fix shell_x; // x position of tank's projectile
    fix shell_y; // y position of tank's projectile
    fix shell_vx; // velocity of projectile in x direction
    fix shell_vy; // velocity of projectile in y direction
    int health; // tank health, checked for endgame conditions
} tftank;


tftank t1, t2;


// initialize everything for a new game
void restart_game() {
    // reset global variables
    new_game = 0;
    turn = 1;
    fire = 0;
    steps = 5;
    end_screen = 0;
    explosion = 0;
    start_game = 0;
    start_screen = 0;
   
    tft_fillScreen(ILI9340_BLACK); // redraw display
    tft_draw_level(); // initializes level

    // re-initialize tanks
    init_tank(&t1, 40, TANK_WIDTH, TANK_HEIGHT, 45, ILI9340_WHITE, 3);
    init_tank(&t2, 280, TANK_WIDTH, TANK_HEIGHT, 135, ILI9340_WHITE, 3);
    
    // correct values on python interface
    printf("$01 %d\r", steps);
    printf("$02 %d\r", t1.health);
    printf("$03 %d\r", t2.health);
    printf("$04 %d\r", turn);
}


// initialize tanks at start of game
void init_tank(struct tftank *t, int x, int w, int h, int ang, unsigned short color, int health){
    // set up initial tank attribute values
    t->x = x;
    t->y = land[x]-1; // y position expressed relative to land position at midpoint
    t->width = w;
    t->height = h;
    t->angle = ang;
    t->vertex_x = x;
    t->vertex_y = t->y-(t->height);
    t->shell_x = (fix) t->x+15*cos(RAD(ang)); // position set based on tank cannon's rotation
    t->shell_y = (fix) t->y-h - 15*sin(RAD(ang));
    t->shell_vx = 0;
    t->shell_vy = 0;
    t->health = health;
    tft_fillTriangle(x-w/2,t->y, x, t->y-h, x+w/2, t->y, color); // draw tank body
    tft_drawLine(t->x, t->y-h, (int) t->x+15*cos(RAD(ang)), (int) t->y-h - 15*sin(RAD(ang)), color);// draw cannon
}


// move tank cannon to specified angle
void aim_cannon(struct tftank *t, int new_ang){
    fix midx =  (fix) t->vertex_x+7.5*cos(RAD(new_ang)); // check midpoint of cannon for interference with terrain
    fix midy =  (fix) t->vertex_y-7.5*sin(RAD(new_ang));
    if ( ((int) midy) < land[(int) midx]) { // when the cannon tip is not inside the land, redraw it with the new angle
        tft_drawLine(t->vertex_x, t->vertex_y, (int) t->vertex_x+15*cos(RAD(t->angle)), (int) t->vertex_y - 15*sin(RAD(t->angle)), ILI9340_BLACK);
        tft_drawLine(t->vertex_x, t->vertex_y, (int) t->vertex_x+15*cos(RAD(new_ang)), (int) t->vertex_y - 15*sin(RAD(new_ang)), ILI9340_WHITE);
        t->angle = new_ang;
        t->shell_x =  (fix) t->vertex_x+15*cos(RAD(t->angle)); // put cannon projectile at the end of the new cannon
        t->shell_y = (fix) t->vertex_y-15*sin(RAD(t->angle));
    }
}


// move tank a step either left or right
void move_step(struct tftank *t, int direction){
    tft_fillTriangle((t->x)-((t->width)/2),t->y, t->x, t->y-(t->height), (t->x)+((t->width)/2), t->y, ILI9340_BLACK); // un-draws previous tank position
    tft_drawLine(t->vertex_x, t->vertex_y, (int) t->vertex_x+15*cos(RAD(t->angle)), (int) t->vertex_y - 15*sin(RAD(t->angle)), ILI9340_BLACK);

    // redraw land around moving tank
    int i;
    for (i = (int) (t->vertex_x-TANK_WIDTH/2); i < (int) (t->vertex_x+TANK_WIDTH/2); i++){
        tft_drawFastVLine(i, land[i], a_c(land[i]), ILI9340_BLUE);
    }
    
    // moving by half a tank width, 10
    int new_x = (direction == 1) ? t->x - STEP_SIZE : t->x + STEP_SIZE;

    // left tank is moving and not off screen and not passing right tank
    if ((turn == 1) && (new_x > TANK_WIDTH/2) && (new_x < t2.x - TANK_WIDTH)) {
        t->x = new_x;
        t->vertex_x = new_x;
        int new_y = land[new_x]-1;
        t->vertex_y = new_y-(t->height);
        t->y = new_y;
    }
    // right tank is moving and not off screen and not passing left tank
    if ((turn == 2) && (new_x < TFT_WIDTH-TANK_WIDTH/2) && (new_x > t1.x + TANK_WIDTH)) {
        t->x = new_x;
        t->vertex_x = new_x;
        int new_y = land[new_x]-1;
        t->vertex_y = new_y-(t->height);
        t->y = new_y;
    }
    
    // redraw once new attributes have been set
    tft_fillTriangle((t->x)-((t->width)/2),t->y, t->x, t->y-(t->height), (t->x)+((t->width)/2), t->y, ILI9340_WHITE);
    tft_drawLine(t->vertex_x, t->vertex_y, (int) t->vertex_x+15*cos(RAD(t->angle)), (int) t->vertex_y - 15*sin(RAD(t->angle)), ILI9340_WHITE);
    t->shell_x = (fix) t->vertex_x+15*cos(RAD(t->angle));
    t->shell_y = (fix) t->vertex_y-15*sin(RAD(t->angle));
}


// detect if a shell hits a tank
// return 0 if no hit, 1 if t1 is hit, 2 if t2 is hit
int collision() {
    static tftank *attacker;
    // determine which shell to check
    if (turn == 1) attacker = &t1;
    else attacker = &t2;
    
    // get coordinates of shell
    fix x = attacker->shell_x;
    fix y = attacker->shell_y;

    // check if t1 is hit
    if (((t1.x - TANK_WIDTH/2) <= x) && (x <= (t1.x + TANK_WIDTH/2)) && 
        (t1.y - TANK_HEIGHT + 1<= y) && (y <= t1.y)) {
        return 1;
    }
    // check if t2 is hit
    else if (((t2.x - TANK_WIDTH/2) <= x) && (x <= (t2.x + TANK_WIDTH/2)) && 
        (t2.y - TANK_HEIGHT + 1 <= y) && (y <= t2.y)) {
        return 2;
    }
    else { // no hit
        return 0;
    }
}


// display a line on the screen
void printLine2(int text_size, int cursor_pos, int v_pos, char* print_buffer, short text_color, short back_color){
    // line number 0 to 31 
    /// !!! assumes tft_setRotation(0);
    // print_buffer is the string to print
    // erase the pixels
    tft_fillRoundRect(cursor_pos, v_pos, 239, 20, 1, back_color); // x,y,w,h,radius,color
    tft_setTextColor(text_color); 
    tft_setCursor(cursor_pos, v_pos);
    tft_setTextSize(text_size);
    tft_writeString(print_buffer);
}


// modifies land in the event of a projectile collision
void destroy_land(int xpos){
    int startx = xpos - 10; // 10 pixels to the left and right of xpos (impact point)
    int endx = xpos+10;
    if (startx < 0) startx = 0; // check boundary conditions at edge of display
    if (endx > 319) endx = 319;
    int i;
    int decrement; // amount the land will be decreased by
    for(i = startx; i <= endx; i++){
        tft_drawFastVLine(i, 0, TFT_HEIGHT, ILI9340_BLACK);
        decrement = (int) ((power/9)*30); // destruction proportional to projectile power
        land[i] += decrement; // lower all land in destruction area
        if (land[i] >= TFT_HEIGHT) land[i] = TFT_HEIGHT; // check boundary conditions at bottom of display
        tft_drawFastVLine(i, land[i], a_c(land[i]), ILI9340_BLUE);
    }
}


// redraw tanks on display
void redraw_tanks(){
    //t1
    tft_fillTriangle((t1.x)-((t1.width)/2),t1.y, t1.x, t1.y-(t1.height), (t1.x)+((t1.width)/2), t1.y, ILI9340_BLACK);
    tft_drawLine(t1.vertex_x, t1.vertex_y, (int) t1.vertex_x+15*cos(RAD(t1.angle)), (int) t1.vertex_y - 15*sin(RAD(t1.angle)), ILI9340_BLACK);
    init_tank(&t1, t1.x, t1.width, t1.height, t1.angle, ILI9340_WHITE, t1.health);

    //t2
    tft_fillTriangle((t2.x)-((t2.width)/2),t2.y, t2.x, t2.y-(t2.height), (t2.x)+((t2.width)/2), t2.y, ILI9340_BLACK);
    tft_drawLine(t2.vertex_x, t2.vertex_y, (int) t2.vertex_x+15*cos(RAD(t2.angle)), (int) t2.vertex_y - 15*sin(RAD(t2.angle)), ILI9340_BLACK);
    init_tank(&t2, t2.x, t2.width, t2.height, t2.angle, ILI9340_WHITE, t2.health);
}


// initializes a new game
void init_game(){
    tft_fillScreen(ILI9340_BLACK);
    tft_draw_level(); // initializes level
    
    // create tanks with default parameters
    init_tank(&t1, 40, TANK_WIDTH, TANK_HEIGHT, 45, ILI9340_WHITE, 3);
    init_tank(&t2, 280, TANK_WIDTH, TANK_HEIGHT, 135, ILI9340_WHITE, 3);
}


// set up thread that runs at animation rate
// update velocity and position incrementally
// x velocity will not change
// update y velocity according to gravity
// make one large animation thread
// exactly what happens in that thread changes depending on game conditions
static PT_THREAD (protothread_animate(struct pt *pt))
{
  PT_BEGIN(pt);
  int begin_time;
  static float increment = 1;
  
  while(1) {
   
    begin_time = PT_GET_TIME(); // start timing here
    static tftank *t;
    
    // initialization screen with no threads running
    if(start_screen == 1){
        tft_fillScreen(ILI9340_BLACK);
        sprintf(buffer, "TFTANKS!");
        printLine2(5, 20, 100, buffer, ILI9340_RED, ILI9340_BLACK);
        sprintf(buffer, "Press START to Begin");
        printLine2(2, 20, 150, buffer, ILI9340_WHITE, ILI9340_BLACK);

        // wait for user to start new game
        PT_YIELD_UNTIL(pt, start_game == 1);
        
        start_screen = 0;
        srand(PT_GET_TIME());
        init_game();
        begin_time = PT_GET_TIME();
    }
    
    // fire button has been pressed on the python interface
    if (fire == 1) { 
        // redraw cannons in place every frame
        tft_drawLine(t1.vertex_x, t1.vertex_y, (int) t1.vertex_x+15*cos(RAD(t1.angle)), (int) t1.vertex_y - 15*sin(RAD(t1.angle)), ILI9340_WHITE);
        tft_drawLine(t2.vertex_x, t2.vertex_y, (int) t2.vertex_x+15*cos(RAD(t2.angle)), (int) t2.vertex_y - 15*sin(RAD(t2.angle)), ILI9340_WHITE);

        // check which tank we are firing from
        if (turn == 1) {
            t = &t1;
        } else {
            t = &t2;
        }
        if (t->shell_y > 0) tft_fillCircle((int)t->shell_x, (int)t->shell_y, 2,ILI9340_BLACK); // animate projectile when on screen

        // projectile motion physics- update shell location and velocity
        fix x = (fix) t->shell_x;
        fix y = (fix) t->shell_y;

        t->shell_vy = t->shell_vy + g;

        x = x + t->shell_vx;
        y = y + t->shell_vy;
        t->shell_x = x;
        t->shell_y = y;
        
        // don't draw shell if its y-value is negative
        if (t->shell_y > 0 && t->shell_y < land[(int)t->shell_x]) {
           tft_fillCircle((int)t->shell_x, (int) t->shell_y, 2, ILI9340_WHITE); 
        }
  
        // check for hit
        int hit = collision();
        
        // if tank or ground is hit, transition to explosion condition
        if (hit == 1) {
            fire = 0;
            explosion = 1;
            t1.health -= 1; // t1 hit and loses health
            printf("$02 %d\r", t1.health);

        }
        else if (hit == 2) {
            fire = 0;
            explosion = 1;
            t2.health -= 1; // t2 hit and loses health
            printf("$03 %d\r", t2.health);
            
        } // if projectile x position is off TFT, stop drawing it
        else if (t->shell_x < 5 || t->shell_x > (TFT_WIDTH-5)) {
            tft_fillCircle((int)t->shell_x, (int) t->shell_y, 2, ILI9340_BLACK);
            fire = 0;
            turn = 2 - turn + 1; // change turn
            printf("$04 %d\r", turn);
            t->shell_x = (fix) t->vertex_x+15*cos(RAD(t->angle)); // redraw shell at tank cannon's tip
            t->shell_y = (fix) t->vertex_y-15*sin(RAD(t->angle));
        } // if projectile hits land, destroy land
        else if (land[(int)t->shell_x] <= t->shell_y) {
            tft_fillCircle((int)t->shell_x, (int) t->shell_y, 2, ILI9340_BLUE);
            destroy_land((int)t->shell_x); // destroy land
            redraw_tanks(); // redraw tanks so they follow the land changes
            fire = 0;
            turn = 2 - turn + 1; // change turn
            printf("$04 %d\r", turn);
            t->shell_x = (fix) t->vertex_x+15*cos(RAD(t->angle));
            t->shell_y = (fix) t->vertex_y-15*sin(RAD(t->angle));
        }
    }
    
    // explosion animation
    if(explosion == 1){
        // draw red expanding circle
        tft_fillCircle((int)t->shell_x, (int) t->shell_y, (short)increment, ILI9340_RED);
        increment = increment + .5;

        // end explosion animation
        if(increment == 20){
            //erase circle
            tft_fillCircle((int)t->shell_x, (int) t->shell_y, (short)increment, ILI9340_BLACK);

            // redraw land
            int i;
            for (i = t->shell_x - 21; i < t->shell_x + 22; i++){
                tft_drawFastVLine(i, land[i], a_c(land[i]), ILI9340_BLUE);
            }
            // redraw both tanks since explosion could cover both tanks
            redraw_tanks();
            explosion=0;
            increment =0;
            t->shell_x = (fix) t->vertex_x+15*cos(RAD(t->angle));
            t->shell_y = (fix) t->vertex_y-15*sin(RAD(t->angle));
            turn = 2 - turn + 1; // change turn
            printf("$04 %d\r", turn);
        }
    }
    
    // game over scenario: t1 health is 0
    if(t1.health == 0 && explosion==0){
        tft_fillScreen(ILI9340_BLACK);
        sprintf(buffer, "GAME OVER");
        printLine2(5, 20, 100, buffer, ILI9340_RED, ILI9340_BLACK);
        sprintf(buffer, "Player 2 wins!");
        printLine2(3, 20, 150, buffer, ILI9340_WHITE, ILI9340_BLACK);
        end_screen = 1;

        // wait for user to start new game
        PT_YIELD_UNTIL(pt, new_game == 1);
        begin_time = PT_GET_TIME();
        restart_game();
    }

    // game over scenario: t2 health is 0
    if(t2.health == 0 && explosion==0){
        tft_fillScreen(ILI9340_BLACK);
        sprintf(buffer, "GAME OVER");
        printLine2(5, 20, 100, buffer, ILI9340_RED, ILI9340_BLACK);
        sprintf(buffer, "Player 1 wins!");
        printLine2(3, 20, 150, buffer, ILI9340_WHITE, ILI9340_BLACK);
        end_screen = 1;

        // wait for user to start new game
        PT_YIELD_UNTIL(pt, new_game == 1);
        begin_time = PT_GET_TIME();
        restart_game();
    }

	// 30 FPS => frame time of 33 msec
    // yield every 33 msec for 30 FPS
	PT_YIELD_TIME_msec(33 - (PT_GET_TIME() - begin_time)) ;
  }
   PT_END(pt);
}


// === outputs from python handler =============================================
// signals from the python handler thread to other threads
// These will be used with the prototreads PT_YIELD_UNTIL(pt, condition);
// to act as semaphores to the processing threads
char new_string = 0;
char new_button = 0;
char new_toggle = 0;
char new_slider = 0;
char new_list = 0 ;
char new_radio = 0 ;
// identifiers and values of controls
// current button
char button_id, button_value ;
// current toggle switch/ check box
char toggle_id, toggle_value ;
// current radio-group ID and button-member ID
char radio_group_id, radio_member_id ;
// current slider
int slider_id;
float slider_value ; // value could be large
// current listbox
int list_id, list_value ; 
// current string
char receive_string[64];


// === string input thread =====================================================
// process text from python
static PT_THREAD (protothread_python_string(struct pt *pt))
{
    PT_BEGIN(pt);
    static int dds_freq;
    // 
    while(1){
        // wait for a new string from Python
        PT_YIELD_UNTIL(pt, new_string==1);
        new_string = 0;
        // parse frequency command
        if (receive_string[0] == 'h'){
            // dds frequency
            printf("f number ...sets DDS freq integer range 0-10000\r");
            // DAC amplitude
            printf("v float ...sets DAC volt, if DDS is off range 0.0-2.0\r");
            // help
            printf("help ...list the avaliable commands\r");
            // default string
            printf("Any other string is just echoed back\r");
        }
        else {
            printf("received>%s", receive_string);        
        }
    }  
    PT_END(pt);  
}


// === Python serial thread ====================================================
// you should not need to change this thread UNLESS you add new control types
static PT_THREAD (protothread_serial(struct pt *pt))
{
    PT_BEGIN(pt);
    static char junk;
    //   
    //
    while(1){
        // There is no YIELD in this loop because there are
        // YIELDS in the spawned threads that determine the 
        // execution rate while WAITING for machine input
        // =============================================
        // NOTE!! -- to use serial spawned functions
        // you MUST edit config_1_3_2 to
        // (1) uncomment the line -- #define use_uart_serial
        // (2) SET the baud rate to match the PC terminal
        // =============================================
        
        // now wait for machine input from python
        // Terminate on the usual <enter key>
        PT_terminate_char = '\r' ; 
        PT_terminate_count = 0 ; 
        PT_terminate_time = 0 ;
        // note that there will NO visual feedback using the following function
        PT_SPAWN(pt, &pt_input, PT_GetMachineBuffer(&pt_input) );
        
        // Parse the string from Python
        // There can be toggle switch, button, slider, and string events
         
        // toggle switch
        if (PT_term_buffer[0]=='t'){
            // signal the button thread
            new_toggle = 1;
            // subtracting '0' converts ascii to binary for 1 character
            toggle_id = (PT_term_buffer[1] - '0')*10 + (PT_term_buffer[2] - '0');
            toggle_value = PT_term_buffer[3] - '0';
        }
        
        // pushbutton
        if (PT_term_buffer[0]=='b'){
            // signal the button thread
            new_button = 1;
            // subtracting '0' converts ascii to binary for 1 character
            button_id = (PT_term_buffer[1] - '0')*10 + (PT_term_buffer[2] - '0');
            button_value = PT_term_buffer[3] - '0';
        }
        
        // slider
        if (PT_term_buffer[0]=='s'){
            sscanf(PT_term_buffer, "%c %d %f", &junk, &slider_id, &slider_value);
            new_slider = 1;
        }
        
        // listbox
        if (PT_term_buffer[0]=='l'){
            new_list = 1;
            list_id = PT_term_buffer[2] - '0' ;
            list_value = PT_term_buffer[3] - '0';
        }
        
        // radio group
        if (PT_term_buffer[0]=='r'){
            new_radio = 1;
            radio_group_id = PT_term_buffer[2] - '0' ;
            radio_member_id = PT_term_buffer[3] - '0';
        }
        
        // string from python input line
        if (PT_term_buffer[0]=='$'){
            // signal parsing thread
            new_string = 1;
            // output to thread which parses the string
            // while striping off the '$'
            strcpy(receive_string, PT_term_buffer+1);
        }                                  
    }  
    PT_END(pt);  
}


// === Buttons thread ==========================================================
static PT_THREAD (protothread_buttons(struct pt *pt))
{
    PT_BEGIN(pt);

    while(1){
        // yield until new button, when we aren't processing a projectile or collision
        PT_YIELD_UNTIL(pt, new_button==1 && fire == 0 && explosion==0);
        // clear flag
        new_button = 0;
        
        // move left
        if (button_id==1 && button_value==1 && end_screen==0 && start_screen==0){
            // only move tank whose turn it is
            if(steps > 0){
                if(turn == 1){
                    // setting the direction 1 == left
                    move_step(&t1,1);
                }else{
                    move_step(&t2,1);
                } 
                steps -= 1;
                printf("$01 %d\r", steps);
            }            
        }

        // move right
        if (button_id==2 && button_value==1 && end_screen==0 && start_screen==0){
            if(steps > 0){
                if(turn == 1){
                    //setting the direction 2 == right
                    move_step(&t1,2);
                }else{
                    move_step(&t2,2);
                }
                steps -= 1;
                printf("$01 %d\r", steps);
            }
        }

        // fire a projectile
        if (button_id==4 && button_value==1 && end_screen==0 && start_screen==0){
            fire = 1; // set flag
            steps = 5;
          
            printf("$01 %d\r", steps);

            // initialize correct tank's projectile velocity
            if(turn ==1){
                t1.shell_vx = (fix) power*cos(RAD(t1.angle));
                t1.shell_vy = (fix) -power*sin(RAD(t1.angle));
            }
            else if(turn ==2){
                t2.shell_vx = (fix) power*cos(RAD(t2.angle));
                t2.shell_vy = (fix) -power*sin(RAD(t2.angle));
            }
        }

        // start a new game, only once we are at end screen
        if (button_id==3 && button_value==1 && end_screen==1 ){
            new_game = 1;
        }
        if (button_id==5 && button_value==1 && start_screen==1){
            start_game = 1;
        }
    }  
    PT_END(pt);  
}


// === Sliders Threads ==========================================================
static PT_THREAD (protothread_sliders(struct pt *pt))
{
    PT_BEGIN(pt);
    while(1){
        PT_YIELD_UNTIL(pt, new_slider==1 && fire == 0 && end_screen==0 && explosion==0);
        // clear flag
        new_slider = 0;
       
        int new_ang;
        if (slider_id == 1){ // change correct tank cannon's angle
            new_ang = (int)slider_value;
            if(turn ==1){
                aim_cannon(&t1,new_ang);
            }else{
                aim_cannon(&t2,new_ang);
            }
        }
        else if (slider_id == 2){ // change power (in range 0 to 9))
            power = (fix)(9*((fix)slider_value))/((fix)100);
        }
    }  
    PT_END(pt);  
}


// generate random land
void rand_land_create(){
    // start at left edge of TFT
    int x = 0;
    int prev_x = 0;
    float prev_y = (float) a_c(land_height);
    float y = (float) a_c(land_height);
    float num;
    float m;

    // randomly generate line segments
    while (x < TFT_WIDTH) {
        x += 30; // increase x coordinate by fixed amount

        // generate random y coordinate
        num = ((float) rand()) / RAND_MAX;
        y = (float) (TFT_HEIGHT-num*200);
        
        // compute slope of this line segment
        m = (y-prev_y)/30;

        // fill in land array with values
        int i;
        for (i = prev_x; i < x; i++) {
            land[i] = (int) (m*(i-x)+y);
        }
        
        prev_x = x;
        prev_y = y;
    }
}


// function for drawing the game level's land
void tft_draw_level(){
    int i;
    rand_land_create();
    for(i = 0; i < 320; i++){
        tft_drawFastVLine(i, land[i], a_c(land[i]), ILI9340_BLUE); 
    }
}


// converts y coordinates into bottom-left orientation, with origin at bottom
// left instead of top left
int a_c(int y_value){
    return TFT_HEIGHT - y_value;
}


// === Main  ====================================================================================================================================
void main(void) {
    // LED for debugging
    mPORTASetPinsDigitalOut(BIT_0); // set port as output
    // LED on
    mPORTAClearBits(BIT_0); 
    
    // === setup system wide interrupts  ========
    INTEnableSystemMultiVectoredInt();
  
    // === TFT setup ============================
    // init the display in main since more than one thread uses it.
    // NOTE that this init assumes SPI channel 1 connections
    PT_setup();
    tft_init_hw();
    tft_begin();
    tft_fillScreen(ILI9340_BLACK);
    //240x320 vertical display
    tft_setRotation(1); // Use tft_setRotation(1) for 320x240

    // === config threads ========================  
    // === identify the threads to the scheduler =====
    // add the thread function pointers to be scheduled
    // --- Two parameters: function_name and rate. ---
    // rate=0 fastest, rate=1 half, rate=2 quarter, rate=3 eighth, rate=4 sixteenth,
    // rate=5 or greater DISABLE thread!

    pt_add(protothread_buttons, 0);
    pt_add(protothread_serial, 0);
    pt_add(protothread_python_string, 0);
    pt_add(protothread_sliders,0);
    pt_add(protothread_animate, 0);

    // === initalize the scheduler ====================
    PT_INIT(&pt_sched) ;
    // >>> CHOOSE the scheduler method: <<<
    // (1)
    // SCHED_ROUND_ROBIN just cycles thru all defined threads  
    // NOTE the controller must run in SCHED_ROUND_ROBIN mode
    // ALSO note that the scheduler is modified to cpy a char
    // from uart1 to uart2 for the controller

    pt_sched_method = SCHED_ROUND_ROBIN ;

    // === scheduler thread =======================
    // scheduler never exits
    PT_SCHEDULE(protothread_sched(&pt_sched));
    // ============================================
  
}
