#include "raylib.h"
#include "raymath.h"
#include <string.h>
#include "canvas.h"

// Number of colors in the palette
#define COLOR_COUNT 8

// Tool type
typedef enum {
    TOOL_BRUSH,
    TOOL_TEXT
} ToolType;

// Text input state
typedef struct {
    char text[256];
    int letterCount;
    bool active;
    Vector2 position;
} TextInput;

int main(void)
{
    // Initialization
    const int screenWidth = 1280;
    const int screenHeight = 720;

    InitWindow(screenWidth, screenHeight, "ccanvas - a C infinite canvas");
    SetTargetFPS(60);

    // Canvas setup
    Canvas canvas = Canvas_Create();

    // Camera setup
    Camera2D camera = { 0 };
    camera.target = (Vector2){ 0.0f, 0.0f };
    camera.offset = (Vector2){ screenWidth / 2.0f, screenHeight / 2.0f };
    camera.rotation = 0.0f;
    camera.zoom = 0.5f;

    // Tool state
    ToolType currentTool = TOOL_BRUSH;
    float brushSize = 20.0f;
    Vector2 lastMousePos = { 0.0f, 0.0f };
    Color currentColor = BLACK;

    // Color palette
    Color palette[COLOR_COUNT] = {
        BLACK, RED, ORANGE, YELLOW, GREEN, BLUE, VIOLET, WHITE
    };
    Rectangle paletteRecs[COLOR_COUNT];
    for (int i = 0; i < COLOR_COUNT; i++) {
        paletteRecs[i] = (Rectangle){ 10 + 35.0f * i, (float)GetScreenHeight() - 80, 30, 30 };
    }

    // Text input state
    TextInput textInput = { 0 };
    textInput.letterCount = 0;
    textInput.active = false;
    strcpy(textInput.text, "");

    // Load custom font
    Font font = LoadFont("LiberationSans-Regular.ttf");


    // Main game loop
    while (!WindowShouldClose())
    {
        // Update
        Canvas_Update(&canvas, camera);


        // Tool switching
        if (IsKeyPressed(KEY_B)) {
            currentTool = TOOL_BRUSH;
            textInput.active = false; // Cancel text input on tool switch
        }
        if (IsKeyPressed(KEY_T)) currentTool = TOOL_TEXT;

        // Color selection logic
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Vector2 mousePoint = GetMousePosition();
            for (int i = 0; i < COLOR_COUNT; i++) {
                if (CheckCollisionPointRec(mousePoint, paletteRecs[i])) {
                    currentColor = palette[i];
                }
            }
        }


        // Pan logic
        if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE))
        {
            Vector2 delta = GetMouseDelta();
            delta = Vector2Scale(delta, -1.0f / camera.zoom);
            camera.target = Vector2Add(camera.target, delta);
        }

        // Zoom logic (towards mouse pointer)
        float wheel = GetMouseWheelMove();
        if (wheel != 0 && IsKeyDown(KEY_LEFT_CONTROL))
        {
            Vector2 mouseWorldPos = GetScreenToWorld2D(GetMousePosition(), camera);
            camera.offset = GetMousePosition();
            camera.target = mouseWorldPos;
            const float zoomIncrement = 0.125f;
            camera.zoom += wheel * zoomIncrement;
            if (camera.zoom < zoomIncrement) camera.zoom = zoomIncrement;
        }

        // Brush size logic
        if (wheel != 0 && !IsKeyDown(KEY_LEFT_CONTROL))
        {
            brushSize += wheel * 5;
            if (brushSize < 2) brushSize = 2;
            if (brushSize > 200) brushSize = 200;
        }

        Vector2 mousePos = GetScreenToWorld2D(GetMousePosition(), camera);
        bool clickedOnUI = false;
        for (int i = 0; i < COLOR_COUNT; i++) {
            if (CheckCollisionPointRec(GetMousePosition(), paletteRecs[i])) {
                clickedOnUI = true;
                break;
            }
        }

        // Text input logic
        if (currentTool == TOOL_TEXT) {

            // When clicking away to stamp text
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !clickedOnUI) {
                if (textInput.active) { 
                    // ++ ADD THIS: Convert world coordinates to local chunk coordinates ++
                    Vector2 textGridPos = WorldToGrid(textInput.position);
                    Vector2 textLocalPos = { textInput.position.x - textGridPos.x * CHUNK_SIZE, textInput.position.y - textGridPos.y * CHUNK_SIZE };

                    Canvas_BeginTextureMode(&canvas, textInput.position);
                        DrawTextEx(font, textInput.text, textLocalPos, 40.0f, 1.0f, currentColor); // <-- Use textLocalPos
                    Canvas_EndTextureMode();
                }
                // Start new text input
                textInput.active = true;
                textInput.letterCount = 0;
                textInput.text[0] = '\0';
                textInput.position = mousePos;
            }

            if (textInput.active) {
                if (IsKeyPressed(KEY_ENTER)) {
                    // When pressing Enter to stamp text
                    // ++ ADD THIS: Convert world coordinates to local chunk coordinates ++
                    Vector2 textGridPos = WorldToGrid(textInput.position);
                    Vector2 textLocalPos = { textInput.position.x - textGridPos.x * CHUNK_SIZE, textInput.position.y - textGridPos.y * CHUNK_SIZE };
                    Canvas_BeginTextureMode(&canvas, textInput.position); // Stamp text
                        DrawTextEx(font, textInput.text, textInput.position, 40.0f, 1.0f, currentColor);
                    Canvas_EndTextureMode();
                    textInput.active = false;
                } else if (IsKeyPressed(KEY_ESCAPE)) {
                    textInput.active = false; // Cancel text
                } else {
                    // Handle character input
                    int key = GetCharPressed();
                    while (key > 0) {
                        if ((key >= 32) && (key <= 125) && (textInput.letterCount < 255)) {
                            textInput.text[textInput.letterCount] = (char)key;
                            textInput.text[textInput.letterCount+1] = '\0';
                            textInput.letterCount++;
                        }
                        key = GetCharPressed();
                    }
                    if (IsKeyPressedRepeat(KEY_BACKSPACE)) {
                        if (textInput.letterCount > 0) {
                            textInput.letterCount--;
                            textInput.text[textInput.letterCount] = '\0';
                        }
                    }
                }
            }
        }


        // Drawing logic
        if (currentTool == TOOL_BRUSH)
        {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !clickedOnUI)
            {
                lastMousePos = mousePos;
            }

            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !clickedOnUI)
            {
                // We need to handle drawing across chunk boundaries
                // For simplicity, we draw a line as a series of circles for now.
                // A more advanced solution would be to clip geometry against chunk borders.


            float dist = Vector2Distance(lastMousePos, mousePos);
            if (dist > 1.0f) {
                int steps = (int)(dist / (brushSize / 4.0f));
                if (steps < 1) steps = 1;
                for (int i = 0; i <= steps; i++) {
                    Vector2 drawPos = Vector2Lerp(lastMousePos, mousePos, (float)i/steps);
                    
                    // ++ ADD THIS: Convert world coordinates to local chunk coordinates ++
                    Vector2 gridPos = WorldToGrid(drawPos);
                    Vector2 localPos = { drawPos.x - gridPos.x * CHUNK_SIZE, drawPos.y - gridPos.y * CHUNK_SIZE };

                    Canvas_BeginTextureMode(&canvas, drawPos);
                        DrawCircleV(localPos, brushSize / 2.0f, currentColor); // <-- Use localPos here
                    Canvas_EndTextureMode();
                }
            } else {
                // ++ ADD THIS for the single point case as well ++
                Vector2 gridPos = WorldToGrid(mousePos);
                Vector2 localPos = { mousePos.x - gridPos.x * CHUNK_SIZE, mousePos.y - gridPos.y * CHUNK_SIZE };

                Canvas_BeginTextureMode(&canvas, mousePos);
                    DrawCircleV(localPos, brushSize / 2.0f, currentColor); // <-- Use localPos here
                Canvas_EndTextureMode();
            }


                lastMousePos = mousePos;
            }
        }


        // Draw
        BeginDrawing();
            ClearBackground(DARKGRAY);

            BeginMode2D(camera);

                Canvas_Draw(canvas);

                // Draw brush outline
                if (currentTool == TOOL_BRUSH)
                {
                    Vector2 mouseWorldPos = GetScreenToWorld2D(GetMousePosition(), camera);
                    DrawCircleLines(mouseWorldPos.x, mouseWorldPos.y, brushSize / 2.0f, GRAY);
                }

                // Draw active text input (and caret)
                if (textInput.active) {
                    DrawTextEx(font, textInput.text, textInput.position, 40.0f, 1.0f, currentColor);
                    // Simple blinking caret
                    if (((int)(GetTime() * 2.0f)) % 2 == 0) {
                        Vector2 textSize = MeasureTextEx(font, textInput.text, 40.0f, 1.0f);
                        DrawRectangle(textInput.position.x + textSize.x, textInput.position.y, 5, 40, currentColor);
                    }
                }


            EndMode2D();

            // Draw UI
            for (int i = 0; i < COLOR_COUNT; i++) {
                DrawRectangleRec(paletteRecs[i], palette[i]);
                if (CheckCollisionPointRec(GetMousePosition(), paletteRecs[i])) DrawRectangleLinesEx(paletteRecs[i], 2.0f, LIGHTGRAY);
                if (ColorToInt(currentColor) == ColorToInt(palette[i])) DrawRectangleLinesEx(paletteRecs[i], 3.0f, WHITE);
            }


            DrawFPS(10, 10);
            DrawTextEx(font, TextFormat("Tool: %s (%s)", (currentTool == TOOL_BRUSH) ? "BRUSH" : "TEXT", (currentTool == TOOL_BRUSH) ? "B" : "T"), (Vector2){10, 40}, 20.0f, 1.0f, LIGHTGRAY);
            DrawTextEx(font, "pan: MMB | zoom: Ctrl+scroll | size: scroll", (Vector2){10, (float)GetScreenHeight() - 30}, 20.0f, 1.0f, LIGHTGRAY);

        EndDrawing();
    }

    // De-Initialization
    UnloadFont(font);
    Canvas_Destroy(canvas);
    CloseWindow();

    return 0;
}
