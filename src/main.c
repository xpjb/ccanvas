#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

//====================================================================================
// Program Defines
//====================================================================================
#define CHUNK_SIZE 512
#define CHUNK_LOAD_PADDING 1
#define CHUNK_POOL_RADIUS 4 
#define MAX_CACHED_CHUNKS 512 // Max number of chunks to keep in CPU memory

#define FONT_SIZE 40.0f
#define TEXT_INPUT_MAX 255

//====================================================================================
// Type and Struct Definitions
//====================================================================================

// A chunk that is active and loaded on the GPU
typedef struct CanvasChunk {
    RenderTexture2D texture;
    Vector2 gridPos;
    bool active;
    bool modified;
} CanvasChunk;

// A chunk that is inactive and stored in CPU RAM
typedef struct CachedChunk {
    Image image;
    Vector2 gridPos;
    bool active; // Is this cache slot in use?
} CachedChunk;

typedef struct Canvas {
    CanvasChunk *chunks;      // GPU pool
    int totalChunks;
    CachedChunk *cache;       // CPU cache
    int cacheSize;
} Canvas;

typedef enum {
    TOOL_BRUSH,
    TOOL_TEXT
} ToolType;

typedef struct TextInput {
    char text[TEXT_INPUT_MAX + 1];
    int letterCount;
    bool active;
    Vector2 position;
} TextInput;

typedef struct UIState {
    Color palette[8];
    Rectangle paletteRecs[8];
    Font font;
} UIState;

//====================================================================================
// Canvas Module: Function Declarations
//====================================================================================
Canvas Canvas_Create(void);
void Canvas_Update(Canvas *canvas, Camera2D camera, int screenWidth, int screenHeight);
bool Canvas_BeginTextureMode(Canvas *canvas, Vector2 worldPos);
void Canvas_EndTextureMode(void);
void Canvas_Draw(Canvas canvas);
void Canvas_Destroy(Canvas canvas);
Vector2 WorldToGrid(Vector2 worldPos);
CanvasChunk* GetAndActivateChunk(Canvas *canvas, Vector2 gridPos);


//====================================================================================
// Helper Functions: Function Declarations
//====================================================================================
void HandleCameraControls(Camera2D *camera);
void HandleToolAndDrawing(Canvas *canvas, Camera2D camera, ToolType *currentTool, float *brushSize, Color *currentColor, TextInput *textInput, UIState ui);
void DrawWorld(Canvas canvas, Camera2D camera, ToolType currentTool, float brushSize, TextInput textInput, UIState ui);
void DrawUI(ToolType currentTool, Color currentColor, UIState ui);
Vector2 GetLocalChunkPos(Vector2 worldPos, Vector2 gridPos);

//====================================================================================
// Main Entry Point
//====================================================================================
int main(void) {
    const int screenWidth = 1280;
    const int screenHeight = 720;

    InitWindow(screenWidth, screenHeight, "ccanvas - with caching");
    SetTargetFPS(60);

    Canvas canvas = Canvas_Create();

    Camera2D camera = { 0 };
    camera.target = (Vector2){ 0.0f, 0.0f };
    camera.offset = (Vector2){ screenWidth / 2.0f, screenHeight / 2.0f };
    camera.rotation = 0.0f;
    camera.zoom = 0.5f;

    ToolType currentTool = TOOL_BRUSH;
    float brushSize = 20.0f;
    Color currentColor = BLACK;

    TextInput textInput = { 0 };
    textInput.active = false;
    textInput.text[0] = '\0';

    UIState ui = {
        .palette = { BLACK, RED, ORANGE, YELLOW, GREEN, BLUE, VIOLET, WHITE },
        .font = LoadFont("LiberationSans-Regular.ttf")
    };
    for (int i = 0; i < 8; i++) {
        ui.paletteRecs[i] = (Rectangle){ 10 + 35.0f * i, (float)GetScreenHeight() - 80, 30, 30 };
    }

    while (!WindowShouldClose()) {
        Canvas_Update(&canvas, camera, screenWidth, screenHeight);

        HandleCameraControls(&camera);
        HandleToolAndDrawing(&canvas, camera, &currentTool, &brushSize, &currentColor, &textInput, ui);

        BeginDrawing();
            ClearBackground(DARKGRAY);
            DrawWorld(canvas, camera, currentTool, brushSize, textInput, ui);
            DrawUI(currentTool, currentColor, ui);
        EndDrawing();
    }

    UnloadFont(ui.font);
    Canvas_Destroy(canvas);
    CloseWindow();

    return 0;
}

//====================================================================================
// Helper Functions: Implementations
//====================================================================================

void HandleCameraControls(Camera2D *camera) {
    if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
        Vector2 delta = GetMouseDelta();
        delta = Vector2Scale(delta, -1.0f / camera->zoom);
        camera->target = Vector2Add(camera->target, delta);
    }

    float wheel = GetMouseWheelMove();
    if (wheel != 0 && IsKeyDown(KEY_LEFT_CONTROL)) {
        Vector2 mouseWorldPos = GetScreenToWorld2D(GetMousePosition(), *camera);
        camera->offset = GetMousePosition();
        camera->target = mouseWorldPos;
        const float zoomIncrement = 0.125f;
        camera->zoom += wheel * zoomIncrement;
        if (camera->zoom < zoomIncrement) camera->zoom = zoomIncrement;
    }
}

void StampText(Canvas *canvas, Font font, TextInput *input, Color color) {
    if (Canvas_BeginTextureMode(canvas, input->position)) {
        Vector2 gridPos = WorldToGrid(input->position);
        Vector2 localPos = GetLocalChunkPos(input->position, gridPos);
        DrawTextEx(font, input->text, localPos, FONT_SIZE, 1.0f, color);
        Canvas_EndTextureMode();
    }
    input->active = false;
}

void HandleToolAndDrawing(Canvas *canvas, Camera2D camera, ToolType *currentTool, float *brushSize, Color *currentColor, TextInput *textInput, UIState ui) {
    static Vector2 lastMousePos = { 0 };
    Vector2 mousePos = GetMousePosition();
    Vector2 mouseWorldPos = GetScreenToWorld2D(mousePos, camera);

    bool clickedOnUI = false;
    for (int i = 0; i < 8; i++) {
        if (CheckCollisionPointRec(mousePos, ui.paletteRecs[i])) {
            clickedOnUI = true;
            break;
        }
    }

    if (IsKeyPressed(KEY_B)) { *currentTool = TOOL_BRUSH; textInput->active = false; }
    if (IsKeyPressed(KEY_T)) { *currentTool = TOOL_TEXT; }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && clickedOnUI) {
        for (int i = 0; i < 8; i++) {
            if (CheckCollisionPointRec(mousePos, ui.paletteRecs[i])) {
                *currentColor = ui.palette[i];
            }
        }
    }

    float wheel = GetMouseWheelMove();
    if (wheel != 0 && !IsKeyDown(KEY_LEFT_CONTROL)) {
        *brushSize += wheel * 5;
        if (*brushSize < 2) *brushSize = 2;
        if (*brushSize > 200) *brushSize = 200;
    }

    if (*currentTool == TOOL_BRUSH) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !clickedOnUI) {
            lastMousePos = mouseWorldPos;
        }

        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !clickedOnUI) {
            float dist = Vector2Distance(lastMousePos, mouseWorldPos);
            int steps = 1 + (int)(dist / (*brushSize / 4.0f));

            for (int i = 0; i <= steps; i++) {
                Vector2 drawPos = Vector2Lerp(lastMousePos, mouseWorldPos, (float)i/steps);
                
                if (Canvas_BeginTextureMode(canvas, drawPos)) {
                    Vector2 gridPos = WorldToGrid(drawPos);
                    Vector2 localPos = GetLocalChunkPos(drawPos, gridPos);
                    DrawCircleV(localPos, *brushSize / 2.0f, *currentColor);
                    Canvas_EndTextureMode();
                }
            }
            lastMousePos = mouseWorldPos;
        }
    } else if (*currentTool == TOOL_TEXT) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !clickedOnUI) {
            if (textInput->active) StampText(canvas, ui.font, textInput, *currentColor);
            
            textInput->active = true;
            textInput->letterCount = 0;
            textInput->text[0] = '\0';
            textInput->position = mouseWorldPos;
        }

        if (textInput->active) {
            if (IsKeyPressed(KEY_ENTER)) {
                StampText(canvas, ui.font, textInput, *currentColor);
            } else if (IsKeyPressed(KEY_ESCAPE)) {
                textInput->active = false;
            } else {
                int key = GetCharPressed();
                while (key > 0) {
                    if ((key >= 32) && (key <= 125) && (textInput->letterCount < TEXT_INPUT_MAX)) {
                        textInput->text[textInput->letterCount] = (char)key;
                        textInput->text[textInput->letterCount+1] = '\0';
                        textInput->letterCount++;
                    }
                    key = GetCharPressed();
                }
                if (IsKeyPressedRepeat(KEY_BACKSPACE) && textInput->letterCount > 0) {
                    textInput->letterCount--;
                    textInput->text[textInput->letterCount] = '\0';
                }
            }
        }
    }
}

void DrawWorld(Canvas canvas, Camera2D camera, ToolType currentTool, float brushSize, TextInput textInput, UIState ui) {
    BeginMode2D(camera);
        Canvas_Draw(canvas);

        if (currentTool == TOOL_BRUSH) {
            Vector2 mouseWorldPos = GetScreenToWorld2D(GetMousePosition(), camera);
            DrawCircleLines(mouseWorldPos.x, mouseWorldPos.y, brushSize / 2.0f, GRAY);
        }

        if (textInput.active) {
            DrawTextEx(ui.font, textInput.text, textInput.position, FONT_SIZE, 1.0f, WHITE);
            if (((int)(GetTime() * 2.0f)) % 2 == 0) {
                Vector2 textSize = MeasureTextEx(ui.font, textInput.text, FONT_SIZE, 1.0f);
                DrawRectangle(textInput.position.x + textSize.x, textInput.position.y, 5, FONT_SIZE, WHITE);
            }
        }
    EndMode2D();
}

void DrawUI(ToolType currentTool, Color currentColor, UIState ui) {
    for (int i = 0; i < 8; i++) {
        DrawRectangleRec(ui.paletteRecs[i], ui.palette[i]);
        if (CheckCollisionPointRec(GetMousePosition(), ui.paletteRecs[i])) DrawRectangleLinesEx(ui.paletteRecs[i], 2.0f, LIGHTGRAY);
        if (ColorToInt(currentColor) == ColorToInt(ui.palette[i])) DrawRectangleLinesEx(ui.paletteRecs[i], 3.0f, WHITE);
    }

    DrawFPS(10, 10);
    const char *toolName = (currentTool == TOOL_BRUSH) ? "BRUSH (B)" : "TEXT (T)";
    DrawTextEx(ui.font, TextFormat("Tool: %s", toolName), (Vector2){10, 40}, 20.0f, 1.0f, LIGHTGRAY);
    DrawTextEx(ui.font, "pan: MMB | zoom: Ctrl+scroll | size: scroll", (Vector2){10, (float)GetScreenHeight() - 30}, 20.0f, 1.0f, LIGHTGRAY);
}

Vector2 GetLocalChunkPos(Vector2 worldPos, Vector2 gridPos) {
    return (Vector2) {
        worldPos.x - gridPos.x * CHUNK_SIZE,
        worldPos.y - gridPos.y * CHUNK_SIZE
    };
}

//====================================================================================
// Canvas Module: Implementations
//====================================================================================

Vector2 WorldToGrid(Vector2 worldPos) {
    return (Vector2){ floorf(worldPos.x / CHUNK_SIZE), floorf(worldPos.y / CHUNK_SIZE) };
}

Canvas Canvas_Create(void) {
    Canvas canvas = { 0 };
    int diameter = (CHUNK_POOL_RADIUS * 2) + 1;
    canvas.totalChunks = diameter * diameter;
    canvas.chunks = (CanvasChunk*)malloc(sizeof(CanvasChunk) * canvas.totalChunks);
    for (int i = 0; i < canvas.totalChunks; i++) {
        canvas.chunks[i].active = false;
        canvas.chunks[i].modified = false;
    }

    canvas.cacheSize = MAX_CACHED_CHUNKS;
    canvas.cache = (CachedChunk*)malloc(sizeof(CachedChunk) * canvas.cacheSize);
    for (int i = 0; i < canvas.cacheSize; i++) {
        canvas.cache[i].active = false;
    }

    printf("Canvas created with GPU pool for %d chunks and CPU cache for %d chunks.\n", canvas.totalChunks, canvas.cacheSize);
    return canvas;
}

CanvasChunk* GetAndActivateChunk(Canvas *canvas, Vector2 gridPos) {
    // Check if the chunk is already active in the GPU pool
    for (int i = 0; i < canvas->totalChunks; i++) {
        if (canvas->chunks[i].active && Vector2Equals(canvas->chunks[i].gridPos, gridPos)) {
            return &canvas->chunks[i];
        }
    }

    // If not, find an inactive GPU slot to reuse
    for (int i = 0; i < canvas->totalChunks; i++) {
        if (!canvas->chunks[i].active) {
            CanvasChunk *newChunk = &canvas->chunks[i];
            newChunk->active = true;
            newChunk->gridPos = gridPos;
            newChunk->modified = false;

            // Check if this chunk exists in the CPU cache
            for (int j = 0; j < canvas->cacheSize; j++) {
                if (canvas->cache[j].active && Vector2Equals(canvas->cache[j].gridPos, gridPos)) {
                    printf("Loading chunk (%.0f, %.0f) from cache.\n", gridPos.x, gridPos.y);
                    Texture2D tex = LoadTextureFromImage(canvas->cache[j].image);
                    newChunk->texture = LoadRenderTexture(tex.width, tex.height);
                    
                    // Copy the loaded texture into the render texture
                    BeginTextureMode(newChunk->texture);
                        DrawTexture(tex, 0, 0, WHITE);
                    EndTextureMode();

                    // Now that it's copied, we can unload the intermediate texture
                    UnloadTexture(tex); 

                    // Free the cached image and mark the cache slot as inactive
                    UnloadImage(canvas->cache[j].image);
                    canvas->cache[j].active = false;
                    return newChunk;
                }
            }

            // If not in cache, create a new blank chunk
            printf("Creating new blank chunk at (%.0f, %.0f).\n", gridPos.x, gridPos.y);
            newChunk->texture = LoadRenderTexture(CHUNK_SIZE, CHUNK_SIZE);
            BeginTextureMode(newChunk->texture);
                ClearBackground(RAYWHITE);
            EndTextureMode();
            return newChunk;
        }
    }
    return NULL; 
}

void Canvas_Update(Canvas *canvas, Camera2D camera, int screenWidth, int screenHeight) {
    Vector2 tl = GetScreenToWorld2D((Vector2){0, 0}, camera);
    Vector2 tr = GetScreenToWorld2D((Vector2){(float)screenWidth, 0}, camera);
    Vector2 bl = GetScreenToWorld2D((Vector2){0, (float)screenHeight}, camera);
    Vector2 br = GetScreenToWorld2D((Vector2){(float)screenWidth, (float)screenHeight}, camera);

    float minWorldX = fminf(fminf(tl.x, tr.x), fminf(bl.x, br.x));
    float minWorldY = fminf(fminf(tl.y, tr.y), fminf(bl.y, br.y));
    float maxWorldX = fmaxf(fmaxf(tl.x, tr.x), fmaxf(bl.x, br.x));
    float maxWorldY = fmaxf(fmaxf(tl.y, tr.y), fmaxf(bl.y, br.y));
    
    Vector2 minGrid = WorldToGrid((Vector2){minWorldX, minWorldY});
    Vector2 maxGrid = WorldToGrid((Vector2){maxWorldX, maxWorldY});

    int minX = (int)minGrid.x - CHUNK_LOAD_PADDING;
    int minY = (int)minGrid.y - CHUNK_LOAD_PADDING;
    int maxX = (int)maxGrid.x + CHUNK_LOAD_PADDING;
    int maxY = (int)maxGrid.y + CHUNK_LOAD_PADDING;

    // Deactivate chunks and move them to cache if modified
    for (int i = 0; i < canvas->totalChunks; i++) {
        if (canvas->chunks[i].active) {
            Vector2 pos = canvas->chunks[i].gridPos;
            if (pos.x < minX || pos.x > maxX || pos.y < minY || pos.y > maxY) {
                
                if (canvas->chunks[i].modified) {
                    // Find an empty cache slot
                    bool cached = false;
                    for (int j = 0; j < canvas->cacheSize; j++) {
                        if (!canvas->cache[j].active) {
                            printf("Caching modified chunk (%.0f, %.0f).\n", pos.x, pos.y);
                            Image img = LoadImageFromTexture(canvas->chunks[i].texture.texture);
                            ImageFlipVertical(&img); // FIX: Flip image to correct orientation before caching
                            canvas->cache[j].image = img;
                            canvas->cache[j].gridPos = pos;
                            canvas->cache[j].active = true;
                            cached = true;
                            break;
                        }
                    }
                    if (!cached) {
                        printf("WARNING: CPU cache is full! Could not save chunk (%.0f, %.0f).\n", pos.x, pos.y);
                    }
                }
                
                UnloadRenderTexture(canvas->chunks[i].texture);
                canvas->chunks[i].active = false;
            }
        }
    }

    // Activate chunks in view
    for (int y = minY; y <= maxY; y++) {
        for (int x = minX; x <= maxX; x++) {
            GetAndActivateChunk(canvas, (Vector2){(float)x, (float)y});
        }
    }
}

bool Canvas_BeginTextureMode(Canvas *canvas, Vector2 worldPos) {
    Vector2 gridPos = WorldToGrid(worldPos);
    CanvasChunk *chunk = GetAndActivateChunk(canvas, gridPos);

    if (chunk != NULL) {
        BeginTextureMode(chunk->texture);
        chunk->modified = true; // Mark chunk as modified since we're about to draw on it
        return true;
    }
    
    fprintf(stderr, "WARNING: Could not find or activate chunk for drawing at (%.2f, %.2f)\n", worldPos.x, worldPos.y);
    return false;
}

void Canvas_EndTextureMode(void) {
    EndTextureMode();
}

void Canvas_Draw(Canvas canvas) {
    for (int i = 0; i < canvas.totalChunks; i++) {
        if (canvas.chunks[i].active) {
            Vector2 chunkTopLeft = {
                canvas.chunks[i].gridPos.x * CHUNK_SIZE,
                canvas.chunks[i].gridPos.y * CHUNK_SIZE
            };
            DrawTextureRec(canvas.chunks[i].texture.texture,
                           (Rectangle){ 0, 0, (float)CHUNK_SIZE, -(float)CHUNK_SIZE },
                           chunkTopLeft,
                           WHITE);
        }
    }
}

void Canvas_Destroy(Canvas canvas) {
    // Unload active GPU textures
    for (int i = 0; i < canvas.totalChunks; i++) {
        if (canvas.chunks[i].active) {
            UnloadRenderTexture(canvas.chunks[i].texture);
        }
    }
    free(canvas.chunks);

    // Unload cached CPU images
    for (int i = 0; i < canvas.cacheSize; i++) {
        if (canvas.cache[i].active) {
            UnloadImage(canvas.cache[i].image);
        }
    }
    free(canvas.cache);
}
