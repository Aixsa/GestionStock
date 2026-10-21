# Construire sur Windows

L'interface est écrite avec GTK 4 et CMake. Pour obtenir l'exécutable Windows localement, installez MSYS2 UCRT64 puis les paquets `mingw-w64-ucrt-x86_64-gcc`, `mingw-w64-ucrt-x86_64-cmake`, `mingw-w64-ucrt-x86_64-ninja`, `mingw-w64-ucrt-x86_64-pkgconf`, `mingw-w64-ucrt-x86_64-gtk4` et `mingw-w64-ucrt-x86_64-sqlite3`.

Depuis le terminal **MSYS2 UCRT64**, à la racine du dépôt :

```sh
cmake -S . -B dev/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build dev/build --parallel
```

L'exécutable est `dev/build/gestion-stock.exe`. L'action GitHub `Construire Gestion des stocks` produit aussi un paquet Windows qui joint les DLL GTK et les ressources de thème nécessaires, ainsi qu'un paquet Linux compilé séparément.
