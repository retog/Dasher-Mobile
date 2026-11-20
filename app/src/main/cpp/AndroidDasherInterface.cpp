/**
 * @file AndroidDasherInterface.cpp
 * @brief Android-specific Dasher rendering and input implementation.
 *
 * Contains:
 * - @c toArgb / @c isUsableColor – colour conversion helpers.
 * - @c AndroidPointerInput – translates touch coordinates into the @c CScreenCoordInput
 *   interface expected by DasherCore.
 * - @c AndroidCommandScreen – a @c CDasherScreen implementation that records every drawing
 *   call as a flat integer command buffer instead of rasterising pixels.  The buffer is
 *   transferred across the JNI boundary each frame and rendered in Kotlin by DasherCanvasView.
 * - @c AndroidDasherInterface – the top-level interface combining settings, screen and input.
 */

#include "AndroidDasherInterface.h"
#include "AndroidFiles.h"
#include "LanguageSet.h"
#include "SymbolText.h"

#include "DasherCore/Alphabet/AlphInfo.h"
#include "DasherCore/AlphabetManager.h"
#include "DasherCore/DasherInput.h"
#include "DasherCore/DasherModel.h"
#include "DasherCore/DasherNode.h"
#include "DasherCore/DasherScreen.h"
#include "DasherCore/DasherView.h"
#include "DasherCore/ExpansionPolicy.h"
#include "DasherCore/ModuleManager.h"
#include "DasherCore/Parameters.h"

#include <android/log.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_set>
#include <utility>
#include <vector>

#define LOG_TAG "DasherInterface"

/**
 * @brief Converts a DasherCore colour to an ARGB packed integer.
 *
 * Handles both floating-point [0, 1] and integer [0, 255] colour component conventions.
 * Near-transparent alpha values (> 0 but < 24) are promoted to fully opaque to avoid
 * invisible rendering artefacts on the Android canvas.
 *
 * @param color DasherCore colour value.
 * @return 32-bit ARGB packed integer (A in bits 31–24, R in 23–16, G in 15–8, B in 7–0).
 */
static int32_t toArgb(const Dasher::ColorPalette::Color &color) {
    int a = color.Alpha;
    int r = color.Red;
    int g = color.Green;
    int b = color.Blue;

    const bool normalized = (a >= 0 && a <= 1) && (r >= 0 && r <= 1) && (g >= 0 && g <= 1) && (b >= 0 && b <= 1);
    if (normalized) {
        a *= 255;
        r *= 255;
        g *= 255;
        b *= 255;
    }

    a = std::clamp(a, 0, 255);
    r = std::clamp(r, 0, 255);
    g = std::clamp(g, 0, 255);
    b = std::clamp(b, 0, 255);

    if (a > 0 && a < 24) {
        a = 255;
    }

    return static_cast<int32_t>((a << 24) | (r << 16) | (g << 8) | b);
}

/**
 * @brief Returns @c true when a DasherCore colour is valid and non-transparent.
 *
 * Rejects colours with any negative component (sentinel "unset" value used by DasherCore)
 * and fully transparent colours (alpha == 0).
 *
 * @param color Colour to test.
 * @return @c true if the colour should be drawn.
 */
static bool isUsableColor(const Dasher::ColorPalette::Color &color) {
    if (color.Red < 0 || color.Green < 0 || color.Blue < 0 || color.Alpha < 0) {
        return false;
    }
    return color.Alpha > 0;
}

/**
 * @brief DasherCore input module that maps Android touch events to screen coordinates.
 *
 * Implements @c CScreenCoordInput so it can be registered with DasherCore's module manager.
 * Coordinates are clamped to the current surface bounds.  While no touch is active the
 * cursor is parked at the centre of the surface.
 */
class AndroidPointerInput : public Dasher::CScreenCoordInput {
public:
    AndroidPointerInput() : Dasher::CScreenCoordInput("Android Touch Input") {}

    /**
     * @brief Updates the known surface dimensions.
     *
     * When no touch is active the cursor is repositioned to the new centre.
     *
     * @param width  Surface width in pixels (clamped to a minimum of 1).
     * @param height Surface height in pixels (clamped to a minimum of 1).
     */
    void SetBounds(int width, int height) {
        m_width = std::max(1, width);
        m_height = std::max(1, height);
        if (!m_hasTouch) {
            m_x = m_width / 2;
            m_y = m_height / 2;
        }
    }

    /**
     * @brief Delivers a touch event and updates the current cursor position.
     *
     * @param action 0 = down (touch active), 1 = move, 2 = up/cancel (touch released).
     * @param x      Raw X coordinate; clamped to [0, width-1].
     * @param y      Raw Y coordinate; clamped to [0, height-1].
     */
    void SetTouch(int action, float x, float y) {
        if (action == 0) {
            m_hasTouch = true;
        } else if (action == 2) {
            m_hasTouch = false;
        }

        const int maxX = std::max(0, m_width - 1);
        const int maxY = std::max(0, m_height - 1);
        m_x = std::clamp(static_cast<int>(std::lround(x)), 0, maxX);
        m_y = std::clamp(static_cast<int>(std::lround(y)), 0, maxY);
    }

    /// @brief Returns the current clamped cursor position. Always returns @c true.
    bool GetScreenCoords(Dasher::screenint &iX, Dasher::screenint &iY, Dasher::CDasherView *) override {
        iX = static_cast<Dasher::screenint>(m_x);
        iY = static_cast<Dasher::screenint>(m_y);
        return true;
    }

private:
    int m_width = 1;    ///< Current surface width.
    int m_height = 1;   ///< Current surface height.
    int m_x = 0;        ///< Current cursor X (clamped).
    int m_y = 0;        ///< Current cursor Y (clamped).
    bool m_hasTouch = false; ///< @c true while a finger is down.
};

/**
 * @brief A @c CDasherScreen that records drawing calls as a flat integer command buffer.
 *
 * Instead of rasterising pixels, every drawing primitive is serialised as a 6-element
 * record @c [op, a, b, c, d, argb] appended to an internal @c std::vector<int32_t>.
 * The buffer is drained once per frame via @ref TakeCommands and sent across the JNI
 * boundary to the Kotlin rendering layer.
 *
 * String labels (op = 5) are stored in a parallel @c std::vector<std::string>; the @c d
 * field of the draw-command record carries the string's index into that vector.
 */
class AndroidCommandScreen final : public Dasher::CDasherScreen {
public:
    /**
     * @param width  Initial surface width in pixels.
     * @param height Initial surface height in pixels.
     */
    AndroidCommandScreen(int width, int height)
        : Dasher::CDasherScreen(static_cast<Dasher::screenint>(width), static_cast<Dasher::screenint>(height)) {}

    /**
     * @brief Resizes the screen and notifies DasherCore.
     * @param width  New width in pixels.
     * @param height New height in pixels.
     */
    void SetSize(int width, int height) {
        resize(static_cast<Dasher::screenint>(width), static_cast<Dasher::screenint>(height));
    }

    /**
     * @brief Clears the command and string buffers and emits a canvas-clear command.
     *
     * Must be called once at the start of each frame before DasherCore calls any draw methods.
     * The background colour used is @c #0D1117 (dark navy), fully opaque.
     */
    void BeginFrame() {
        m_commands.clear();
        m_strings.clear();
        push(0, 0, 0, 0, 0, static_cast<int32_t>(0xFF0D1117));
    }

    /**
     * @brief Returns and clears the accumulated draw-command buffer.
     * @return Flat command array (multiples of 6 elements).
     */
    std::vector<int32_t> TakeCommands() {
        return std::move(m_commands);
    }

    /**
     * @brief Returns and clears the accumulated string label buffer.
     * @return Strings in the order they were emitted by @c DrawString.
     */
    std::vector<std::string> TakeStrings() {
        return std::move(m_strings);
    }

    /**
     * @brief Estimates the rendered size of a text label.
     *
     * Uses a simple proportional approximation (width = char count * fontSize / 2) since
     * the actual font metrics are unknown at the native layer.
     */
    std::pair<Dasher::screenint, Dasher::screenint> TextSize(Label *label, unsigned int iFontSize) override {
        if (!label) return {0, 0};
        const int width = static_cast<int>(label->m_strText.size()) * static_cast<int>(iFontSize) / 2;
        const int height = static_cast<int>(iFontSize);
        return {static_cast<Dasher::screenint>(width), static_cast<Dasher::screenint>(height)};
    }

    void DrawString(Label *label,
                    Dasher::screenint x,
                    Dasher::screenint y,
                    unsigned int iFontSize,
                    const Dasher::ColorPalette::Color &color) override {
        if (!label || label->m_strText.empty() || iFontSize == 0) return;
        const int idx = static_cast<int>(m_strings.size());
        m_strings.push_back(label->m_strText);
        push(5, static_cast<int>(x), static_cast<int>(y), static_cast<int>(iFontSize), idx, toArgb(color));

        static int s_logCounter = 0;
        if (++s_logCounter % 360 == 0) {
            __android_log_print(ANDROID_LOG_DEBUG, "DasherInterface",
                                "DrawString: \"%s\" x=%d y=%d size=%u",
                                label->m_strText.c_str(),
                                static_cast<int>(x), static_cast<int>(y), iFontSize);
        }
    }

    /**
     * @brief Records a rectangle draw command (filled and/or outlined).
     *
     * Emits op=4 (filled rect) when @p color is usable, and op=3 (stroked rect)
     * when @p outlineColor is usable and @p iThickness > 0.
     */
    void DrawRectangle(Dasher::screenint x1,
                       Dasher::screenint y1,
                       Dasher::screenint x2,
                       Dasher::screenint y2,
                       const Dasher::ColorPalette::Color &color,
                       const Dasher::ColorPalette::Color &outlineColor,
                       int iThickness) override {
        if (isUsableColor(color)) {
            push(4, x1, y1, x2, y2, toArgb(color));
        }
        if (iThickness > 0 && isUsableColor(outlineColor)) {
            push(3, x1, y1, x2, y2, toArgb(outlineColor));
        }
    }

    /**
     * @brief Records a circle draw command (filled and/or outlined).
     *
     * Emits op=1 with @c d=1 for a filled circle and op=1 with @c d=0 for an outline circle.
     */
    void DrawCircle(Dasher::screenint iCX,
                    Dasher::screenint iCY,
                    Dasher::screenint iR,
                    const Dasher::ColorPalette::Color &fillColor,
                    const Dasher::ColorPalette::Color &lineColor,
                    int iLineWidth) override {
        if (isUsableColor(fillColor)) {
            push(1, iCX, iCY, iR, 1, toArgb(fillColor));
        }
        if (iLineWidth > 0 && isUsableColor(lineColor)) {
            push(1, iCX, iCY, iR, 0, toArgb(lineColor));
        }
    }

    /**
     * @brief Records a polyline as a sequence of op=2 (line) commands.
     *
     * @param Points Pointer to an array of @p Number points.
     * @param Number Number of points; must be ≥ 2 to emit any commands.
     */
    void Polyline(Dasher::point *Points,
                  int Number,
                  int,
                  const Dasher::ColorPalette::Color &color = {255, 255, 255}) override {
        if (!Points || Number < 2 || !isUsableColor(color)) return;
        const int32_t argb = toArgb(color);
        for (int i = 1; i < Number; ++i) {
            push(2, Points[i - 1].x, Points[i - 1].y, Points[i].x, Points[i].y, argb);
        }
    }

    /**
     * @brief Records a polygon outline as a sequence of op=2 (line) commands.
     *
     * Only the outline is drawn (fill colour is ignored).  The closing edge from the last
     * point back to the first is added automatically.
     */
    void Polygon(Dasher::point *Points,
                 int Number,
                 const Dasher::ColorPalette::Color &,
                 const Dasher::ColorPalette::Color &outlineColor,
                 int lineWidth) override {
        if (!Points || Number < 2 || lineWidth <= 0 || !isUsableColor(outlineColor)) return;
        const int32_t argb = toArgb(outlineColor);
        for (int i = 1; i < Number; ++i) {
            push(2, Points[i - 1].x, Points[i - 1].y, Points[i].x, Points[i].y, argb);
        }
        push(2, Points[Number - 1].x, Points[Number - 1].y, Points[0].x, Points[0].y, argb);
    }

    /// @brief No-op: the command buffer is already "displayed" by being drained each frame.
    void Display() override {}

    /// @brief Always returns @c true; clipping is handled on the Kotlin side.
    bool IsPointVisible(Dasher::screenint, Dasher::screenint) override {
        return true;
    }

private:
    /**
     * @brief Appends a 6-element draw-command record to @ref m_commands.
     *
     * @param op    Primitive opcode (0–5).
     * @param a–d   Primitive parameters (interpretation depends on @p op).
     * @param color Packed ARGB colour from @ref toArgb.
     */
    void push(int op, int a, int b, int c, int d, int32_t color) {
        m_commands.push_back(op);
        m_commands.push_back(a);
        m_commands.push_back(b);
        m_commands.push_back(c);
        m_commands.push_back(d);
        m_commands.push_back(color);
    }

    std::vector<int32_t> m_commands;   ///< Flat draw-command buffer, drained each frame.
    std::vector<std::string> m_strings; ///< String label buffer, drained each frame.
};

namespace {

/**
 * @brief Returns the current time as a millisecond timestamp (monotonic clock).
 * @return Milliseconds since an arbitrary epoch, suitable for passing to DasherCore.
 */
unsigned long nowMs() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<unsigned long>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

}

AndroidSettingsHolder::AndroidSettingsHolder(const std::string &settingsPath) {
    settings = std::make_unique<Dasher::XmlSettingsStore>(settingsPath, nullptr);
    settings->Load();
}

AndroidDasherInterface::AndroidDasherInterface(const std::string &filesDir, const std::string &languages)
    : AndroidSettingsHolder(filesDir + "/dasher_settings.xml")
    , Dasher::CDashIntfScreenMsgs(AndroidSettingsHolder::settings.get())
    , m_languages(DasherAndroid::SetLanguages(languages))
{
    // The file layer offers DasherCore one alphabet, generated for these languages.
    SetStringParameter(Dasher::SP_ALPHABET_ID,
                       DasherAndroid::AlphabetIdFor(DasherAndroid::ParseLanguages(m_languages)));
    // Learning is on unless a host turns it off for one editor (SetLearningEnabled); the value
    // is persisted, so do not let an editor that switched it off outlive the process.
    SetBoolParameter(Dasher::BP_LM_ADAPTIVE, true);
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "created (filesDir=%s)", filesDir.c_str());
    __android_log_print(ANDROID_LOG_INFO,
                        LOG_TAG,
                        "inputFilter=%s",
                        GetStringParameter(Dasher::SP_INPUT_FILTER).c_str());
}

AndroidDasherInterface::~AndroidDasherInterface() {
    FlushTrainingText();
}

void AndroidDasherInterface::CallNewFrame(unsigned long timeMs, bool forceRedraw) {
    NewFrame(timeMs, forceRedraw);
}

void AndroidDasherInterface::CreateModules() {
    Dasher::CDashIntfScreenMsgs::CreateModules();
    auto input = std::make_unique<AndroidPointerInput>();
    m_input = input.get();
    GetModuleManager()->RegisterInputDeviceModule(std::move(input), true);
}

void AndroidDasherInterface::SetScreenSize(int width, int height) {
    if (width <= 0 || height <= 0) {
        return;
    }

    if (!m_screen) {
        m_screen = std::make_unique<AndroidCommandScreen>(width, height);
        ChangeScreen(m_screen.get());
    } else {
        m_screen->SetSize(width, height);
        ScreenResized(m_screen.get());
    }

    if (!m_realized) {
        Realize(nowMs());
        m_realized = true;
        __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "alphabetID=%s",
                            GetStringParameter(Dasher::SP_ALPHABET_ID).c_str());
    }

    if (m_input) {
        m_input->SetBounds(width, height);
    }
}

void AndroidDasherInterface::SetTouch(int action, float x, float y) {
    if (!m_input) {
        return;
    }
    m_input->SetTouch(action, x, y);
    if (action == 0) {
        stopSteering();
    }

    if (action == 0 && !m_startedByTouch) {
        SetBoolParameter(Dasher::BP_START_MOUSE, true);
        KeyDown(nowMs(), Dasher::Keys::Primary_Input);
        m_startedByTouch = true;
    } else if (action == 2 && m_startedByTouch) {
        KeyDown(nowMs(), Dasher::Keys::Primary_Input);
        m_startedByTouch = false;
    }
}

std::vector<int32_t> AndroidDasherInterface::Frame(long timeMs) {
    if (!m_realized || !m_screen) {
        return {};
    }

    if (m_steer.active) {
        advanceSteering();
    }
    m_screen->BeginFrame();
    CallNewFrame(static_cast<unsigned long>(std::max(0L, timeMs)), false);
    return m_screen->TakeCommands();
}

std::vector<std::string> AndroidDasherInterface::TakeFrameStrings() {
    if (!m_screen) return {};
    return m_screen->TakeStrings();
}

int AndroidDasherInterface::GetLanguageModelId() const {
    return static_cast<int>(GetLongParameter(Dasher::LP_LANGUAGE_MODEL_ID));
}

void AndroidDasherInterface::SetLanguageModelId(int modelId) {
    const int resolved = (modelId == 0 || modelId == 2 || modelId == 3 || modelId == 4 || modelId == 5) ? modelId : 0;
    if (GetLongParameter(Dasher::LP_LANGUAGE_MODEL_ID) == static_cast<long>(resolved)) {
        return;
    }
    if (m_startedByTouch) {
        KeyDown(nowMs(), Dasher::Keys::Primary_Input);
        m_startedByTouch = false;
    }
    stopSteering();
    // The switch recreates the alphabet manager, dropping its unsaved training buffer.
    FlushTrainingText();
    SetLongParameter(Dasher::LP_LANGUAGE_MODEL_ID, static_cast<long>(resolved));
}

int AndroidDasherInterface::GetMovementSpeedPercent() const {
    constexpr double kBaseBitrate = 160.0;
    const auto bitrate = static_cast<double>(GetLongParameter(Dasher::LP_MAX_BITRATE));
    const int percent = static_cast<int>(std::lround((bitrate / kBaseBitrate) * 100.0));
    return std::clamp(percent, 20, 400);
}

void AndroidDasherInterface::SetMovementSpeedPercent(int percent) {
    constexpr double kBaseBitrate = 160.0;
    const int resolvedPercent = std::clamp(percent, 20, 400);
    const long requested = static_cast<long>(std::lround((resolvedPercent / 100.0) * kBaseBitrate));
    const long bitrate = std::max(1L, requested);
    if (GetLongParameter(Dasher::LP_MAX_BITRATE) == bitrate) {
        return;
    }
    SetLongParameter(Dasher::LP_MAX_BITRATE, bitrate);
}

unsigned int AndroidDasherInterface::ctrlMove(bool, Dasher::EditDistance) {
    return static_cast<unsigned int>(m_symbolEnds.size());
}

unsigned int AndroidDasherInterface::ctrlDelete(bool, Dasher::EditDistance) {
    return static_cast<unsigned int>(m_symbolEnds.size());
}

void AndroidDasherInterface::editOutput(const std::string &strText, Dasher::CDasherNode *pCause) {
    m_editBuffer += strText;
    m_symbolEnds.push_back(m_editBuffer.size());
    Dasher::CDashIntfScreenMsgs::editOutput(strText, pCause);
}

void AndroidDasherInterface::editDelete(const std::string &strText, Dasher::CDasherNode *pCause) {
    if (!m_symbolEnds.empty()) {
        m_symbolEnds.pop_back();
        m_editBuffer.resize(m_symbolEnds.empty() ? 0 : m_symbolEnds.back());
    }
    Dasher::CDashIntfScreenMsgs::editDelete(strText, pCause);
}

std::string AndroidDasherInterface::GetContext(unsigned int iStart, unsigned int iLength) {
    const size_t count = m_symbolEnds.size();
    if (iLength == 0 || iStart >= count) return {};
    const size_t end = std::min(count, static_cast<size_t>(iStart) + iLength);
    const size_t begin = iStart == 0 ? 0 : m_symbolEnds[iStart - 1];
    return m_editBuffer.substr(begin, m_symbolEnds[end - 1] - begin);
}

std::string AndroidDasherInterface::GetAllContext() {
    return m_editBuffer;
}

int AndroidDasherInterface::GetAllContextLenght() {
    return static_cast<int>(m_symbolEnds.size());
}

std::string AndroidDasherInterface::GetOutputText() const {
    return DasherAndroid::ToPlainText(m_editBuffer);
}

void AndroidDasherInterface::ResetOutputText() {
    stopSteering();
    FlushTrainingText();
    m_editBuffer.clear();
    m_symbolEnds.clear();
    if (m_realized) {
        SetBuffer(0);
    }
}

std::string AndroidDasherInterface::SetContext(const std::string &textBeforeCursor) {
    stopSteering();
    FlushTrainingText();
    m_editBuffer.clear();
    m_symbolEnds.clear();
    const Dasher::CAlphInfo *alphabet = m_realized ? GetActiveAlphabet() : nullptr;
    if (!alphabet) {
        return {};
    }

    if (m_startedByTouch) {
        KeyDown(nowMs(), Dasher::Keys::Primary_Input);
        m_startedByTouch = false;
    }

    std::unordered_set<std::string> known;
    for (int i = 1; i < alphabet->iEnd; ++i) {
        known.insert(alphabet->GetText(i));
    }
    // One symbol per UTF-8 character; restart after any character the alphabet lacks.
    const std::string symbolText = DasherAndroid::ToSymbolText(textBeforeCursor);
    for (size_t i = 0; i < symbolText.size();) {
        size_t next = i + 1;
        while (next < symbolText.size() && (static_cast<unsigned char>(symbolText[next]) & 0xC0) == 0x80) ++next;
        const std::string ch = symbolText.substr(i, next - i);
        if (known.count(ch)) {
            m_editBuffer += ch;
            m_symbolEnds.push_back(m_editBuffer.size());
        } else {
            m_editBuffer.clear();
            m_symbolEnds.clear();
        }
        i = next;
    }
    SetBuffer(static_cast<int>(m_symbolEnds.size()));
    return GetOutputText();
}

void AndroidDasherInterface::SetLearningEnabled(bool enabled) {
    if (GetBoolParameter(Dasher::BP_LM_ADAPTIVE) == enabled) {
        return;
    }
    FlushTrainingText();
    SetBoolParameter(Dasher::BP_LM_ADAPTIVE, enabled);
}

void AndroidDasherInterface::FlushTrainingText() {
    if (m_realized) {
        WriteTrainFileFull();
    }
}


// ---------------------------------------------------------------------------------------------
// Steering into a symbol's box (spoken letters)
// ---------------------------------------------------------------------------------------------

namespace {

/// Frames a zoom and a scroll take.
constexpr int kZoomFrames = 12;
constexpr int kScrollFrames = 10;
/// Zooms and scrolls after which a move is given up; a box is normally reached in two to four.
constexpr int kMaxSteerMoves = 12;
/// Share of the screen height the box moved into fills when the move ends.  The crosshair is
/// half a screen height from the edge the boxes grow from, and a box is as wide as it is high,
/// so above one half the crosshair is inside the box.
constexpr double kFinalShare = 0.6;
/// A box inside it is entered too if it fills more than half the screen; stay clear of any that
/// comes close.
constexpr double kSafeChildShare = 0.45;
/// Share of the screen height a box fills after a zoom that only brings it closer.
constexpr double kApproachShare = 0.15;
/// Height, in Dasher coordinates, below which a box is first brought closer.  Zooms are given
/// in whole coordinates, too coarse to aim into a smaller box.
constexpr double kMinExactHeight = 200;
/// How far, in Dasher coordinates, the crosshair may end up from where a zoom was aimed.
constexpr double kAimError = 2;
/// Height the model's root may reach in a zoom.  Beyond its own limit (2^46) the model moves
/// its root down, in arithmetic that overflows if the limit is passed by far.
constexpr double kMaxRootHeight = 1e13;
/// The crosshair's place on the axis the boxes are stacked along.
constexpr double kCrosshair = static_cast<double>(Dasher::CDasherModel::ORIGIN_Y);

struct Range {
    Dasher::myint min = 0;
    Dasher::myint max = 0;
    Dasher::myint height() const { return max - min; }
};

Range childRange(const Range &parent, const Dasher::CDasherNode *child) {
    const auto norm = static_cast<Dasher::myint>(Dasher::CDasherModel::NORMALIZATION);
    return {parent.min + parent.height() * static_cast<Dasher::myint>(child->Lbnd()) / norm,
            parent.min + parent.height() * static_cast<Dasher::myint>(child->Hbnd()) / norm};
}

/**
 * A view that draws nothing.  It notes which node the model has as its root and where that
 * root is, in Dasher coordinates: the model keeps both to itself, but passes them to the view
 * it renders to.
 *
 * Like a drawing view it also tells the model which boxes cover the whole screen, so the model
 * moves its root down to the innermost of them.  DasherCore's own view never reports a group
 * of symbols as covering the screen, so the root stays at the first symbol written and grows
 * with every zoom, until the zooms scheduled here overflow the model's coordinates.
 */
class RootProbe final : public Dasher::CDasherView {
public:
    /// @param output    The node under the crosshair, handed back so nothing is written.
    /// @param reparents How many times to make the model put the root's parent in its place;
    ///                  if zero, the root is moved down instead.
    RootProbe(Dasher::CDasherScreen *screen, Dasher::CDasherNode *output, int reparents)
        : Dasher::CDasherView(screen, Dasher::Options::LeftToRight)
        , m_output(output), m_reparents(reparents), m_moveRootDown(reparents == 0) {}

    /// The model's root once the render is over, and where it is.
    Dasher::CDasherNode *root = nullptr;
    Range rootRange;

    Dasher::CDasherNode *Render(Dasher::CDasherNode *pRoot, Dasher::myint iRootMin, Dasher::myint iRootMax,
                                Dasher::CExpansionPolicy &) override {
        root = pRoot;
        rootRange = {iRootMin, iRootMax};
        std::vector<Dasher::CDasherNode *> path;   // From the node under the crosshair up to the root.
        Dasher::CDasherNode *node = m_output;
        for (; node && node != pRoot; node = node->Parent()) path.push_back(node);
        if (m_moveRootDown && node == pRoot) {
            const Dasher::myint screen = Dasher::CDasherModel::MAX_Y;
            for (auto it = path.rbegin(); it != path.rend(); ++it) {
                const Range range = childRange(rootRange, *it);
                if (range.min > 0 || range.max < screen) break;
                // The model makes a root's child the root if it is the only one drawn and
                // covers the screen.
                root->onlyChildRendered = *it;
                (*it)->SetFlag(Dasher::CDasherNode::NF_SUPER, true);
                root = *it;
                rootRange = range;
            }
            root->onlyChildRendered = nullptr;
        }
        return m_output;
    }
    bool IsSpaceAroundNode(Dasher::myint, Dasher::myint) const override {
        if (m_reparents <= 0) return false;
        --m_reparents;
        return true;
    }
    void Screen2Dasher(Dasher::screenint, Dasher::screenint, Dasher::myint &x, Dasher::myint &y) const override {
        x = y = 0;
    }
    void Dasher2Screen(Dasher::myint, Dasher::myint, Dasher::screenint &x, Dasher::screenint &y) const override {
        x = y = 0;
    }
    void Dasher2Polar(Dasher::myint, Dasher::myint, double &r, double &theta) const override {
        r = theta = 0;
    }
    DasherCoordScreenRegion VisibleRegion() const override {
        return {0, 0, Dasher::CDasherModel::MAX_Y, Dasher::CDasherModel::MAX_Y};
    }
    void DasherSpaceArc(Dasher::myint, Dasher::myint, Dasher::myint, Dasher::myint, Dasher::myint, Dasher::myint,
                        const Dasher::ColorPalette::Color &, int) const override {}
    void DasherLine2Screen(Dasher::myint, Dasher::myint, Dasher::myint, Dasher::myint,
                           std::vector<Dasher::point> &) const override {}

private:
    Dasher::CDasherNode *m_output;
    mutable int m_reparents;
    bool m_moveRootDown;
};

/// Where @p node is, given where the model's root is; false if the two are not in one line of
/// descent.
bool rangeOf(Dasher::CDasherNode *node, Dasher::CDasherNode *root, const Range &rootRange, Range &out) {
    std::vector<Dasher::CDasherNode *> chain;
    Dasher::CDasherNode *n = node;
    for (; n && n != root; n = n->Parent()) chain.push_back(n);
    if (n == root) {
        out = rootRange;
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) out = childRange(out, *it);
        return true;
    }
    // The node is above the root: work outwards from the root.
    const auto norm = static_cast<double>(Dasher::CDasherModel::NORMALIZATION);
    double min = static_cast<double>(rootRange.min);
    double max = static_cast<double>(rootRange.max);
    for (n = root; n && n != node; n = n->Parent()) {
        if (n->Range() == 0) return false;
        const double parentHeight = (max - min) * norm / n->Range();
        min -= parentHeight * n->Lbnd() / norm;
        max = min + parentHeight;
    }
    if (n != node) return false;
    out = {static_cast<Dasher::myint>(std::llround(min)), static_cast<Dasher::myint>(std::llround(max))};
    return true;
}

bool isGroup(Dasher::CDasherNode *node) {
    return dynamic_cast<Dasher::CGroupNode *>(node) != nullptr;
}

/// The node of the symbol written last as seen from @p node: itself, or the nearest node above
/// it that is not a group of symbols.  The node of an empty text is a group without a parent.
Dasher::CDasherNode *symbolNodeOf(Dasher::CDasherNode *node) {
    while (node && isGroup(node) && node->Parent()) node = node->Parent();
    return node;
}

/// The symbol boxes directly inside @p parent, looking through groups, each with the share of
/// @p parent it takes (from, to).
struct SymbolBox {
    Dasher::CSymbolNode *node;
    double from;
    double to;
};

void collectSymbols(Dasher::CDasherModel *model, Dasher::CDasherNode *parent, double from, double to,
                    std::vector<SymbolBox> &out) {
    model->ExpandNode(parent);
    const auto norm = static_cast<double>(Dasher::CDasherModel::NORMALIZATION);
    for (Dasher::CDasherNode *child : parent->GetChildren()) {
        const double childFrom = from + (to - from) * child->Lbnd() / norm;
        const double childTo = from + (to - from) * child->Hbnd() / norm;
        if (auto *symbol = dynamic_cast<Dasher::CSymbolNode *>(child)) {
            out.push_back({symbol, childFrom, childTo});
        } else if (isGroup(child)) {
            collectSymbols(model, child, childFrom, childTo, out);
        }
    }
}

/// The point of a box, as a share of its height, to put the crosshair on so that no box inside
/// it is entered as well: its middle, unless a box there would fill half the screen.
double safePoint(const std::vector<SymbolBox> &boxes) {
    const auto tooBig = [](const SymbolBox &box) {
        return (box.to - box.from) * kFinalShare >= kSafeChildShare;
    };
    for (const SymbolBox &box : boxes) {
        if (box.from <= 0.5 && 0.5 < box.to && !tooBig(box)) return 0.5;
    }
    const SymbolBox *best = nullptr;
    for (const SymbolBox &box : boxes) {
        if (tooBig(box)) continue;
        if (!best || box.to - box.from > best->to - best->from) best = &box;
    }
    return best ? (best->from + best->to) / 2 : 0.5;
}

}

void AndroidDasherInterface::stopSteering() {
    if (!m_steer.active) return;
    m_steer = Steering();
    if (m_pDasherModel) m_pDasherModel->ClearScheduledSteps();
}

bool AndroidDasherInterface::SteerToSymbol(const std::string &plainText) {
    if (!m_realized || !m_pDasherModel || plainText.empty()) return false;
    const std::string symbol = DasherAndroid::ToSymbolText(plainText);
    const Dasher::CAlphInfo *alphabet = GetActiveAlphabet();
    bool known = false;
    for (int i = 1; alphabet && i < alphabet->iEnd && !known; ++i) {
        known = alphabet->GetText(i) == symbol;
    }
    if (!known) return false;
    if (m_startedByTouch) {
        KeyDown(nowMs(), Dasher::Keys::Primary_Input);
        m_startedByTouch = false;
    }
    stopSteering();
    m_steer.active = true;
    m_steer.symbol = symbol;
    m_steer.from = symbolNodeOf(m_pDasherModel->Get_node_under_crosshair());
    advanceSteering();
    return m_steer.active;
}

bool AndroidDasherInterface::SteerBack() {
    if (!m_realized || !m_pDasherModel || m_symbolEnds.empty()) return false;
    if (m_startedByTouch) {
        KeyDown(nowMs(), Dasher::Keys::Primary_Input);
        m_startedByTouch = false;
    }
    stopSteering();
    m_steer.active = true;
    m_steer.back = true;
    m_steer.from = symbolNodeOf(m_pDasherModel->Get_node_under_crosshair());
    advanceSteering();
    return m_steer.active;
}

void AndroidDasherInterface::advanceSteering() {
    if (m_steer.framesLeft > 0) {
        --m_steer.framesLeft;
        return;
    }
    Dasher::CDasherModel *model = m_pDasherModel.get();
    Dasher::CDasherNode *under = model ? model->Get_node_under_crosshair() : nullptr;
    Dasher::CDasherNode *current = symbolNodeOf(under);
    // In another symbol's box than at the start, the move is over: the box aimed for was
    // entered (or left, moving back), or the user moved elsewhere.
    if (!current || current != m_steer.from || !m_screen || m_steer.moves >= kMaxSteerMoves) {
        stopSteering();
        return;
    }

    // The model's movements are given as the stretch of the axis that is to fill the screen:
    // here "window" coordinates high, with "centre" in the middle, i.e. under the crosshair.
    const double screenHeight = static_cast<double>(Dasher::CDasherModel::MAX_Y);
    double rootHeight = 0;
    const auto move = [&](double centre, double window, int frames) {
        // Keep the root within the model's coordinates; the next zoom goes on from there.
        window = std::max(window, screenHeight * rootHeight / kMaxRootHeight);
        const auto y1 = static_cast<Dasher::myint>(std::llround(centre - window / 2));
        const auto y2 = y1 + std::max<Dasher::myint>(1, static_cast<Dasher::myint>(std::llround(window)));
        model->ScheduleZoom(y1, y2, frames);
        m_steer.framesLeft = frames;
        ++m_steer.moves;
    };

    Dasher::NoExpansions noExpansions;
    if (m_steer.back) {
        // The node of the symbol before.  After text was loaded the model has yet to rebuild
        // the nodes above its root, which it does when a view reports space around the root.
        Dasher::CDasherNode *before = nullptr;
        Dasher::CDasherNode *previous = nullptr;
        Dasher::CDasherNode *root = nullptr;
        Range rootRange;
        for (int attempt = 0; attempt < 8 && !before; ++attempt) {
            RootProbe probe(m_screen.get(), under, 1);
            model->RenderToView(&probe, noExpansions);
            root = probe.root;
            rootRange = probe.rootRange;
            Dasher::CDasherNode *candidate = current->Parent() ? symbolNodeOf(current->Parent()) : nullptr;
            if (candidate && !isGroup(candidate)) {
                before = candidate;
            } else if (candidate && candidate == previous) {
                // A group with nothing above it even after rebuilding: the start of the text.
                before = candidate;
            }
            previous = candidate;
        }
        Range box;
        if (!before || !root || !rangeOf(before, root, rootRange, box)) {
            stopSteering();
            return;
        }
        rootHeight = static_cast<double>(rootRange.height());
        const double height = std::max<double>(1.0, static_cast<double>(box.height()));
        if (!m_steer.zoomedOut) {
            // Zoom out, the crosshair staying where it is, until the box before fills the
            // screen as a box moved into does.  That alone takes the crosshair out of the
            // last symbol's box, unless that box is most of the one before.
            m_steer.zoomedOut = true;
            move(kCrosshair, height / kFinalShare, kZoomFrames);
        } else {
            std::vector<SymbolBox> inside;
            collectSymbols(model, before, 0, 1, inside);
            move(static_cast<double>(box.min) + safePoint(inside) * height, screenHeight, kScrollFrames);
        }
        return;
    }

    std::vector<SymbolBox> boxes;
    collectSymbols(model, current, 0, 1, boxes);
    const SymbolBox *target = nullptr;
    for (const SymbolBox &box : boxes) {
        if (box.node->outputText() == m_steer.symbol) target = &box;
    }
    RootProbe probe(m_screen.get(), under, 0);
    model->RenderToView(&probe, noExpansions);
    Range currentRange;
    if (!target || !probe.root || !rangeOf(current, probe.root, probe.rootRange, currentRange)) {
        stopSteering();
        return;
    }
    rootHeight = static_cast<double>(probe.rootRange.height());

    // Boxes are placed by their share of the current box, finer than whole coordinates.
    struct Span {
        double top;
        double bottom;
        bool isTarget;
    };
    const double origin = static_cast<double>(currentRange.min);
    const double scale = static_cast<double>(currentRange.height());
    std::vector<Span> spans;
    for (const SymbolBox &box : boxes) {
        spans.push_back({origin + box.from * scale, origin + box.to * scale, &box == target});
    }
    const double top = origin + target->from * scale;
    const double bottom = origin + target->to * scale;
    const double height = bottom - top;

    // Whether a box other than the target's is entered while the crosshair goes from one
    // point to another and the screen comes to show "window" coordinates: one it passes
    // over that fills close to half the screen by the end.
    const auto entersOther = [&](double from, double to, double window) {
        const double low = std::min(from, to) - kAimError;
        const double high = std::max(from, to) + kAimError;
        for (const Span &span : spans) {
            if (span.isTarget || span.bottom < low || span.top > high) continue;
            if (span.bottom - span.top >= kSafeChildShare * window) return true;
        }
        return false;
    };
    // Scrolls a point level with the crosshair and zooms until the screen shows "window"
    // coordinates around it: in one movement if no other box is entered on the way, else
    // the scroll first, at a scale where the boxes are smaller.
    const auto approach = [&](double point, double window) {
        const bool level = std::abs(point - kCrosshair) <= kAimError / 2;
        if (level || !entersOther(kCrosshair, point, window)) {
            move(level ? kCrosshair : point, window, kZoomFrames);
        } else {
            move(point, screenHeight, kScrollFrames);
        }
    };

    if (height >= kMinExactHeight) {
        // Big enough to aim into: make it fill the screen as far as enters it, with the
        // crosshair on a point where no box inside it is entered as well.
        std::vector<SymbolBox> inside;
        collectSymbols(model, target->node, 0, 1, inside);
        for (const SymbolBox &box : inside) {
            spans.push_back({top + box.from * height, top + box.to * height, false});
        }
        approach(top + safePoint(inside) * height, height / kFinalShare);
        return;
    }

    // Too small to aim into: bring it closer.  The crosshair may end up beside it, so it is
    // put where every box near it stays too small to be entered.
    const double middle = std::round((top + bottom) / 2);
    for (double window = std::max(8.0, height / kApproachShare); window < screenHeight; window *= 2) {
        for (double shift = 0; shift <= window / 4; shift += kAimError) {
            for (const double point : {middle + shift, middle - shift}) {
                if (entersOther(point, point, window)) continue;
                approach(point, window);
                return;
            }
        }
    }
    // Wedged between boxes too big to come closer without entering one of them.
    stopSteering();
}
