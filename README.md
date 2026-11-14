<<<<<<< HEAD
# Demand Paging with Swap and Page Replacement

**PageFlow** is an educational virtual-memory simulator for an Operating Systems project. A C++17 engine processes page references and applies FIFO, LRU, or Optimal replacement. A browser dashboard visualizes the engine's results, including page faults, page-table changes, and movement between RAM and simulated swap.

> This program does not inspect, modify, or use the Windows memory manager or Windows pagefile. Swap is simulated in this project's `swap/swap.txt` file only.

## Problem statement

Physical memory is limited. When a process references a page that is not resident, the operating system must handle a page fault, load the page, and—if all frames are occupied—choose a page to evict. This project makes the decisions and their performance consequences visible.

## Objectives

- Simulate demand paging and page-fault handling.
- Maintain a page table alongside RAM frames and swap slots.
- Compare FIFO, LRU, and Optimal replacement on the same reference string.
- Record page-in/page-out state in a project-owned text file.
- Visualize requests step by step and calculate performance metrics dynamically.

## Technologies

- C++17 for the memory manager, page-replacement policies, local HTTP API, and swap-file handling.
- HTML, CSS, and vanilla JavaScript for the local dashboard and animation.
- MinGW-w64 (or another C++17 compiler) and CMake for Windows builds.
- Winsock for the localhost-only HTTP server on Windows.

The project has no third-party runtime or frontend dependency. The dashboard and API are served by the C++ program on `127.0.0.1`.

## Project structure

```text
Demand-Paging-Simulator/
|-- src/
|   |-- main.cpp                 # Local HTTP server and API
|   |-- MemoryManager.h/.cpp     # Frames, page table, faults, simulation trace
|   |-- PageReplacement.h/.cpp   # FIFO, LRU, and Optimal victim selection
|   `-- SwapManager.h/.cpp       # Bounded simulated swap and file persistence
|-- frontend/
|   |-- index.html               # Dashboard markup
|   |-- style.css                # Responsive theme and animations
|   `-- script.js                # API client, playback, metrics, and chart
|-- swap/
|   `-- swap.txt                 # Simulated swap contents (updated at runtime)
|-- tests/
|   |-- test_memory_manager.cpp  # C++ engine regression tests
|   `-- test_cases.txt           # Test scenarios and expected counts
|-- CMakeLists.txt
`-- README.md
```

## Operating concepts

### Demand paging and page faults

Pages begin in the program's backing store. A page is loaded into a free RAM frame only when referenced. A reference to a page already in a frame is a **hit**; a reference not in RAM is a **page fault**. If memory is full, the configured policy selects a resident victim.

### Page table

The page table tracks every page in the input sequence and whether it is present in RAM, its frame (if present), and whether it is stored in simulated swap. Each execution step returns a RAM, swap, and page-table snapshot so the UI displays a coherent state.

### Simulated swap

When a victim is evicted, `SwapManager` assigns it a bounded slot and rewrites `swap/swap.txt`. Referencing a swapped page restores it to RAM. If swap is full, a page-in can reuse the incoming page's freed slot; if no slot can be freed or allocated, the engine reports an out-of-swap fault without changing the resident frame or page table. Reset clears simulated swap. Nothing is written to the Windows pagefile.

The API processes a complete reference sequence for each request and returns its per-step snapshots. A normal run records the final swap contents in the project file; playback is the browser presenting those engine-generated snapshots. Comparison-only requests use in-memory swap slots so they do not overwrite the active run's swap file.

### Replacement policies

- **FIFO (First In, First Out):** evicts the resident page loaded earliest.
- **LRU (Least Recently Used):** evicts the resident page whose most recent access is oldest.
- **Optimal:** evicts the resident page whose next use is farthest in the remaining sequence, or that is never used again. It is an offline benchmark because a real operating system cannot know future references.

## Build and run on Windows (VS Code + MinGW-w64)

Open the project root folder in VS Code. In a PowerShell terminal, confirm the tools are available:

```powershell
g++ --version
cmake --version
```

Configure, build, and run tests:

```powershell
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
ctest --test-dir build --output-on-failure
```

Run the server from the project root so it can find `frontend/` and `swap/`:

```powershell
.\build\bin\DemandPagingSimulator.exe
```

Then open **http://127.0.0.1:8080** in a browser. Keep the terminal open while using the app; press **Ctrl+C** to stop the server. If port 8080 is already in use, start it on a different port, for example `.\build\bin\DemandPagingSimulator.exe --port 8081`, then visit `http://127.0.0.1:8081`.

The server binds only to localhost. The browser does not need an extension, and the frontend does not need a separate development server.

### Build without CMake

From the project root:

```powershell
g++ -std=c++17 -Wall -Wextra -Wpedantic -Isrc src\main.cpp src\MemoryManager.cpp src\PageReplacement.cpp src\SwapManager.cpp -lws2_32 -o DemandPagingSimulator.exe
.\DemandPagingSimulator.exe
```

Compile and run the engine tests independently:

```powershell
g++ -std=c++17 -Wall -Wextra -Wpedantic -Isrc tests\test_memory_manager.cpp src\MemoryManager.cpp src\PageReplacement.cpp src\SwapManager.cpp -o DemandPagingTests.exe
.\DemandPagingTests.exe
```

## Dashboard controls and metrics

- Configure 1–16 RAM frames, 1–32 swap slots, an algorithm, and up to 100 page references (page IDs 0–99).
- **Start** requests a trace from the C++ API and begins playback. Pause/resume, single-step, and speed controls operate on that trace.
- Inspect RAM, swap, the page table, the action explanation, and the execution history.
- View request, hit, fault, fault-rate, hit-rate, swap-in, and swap-out counters as steps run.
- **Compare all policies** runs the same configuration through FIFO, LRU, and Optimal and draws a dynamically scaled fault-count chart.
- **Reset** clears the dashboard and the project-owned simulated swap file.

## Local API

- `GET /api/health` — reports that the C++ engine is running.
- `GET /api/simulate?frames=3&swap=8&algorithm=FIFO&references=1%2C2%2C3` — validates the inputs, runs the engine, persists simulated swap, and returns statistics and per-step snapshots as JSON. `algorithm` accepts `FIFO`, `LRU`, or `OPT`. Add `persist=0` for a comparison-only run that does not change `swap/swap.txt`.
- `GET /api/reset` — clears the simulated swap file.

The server accepts requests only on `127.0.0.1`; input limits are validated by the C++ API as well as by the UI.

## Tests and expected results

The test suite checks the supplied assignment input, hits and faults, page-table/frame/swap consistency, swap-in/out, a full swap area, empty and repeated references, and invalid capacities and page IDs. Run it with `ctest --test-dir build --output-on-failure`.

For **3 frames** and reference string **`1 2 3 4 1 2 5 1 2 3 4 5`**:

| Policy | Requests | Hits | Faults | Hit rate | Fault rate |
|---|---:|---:|---:|---:|---:|
| FIFO | 12 | 3 | 9 | 25.00% | 75.00% |
| LRU | 12 | 2 | 10 | 16.67% | 83.33% |
| Optimal | 12 | 5 | 7 | 41.67% | 58.33% |

The rates and comparison chart are calculated from simulation results, not hardcoded into the program. Additional edge cases are listed in `tests/test_cases.txt`.
=======
# OS-project
my IIITA OS on Demand Paging with Swap and Page Replacement work is here
>>>>>>> f0ef970515e7e5cdfce75be67966ce11a40ed8ec
