#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <pthread.h>
#include <SDL2/SDL.h>

#define WIDTH 800
#define HEIGHT 600
#define THREAD_COUNT 4

#define MAX_PATTERN_LEN 3
#define SUSPEND_DELAY 20

#define NOW (timekeeper)
#define NXT (!timekeeper)

double ZOOM = 1;
int speed = 1;

SDL_Window *win;
SDL_Renderer *ren;

typedef struct {
    int x;
    int y;
} XY;

typedef struct Cell_ Cell;
struct Cell_ {
    Cell* adj[9];
    bool alive[2];
    XY xy;
    int last_logged;
    pthread_mutex_t alive_mutex;
};

Cell cellArray[WIDTH][HEIGHT];

/* size = 2^9 */
bool awnserCache[512];

int step = 0;
bool timekeeper = 0;

/* mouse position on screen */
int mx;
int my;

/* Change in screen position per frame */
int movingX = 0;
int movingY = 0;

/* top left corner of what can bee seen on screen */
double screenpx = 0;
double screenpy = 0;

/* used for storing the rectangles drawn by SDL */
int screenw, screenh;
SDL_Rect screen[WIDTH + 1][HEIGHT + 1];

bool running = true;
bool paused = true;

void step_until(unsigned int frame);
void proceed_step(void);
void process_cell(Cell *c);
void write_log(Cell *c);
Cell *read_log(void);
bool get_next(Cell *c);
void move_screen(void);
void update_screen_pos(void);
void set_zoom(double z);
void draw_screen(void);
XY get_tiled(int x, int y);
void init(void);
void handle_input(void);

Cell* cell_log[2][WIDTH * HEIGHT];
long write_log_pos;
long last_log_pos;
pthread_mutex_t write_log_mutex;

int threadids[THREAD_COUNT];
pthread_t threads[THREAD_COUNT];
bool thread_run[THREAD_COUNT];

void *thread_calc_cells(void *arg)
{
    int tid;

    tid = *((int *) arg);

    while (running)
    {
        /* I think this is actually enough to slow down the program */
        while (thread_run[tid] == false)
            usleep(1);

        int min = last_log_pos * tid / THREAD_COUNT;
        int max = last_log_pos * (tid + 1) / THREAD_COUNT;

        int i;
        for (i = min; i < max; i++)
            process_cell(cell_log[NOW][i]);

        thread_run[tid] = false;
    }

    return NULL;
}

void step_until(unsigned int frame)
{
    while (frame > step)
    {
        Cell *c;
        int i;
        for (i = 0; i < THREAD_COUNT; i++)
            thread_run[i] = true;

        bool allrunning = true;
        while (allrunning)
        {
            usleep(1);

            bool tmp = false;
            for (i = 0; i < THREAD_COUNT; i++)
                tmp = tmp || thread_run[i];

            allrunning = tmp;
        }

        proceed_step();
    }
}

void proceed_step(void)
{
    last_log_pos = write_log_pos;
    write_log_pos = 0;

    NOW = NXT;
    step++;
}

void process_cell(Cell *c)
{
    register unsigned int solIndex = 0;

    int i;
    /* we know that an adjacent cell changed last */
    for (i = 0; i < 9; i++)
        solIndex = (solIndex << 1) | c->adj[i]->alive[NOW];

    bool alivenow = c->alive[NXT];
    bool alivenxt = awnserCache[solIndex];


     if (alivenow != alivenxt)
        for (i = 0; i < 9; i++)
            write_log(c->adj[i]);
    /* if we know the other alive cells, and that they're going to be read
     * don't try adding them to list.
     */

    pthread_mutex_lock(&c->alive_mutex);
    c->alive[NXT] = alivenxt;
    pthread_mutex_unlock(&c->alive_mutex);
}

bool get_next(Cell *c)
{
    unsigned int solIndex = 0;

    int i;
    for (i = 0; i < 9; i++)
    {
        solIndex = solIndex << 1;
        solIndex += c->adj[i]->alive[NOW];
    }

    return awnserCache[solIndex];
}

void write_log(Cell *c)
{
    if (c->last_logged == step)
        return;

    c->last_logged = step;

    pthread_mutex_lock(&write_log_mutex);
    cell_log[NXT][write_log_pos++] = c;
    pthread_mutex_unlock(&write_log_mutex);

}

void move_screen(void)
{
    screenpx += movingX;
    if (screenpx <= 0)
        screenpx = 0;
    else if (screenpx >= WIDTH - WIDTH / ZOOM)
        screenpx = WIDTH - WIDTH / ZOOM;

    screenpy += movingY;
    if (screenpy <= 0)
        screenpy = 0;
    else if (screenpy >= HEIGHT - HEIGHT / ZOOM)
        screenpy = HEIGHT - HEIGHT / ZOOM;

    movingX = movingY = 0;

    update_screen_pos();
}

void update_screen_pos(void)
{
    /* bounds of screen, accounting for non-integer camera coordinates */
    int xmax = WIDTH  / ZOOM + 1;
    int ymax = HEIGHT / ZOOM + 1;

    double xdiff = screenpx - (int)screenpx;
    double ydiff = screenpy - (int)screenpy;

    int x, y;
    for (x = 0; x < xmax; x++) for (y = 0; y < ymax; y++)
    {
        /* calculate the corners of cells,
         * if out of bounds, adjust them.
         */
        int x1 = (x - xdiff) * ZOOM;
        int y1 = (y - ydiff) * ZOOM;
        int x2 = (x + (1 - xdiff)) * ZOOM;
        int y2 = (y + (1 - ydiff)) * ZOOM;

        if (x1 < 0)
            x1 = 0;

        if (y1 < 0)
            y1 = 0;

        if (x2 > WIDTH)
            x2 = WIDTH;

        if (y2 > HEIGHT)
            y2 = HEIGHT;

        screen[x][y].x = x1;
        screen[x][y].y = y1;
        screen[x][y].w = x2 - x1;
        screen[x][y].h = y2 - y1;
    }
    screenw = x;
    screenh = y;
}

void set_zoom(double z)
{
    if (z < 1)
        z = 1;

    if (z == 1)
    {
        screenpx = 0;
        screenpy = 0;
    } else {
        screenpx += (WIDTH  / ZOOM - WIDTH  / z) / 2;
        screenpy += (HEIGHT / ZOOM - HEIGHT / z) / 2;
    }

    ZOOM = z;
    update_screen_pos();
}

void draw_screen(void)
{
    Cell* c;
    int x, y;
    /* draw internal rectangles */
    for (x = 0; x < screenw; x++) for (y = 0; y < screenh; y++)
    {
        Cell* c = &cellArray[(int)screenpx + x][(int)screenpy + y];

        if (c->alive[NOW])
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        else
            SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);

        SDL_RenderFillRect(ren, &screen[x][y]);
    }

    SDL_RenderPresent(ren);
}

/* helper function that returns torodial coordinates, folding over edge */
XY get_tiled(int x, int y)
{
    if (x < 0)
        x = WIDTH - 1;
    else if (x >= WIDTH)
        x = 0;

    if (y < 0)
        y = HEIGHT - 1;
    else if (y >= HEIGHT)
        y = 0;

    return (XY){ x, y };
}

void init(void)
{
    int x, y;
    for (x = 0; x < WIDTH; x++) for (y = 0; y < HEIGHT; y++)
    {
        XY cE  = get_tiled( x + 1, y     );
        XY cNE = get_tiled( x + 1, y + 1 );
        XY cN  = get_tiled( x    , y + 1 );
        XY cNW = get_tiled( x - 1, y + 1 );
        XY cW  = get_tiled( x - 1, y     );
        XY cSW = get_tiled( x - 1, y - 1 );
        XY cS  = get_tiled( x    , y - 1 );
        XY cSE = get_tiled( x + 1, y - 1 );

        cellArray[x][y].adj[0] = &cellArray[x][y];
        cellArray[x][y].adj[1] = &cellArray[ cE.x][ cE.y];
        cellArray[x][y].adj[2] = &cellArray[cNE.x][cNE.y];
        cellArray[x][y].adj[3] = &cellArray[ cN.x][ cN.y];
        cellArray[x][y].adj[4] = &cellArray[cNW.x][cNW.y];
        cellArray[x][y].adj[5] = &cellArray[ cW.x][ cW.y];
        cellArray[x][y].adj[6] = &cellArray[cSW.x][cSW.y];
        cellArray[x][y].adj[7] = &cellArray[ cS.x][ cS.y];
        cellArray[x][y].adj[8] = &cellArray[cSE.x][cSE.y];

        cellArray[x][y].xy = (XY){ x, y };
        cellArray[x][y].last_logged = step - 1;

        pthread_mutex_init(&cellArray[x][y].alive_mutex, NULL);
    }
    pthread_mutex_init(&write_log_mutex, NULL);

    /* set up awnserCache, iterate over possible neighbor conditions */
    int i;
    for (i = 0; i < 512; i++)
    {
        /* count neighbors, use last bit to determine if alive */
        int neighbors = 0;
        int p;
        for (p = 1; p < (1 << 8); p = p << 1)
        {
            if (i & p)
                neighbors++;
        }

        bool alive = i & (1 << 8);
        if (alive)
            awnserCache[i] = (neighbors == 2 || neighbors == 3);
        else
            awnserCache[i] = (neighbors == 3);
    }

    /* add everything to log, to start updating */
    for (x = 0; x < WIDTH; x++) for (y = 0; y < HEIGHT; y++)
    {
        Cell *c = &cellArray[x][y];
        process_cell(c);
    }

    proceed_step();

    /* start threads */
    for (i = 0; i < THREAD_COUNT; i++)
    {
        threadids[i] = i;
        pthread_create(&threads[i], NULL, &thread_calc_cells, &threadids[i]);
    }
}

void handle_input(void)
{
    SDL_Event e;
    while (SDL_PollEvent(&e))
    {
        switch (e.type)
        {
            case SDL_KEYDOWN:
                switch (e.key.keysym.sym)
                {
                    case SDLK_SPACE:
                        paused = !paused;
                        break;

                    case SDLK_RETURN:
                        paused = true;
                        step_until(step + 1);
                        draw_screen();
                        break;

                    case SDLK_UP:
                        set_zoom(ZOOM*2);
                        break;

                    case SDLK_DOWN:
                        set_zoom(ZOOM/2);
                        break;

                    case SDLK_LEFT:
                        if (speed > 1)
                            speed--;
                        break;

                    case SDLK_RIGHT:
                        speed++;
                        break;

                    case SDLK_k:
                        movingY = -1;
                        break;

                    case SDLK_h:
                        movingX = -1;
                        break;

                    case SDLK_j:
                        movingY = 1;
                        break;

                    case SDLK_l:
                        movingX = 1;
                        break;
                }
            break;

            case SDL_MOUSEMOTION:
            /*
                mx = e.motion.x;
                my = e.motion.y;
                if (mx > WIDTH - 5)
                    movingX = 5;
                else if (mx < 5)
                    movingX = -5;
                else
                    movingX = 0;

                if (my > HEIGHT - 5)
                    movingY = 5;
                else if (my < 5)
                    movingY = -5;
                else
                    movingY = 0;
            */
            break;

            case SDL_MOUSEBUTTONDOWN:
            case SDL_QUIT:
                running = 0;
            break;
        }
    }
}


int main(int argc, char* argv[])
{
    bool gui = true;
    int maxstep = 500;

    int i;
    for (i = 0; i < argc; i++)
    {
        char* arg = argv[i];
        if ((arg[0] == '-') && (arg[1] == 'f'))
        {
            gui = false;
            sscanf(argv[i+1], "%i", &maxstep);
        }
        else if ((arg[0] == '-') && (arg[1] == 'p'))
            paused = true;
    }


    /* Large Block */
    int x, y;
    for (x = WIDTH/2 - 100; x < (WIDTH/2 + 100); x++) for (y = HEIGHT/2 - 100; y < (HEIGHT/2 + 100); y++)
        cellArray[x][y].alive[NOW] = true;

    /* R-pentomino */
    /*
    int x = WIDTH/2;
    int y = HEIGHT/2;
    cellArray[x  ][y  ].alive[NOW] = true;
    cellArray[x-1][y  ].alive[NOW] = true;
    cellArray[x  ][y+1].alive[NOW] = true;
    cellArray[x+1][y+1].alive[NOW] = true;
    cellArray[x  ][y-1].alive[NOW] = true;
    */

    init();

    if (gui == true)
    {
        set_zoom(1);

        win = SDL_CreateWindow("Life", 0, 0, WIDTH, HEIGHT, SDL_WINDOW_MAXIMIZED );
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

        while (running)
        {
            if (!paused)
                step_until(step + speed);

            handle_input();
            draw_screen();
            move_screen();
        }

        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);

        SDL_Quit();
    } else { /* if (gui == false) */
        step_until(maxstep);
        /*
        int i;
        for (i = 0; i < maxstep; i++)
            printf("%i\n", data_collect[i]);*/
    }

    /* ensure other threads terminate */
    running = false;

    return 0;
}
