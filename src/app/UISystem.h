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

    // CPU profiling (milliseconds per section)
    float cpuEventsMs = 0.0f;
    float cpuVideoUpdateMs = 0.0f;
    float cpuUboUpdateMs = 0.0f;
    float cpuUiRenderMs = 0.0f;
    float cpuRecordCmdMs = 0.0f;
    float cpuSubmitPresentMs = 0.0f;
    float cpuFrameTotalMs = 0.0f;

    // Frame rate of the main loop (shared by all windows)
    float appFps = 0.0f;
};

// ---------------------------------------------------------------------------
// Callbacks que la UI dispara hacia App.
// Usamos std::function para no acoplar UISystem a App directamente.
// ---------------------------------------------------------------------------
struct UICallbacks {
    // Pide recargar un video por path
    std::function<void(const std::string& path)> onReloadVideo;
    std::function<void(const std::string& path)> onReloadVideo2;
    std::function<void(const std::string& path)> onReloadVideo3;

    // Pide randomizar el video actual
    std::function<void()> onRandomizeVideo;
    std::function<void()> onRandomizeVideo2;
    std::function<void()> onRandomizeVideo3;
    std::function<void()> onRandomizePreviewVideo1;
    std::function<void()> onRandomizePreviewVideo2;
    std::function<void()> onRandomizePreviewVideo3;

    // Pide recargar videos cuando cambia la carpeta seleccionada
    std::function<void()> onFolderChanged;
    std::function<void()> onFolderChanged2;
    std::function<void()> onFolderChanged3;

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
    std::function<bool(const std::string&, const std::string&)> onRenamePreset;
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

    // Procesa un evento SDL (llamar antes de newFrame).
    // Devuelve true si ImGui consumio el evento.
    bool processEvent(const SDL_Event& event);

    // Pause/resume the ImGui renderer (lightweight overlay when paused)
    void toggleRendererPause() { rendererPaused = !rendererPaused; }
    bool isRendererPaused() const { return rendererPaused; }

    // Construye y renderiza todas las ventanas ImGui en un frame
    void render(
        VisualControls&       controls,
        VideoRandomizerState& randomizer,
        VideoRandomizerState2& randomizer2,
        VideoRandomizerState2& randomizer3,
        VideoPlayer&          player,
        VideoPlayer&          player2,
        VideoPlayer&          player3,
        VideoRegistry&        registry,
        int&                  selectedVideoAsset,
        int&                  selectedVideoAsset2,
        int&                  selectedVideoAsset3,
        float&                transitionDuration,
        float&                transitionDuration2,
        float&                transitionDuration3,
        bool&                 allowDimensionChangeRecreation,
        bool&                 controlsDirty,
        std::mt19937&         rng,
        const UIDiagnostics&  diag,
        const UICallbacks&    callbacks,
        MidiSystem&           midiSystem,
        OscSystem&            oscSystem,
        AudioSystem&          audioSystem,
        const std::string&    video1Path,
        const std::string&    video2Path,
        const std::string&    video3Path
    );

    void forcePreviewShuffle(int slotIndex);
    void processPreviewShuffles(VideoRegistry& registry,
                                int& selAsset, int& selAsset2, int& selAsset3,
                                const std::string& video1Folder,
                                const std::string& video2Folder,
                                const std::string& video3Folder);

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

public:
    void setPostEffectNames(const std::vector<std::string>& names);
    int cycleLayerMode(int currentMode, int delta) const;
    std::string cyclePostEffect(const std::string& current, int delta) const;

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
        bool                      paused             = false;
        bool                      manualUnload       = false;
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

    void drawNLEEditor(const UICallbacks& callbacks, const std::string& video1Path, const std::string& video2Path, const std::string& video3Path);

    void drawPresetsContent(VisualControls& controls, bool& controlsDirty, const UICallbacks& callbacks);

    void drawDiagnostics(
        const UIDiagnostics& diag,
        VideoPlayer&         player,
        VideoPlayer&         player2,
        VideoPlayer&         player3,
        VideoRegistry&       registry,
        int&                 selectedAsset,
        int&                 selectedAsset2,
        int&                 selectedAsset3,
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
        VideoRandomizerState2& randomizer3,
        VideoPlayer&          player,
        VideoPlayer&          player2,
        VideoPlayer&          player3,
        VideoRegistry&        registry,
        int&                  selectedVideoAsset,
        int&                  selectedVideoAsset2,
        int&                  selectedVideoAsset3,
        float&                transitionDuration,
        float&                transitionDuration2,
        float&                transitionDuration3,
        bool&                 allowDimensionChangeRecreation,
        bool&                 controlsDirty,
        std::mt19937&         rng,
        const UIDiagnostics&  diag,
        const UICallbacks&    callbacks,
        MidiSystem&           midiSystem,
        OscSystem&            oscSystem,
        AudioSystem&          audioSystem,
        const std::string&    video1Path,
        const std::string&    video2Path,
        const std::string&    video3Path
    );
    
    // Content functions for tabs (extracted from window functions)
    void drawProceduralControlsContent(
        VisualControls&       controls,
        VideoRandomizerState& randomizer,
        VideoPlayer&          player,
        VideoRegistry&        registry,
        int&                  selectedVideoAsset,
        int&                  selectedVideoAsset2,
        int&                  selectedVideoAsset3,
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
        VideoRandomizerState2& randomizer3,
        VideoRegistry&        registry,
        int&                  selectedVideoAsset,
        int&                  selectedVideoAsset2,
        int&                  selectedVideoAsset3,
        float&                transitionDuration,
        float&                transitionDuration2,
        float&                transitionDuration3,
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
    
    void drawNLEEditorContent(const UICallbacks& callbacks, const std::string& video1Path, const std::string& video2Path, const std::string& video3Path);
    
    void drawDiagnosticsContent(
        const UIDiagnostics& diag,
        VideoPlayer&         player,
        VideoPlayer&         player2,
        VideoPlayer&         player3,
        VideoRegistry&       registry,
        int&                 selectedAsset,
        int&                 selectedAsset2,
        int&                 selectedAsset3,
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
        int&                  selectedVideoAsset3,
        VideoRandomizerState& randomizer,
        VideoRandomizerState2& randomizer2,
        VideoRandomizerState2& randomizer3,
        float&                transitionDuration,
        float&                transitionDuration2,
        float&                transitionDuration3,
        bool&                 controlsDirty,
        const UIDiagnostics&  diag,
        const UICallbacks&    callbacks,
        const std::string&    video1Path,
        const std::string&    video2Path,
        const std::string&    video3Path,
        std::mt19937&         rng);

    SDL_Window*   window   = nullptr;
    SDL_Renderer* renderer = nullptr;
    ImGuiContext* context  = nullptr;
    bool          initialized = false;
    bool          rendererPaused = false;

    VideoPreviewSlot previewSlotVideo1;
    VideoPreviewSlot previewSlotVideo2;
    VideoPreviewSlot previewSlotVideo3;
    bool previewShuffleRequested[3] = {false, false, false};
    float previewAutoRandomizeElapsed[3] = {0.0f, 0.0f, 0.0f};
    std::mt19937 previewRng{std::random_device{}()};
    
    // Performance option to disable video previews
    bool enableVideoPreviews = true;

    char presetNameBuffer[64] = "";
    std::string renamingPreset;
    std::vector<std::string> postEffectNames;

    void destroyPreviewSlot(VideoPreviewSlot& slot);
    bool loadPreview(VideoPreviewSlot& slot, const std::string& path);
    bool decodePreviewFrame(VideoPreviewSlot& slot);
    bool ensurePreviewTexture(VideoPreviewSlot& slot, int width, int height);
    void updatePreviewSlot(VideoPreviewSlot& slot, float deltaTime);
};
