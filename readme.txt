Summary
This is a fast, basic implementation of Conway's Game of Life, using the SDL2
library for graphics. The program uses a torodial array, which ``rolls over''
when life reaches one edge of the screen. The program does not use advanced
algorithms such as HashLife, because it's purpose was to experiment with 
optimizations involving changes in a program's implementation as opposed to 
algorithm.

TODO
- Make cell count independent of window size
- Don't hardcode starting conditions: take input files
- Dynamic frame rate
