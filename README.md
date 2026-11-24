## `X-Player` Engine::

### - is a experemental open-source C++20 multithreaded game engine built on Vulkan 1.3 and SDL3 with asynchronous glTF loading, bindless textures

The current code includes a working model rendering demo.

| Development Stage                                        |
| :---                                                     |
| Models, Textures & glTF Materials async loading;         |
| Scene;                                                   |
| Demo ImGui menu & scene;                                 |
| Free-flight playing camera;                              |

Full engine specification is available here: [Docs/X-Player/Specification.md](/Docs/X-Player/Specification.md)

## Demo::

![Screenshot-Demo-01](/Docs/X-Player/Assets/Screenshot-Demo-01.png)

Demo screenshots gallery - [Docs/X-Player/ScreenshotsGallery.md](/Docs/X-Player/ScreenshotsGallery.md)

## How to build::

### Linux [in dev mode]:

#### [0.] To begin working with the engine, you need to install all development dependencies:
```
./Scripts/Dev/XEnvPreparator.sh
```

#### 1. Activate the Linux development profile in the current shell
```
source ./ProjectProfiles/Linux/Dev
```

#### 2. Building:
```
./Scripts/Building/XBuilder.sh -r
```

Omit `-r` (`--run`) to build without launching. The Dev profile produces `Build/Dev/LINUX/Release/dev-x-game-demo`.

## Source rules::

| License |
| :--- |
| Apache License 2.0 |
