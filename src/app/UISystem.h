#pragma once

#include <SDL2/SDL.h>
#include <string>
#include <functional>
#include <random>

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

    SDL_Window*   window   = nullptr;
    SDL_Renderer* renderer = nullptr;
    ImGuiContext* context  = nullptr;
    bool          initialized = false;
};
