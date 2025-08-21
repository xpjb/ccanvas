#include "canvas.h"
#include "raymath.h"
#include "rlgl.h"
#include <stdio.h>
#include <stdlib.h>

// Helper function to convert world coordinates to chunk grid coordinates
Vector2 WorldToGrid(Vector2 worldPos) {
    return (Vector2){
        floorf(worldPos.x / CHUNK_SIZE),
        floorf(worldPos.y / CHUNK_SIZE)
    };
}

// Create and initialize the canvas
Canvas Canvas_Create(void) {
    Canvas canvas = { 0 };
    // Total chunks in our loaded grid (e.g., for view distance 1, it's 3x3=9, for 2 it's 5x5=25)
    int diameter = (CHUNK_VIEW_DISTANCE * 2) + 1;
    canvas.totalChunks = diameter * diameter;
    canvas.chunks = (CanvasChunk*)malloc(sizeof(CanvasChunk) * canvas.totalChunks);

    for (int i = 0; i < canvas.totalChunks; i++) {
        canvas.chunks[i].active = false;
        canvas.chunks[i].modified = false;
        // We don't load the texture yet, we wait for Canvas_Update
    }
    printf("Canvas created with space for %d chunks.\n", canvas.totalChunks);
    return canvas;
}

// Find an inactive chunk to reuse or activate a new one
CanvasChunk* GetAndActivateChunk(Canvas *canvas, Vector2 gridPos) {
    // First, try to find if this chunk is already loaded
    for (int i = 0; i < canvas->totalChunks; i++) {
        if (canvas->chunks[i].active && Vector2Equals(canvas->chunks[i].gridPos, gridPos)) {
            return &canvas->chunks[i];
        }
    }

    // If not found, find an inactive chunk slot to reuse
    for (int i = 0; i < canvas->totalChunks; i++) {
        if (!canvas->chunks[i].active) {
            printf("Activating chunk for grid position: (%.0f, %.0f)\n", gridPos.x, gridPos.y);
            canvas->chunks[i].active = true;
            canvas->chunks[i].gridPos = gridPos;
            canvas->chunks[i].texture = LoadRenderTexture(CHUNK_SIZE, CHUNK_SIZE);
            // Clear new chunks to a default background color
            BeginTextureMode(canvas->chunks[i].texture);
                ClearBackground(RAYWHITE);
            EndTextureMode();
            return &canvas->chunks[i];
        }
    }
    return NULL; // Should not happen if totalChunks is large enough
}


// Update which chunks are active based on camera position
void Canvas_Update(Canvas *canvas, Camera2D camera) {
    Vector2 cameraGridPos = WorldToGrid(camera.target);

    // Deactivate chunks that are too far away
    for (int i = 0; i < canvas->totalChunks; i++) {
        if (canvas->chunks[i].active) {
            if (fabs(canvas->chunks[i].gridPos.x - cameraGridPos.x) > CHUNK_VIEW_DISTANCE ||
                fabs(canvas->chunks[i].gridPos.y - cameraGridPos.y) > CHUNK_VIEW_DISTANCE)
            {
                printf("Deactivating chunk at (%.0f, %.0f)\n", canvas->chunks[i].gridPos.x, canvas->chunks[i].gridPos.y);
                canvas->chunks[i].active = false;
                UnloadRenderTexture(canvas->chunks[i].texture);
                // Here you would save the chunk to a file if it was modified
            }
        }
    }

    // Activate chunks that are now in view
    for (int y = -CHUNK_VIEW_DISTANCE; y <= CHUNK_VIEW_DISTANCE; y++) {
        for (int x = -CHUNK_VIEW_DISTANCE; x <= CHUNK_VIEW_DISTANCE; x++) {
            Vector2 gridPos = { cameraGridPos.x + x, cameraGridPos.y + y };
            GetAndActivateChunk(canvas, gridPos);
        }
    }
}

// Find the correct chunk for a world position and begin texture mode
void Canvas_BeginTextureMode(Canvas *canvas, Vector2 worldPos) {
    Vector2 gridPos = WorldToGrid(worldPos);
    CanvasChunk *chunk = GetAndActivateChunk(canvas, gridPos);

    if (chunk != NULL) {
        BeginTextureMode(chunk->texture);
        
        // We need to transform the world coordinates into the chunk's local coordinates
        // The top-left of the chunk is at (gridPos.x * CHUNK_SIZE, gridPos.y * CHUNK_SIZE)
        
        // --- DELETE THESE THREE LINES ---
        // float localX = worldPos.x - (gridPos.x * CHUNK_SIZE);
        // float localY = worldPos.y - (gridPos.y * CHUNK_SIZE);
        // rlTranslatef(-localX, -localY, 0);

    } else {
        BeginTextureMode(LoadRenderTexture(1,1)); // Dummy texture
    }
}

void Canvas_EndTextureMode(void) {
    EndTextureMode();
}


// Draw all active chunks
void Canvas_Draw(Canvas canvas) {
    for (int i = 0; i < canvas.totalChunks; i++) {
        if (canvas.chunks[i].active) {
            Vector2 chunkTopLeft = {
                canvas.chunks[i].gridPos.x * CHUNK_SIZE,
                canvas.chunks[i].gridPos.y * CHUNK_SIZE
            };
            DrawTextureRec(canvas.chunks[i].texture.texture,
                           (Rectangle){ 0, 0, (float)CHUNK_SIZE, -(float)CHUNK_SIZE }, // Flip Y
                           chunkTopLeft,
                           WHITE);
        }
    }
}

// Destroy the canvas and free memory
void Canvas_Destroy(Canvas canvas) {
    for (int i = 0; i < canvas.totalChunks; i++) {
        if (canvas.chunks[i].active) {
            UnloadRenderTexture(canvas.chunks[i].texture);
        }
    }
    free(canvas.chunks);
}
