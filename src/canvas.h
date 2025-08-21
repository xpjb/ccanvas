#ifndef CANVAS_H
#define CANVAS_H

#include "raylib.h"

// The size of one chunk in pixels (e.g., 512x512)
#define CHUNK_SIZE 512

// The number of chunks visible around the player at any time
// A view distance of 1 means a 3x3 grid (1 chunk in each direction from the center)
#define CHUNK_VIEW_DISTANCE 2

// A single chunk of the canvas
typedef struct CanvasChunk {
    RenderTexture2D texture;
    Vector2 gridPos; // Position in the grid of chunks (e.g., {0,0}, {1,0})
    bool active;
    bool modified; // To know if we need to save it to disk later (not implemented yet)
} CanvasChunk;

// The main canvas structure
typedef struct Canvas {
    CanvasChunk *chunks;
    int totalChunks;
} Canvas;


// Function declarations
Canvas Canvas_Create(void);
void Canvas_Update(Canvas *canvas, Camera2D camera);
void Canvas_BeginTextureMode(Canvas *canvas, Vector2 worldPos);
void Canvas_EndTextureMode(void);
void Canvas_Draw(Canvas canvas);
void Canvas_Destroy(Canvas canvas);
Vector2 WorldToGrid(Vector2 worldPos);


#endif //CANVAS_H
