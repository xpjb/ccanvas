#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

//--- Defines ---
#define CHUNK_SIZE 1024
#define CHUNK_LOAD_PADDING 1
#define CHUNK_POOL_RADIUS 5
#define MAX_CACHED_CHUNKS 512
#define TEXT_INPUT_MAX 255
#define BASE_FONT_SIZE 256
#define COLOR_PICKER_GAMMA 1.5f
#define MAX_UNDO_ACTIONS 100
#define SAVE_FILE_MAGIC 0x43414E56 // "CANV"
#define SAVE_FILE_VERSION 1

//--- Structs ---
typedef struct CanvasChunk {
    RenderTexture2D texture;
    Vector2 gridPos;
    bool active;
    bool modified;
} CanvasChunk;

typedef struct CachedChunk {
    Image image;
    Vector2 gridPos;
    bool active;
} CachedChunk;

// --- Undo/Redo Structs ---
// Represents the state of a single chunk before a modification
typedef struct UndoChunkState {
    Image beforeImage;
    Vector2 gridPos;
} UndoChunkState;

// Represents a single atomic action (like a brush stroke or text stamp)
typedef struct UndoAction {
    UndoChunkState *chunkStates;
    int numChunks;
} UndoAction;

// Manages the entire undo/redo history
typedef struct UndoState {
    UndoAction undoStack[MAX_UNDO_ACTIONS];
    UndoAction redoStack[MAX_UNDO_ACTIONS];
    int undoCount;
    int redoCount;
    UndoAction *currentAction; // Action currently being recorded
} UndoState;


typedef struct Canvas {
    CanvasChunk *chunks;
    int totalChunks;
    CachedChunk *cache;
    int cacheSize;
    UndoState undoState; // Add undo state to the canvas
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
    Font font;
    Texture2D colorPickerTexture;
    Rectangle colorPickerRect;
    Vector3 selectedHSV; // x: hue, y: saturation, z: value
} UIState;

//--- Canvas Module ---
Canvas Canvas_Create(void);
void Canvas_Update(Canvas *canvas, Camera2D camera, int screenWidth, int screenHeight);
bool Canvas_BeginTextureMode(Canvas *canvas, Vector2 worldPos);
void Canvas_EndTextureMode(void);
void Canvas_Draw(Canvas canvas);
void Canvas_Destroy(Canvas canvas);
Vector2 WorldToGrid(Vector2 worldPos);
CanvasChunk* GetAndActivateChunk(Canvas *canvas, Vector2 gridPos);
void Canvas_Save(Canvas *canvas, const char* path);
void Canvas_Load(Canvas *canvas, const char* path);


//--- Undo/Redo Module ---
void Undo_BeginAction(UndoState *undoState);
void Undo_AddChunkToCurrentAction(Canvas *canvas, UndoState *undoState, Vector2 gridPos);
void Undo_EndAction(UndoState *undoState);
void Undo_PerformUndo(Canvas *canvas, UndoState *undoState);
void Undo_PerformRedo(Canvas *canvas, UndoState *undoState);
void Undo_Destroy(UndoState *undoState);


//--- Helper Functions ---
void HandleCameraControls(Camera2D *camera);
void HandleToolAndDrawing(Canvas *canvas, Camera2D camera, ToolType *currentTool, float *brushSize, float *textSize, Color *currentColor, TextInput *textInput, UIState *ui);
void DrawWorld(Canvas canvas, Camera2D camera, ToolType currentTool, float brushSize, float textSize, TextInput textInput, UIState ui, Color currentColor);
void DrawUI(ToolType currentTool, UIState *ui);
Vector2 GetLocalChunkPos(Vector2 worldPos, Vector2 gridPos);
Image GenImageColorPicker(int width, int height, float hue);

//--- Main Entry Point ---
int main(void) {
    const int screenWidth = 1920;
    const int screenHeight = 1080;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(screenWidth, screenHeight, "canvas.dat"); // Set default filename in title
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);

    Canvas canvas = Canvas_Create();

    Camera2D camera = { 0 };
    camera.target = (Vector2){ 0.0f, 0.0f };
    camera.offset = (Vector2){ GetScreenWidth() / 2.0f, GetScreenHeight() / 2.0f };
    camera.rotation = 0.0f;
    camera.zoom = 0.5f;

    ToolType currentTool = TOOL_BRUSH;
    float brushSize = 20.0f;
    float textSize = 40.0f;
    Color currentColor = BLACK;

    TextInput textInput = { 0 };

    UIState ui = {
        .font = LoadFontEx("LiberationSans-Regular.ttf", BASE_FONT_SIZE, 0, 0),
        .selectedHSV = { 0.0f, 0.0f, 0.0f } // Start with black
    };

    Image colorPickerImage = GenImageColorPicker(450, 450, ui.selectedHSV.x);
    ui.colorPickerTexture = LoadTextureFromImage(colorPickerImage);
    UnloadImage(colorPickerImage);

    char filePath[256] = "canvas.dat"; // Default save path

    while (!WindowShouldClose()) {
        camera.offset = (Vector2){ GetScreenWidth() / 2.0f, GetScreenHeight() / 2.0f };
        ui.colorPickerRect = (Rectangle){ 0, (float)GetScreenHeight() - 450, 450, 450 };

        Canvas_Update(&canvas, camera, GetScreenWidth(), GetScreenHeight());

        HandleCameraControls(&camera);
        HandleToolAndDrawing(&canvas, camera, &currentTool, &brushSize, &textSize, &currentColor, &textInput, &ui);

        // Handle Save/Load
        if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_S)) {
            // NOTE: raylib does not have a GetWindowTitle function.
            // We'll use a fixed file path for now. A more advanced solution
            // would involve implementing a text input box for the filename.
            Canvas_Save(&canvas, filePath);
        }
        if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_L)) {
            Canvas_Load(&canvas, filePath);
        }

        // Handle Undo/Redo with immediate press and key repeat
        if (IsKeyDown(KEY_LEFT_CONTROL)) {
            if (IsKeyPressed(KEY_Z) || IsKeyPressedRepeat(KEY_Z)) {
                Undo_PerformUndo(&canvas, &canvas.undoState);
            }
            if (IsKeyPressed(KEY_Y) || IsKeyPressedRepeat(KEY_Y)) {
                Undo_PerformRedo(&canvas, &canvas.undoState);
            }
        }


        BeginDrawing();
            ClearBackground(DARKGRAY);
            DrawWorld(canvas, camera, currentTool, brushSize, textSize, textInput, ui, currentColor);
            DrawUI(currentTool, &ui);
        EndDrawing();
    }

    UnloadFont(ui.font);
    UnloadTexture(ui.colorPickerTexture);
    Canvas_Destroy(canvas);
    CloseWindow();

    return 0;
}

//--- Helper Implementations ---

Image GenImageColorPicker(int width, int height, float hue) {
    Color *pixels = (Color *)malloc(width*height*sizeof(Color));
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            float saturation = (float)x / (width - 1);
            float linearValue = 1.0f - ((float)y / (height - 1));
            float displayValue = powf(linearValue, COLOR_PICKER_GAMMA);
            pixels[y*width + x] = ColorFromHSV(hue, saturation, displayValue);
        }
    }
    Image image = { .data = pixels, .width = width, .height = height, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, .mipmaps = 1 };
    return image;
}

void HandleCameraControls(Camera2D *camera) {
    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
        Vector2 delta = GetMouseDelta();
        delta = Vector2Scale(delta, -1.0f / camera->zoom);
        camera->target = Vector2Add(camera->target, delta);
    }

    float wheel = GetMouseWheelMove();
    if (wheel != 0 && IsKeyDown(KEY_LEFT_CONTROL)) {
        Vector2 mouseWorldPosBeforeZoom = GetScreenToWorld2D(GetMousePosition(), *camera);
        camera->zoom *= (1.0f + wheel * 0.1f);
        if (camera->zoom < 0.01f) camera->zoom = 0.01f;
        Vector2 mouseWorldPosAfterZoom = GetScreenToWorld2D(GetMousePosition(), *camera);
        camera->target = Vector2Add(camera->target, Vector2Subtract(mouseWorldPosBeforeZoom, mouseWorldPosAfterZoom));
    }
}

void StampText(Canvas *canvas, Font font, TextInput *input, Color color, float textSize) {
    Vector2 textWorldPos = input->position;
    Vector2 measuredSize = MeasureTextEx(font, input->text, textSize, (float)textSize/BASE_FONT_SIZE);
    Vector2 minGrid = WorldToGrid(textWorldPos);
    Vector2 maxGrid = WorldToGrid(Vector2Add(textWorldPos, measuredSize));

    Undo_BeginAction(&canvas->undoState);
    for (int y = (int)minGrid.y; y <= (int)maxGrid.y; y++) {
        for (int x = (int)minGrid.x; x <= (int)maxGrid.x; x++) {
            Undo_AddChunkToCurrentAction(canvas, &canvas->undoState, (Vector2){(float)x, (float)y});
        }
    }
    Undo_EndAction(&canvas->undoState);


    for (int y = (int)minGrid.y; y <= (int)maxGrid.y; y++) {
        for (int x = (int)minGrid.x; x <= (int)maxGrid.x; x++) {
            if (Canvas_BeginTextureMode(canvas, (Vector2){x * CHUNK_SIZE, y * CHUNK_SIZE})) {
                Vector2 localPos = GetLocalChunkPos(textWorldPos, (Vector2){(float)x, (float)y});
                DrawTextEx(font, input->text, localPos, textSize, (float)textSize/BASE_FONT_SIZE, color);
                Canvas_EndTextureMode();
            }
        }
    }
    input->active = false;
}

void HandleToolAndDrawing(Canvas *canvas, Camera2D camera, ToolType *currentTool, float *brushSize, float *textSize, Color *currentColor, TextInput *textInput, UIState *ui) {
    static bool isInteractingWithUI = false;
    Vector2 mousePos = GetMousePosition();
    Vector2 mouseWorldPos = GetScreenToWorld2D(mousePos, camera);

    bool mouseOverUI = CheckCollisionPointRec(mousePos, ui->colorPickerRect);

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && mouseOverUI) isInteractingWithUI = true;
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) isInteractingWithUI = false;

    if (!textInput->active) {
        if (IsKeyPressed(KEY_B)) *currentTool = TOOL_BRUSH;
        if (IsKeyPressed(KEY_T)) *currentTool = TOOL_TEXT;
    }

    if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) && !mouseOverUI) {
        Vector2 gridPos = WorldToGrid(mouseWorldPos);
        for (int i = 0; i < canvas->totalChunks; i++) {
            if (canvas->chunks[i].active && Vector2Equals(canvas->chunks[i].gridPos, gridPos)) {
                Vector2 localPos = GetLocalChunkPos(mouseWorldPos, gridPos);
                Image chunkImage = LoadImageFromTexture(canvas->chunks[i].texture.texture);
                ImageFlipVertical(&chunkImage);
                Color sampledColor = GetImageColor(chunkImage, (int)localPos.x, (int)localPos.y);
                UnloadImage(chunkImage);

                *currentColor = sampledColor;
                ui->selectedHSV = ColorToHSV(sampledColor);

                Image newPickerImage = GenImageColorPicker(ui->colorPickerTexture.width, ui->colorPickerTexture.height, ui->selectedHSV.x);
                UpdateTexture(ui->colorPickerTexture, newPickerImage.data);
                UnloadImage(newPickerImage);
                break;
            }
        }
    }

    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && mouseOverUI) {
        Vector2 localMouse = Vector2Subtract(mousePos, (Vector2){ui->colorPickerRect.x, ui->colorPickerRect.y});
        ui->selectedHSV.y = Clamp(localMouse.x / (ui->colorPickerRect.width - 1), 0.0f, 1.0f); // Saturation
        float linearValue = 1.0f - Clamp(localMouse.y / (ui->colorPickerRect.height - 1), 0.0f, 1.0f);
        ui->selectedHSV.z = powf(linearValue, COLOR_PICKER_GAMMA); // Value
    }
    *currentColor = ColorFromHSV(ui->selectedHSV.x, ui->selectedHSV.y, ui->selectedHSV.z);

    float wheel = GetMouseWheelMove();
    if (wheel != 0 && !IsKeyDown(KEY_LEFT_CONTROL)) {
        if (mouseOverUI) {
            ui->selectedHSV.x -= wheel * 10.0f; // Scroll Hue
            if (ui->selectedHSV.x < 0) ui->selectedHSV.x += 360;
            if (ui->selectedHSV.x >= 360) ui->selectedHSV.x -= 360;
            Image newImage = GenImageColorPicker(ui->colorPickerTexture.width, ui->colorPickerTexture.height, ui->selectedHSV.x);
            UpdateTexture(ui->colorPickerTexture, newImage.data);
            UnloadImage(newImage);
        } else {
            if (*currentTool == TOOL_BRUSH) {
                *brushSize *= (1.0f + wheel * 0.2f);
                if (*brushSize < 2) *brushSize = 2;
                if (*brushSize > 2000) *brushSize = 2000;
            } else if (*currentTool == TOOL_TEXT) {
                *textSize *= (1.0f + wheel * 0.1f);
                if (*textSize < 8) *textSize = 8;
                if (*textSize > 500) *textSize = 500;
            }
        }
    }

    if (*currentTool == TOOL_BRUSH && !isInteractingWithUI) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
             Undo_BeginAction(&canvas->undoState);
        }

        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            static Vector2 lastMousePos = { 0 };
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                lastMousePos = mouseWorldPos;
            }

            float radius = *brushSize / 2.0f;
            
            Vector2 minWorld = {
                fminf(lastMousePos.x, mouseWorldPos.x) - radius,
                fminf(lastMousePos.y, mouseWorldPos.y) - radius
            };
            Vector2 maxWorld = {
                fmaxf(lastMousePos.x, mouseWorldPos.x) + radius,
                fmaxf(lastMousePos.y, mouseWorldPos.y) + radius
            };
            Vector2 minGrid = WorldToGrid(minWorld);
            Vector2 maxGrid = WorldToGrid(maxWorld);

            for (int y = (int)minGrid.y; y <= (int)maxGrid.y; y++) {
                for (int x = (int)minGrid.x; x <= (int)maxGrid.x; x++) {
                    Vector2 currentGridPos = {(float)x, (float)y};
                    Undo_AddChunkToCurrentAction(canvas, &canvas->undoState, currentGridPos);
                    if (Canvas_BeginTextureMode(canvas, (Vector2){x * CHUNK_SIZE, y * CHUNK_SIZE})) {
                        Vector2 localLast = GetLocalChunkPos(lastMousePos, currentGridPos);
                        Vector2 localCurrent = GetLocalChunkPos(mouseWorldPos, currentGridPos);
                        
                        DrawLineEx(localLast, localCurrent, *brushSize, *currentColor);
                        DrawCircleV(localLast, radius, *currentColor);
                        DrawCircleV(localCurrent, radius, *currentColor);
                        
                        Canvas_EndTextureMode();
                    }
                }
            }
            
            lastMousePos = mouseWorldPos;
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            Undo_EndAction(&canvas->undoState);
        }
    } else if (*currentTool == TOOL_TEXT) {
        if (textInput->active) {
            bool stampAndSwitch = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ESCAPE) ||
                                  (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !mouseOverUI && !isInteractingWithUI) ||
                                  IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
            if (stampAndSwitch) {
                if (IsKeyPressed(KEY_ENTER) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    StampText(canvas, ui->font, textInput, *currentColor, *textSize);
                }
                textInput->active = false;
                *currentTool = TOOL_BRUSH;
            } else {
                int key = GetCharPressed();
                while (key > 0) {
                    if ((key >= 32) && (key <= 125) && (textInput->letterCount < TEXT_INPUT_MAX)) {
                        textInput->text[textInput->letterCount++] = (char)key;
                        textInput->text[textInput->letterCount] = '\0';
                    }
                    key = GetCharPressed();
                }
                if (IsKeyPressedRepeat(KEY_BACKSPACE) && textInput->letterCount > 0) {
                    textInput->letterCount--;
                    textInput->text[textInput->letterCount] = '\0';
                }
            }
        } else {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !isInteractingWithUI) {
                textInput->active = true;
                textInput->letterCount = 0;
                textInput->text[0] = '\0';
                textInput->position = mouseWorldPos;
            }
        }
    }
}

void DrawWorld(Canvas canvas, Camera2D camera, ToolType currentTool, float brushSize, float textSize, TextInput textInput, UIState ui, Color currentColor) {
    BeginMode2D(camera);
        Canvas_Draw(canvas);
        if (currentTool == TOOL_BRUSH) {
            Vector2 mouseWorldPos = GetScreenToWorld2D(GetMousePosition(), camera);
            float radius = brushSize / 2.0f;
            DrawRingLines(mouseWorldPos, radius - 2.0f, radius, 0, 360, 32, GRAY);
        }
        if (textInput.active) {
            float spacing = textSize / BASE_FONT_SIZE;
            DrawTextEx(ui.font, textInput.text, textInput.position, textSize, spacing, currentColor);
            if (((int)(GetTime() * 2.0f)) % 2 == 0) {
                Vector2 measuredSize = MeasureTextEx(ui.font, textInput.text, textSize, spacing);
                DrawRectangle(textInput.position.x + measuredSize.x, textInput.position.y, 8, textSize, currentColor);
            }
        }
    EndMode2D();
}

void DrawUI(ToolType currentTool, UIState *ui) {
    DrawTexture(ui->colorPickerTexture, ui->colorPickerRect.x, ui->colorPickerRect.y, WHITE);
    DrawRectangleLinesEx(ui->colorPickerRect, 1.0f, LIGHTGRAY);
    float linearValue = powf(ui->selectedHSV.z, 1.0f / COLOR_PICKER_GAMMA);
    float crosshairX = ui->colorPickerRect.x + ui->selectedHSV.y * (ui->colorPickerRect.width -1);
    float crosshairY = ui->colorPickerRect.y + (1.0f - linearValue) * (ui->colorPickerRect.height - 1);
    DrawLine(crosshairX, crosshairY - 8, crosshairX, crosshairY + 8, WHITE);
    DrawLine(crosshairX - 8, crosshairY, crosshairX + 8, crosshairY, WHITE);
    DrawLine(crosshairX, crosshairY - 7, crosshairX, crosshairY + 7, BLACK);
    DrawLine(crosshairX - 7, crosshairY, crosshairX + 7, crosshairY, BLACK);

    const char *toolName = (currentTool == TOOL_BRUSH) ? "BRUSH (B)" : "TEXT (T)";
    DrawTextEx(ui->font, TextFormat("Tool: %s", toolName), (Vector2){10, 10}, 20.0f, 20.0f/BASE_FONT_SIZE, LIGHTGRAY);
    DrawTextEx(ui->font, "pan: RMB | zoom: Ctrl+scroll | size/hue: scroll", (Vector2){10, 40}, 20.0f, 20.0f/BASE_FONT_SIZE, LIGHTGRAY);
    DrawTextEx(ui->font, "undo: Ctrl+Z | redo: Ctrl+Y | save: Ctrl+S | load: Ctrl+L", (Vector2){10, 70}, 20.0f, 20.0f/BASE_FONT_SIZE, LIGHTGRAY);
}

Vector2 GetLocalChunkPos(Vector2 worldPos, Vector2 gridPos) {
    return (Vector2) { worldPos.x - gridPos.x * CHUNK_SIZE, worldPos.y - gridPos.y * CHUNK_SIZE };
}

//--- Canvas Implementations ---

Vector2 WorldToGrid(Vector2 worldPos) {
    return (Vector2){ floorf(worldPos.x / CHUNK_SIZE), floorf(worldPos.y / CHUNK_SIZE) };
}

Canvas Canvas_Create(void) {
    Canvas canvas = { 0 };
    int diameter = (CHUNK_POOL_RADIUS * 2) + 1;
    canvas.totalChunks = diameter * diameter;
    canvas.chunks = (CanvasChunk*)malloc(sizeof(CanvasChunk) * canvas.totalChunks);
    for (int i = 0; i < canvas.totalChunks; i++) canvas.chunks[i].active = false;
    canvas.cacheSize = MAX_CACHED_CHUNKS;
    canvas.cache = (CachedChunk*)malloc(sizeof(CachedChunk) * canvas.cacheSize);
    for (int i = 0; i < canvas.cacheSize; i++) canvas.cache[i].active = false;
    
    // Initialize UndoState
    canvas.undoState.undoCount = 0;
    canvas.undoState.redoCount = 0;
    canvas.undoState.currentAction = NULL;

    printf("Canvas created with GPU pool for %d chunks and CPU cache for %d chunks.\n", canvas.totalChunks, canvas.cacheSize);
    return canvas;
}

CanvasChunk* GetAndActivateChunk(Canvas *canvas, Vector2 gridPos) {
    for (int i = 0; i < canvas->totalChunks; i++) {
        if (canvas->chunks[i].active && Vector2Equals(canvas->chunks[i].gridPos, gridPos)) return &canvas->chunks[i];
    }
    for (int i = 0; i < canvas->totalChunks; i++) {
        if (!canvas->chunks[i].active) {
            CanvasChunk *newChunk = &canvas->chunks[i];
            newChunk->active = true;
            newChunk->gridPos = gridPos;
            newChunk->modified = false;
            for (int j = 0; j < canvas->cacheSize; j++) {
                if (canvas->cache[j].active && Vector2Equals(canvas->cache[j].gridPos, gridPos)) {
                    printf("Loading chunk (%.0f, %.0f) from cache.\n", gridPos.x, gridPos.y);
                    Texture2D tex = LoadTextureFromImage(canvas->cache[j].image);
                    newChunk->texture = LoadRenderTexture(tex.width, tex.height);
                    BeginTextureMode(newChunk->texture);
                        DrawTexture(tex, 0, 0, WHITE);
                    EndTextureMode();
                    UnloadTexture(tex);
                    UnloadImage(canvas->cache[j].image);
                    canvas->cache[j].active = false;
                    newChunk->modified = true;
                    return newChunk;
                }
            }
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

    for (int i = 0; i < canvas->totalChunks; i++) {
        if (canvas->chunks[i].active) {
            Vector2 pos = canvas->chunks[i].gridPos;
            if (pos.x < minX || pos.x > maxX || pos.y < minY || pos.y > maxY) {
                if (canvas->chunks[i].modified) {
                    bool cached = false;
                    for (int j = 0; j < canvas->cacheSize; j++) {
                        if (!canvas->cache[j].active) {
                            printf("Caching modified chunk (%.0f, %.0f).\n", pos.x, pos.y);
                            Image img = LoadImageFromTexture(canvas->chunks[i].texture.texture);
                            ImageFlipVertical(&img);
                            canvas->cache[j].image = img;
                            canvas->cache[j].gridPos = pos;
                            canvas->cache[j].active = true;
                            cached = true;
                            break;
                        }
                    }
                    if (!cached) printf("WARNING: CPU cache is full! Could not save chunk (%.0f, %.0f).\n", pos.x, pos.y);
                }
                UnloadRenderTexture(canvas->chunks[i].texture);
                canvas->chunks[i].active = false;
            }
        }
    }
    for (int y = minY; y <= maxY; y++) {
        for (int x = minX; x <= maxX; x++) {
            GetAndActivateChunk(canvas, (Vector2){(float)x, (float)y});
        }
    }
}

bool Canvas_BeginTextureMode(Canvas *canvas, Vector2 worldPos) {
    CanvasChunk *chunk = GetAndActivateChunk(canvas, WorldToGrid(worldPos));
    if (chunk != NULL) {
        BeginTextureMode(chunk->texture);
        chunk->modified = true;
        return true;
    }
    fprintf(stderr, "WARNING: Could not activate chunk for drawing at (%.2f, %.2f).\n", worldPos.x, worldPos.y);
    return false;
}

void Canvas_EndTextureMode(void) {
    EndTextureMode();
}

void Canvas_Draw(Canvas canvas) {
    for (int i = 0; i < canvas.totalChunks; i++) {
        if (canvas.chunks[i].active) {
            Vector2 chunkTopLeft = { canvas.chunks[i].gridPos.x * CHUNK_SIZE, canvas.chunks[i].gridPos.y * CHUNK_SIZE };
            DrawTextureRec(canvas.chunks[i].texture.texture, (Rectangle){ 0, 0, (float)CHUNK_SIZE, -(float)CHUNK_SIZE }, chunkTopLeft, WHITE);
        }
    }
}

void Canvas_Destroy(Canvas canvas) {
    for (int i = 0; i < canvas.totalChunks; i++) {
        if (canvas.chunks[i].active) UnloadRenderTexture(canvas.chunks[i].texture);
    }
    free(canvas.chunks);
    for (int i = 0; i < canvas.cacheSize; i++) {
        if (canvas.cache[i].active) UnloadImage(canvas.cache[i].image);
    }
    free(canvas.cache);
    Undo_Destroy(&canvas.undoState);
}

void Canvas_Save(Canvas *canvas, const char* path) {
    FILE *file = fopen(path, "wb");
    if (!file) {
        printf("ERROR: Could not open file '%s' for writing.\n", path);
        return;
    }

    // Write header
    unsigned int magic = SAVE_FILE_MAGIC;
    unsigned int version = SAVE_FILE_VERSION;
    fwrite(&magic, sizeof(unsigned int), 1, file);
    fwrite(&version, sizeof(unsigned int), 1, file);

    // Save active chunks
    for (int i = 0; i < canvas->totalChunks; i++) {
        if (canvas->chunks[i].active && canvas->chunks[i].modified) {
            Image img = LoadImageFromTexture(canvas->chunks[i].texture.texture);
            ImageFlipVertical(&img);
            fwrite(&canvas->chunks[i].gridPos, sizeof(Vector2), 1, file);
            fwrite(img.data, sizeof(Color), CHUNK_SIZE * CHUNK_SIZE, file);
            UnloadImage(img);
        }
    }
    // Save cached chunks
    for (int i = 0; i < canvas->cacheSize; i++) {
        if (canvas->cache[i].active) {
            fwrite(&canvas->cache[i].gridPos, sizeof(Vector2), 1, file);
            fwrite(canvas->cache[i].image.data, sizeof(Color), CHUNK_SIZE * CHUNK_SIZE, file);
        }
    }

    fclose(file);
    printf("Canvas saved to '%s'\n", path);
}

void Canvas_Load(Canvas *canvas, const char* path) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        printf("ERROR: Could not open file '%s' for reading.\n", path);
        return;
    }

    // Read and verify header
    unsigned int magic;
    unsigned int version;
    fread(&magic, sizeof(unsigned int), 1, file);
    fread(&version, sizeof(unsigned int), 1, file);

    if (magic != SAVE_FILE_MAGIC || version != SAVE_FILE_VERSION) {
        printf("ERROR: Invalid save file format or version.\n");
        fclose(file);
        return;
    }

    // Clear existing canvas state
    for (int i = 0; i < canvas->totalChunks; i++) {
        if (canvas->chunks[i].active) {
            UnloadRenderTexture(canvas->chunks[i].texture);
            canvas->chunks[i].active = false;
        }
    }
    for (int i = 0; i < canvas->cacheSize; i++) {
        if (canvas->cache[i].active) {
            UnloadImage(canvas->cache[i].image);
            canvas->cache[i].active = false;
        }
    }
    Undo_Destroy(&canvas->undoState);
    canvas->undoState = (UndoState){0};


    // Load chunks into cache
    int cacheIndex = 0;
    while (!feof(file) && cacheIndex < canvas->cacheSize) {
        Vector2 gridPos;
        if (fread(&gridPos, sizeof(Vector2), 1, file) != 1) break;

        Image img = {
            .data = malloc(CHUNK_SIZE * CHUNK_SIZE * sizeof(Color)),
            .width = CHUNK_SIZE,
            .height = CHUNK_SIZE,
            .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
            .mipmaps = 1
        };
        fread(img.data, sizeof(Color), CHUNK_SIZE * CHUNK_SIZE, file);
        
        canvas->cache[cacheIndex].image = img;
        canvas->cache[cacheIndex].gridPos = gridPos;
        canvas->cache[cacheIndex].active = true;
        cacheIndex++;
    }

    fclose(file);
    printf("Canvas loaded from '%s'\n", path);
}


//--- Undo/Redo Implementations ---

void Undo_BeginAction(UndoState *undoState) {
    if (undoState->currentAction != NULL) {
        // This case should ideally not happen if EndAction is always called.
        // But as a safeguard, we can end the previous action.
        Undo_EndAction(undoState);
    }
    undoState->currentAction = (UndoAction*)calloc(1, sizeof(UndoAction));
}

void Undo_AddChunkToCurrentAction(Canvas *canvas, UndoState *undoState, Vector2 gridPos) {
    if (undoState->currentAction == NULL) return;

    // Check if we've already stored the "before" state for this chunk in this action
    for (int i = 0; i < undoState->currentAction->numChunks; i++) {
        if (Vector2Equals(undoState->currentAction->chunkStates[i].gridPos, gridPos)) {
            return; // Already saved
        }
    }

    CanvasChunk* chunk = GetAndActivateChunk(canvas, gridPos);
    if (chunk) {
        undoState->currentAction->numChunks++;
        undoState->currentAction->chunkStates = (UndoChunkState*)realloc(undoState->currentAction->chunkStates, undoState->currentAction->numChunks * sizeof(UndoChunkState));
        
        Image img = LoadImageFromTexture(chunk->texture.texture);
        ImageFlipVertical(&img); // Important for correct re-drawing
        
        undoState->currentAction->chunkStates[undoState->currentAction->numChunks - 1] = (UndoChunkState){
            .beforeImage = img,
            .gridPos = gridPos
        };
    }
}

void Undo_EndAction(UndoState *undoState) {
    if (undoState->currentAction == NULL || undoState->currentAction->numChunks == 0) {
        if (undoState->currentAction) {
            free(undoState->currentAction);
            undoState->currentAction = NULL;
        }
        return;
    }

    if (undoState->undoCount >= MAX_UNDO_ACTIONS) {
        // Free the oldest action to make space
        UndoAction *oldestAction = &undoState->undoStack[0];
        for (int j = 0; j < oldestAction->numChunks; j++) {
            UnloadImage(oldestAction->chunkStates[j].beforeImage);
        }
        free(oldestAction->chunkStates);

        // Shift everything down
        for (int i = 0; i < MAX_UNDO_ACTIONS - 1; i++) {
            undoState->undoStack[i] = undoState->undoStack[i+1];
        }
        undoState->undoCount--;
    }

    undoState->undoStack[undoState->undoCount++] = *undoState->currentAction;
    
    free(undoState->currentAction);
    undoState->currentAction = NULL;

    // Clear the redo stack
    for(int i = 0; i < undoState->redoCount; i++) {
        for(int j = 0; j < undoState->redoStack[i].numChunks; j++) {
            UnloadImage(undoState->redoStack[i].chunkStates[j].beforeImage);
        }
        free(undoState->redoStack[i].chunkStates);
    }
    undoState->redoCount = 0;
}

void ApplyUndoAction(Canvas *canvas, UndoAction *action) {
    for (int i = 0; i < action->numChunks; i++) {
        UndoChunkState *state = &action->chunkStates[i];
        CanvasChunk* chunk = GetAndActivateChunk(canvas, state->gridPos);
        if (chunk) {
            // The image in the undo state becomes the *new* texture
            Texture2D tex = LoadTextureFromImage(state->beforeImage);
            BeginTextureMode(chunk->texture);
                ClearBackground(RAYWHITE); // Clear to avoid artifacts
                DrawTexture(tex, 0, 0, WHITE);
            EndTextureMode();
            UnloadTexture(tex);
            chunk->modified = true;
        }
    }
}


void Undo_PerformUndo(Canvas *canvas, UndoState *undoState) {
    if (undoState->undoCount == 0) return;

    // 1. Pop the action from the undo stack
    undoState->undoCount--;
    UndoAction actionToUndo = undoState->undoStack[undoState->undoCount];

    // 2. Create a "redo" action to store the *current* state before we undo
    UndoAction redoAction = {0};
    redoAction.numChunks = actionToUndo.numChunks;
    redoAction.chunkStates = (UndoChunkState*)malloc(redoAction.numChunks * sizeof(UndoChunkState));

    for (int i = 0; i < actionToUndo.numChunks; i++) {
        Vector2 gridPos = actionToUndo.chunkStates[i].gridPos;
        CanvasChunk *chunk = GetAndActivateChunk(canvas, gridPos);
        Image currentImage = LoadImageFromTexture(chunk->texture.texture);
        ImageFlipVertical(&currentImage);
        redoAction.chunkStates[i] = (UndoChunkState){ .beforeImage = currentImage, .gridPos = gridPos };
    }

    // 3. Apply the "before" images from the undo action back to the canvas
    ApplyUndoAction(canvas, &actionToUndo);

    // 4. Push the new redo action onto the redo stack
    if (undoState->redoCount < MAX_UNDO_ACTIONS) {
        undoState->redoStack[undoState->redoCount++] = redoAction;
    } else {
        // Redo stack is full, discard this redo action
        for(int i = 0; i < redoAction.numChunks; i++) UnloadImage(redoAction.chunkStates[i].beforeImage);
        free(redoAction.chunkStates);
    }

    // 5. Free the original undo action's images
    for(int i = 0; i < actionToUndo.numChunks; i++) UnloadImage(actionToUndo.chunkStates[i].beforeImage);
    free(actionToUndo.chunkStates);
}

void Undo_PerformRedo(Canvas *canvas, UndoState *undoState) {
    if (undoState->redoCount == 0) return;

    // 1. Pop the action from the redo stack
    undoState->redoCount--;
    UndoAction actionToRedo = undoState->redoStack[undoState->redoCount];

    // 2. Create an "undo" action to store the *current* state before we redo
    UndoAction undoAction = {0};
    undoAction.numChunks = actionToRedo.numChunks;
    undoAction.chunkStates = (UndoChunkState*)malloc(undoAction.numChunks * sizeof(UndoChunkState));

    for (int i = 0; i < actionToRedo.numChunks; i++) {
        Vector2 gridPos = actionToRedo.chunkStates[i].gridPos;
        CanvasChunk *chunk = GetAndActivateChunk(canvas, gridPos);
        Image currentImage = LoadImageFromTexture(chunk->texture.texture);
        ImageFlipVertical(&currentImage);
        undoAction.chunkStates[i] = (UndoChunkState){ .beforeImage = currentImage, .gridPos = gridPos };
    }

    // 3. Apply the "before" images from the redo action back to the canvas
    ApplyUndoAction(canvas, &actionToRedo);

    // 4. Push the new undo action onto the undo stack
    if (undoState->undoCount < MAX_UNDO_ACTIONS) {
        undoState->undoStack[undoState->undoCount++] = undoAction;
    } else {
        // Undo stack is full, discard
        for(int i = 0; i < undoAction.numChunks; i++) UnloadImage(undoAction.chunkStates[i].beforeImage);
        free(undoAction.chunkStates);
    }
    
    // 5. Free the original redo action's images
    for(int i = 0; i < actionToRedo.numChunks; i++) UnloadImage(actionToRedo.chunkStates[i].beforeImage);
    free(actionToRedo.chunkStates);
}


void Undo_Destroy(UndoState *undoState) {
    for (int i = 0; i < undoState->undoCount; i++) {
        for(int j = 0; j < undoState->undoStack[i].numChunks; j++) {
            UnloadImage(undoState->undoStack[i].chunkStates[j].beforeImage);
        }
        free(undoState->undoStack[i].chunkStates);
    }
    for (int i = 0; i < undoState->redoCount; i++) {
        for(int j = 0; j < undoState->redoStack[i].numChunks; j++) {
            UnloadImage(undoState->redoStack[i].chunkStates[j].beforeImage);
        }
        free(undoState->redoStack[i].chunkStates);
    }
}

