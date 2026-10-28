# Android template

What `File › Export Game › Android` copies: a Gradle project with SDL's activity
(`org.libsdl.app`, vendored from SDL 3.4.8, zlib license), the Artisan player built
from the engine's sources as `libmain.so`, and the game in `app/src/main/assets/game`.

Build the APK with the Android SDK and NDK 28 installed:

```bash
./gradlew assembleRelease
```

The release build is signed with the debug key; sign with your own to publish.
Vulkan is required (Android 7.0, API 24, and newer).
