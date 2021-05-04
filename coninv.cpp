/**
 * Program: Console Invaders
 * 
 * A simple Space Invaders clone done on the MS Windows Console. The objective of this 
 * program is to be a demo of standard game development concepts following the example
 * of Javidx9's blog (http://www.onelonecoder.com/).
 * 
 * It showcases some interesting techniques:
 * - Fast prototyping on the console
 * - Focus on getting the fundamental game logic done, rather than fancy graphics
 * - Experiment with game loop timing and the practical uses of the `chrono` library
 * - Simple animations and a pinch of creativity with characters
 * - Use of UNICODE strings
 * - Using some Win32 API on the console for fast animations
 * 
 * \author Christian Bruccoleri
 * This is free software. Do with it whatever you want. LICENSE: GPLv3
 */

#include <iostream>
#include <string>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <vector>
#include <assert.h>
using namespace std;

#include <Windows.h>

// Game screen parameters
static const int nScreenWidth = 120;
static const int nScreenHeight = 30;

// Array of 10 x 4 aliens; valid states:
static const int nAlienBlockWidth = 10;
static const int nAlienBlockHeight = 4;
// 0 - Alive, 1 - Exploding, 2 - Dead
unsigned char alienState[nAlienBlockWidth*nAlienBlockHeight];
// 1: left to right, -1: right to left 
int nAlienStep = 1;
// Number of aliens remaining
int nAliens = nAlienBlockWidth*nAlienBlockHeight;

// "graphical" representation of the aliens; each frame is 3 characters
const int nAlienGlyphWidth = 3;
wstring alienGlyphs[4] = {L"<o>>o<", L"}O{-O-", L"[T]]+[", L"(+)-x-"};
//wstring shieldGlyphs = {L" \x2591\2592\2593\x2588"};
wstring shieldGlyphs = {L" -=#"};
wstring playerGlyph = L"<I>";

// game state variables
float fPlayerX;
static const int nPlayerY = nScreenHeight - 1;
float fPlayerVx;
float fPlayerBulletSpeed;
int nAlienBlockX;
int nAlienBlockY;
float fExplodingElapsed;
int nAlienExploding; // -1: none exploding yet
// How fast alien bullets move
float fAlienBulletSpeed;

// Arrow Left, Arrow Right, Spacebar, ESC, Pause
static const int nPlayerKeys = 5;
bool bKeyPressed[nPlayerKeys] = {false, false, false, false, false};
// Used to latch the key presses
bool bKeyHold[nPlayerKeys] = {true, true, true, true, true};
enum KeyMnemonics {
    LEFT_ARROW,
    RIGHT_ARROW,
    SPACEBAR,
    ESC,
    PAUSE
};


struct Shield {
    static const int Length = 8;
    static const int Height = 3;
    static const int MaxStrength = 3;
    int m_strength[Length*Height];
    int nX;
    int nY;


    Shield(int x, int y) {
        for (int i = 0; i < Length*Height; ++i) m_strength[i] = MaxStrength;
        nX = x;
        nY = y;
    }


    bool hit(int colX, int rowY) {
        if (nX <= colX && colX < (nX + Length) && nY <= rowY && rowY < (nY + Height)) {
            int shieldOff = (rowY-nY)*Length + (colX-nX);
            assert(0 <= shieldOff && shieldOff < Length*Height);
            if (m_strength[shieldOff] > 0) {
                m_strength[shieldOff] --;
                return true;
            }
            else return false;
        }
        else return false;
    }
};

// Barriers to protect the player
vector<Shield> shields;


// Screen buffer
wchar_t *screen = new wchar_t[nScreenWidth*nScreenHeight];

struct Bullet {
    bool visible;
    float x;
    float y;
    wchar_t glyph;
    Bullet(wchar_t _glyph=L'|'): visible(false), x(0), y(0), glyph(_glyph) {};
};


void InitGame()
{
    // game state variables
    fPlayerX = (float)(nScreenWidth - playerGlyph.length()) / 2.0f;
    fPlayerVx = 12.0f;
    fPlayerBulletSpeed = -20.0f;
    nAlienBlockX = 2;
    nAlienBlockY = 2;
    fExplodingElapsed = 0.0f;
    nAlienExploding = -1; // -1: none exploding yet
    // How fast alien bullets move
    fAlienBulletSpeed = 20.0f;
    shields.clear();
    for (int i = 0; i < 3; ++i) shields.push_back(Shield((i+1)*30, nScreenHeight-6)); 
}


inline bool AlienFire(vector<Bullet>& alienBullets, int i, int j)
{
    for (auto& b: alienBullets)
        if (! b.visible) {
            b.x = (float)(nAlienBlockX + 6*j + 1);
            b.y = (float)(nAlienBlockY + i + 1);
            b.visible = true;
            return true;
        }
    return false;
}


inline void ClearBuffer(wchar_t* pScreenBuf)
{
	for (int i = 0; i < nScreenWidth*nScreenHeight; i++) 
        pScreenBuf[i] = L' ';
}


inline int GetAlienScreenOffset(int i, int j)
{
    return (2*i + nAlienBlockY)*nScreenWidth + nAlienBlockX + j*6;
}


void DrawAliens(int nFrameOffset)
{
    for (int i = 0; i < nAlienBlockHeight; ++i) {
        for (int j = 0; j < nAlienBlockWidth; ++j) {
            int nScreenOffset = GetAlienScreenOffset(i,j);
            const int alienIndex = i*nAlienBlockWidth + j;
            int state = alienState[alienIndex];
            switch (state) {
                case 0: // alive
                    for (int k = 0; k < 3; ++k)
                        screen[nScreenOffset + k] = alienGlyphs[i % 4][nFrameOffset + k];
                    break;
                case 1: // exploding
                    for (int k = 0; k < 3; ++k)
                        screen[nScreenOffset + k] = L'x';
                    break;
                case 2: // dead; draw blanks
                    //screen[nScreenOffset + k] = L' ';
                    break;
                default:
                    break;
            }
        }
    }
}


bool HitAlien(const Bullet* bullet, int* iAlien, int* jAlien) {
    int nBulletY = (int)roundf(bullet->y);
    int nBulletX = (int)roundf(bullet->x);
    int bulletIndex = (nBulletY-1)*nScreenWidth + nBulletX;
    const wstring specialChars = L"*#=- "; // characters to ignore
    bool bHitAlien = bullet->y > 0; // have not reached the top of the screen AND ..
    for (int k = 0; k < specialChars.length(); ++k) { // not any of the special characters
        bHitAlien &= (screen[bulletIndex] != specialChars[k]);
        if (!bHitAlien) break;
    }
    if (bHitAlien) { // Got one; figure out which one
        for (int i = 0; i < nAlienBlockHeight; ++i) {
            for (int j = 0; j < nAlienBlockWidth; ++j) {
                int alienOffset = GetAlienScreenOffset(i, j);
                if (alienOffset <= bulletIndex && bulletIndex < (alienOffset+3)) {
                    *iAlien = i;
                    *jAlien = j;
                    i = nAlienBlockHeight;
                    break;
                }
            }
        }
    }
    return bHitAlien;
}


void DrawPlayer(bool bPlayerHit)
{
    int nPlayerX = (int)roundf(fPlayerX);
    for (int k = 0; k < playerGlyph.length(); ++k) {
        const int offset = nPlayerY*nScreenWidth + nPlayerX + k;
        if (offset < nScreenHeight*nScreenWidth) screen[offset] = bPlayerHit ? L'X': playerGlyph[k];
    }
}


inline void DrawBullets(const vector<Bullet>& alienBullets, Bullet* pBullet)
{
    // player
    int nBulletY = (int)roundf(pBullet->y);
    int nBulletX = (int)roundf(pBullet->x);
    if (pBullet->visible)
        screen[nBulletY*nScreenWidth + nBulletX] = pBullet->glyph;
    // aliens
    for (auto &b: alienBullets)
        if (b.visible) {
            int nX = (int)roundf(b.x);
            int nY = (int)roundf(b.y);
            screen[nY*nScreenWidth + nX] = b.glyph;
        }
}


void DrawShields()
{
    for (auto& shld: shields)
        for (int i = 0; i < Shield::Height; ++i)
            for (int j = 0; j < Shield::Length; ++j) {
                const int nScreenOffset = (shld.nY + i)*nScreenWidth + shld.nX + j;
                const int nStrengthVal =  shld.m_strength[i*Shield::Length + j];
                screen[nScreenOffset] = shieldGlyphs[nStrengthVal];
            }
}



int main()
{
    ClearBuffer(screen);
	HANDLE hConsole = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE, 0, NULL, CONSOLE_TEXTMODE_BUFFER, NULL);
	SetConsoleActiveScreenBuffer(hConsole);
	DWORD dwBytesWritten = 0;
    bool bQuit = false;
    while (! bQuit) {
        InitGame();
        bool bGameOver = false;
        bool bPlayerHit = false;    // true IFF player has been hit
        bool bUpdateAnim = false;   // true IFF a predetermined amount of time has elapsed since last frame
        float fAnimElapsed = 0.0f;  // counter for animation time
        int nFrameOffset = 0;
        int nScore = 0; // Score
        int nLives = 3;// Number of player lives
        int iAlien = -1, jAlien = -1;
        float fAnimDelay = 0.35f; // Delay between animations
        Bullet bullet;
        // Used to initialize the vector
        const int nMaxAlienBullet = 5;
        vector<Bullet> alienBullets;
        // initialize all aliens to state "Alive
        alienBullets.clear();
        for (int k = 0; k < nAlienBlockHeight*nAlienBlockWidth; ++k) alienState[k] = 0;
        for (int k = 0; k < nMaxAlienBullet; ++k) alienBullets.push_back(Bullet('*'));
        // initialize timers
        auto tp1 = chrono::system_clock::now();
        auto tp2 = chrono::system_clock::now();
        while (! bGameOver) {
            // Update timing
            tp2 = chrono::system_clock::now();
            chrono::duration<float> elapsedTime = tp2 - tp1;
            tp1 = tp2;
            float fElapsedTime = elapsedTime.count();
            fAnimElapsed += fElapsedTime;
            bUpdateAnim = fAnimElapsed >= fAnimDelay;

            // Get Player Input
            for (int k = 0; k < nPlayerKeys; ++k)
                bKeyPressed[k] = (GetAsyncKeyState((unsigned char)"\x25\x27\x20\x1b\x13"[k]) & 0x8000) != 0;

            if (bKeyPressed[ESC]) // player requests exit
                bGameOver = bQuit = true;

            if (bKeyPressed[LEFT_ARROW] && ! bPlayerHit) {
                float dx = fPlayerVx * fElapsedTime;
                if (fPlayerX > dx) fPlayerX -= dx;
                else fPlayerX = 0.0f;
            }
            else bKeyHold[LEFT_ARROW] = true;

            if (bKeyPressed[RIGHT_ARROW] && ! bPlayerHit) {
                float dx = fPlayerVx * fElapsedTime;
                const float maxX = (float)(nScreenWidth - playerGlyph.length());
                if (fPlayerX + dx <= maxX) fPlayerX += dx;
                else fPlayerX = maxX;
            }
            else bKeyHold[RIGHT_ARROW] = true;

            // Firing
            if (bullet.visible) { // already fired; move bullet
                bullet.y += fPlayerBulletSpeed * fElapsedTime;
                int nBulletX = (int)roundf(bullet.x);
                int nBulletY = (int)roundf(bullet.y);
                // check if the shields are hit
                for (auto& shld: shields)
                    if (shld.hit(nBulletX, nBulletY)) 
                        bullet.visible = false;
                if (nBulletY <= 0)
                    bullet.visible = false;
                else if (HitAlien(&bullet, &iAlien, &jAlien)) {
                    bullet.visible = false;
                    alienState[iAlien*nAlienBlockWidth + jAlien] = 1; // exploding
                    fExplodingElapsed = 0.0f;
                    nAlienExploding = iAlien*nAlienBlockWidth + jAlien;
                    nScore += 100;
                }
            }
            else if (bKeyPressed[SPACEBAR] && bKeyHold[SPACEBAR]  && ! bPlayerHit) { // firing new bullet?
                bullet.y = (float)nScreenHeight - 2.0f;
                bullet.x = fPlayerX + 1.0f;
                bullet.visible = true;
                bKeyHold[SPACEBAR] = false;
            }
            else { // spacebar released
                bKeyHold[SPACEBAR] = true;
            }

            //// Update Logic
            // Move the aliens
            if (nAlienBlockY + nAlienBlockHeight >= nScreenHeight) {
                // Aliens at the bottom of the screen
                bGameOver = true;
                nAlienBlockY = 2;
            }
            else if (bUpdateAnim && (nAlienBlockX + 2*nAlienBlockWidth*nAlienGlyphWidth >= nScreenWidth)) {
                // reached the right side of the screen
                nAlienStep = (nAlienStep == 1) ? -1: 1;
                nAlienBlockY ++;
                nAlienBlockX --;
                fAnimDelay -= (fAnimDelay > 10.0) ? 0.05f: 0.0f;
            }
            else if (bUpdateAnim && (nAlienBlockX <= 0)) {
                // reached the left side of the screen
                nAlienStep = (nAlienStep == 1) ? -1: 1;
                nAlienBlockY ++;
                nAlienBlockX ++;
                fAnimDelay -= (fAnimDelay > 10.0) ? 0.05f: 0.0f;
            }
            else {
                // move aliens by one lateral step, if it is time to do it
                nAlienBlockX += bUpdateAnim ? nAlienStep: 0;
            }
            // update alien firing
            for (int i = 0; i < nAlienBlockHeight; ++i)
                for (int j = 0; j < nAlienBlockWidth; ++j) {
                    if (alienState[i*nAlienBlockWidth + j] == 0) { // alien is alive
                        float fRandfireVal = (float)rand() / (float)RAND_MAX;
                        float fProbFire;
                        // prefer firing if aligned with the player; otherwise at random
                        if ( (nAlienBlockX + 6*j) == (int)roundf(fPlayerX) ) fProbFire = 0.20f;
                        else fProbFire = 0.02f;
                        if (fRandfireVal < fProbFire) AlienFire(alienBullets, i, j);
                    }
                }
            // update alien bullets
            for (auto& b: alienBullets)
                if (b.visible) {
                    b.y += fAlienBulletSpeed*fElapsedTime;
                    int nY = (int)roundf(b.y);
                    int nX = (int)roundf(b.x);
                    int nPlayerX = (int)roundf(fPlayerX);
                    // check if a shield has been hit
                    for (auto& shld: shields)
                        if (shld.hit(nX, nY)) b.visible = false;
                    if ( ! bPlayerHit && nY == nScreenHeight && nPlayerX <= nX && nX < (nPlayerX+3) ) {
                        // player has been hit
                        bPlayerHit = true;
                        nLives -= (nLives > 0) ? 1: 0;
                        bGameOver = nLives == 0;
                        b.visible = false;

                    }
                    else if ( nY >= nScreenHeight ) b.visible = false;
                }
            // animate exploding alien
            if (nAlienExploding != -1) {
                // one alien is blowing up
                if (fExplodingElapsed < 0.6f) {
                    fExplodingElapsed += fElapsedTime;
                }
                else {
                    alienState[nAlienExploding] = 2; // dead
                    nAlienExploding = -1;
                    fExplodingElapsed = 0.0f;
                }
            }
            // animate exploding player
            if (bPlayerHit)
                if (fExplodingElapsed < 1.0f)
                    fExplodingElapsed += fElapsedTime;
                else {
                    fExplodingElapsed = 0.0f;
                    bPlayerHit = false;
                }

            // Update screen
            ClearBuffer(screen);
            int nc = swprintf_s(&screen[2], 64, 
                L"Score: %6d   Lives: %2d   FPS: %.1f", nScore, nLives, 1.0f/fElapsedTime);
            screen[2+nc] = ' ';
            DrawShields();
            DrawAliens(nFrameOffset);
            DrawPlayer(bPlayerHit);
            DrawBullets(alienBullets, &bullet);
            WriteConsoleOutputCharacter(hConsole, screen, nScreenWidth * nScreenHeight, { 0,0 }, &dwBytesWritten);
            if (bUpdateAnim) {
                nFrameOffset = nFrameOffset == 3 ? 0 : 3;
                fAnimElapsed = 0.0f;
            }
        }
        swprintf_s(&screen[nScreenWidth*nScreenHeight/2 + nScreenWidth/2 - 20], 40, L"GAME OVER! Press Spacebar to restart.");
        WriteConsoleOutputCharacter(hConsole, screen, nScreenWidth * nScreenHeight, { 0,0 }, &dwBytesWritten);
        // Spacebar: continue, ESC: exit
        while ( ! bQuit && (GetAsyncKeyState((unsigned char)'\x20') & 0x8000) == 0 ) {
            bQuit = (GetAsyncKeyState( (unsigned char)'\x1b') & 0x8000) != 0;
            this_thread::sleep_for(5ms);
        }
    }

	CloseHandle(hConsole);
	cout << "Game Over!!" << endl;
    return 0;
}
