# Renderer Strategy Research: Terminal-Buddy on Snapdragon X Elite

> Researched 2026-04-07. Target: Surface Pro with Snapdragon X Elite, Windows 11 ARM64.

## Current State

The app uses `SDL_CreateRenderer(state.window, "software")` with window flags
`SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_TRANSPARENT | SDL_WINDOW_NOT_FOCUSABLE`.

---

## 1. Renderer Tradeoffs on Windows ARM64

### SDL3 + Direct3D 11 (`"direct3d11"`) -- Recommended

- **Transparent window support: YES** -- uses `DXGI_SWAP_EFFECT_DISCARD`, which DWM needs for per-pixel alpha composition
- SDL3's default on Windows (first in the driver priority array ahead of D3D12)
- Mature driver path on Adreno X1 -- Chips & Cheese analysis notes the Adreno X1 "still feels like a GPU optimized for the DirectX 11 era where pixel shader work dominates"
- Lowest baseline memory of the hardware-accelerated options
- Fully supports the current Clay -> SDL_Renderer -> draw-calls pipeline with zero code changes beyond the renderer string

### SDL3 + Direct3D 12 (`"direct3d12"`) -- Not Viable

- **Transparent window support: NO** -- SDL3 explicitly rejects transparent windows: `"The direct3d12 renderer doesn't work with transparent windows"` (SDL source: `SDL_render_d3d12.c:3499-3501`). `DXGI_SWAP_EFFECT_DISCARD` was removed in D3D12.
- Higher baseline memory (64MB heap granularity, explicit descriptor heaps, manual triple-buffering)
- No advantage for a 2D overlay with low draw-call counts
- **Eliminate this option entirely**

### SDL3 + Software (`"software"`) -- Current Choice

- Works, but all rendering is CPU-bound -- wasteful when idle GPU silicon is available
- Had transparency bugs (fixed in PR #13866), verify on SDL 3.4.4+
- Higher CPU power draw under animation (processing bars, keyboard later)
- Reasonable as a **fallback** but not ideal as the primary path
- On Snapdragon X Elite, the CPU efficiency cores handle this fine for the current 96x96 idle bubble, but it won't scale well to a full on-screen keyboard

### SDL3 + Vulkan (`"vulkan"`)

- **Transparent window support: YES** -- sets `compositeAlpha` correctly
- Native Vulkan drivers from Qualcomm are now available for Adreno X1 (Vulkan 1.3). Earlier devices shipped with only "Dzn" -- a Vulkan-over-D3D12 translation layer with ~50% perf penalty. Native drivers show 2-3x improvement.
- More complex driver situation -- users may be on Dzn or native depending on driver version
- Viable future option but D3D11 is more battle-tested today

### SDL3 + OpenGL (`"opengl"`) -- Avoid

- Only available via the Microsoft OpenCL/OpenGL Compatibility Pack (Mesa -> D3D12 translation)
- Capped at OpenGL 3.3
- `GlU32.Lib` is not in the Windows SDK for ARM64
- Unnecessary indirection when D3D11 is available natively

### SDL3 + GPU API (`"gpu"`) -- Not Viable

- **Transparent window support: NO** -- `SDL_ClaimWindowForGPUDevice()` fails on transparent windows. Confirmed by SDL maintainer Sam Lantinga in [Issue #12410](https://github.com/libsdl-org/SDL/issues/12410).
- Uses D3D12 under the hood on Windows.

### Custom Direct2D / DirectComposition

- **Theoretically optimal** for this exact use case -- Direct2D provides hardware-accelerated 2D primitives, DirectWrite gives superior text rendering, DirectComposition with `WS_EX_NOREDIRECTIONBITMAP` is the fastest transparent-window path on Windows (GPU-only, no CPU round-trip)
- **Implementation cost is high from plain C** -- all COM interfaces, verbose initialization, manual reference counting. Feasible but ~3-5x more code than the SDL path for equivalent functionality.
- DirectWrite text quality advantage over SDL_ttf is negligible at high DPI (see Text Rendering section below)
- Makes sense only if the app outgrows SDL's rendering capabilities

---

## 2. GPU Driver Memory on Snapdragon X Elite

When switching from `"software"` to `"direct3d11"`, the Qualcomm Adreno driver stack (`qcdx11*.dll`, `adreno_utils.dll`, shader compiler, etc.) will be loaded. Expected impact:

| Memory category | What it is | Concerning? |
|---|---|---|
| **Image (DLL mappings)** | Adreno driver DLLs mapped into address space. Typically 30-80MB virtual, much is **shareable**. | No -- shared with all D3D11 apps |
| **Shader compiler** | Runtime shader compiler loaded eagerly on first D3D11 device creation. | Normal -- one-time cost, lazy-paged |
| **Private working set increase** | Expect ~15-30MB increase over software renderer | Normal for any D3D11 app on this hardware |
| **Virtual size increase** | May appear large (100MB+) but most is reserved, not committed | Cosmetic -- not real resource consumption |

**Use VMMap** (Sysinternals) to distinguish: `Private WS` is what matters. `Shareable WS` and `Image` sections for driver DLLs are shared system-wide.

---

## 3. SDL3 Renderer Backend Details

From SDL3 source (`SDL_render.c:110-151`), the compile-time render driver priority on Windows:

| # | Backend Name | Available on Win ARM64? | Transparent Windows? |
|---|---|---|---|
| 1 | `"direct3d11"` | Yes | **Yes** |
| 2 | `"direct3d12"` | Yes | **No** |
| 3 | `"direct3d"` (D3D9) | Yes (via D3D9-on-D3D12 translation) | No |
| 4 | `"opengl"` | Yes (via Compatibility Pack only) | No |
| 5 | `"vulkan"` | Yes (Adreno X1: Vulkan 1.3) | **Yes** |
| 6 | `"gpu"` | Yes (uses D3D12 internally) | **No** |
| 7 | `"software"` | Yes | Yes (buggy, fixed in PR #13866) |

Specifying a renderer:
```c
// Explicit with fallback chain:
SDL_Renderer *renderer = SDL_CreateRenderer(window, "direct3d11,software");

// Or via hint before creation:
SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d11");
SDL_Renderer *renderer = SDL_CreateRenderer(window, NULL);
```

---

## 4. DirectComposition vs SDL3 -- Is It Worth It?

The gain over SDL3+D3D11:
- ~10-20MB less private working set (no SDL overhead, thinner driver path)
- Slightly lower composition latency (direct swap chain -> DWM, no redirection surface)
- Better text rendering via DirectWrite (marginal at high DPI)

The cost:
- Rewriting the Clay renderer backend from SDL3 draw calls to Direct2D draw calls
- Managing COM lifetime, D2D factories, render targets, swap chains manually in C
- Losing cross-platform portability
- ~2-4 weeks of implementation work for equivalent functionality

**Verdict: Not worth it now. Consider only if the on-screen keyboard needs pixel-perfect text at low DPI or absolute minimum memory footprint.**

---

## 5. Text Rendering: DirectWrite vs SDL_ttf

DirectWrite's advantages over SDL_ttf (FreeType):
- **Hinting/grid-fitting**: Microsoft's proprietary hinting engine tuned for Windows system fonts. Diverges most at 9-14pt on non-HiDPI screens.
- **Subpixel positioning**: Fractional-pixel glyph placement by default.
- **ClearType**: LCD subpixel rendering. But Windows 11 on HiDPI defaults to greyscale AA anyway.
- **Shaping**: Integrated with Windows shaping engine. But SDL_ttf 3.x integrates HarfBuzz, closing this gap.

**At Surface Pro's ~267 PPI, hinting differences are invisible.** Both engines produce good results at high DPI. The visual difference is negligible for English text in a dictation overlay and on-screen keyboard.

Both paths ultimately:
1. Rasterize glyphs to bitmaps (CPU)
2. Upload to a texture/atlas (CPU -> GPU)
3. Draw quads at glyph positions (GPU)

DirectWrite can shortcut step 2 by rendering directly to a D2D render target (backed by D3D11 texture). But SDL_ttf's `TTF_TextEngine` also caches glyph textures GPU-side, so steady-state cost is similar.

**Don't let text rendering be the reason to leave SDL.**

---

## 6. Windows ARM / Snapdragon X Elite Gotchas

| Gotcha | Details |
|---|---|
| **D3D11 rendering artifacts** | Chips & Cheese reported Civ 5 (D3D11) "suffers rendering artifacts on Adreno X1." Test rounded-rect geometry carefully. |
| **Driver update quality** | Qualcomm's driver releases have been problematic -- manual CAB extraction, stability regressions. Pin a known-good driver version. |
| **OpenGL is a dead end** | Capped at 3.3, translation-layer only, missing SDK libs. |
| **Vulkan driver fragmentation** | Users may have Dzn (translation) or native Qualcomm Vulkan. Performance varies 2-5x. |
| **IFPC power management** | Adreno X1 supports Inter Frame Power Collapse -- GPU powers down between frames. Event-driven render loops enable this automatically. Only present frames when `needs_redraw` is true. |
| **WS_EX_TOPMOST z-order bug** | Windows 11 (all architectures): opening Paint can cause topmost windows to temporarily lose z-order. Known DWM bug, not ARM-specific. |
| **Build natively** | ARM64-native vs x86 emulation: 40% less GPU power draw, 2.5-3x less CPU overhead. |
| **DWM composition** | No ARM64-specific DWM bugs documented. Same code path as x64. |
| **WS_EX_NOACTIVATE + WS_EX_TOPMOST** | Behave identically on ARM64 and x64. No ARM-specific differences. |

---

## 7. Clay Renderer Architecture Recommendation

**Now (MVP):** Stay on SDL3 renderer. Change `"software"` -> `"direct3d11,software"`. One line change, zero other code modifications.

**Medium-term (on-screen keyboard):** Still SDL3+D3D11. The Clay SDL3 renderer (`third_party/clay/clay_renderer_SDL3.c`) already handles rounded rects via vertex geometry, text via SDL_ttf, borders, and clipping. Optimize by:
- Caching `TTF_Text` objects instead of recreating per frame
- Using texture atlases for keyboard key glyphs
- Only re-rendering dirty regions

**Long-term (if the app becomes a rich overlay suite):** Write a Clay -> Direct2D renderer backend. This gives:
- DirectWrite for production-quality text
- Hardware-accelerated rounded rects, anti-aliased shapes
- DirectComposition for optimal transparent window performance
- But commits to Windows-only

---

## 8. Benchmarking Strategy

| Metric | Tool | Target |
|---|---|---|
| **Private Working Set** | VMMap (Sysinternals) | <30MB with D3D11, <10MB with software |
| **Private Bytes** | Process Explorer | Should be close to Private WS |
| **Idle CPU** | Process Explorer (CPU column) | 0.0% when not animating. >0.1% idle = render loop bug. |
| **Frame time (drag)** | `SDL_GetPerformanceCounter()` bracketing `SDL_RenderPresent()` | <4ms |
| **Frame time (keyboard animation)** | Same | <8ms |
| **Battery impact** | `powercfg /srumutil` or Windows battery report | Compare idle drain with/without app |
| **Startup latency** | `QueryPerformanceCounter` from `main()` to first `SDL_RenderPresent()` | <200ms. D3D11 adds 50-100ms over software. |
| **GPU memory** | Task Manager -> Performance -> GPU | <20MB dedicated |

### Testing Matrix

| Config | Renderer | Purpose |
|---|---|---|
| A | `"software"` | Baseline (current) |
| B | `"direct3d11"` | **Primary target** |
| C | `"direct3d11,software"` | Fallback chain validation |
| D | `"vulkan,direct3d11,software"` | Future-proofing test |

Run each through: idle 60s -> tap to listen -> process -> paste transcript -> drag window -> repeat 10x.

---

## 9. Concrete Recommendation Summary

| | Choice | Why |
|---|---|---|
| **MVP/default** | `"direct3d11"` | Transparent window support, lowest-overhead HW-accelerated path, SDL3's own default, good Adreno X1 compatibility |
| **Fallback** | `"software"` | Already proven working, zero driver dependencies |
| **Renderer string** | `"direct3d11,software"` | One line change, automatic fallback |
| **Long-term best** | Custom Clay -> Direct2D backend + DirectComposition | Only if app grows to need DirectWrite text quality or absolute minimum footprint |
| **Avoid** | `"direct3d12"`, `"gpu"`, `"opengl"` | D3D12/GPU can't do transparent windows; OpenGL is translation-layer-only on ARM64 |

---

## Key Sources

- [SDL3 renderer priority and D3D11 default](https://discourse.libsdl.org/t/sdl-use-direct3d11-as-the-default-renderer-on-windows/49482)
- [SDL3 D3D11 transparent window support](https://discourse.libsdl.org/t/sdl-enable-transparent-windows-when-using-the-d3d11-renderer/47365)
- [SDL3 D3D12 rejects transparent windows / GPU API limitation](https://github.com/libsdl-org/SDL/issues/12410)
- [Adreno X1 GPU analysis -- Chips and Cheese](https://chipsandcheese.com/p/the-snapdragon-x-elites-adreno-igpu)
- [DirectComposition for high-perf transparent windows](https://learn.microsoft.com/en-us/archive/msdn-magazine/2014/june/windows-with-c-high-performance-window-layering-using-the-windows-composition-engine)
- [Qualcomm IFPC power management](https://www.phoronix.com/news/Qualcomm-Adreno-IFPC)
- [VMMap for memory analysis](https://learn.microsoft.com/en-us/sysinternals/downloads/vmmap)
- [D3D11On12 overhead characteristics](https://learn.microsoft.com/en-us/windows/win32/direct3d12/direct3d-11-on-12)
- [OpenGL Compatibility Pack for ARM64](https://devblogs.microsoft.com/directx/announcing-the-opencl-and-opengl-compatibility-pack-for-windows-10-on-arm/)
- [SDL3/SDL_CreateRenderer Wiki](https://wiki.libsdl.org/SDL3/SDL_CreateRenderer)
- [SDL3/SDL_HINT_RENDER_DRIVER Wiki](https://wiki.libsdl.org/SDL3/SDL_HINT_RENDER_DRIVER)
- [D3D12 Memory Management Strategies](https://learn.microsoft.com/en-us/windows/win32/direct3d12/memory-management-strategies)
