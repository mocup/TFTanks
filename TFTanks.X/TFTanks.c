/*
 * File:        TFTanks
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

#define RAD(angle) (double)(((angle)*M_PI)/180)

#define TFT_HEIGHT 239
#define TFT_WIDTH  319

#define TANK_WIDTH 20
#define TANK_HEIGHT 10
#define STEP_SIZE 5
//

unsigned int turn = 1; //Which tank's turn it is (1 or 2)

volatile int fire = 0; //Whether or not a projectile is in the air (0 or 1)

volatile fix power = 4.5; //Scalar for projectile initial velocity (1 to 9)

volatile int land[320]; //Array holding y positions of each land pixel
int land_height = 80; //Initial default land height

const fix g = (fix) 0.1; //Grav constant

volatile int steps = 5; //Number of steps per turn per player (5 to 0)

volatile int new_game = 0; //Whether or not the new game button was pressed

volatile int end_screen =0;//Whether or not we are on the end screen

volatile int explosion = 0;//Turns to 1 when a projectile hits the ground/tank

volatile int start_screen = 1;//1 Until Start Game button is pressed

volatile int start_game = 0;//Used to check state of game at start

char tft_str_buffer[60];
char buffer[60];

typedef struct tftank //Structure to represent a tank
{
    int x; // Represents middle x position of tank
    int y; //Represent bottom y middle position of tank
    int angle; // Invariant 0..180
    int width; //Tank bottom width
    int height; //Height of tank at midpoint
    int vertex_x; //X position of top of tank/vertex of cannon
    int vertex_y; //Y position of top of tank/vertex of cannon
    fix shell_x; //X position of tank's projectile
    fix shell_y; //Y position of tank's projectile
    fix shell_vx; //Velocity of projectile in x direction
    fix shell_vy; //Velocity of projectile in y direction
    int health; //Tank health, checked for endgame conditions
    
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
    
    
   
    // redraw display
    tft_fillScreen(ILI9340_BLACK);
    //srand(PT_GET_TIME());
    tft_draw_level(); //Initializes level

    // re-initialize tanks
    init_tank(&t1, 40, TANK_WIDTH, TANK_HEIGHT, 45, ILI9340_WHITE, 3);
    init_tank(&t2, 280, TANK_WIDTH, TANK_HEIGHT, 135, ILI9340_WHITE, 3);
    
    // correct values on python interface
    printf("$01 %d\r", steps);
    printf("$02 %d\r", t1.health);
    printf("$03 %d\r", t2.health);
    printf("$04 %d\r", turn);
}



void init_tank(struct tftank *t, int x, int w, int h, int ang, unsigned short color, int health){
    //Sets up initial tank attribute values
    t->x = x;
    t->y = land[x]-1; //Y position expressed relative to land position at midpoint
    t->width = w;
    t->height = h;
    t->angle = ang;
    t->vertex_x = x;
    t->vertex_y = t->y-(t->height);
    t->shell_x = (fix) t->x+15*cos(RAD(ang)); //Position set based on tank cannon's rotation
    t->shell_y = (fix) t->y-h - 15*sin(RAD(ang));
    t->shell_vx = 0;
    t->shell_vy = 0;
    t->health = health;
    tft_fillTriangle(x-w/2,t->y, x, t->y-h, x+w/2, t->y, color);//Tank body
    tft_drawLine(t->x, t->y-h, (int) t->x+15*cos(RAD(ang)), (int) t->y-h - 15*sin(RAD(ang)), color);//Cannon
    
}

void aim_cannon(struct tftank *t, int new_ang){
    // if cannon tip is above land, redraw black
    fix midx =  (fix) t->vertex_x+7.5*cos(RAD(new_ang)); //Check midpoint of cannon for interference with terrain
    fix midy =  (fix) t->vertex_y-7.5*sin(RAD(new_ang));
    //if (((int) t->vertex_y - 15*sin(RAD(new_ang))) < land[(int) (t->vertex_x + 15*cos(RAD(new_ang)))]) {
    if ( ((int) midy) < land[(int) midx]) { //When the cannon tip is not inside the land, redraw it with the new angle
        tft_drawLine(t->vertex_x, t->vertex_y, (int) t->vertex_x+15*cos(RAD(t->angle)), (int) t->vertex_y - 15*sin(RAD(t->angle)), ILI9340_BLACK);
        //} else {
        //    tft_drawLine(t->vertex_x, t->vertex_y, (int) t->vertex_x+15*cos(RAD(t->angle)), (int) t->vertex_y - 15*sin(RAD(t->angle)), ILI9340_BLUE);
        //}
        tft_drawLine(t->vertex_x, t->vertex_y, (int) t->vertex_x+15*cos(RAD(new_ang)), (int) t->vertex_y - 15*sin(RAD(new_ang)), ILI9340_WHITE);
        t->angle = new_ang;
        t->shell_x =  (fix) t->vertex_x+15*cos(RAD(t->angle));//Put cannon projectile at the end of the new cannon
        t->shell_y = (fix) t->vertex_y-15*sin(RAD(t->angle));
    }
    
}



void move_step(struct tftank *t, int direction){
    //Function to move the tank by a step
    tft_fillTriangle((t->x)-((t->width)/2),t->y, t->x, t->y-(t->height), (t->x)+((t->width)/2), t->y, ILI9340_BLACK); //Un-draws previous tank position
    tft_drawLine(t->vertex_x, t->vertex_y, (int) t->vertex_x+15*cos(RAD(t->angle)), (int) t->vertex_y - 15*sin(RAD(t->angle)), ILI9340_BLACK);
    //moving  by half a tank width, 10
    
    // redraw land around moving tank
    int i;
    for (i = (int) (t->vertex_x-TANK_WIDTH/2); i < (int) (t->vertex_x+TANK_WIDTH/2); i++){
        tft_drawFastVLine(i, land[i], a_c(land[i]), ILI9340_BLUE);
    }
    
    int new_x = (direction == 1)? t->x - STEP_SIZE : t->x + STEP_SIZE;
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
    
    //Redraw once new attributes have been set
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

void printLine(int line_number, char* print_buffer, short text_color, short back_color){
    // line number 0 to 31 
    /// !!! assumes tft_setRotation(0);
    // print_buffer is the string to print
    int v_pos;
    v_pos = line_number * 10 ;
    // erase the pixels
    tft_fillRoundRect(0, v_pos, 239, 8, 1, back_color);// x,y,w,h,radius,color
    tft_setTextColor(text_color); 
    tft_setCursor(0, v_pos);
    tft_setTextSize(1);
    tft_writeString(print_buffer);
}


void printLine2(int text_size, int cursor_pos, int v_pos, char* print_buffer, short text_color, short back_color){
    // line number 0 to 31 
    /// !!! assumes tft_setRotation(0);
    // print_buffer is the string to print
    // erase the pixels
    tft_fillRoundRect(cursor_pos, v_pos, 239, 20, 1, back_color);// x,y,w,h,radius,color
    tft_setTextColor(text_color); 
    tft_setCursor(cursor_pos, v_pos);
    tft_setTextSize(text_size);
    tft_writeString(print_buffer);
}

void destroy_land(int xpos){
    //Modifies land in the event of a projectile collision
    int startx = xpos - 10;//10 pixels to the left and right of xpos (impact point)
    int endx = xpos+10;
    if (startx < 0) startx = 0;
    if (endx > 319) endx = 319;
    int i;
    int decrement;//How much the land will be decreased by
    for(i = startx; i <= endx; i++){
        //tft_drawFastVLine(i, land[i], a_c(land[i]), ILI9340_BLACK);
        //Draws over the land in black and redraws new shorter VLines, also setting land[i] positions to new values
        tft_drawFastVLine(i, 0, TFT_HEIGHT, ILI9340_BLACK);
        decrement = (int) ((power/9)*30);
        land[i] += decrement;
        if (land[i] >= TFT_HEIGHT) land[i] = TFT_HEIGHT;
        tft_drawFastVLine(i, land[i], a_c(land[i]), ILI9340_BLUE);
    }
}

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

void init_game(){
    tft_fillScreen(ILI9340_BLACK);
    tft_draw_level(); //Initializes level
    
    //Create tanks with default parameters
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
  // maybe put initialization here

  static float increment = 1;
  
  while(1) {
   
    begin_time = PT_GET_TIME(); //start timing here
    static tftank *t;
    
    if(start_screen == 1){ //Initialization screen with no threads running
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
    

    if (fire == 1) { //Once the fire button has been pressed on the python interface
        tft_drawLine(t1.vertex_x, t1.vertex_y, (int) t1.vertex_x+15*cos(RAD(t1.angle)), (int) t1.vertex_y - 15*sin(RAD(t1.angle)), ILI9340_WHITE); //Redraw cannons in place every frame
        tft_drawLine(t2.vertex_x, t2.vertex_y, (int) t2.vertex_x+15*cos(RAD(t2.angle)), (int) t2.vertex_y - 15*sin(RAD(t2.angle)), ILI9340_WHITE);
        if (turn == 1) {//Check which tank we are firing from
            t = &t1;
        } else {
            t = &t2;
        }
        if (t->shell_y > 0) tft_fillCircle((int)t->shell_x, (int)t->shell_y, 2,ILI9340_BLACK);//Projectile represented as a circle

        //Projectile motion physics
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
        } //Some of these conditional checks might be useless --recheck
        
//        sprintf(buffer, "%d ", (int) t->shell_y);
//        printLine2(3, 10,3, buffer, ILI9340_WHITE, ILI9340_BLACK);

        
        // check for hit
        int hit = collision();
        
        //If hit, transition to explosion condition (ground vs tank)
        if (hit == 1) {
            fire = 0;
            explosion = 1;
      
            t1.health -= 1;
            printf("$02 %d\r", t1.health);

        }
        else if (hit == 2) {
            fire = 0;
            explosion = 1;
            
            t2.health -= 1;
            printf("$03 %d\r", t2.health);
            
        } //if projectile x position is off TFT, stop drawing it
        else if (t->shell_x < 5 || t->shell_x > (TFT_WIDTH-5)) {
            tft_fillCircle((int)t->shell_x, (int) t->shell_y, 2, ILI9340_BLACK);
            fire = 0;
            turn = 2 - turn + 1; // change turn
            printf("$04 %d\r", turn);
            t->shell_x = (fix) t->vertex_x+15*cos(RAD(t->angle));
            t->shell_y = (fix) t->vertex_y-15*sin(RAD(t->angle));
        }
        // if projectile hits land
        else if (land[(int)t->shell_x] <= t->shell_y) {
            tft_fillCircle((int)t->shell_x, (int) t->shell_y, 2, ILI9340_BLUE);
            //Change land
            destroy_land((int)t->shell_x);
            //Redraw tanks so they follow the land changes
            redraw_tanks();
//            tft_fillTriangle((t1.x)-((t1.width)/2),t1.y, t1.x, t1.y-(t1.height), (t1.x)+((t1.width)/2), t1.y, ILI9340_WHITE);
//            tft_drawLine(t1.vertex_x, t1.vertex_y, (int) t1.vertex_x+15*cos(RAD(t1.angle)), (int) t1.vertex_y - 15*sin(RAD(t1.angle)), ILI9340_WHITE);
            fire = 0;
            turn = 2 - turn + 1; // change turn
            printf("$04 %d\r", turn);
            t->shell_x = (fix) t->vertex_x+15*cos(RAD(t->angle));
            t->shell_y = (fix) t->vertex_y-15*sin(RAD(t->angle));
        }
        
    }
    
    // explosion animation
    if(explosion == 1){
        // red expanding circle
        tft_fillCircle((int)t->shell_x, (int) t->shell_y, (short)increment, ILI9340_RED);
        increment = increment + .5;
        if(increment == 20){
            //erase circle
            tft_fillCircle((int)t->shell_x, (int) t->shell_y, (short)increment, ILI9340_BLACK);
            // redraw land
            int i;
            for (i = t->shell_x - 21; i < t->shell_x + 22; i++){
                tft_drawFastVLine(i, land[i], a_c(land[i]), ILI9340_BLUE);
            }
            //redraw both tanks since explosion could cover both tanks
            redraw_tanks();
            explosion=0;
            increment =0;
            t->shell_x = (fix) t->vertex_x+15*cos(RAD(t->angle));
            t->shell_y = (fix) t->vertex_y-15*sin(RAD(t->angle));
            turn = 2 - turn + 1; // change turn
            printf("$04 %d\r", turn);
        }
           
    }
    
    
    //Game Over Scenario
    if(t1.health == 0 && explosion==0){
        tft_fillScreen(ILI9340_BLACK);

        sprintf(buffer, "GAME OVER");
        printLine2(5, 20, 100, buffer, ILI9340_RED, ILI9340_BLACK);
        sprintf(buffer, "Player 2 wins!");
        printLine2(3, 20, 150, buffer, ILI9340_WHITE, ILI9340_BLACK);
        end_screen =1;
        // wait for user to start new game
        PT_YIELD_UNTIL(pt, new_game == 1);
        begin_time = PT_GET_TIME();
        restart_game();
    }

    if(t2.health == 0 && explosion==0){
        tft_fillScreen(ILI9340_BLACK);

        sprintf(buffer, "GAME OVER");
        printLine2(5, 20, 100, buffer, ILI9340_RED, ILI9340_BLACK);
        sprintf(buffer, "Player 1 wins!");
        printLine2(3, 20, 150, buffer, ILI9340_WHITE, ILI9340_BLACK);
        end_screen=1;

        // wait for user to start new game
        PT_YIELD_UNTIL(pt, new_game == 1);
        begin_time = PT_GET_TIME();
        restart_game();
    }

	// 30 fps => frame time of 33 mSec
    // "B" and "G" code is for testing and is not needed to run simulation
    
    
    //reset begin the time
    //Yield every 33msec for 30FPS
	PT_YIELD_TIME_msec(33 - (PT_GET_TIME() - begin_time)) ;
  } // end while

   PT_END(pt);
} // end thread



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
        //
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
        //
        else {
            //tft_printLine(1,0, receive_string, ILI9340_GREEN, ILI9340_BLACK, 2);
            printf("received>%s", receive_string);        
        }
    } // END WHILE(1)   
    PT_END(pt);  
} // thread python_string


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
            //printf("%d %d", list_id, list_value);
        }
        
        // radio group
        if (PT_term_buffer[0]=='r'){
            new_radio = 1;
            radio_group_id = PT_term_buffer[2] - '0' ;
            radio_member_id = PT_term_buffer[3] - '0';
            //printf("%d %d", radio_group_id, radio_member_id);
        }
        
        // string from python input line
        if (PT_term_buffer[0]=='$'){
            // signal parsing thread
            new_string = 1;
            // output to thread which parses the string
            // while striping off the '$'
            strcpy(receive_string, PT_term_buffer+1);
        }                                  
        
    } // END WHILE(1)   
    PT_END(pt);  
} // thread blink



// === Buttons thread ==========================================================
// process buttons from Python for clear LCD and blink the on-board LED
static PT_THREAD (protothread_buttons(struct pt *pt))
{
    PT_BEGIN(pt);
    // Set up buttons for bird sound inputs

    while(1){
        //Yield until new button, when we aren't processing a projectile or collision
        PT_YIELD_UNTIL(pt, new_button==1 && fire == 0 && explosion==0);
        // clear flag
        new_button = 0;
        
        //move left
        if (button_id==1 && button_value==1 && end_screen==0 && start_screen==0){
            //only move tank whose turn it is
            if(steps > 0){
                if(turn ==1){
                    //sending the direction 1 == left
                    move_step(&t1,1);
                }else{
                    move_step(&t2,1);
                } 
                steps -= 1;
                printf("$01 %d\r", steps);
            }
            ///testing the writing a number when something happens------------------------------------------------------------------------
            
        }
        //move right
        if (button_id==2 && button_value==1 && end_screen==0 && start_screen==0){
            if(steps > 0){
                if(turn == 1){
                    //sending the direction 2 == right
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
//            if(turn == 1){
//                fix midx =  (fix) t1.vertex_x+7.5*cos(RAD(t1.angle));
//                fix midy =  (fix) t1.vertex_y+7.5*sin(RAD(t1.angle));
//                if(!(t1.shell_y < land[(int)t1.shell_x] && midy > land[(int)midx])){
//                    fire = 1;
//                    steps = 5;
//                    printf("$01 %d\r", steps);
//                    t1.shell_vx = (fix) power*cos(RAD(t1.angle));
//                    t1.shell_vy = (fix) -power*sin(RAD(t1.angle));
//                }
//            }
//            else if(turn == 2){
//                fix midx =  (fix) t2.vertex_x+7.5*cos(RAD(t2.angle));
//                fix midy =  (fix) t2.vertex_y+7.5*sin(RAD(t2.angle));
//                if(!(t2.shell_y < land[(int)t2.shell_x] && midy > land[(int)midx])){
//                    fire = 1;
//                    steps = 5;
//                    printf("$01 %d\r", steps);
//                    t2.shell_vx = (fix) power*cos(RAD(t2.angle));
//                    t2.shell_vy = (fix) -power*sin(RAD(t2.angle));
//                }
//            }
            fire = 1;
            steps = 5;
          
            printf("$01 %d\r", steps);
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
        
        
    } // END WHILE(1)   
    PT_END(pt);  
} // thread blink

// === Sliders Threads ==========================================================
// process buttons from Python for clear LCD and blink the on-board LED
static PT_THREAD (protothread_sliders(struct pt *pt))
{
    PT_BEGIN(pt);
    while(1){
        PT_YIELD_UNTIL(pt, new_slider==1 && fire == 0 && end_screen==0 && explosion==0);
        // clear flag
        new_slider = 0;
       
        //mPORTASetBits(BIT_0); 
        int new_ang;
        if (slider_id == 1){ // change cannon angle
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
    } // END WHILE(1)   
    PT_END(pt);  
} // thread blink

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
        x += 30; //fixed x increase
        // random y coordinate
        num = ((float) rand()) / RAND_MAX;
        y = (float) (TFT_HEIGHT-num*200);
        
        // slope of this line segment
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

void tft_draw_level(){
    //Function for drawing the game level - initially is just a flat surface
    int i;
    rand_land_create();
    for(i = 0; i < 320; i++){
//        if(i >= 100 && i < 160){
//            land[i] = a_c(land_height+(i-100));
//        }
//        else if(i >= 160 && i < 220){
//            land[i] = a_c(land_height + (220 - i));
//        }
//        else{
//            land[i] = a_c(land_height);  
//        }
        tft_drawFastVLine(i, land[i], a_c(land[i]), ILI9340_BLUE); 
    }
}

int a_c(int y_value){
    //Converts y coordinates into bottom-left orientation, with origin at bottom
    //left instead of top left
    return TFT_HEIGHT - y_value;
}


// === Main  ====================================================================================================================================

void main(void) {
    
    //LED for debugging
    mPORTASetPinsDigitalOut(BIT_0);    //Set port as output
    //LED on
    mPORTAClearBits(BIT_0); 
    
    
    

    // === setup system wide interrupts  ========
    INTEnableSystemMultiVectoredInt();
  
    // === TFT setup ============================
    // init the display in main since more than one thread uses it.
    // NOTE that this init assumes SPI channel 1 connections
    
//    srand((unsigned) time(&t));
    PT_setup();
    //srand(56); //56


    
    
    tft_init_hw();
    tft_begin();
    tft_fillScreen(ILI9340_BLACK);
    //240x320 vertical display
    tft_setRotation(1); // Use tft_setRotation(1) for 320x240

    // === config threads ========================

//    time_t timer;
//    
//    sprintf(buffer, "%d ", (int) time(&timer));
//    printLine(3, buffer, ILI9340_WHITE, ILI9340_BLACK);
  
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
    //pt_sched_method = SCHED_ROUND_ROBIN ;
  
    // NOTE the controller must run in SCHED_ROUND_ROBIN mode
    // ALSO note that the scheduler is modified to cpy a char
    // from uart1 to uart2 for the controller

    pt_sched_method = SCHED_ROUND_ROBIN ;

    // === scheduler thread =======================
    // scheduler never exits
    PT_SCHEDULE(protothread_sched(&pt_sched));
    // ============================================
  
} // main
// === end  ======================================================

