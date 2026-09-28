# Soldat 3D

A 3D adaptation of [Soldat](https://github.com/Soldat/soldat). Original weapons and movement, 99 maps, bots and 32-player multiplayer.

Install a C compiler, CMake 3.25+ and Zstandard. On macOS, install Xcode command-line tools and run `brew install cmake zstd`. On Debian/Ubuntu:

```sh
sudo apt install build-essential cmake pkg-config libzstd-dev libgl1-mesa-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxext-dev libwayland-dev libxkbcommon-dev
```

Run `./client3d/play` on macOS or Linux. The first build downloads raylib. Keep the checkout for its assets. Multiplayer requires matching 3D clients.

WASD moves, mouse aims, left click fires, right click jets, Space jumps, E throws grenades, Q swaps weapons, R reloads. Enter deploys, Escape opens the menu.

Host with Docker, then join from a native client:

```sh
docker build -t soldat-3d .
docker run -d --name soldat-3d --restart unless-stopped -p 23073:23073/udp soldat-3d --map ctf_Ash --mode ctf --bots 6
./client3d/play --join SERVER_HOSTNAME
```

Allow incoming UDP 23073. For local hosting without Docker, use `./client3d/play --host`.

Run the simulation tests without graphics:

```sh
cmake -S client3d -B build/tests -DSOLDAT3D_RENDERER=OFF
cmake --build build/tests --parallel
ctest --test-dir build/tests --output-on-failure
```

Code: [MIT](LICENSE.md). Assets: [credits and licenses](client3d/assets/ATTRIBUTION.txt).
