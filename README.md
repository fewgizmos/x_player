## `X-Player` Engine Overview

#### X-Player is an experemental open-source C++20 multithreaded game engine built on Vulkan 1.3 and SDL3 with asynchronous glTF loading, bindless textures

![Screenshot-Demo-01](/Docs/X-Player/Assets/Screenshot-Demo-01.png)

| Development Stage                                |
| :---                                             |
| Models, Textures & glTF Materials async loading; |
| Scene;                                           |
| Demo ImGui menu & scene;                         |
| Free-flight playing camera;                      |
| Scene light points;                              |

## Links

Full engine specification (GitHub): [Docs/X-Player/Specification.md](/Docs/X-Player/Specification.md)

Demo screenshots gallery (GitHub): [Docs/X-Player/ScreenshotsGallery.md](/Docs/X-Player/ScreenshotsGallery.md)

## How to build demo

### Linux [in dev mode]:

#### [0.] To begin working with the engine, you need to install all development dependencies:
```
./Scripts/XDevEnvironmentInstaller.sh
```

#### 1. Activate the Linux development profile in the current shell
```
source ./ProjectProfiles/Linux/Dev
```

#### 2. Building:
```
./Scripts/XDevLauncher.sh -r
```

Add `--full-vk-logging` to `--run` to print Vulkan API calls to stdout. Omit `-r` (`--run`) to build without launching. The Dev profile produces `Build/Dev/LINUX/Release/dev-x-game-demo`.

## Project

| License            |
| :---               |
| Apache License 2.0 |
