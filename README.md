# davepdf

davepdf is a portable native PDF viewer for Windows x64, built in C++20 with SDL3 for the window and input, Dear ImGui (docking branch) for the interface and PDFium for rendering. It opens a PDF, shows the pages as a continuously scrolled stack with thumbnails, outline, search, links and document properties, and remembers its theme, scale, layout and recent files.

## Run

Run `davepdf.exe` and keep `assets` and `pdfium.dll` beside it. No installer, Visual C++ redistributable, network connection or other runtime is needed: SDL, Dear ImGui and the C runtime are linked statically, and PDFium ships as the one DLL in the folder. Fonts, the PDFium library, settings and the dock layout are all resolved relative to the executable, so the folder keeps working when it is launched from a shortcut, another drive or a different working directory. If the folder is read-only, settings go to `%LOCALAPPDATA%\davepdf` instead.

The build stages the application in `C:\buildfiles\davepdf\release\portable\davepdf`. Produce a clean copy for distribution with:

```powershell
cmake --install C:\buildfiles\davepdf\release --prefix C:\buildfiles\davepdf\dist\davepdf
```

Command line: `davepdf.exe file.pdf` opens a document, `davepdf.exe --sample` opens the built-in sample, `davepdf.exe --smoke` renders eight frames and exits, and `davepdf.exe --shot out.bmp` saves a screenshot of the first frames and exits. Dropping a PDF onto the window opens it.

## What it does

- **Opening**: native file dialog, drag and drop, command line, a recent-files list (menu and welcome screen) and a generated three-page sample document. Encrypted documents prompt for their password. Files that are missing, damaged or not PDFs are reported in a modal message and the current document stays open.
- **Reading**: all pages in one scrolled column, centred, with soft shadows. Scroll with the wheel, drag the page, or use the keyboard (arrows, Page Up/Down, Space, Home, End). The page counter follows the page that fills most of the view; type a number into it, or use **Go > Go to page** (Ctrl+G).
- **Zoom**: presets from 25% to 1600%, zoom in/out, Ctrl+wheel around the pointer, 100% actual size (96 dpi), fit width and fit page. Fit modes follow window resizes.
- **Rotation**: whole-document rotation in 90 degree steps.
- **Search**: a find box sits on the toolbar; typing into it starts the search and opens the **Search results** panel on the right (Ctrl+F just focuses the box). Results are gathered a few milliseconds per frame starting at the page being read; every hit is highlighted on the page, the current one in the accent colour, and Enter / Shift+Enter / F3 / Shift+F3 step through them. The panel lists each match in page order with its sentence context and position on the page; click one to jump to it. Match case is optional.
- **Navigation aids**: a **Pages** panel with live thumbnails, an **Outline** panel for the document's bookmarks, a **Recent** panel listing recently opened documents as a tree grouped by folder (click to open, right-click or the x to remove an entry, Show in folder, Clear list), and link annotations on the page: internal links jump to their page, web and mail links open in the default browser (other URI schemes are refused).
- **Properties**: page count, file size, PDF version, encryption, the current page size in points, millimetres and inches, and the document metadata.
- **Text selection**: drag across text to select it (the pointer becomes an I-beam over text; dragging elsewhere pans). Ctrl+C copies the selection, or the whole page's text layer when nothing is selected. Escape clears the selection.
- **Night reading**: **View > Invert page colours** (Ctrl+Shift+N) inverts the page pixels while the interface keeps its theme.
- **Appearance**: eight fwcom token themes (dark and light), 75% to 200% GUI scaling independent of monitor DPI, a dockable layout with **View > Reset layout**, and a modal About box. Theme, scale, sidebar, fit mode, night mode and recent files persist between runs.

### Keyboard

| Shortcut | Action |
| --- | --- |
| Ctrl+O / Ctrl+W | Open / close |
| Ctrl+F, F3, Shift+F3 | Focus the find box, next match, previous match |
| Ctrl+G | Go to page |
| Page Down / Page Up / Space / Shift+Space | Scroll a screen |
| Home / End | Top of the current page / end of the document |
| Arrow keys | Scroll; left and right step pages when there is nothing to scroll sideways |
| Ctrl+Plus / Ctrl+Minus / Ctrl+wheel | Zoom in, out, around the pointer |
| Ctrl+1 / Ctrl+2 / Ctrl+0 | Actual size / fit width / fit page |
| Ctrl+R / Ctrl+Shift+R | Rotate clockwise / counter-clockwise |
| Ctrl+B / Ctrl+I | Toggle sidebar / properties |
| Ctrl+C | Copy the selection, or the whole page's text |
| Ctrl+Shift+N | Invert page colours |
| Escape | Clear the selection, or close the find bar |
| F1 | Keyboard shortcut reference |

## Build and test

Requires Visual Studio 2022 or later with the C++ x64 tools, CMake 3.24+ and Ninja. Every build is out of source under `C:\buildfiles\davepdf`, selected through `CMakePresets.json`.

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1                      # release build + tests
powershell -ExecutionPolicy Bypass -File build.ps1 -Configuration debug
powershell -ExecutionPolicy Bypass -File build.ps1 -SkipTests
ctest --preset release
```

The script finds Visual Studio with `vswhere`, enters its x64 environment, then configures, builds and tests with Ninja. The first configuration downloads the pinned dependencies: SDL 3.2.28, Dear ImGui 1.92.9b (docking), ImGui Test Engine and the PDFium 158.0.8111 prebuilt from [pdfium-binaries](https://github.com/bblanchon/pdfium-binaries), verified by SHA-256. Later builds use the cached sources. Application code compiles with `/W4 /permissive- /utf-8` and the static CRT.

### Tests

`ctest` runs three programs, all from `C:\buildfiles` rather than the executable's folder so that asset and DLL lookup is proven independent of the working directory:

- `davepdf_app_tests`: the window-free logic in `src/app` (zoom presets, page layout, fit calculations, current-page tracking, scroll anchoring under zoom, rotation of normalised boxes, settings round trip, recent files, and the generated sample PDF's cross-reference table).
- `davepdf_pdf_tests`: the PDFium wrapper against the sample document (rejecting garbage, page sizes, rendering pixels at two rotations, metadata, outline, text extraction, case-sensitive and insensitive search with normalised boxes, character hit testing and text ranges for selection, link annotations, file loading).
- `davepdf_ui_tests --test`: the real SDL/ImGui application driven by ImGui Test Engine. It opens a window for a couple of seconds and covers the welcome screen, opening the sample, toolbar and keyboard navigation, zoom presets and fit modes, Ctrl+wheel zoom, rotation, the find bar with stepping and match case, the outline and thumbnail panels, clicking a link through the real canvas geometry, drag panning, mouse text selection with clipboard verification, night mode, GUI scale, themes, the properties panel, the modal About box, the go-to-page dialog, opening files from disk, the error modal and the recent-files menu. Set `DAVEPDF_TEST_SCREENSHOTS` to a folder to get BMP captures of key states. Results are written to `ui-test-results.xml` beside the test binary.

## Source layout

```
CMakeLists.txt
src/main.cpp          entry point
src/app/              state and logic with no ImGui, SDL or PDFium: view geometry, settings, the sample PDF, the Session struct
src/ui/               drawing: workspace (menu, toolbar, find bar, status, welcome), canvas (page stack), panels, dialogs, fwTheme
src/platform/         the SDL/ImGui loop and page texture cache (desktop.cpp), the PDFium wrapper (pdf_engine.cpp), Windows services (host_windows.cpp)
assets/               Open Sans and Material Icons from fwcom, the app icon, third-party licenses
tests/                console tests and the UI test
tools/                icon generation scripts
```

The drawing code reads the `Session` and never owns state; it calls back into `platform` for anything that needs the engine or the OS. Failures come back as a bool plus a message that the interface shows in a modal. Page images are rendered by PDFium into BGRA buffers on a dedicated render thread (`render_worker.cpp`), uploaded as SDL textures on the frame loop and cached by page, size and rotation under a memory budget. PDFium is not thread safe, so every engine call holds the document's mutex; frame-time lookups (hover, links, search scanning) try the lock and simply retry next frame while a render is in progress. Requests are prioritised by distance from the page being read, a half-size preview is rendered first when nothing cached can stand in, and after a zoom or rotation the exact-size render waits until the size has held still for 120 ms so gestures stretch the cached page instead of rendering every frame. The status bar shows the last render time and the queue depth.

## Limits and next work

PDFium is used as a prebuilt DLL because building it from source needs Chromium's depot_tools; it is the only file beside the executable and the assets. Selection is limited to one page at a time. Annotations, form filling, printing and a two-page spread are not implemented yet. Very large zoom levels are capped at 8192 pixel textures per page. Only Windows x64 is built; the code has no other Windows-only dependency than the file dialog, icon and settings folder in `host_windows.cpp`.

Third-party licenses: `assets/licenses` (SDL, Dear ImGui, ImGui Test Engine, Open Sans, Material Icons, PDFium and its bundled libraries).
