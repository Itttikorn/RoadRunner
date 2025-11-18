// main.cpp (from model_loading.cpp)

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <learnopengl/filesystem.h>
#include <learnopengl/shader_m.h>
#include <learnopengl/camera.h>
//#include <learnopengl/model.h>
#include <learnopengl/model_animation.h>
#include <learnopengl/animator.h>
#include <learnopengl/animation.h>
#include <stb_image.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <iostream>
#include <vector>
#include <random>
#include <algorithm>
#include <limits>
#include <array>
#include <cmath>
#include <string>
#include <fstream>
#include <iomanip>

// --- irrKlang audio ---
#include <irrKlang/irrKlang.h>
using namespace irrklang;

#include <vector>

static ISoundEngine* g_audioEngine = nullptr;
static ISound* g_ambience = nullptr;
static ISound* g_slide = nullptr;
static ISound* g_running = nullptr;

// track short one-shot sounds so we can drop them cleanly after they finish
static std::vector<ISound*> g_trackedOneShots;

static std::string g_ambiencePath, g_clickPath, g_jumpPath, g_slidePath, g_runningPath, g_failPath;

static bool Audio_Init()
{
    if (g_audioEngine) return true;
    g_audioEngine = createIrrKlangDevice();
    if (!g_audioEngine) {
        std::cerr << "Audio: failed to create irrKlang device\n";
        return false;
    }
    return true;
}

static void Audio_Shutdown()
{
    // stop & drop looped sounds
    if (g_ambience) { g_ambience->stop(); g_ambience->drop(); g_ambience = nullptr; }
    if (g_slide)   { g_slide->stop();   g_slide->drop();   g_slide = nullptr; }
    if (g_running) { g_running->stop(); g_running->drop(); g_running = nullptr; }

    // ensure any tracked one-shots are stopped/dropped
    for (ISound* s : g_trackedOneShots) {
        if (s) { s->stop(); s->drop(); }
    }
    g_trackedOneShots.clear();

    if (g_audioEngine) { g_audioEngine->drop(); g_audioEngine = nullptr; }
}

// call this once per frame from the main loop to clean up finished one-shot sounds
static void Audio_Update()
{
    if (g_trackedOneShots.empty()) return;
    // remove finished sounds (drop them)
    auto it = g_trackedOneShots.begin();
    while (it != g_trackedOneShots.end()) {
        ISound* s = *it;
        if (!s) { it = g_trackedOneShots.erase(it); continue; }
        // isFinished() returns true once playback ended
        if (s->isFinished()) {
            s->drop();           // release the reference
            it = g_trackedOneShots.erase(it);
        } else {
            ++it;
        }
    }
}

static void Audio_LoadFiles(const std::string& ambience,
                            const std::string& click,
                            const std::string& jump,
                            const std::string& slide,
                            const std::string& running,
                            const std::string& fail)
{
    // store paths; call Audio_Init() before load/play
    g_ambiencePath = ambience;
    g_clickPath = click;
    g_jumpPath = jump;
    g_slidePath = slide;
    g_runningPath = running;
    g_failPath = fail;
}

static void Audio_PlayAmbienceLoop()
{
    if (!g_audioEngine || g_ambiencePath.empty()) return;
    if (g_ambience) return; // already playing
    g_ambience = g_audioEngine->play2D(g_ambiencePath.c_str(), true, false, true);
    if (g_ambience) g_ambience->setVolume(0.7f);
}

static void Audio_PlayClick()
{
    if (!g_audioEngine || g_clickPath.empty()) return;
    // track the one-shot so we can drop it cleanly when finished
    ISound* s = g_audioEngine->play2D(g_clickPath.c_str(), false, false, true);
    if (s) {
        s->setVolume(1.0f);
        g_trackedOneShots.push_back(s);
    }
}

static void Audio_PlayJump()
{
    if (!g_audioEngine || g_jumpPath.empty()) return;
    ISound* s = g_audioEngine->play2D(g_jumpPath.c_str(), false, false, true);
    if (s) {
        s->setVolume(1.0f);
        g_trackedOneShots.push_back(s);
    }
}

static void Audio_PlaySlideLoop()
{
    if (!g_audioEngine || g_slidePath.empty()) return;
    // stop running if playing
    if (g_running) { g_running->stop(); g_running->drop(); g_running = nullptr; }
    if (!g_slide) {
        g_slide = g_audioEngine->play2D(g_slidePath.c_str(), true, false, true);
        if (g_slide) g_slide->setVolume(1.0f);
    } else {
        g_slide->setIsPaused(false);
    }
}

static void Audio_StopSlide()
{
    if (g_slide) { g_slide->stop(); g_slide->drop(); g_slide = nullptr; }
}

static void Audio_PlayRunningLoop()
{
    if (!g_audioEngine || g_runningPath.empty()) return;
    // stop slide if playing
    if (g_slide) { g_slide->stop(); g_slide->drop(); g_slide = nullptr; }
    if (!g_running) {
        g_running = g_audioEngine->play2D(g_runningPath.c_str(), true, false, true);
        if (g_running) g_running->setVolume(0.9f);
    } else {
        g_running->setIsPaused(false);
    }
}

static void Audio_StopRunning()
{
    if (g_running) { g_running->stop(); g_running->drop(); g_running = nullptr; }
}

static void Audio_PlayFail()
{
    if (!g_audioEngine || g_failPath.empty()) return;
    ISound* s = g_audioEngine->play2D(g_failPath.c_str(), false, false, true);
    if (s) {
        s->setVolume(1.0f);
        g_trackedOneShots.push_back(s);
    }
}
// --- end irrKlang audio ---


// ===================== Config =====================
static const unsigned int SCR_WIDTH = 1280;
static const unsigned int SCR_HEIGHT = 720;

// Game States
enum class GameState {
    START_SCREEN,
    PLAYING,
    PAUSED,
    GAME_OVER
};

static GameState currentGameState = GameState::START_SCREEN;
static bool startKeyPressed = false;

// UI Button struct
struct Button {
    float x, y;           // Center position in screen coordinates
    float width, height;
    bool isHovered = false;
    bool isPressed = false;

    bool contains(float mouseX, float mouseY) const {
        float left = x - width / 2.0f;
        float right = x + width / 2.0f;
        float bottom = y - height / 2.0f;
        float top = y + height / 2.0f;
        return mouseX >= left && mouseX <= right && mouseY >= bottom && mouseY <= top;
    }
};

// Start button (centered on screen)
static Button startButton = {
    SCR_WIDTH / 2.0f,   // x - center of screen
    SCR_HEIGHT / 2.0f,  // y - center of screen
    200.0f,             // width
    80.0f               // height
};

// Restart button (for game over screen)
static Button restartButton = {
    SCR_WIDTH / 2.0f,   // x - center of screen
    SCR_HEIGHT / 2.0f,  // y - center of screen
    200.0f,             // width
    80.0f               // height
};

// Mouse state
static double mouseX = 0.0;
static double mouseY = 0.0;
static bool mouseButtonPressed = false;

// ===================== Text Rendering =====================
struct Character {
    unsigned int TextureID; // ID handle of the glyph texture
    glm::ivec2   Size;      // Size of glyph
    glm::ivec2   Bearing;   // Offset from baseline to left/top of glyph
    unsigned int Advance;   // Horizontal offset to advance to next glyph
};

static std::map<GLchar, Character> Characters;
static GLuint textVAO, textVBO;
static Shader* textShader = nullptr;

// Player Config

// Player Config
static const float PLAYER_SCALE = 3.75f;
static const float PLAYER_RADIUS = 1.0f * PLAYER_SCALE;
static const float PLAYER_SPAWN_HEIGHT = 1.0f;

// Camera Config
static const float CAM_DISTANCE = 25.0f;
static const float CAM_HEIGHT = 10.0f;
static const float CAM_SMOOTHING = 5.0f;  // Camera smoothing factor (higher = faster follow)

// Scene Config
static const float LANE_Z_SPACING = 15.0f;   // z offset between lanes
static const float SIDEWALK_WIDTH = 12.0f;   // extra width on each side of the road for buildings
static const int   LANE_COUNT = 3;
static const float SECTION_LENGTH = 64.0f;  // length of a road section (equal to 2 buildings)
static const float BUILDING_LENGTH = SECTION_LENGTH / 2.0f;

static const float PLAYER_FORWARD_SPEED = 30.0f; // automatic forward speed (units/sec) along +X
static const float LANE_SWITCH_COOLDOWN = 0.18f; // seconds between lane changes

// Obstacle spawn chances (per lane per section)
static float PROB_CAR = 0.25f;
static float PROB_JUMP = 0.20f;
static float PROB_SLIDE = 0.20f;
static float PROB_WIRES = 0.08f; // special case; if wires spawn, only middle lane spawns them (they cross all lanes)

// Collision threshold along forward axis (X)
static const float OBSTACLE_HIT_AXIS_RADIUS = 1.2f; // distance along X to consider collision
static const float OBSTACLE_RENDER_HEIGHT = 0.0f;

// Crouch render height (when holding S)
static const float PLAYER_CROUCH_HEIGHT = 0.0f;

// Animation states
enum AnimState {
    RUNNING,
    RUNNING_JUMP,
    JUMP,
    JUMP_RUNNING,
    RUNNING_SLIDE,
    SLIDE,
    SLIDE_RUNNING,
    RUNNING_SIDESTEP_LEFT,
    SIDESTEP_LEFT,
    SIDESTEP_LEFT_RUNNING,
    RUNNING_SIDESTEP_RIGHT,
    SIDESTEP_RIGHT,
    SIDESTEP_RIGHT_RUNNING
};

// ===================== State & Helpers =====================
Camera camera(glm::vec3(0.0f, 2.0f, 8.0f));
float deltaTime = 0.f, lastFrame = 0.f;

// Camera smoothing
static glm::vec3 cameraTargetPos = glm::vec3(0.0f, CAM_HEIGHT, -CAM_DISTANCE);

static bool debugCameraEnabled = false;
static bool debugTogglePressed = false;
static float debugYaw = -90.0f;
static float debugPitch = 0.0f;
static const float DEBUG_CAM_SPEED = 40.0f;      // units / second
static const float DEBUG_CAM_TURN_SPEED = 90.0f; // degrees / second

// Mouse-look for debug camera
static bool firstMouse = true;
static float lastX = SCR_WIDTH * 0.5f;
static float lastY = SCR_HEIGHT * 0.5f;
static bool debugMouseCapture = false;

// Random
static std::mt19937 rng(std::random_device{}());
static std::uniform_real_distribution<float> uni01(0.0f, 1.0f);

// Forward declarations
void framebuffer_size_callback(GLFWwindow*, int w, int h);
static void processInput(GLFWwindow* window);
static void updateDebugCameraVectors(Camera& cam);
static void mouse_callback(GLFWwindow* window, double xpos, double ypos);
static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
static void scroll_callback(GLFWwindow* window, double xoff, double yoff);

// Skybox helper
static GLuint loadCubemap(const std::vector<std::string>& faces);

// ===================== Models =====================
// All models are loaded once and instanced in the scene.
// Note: model_animation.h defines the class as "Model", not "Model_animation"
static Model modelPlayer("");           // player
static Model modelSection("");          // road section
static Model modelWires("");            // single wires model

// Multiple variant lists
static std::vector<Model> modelCars;     // Taxi, Police, SUV, TukTuk
static std::vector<Model> modelJumps;    // Cart, TrashBin
static std::vector<Model> modelSlides;   // Barrier
static std::vector<Model> modelBuildings; // Building1, Building2, Building3, Building4

// ===================== Game Structures =====================
enum class ObstacleType { None, Car, Jump, Slide, Wires };

struct Obstacle {
    ObstacleType type = ObstacleType::None;
    glm::vec3    pos;    // world position
    int          lane;   // 0..2 (index into lateral lanes along Z)
    int          variantIndex = -1; // which model variant to render
};

struct Section {
    float centerX;                // center x position of the section (forward axis)
    std::array<Obstacle, LANE_COUNT> laneObstacles; // one obstacle slot per lane
    bool hasWires = false;
    // building variants for this section: left front, left back, right front, right back
    std::array<int, 4> buildingVariants;  // FIXED: Changed ) to >
};

// ===================== Player =====================
struct Player {
    glm::vec3 pos;
    int laneIndex = 1;
    float laneSwitchTimer = 0.0f;

    // Jump / Slide state
    bool isJumping = false;
    float jumpTimer = 0.0f;
    static constexpr float jumpDuration = 0.8f;
    static constexpr float jumpHeight = 5.0f;

    // NEW: Track root motion offset from animation
    glm::vec3 animationRootOffset = glm::vec3(0.0f);
    glm::vec3 lastAnimationRootPos = glm::vec3(0.0f);

    bool isSliding = false;
    float slideTimer = 0.0f;
    static constexpr float slideDuration = 0.6f;  // Fixed duration - slide stops automatically

    // Lane change animation state
    bool isSidestepping = false;
    float sidestepTimer = 0.0f;
    static constexpr float sidestepDuration = 0.3f;  // Duration of sidestep animation
    int sidestepDirection = 0;  // -1 for left, +1 for right, 0 for none
    float sidestepStartZ = 0.0f;  // Starting Z position for interpolation
    float sidestepTargetZ = 0.0f;  // Target Z position for interpolation

    // Crouch state (held while S pressed)
    bool isCrouching = false;

    // Score tracking
    int score = 0;
    float lastScoreUpdateX = 0.0f;

    void startJump() {
        if (!isJumping && !isSliding && !isSidestepping) {
            isJumping = true;
            jumpTimer = 0.0f;
            animationRootOffset = glm::vec3(0.0f);
            lastAnimationRootPos = glm::vec3(0.0f);
        }
    }

    void startSlide() {
        if (!isSliding && !isJumping && !isSidestepping) {
            isSliding = true;
            slideTimer = 0.0f;
            animationRootOffset = glm::vec3(0.0f);
            lastAnimationRootPos = glm::vec3(0.0f);
            std::cout << "*** SLIDE STARTED ***\n";
        }
    }

    void startSidestep(int direction, float currentZ, float targetZ) {
        if (!isSidestepping && !isJumping && !isSliding) {
            isSidestepping = true;
            sidestepTimer = 0.0f;
            sidestepDirection = direction;
            sidestepStartZ = currentZ;
            sidestepTargetZ = targetZ;
            animationRootOffset = glm::vec3(0.0f);
            lastAnimationRootPos = glm::vec3(0.0f);
            std::cout << "*** SIDESTEP " << (direction < 0 ? "LEFT" : "RIGHT") << " STARTED (from " << currentZ << " to " << targetZ << ") ***\n";
        }
    }

    void updateScore(float dt) {
        // Score increases based on distance traveled
        // Every 10 units of distance = 10 points
        float distanceTraveled = pos.x - lastScoreUpdateX;
        if (distanceTraveled >= 10.0f) {
            score += 10;
            lastScoreUpdateX = pos.x;
        }
    }

    void updateTimers(float dt) {
        if (isJumping) {
            jumpTimer += dt;
            if (jumpTimer >= jumpDuration) {
                isJumping = false;
                jumpTimer = 0.0f;
                animationRootOffset = glm::vec3(0.0f);
            }
        }
        if (isSliding) {
            slideTimer += dt;
            if (slideTimer >= slideDuration) {
                isSliding = false;
                slideTimer = 0.0f;
                animationRootOffset = glm::vec3(0.0f);
                std::cout << "*** SLIDE ENDED ***\n";
                Audio_StopSlide();
                Audio_PlayRunningLoop();
            }
        }
        if (isSidestepping) {
            sidestepTimer += dt;
            if (sidestepTimer >= sidestepDuration) {
                isSidestepping = false;
                sidestepTimer = 0.0f;
                sidestepDirection = 0;
                animationRootOffset = glm::vec3(0.0f);
                // Snap to final target position when animation ends
                pos.z = sidestepTargetZ;
                std::cout << "*** SIDESTEP ENDED (final Z: " << pos.z << ") ***\n";
            }
        }
        if (laneSwitchTimer > 0.0f) laneSwitchTimer = std::max(0.0f, laneSwitchTimer - dt);
    }
};

// ===================== World =====================
static std::vector<Section> sections;
static Player player;

// Keep track of which section index the player is currently in (sections are laid out increasing in X)
static int currentSectionIndex = 0;
static const int SECTIONS_AHEAD = 10;

// Game spawn/reset positions
static glm::vec3 worldStartCenter(0.0f, 0.0f, 0.0f);
static glm::vec3 playerSpawnPos;

// Utility: lane Z coordinate (lateral)
static float laneZ(int idx) {
    return (idx - 1) * LANE_Z_SPACING; // idx 0 -> -spacing, 1->0, 2->+spacing
}

// Helper to pick a variant index safely
static int pickVariantIndex(size_t count) {
    if (count == 0) return -1;
    int idx = static_cast<int>(std::floor(uni01(rng) * float(count)));
    if (idx < 0) idx = 0;
    if (idx >= (int)count) idx = (int)count - 1;
    return idx;
}

// Helper to stringify obstacle type
static const char* obstacleTypeName(ObstacleType t) {
    switch (t) {
    case ObstacleType::None: return "None";
    case ObstacleType::Car:  return "Car";
    case ObstacleType::Jump: return "Jump";
    case ObstacleType::Slide: return "Slide";
    case ObstacleType::Wires: return "Wires";
    default: return "Unknown";
    }
}

// ===================== Generation =====================
static Section generateSection(float centerX)
{
    Section s;
    s.centerX = centerX;
    s.hasWires = false;
    // By default fill lanes with None
    for (int i = 0; i < LANE_COUNT; ++i) {
        s.laneObstacles[i].type = ObstacleType::None;
        s.laneObstacles[i].variantIndex = -1;
        s.laneObstacles[i].lane = i;
    }

    // choose building variants for this section (persistent)
    for (int i = 0; i < 4; ++i) s.buildingVariants[i] = pickVariantIndex(modelBuildings.size());

    // Decide wires first (special case)
    if (uni01(rng) < PROB_WIRES) {
        s.hasWires = true;
        Obstacle wires;
        wires.type = ObstacleType::Wires;
        wires.lane = 1;
        wires.variantIndex = -1;
        // generate x offset consistently with other obstacles (sections increase in +X)
        float xOffset = (uni01(rng) - 0.1f) * (SECTION_LENGTH * 0.45f);
        wires.pos = glm::vec3(centerX + xOffset, OBSTACLE_RENDER_HEIGHT, laneZ(1));
        s.laneObstacles[1] = wires;
        return s;
    }

    // Otherwise spawn per-lane obstacles
    for (int lane = 0; lane < LANE_COUNT; ++lane) {
        float r = uni01(rng);
        Obstacle obs;
        obs.lane = lane;
        obs.pos.z = laneZ(lane); // lateral position along Z
        obs.pos.y = OBSTACLE_RENDER_HEIGHT;
        float xOffset = (uni01(rng) - 0.1f) * (SECTION_LENGTH * 0.45f);
        obs.pos.x = centerX + xOffset;
        obs.variantIndex = -1;

        if (r < PROB_CAR) {
            obs.type = ObstacleType::Car;
            obs.variantIndex = pickVariantIndex(modelCars.size());
        }
        else if (r < PROB_CAR + PROB_JUMP) {
            obs.type = ObstacleType::Jump;
            obs.variantIndex = pickVariantIndex(modelJumps.size());
        }
        else if (r < PROB_CAR + PROB_JUMP + PROB_SLIDE) {
            obs.type = ObstacleType::Slide;
            obs.variantIndex = pickVariantIndex(modelSlides.size());
        }
        else {
            obs.type = ObstacleType::None;
        }
        s.laneObstacles[lane] = obs;
    }

    // Ensure we don't spawn cars in all lanes for the same section.
    // If all lanes were assigned Car, clear one random lane to None.
    int carCount = 0;
    for (int lane = 0; lane < LANE_COUNT; ++lane) {
        if (s.laneObstacles[lane].type == ObstacleType::Car) ++carCount;
    }
    if (carCount == LANE_COUNT) {
        int laneToClear = static_cast<int>(std::floor(uni01(rng) * float(LANE_COUNT)));
        if (laneToClear < 0) laneToClear = 0;
        if (laneToClear >= LANE_COUNT) laneToClear = LANE_COUNT - 1;
        s.laneObstacles[laneToClear].type = ObstacleType::None;
        s.laneObstacles[laneToClear].variantIndex = -1;
    }

    return s;
}

static void generateSectionsUpTo(float playerX)
{
    if (sections.empty()) {
        // align the first generated section so the player is inside it
        float startCenterX = std::floor(playerX / SECTION_LENGTH) * SECTION_LENGTH + SECTION_LENGTH * 0.5f;
        sections.push_back(generateSection(startCenterX));

        // ensure first section has no obstacles (for safe spawn / debugging)
        Section& first = sections.front();
        first.hasWires = false;
        for (int i = 0; i < LANE_COUNT; ++i) {
            first.laneObstacles[i].type = ObstacleType::None;
            first.laneObstacles[i].variantIndex = -1;
            first.laneObstacles[i].pos.x = first.centerX; // center
            first.laneObstacles[i].pos.y = 0.0f;
            first.laneObstacles[i].pos.z = laneZ(i);
        }

        for (int i = 1; i <= SECTIONS_AHEAD; ++i) {
            sections.push_back(generateSection(startCenterX + i * SECTION_LENGTH));
        }
        currentSectionIndex = 0;
        return;
    }

    // find which section player is in (index into sections)
    int playerSection = -1;
    for (size_t i = 0; i < sections.size(); ++i) {
        float cx = sections[i].centerX;
        if (playerX <= cx + SECTION_LENGTH / 2.0f && playerX >= cx - SECTION_LENGTH / 2.0f) { playerSection = (int)i; break; }
    }
    if (playerSection == -1) playerSection = 0;
    currentSectionIndex = playerSection;

    // Ensure we keep existing sections intact and only append the needed sections at the back.
    // Desired size = index of current section + SECTIONS_AHEAD + 1 (current + ahead)
    int desiredSize = currentSectionIndex + SECTIONS_AHEAD + 1;
    // Append new sections until we reach desired size; do not regenerate existing sections.
    while ((int)sections.size() < desiredSize) {
        float lastCenterX = sections.back().centerX;
        float newX = lastCenterX + SECTION_LENGTH;
        sections.push_back(generateSection(newX));
    }

    // Remove old sections at front if player moved far ahead
    while (sections.size() > 0) {
        float firstCenter = sections.front().centerX;
        if (playerX > firstCenter + SECTION_LENGTH * 3.5f) {
            sections.erase(sections.begin());
            if (currentSectionIndex > 0) --currentSectionIndex;
        }
        else break;
    }
}

// ===================== Collision & Game Reset =====================

// Helper: decide if player state allows passing a given obstacle
static bool playerCanPassObstacle(const Player& p, const Obstacle& obs)
{
    switch (obs.type) {
    case ObstacleType::None:
        return true;
    case ObstacleType::Jump:
        // this obstacle requires jumping to avoid
        return p.isJumping;
    case ObstacleType::Slide:
        // can pass by sliding or by crouching (holding S)
        return p.isSliding || p.isCrouching;
    case ObstacleType::Wires:
        // wires cross lanes; can avoid by sliding or crouching
        return p.isSliding || p.isCrouching;
    default:
        return false;
    }
}

static bool checkHitObstacle(const Player& p)
{
    for (const auto& s : sections) {
        // Wires are special (center lane) and affect all lanes when present
        if (s.hasWires) {
            const Obstacle& w = s.laneObstacles[1];
            float dx = std::fabs(p.pos.x - w.pos.x);
            if (dx < OBSTACLE_HIT_AXIS_RADIUS) {
                if (!playerCanPassObstacle(p, w)) return true;
            }
        }

        // Only check player's lane obstacle for other obstacle types
        int lane = p.laneIndex;
        const Obstacle& obs = s.laneObstacles[lane];
        if (obs.type == ObstacleType::None || obs.type == ObstacleType::Wires) continue;

        float dx = std::fabs(p.pos.x - obs.pos.x);
        if (dx < OBSTACLE_HIT_AXIS_RADIUS) {
            if (!playerCanPassObstacle(p, obs)) return true;
        }
    }
    return false;
}

static void resetGame()
{
    std::cout << "Game Over! Score: " << player.score << "\n";
    Audio_PlayFail();
    Audio_StopRunning();
    Audio_StopSlide();
    // Don't reset immediately - go to game over screen
    currentGameState = GameState::GAME_OVER;
}

// Function to restart game from game over screen
static void restartGameFromGameOver()
{
    std::cout << "Restarting game...\n";
    player.laneIndex = 1;
    player.pos = playerSpawnPos;
    player.isJumping = false;
    player.jumpTimer = 0.0f;
    player.isSliding = false;
    player.slideTimer = 0.0f;
    player.laneSwitchTimer = 0.0f;
    player.score = 0;
    player.lastScoreUpdateX = playerSpawnPos.x;
    sections.clear();
    generateSectionsUpTo(player.pos.x);
    currentGameState = GameState::PLAYING;
    Audio_PlayRunningLoop();
}

// ===================== Input & Debug Camera =====================

// ===================== Input & Debug Camera =====================
static void updateDebugCameraVectors(Camera& cam)
{
    glm::vec3 front;
    front.x = cos(glm::radians(debugYaw)) * cos(glm::radians(debugPitch));
    front.y = sin(glm::radians(debugPitch));
    front.z = sin(glm::radians(debugYaw)) * cos(glm::radians(debugPitch));
    cam.Front = glm::normalize(front);
    cam.Right = glm::normalize(glm::cross(cam.Front, cam.WorldUp));
    cam.Up = glm::normalize(glm::cross(cam.Right, cam.Front));
    cam.Yaw = debugYaw;
    cam.Pitch = debugPitch;
}

// Add these static variables near the top with other input state
static bool jumpKeyPressed = false;
static bool slideKeyPressed = false;
static bool leftKeyPressed = false;
static bool rightKeyPressed = false;

static void processInput(GLFWwindow* window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(window, true);

    // Handle start screen
    if (currentGameState == GameState::START_SCREEN) {
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS && !startKeyPressed) {
            startKeyPressed = true;
            currentGameState = GameState::PLAYING;
            std::cout << "Game Started!\n";
            Audio_PlayClick();
            Audio_PlayRunningLoop();
        }
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_RELEASE) {
            startKeyPressed = false;
        }
        return; // Don't process other inputs on start screen
    }

    // Handle game over screen
    if (currentGameState == GameState::GAME_OVER) {
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS && !startKeyPressed) {
            startKeyPressed = true;
            restartGameFromGameOver();
            std::cout << "Game Restarted!\n";
            Audio_PlayClick();
            Audio_PlayRunningLoop();
        }
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_RELEASE) {
            startKeyPressed = false;
        }
        return; // Don't process other inputs on game over screen
    }

    // Toggle debug camera with F1 (debounced)
    if (glfwGetKey(window, GLFW_KEY_F1) == GLFW_PRESS && !debugTogglePressed) {
        debugTogglePressed = true;
        debugCameraEnabled = !debugCameraEnabled;
        firstMouse = true;
        if (debugCameraEnabled) {
            std::cout << "Debug camera: ON\n";
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            debugYaw = camera.Yaw;
            debugPitch = camera.Pitch;
            updateDebugCameraVectors(camera);
            debugMouseCapture = true;
        }
        else {
            std::cout << "Debug camera: OFF\n";
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            debugMouseCapture = false;
        }
    }
    if (glfwGetKey(window, GLFW_KEY_F1) == GLFW_RELEASE) debugTogglePressed = false;

    if (debugCameraEnabled) {
        float moveSpeed = DEBUG_CAM_SPEED * deltaTime;
        float turnSpeed = DEBUG_CAM_TURN_SPEED * deltaTime;
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) camera.Position += camera.Front * moveSpeed;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) camera.Position -= camera.Front * moveSpeed;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) camera.Position -= camera.Right * moveSpeed;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) camera.Position += camera.Right * moveSpeed;
        if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS) camera.Position += camera.WorldUp * moveSpeed;
        if (glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS) camera.Position -= camera.WorldUp * moveSpeed;
        if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) debugYaw -= turnSpeed;
        if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) debugYaw += turnSpeed;
        if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) debugPitch += turnSpeed;
        if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) debugPitch -= turnSpeed;
        if (debugPitch > 89.0f) debugPitch = 89.0f;
        if (debugPitch < -89.0f) debugPitch = -89.0f;
        updateDebugCameraVectors(camera);
        return; // don't process gameplay input while debugging
    }

    // Gameplay input (lane switching, jump, slide, crouch)
    if (player.laneSwitchTimer <= 0.0f) {
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
            if (!leftKeyPressed && player.laneIndex > 0) {
                int newLane = player.laneIndex - 1;
                float targetZ = laneZ(newLane);
                player.startSidestep(-1, player.pos.z, targetZ);  // Pass current and target Z
                player.laneIndex = newLane;
                player.laneSwitchTimer = LANE_SWITCH_COOLDOWN;
                leftKeyPressed = true;
            }
        }
        else {
            leftKeyPressed = false;
        }

        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
            if (!rightKeyPressed && player.laneIndex < LANE_COUNT - 1) {
                int newLane = player.laneIndex + 1;
                float targetZ = laneZ(newLane);
                player.startSidestep(1, player.pos.z, targetZ);  // Pass current and target Z
                player.laneIndex = newLane;
                player.laneSwitchTimer = LANE_SWITCH_COOLDOWN;
                rightKeyPressed = true;
            }
        }
        else {
            rightKeyPressed = false;
        }
    }

    // Jump (edge-triggered: only trigger on key DOWN, not while held)
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
        if (!jumpKeyPressed) {
            player.startJump();
            jumpKeyPressed = true;
            std::cout << "Jump key pressed!\n"; // Debug
            Audio_PlayJump();
        }
    }
    else {
        jumpKeyPressed = false;
    }

    // REVERTED: Slide is now edge-triggered again (one press = fixed duration animation)
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
        if (!slideKeyPressed) {
            player.startSlide();
            slideKeyPressed = true;
            std::cout << "Slide key pressed!\n"; // Debug
            Audio_PlaySlideLoop();
        }
    }
    else {
        slideKeyPressed = false;
    }
}

// Mouse callback for debug mouse-look
// Mouse callback for debug mouse-look
static void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
    // Store mouse position for UI interactions
    mouseX = xpos;
    mouseY = SCR_HEIGHT - ypos; // Flip Y coordinate (OpenGL uses bottom-left origin)

    // Update button hover state when on start screen
    if (currentGameState == GameState::START_SCREEN) {
        startButton.isHovered = startButton.contains((float)mouseX, (float)mouseY);
        return;
    }

    // Update button hover state when on game over screen
    if (currentGameState == GameState::GAME_OVER) {
        restartButton.isHovered = restartButton.contains((float)mouseX, (float)mouseY);
        return;
    }

    if (!debugCameraEnabled || !debugMouseCapture) return;

    float x = (float)xpos;
    float y = (float)ypos;

    if (firstMouse) { lastX = x; lastY = y; firstMouse = false; }

    float xoffset = x - lastX;
    float yoffset = lastY - y; // reversed: y ranges bottom->top
    lastX = x; lastY = y;

    float sensitivity = 0.1f;
    xoffset *= sensitivity;
    yoffset *= sensitivity;

    debugYaw += xoffset;
    debugPitch += yoffset;

    if (debugPitch > 89.0f) debugPitch = 89.0f;
    if (debugPitch < -89.0f) debugPitch = -89.0f;

    updateDebugCameraVectors(camera);
}

// Mouse button callback for UI interactions
static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            mouseButtonPressed = true;

            // Check if start button was clicked
            if (currentGameState == GameState::START_SCREEN && startButton.isHovered) {
                startButton.isPressed = true;
            }

            // Check if restart button was clicked
            if (currentGameState == GameState::GAME_OVER && restartButton.isHovered) {
                restartButton.isPressed = true;
            }
        }
        else if (action == GLFW_RELEASE) {
            mouseButtonPressed = false;

            // Handle button click on release (only if still hovering)
            if (currentGameState == GameState::START_SCREEN &&
                startButton.isPressed && startButton.isHovered) {
                currentGameState = GameState::PLAYING;
                std::cout << "Game Started via Button Click!\n";
                Audio_PlayClick();
                Audio_PlayRunningLoop();
            }

            // Handle restart button click
            if (currentGameState == GameState::GAME_OVER &&
                restartButton.isPressed && restartButton.isHovered) {
                restartGameFromGameOver();
                std::cout << "Game Restarted via Button Click!\n";
                Audio_PlayClick();
                Audio_PlayRunningLoop();
            }

            startButton.isPressed = false;
            restartButton.isPressed = false;
        }
    }
}

// Scroll callback for camera zoom (debug mode only)
static void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    if (debugCameraEnabled) {
        camera.ProcessMouseScroll(static_cast<float>(yoffset));
    }
    // Ignore scroll input when not in debug camera mode
}


// ===================== Rendering helpers =====================
static void drawModelAt(Shader& shader, Model& m, const glm::vec3& pos, float yawDeg = 0.0f, float scale = 1.0f)
{
    glm::mat4 M(1.0f);
    M = glm::translate(M, pos);
    if (yawDeg != 0.0f) M = glm::rotate(M, glm::radians(yawDeg), glm::vec3(0, 1, 0));
    M = glm::scale(M, glm::vec3(scale));
    shader.setMat4("model", M);
    m.Draw(shader); // Model::Draw expects Shader& in model_animation.h
}

// Render a button
static void renderButton(Shader& uiShader, GLuint VAO, const Button& btn) {
    uiShader.use();

    // Set color based on button state
    glm::vec3 buttonColor;
    if (btn.isPressed && btn.isHovered) {
        buttonColor = glm::vec3(0.2f, 0.5f, 0.2f); // Dark green when pressed
    }
    else if (btn.isHovered) {
        buttonColor = glm::vec3(0.3f, 0.7f, 0.3f); // Light green when hovered
    }
    else {
        buttonColor = glm::vec3(0.2f, 0.6f, 0.2f); // Normal green
    }

    uiShader.setVec3("color", buttonColor);

    // Create orthographic projection for screen coordinates
    glm::mat4 projection = glm::ortho(0.0f, (float)SCR_WIDTH, 0.0f, (float)SCR_HEIGHT);
    uiShader.setMat4("projection", projection);

    // Update button vertices
    float left = btn.x - btn.width / 2.0f;
    float right = btn.x + btn.width / 2.0f;
    float bottom = btn.y - btn.height / 2.0f;
    float top = btn.y + btn.height / 2.0f;

    float vertices[] = {
        left, bottom,
        right, bottom,
        right, top,
        left, bottom,
        right, top,
        left, top
    };

    GLuint VBO;
    glGenBuffers(1, &VBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindVertexArray(0);
    glDeleteBuffers(1, &VBO);
}

// ===================== Text Rendering Functions =====================

// Initialize FreeType and load font
static bool initTextRendering() {
    // FreeType
    FT_Library ft;
    if (FT_Init_FreeType(&ft)) {
        std::cerr << "ERROR::FREETYPE: Could not init FreeType Library" << std::endl;
        return false;
    }

    // Load font as face
    FT_Face face;
    std::string fontPath = FileSystem::getPath("resources/fonts/Antonio-Regular.ttf");
    if (FT_New_Face(ft, fontPath.c_str(), 0, &face)) {
        std::cerr << "ERROR::FREETYPE: Failed to load font at: " << fontPath << std::endl;
        FT_Done_FreeType(ft);
        return false;
    }

    // Set size to load glyphs as
    FT_Set_Pixel_Sizes(face, 0, 48);

    // Disable byte-alignment restriction
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // Load first 128 characters of ASCII set
    for (unsigned char c = 0; c < 128; c++) {
        // Load character glyph 
        if (FT_Load_Char(face, c, FT_LOAD_RENDER)) {
            std::cerr << "ERROR::FREETYPE: Failed to load Glyph for character " << c << std::endl;
            continue;
        }

        // Generate texture
        unsigned int texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RED,
            face->glyph->bitmap.width,
            face->glyph->bitmap.rows,
            0,
            GL_RED,
            GL_UNSIGNED_BYTE,
            face->glyph->bitmap.buffer
        );

        // Set texture options
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        // Now store character for later use
        Character character = {
            texture,
            glm::ivec2(face->glyph->bitmap.width, face->glyph->bitmap.rows),
            glm::ivec2(face->glyph->bitmap_left, face->glyph->bitmap_top),
            static_cast<unsigned int>(face->glyph->advance.x)
        };
        Characters.insert(std::pair<char, Character>(c, character));
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    // Destroy FreeType once we're finished
    FT_Done_Face(face);
    FT_Done_FreeType(ft);

    // Configure VAO/VBO for texture quads
    glGenVertexArrays(1, &textVAO);
    glGenBuffers(1, &textVBO);
    glBindVertexArray(textVAO);
    glBindBuffer(GL_ARRAY_BUFFER, textVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    std::cout << "Text rendering initialized successfully\n";
    return true;
}

// Render text
static void RenderText(Shader& shader, std::string text, float x, float y, float scale, glm::vec3 color) {
    // Activate corresponding render state	
    shader.use();
    glUniform3f(glGetUniformLocation(shader.ID, "textColor"), color.x, color.y, color.z);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(textVAO);

    // Iterate through all characters
    std::string::const_iterator c;
    for (c = text.begin(); c != text.end(); c++) {
        Character ch = Characters[*c];

        float xpos = x + ch.Bearing.x * scale;
        float ypos = y - (ch.Size.y - ch.Bearing.y) * scale;

        float w = ch.Size.x * scale;
        float h = ch.Size.y * scale;

        // Update VBO for each character
        float vertices[6][4] = {
            { xpos,     ypos + h,   0.0f, 0.0f },
            { xpos,     ypos,       0.0f, 1.0f },
            { xpos + w, ypos,       1.0f, 1.0f },

            { xpos,     ypos + h,   0.0f, 0.0f },
            { xpos + w, ypos,       1.0f, 1.0f },
            { xpos + w, ypos + h,   1.0f, 0.0f }
        };

        // Render glyph texture over quad
        glBindTexture(GL_TEXTURE_2D, ch.TextureID);

        // Update content of VBO memory
        glBindBuffer(GL_ARRAY_BUFFER, textVBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        // Render quad
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // Now advance cursors for next glyph (note that advance is number of 1/64 pixels)
        x += (ch.Advance >> 6) * scale; // Bitshift by 6 to get value in pixels (2^6 = 64)
    }
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// Helper function to calculate the actual width of text for proper centering
static float GetTextWidth(const std::string& text, float scale) {
    float width = 0.0f;
    for (char c : text) {
        Character ch = Characters[c];
        width += (ch.Advance >> 6) * scale;
    }
    return width;
}

// ===================== Skybox loader =====================
static GLuint loadCubemap(const std::vector<std::string>& faces)
{
    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);

    int width, height, nrChannels;
    for (unsigned int i = 0; i < faces.size(); ++i) {
        unsigned char* data = stbi_load(faces[i].c_str(), &width, &height, &nrChannels, 0);
        if (data) {
            GLenum format = GL_RGB;
            if (nrChannels == 1) format = GL_RED;
            else if (nrChannels == 3) format = GL_RGB;
            else if (nrChannels == 4) format = GL_RGBA;
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
                0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
            stbi_image_free(data);
        }
        else {
            std::cerr << "Cubemap texture failed to load at path: " << faces[i] << std::endl;
            stbi_image_free(data);
        }
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    return textureID;
}

// ===================== Callbacks =====================
void framebuffer_size_callback(GLFWwindow*, int w, int h) { glViewport(0, 0, w, h); }

// ===================== Main =====================
int main()
{
    if (!glfwInit()) { std::cerr << "Failed to init GLFW\n"; return -1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "RoadRunner", nullptr, nullptr);
    if (!window) { std::cerr << "Failed to create window\n"; glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetScrollCallback(window, scroll_callback);

    // start with cursor visible; debug mode will capture it
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) { std::cerr << "Failed to init GLAD\n"; return -1; }

    // Default: models/textures loaded with vertical flip enabled (most model textures expect this)
    stbi_set_flip_vertically_on_load(true);

    glEnable(GL_DEPTH_TEST);

    Shader shader(
        FileSystem::getPath("src/3.model_loading/1.model_loading/main.vs").c_str(),
        FileSystem::getPath("src/3.model_loading/1.model_loading/main.fs").c_str()
    );
    shader.use();
    shader.setInt("texture_diffuse1", 0);

    Shader skyboxShader(
        FileSystem::getPath("src/3.model_loading/1.model_loading/skybox.vs").c_str(),
        FileSystem::getPath("src/3.model_loading/1.model_loading/skybox.fs").c_str()
    );
    skyboxShader.use();
    skyboxShader.setInt("skybox", 0);

    // Depth/shadow shader and shadow map setup
    Shader depthShader(
        FileSystem::getPath("src/3.model_loading/1.model_loading/depth.vs").c_str(),
        FileSystem::getPath("src/3.model_loading/1.model_loading/depth.fs").c_str()
    );

    // Shadow map size
    const unsigned int SHADOW_WIDTH = 2048, SHADOW_HEIGHT = 2048;
    GLuint depthMapFBO;
    glGenFramebuffers(1, &depthMapFBO);

    // Create depth texture
    GLuint depthMap;
    glGenTextures(1, &depthMap);
    glBindTexture(GL_TEXTURE_2D, depthMap);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, SHADOW_WIDTH, SHADOW_HEIGHT, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

    // Attach depth texture as FBO's depth buffer
    glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthMap, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "Shadow Framebuffer not complete!\n";
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Create UI shader from external files
    Shader uiShader(
        FileSystem::getPath("src/3.model_loading/1.model_loading/ui.vs").c_str(),
        FileSystem::getPath("src/3.model_loading/1.model_loading/ui.fs").c_str()
    );
    GLuint buttonVAO;
    glGenVertexArrays(1, &buttonVAO);

    // Initialize text rendering system
    textShader = new Shader(
        FileSystem::getPath("src/3.model_loading/1.model_loading/text.vs").c_str(),
        FileSystem::getPath("src/3.model_loading/1.model_loading/text.fs").c_str()
    );

    if (!initTextRendering()) {
        std::cerr << "Failed to initialize text rendering\n";
        return -1;
    }

    // Set up text shader projection (orthographic for screen space)
    glm::mat4 textProjection = glm::ortho(0.0f, (float)SCR_WIDTH, 0.0f, (float)SCR_HEIGHT);
    textShader->use();
    glUniformMatrix4fv(glGetUniformLocation(textShader->ID, "projection"), 1, GL_FALSE, glm::value_ptr(textProjection));

    // Load Models (validate paths)
    const std::string base = "resources/objects";
    auto resolveAndCheck = [&](const std::string& rel) -> std::string {
        std::string full = FileSystem::getPath(rel);
        if (full.empty()) return "";
        std::ifstream f(full);
        if (!f.good()) return "";
        return full;
        };

    // Load player model and animations
    // Load the BASE MODEL (static T-pose or bind pose) - this should be a separate file
    std::string playerModelPath = resolveAndCheck(base + "/RoadRunner/player.dae");
    if (playerModelPath.empty()) {
        std::cerr << "Failed to load player model, trying Treadmill Running.dae as fallback\n";
        playerModelPath = resolveAndCheck(base + "/RoadRunner/Treadmill Running.dae");
        if (playerModelPath.empty()) return -1;
    }
    modelPlayer = Model(playerModelPath);
    std::cout << "Loaded player model from: " << playerModelPath << "\n";
    std::cout << "Player model has " << modelPlayer.GetBoneCount() << " bones\n";

    // Load animations - these reference the model's bone structure
    std::string runAnimPath = FileSystem::getPath(base + "/RoadRunner/Treadmill Running.dae");
    std::string jumpAnimPath = FileSystem::getPath(base + "/RoadRunner/Jump.dae");
    std::string slideAnimPath = FileSystem::getPath(base + "/RoadRunner/Running Slide.dae");
    std::string sidestepLeftAnimPath = FileSystem::getPath(base + "/RoadRunner/Side Step Left.dae");
    std::string sidestepRightAnimPath = FileSystem::getPath(base + "/RoadRunner/Side Step Right.dae");

    std::cout << "Loading animations...\n";
    Animation runAnimation(runAnimPath, &modelPlayer);
    Animation jumpAnimation(jumpAnimPath, &modelPlayer);
    Animation slideAnimation(slideAnimPath, &modelPlayer);
    Animation sidestepLeftAnimation(sidestepLeftAnimPath, &modelPlayer);
    Animation sidestepRightAnimation(sidestepRightAnimPath, &modelPlayer);


    Animator animator(&runAnimation);
    AnimState animState = RUNNING;
    float blendAmount = 0.0f;
    float blendRate = 5.0f;  // Changed from 0.055f - this is now per SECOND, not per frame!

    // Debug: Force initial animation update
    animator.UpdateAnimation(0.01f);
    auto initialTransforms = animator.GetFinalBoneMatrices();

    // Load static environment models
    {
        std::string p = resolveAndCheck(base + "/RoadRunner/Section.obj");
        if (p.empty()) return -1;
        modelSection = Model(p);
        std::cout << "Loaded section model\n";

        // load building variants into modelBuildings
        auto tryLoadVariant = [&](const std::string& rel) {
            std::string p2 = resolveAndCheck(rel);
            if (p2.empty()) { return false; }
            modelBuildings.emplace_back(p2);
            std::cout << "Loaded " << rel << " model\n";
            return true;
            };

        tryLoadVariant(base + "/RoadRunner/Building1.obj");
        tryLoadVariant(base + "/RoadRunner/Building2.obj");
        tryLoadVariant(base + "/RoadRunner/Building3.obj");
        tryLoadVariant(base + "/RoadRunner/Building4.obj");

        p = resolveAndCheck(base + "/RoadRunner/Wires.obj");
        if (!p.empty()) { modelWires = Model(p); std::cout << "Loaded wires model\n"; }
    }

    auto tryLoadVariantGeneric = [&](const std::string& rel, std::vector<Model>& list, const char*) {
        std::string p = resolveAndCheck(rel);
        if (p.empty()) return false;
        list.emplace_back(p);
        std::cout << "Loaded " << rel << " model\n";
        return true;
        };

    modelCars.clear();
    tryLoadVariantGeneric(base + "/RoadRunner/Taxi.obj", modelCars, "car");
    tryLoadVariantGeneric(base + "/RoadRunner/Police.obj", modelCars, "car");
    tryLoadVariantGeneric(base + "/RoadRunner/SUV.obj", modelCars, "car");
    tryLoadVariantGeneric(base + "/RoadRunner/TukTuk.obj", modelCars, "car");

    modelJumps.clear();
    tryLoadVariantGeneric(base + "/RoadRunner/Cart.obj", modelJumps, "jump");
    tryLoadVariantGeneric(base + "/RoadRunner/TrashBin.obj", modelJumps, "jump");

    modelSlides.clear();
    tryLoadVariantGeneric(base + "/RoadRunner/Barrier.obj", modelSlides, "slide");

    // Skybox setup
    stbi_set_flip_vertically_on_load(false);
    std::vector<std::string> faces{
        FileSystem::getPath("resources/textures/skybox/right.jpg"),
        FileSystem::getPath("resources/textures/skybox/left.jpg"),
        FileSystem::getPath("resources/textures/skybox/top.jpg"),
        FileSystem::getPath("resources/textures/skybox/bottom.jpg"),
        FileSystem::getPath("resources/textures/skybox/front.jpg"),
        FileSystem::getPath("resources/textures/skybox/back.jpg")
    };
    GLuint cubemapTexture = loadCubemap(faces);
    stbi_set_flip_vertically_on_load(true);

    float skyboxVertices[] = {
        -1.0f,  1.0f, -1.0f,  -1.0f, -1.0f, -1.0f,   1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,   1.0f,  1.0f, -1.0f,  -1.0f,  1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f,  -1.0f, -1.0f, -1.0f,  -1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,  -1.0f,  1.0f,  1.0f,  -1.0f, -1.0f,  1.0f,
         1.0f, -1.0f, -1.0f,   1.0f, -1.0f,  1.0f,   1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,   1.0f,  1.0f, -1.0f,   1.0f, -1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f,  -1.0f,  1.0f,  1.0f,   1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,   1.0f, -1.0f,  1.0f,  -1.0f, -1.0f,  1.0f,
        -1.0f,  1.0f, -1.0f,   1.0f,  1.0f, -1.0f,   1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,  -1.0f,  1.0f,  1.0f,  -1.0f,  1.0f, -1.0f,
        -1.0f, -1.0f, -1.0f,  -1.0f, -1.0f,  1.0f,   1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,  -1.0f, -1.0f,  1.0f,   1.0f, -1.0f,  1.0f
    };

    GLuint skyboxVAO, skyboxVBO;
    glGenVertexArrays(1, &skyboxVAO);
    glGenBuffers(1, &skyboxVBO);
    glBindVertexArray(skyboxVAO);
    glBindBuffer(GL_ARRAY_BUFFER, skyboxVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices), skyboxVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindVertexArray(0);

    // spawn
    worldStartCenter = glm::vec3(0.0f, 0.0f, 0.0f);
    playerSpawnPos = glm::vec3(worldStartCenter.x - SECTION_LENGTH * 0.5f, PLAYER_SPAWN_HEIGHT, laneZ(player.laneIndex));
    player.pos = playerSpawnPos;

    // Pre-generate
    generateSectionsUpTo(player.pos.x);

    // debug print timer (to avoid spamming every frame)
    float debugPrintTimer = 0.0f;
    bool collisionPrintedLastFrame = false;

    // Light setup (directional light) - sun shining from above (top-down)
    // Slightly tilted if you want soft angled shadows: glm::vec3(0.1f, -1.0f, 0.05f)
    glm::vec3 lightDir = glm::normalize(glm::vec3(0.0f, -1.0f, 0.0f));

    // --- AUDIO: init and load (no separate header required) ---
    Audio_Init();
    Audio_LoadFiles(
        FileSystem::getPath("resources/audio/ambience.mp3"),
        FileSystem::getPath("resources/audio/click.mp3"),
        FileSystem::getPath("resources/audio/jump.wav"),
        FileSystem::getPath("resources/audio/slide.mp3"),
        FileSystem::getPath("resources/audio/running.mp3"),
        FileSystem::getPath("resources/audio/fail.wav")
    );
    Audio_PlayAmbienceLoop();

    // Main loop
    while (!glfwWindowShouldClose(window)) {
        float t = (float)glfwGetTime();
        deltaTime = t - lastFrame; lastFrame = t;

        // --- AUDIO: cleanup finished one-shot sounds ---
        Audio_Update();

        processInput(window);

        // ===== START SCREEN STATE =====
        if (currentGameState == GameState::START_SCREEN) {
            // Render plain color screen (dark blue)
            glClearColor(0.1f, 0.15f, 0.3f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            // Disable depth test for 2D UI rendering
            glDisable(GL_DEPTH_TEST);

            // Enable blending for text rendering
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            // Render the start button
            renderButton(uiShader, buttonVAO, startButton);

            // Render text on the button (centered)
            std::string buttonText = "START";
            float textScale = 1.0f;
            float textWidth = GetTextWidth(buttonText, textScale);
            float textX = (SCR_WIDTH - textWidth) / 2.0f;
            float textY = startButton.y - 15.0f;
            RenderText(*textShader, buttonText, textX, textY, textScale, glm::vec3(1.0f, 1.0f, 1.0f));

            // Render title text (centered)
            std::string titleText = "ROAD RUNNER";
            float titleScale = 1.5f;
            float titleWidth = GetTextWidth(titleText, titleScale);
            float titleX = (SCR_WIDTH - titleWidth) / 2.0f;
            float titleY = SCR_HEIGHT - 150.0f;
            RenderText(*textShader, titleText, titleX, titleY, titleScale, glm::vec3(1.0f, 1.0f, 0.0f));

            // Render instructions (centered)
            std::string instructionText = "Click button or press SPACE to start";
            float instrScale = 0.5f;
            float instrWidth = GetTextWidth(instructionText, instrScale);
            float instrX = (SCR_WIDTH - instrWidth) / 2.0f;
            float instrY = 100.0f;
            RenderText(*textShader, instructionText, instrX, instrY, instrScale, glm::vec3(0.8f, 0.8f, 0.8f));

            glDisable(GL_BLEND);

            // Re-enable depth test for 3D rendering
            glEnable(GL_DEPTH_TEST);

            glfwSwapBuffers(window);
            glfwPollEvents();
            continue; // Skip game logic
        }

        // ===== GAME OVER SCREEN STATE =====
        if (currentGameState == GameState::GAME_OVER) {
            // Render plain color screen (dark red)
            glClearColor(0.3f, 0.1f, 0.1f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            // Disable depth test for 2D UI rendering
            glDisable(GL_DEPTH_TEST);

            // Enable blending for text rendering
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            // Render the restart button
            renderButton(uiShader, buttonVAO, restartButton);

            // Render text on the button (centered)
            std::string buttonText = "RESTART";
            float textScale = 0.8f;
            float textWidth = GetTextWidth(buttonText, textScale);
            float textX = (SCR_WIDTH - textWidth) / 2.0f;
            float textY = restartButton.y - 15.0f;
            RenderText(*textShader, buttonText, textX, textY, textScale, glm::vec3(1.0f, 1.0f, 1.0f));

            // Render "GAME OVER" text (centered)
            std::string gameOverText = "GAME OVER";
            float gameOverScale = 2.0f;
            float gameOverWidth = GetTextWidth(gameOverText, gameOverScale);
            float gameOverX = (SCR_WIDTH - gameOverWidth) / 2.0f;
            float gameOverY = SCR_HEIGHT - 150.0f;
            RenderText(*textShader, gameOverText, gameOverX, gameOverY, gameOverScale, glm::vec3(1.0f, 0.2f, 0.2f));

            // Render final score (centered)
            std::string scoreText = "Final Score: " + std::to_string(player.score);
            float scoreScale = 1.0f;
            float scoreWidth = GetTextWidth(scoreText, scoreScale);
            float scoreX = (SCR_WIDTH - scoreWidth) / 2.0f;
            float scoreY = SCR_HEIGHT - 250.0f;
            RenderText(*textShader, scoreText, scoreX, scoreY, scoreScale, glm::vec3(1.0f, 1.0f, 1.0f));

            // Render instructions (centered)
            std::string instructionText = "Click button or press SPACE to restart";
            float instrScale = 0.5f;
            float instrWidth = GetTextWidth(instructionText, instrScale);
            float instrX = (SCR_WIDTH - instrWidth) / 2.0f;
            float instrY = 100.0f;
            RenderText(*textShader, instructionText, instrX, instrY, instrScale, glm::vec3(0.8f, 0.8f, 0.8f));

            glDisable(GL_BLEND);

            // Re-enable depth test for 3D rendering
            glEnable(GL_DEPTH_TEST);

            glfwSwapBuffers(window);
            glfwPollEvents();
            continue; // Skip game logic
        }


        // ===== PLAYING STATE - Normal game logic =====
        player.updateTimers(deltaTime);
        player.updateScore(deltaTime);  // Add this line to update score

        // Calculate speed multiplier based on player state
        float speedMultiplier = 1.0f;

        // REMOVED: No speed changes during jump anymore
        if (player.isSliding) {
            // Slightly faster during slide (optional)
            speedMultiplier = 1.9f;  // 110% of normal speed
        }

        player.pos.x += PLAYER_FORWARD_SPEED * speedMultiplier * deltaTime;

        // Smoothly interpolate Z position during sidestep
        if (player.isSidestepping) {
            // Use smooth interpolation (ease-in-out)
            float t = player.sidestepTimer / Player::sidestepDuration;
            // Smooth step interpolation for natural movement
            float smoothT = t * t * (3.0f - 2.0f * t);
            player.pos.z = glm::mix(player.sidestepStartZ, player.sidestepTargetZ, smoothT);
        }
        else {
            // When not sidestepping, snap to target lane position
            player.pos.z = laneZ(player.laneIndex);
        }

        // Animation state machine - WITH SMOOTH BLENDING FOR ALL TRANSITIONS
        const float BLEND_SPEED = 10.0f; // Fast blending (0.1 seconds)

        switch (animState) {
        case RUNNING:
            animator.PlayAnimation(&runAnimation, NULL, animator.m_CurrentTime, 0.0f, 0.0f);

            // Check for jump - START BLENDING
            if (player.isJumping) {
                blendAmount = 0.0f;
                animator.PlayAnimation(&runAnimation, &jumpAnimation, animator.m_CurrentTime, 0.0f, blendAmount);
                animState = RUNNING_JUMP;
                std::cout << "RUNNING -> RUNNING_JUMP (start blend)\n";
            }
            // Check for slide - START BLENDING
            else if (player.isSliding) {
                blendAmount = 0.0f;
                animator.PlayAnimation(&runAnimation, &slideAnimation, animator.m_CurrentTime, 0.0f, blendAmount);
                animState = RUNNING_SLIDE;
                std::cout << "RUNNING -> RUNNING_SLIDE (start blend)\n";
            }
            // Check for sidestep left
            else if (player.isSidestepping && player.sidestepDirection < 0) {
                blendAmount = 0.0f;
                animator.PlayAnimation(&runAnimation, &sidestepLeftAnimation, animator.m_CurrentTime, 0.0f, blendAmount);
                animState = RUNNING_SIDESTEP_LEFT;
                std::cout << "RUNNING -> RUNNING_SIDESTEP_LEFT (start blend)\n";
            }
            // Check for sidestep right
            else if (player.isSidestepping && player.sidestepDirection > 0) {
                blendAmount = 0.0f;
                animator.PlayAnimation(&runAnimation, &sidestepRightAnimation, animator.m_CurrentTime, 0.0f, blendAmount);
                animState = RUNNING_SIDESTEP_RIGHT;
                std::cout << "RUNNING -> RUNNING_SIDESTEP_RIGHT (start blend)\n";
            }
            break;

        case RUNNING_JUMP:
            // Smoothly blend from running to jump
            blendAmount += BLEND_SPEED * deltaTime;

            if (blendAmount >= 1.0f) {
                // Blend complete - switch to pure jump animation
                blendAmount = 1.0f;
                animator.PlayAnimation(&jumpAnimation, NULL, animator.m_CurrentTime2, 0.0f, 0.0f);
                animState = JUMP;
                std::cout << "RUNNING_JUMP -> JUMP (blend complete)\n";
            }
            else {
                // Continue blending
                animator.PlayAnimation(&runAnimation, &jumpAnimation, animator.m_CurrentTime, animator.m_CurrentTime2, blendAmount);
            }
            break;

        case JUMP:
            animator.PlayAnimation(&jumpAnimation, NULL, animator.m_CurrentTime, 0.0f, 0.0f);

            // When jump completes, start blending back to running
            if (!player.isJumping) {
                blendAmount = 0.0f;
                animator.PlayAnimation(&jumpAnimation, &runAnimation, animator.m_CurrentTime, 0.0f, blendAmount);
                animState = JUMP_RUNNING;
                std::cout << "JUMP -> JUMP_RUNNING (start blend back)\n";
            }
            break;

        case JUMP_RUNNING:
            // Smoothly blend from jump back to running
            blendAmount += BLEND_SPEED * deltaTime;

            if (blendAmount >= 1.0f) {
                // Blend complete - switch to pure running animation
                blendAmount = 1.0f;
                animator.PlayAnimation(&runAnimation, NULL, animator.m_CurrentTime2, 0.0f, 0.0f);
                animState = RUNNING;
                std::cout << "JUMP_RUNNING -> RUNNING (blend complete)\n";
            }
            else {
                // Continue blending
                animator.PlayAnimation(&jumpAnimation, &runAnimation, animator.m_CurrentTime, animator.m_CurrentTime2, blendAmount);
            }
            break;

        case RUNNING_SLIDE:
            // Smoothly blend from running to slide
            blendAmount += BLEND_SPEED * deltaTime;

            if (blendAmount >= 1.0f) {
                // Blend complete - switch to pure slide animation
                blendAmount = 1.0f;
                animator.PlayAnimation(&slideAnimation, NULL, animator.m_CurrentTime2, 0.0f, 0.0f);
                animState = SLIDE;
                std::cout << "RUNNING_SLIDE -> SLIDE (blend complete)\n";
            }
            else {
                // Continue blending
                animator.PlayAnimation(&runAnimation, &slideAnimation, animator.m_CurrentTime, animator.m_CurrentTime2, blendAmount);
            }
            break;

        case SLIDE:
            // CHANGED: Keep playing slide animation as long as player is sliding
            animator.PlayAnimation(&slideAnimation, NULL, animator.m_CurrentTime, 0.0f, 0.0f);

            // When slide ends (key released), start blending back to running
            if (!player.isSliding) {
                blendAmount = 0.0f;
                animator.PlayAnimation(&slideAnimation, &runAnimation, animator.m_CurrentTime, 0.0f, blendAmount);
                animState = SLIDE_RUNNING;
                std::cout << "SLIDE -> SLIDE_RUNNING (start blend back)\n";
            }
            break;

        case SLIDE_RUNNING:
            // Smoothly blend from slide back to running
            blendAmount += BLEND_SPEED * deltaTime;

            if (blendAmount >= 1.0f) {
                // Blend complete - switch to pure running animation
                blendAmount = 1.0f;
                animator.PlayAnimation(&runAnimation, NULL, animator.m_CurrentTime2, 0.0f, 0.0f);
                animState = RUNNING;
                std::cout << "SLIDE_RUNNING -> RUNNING (blend complete)\n";
            }
            else {
                // Continue blending
                animator.PlayAnimation(&slideAnimation, &runAnimation, animator.m_CurrentTime, animator.m_CurrentTime2, blendAmount);
            }
            break;

            // ========== SIDESTEP LEFT STATES ==========
        case RUNNING_SIDESTEP_LEFT:
            // Smoothly blend from running to sidestep LEFT
            blendAmount += BLEND_SPEED * deltaTime;

            if (blendAmount >= 1.0f) {
                // Blend complete - switch to pure sidestep LEFT animation
                blendAmount = 1.0f;
                animator.PlayAnimation(&sidestepLeftAnimation, NULL, animator.m_CurrentTime2, 0.0f, 0.0f);
                animState = SIDESTEP_LEFT;
                std::cout << "RUNNING_SIDESTEP_LEFT -> SIDESTEP_LEFT (blend complete)\n";
            }
            else {
                // Continue blending
                animator.PlayAnimation(&runAnimation, &sidestepLeftAnimation, animator.m_CurrentTime, animator.m_CurrentTime2, blendAmount);
            }
            break;

        case SIDESTEP_LEFT:
            animator.PlayAnimation(&sidestepLeftAnimation, NULL, animator.m_CurrentTime, 0.0f, 0.0f);

            // When sidestep ends, start blending back to running
            if (!player.isSidestepping) {
                blendAmount = 0.0f;
                animator.PlayAnimation(&sidestepLeftAnimation, &runAnimation, animator.m_CurrentTime, 0.0f, blendAmount);
                animState = SIDESTEP_LEFT_RUNNING;
                std::cout << "SIDESTEP_LEFT -> SIDESTEP_LEFT_RUNNING (start blend back)\n";
            }
            break;

        case SIDESTEP_LEFT_RUNNING:
            // Smoothly blend from sidestep LEFT back to running
            blendAmount += BLEND_SPEED * deltaTime;

            if (blendAmount >= 1.0f) {
                // Blend complete - switch to pure running animation
                blendAmount = 1.0f;
                animator.PlayAnimation(&runAnimation, NULL, animator.m_CurrentTime2, 0.0f, 0.0f);
                animState = RUNNING;
                std::cout << "SIDESTEP_LEFT_RUNNING -> RUNNING (blend complete)\n";
            }
            else {
                // Continue blending
                animator.PlayAnimation(&sidestepLeftAnimation, &runAnimation, animator.m_CurrentTime, animator.m_CurrentTime2, blendAmount);
            }
            break;

            // ========== SIDESTEP RIGHT STATES ==========
        case RUNNING_SIDESTEP_RIGHT:
            // Smoothly blend from running to sidestep RIGHT
            blendAmount += BLEND_SPEED * deltaTime;

            if (blendAmount >= 1.0f) {
                // Blend complete - switch to pure sidestep RIGHT animation
                blendAmount = 1.0f;
                animator.PlayAnimation(&sidestepRightAnimation, NULL, animator.m_CurrentTime2, 0.0f, 0.0f);
                animState = SIDESTEP_RIGHT;
                std::cout << "RUNNING_SIDESTEP_RIGHT -> SIDESTEP_RIGHT (blend complete)\n";
            }
            else {
                // Continue blending
                animator.PlayAnimation(&runAnimation, &sidestepRightAnimation, animator.m_CurrentTime, animator.m_CurrentTime2, blendAmount);
            }
            break;

        case SIDESTEP_RIGHT:
            animator.PlayAnimation(&sidestepRightAnimation, NULL, animator.m_CurrentTime, 0.0f, 0.0f);

            // When sidestep ends, start blending back to running
            if (!player.isSidestepping) {
                blendAmount = 0.0f;
                animator.PlayAnimation(&sidestepRightAnimation, &runAnimation, animator.m_CurrentTime, 0.0f, blendAmount);
                animState = SIDESTEP_RIGHT_RUNNING;
                std::cout << "SIDESTEP_RIGHT -> SIDESTEP_RIGHT_RUNNING (start blend back)\n";
            }
            break;

        case SIDESTEP_RIGHT_RUNNING:
            // Smoothly blend from sidestep RIGHT back to running
            blendAmount += BLEND_SPEED * deltaTime;

            if (blendAmount >= 1.0f) {
                // Blend complete - switch to pure running animation
                blendAmount = 1.0f;
                animator.PlayAnimation(&runAnimation, NULL, animator.m_CurrentTime2, 0.0f, 0.0f);
                animState = RUNNING;
                std::cout << "SIDESTEP_RIGHT_RUNNING -> RUNNING (blend complete)\n";
            }
            else {
                // Continue blending
                animator.PlayAnimation(&sidestepRightAnimation, &runAnimation, animator.m_CurrentTime, animator.m_CurrentTime2, blendAmount);
            }
            break;
        }

        // Update animator
        animator.UpdateAnimation(deltaTime);

        if (!debugCameraEnabled) {
            glm::vec3 playerForward(1.0f, 0.0f, 0.0f);

            // Calculate target camera position - NOW FOLLOWS PLAYER'S Z POSITION (lane changes)
            glm::vec3 targetCamPos = glm::vec3(player.pos.x, 0.0f, player.pos.z) - playerForward * CAM_DISTANCE + glm::vec3(0.0f, CAM_HEIGHT, 0.0f);

            // Smoothly interpolate camera position towards target
            float lerpFactor = CAM_SMOOTHING * deltaTime;
            lerpFactor = glm::clamp(lerpFactor, 0.0f, 1.0f);  // Clamp to [0,1] to prevent overshooting
            camera.Position = glm::mix(camera.Position, targetCamPos, lerpFactor);

            camera.Front = glm::normalize(playerForward);
            camera.Right = glm::normalize(glm::cross(camera.Front, camera.WorldUp));
            camera.Up = glm::normalize(glm::cross(camera.Right, camera.Front));
            camera.Yaw = 0.0f;
            camera.Pitch = 0.0f;
        }

        generateSectionsUpTo(player.pos.x);

        // Check collision
        bool hit = checkHitObstacle(player);
        if (hit) {
            if (!collisionPrintedLastFrame) {
                std::cout << "Hit\n";
                collisionPrintedLastFrame = true;
            }
            if (!debugCameraEnabled) resetGame();
        }
        else {
            collisionPrintedLastFrame = false;
        }

        // ---------- Shadow pass (render scene from light into depth map) ----------
        // Compute light-space matrix that covers the playing area centered on player
        float near_plane = 1.0f, far_plane = 200.0f;
        float orthoSize = 120.0f; // adjust to your scene; bigger = more area covered but less precision
        glm::mat4 lightProjection = glm::ortho(-orthoSize, orthoSize, -orthoSize, orthoSize, near_plane, far_plane);
        // place light so it looks at the player's forward area
        glm::vec3 lightPos = glm::vec3(player.pos.x, 40.0f, player.pos.z) - lightDir * 50.0f;
        glm::mat4 lightView = glm::lookAt(lightPos, glm::vec3(player.pos.x, 0.0f, player.pos.z), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 lightSpaceMatrix = lightProjection * lightView;

        // Render to depth map
        glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
        glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
        glClear(GL_DEPTH_BUFFER_BIT);
        depthShader.use();
        depthShader.setMat4("lightSpaceMatrix", lightSpaceMatrix);

        // Draw world to depth map
        depthShader.setInt("isAnimated", 0);
        for (auto& s : sections) {
            glm::vec3 sectionPos(s.centerX, 0.0f, 0.0f);
            drawModelAt(depthShader, modelSection, sectionPos, 0.0f, 1.0f);

            if (!modelBuildings.empty()) {
                glm::vec3 bL1(s.centerX + BUILDING_LENGTH * 0.5f, 0.0f, -(((LANE_COUNT - 1) * LANE_Z_SPACING) + (LANE_Z_SPACING / 2)) - SIDEWALK_WIDTH);
                glm::vec3 bL2(s.centerX - BUILDING_LENGTH * 0.5f, 0.0f, -(((LANE_COUNT - 1) * LANE_Z_SPACING) + (LANE_Z_SPACING / 2)) - SIDEWALK_WIDTH);
                glm::vec3 bR1(s.centerX + BUILDING_LENGTH * 0.5f, 0.0f, (((LANE_COUNT - 1) * LANE_Z_SPACING) + (LANE_Z_SPACING / 2)) + SIDEWALK_WIDTH);
                glm::vec3 bR2(s.centerX - BUILDING_LENGTH * 0.5f, 0.0f, (((LANE_COUNT - 1) * LANE_Z_SPACING) + (LANE_Z_SPACING / 2)) + SIDEWALK_WIDTH);

                const std::array<glm::vec3, 4> bpos = { bL1, bL2, bR1, bR2 };
                for (int i = 0; i < 4; ++i) {
                    int vid = s.buildingVariants[i];
                    if (vid >= 0 && vid < (int)modelBuildings.size()) {
                        float yaw = (i == 0 || i == 1) ? 180.0f : 0.0f;
                        drawModelAt(depthShader, modelBuildings[vid], bpos[i], yaw, 1.0f);
                    }
                }
            }

            if (s.hasWires && !modelWires.meshes.empty()) {
                const Obstacle& w = s.laneObstacles[1];
                drawModelAt(depthShader, modelWires, w.pos, 180.0f, 1.0f);
            }

            for (int lane = 0; lane < LANE_COUNT; ++lane) {
                const Obstacle& o = s.laneObstacles[lane];
                if (o.type == ObstacleType::None || o.type == ObstacleType::Wires) continue;
                float obsYaw = 180.0f;
                if (o.type == ObstacleType::Car) {
                    if (o.variantIndex >= 0 && o.variantIndex < (int)modelCars.size())
                        drawModelAt(depthShader, modelCars[o.variantIndex], o.pos, obsYaw, 0.9f);
                    else if (!modelCars.empty())
                        drawModelAt(depthShader, modelCars[0], o.pos, obsYaw, 0.9f);
                }
                else if (o.type == ObstacleType::Jump) {
                    if (o.variantIndex >= 0 && o.variantIndex < (int)modelJumps.size())
                        drawModelAt(depthShader, modelJumps[o.variantIndex], o.pos, obsYaw, 0.9f);
                    else if (!modelJumps.empty())
                        drawModelAt(depthShader, modelJumps[0], o.pos, obsYaw, 0.9f);
                }
                else if (o.type == ObstacleType::Slide) {
                    if (o.variantIndex >= 0 && o.variantIndex < (int)modelSlides.size())
                        drawModelAt(depthShader, modelSlides[o.variantIndex], o.pos, obsYaw, 1.0f);
                    else if (!modelSlides.empty())
                        drawModelAt(depthShader, modelSlides[0], o.pos, obsYaw, 1.0f);
                }
            }
        }

        // Draw animated player into depth map as well
        depthShader.setInt("isAnimated", 1);
        auto depthTransforms = animator.GetFinalBoneMatrices();
        for (int i = 0; i < depthTransforms.size(); ++i) {
            depthShader.setMat4("finalBonesMatrices[" + std::to_string(i) + "]", depthTransforms[i]);
        }
        // Use same playerRenderPos logic as later (include vertical offsets so shadows follow)
        glm::vec3 playerDepthPos = player.pos;
        if (player.isJumping) {
            float tfrac = player.jumpTimer / Player::jumpDuration;
            playerDepthPos.y += Player::jumpHeight * (4.0f * tfrac * (1.0f - tfrac));
        }
        else if (player.isSliding) {
            float slideAnimProgress = player.slideTimer / Player::slideDuration;
            float estimatedAnimForwardMotion = 2.0f * slideAnimProgress;
            playerDepthPos.x -= estimatedAnimForwardMotion * PLAYER_SCALE;
        }
        else if (player.isCrouching) {
            playerDepthPos.y = PLAYER_CROUCH_HEIGHT;
        }
        drawModelAt(depthShader, modelPlayer, playerDepthPos, 90.0f, PLAYER_SCALE);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        // Reset viewport for normal rendering
        glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);

        // Render
        glClearColor(0.05f, 0.05f, 0.07f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 P = glm::perspective(glm::radians(camera.Zoom), (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 500.0f);
        glm::mat4 V = glm::lookAt(camera.Position, camera.Position + camera.Front, camera.WorldUp);

        // Use shader for world models
        shader.use();
        shader.setMat4("projection", P);
        shader.setMat4("view", V);
        shader.setInt("isAnimated", 0);

        // Pass lighting uniforms
        shader.setVec3("lightDir", lightDir);
        shader.setVec3("viewPos", camera.Position);
        shader.setMat4("lightSpaceMatrix", lightSpaceMatrix);
        shader.setInt("shadowMap", 1);

        // Bind shadow map to texture unit 1
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, depthMap);
        glActiveTexture(GL_TEXTURE0);

        // Draw generated sections
        float roadHalf = ((LANE_COUNT - 1) * LANE_Z_SPACING) + (LANE_Z_SPACING / 2);
        float buildingZLeft = -(roadHalf + SIDEWALK_WIDTH);
        float buildingZRight = (roadHalf + SIDEWALK_WIDTH);
        for (auto& s : sections) {
            glm::vec3 sectionPos(s.centerX, 0.0f, 0.0f);
            drawModelAt(shader, modelSection, sectionPos, 0.0f, 1.0f);

            if (!modelBuildings.empty()) {
                glm::vec3 bL1(s.centerX + BUILDING_LENGTH * 0.5f, 0.0f, buildingZLeft);
                glm::vec3 bL2(s.centerX - BUILDING_LENGTH * 0.5f, 0.0f, buildingZLeft);
                glm::vec3 bR1(s.centerX + BUILDING_LENGTH * 0.5f, 0.0f, buildingZRight);
                glm::vec3 bR2(s.centerX - BUILDING_LENGTH * 0.5f, 0.0f, buildingZRight);

                const std::array<glm::vec3, 4> bpos = { bL1, bL2, bR1, bR2 };
                for (int i = 0; i < 4; ++i) {
                    int vid = s.buildingVariants[i];
                    if (vid >= 0 && vid < (int)modelBuildings.size()) {
                        float yaw = (i == 0 || i == 1) ? 180.0f : 0.0f;
                        drawModelAt(shader, modelBuildings[vid], bpos[i], yaw, 1.0f);
                    }
                }
            }

            if (s.hasWires && !modelWires.meshes.empty()) {
                const Obstacle& w = s.laneObstacles[1];
                drawModelAt(shader, modelWires, w.pos, 180.0f, 1.0f);
            }

            for (int lane = 0; lane < LANE_COUNT; ++lane) {
                const Obstacle& o = s.laneObstacles[lane];
                if (o.type == ObstacleType::None || o.type == ObstacleType::Wires) continue;
                float obsYaw = 180.0f;
                if (o.type == ObstacleType::Car) {
                    if (o.variantIndex >= 0 && o.variantIndex < (int)modelCars.size())
                        drawModelAt(shader, modelCars[o.variantIndex], o.pos, obsYaw, 0.9f);
                    else if (!modelCars.empty())
                        drawModelAt(shader, modelCars[0], o.pos, obsYaw, 0.9f);
                }
                else if (o.type == ObstacleType::Jump) {
                    if (o.variantIndex >= 0 && o.variantIndex < (int)modelJumps.size())
                        drawModelAt(shader, modelJumps[o.variantIndex], o.pos, obsYaw, 0.9f);
                    else if (!modelJumps.empty())
                        drawModelAt(shader, modelJumps[0], o.pos, obsYaw, 0.9f);
                }
                else if (o.type == ObstacleType::Slide) {
                    if (o.variantIndex >= 0 && o.variantIndex < (int)modelSlides.size())
                        drawModelAt(shader, modelSlides[o.variantIndex], o.pos, obsYaw, 1.0f);
                    else if (!modelSlides.empty())
                        drawModelAt(shader, modelSlides[0], o.pos, obsYaw, 1.0f);
                }
            }
        }

        // Draw animated player
        shader.setInt("isAnimated", 1);
        auto transforms = animator.GetFinalBoneMatrices();

        // Calculate player render position
        glm::vec3 playerRenderPos = player.pos;

        // Apply vertical offset for jump and slide
        if (player.isJumping) {
            // ADD: Vertical jump movement with parabolic arc
            float tfrac = player.jumpTimer / Player::jumpDuration;
            playerRenderPos.y += Player::jumpHeight * (4.0f * tfrac * (1.0f - tfrac));

        }
        else if (player.isSliding) {
            // REMOVED: playerRenderPos.y = 0.15f;  - Character no longer moves down during slide

            // Use fixed duration slide animation
            float slideAnimProgress = player.slideTimer / Player::slideDuration;
            float estimatedAnimForwardMotion = 2.0f * slideAnimProgress;
            playerRenderPos.x -= estimatedAnimForwardMotion * PLAYER_SCALE;

        }
        else if (player.isCrouching) {
            playerRenderPos.y = PLAYER_CROUCH_HEIGHT;
        }

        // Debug output (print once every 60 frames to avoid spam)
        static int frameCount = 0;
        if (frameCount % 60 == 0) {
            std::cout << "=== PLAYER RENDER DEBUG ===\n";
            std::cout << "Player position: (" << playerRenderPos.x << ", " << playerRenderPos.y << ", " << playerRenderPos.z << ")\n";
            std::cout << "Player scale: " << PLAYER_SCALE << "\n";
            std::cout << "Player meshes: " << modelPlayer.meshes.size() << "\n";
            std::cout << "Bone matrices: " << transforms.size()
                << " | AnimState: " << animState
                << " | CurrentTime: " << animator.m_CurrentTime
                << " | BlendAmount: " << blendAmount << "\n";

            // Debug: Check if the first bone matrix is identity (which would mean no animation)
            if (!transforms.empty()) {
                const auto& firstMatrix = transforms[0];
                bool isIdentity = (firstMatrix[0][0] == 1.0f && firstMatrix[1][1] == 1.0f &&
                    firstMatrix[2][2] == 1.0f && firstMatrix[3][3] == 1.0f &&
                    firstMatrix[0][1] == 0.0f && firstMatrix[0][2] == 0.0f);
                std::cout << "  First bone matrix is " << (isIdentity ? "IDENTITY (no animation!)" : "animated") << "\n";

                // Print first matrix for inspection
                std::cout << "  First bone matrix[0]: ["
                    << firstMatrix[0][0] << ", " << firstMatrix[0][1] << ", "
                    << firstMatrix[0][2] << ", " << firstMatrix[0][3] << "]\n";
            }
            std::cout << "===========================\n";
        }
        frameCount++;

        for (int i = 0; i < transforms.size(); ++i) {
            shader.setMat4("finalBonesMatrices[" + std::to_string(i) + "]", transforms[i]);
        }

        drawModelAt(shader, modelPlayer, playerRenderPos, 90.0f, PLAYER_SCALE);

        // Draw skybox
        glDepthFunc(GL_LEQUAL);
        skyboxShader.use();
        glm::mat4 viewNoTrans = glm::mat4(glm::mat3(V));
        skyboxShader.setMat4("view", viewNoTrans);
        skyboxShader.setMat4("projection", P);
        glBindVertexArray(skyboxVAO);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glBindVertexArray(0);
        glDepthFunc(GL_LESS);

        // ===== RENDER SCORE HUD =====
        // Disable depth test for 2D UI rendering
        glDisable(GL_DEPTH_TEST);

        // Enable blending for text rendering
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // Render score in top-left corner
        std::string scoreText = "Score: " + std::to_string(player.score);
        float scoreScale = 0.6f;
        float scoreX = 20.0f; // 20 pixels from left edge
        float scoreY = SCR_HEIGHT - 50.0f; // 50 pixels from top edge
        RenderText(*textShader, scoreText, scoreX, scoreY, scoreScale, glm::vec3(1.0f, 1.0f, 1.0f));

        glDisable(GL_BLEND);

        // Re-enable depth test for next frame
        glEnable(GL_DEPTH_TEST);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    Audio_Shutdown();
    glfwTerminate();
    return 0;
}