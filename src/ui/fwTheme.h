// Token-based theming, copied from fwcom (FreeWili).
//
// A theme is 17 semantic colors. Everything else -- the ~50 ImGui style
// colors, the style metrics, the markdown palette, the ImPlot style -- is
// derived from them, so adding a theme means picking 17 colors rather than
// maintaining a 50-entry list by hand and hoping nothing was missed.
#ifndef FWTHEME_H
#define FWTHEME_H

#include "imgui.h"

struct fwThemeTokens
{
    const char* szName;
    bool        bIsDark;      // chooses the ImGui base style to layer over

    ImU32 background;         // window bg   -- MUST be alpha 255 (viewports)
    ImU32 surface;             // panels, child regions
    ImU32 surfaceInput;       // text fields, buttons
    ImU32 surfacePopup;       // menus, combos, tooltips -- MUST be alpha 255
    ImU32 border;

    ImU32 text;
    ImU32 textMuted;
    ImU32 textFaint;

    ImU32 accent;
    ImU32 link;

    ImU32 codeBackground;
    ImU32 inlineCodeBackground;
    ImU32 inlineCodeText;

    ImU32 selection;          // carries its own alpha; drawn over content

    ImU32 success;
    ImU32 warning;
    ImU32 danger;
};

// Colors the widget helpers, markdown renderer and plot theming draw with.
// Rebuilt whenever a theme is applied.
struct fwPalette
{
    ImU32 text, textMuted, textFaint;
    ImU32 heading;
    ImU32 rule, quoteBar;
    ImU32 link;

    ImU32 surface, surfaceHovered, surfaceActive;
    ImU32 border, borderHovered;

    ImU32 codeBackground;
    ImU32 inlineCodeBackground, inlineCodeText;

    ImU32 accent, accentHovered, accentActive, accentText;

    ImU32 success, warning, danger, neutral;

    // Pill/status fills: the state color at low alpha, for filled backgrounds.
    ImU32 successFill, warningFill, dangerFill, neutralFill, accentFill;

    ImU32 plotFrame, plotGrid, plotAxisText, plotLegendBg;
};

int                   fwThemeCount();
const fwThemeTokens&  fwThemeAt(int iIndex);          // clamps to range
int                   fwThemeIndexByName(const char* szName);  // 0 if unknown

// Pure settings-load mapping, shared by fwguiAppManager's defaults.txt
// loader and the e2e suite so there is exactly one implementation of the
// rule. Handles both persisted keys:
//   "imguithemename" -- looked up by name via fwThemeIndexByName (0/Wili
//                        Dark if the name is unknown).
//   "imguitheme"      -- legacy integer key from before themes were named.
//                        The old order was Wili, Dark, Light, Classic; only
//                        Wili and Light survive, so old index 2 maps to
//                        "Light" and everything else (including
//                        out-of-range or non-numeric values) lands on the
//                        default (0/Wili Dark).
// Returns -1 if szKey is neither of the above, so the caller can tell
// "not a theme setting" apart from "resolved to theme 0".
int fwResolveThemeFromSettingKV(const char* szKey, const char* szValue);

const fwPalette&      fwPaletteCurrent();
fwPalette             fwBuildPalette(const fwThemeTokens& t);

// Blend toward white on dark themes, toward black on light ones, so one
// "slightly brighter on hover" rule reads correctly in both directions.
ImU32 fwLift(ImU32 color, float fAmount, bool bIsDark);
ImU32 fwWithAlpha(ImU32 color, int iAlpha);

// WCAG relative-luminance contrast ratio, 1.0 .. 21.0. Alpha is ignored.
float fwContrastRatio(ImU32 a, ImU32 b);

// Rebuilds the entire ImGui style -- colors AND metrics -- from the theme's
// tokens, then scales sizes by fSizeScale.
//
// Sizes are reset from constants on every call, so this is idempotent.
// ScaleAllSizes multiplies the current metrics and is NOT idempotent on its
// own; that is why the reset is mandatory rather than tidy.
//
// Call between frames only: it replaces the whole style, and applying it
// mid-frame gives inconsistent metrics for the rest of that frame.
void fwApplyThemeNow(int iIndex, float fSizeScale);

// Application appearance: user scale and monitor DPI are independent.
// These entry points run between frames and scale fonts along with metrics.
float fwClampUiScale(float scale);
float fwUiScaleFromSetting(const char* value);
void fwApplyAppearanceNow(int theme, float uiScale, float dpiScale);
void fwPumpAppearance(float uiScale, float dpiScale);


int  fwThemeCurrentIndex();

// Records a theme change to be applied at the next frame boundary. Safe to
// call from inside a frame (e.g. a menu item handler); fwApplyThemeNow is not.
void fwRequestTheme(int iIndex);

// Applies any pending theme. Call immediately before ImGui::NewFrame().
// A no-op when nothing is pending, so it is cheap to call every frame.
void fwPumpPendingTheme(float fSizeScale);

bool fwHasPendingTheme();

// Number of times fwApplyThemeNow has actually run, process-lifetime. Exists
// so tests can prove a pump did (or did not) do work -- a style memcmp alone
// cannot distinguish "no-op" from "re-applied the same theme", since
// fwApplyThemeNow is deterministic and produces byte-identical output either
// way.
unsigned fwThemeApplyCountForTests();

#endif // FWTHEME_H
