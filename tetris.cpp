/*
Console Tetris - Single-file C++ implementation

How to use:
- Windows (MSVC / MinGW):
    g++ -std=c++17 -O2 tetris.cpp -o tetris.exe
    ./tetris.exe

- Linux / macOS:
    g++ -std=c++17 -O2 tetris.cpp -o tetris
    ./tetris

This is a terminal/console version that uses simple ANSI escape sequences to redraw the board.
It provides its own small cross-platform non-blocking input layer using:
 - _kbhit()/_getch() on Windows
 - termios + select on POSIX

Keys:
 - a / A / <- : move left
 - d / D / -> : move right
 - s / S / down arrow : soft drop
 - w / W / up arrow : rotate clockwise
 - space : hard drop
 - p : pause
 - q : quit

Features:
 - Standard 7 tetrominoes
 - Rotation (simple 4x4 matrix rotation)
 - Line clearing, scoring, and level progression
 - Next piece preview
 - Simple game loop with gravity and input handling

Notes & limitations:
 - Terminal must support ANSI escape codes (most modern terminals do).
 - Timing may vary a bit between platforms.
 - This is a teaching/learning implementation — not optimized for extreme performance.

Enjoy! If you want a graphical version (SDL2/SFML/SDL), tell me and I can provide that too.
*/

#include <bits/stdc++.h>
#ifdef _WIN32
#include <conio.h>
#include <thread>      // ✅ correct place
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#endif

using namespace std;

// Board size
const int BOARD_W = 10;
const int BOARD_H = 20;

// Tetromino definitions (4x4 grids, 16 chars). Using X for filled, . for empty.
const vector<vector<string>> TETROMINO = {
    // I
    {
        "....",
        "XXXX",
        "....",
        "...."
    },
    // J
    {
        "X..",
        "XXX",
        "..."
    },
    // L
    {
        "..X",
        "XXX",
        "..."
    },
    // O (2x2)
    {
        "XX",
        "XX"
    },
    // S
    {
        ".XX",
        "XX.",
        "..."
    },
    // T
    {
        ".X.",
        "XXX",
        "..."
    },
    // Z
    {
        "XX.",
        ".XX",
        "..."
    }
};

// We'll store tetrominoes normalized into 4x4 bool arrays (to make rotation easier)
struct Piece {
    array<array<int,4>,4> cells{}; // 0 or 1
    int size; // 2,3 or 4
};

vector<Piece> pieces;

void initPieces(){
    auto toPiece = [&](const vector<string>& shape){
        Piece p{};
        p.size = (int)shape.size();
        for(int r=0;r<p.size;++r){
            for(int c=0;c<p.size;++c){
                p.cells[r][c] = (shape[r][c]=='X')?1:0;
            }
        }
        // put into 4x4 grid (top-left)
        Piece out{}; out.size = 4;
        for(int r=0;r<4;++r) for(int c=0;c<4;++c) out.cells[r][c]=0;
        for(int r=0;r<p.size;++r) for(int c=0;c<p.size;++c) out.cells[r][c]=p.cells[r][c];
        return out;
    };
    for(auto &s:TETROMINO) pieces.push_back(toPiece(s));
}

// Rotate 4x4 piece clockwise times (0..3)
Piece rotatePiece(const Piece &p, int times){
    Piece cur = p;
    times = (times%4+4)%4;
    while(times--){
        Piece nw{}; nw.size = 4;
        for(int r=0;r<4;++r) for(int c=0;c<4;++c) nw.cells[r][c] = cur.cells[3-c][r];
        cur = nw;
    }
    return cur;
}

// Terminal control
void clearScreen(){
#ifdef _WIN32
    // Using WinAPI to avoid flicker
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(h, &csbi);
    DWORD cells = csbi.dwSize.X * csbi.dwSize.Y;
    COORD home = {0,0};
    DWORD written;
    FillConsoleOutputCharacter(h,' ',cells,home,&written);
    SetConsoleCursorPosition(h,home);
#else
    cout << "\x1b[2J\x1b[H"; // clear and home
#endif
}

void hideCursor(){
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO info;
    GetConsoleCursorInfo(h,&info);
    info.bVisible = FALSE;
    SetConsoleCursorInfo(h,&info);
#else
    cout << "\x1b[?25l";
#endif
}
void showCursor(){
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO info;
    GetConsoleCursorInfo(h,&info);
    info.bVisible = TRUE;
    SetConsoleCursorInfo(h,&info);
#else
    cout << "\x1b[?25h";
#endif
}

// Cross-platform non-blocking keyboard
void initTerminal(){
#ifndef _WIN32
    // make stdin non-blocking and disable echo
    termios t;
    tcgetattr(STDIN_FILENO, &t);
    t.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
#endif
}
void restoreTerminal(){
#ifndef _WIN32
    termios t;
    tcgetattr(STDIN_FILENO, &t);
    t.c_lflag |= (ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    flags &= ~O_NONBLOCK;
    fcntl(STDIN_FILENO, F_SETFL, flags);
#endif
}

int kbHit(){
#ifdef _WIN32
    return _kbhit();
#else
    fd_set set;
    struct timeval tv;
    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);
    tv.tv_sec = 0; tv.tv_usec = 0;
    return select(STDIN_FILENO+1, &set, NULL, NULL, &tv) > 0;
#endif
}

int getChNonBlocking(){
#ifdef _WIN32
    if(_kbhit()) return _getch();
    return -1;
#else
    int c = getchar();
    if(c==EOF) return -1;
    return c;
#endif
}

// Game state
struct Game{
    vector<vector<int>> board; // 0 empty, >0 filled piece id
    int curPieceId;
    int curRot; // 0..3
    int curX, curY; // position of top-left of 4x4 block relative to board (x: col, y: row)
    int nextPieceId;
    bool gameOver = false;
    long long score = 0;
    int level = 1;
    int linesCleared = 0;
};

// Utilities
bool collides(const Game &g, int pieceId, int rot, int x, int y){
    Piece p = rotatePiece(pieces[pieceId], rot);
    for(int r=0;r<4;++r) for(int c=0;c<4;++c){
        if(!p.cells[r][c]) continue;
        int br = y + r;
        int bc = x + c;
        if(bc < 0 || bc >= BOARD_W || br >= BOARD_H) return true; // out of bounds
        if(br >=0 && g.board[br][bc]) return true; // hit filled cell
    }
    return false;
}

void placePiece(Game &g){
    Piece p = rotatePiece(pieces[g.curPieceId], g.curRot);
    for(int r=0;r<4;++r) for(int c=0;c<4;++c){
        if(!p.cells[r][c]) continue;
        int br = g.curY + r;
        int bc = g.curX + c;
        if(br>=0 && br<BOARD_H && bc>=0 && bc<BOARD_W) g.board[br][bc] = g.curPieceId+1; // store id+1
    }
}

int clearLines(Game &g){
    int cleared = 0;
    for(int r=BOARD_H-1;r>=0;--r){
        bool full = true;
        for(int c=0;c<BOARD_W;++c) if(g.board[r][c]==0){ full=false; break; }
        if(full){
            cleared++;
            // move everything above down
            for(int rr=r; rr>0; --rr) g.board[rr] = g.board[rr-1];
            g.board[0] = vector<int>(BOARD_W,0);
            ++r; // re-check this row after shift
        }
    }
    if(cleared>0){
        // Scoring: classic Tetris: 1 line=40 * level, 2=100*level, 3=300*level, 4=1200*level (using SRS-like)
        static int scoreTable[5] = {0,40,100,300,1200};
        g.score += scoreTable[cleared] * g.level;
        g.linesCleared += cleared;
        g.level = 1 + g.linesCleared / 10; // level up every 10 lines
    }
    return cleared;
}

void spawnPiece(Game &g){
    g.curPieceId = g.nextPieceId;
    g.nextPieceId = rand() % pieces.size();
    g.curRot = 0;
    g.curX = BOARD_W/2 - 2;
    g.curY = -2; // allow spawn partly above board
    if(collides(g, g.curPieceId, g.curRot, g.curX, g.curY)){
        g.gameOver = true;
    }
}

// Draw functions
string pieceChar(int id){
    static const char *ch = "@#%*+xo"; // up to 7
    if(id<=0) return " ";
    int idx = (id-1) % 7;
    return string(1, ch[idx]);
}

void drawGame(const Game &g){
    // Build a visual buffer
    vector<string> out(BOARD_H, string(BOARD_W, ' '));
    // copy board
    for(int r=0;r<BOARD_H;++r) for(int c=0;c<BOARD_W;++c) if(g.board[r][c]) out[r][c] = pieceChar(g.board[r][c])[0];
    // overlay current piece
    Piece p = rotatePiece(pieces[g.curPieceId], g.curRot);
    for(int r=0;r<4;++r) for(int c=0;c<4;++c){
        if(!p.cells[r][c]) continue;
        int br = g.curY + r;
        int bc = g.curX + c;
        if(br>=0 && br<BOARD_H && bc>=0 && bc<BOARD_W) out[br][bc] = pieceChar(g.curPieceId+1)[0];
    }
    // Render
    clearScreen();
    cout << "+" << string(BOARD_W,'-') << "+\n";
    for(int r=0;r<BOARD_H;++r){
        cout << "|";
        for(int c=0;c<BOARD_W;++c){
            char ch = out[r][c];
            if(ch==' ') cout << ' ';
            else cout << ch;
        }
        cout << "|\n";
    }
    cout << "+" << string(BOARD_W,'-') << "+\n";
    cout << "Score: "<< g.score << "  Level: "<< g.level << "  Lines: "<< g.linesCleared << "\n";
    // Next piece preview
    cout << "Next:\n";
    Piece np = rotatePiece(pieces[g.nextPieceId],0);
    for(int r=0;r<4;++r){
        for(int c=0;c<4;++c) cout << (np.cells[r][c] ? pieceChar(g.nextPieceId+1) : string(" "));
        cout << "\n";
    }
    cout << "Controls: a/d left-right, w rotate, s soft drop, space hard drop, p pause, q quit\n";
}

int main(){
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    srand((unsigned)time(nullptr));

    initPieces();
    initTerminal();
    hideCursor();

    Game g;
    g.board.assign(BOARD_H, vector<int>(BOARD_W,0));
    g.nextPieceId = rand() % pieces.size();
    spawnPiece(g);

    using clk = chrono::steady_clock;
    auto lastFall = clk::now();
    double gravityInterval = 0.8; // seconds per automatic drop (will speed with level)
    bool paused = false;

    while(true){
    if(g.gameOver) break;
    // adjust gravity based on level (simple exponential decay)
    gravityInterval = max(0.05, 0.8 * pow(0.85, g.level-1));

    // input handling (non-blocking)
    while(kbHit()){
        int ch = getChNonBlocking();
        if(ch==-1) break;
        // handle escape sequences for arrows on some terminals (simple support)
        if(ch==27){ // ESC
            // possibly arrow sequence
            int n1 = getChNonBlocking();
            if(n1==91){ // '['
                int n2 = getChNonBlocking();
                if(n2==65) ch='w'; // up
                else if(n2==66) ch='s'; // down
                else if(n2==67) ch='d'; // right
                else if(n2==68) ch='a'; // left
            }
        }
        if(ch=='q' || ch=='Q'){
            g.gameOver = true; break;
        } else if(ch=='p' || ch=='P'){
            paused = !paused;
        } else if(!paused){
            if(ch=='a' || ch=='A'){
                if(!collides(g, g.curPieceId, g.curRot, g.curX-1, g.curY)) g.curX--;
            } else if(ch=='d' || ch=='D'){
                if(!collides(g, g.curPieceId, g.curRot, g.curX+1, g.curY)) g.curX++;
            } else if(ch=='s' || ch=='S' || ch== 'B'){
                // soft drop
                if(!collides(g, g.curPieceId, g.curRot, g.curX, g.curY+1)) g.curY++;
                else { placePiece(g); clearLines(g); spawnPiece(g); }
                lastFall = clk::now();
            } else if(ch=='w' || ch=='W' || ch=='A'){
                int newRot = (g.curRot+1)%4;
                if(!collides(g, g.curPieceId, newRot, g.curX, g.curY)) g.curRot = newRot;
            } else if(ch==' '){
                // hard drop
                while(!collides(g, g.curPieceId, g.curRot, g.curX, g.curY+1)) g.curY++;
                placePiece(g);
                clearLines(g);
                spawnPiece(g);
                lastFall = clk::now();
            }
        }
    }

    if(paused){
        // display paused state
        drawGame(g);
        cout << "*** PAUSED - press 'p' to resume ***\n";
        Sleep(100); // pause for 100 milliseconds

        continue;
    }

    auto now = clk::now();
    double elapsed = chrono::duration<double>(now - lastFall).count();
    if(elapsed >= gravityInterval){
        // try move down
        if(!collides(g, g.curPieceId, g.curRot, g.curX, g.curY+1)){
            g.curY++;
        } else {
            // place and spawn new
            placePiece(g);
            clearLines(g);
            spawnPiece(g);
        }
        lastFall = now;
    }

    drawGame(g);
    // tiny sleep to limit CPU
    Sleep(20); // pause for 20 milliseconds

}

// final screen
drawGame(g);
cout << "GAME OVER! Final Score: "<< g.score << "\n";
showCursor();
restoreTerminal();
return 0;
}
