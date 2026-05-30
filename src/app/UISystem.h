#pragma once

#include <SDL2/SDL.h>
#include <string>
#include <functional>
#include <random>
#include <memory>
#include <vector>
#include <array>

// Forward declarations para no incluir todo en el header
struct ImGuiContext;
struct VisualControls;
struct VideoRandomizerState;
struct VideoRandomizerState2;
class VideoPlayer;
class VideoRegistry;
class MidiSystem;
class OscSystem;
class AudioSystem;

// Agregamos solo lo imprescindible de imgui aqui.
// El .cpp incluye los headers completos de imgui.

// ---------------------------------------------------------------------------
// Contexto de diagnostico: lo que la UI necesita leer de App para mostrar
// info de solo lectura. Solo punteros/referencias baratas, sin ownership.
// ---------------------------------------------------------------------------
struct UIDiagnostics {
    uint32_t lastFrameFrameIndex = 0;
    uint32_t lastFrameImageIndex = 0;
    uint32_t swapchainWidth      = 0;
    uint32_t swapchainHeight     = 0;
    int      currentMode         = 0;
    bool     videoReady          = false;
    uint32_t videoWidth          = 0;
    uint32_t videoHeight         = 0;
    float    animationTime       = 0.0f;
    float    animationDelta      = 0.0f;
    float    animationModulo     = 0.0f;
    float    animationRelativeSpeed = 1.0f;
    float    animationSecondsPerUnit = 1.0f;
    float    animationElapsedSeconds = 0.0f;

    // GPU profiling
    std::array<float, 8> gpuPassTimes{};
    float                gpuTotalTime = 0.0f;
};

// ---------------------------------------------------------------------------
// Callbacks que la UI dispara hacia App.
// Usamos std::function para no acoplar UISystem a App directamente.
// ---------------------------------------------------------------------------
struct UICallbacks {
    // Pide recargar un video por path
    std::function<void(const std::string& path)> onReloadVideo;
    std::function<void(const std::string& path)> onReloadVideo2;

    // Pide randomizar el video actual
    std::function<void()> onRandomizeVideo;
    std::function<void()> onRandomizeVideo2;
    std::function<void()> onRandomizePreviewVideo1;
    std::function<void()> onRandomizePreviewVideo2;

    // Pide recargar videos cuando cambia la carpeta seleccionada
    std::function<void()> onFolderChanged;
    std::function<void()> onFolderChanged2;

    // Pide un jump aleatorio dentro del clip actual
    std::function<void()> onJumpRandom;

    // Pide renombrar el asset seleccionado
    std::function<void(int assetIndex, const std::string& newName)> onRenameAsset;

    // Pide eliminar el asset seleccionado
    std::function<void(int assetIndex)> onDeleteAsset;

    // Pide aplicar cambios (copy output.mp4 → active_file)
    std::function<void()> onApplyChanges;

    // Notifica que los controles cambiaron (para marcar controlsDirty)
    std::function<void()> onControlsChanged;

    // Per-video playback speed cache
    std::function<float(const std::string&)> onGetVideoSpeed;
    std::function<void(const std::string&, float)> onSetVideoSpeed;

    // Presets
    std::function<std::vector<std::string>()> onListPresets;
    std::function<bool(const std::string&)> onSavePreset;
    std::function<bool(const std::string&)> onLoadPreset;
    std::function<bool(const std::string&)> onDeletePreset;
};

// ---------------------------------------------------------------------------
// UISystem: encapsula init/shutdown/render de ImGui.
// No tiene ownership de los datos que renderiza: recibe referencias.
// ---------------------------------------------------------------------------
class UISystem {
public:
    UISystem() = default;
    ~UISystem();

    // No copiable
    UISystem(const UISystem&)            = delete;
    UISystem& operator=(const UISystem&) = delete;

    // Inicializa ImGui sobre una ventana SDL + renderer SDL
    bool initialize(SDL_Window* window, SDL_Renderer* renderer);

    void shutdown();

    // Procesa un evento SDL (llamar antes de newFrame)
    void processEvent(const SDL_Event& event);

    // Construye y renderiza todas las ventanas ImGui en un frame
    void render(
        VisualControls&       controls,
        VideoRandomizerState& randomizer,
        VideoRandomizerState2& randomizer2,
        VideoPlayer&          player,
        VideoPlayer&          player2,
        VideoRegistry&        registry,
        int&                  selectedVideoAsset,
        int&                  selectedVideoAsset2,
        float&                transitionDuration,
        float&                transitionDuration2,
        bool&                 allowDimensionChangeRecreation,
        bool&                 controlsDirty,
        std::mt19937&         rng,
        const UIDiagnostics&  diag,
        const UICallbacks&    callbacks,
        MidiSystem&           midiSystem,
        OscSystem&            oscSystem,
        AudioSystem&          audioSystem,
        const std::string&    video1Path,
        const std::string&    video2Path
    );

    void forcePreviewShuffle(int slotIndex);

    // Randomiza los controles VJAY Basics directamente
    void randomizeVJayBasics(VisualControls& controls);

    // Randomiza los controles VJAY Extra directamente
    void randomizeVJayExtra(VisualControls& controls);

private:
    void drawProceduralControls(
        VisualControls&       controls,
        VideoRandomizerState& randomizer,
        VideoPlayer&          player,
        VideoRegistry&        registry,
        int&                  selectedVideoAsset,
        int&                  selectedVideoAsset2,
        float&                transitionDuration,
        bool&                 allowDimensionChangeRecreation,
        bool&                 controlsDirty,
        std::mt19937&         rng,
        const UIDiagnostics&  diag,
        const UICallbacks&    callbacks
    );

    bool isInitialized() const { return initialized; }

    // Flags de visibilidad de ventanas (App puede leer/escribir)
    bool showDiagnostics = true;
    bool showDemoWindow  = false;
    bool showNLEWindow   = true;
    bool showMidiWindow  = true;
    bool showOscWindow   = true;
    bool showAudioWindow = true;
    bool showParameterIndex = true;
    
    // Navbar tab selection
    int selectedTab = 0;

private:
    void drawPerformanceContent(const UIDiagnostics& diag);

    struct VideoPreviewSlot {
        int                       previewSelection   = -1;
        int                       confirmedSelection = -1;
        std::string               previewPath;
        std::string               loadedPath;
        std::string               lastError;
        std::vector<uint8_t>      frameBuffer;
        SDL_Texture*              texture            = nullptr;
        int                       textureWidth       = 0;
        int                       textureHeight      = 0;
        float                     frameAccumulator   = 0.0f;
        std::unique_ptr<VideoPlayer> player;
    };

    void beginFrame();
    void endFrame();


    void drawVJayBasics(
        VisualControls& controls,
        bool&           controlsDirty,
        std::mt19937&   rng
    );

    void drawVJayExtra(
        VisualControls& controls,
        bool&           controlsDirty,
        std::mt19937&   rng
    );

    void drawNLEEditor(const UICallbacks& callbacks, const std::string& video1Path, const std::string& video2Path);

    void drawPresetsContent(VisualControls& controls, bool& controlsDirty, const UICallbacks& callbacks);

    void drawDiagnostics(
        const UIDiagnostics& diag,
        VideoPlayer&         player,
        VideoPlayer&         player2,
        VideoRegistry&       registry,
        int&                 selectedAsset,
        int&                 selectedAsset2,
        VisualControls&      controls,
        const UICallbacks&   callbacks
    );

    void drawMidiControls(MidiSystem& midiSystem);
    void drawOscControls(OscSystem& oscSystem);
    void drawAudioDebug(AudioSystem& audioSystem);
    void drawParameterIndex();
    
    void drawMainNavbar(
        VisualControls&       controls,
        VideoRandomizerState& randomizer,
        VideoRandomizerState2& randomizer2,
        VideoPlayer&          player,
        VideoPlayer&          player2,
        VideoRegistry&        registry,
        int&                  selectedVideoAsset,
        int&                  selectedVideoAsset2,
        float&                transitionDuration,
        float&                transitionDuration2,
        bool&                 allowDimensionChangeRecreation,
        bool&                 controlsDirty,
        std::mt19937&         rng,
        const UIDiagnostics&  diag,
        const UICallbacks&    callbacks,
        MidiSystem&           midiSystem,
        OscSystem&            oscSystem,
        AudioSystem&          audioSystem,
        const std::string&    video1Path,
        const std::string&    video2Path
    );
    
    // Content functions for tabs (extracted from window functions)
    void drawProceduralControlsContent(
        VisualControls&       controls,
        VideoRandomizerState& randomizer,
        VideoPlayer&          player,
        VideoRegistry&        registry,
        int&                  selectedVideoAsset,
        int&                  selectedVideoAsset2,
        float&                transitionDuration,
        bool&                 allowDimensionChangeRecreation,
        bool&                 controlsDirty,
        std::mt19937&         rng,
        const UIDiagnostics&  diag,
        const UICallbacks&    callbacks
    );
    
    void drawVJayBasicsContent(
        VisualControls& controls,
        bool&           controlsDirty,
        std::mt19937&   rng
    );

    void drawPostFxContent(
        VisualControls& controls,
        bool&           controlsDirty,
        std::mt19937&   rng
    );
    
    void drawVideoContent(
        VisualControls&       controls,
        VideoRandomizerState& randomizer,
        VideoRandomizerState2& randomizer2,
        VideoRegistry&        registry,
        int&                  selectedVideoAsset,
        int&                  selectedVideoAsset2,
        float&                transitionDuration,
        float&                transitionDuration2,
        bool&                 allowDimensionChangeRecreation,
        bool&                 controlsDirty,
        const UIDiagnostics&  diag,
        const UICallbacks&    callbacks
    );
    
    void drawVJayExtraContent(
        VisualControls& controls,
        bool&           controlsDirty,
        std::mt19937&   rng
    );
    
    void drawNLEEditorContent(const UICallbacks& callbacks, const std::string& video1Path, const std::string& video2Path);
    
    void drawDiagnosticsContent(
        const UIDiagnostics& diag,
        VideoPlayer&         player,
        VideoPlayer&         player2,
        VideoRegistry&       registry,
        int&                 selectedAsset,
        int&                 selectedAsset2,
        VisualControls&      controls,
        const UICallbacks&   callbacks
    );
    
    void drawMidiControlsContent(MidiSystem& midiSystem);
    void drawOscControlsContent(OscSystem& oscSystem);
    void drawAudioDebugContent(AudioSystem& audioSystem, VisualControls& controls);
    void drawParameterIndexContent();
    void drawPreviewContent(
        VisualControls&       controls,
        VideoRegistry&        registry,
        int&                  selectedVideoAsset,
        int&                  selectedVideoAsset2,
        VideoRandomizerState& randomizer,
        VideoRandomizerState2& randomizer2,
        float&                transitionDuration,
        float&                transitionDuration2,
        bool&                 controlsDirty,
        const UIDiagnostics&  diag,
        const UICallbacks&    callbacks,
        const std::string&    video1Path,
        const std::string&    video2Path,
        std::mt19937&         rng);

    SDL_Window*   window   = nullptr;
    SDL_Renderer* renderer = nullptr;
    ImGuiContext* context  = nullptr;
    bool          initialized = false;

    VideoPreviewSlot previewSlotVideo1;
    VideoPreviewSlot previewSlotVideo2;
    bool previewShuffleRequested[2] = {false, false};
    std::mt19937 previewRng{std::random_device{}()};

    char presetNameBuffer[64] = "";

    void destroyPreviewSlot(VideoPreviewSlot& slot);
    bool loadPreview(VideoPreviewSlot& slot, const std::string& path);
    bool decodePreviewFrame(VideoPreviewSlot& slot);
    bool ensurePreviewTexture(VideoPreviewSlot& slot, int width, int height);
    void updatePreviewSlot(VideoPreviewSlot& slot, float deltaTime);
};
