# Fichiers du projet moteur

Explication des fichiers du dossier `moteur/` : à quoi ils servent, ce qu'ils contiennent et quand les modifier.

Retour à la [documentation du jalon 1](JALON_1_FONDATIONS.md#squelette-réalisé-et-choix-pris) ou à la [roadmap](ROADMAP.md).

## Sommaire

1. [Arborescence](#1-arborescence)
2. [Comment un build fonctionne](#2-comment-un-build-fonctionne)
3. [Fichiers de build](#3-fichiers-de-build)
4. [Code source](#4-code-source)
5. [Fichiers de dépôt et de documentation](#5-fichiers-de-dépôt-et-de-documentation)
6. [Dossiers générés (à ne pas modifier)](#6-dossiers-générés-à-ne-pas-modifier)
7. [Fichiers à venir](#7-fichiers-à-venir)

---

## 1. Arborescence

```
moteur/
  CMakeLists.txt                      racine du build
  CMakePresets.json                   configurations Windows / macOS
  vcpkg.json                          dépendances
  cmake/
    Warnings.cmake                    règle des avertissements du compilateur
    Shaders.cmake                     chaîne de compilation des shaders
    Assets.cmake                      copie des assets à côté de l'exécutable
    Atlas.cmake                       empaquetage des atlas au build
  src/
    moteur/                           bibliothèque du moteur
      CMakeLists.txt
      include/moteur/version.hpp      en-tête public : version de SDL
      include/moteur/application.hpp  en-tête public : Application et Game
      include/moteur/renderer.hpp     en-tête public : Renderer (GPU)
      include/moteur/sprite_renderer.hpp  en-tête public : dessin de sprites
      include/moteur/sprite_batcher.hpp   en-tête public : tri et lots (sans GPU)
      include/moteur/camera.hpp       en-tête public : caméra 2D (sans GPU)
      include/moteur/iso.hpp          en-tête public : projection isométrique (sans GPU)
      include/moteur/tilemap.hpp      en-tête public : carte de tuiles et types de tuiles (sans GPU)
      include/moteur/animation.hpp    en-tête public : clips et lecteur d'animation (sans GPU)
      include/moteur/atlas_builder.hpp    en-tête public : empaquetage d'atlas (sans GPU)
      include/moteur/sprite_region.hpp    en-tête public : région d'atlas et placement
      include/moteur/texture_atlas.hpp    en-tête public : lecture d'un atlas
      include/moteur/utf8.hpp         en-tête public : décodage UTF-8 (sans GPU)
      include/moteur/text_layout.hpp  en-tête public : mise en page du texte (sans GPU)
      include/moteur/font.hpp         en-tête public : police et dessin de texte
      include/moteur/gpu_resource.hpp en-tête public : ressources GPU (RAII)
      include/moteur/fixed_timestep.hpp   en-tête public : pas de temps fixe
      include/moteur/frame_stats.hpp  en-tête public : statistiques de frame
      include/moteur/image.hpp        en-tête public : chargement d'image
      include/moteur/paths.hpp        en-tête public : chemins des ressources
      include/moteur/screenshot.hpp   en-tête public : écriture PNG d'une capture GPU
      include/moteur/debug_ui.hpp     en-tête public : interface de debug (Dear ImGui)
      version.cpp                     implémentation
      application.cpp                 fenêtre et boucle de jeu
      renderer.cpp                    périphérique GPU, frame, ressources
      screenshot.cpp                  conversion de pixels bruts en PNG (capture)
      debug_ui.cpp                    ImGui : contexte, backends SDL3 et SDL_GPU
      sprite_renderer.cpp             pipeline de sprites, envoi et draw calls
      sprite_batcher.cpp              tri, sommets et découpe en lots
      camera.cpp                      matrices et conversions de la caméra
      iso.cpp                         conversions grille / monde, plage de tuiles
      tilemap.cpp                     calques, découpe de plage, cases praticables
      animation.cpp                   lecture des clips, événements, fichier JSON
      atlas_builder.cpp               rognage, marge, empaquetage, pages
      sprite_region.cpp               placement d'un sprite d'atlas
      texture_atlas.cpp               chargement du JSON et des pages
      utf8.cpp                        décodage UTF-8
      text_layout.cpp                 retour à la ligne, alignement
      font.cpp                        chargement de police, mesure, dessin
      fixed_timestep.cpp              calcul des pas de logique
      frame_stats.cpp                 moyenne, maximum, percentiles
      image.cpp                       décodage des PNG (stb_image)
      paths.cpp                       dossier de l'exécutable et des assets
  apps/
    bac_a_sable/                      exécutable de test
      CMakeLists.txt
      main.cpp
  tools/
    atlas_packer/                     outil d'empaquetage d'atlas
      CMakeLists.txt
      main.cpp
  art/                                sources d'art (les PNG que l'on dessine)
    world/                            le sol isométrique + pivots.json
    test/                             19 images de test (orbes, barres, marche...)
  tests/
    CMakeLists.txt                    cible des tests unitaires
    main.cpp                          point d'entrée doctest
    test_fixed_timestep.cpp           tests du pas de temps fixe
    test_frame_stats.cpp              tests des statistiques
    test_sprite_batcher.cpp           tests du tri et des lots
    test_camera.cpp                   tests de la caméra
    test_iso.cpp                      tests de la projection isométrique
    test_tilemap.cpp                  tests de la carte de tuiles
    test_animation.cpp                tests des animations
    test_atlas_builder.cpp            tests de l'empaquetage d'atlas
    test_sprite_region.cpp            tests du placement des sprites d'atlas
    test_image.cpp                    tests de l'alpha pré-multiplié
    test_utf8.cpp                     tests du décodage UTF-8
    test_text_layout.cpp              tests de la mise en page du texte
  shaders/
    sprite.vert.hlsl                  sources HLSL
    sprite.frag.hlsl
    generated/msl/                    MSL pré-généré (versionné, utilisé sur macOS)
  assets/
    sprite.png                        image de test (32x32, avec transparence)
    fonts/Inter-Regular.ttf           police (SIL Open Font License)
    fonts/Inter-OFL.txt               texte de la licence
  .gitignore
  .gitattributes
  README.md
```

Trois grandes familles :

- **Build** : les `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`, les modules du dossier `cmake/`. Ils décrivent comment construire le projet.
- **Code** : le moteur (`src/moteur/`), le bac à sable (`apps/`) et les tests (`tests/`). Ce que le projet fait.
- **Dépôt** : `.gitignore`, `.gitattributes`, `README.md`. Ils rendent le projet partageable et reproductible.

---

## 2. Comment un build fonctionne

```
CMakePresets.json  ──►  choisit générateur, dossier de build, triplet vcpkg
        │
        ▼
CMakeLists.txt (racine)
        │   ├─ trouve la toolchain vcpkg
        │   ├─ règle C++20 et les avertissements
        │   └─ find_package(SDL3) ◄── vcpkg installe SDL3 d'après vcpkg.json
        │
        ├─► src/moteur/CMakeLists.txt     construit la bibliothèque "moteur"
        │
        └─► apps/bac_a_sable/CMakeLists.txt   construit l'exécutable, lié à "moteur"
```

Deux étapes distinctes :

1. **Configuration** (`cmake --preset ...`) : CMake lit les fichiers, vcpkg télécharge et compile SDL3, puis CMake génère les fichiers Ninja.
2. **Compilation** (`cmake --build ...`) : Ninja compile le code et produit l'exécutable.

Le premier build est long à cause de la compilation de SDL3 par vcpkg. Les suivants sont rapides, car vcpkg met le résultat en cache.

---

## 3. Fichiers de build

### `CMakeLists.txt` (racine)

**Rôle** : point d'entrée du build. Il fixe les règles communes à tout le projet et déclare les sous-projets.

**Contenu, dans l'ordre**

| Élément | Explication |
|---|---|
| `cmake_minimum_required(VERSION 3.25)` | Version minimale de CMake. Le 3.25 est requis par la version 6 des presets. |
| Bloc `CMAKE_TOOLCHAIN_FILE` | Si aucune toolchain n'est fournie et que `VCPKG_ROOT` existe, utilise le fichier de vcpkg. Ce bloc est placé **avant** `project()`, sinon il est ignoré. Le vcpkg intégré à CLion injecte sa propre toolchain, qui est prioritaire. |
| `project(moteur LANGUAGES CXX)` | Nom du projet, C++ uniquement. |
| `CMAKE_CXX_STANDARD 20` + `..._REQUIRED ON` | Impose C++20 et refuse de continuer si le compilateur ne le supporte pas. |
| `CMAKE_CXX_EXTENSIONS OFF` | Reste sur le C++ standard (`-std=c++20`) au lieu des extensions propres au compilateur (`-std=gnu++20`). Le code sera plus portable entre MSVC et Clang. |
| `CMAKE_EXPORT_COMPILE_COMMANDS ON` | Génère `compile_commands.json`, utilisé par CLion et les outils d'analyse. |
| `list(APPEND CMAKE_MODULE_PATH ...)` + `include(Warnings)` | Ajoute le dossier `cmake/` au chemin de recherche des modules et charge `Warnings.cmake`. |
| `find_package(SDL3 CONFIG REQUIRED)` | Cherche SDL3 (installé par vcpkg) et arrête la configuration s'il est absent. |
| `include(Shaders)` | Charge `cmake/Shaders.cmake`, qui fournit `moteur_add_shaders()`. Doit venir **après** `find_package`, car il a besoin de l'emplacement des paquets vcpkg. |
| `include(Assets)` | Charge `cmake/Assets.cmake`, qui fournit `moteur_add_assets()`. |
| `include(Atlas)` | Charge `cmake/Atlas.cmake`, qui fournit `moteur_add_atlas()`. |
| `add_subdirectory(...)` | Descend dans `src/moteur`, `tools/atlas_packer` puis `apps/bac_a_sable`. L'ordre compte : la bibliothèque doit exister avant l'outil et l'exécutable qui la lient, et l'outil avant l'exécutable qui l'utilise au build. |
| `option(MOTEUR_BUILD_TESTS ...)` | Active la compilation des tests unitaires (par défaut oui) : `enable_testing()` puis `add_subdirectory(tests)`. Mettre l'option à `OFF` pour un build sans tests. |

**Quand le modifier** : ajout d'une dépendance globale, changement de standard C++, ajout d'un nouveau sous-projet (tests, outils, ARPG).

### `CMakePresets.json`

**Rôle** : décrire des configurations prêtes à l'emploi, pour ne pas retaper les options à la main et obtenir les mêmes résultats sur les deux machines. CLion les affiche comme des profils.

**Contenu**

- `configurePresets` : 5 entrées.
  - `base` (masqué) : générateur **Ninja** et dossier de build `build/<nom du preset>/`. Les autres presets en héritent.
  - `windows-debug` / `windows-release` : actifs uniquement sur Windows (`condition` sur `hostSystemName`). Fixent le compilateur `cl` (MSVC) et le triplet `x64-windows-static-md`.
  - `macos-debug` / `macos-release` : actifs uniquement sur macOS. Fixent l'architecture `arm64`, la cible minimale `13.0` et le triplet `arm64-osx`.
- `buildPresets` : un preset de compilation par preset de configuration, pour `cmake --build --preset ...`.

**À savoir**

- Le *triplet* vcpkg décrit la combinaison plateforme + architecture + type de lien. `x64-windows-static-md` signifie : bibliothèques **statiques**, runtime C de Microsoft **dynamique** (`/MD`). C'est l'option la plus courante avec MSVC.
- La `condition` permet à chaque OS de ne voir que ses propres presets.
- Aucun chemin absolu n'apparaît : le fichier fonctionne tel quel sur toute machine.
- En ligne de commande sur Windows, `cl` doit être accessible : utiliser un « Developer PowerShell for VS 2022 ». Dans CLion, la toolchain Visual Studio prépare l'environnement.

**Quand le modifier** : ajout d'une configuration (par exemple `windows-relwithdebinfo`), changement de cible macOS, changement de triplet.

### `vcpkg.json`

**Rôle** : le *manifeste* des dépendances. vcpkg le lit et installe ce qui y figure dans un dossier propre au projet.

**Contenu**

| Champ | Explication |
|---|---|
| `name` | Nom du paquet, en minuscules. |
| `version-string` | Version du projet. Libre, sans effet sur les dépendances. |
| `builtin-baseline` | Identifiant d'un commit du dépôt vcpkg. Il **épingle les versions** de tous les ports : deux machines avec la même baseline compilent la même version de SDL3. Valeur actuelle : `5f96cd15fd745122cf27e0524606d6c1efc5fd07`. |
| `dependencies` | Liste des bibliothèques : `sdl3`, `glm` (mathématiques : matrices et vecteurs), `stb` (décodage des PNG), `doctest` (tests unitaires), `nlohmann-json` (description des atlas et des animations), `imgui` avec les fonctionnalités `sdl3-binding` et `sdlgpu3-binding` (interface de debug ; ses shaders sont précompilés pour chaque backend, donc rien à ajouter à la chaîne des shaders), et `sdl3-shadercross` **uniquement sur Windows** (`"platform": "windows"`). Ce dernier apporte l'outil `shadercross`, le compilateur DirectX (DXC) et SPIRV-Cross. Il impose aussi la fonctionnalité `vulkan` de SDL3, sans effet sur le backend choisi. |

**Quand le modifier** : ajouter une bibliothèque (GLM, EnTT, Dear ImGui, nlohmann-json, etc.) ou mettre à jour la baseline.

**Piège** : sans baseline, chaque machine peut résoudre une version différente. Ne jamais la retirer.

### `cmake/Warnings.cmake`

**Rôle** : centraliser le niveau d'avertissements du compilateur dans une fonction réutilisable.

**Contenu** : la fonction `moteur_enable_warnings(<cible>)`.

- MSVC : `/W4` (niveau élevé) et `/permissive-` (refuse le code non conforme au standard que MSVC tolère par défaut).
- Clang / GCC : `-Wall -Wextra -Wpedantic`.
- Le mot-clé `PRIVATE` limite ces options à la cible concernée : elles ne se propagent pas aux utilisateurs de la bibliothèque.

**Pourquoi une fonction** : appliquer ces règles **uniquement à notre code**, pas au code tiers.

**Pas de « warnings as errors »** : les avertissements ne bloquent pas le build. Ce choix évite des échecs à chaque mise à jour du compilateur, mais oblige à rester attentif aux avertissements.

**Quand le modifier** : durcir la politique (`/WX`, `-Werror`), ajouter des options d'analyse ou de sanitizers.

### `cmake/Shaders.cmake`

**Rôle** : compiler les shaders HLSL au build et les déposer à côté de l'exécutable.

**Contenu**

| Élément | Explication |
|---|---|
| Détection de `shadercross` (Windows) | Cherche l'outil dans `vcpkg_installed/<triplet>/tools/sdl3-shadercross`. Arrête la configuration s'il est absent. |
| Dossier `shader_tools/` | Au configure, copie `shadercross.exe`, `dxcompiler.dll` et `dxil.dll` dans `build/shader_tools/`, pour que l'outil trouve les bibliothèques dont il dépend. |
| `moteur_add_shaders(cible SOURCES ...)` | Pour chaque fichier `<nom>.vert\|frag\|comp.hlsl` de `shaders/`, déduit l'étage du nom de fichier et ajoute une commande de compilation. La cible dépend de ces sorties, donc un shader modifié est recompilé et une erreur fait échouer le build. |
| Windows | HLSL vers **DXIL**, écrit dans `<dossier de la cible>/shaders/<nom>.dxil`. |
| Cible `export_msl_shaders` | Manuelle, hors build normal. HLSL vers **MSL**, écrit dans `shaders/generated/msl/`. À lancer après toute modification d'un shader, puis à versionner. |
| macOS | Ne compile rien : copie `shaders/generated/msl/<nom>.msl` vers `<dossier de la cible>/shaders/`. Arrête la configuration si le fichier MSL est absent. |

**Pourquoi cette différence entre OS** : le port vcpkg `sdl3-shadercross` dépend de `directx-dxc`, qui n'existe pas sur macOS. On génère donc le MSL sur Windows, où tout fonctionne.

**Limite** : le dossier de sortie est celui de la cible, ce qui suppose un générateur mono-configuration (Ninja), comme dans nos presets.

**Quand le modifier** : ajout d'un nouveau format (par exemple SPIR-V pour Vulkan), changement de la stratégie macOS.

### `cmake/Assets.cmake`

**Rôle** : copier le dossier `assets/` à côté de l'exécutable.

**Contenu** : la fonction `moteur_add_assets(cible)`. Elle crée une cible qui copie `assets/` vers `<dossier de la cible>/assets/` et dont dépend l'exécutable. La copie a lieu à **chaque build**, ce qui garde les assets à jour même quand seul un fichier image a changé (une copie attachée à l'édition de liens ne s'exécuterait pas dans ce cas).

**Pourquoi** : le programme cherche ses assets relativement à l'exécutable (`asset_path()`), pas au dossier de travail. Il fonctionne donc quel que soit l'endroit d'où on le lance.

### `src/moteur/CMakeLists.txt`

**Rôle** : définit la **bibliothèque** `moteur`.

**Contenu**

| Ligne | Explication |
|---|---|
| `find_package(glm ...)` et `find_path(STB_INCLUDE_DIRS ...)` | Cherchent GLM (paquet CMake) et stb (simple dossier d'en-têtes, sans configuration CMake). |
| `add_library(moteur STATIC animation.cpp application.cpp atlas_builder.cpp camera.cpp debug_ui.cpp fixed_timestep.cpp font.cpp frame_stats.cpp image.cpp iso.cpp paths.cpp renderer.cpp screenshot.cpp sprite_batcher.cpp sprite_region.cpp sprite_renderer.cpp text_layout.cpp texture_atlas.cpp tilemap.cpp utf8.cpp version.cpp)` | Crée une bibliothèque statique à partir des fichiers listés. **Chaque nouveau `.cpp` doit être ajouté ici.** |
| `target_include_directories(moteur PUBLIC include)` | Le dossier `include/` est exposé à la bibliothèque **et** à ceux qui la lient. Le code peut donc écrire `#include "moteur/version.hpp"`. |
| `target_include_directories(moteur SYSTEM PRIVATE ...)` | Ajoute les en-têtes de stb. `SYSTEM` les traite comme du code tiers : leurs avertissements sont ignorés. `PRIVATE` : stb reste un détail interne. |
| `target_link_libraries(moteur PUBLIC SDL3::SDL3 glm::glm PRIVATE nlohmann_json::nlohmann_json)` | Lie SDL3 et GLM. `PUBLIC` transmet ces dépendances : le projet ARPG les verra en liant `moteur`. Le JSON n'est utilisé que dans `texture_atlas.cpp` et `animation.cpp`, donc `PRIVATE` : il ne se propage pas. |
| `moteur_enable_warnings(moteur)` | Applique les avertissements définis dans `Warnings.cmake`. |

**`PUBLIC` ou `PRIVATE` ?** `PUBLIC` : la dépendance apparaît dans nos en-têtes publics ou doit être visible par l'utilisateur. `PRIVATE` : détail interne, invisible de l'extérieur. Choisir `PRIVATE` par défaut dès qu'une dépendance n'apparaît pas dans un en-tête public : cela garde les interfaces propres.

**Quand le modifier** : chaque nouveau fichier source du moteur, chaque nouvelle dépendance.

### `apps/bac_a_sable/CMakeLists.txt`

**Rôle** : définit l'**exécutable** de test, séparé de la bibliothèque.

**Contenu** : `add_executable(bac_a_sable main.cpp)`, liaison `PRIVATE` avec `moteur`, avertissements, `moteur_add_shaders(...)` avec la liste des shaders de l'exécutable, `moteur_add_assets(...)` pour copier les assets, et `moteur_add_atlas(...)` (une fois par atlas) pour empaqueter les sprites. SDL3 arrive automatiquement par la liaison `PUBLIC` de la bibliothèque.

**Pourquoi séparé** : le moteur reste réutilisable par l'ARPG, sans emporter le bac à sable. Les futurs tests pourront aussi lier `moteur` sans passer par l'exécutable.

---

## 4. Code source

### `src/moteur/include/moteur/version.hpp`

**Rôle** : en-tête **public** de la bibliothèque. C'est ce que les autres projets incluent.

**Contenu** : `#pragma once` (évite l'inclusion multiple) et la déclaration `std::string moteur::sdl_version()`.

**Pourquoi le dossier `include/moteur/`** : l'imbrication oblige à écrire `#include "moteur/version.hpp"`. Le préfixe évite les collisions de noms avec d'autres bibliothèques et montre d'où vient chaque en-tête. C'est la convention habituelle.

### `src/moteur/version.cpp`

**Rôle** : implémentation de `sdl_version()`, qui interroge la version de SDL réellement liée à l'exécution (`SDL_GetVersion()`), puis la formate en `major.minor.micro`.

**Utilité réelle** : c'est un test de fumée. Si l'exécutable affiche `SDL 3.x.x`, la chaîne CMake + vcpkg + lien fonctionne. Ce fichier a vocation à évoluer ou disparaître ensuite.

**Convention** : les fonctions du moteur sont dans le namespace `moteur`.

### `src/moteur/include/moteur/application.hpp`

**Rôle** : interface publique de la fenêtre et de la boucle de jeu.

**Contenu**

| Élément | Explication |
|---|---|
| `ApplicationConfig` | Titre, taille de la fenêtre, fréquence de la logique (`fixed_hz`, 60 par défaut), plafond de rattrapage (`max_frame_time`, 0,25 s), `vsync` (activé par défaut), `gpu_debug` (activé hors Release) et `report_performance` (affiche un résumé des temps CPU à la fin). |
| `Game` | Interface que le programme implémente. `update(dt)` est appelé à fréquence fixe avec un `dt` constant. `render(renderer, alpha)` est appelé une fois par frame dessinable et sert à **enregistrer** ce qu'il faut dessiner (`renderer.sprites().draw(...)`) : rien n'est envoyé au GPU tant que la frame n'est pas terminée. `alpha` est l'avancement entre deux mises à jour, pour l'interpolation. `on_event()` reçoit les événements SDL bruts. |
| `Application` | Crée SDL, la fenêtre et le `Renderer` dans son constructeur, les libère dans l'ordre inverse dans son destructeur (RAII), et contient la boucle `run(Game&)`. `quit()` demande l'arrêt. Non copiable. |
| `to_pixels(point)` | Convertit une position en points de fenêtre (ce que SDL donne pour la souris) en pixels (ce que le rendu dessine). Identique sur un écran normal, différent d'un facteur 2 sur beaucoup de Mac. |

**À savoir** : c'est la première API du moteur. `Game` est ce que l'ARPG devra implémenter.

### `src/moteur/application.cpp`

**Rôle** : implémentation de `Application`.

**Contenu**

- **Constructeur** : `SDL_Init`, `SDL_CreateWindow` (redimensionnable, haute densité de pixels), puis création du `Renderer`. Lève `std::runtime_error` en cas d'échec et nettoie ce qui a déjà été créé. Écrit dans les logs la taille en points et en pixels.
- **Destructeur** : libère le `Renderer` **avant** de détruire la fenêtre, puis appelle `SDL_Quit`.
- **`run()`** : mesure le temps avec `SDL_GetPerformanceCounter`, confie le calcul des pas de logique à un `FixedTimestep`, traite les événements, exécute les `update()` de durée fixe, puis dessine la frame (`begin_frame`, `Game::render`, `end_frame`).
- **Mesures** : chaque frame dessinée est chronométrée en trois phases (événements et mises à jour, enregistrement par `Game::render`, exécution par `end_frame`). L'attente de l'écran, qui a lieu dans `begin_frame`, est **exclue** : c'est de l'attente, pas du travail. Les 60 premières frames sont ignorées du résumé (préchauffage). Une fois par seconde, le titre de la fenêtre affiche les FPS, les ticks par seconde, le temps CPU moyen, le nombre de sprites et de draw calls. Avec `report_performance`, un résumé (moyenne, percentile 99, maximum, moyenne par phase) est écrit dans les logs à la fermeture.
- **Cadence** : c'est le VSync du swapchain qui rythme la boucle, car `begin_frame()` attend l'image suivante. Si la fenêtre est minimisée, rien n'est dessiné et la boucle attend 10 ms pour ne pas tourner à vide.

### `src/moteur/include/moteur/renderer.hpp`

**Rôle** : interface publique du rendu GPU.

**Contenu**

| Élément | Explication |
|---|---|
| `RendererConfig` | `debug` (couche de validation du GPU) et `vsync`. |
| `ShaderInfo` | Ce qu'un shader déclare : son étage (vertex ou fragment) et le nombre de samplers, uniformes et tampons de stockage qu'il utilise. SDL_GPU s'en sert pour valider les liaisons. |
| `Texture` | Une texture GPU (`GpuTexture`) et sa taille en pixels. |
| `RenderStats` | Compteurs de la dernière frame : sprites dessinés, draw calls et octets envoyés au GPU. Remis à zéro à chaque `begin_frame()`. |
| `load_shader()` | Charge `shaders/<nom>.<ext>` à côté de l'exécutable, avec l'extension et le point d'entrée du backend actif. Lève une exception si le fichier manque ou si le shader est rejeté. Renvoie un `GpuShader` qui se libère seul. Le shader est nommé `<nom>` pour les outils de capture. |
| `swapchain_format()` | Format des pixels de la fenêtre, nécessaire pour créer un pipeline graphique. |
| `create_buffer(usage, données, taille, nom)` | Crée un buffer GPU (sommets ou indices), y envoie les données et attend la fin. Renvoie un `GpuBuffer`. |
| `create_buffer(usage, taille, nom)` | Crée un buffer vide, sans rien envoyer ni attendre : assez léger pour un buffer qui grandit en cours de route. |
| `create_transfer_buffer()` | Crée une zone de transfert : la mémoire que le CPU écrit avant qu'un copy pass l'envoie au GPU. |
| `create_texture(image, nom)` | Crée une texture RGBA8 à partir d'une `Image`, sans mipmaps, en UNORM (pas de conversion sRGB). **L'alpha est pré-multiplié dans la couleur** avant l'envoi, quel que soit l'alpha de l'image. Renvoie une `Texture`. |
| `create_sampler(filtre, nom)` | Crée un échantillonneur avec le filtrage voulu (`NEAREST` pour le pixel art, `LINEAR` pour un rendu lissé) et renvoie un `GpuSampler`. Les coordonnées hors de [0, 1] sont ramenées au bord. |
| `Renderer` | Possède le périphérique GPU. Une frame se déroule en deux phases : `begin_frame()` acquiert l'image de la fenêtre, le jeu **enregistre** ses sprites, puis `end_frame()` envoie les données (copy pass), dessine (render pass) et soumet. Non copiable. |
| `sprites()` | Le `SpriteRenderer` du renderer, utilisé pour enregistrer les sprites de la frame. |
| `stats()` | Les `RenderStats` de la frame qui vient de se terminer. **N'est valide qu'entre `end_frame()` et le `begin_frame()` suivant** (voir la partie 9) : lue trop tôt (par exemple dans `Game::render()`, avant `end_frame()`), elle est déjà remise à zéro. |
| `request_capture(chemin)` | Partie 9. Un indicateur, consommé une fois : au prochain `end_frame()`, l'image du swapchain est relue vers le CPU et écrite en PNG (`write_capture_png()`, voir `screenshot.hpp`), pour des tests automatiques par comparaison de pixels. Coûte une attente GPU complète ; jamais à utiliser à chaque frame. |
| `set_clear_color()` | Couleur d'effacement de l'écran. |
| `width()`, `height()` | Taille en pixels de l'image du swapchain de la frame courante. |

### `src/moteur/renderer.cpp`

**Rôle** : implémentation de `Renderer`, au-dessus de SDL_GPU.

**Contenu**

- **Formats de shaders** (`kShaderFormats`) : DXIL sur Windows (Direct3D 12), MSL et metallib sur macOS (Metal), SPIR-V sinon. C'est ce qui détermine le backend choisi.
- **Constructeur** : crée le périphérique (`SDL_CreateGPUDevice`), l'associe à la fenêtre, règle le mode de présentation (VSync par défaut), puis écrit dans les logs le backend, le nom du GPU, le mode de présentation et l'état du mode debug.
- **`begin_frame()`** : remet les statistiques à zéro, acquiert un command buffer et l'image du swapchain (c'est là que la boucle attend l'écran). Il n'ouvre **pas** de render pass. Si l'image est absente (fenêtre minimisée), il soumet le command buffer sans dessiner et renvoie `false`.
- **`end_frame()`** : phase 1, `SpriteRenderer::prepare()` (les copies vers le GPU, qui doivent précéder le render pass) ; phase 2, ouverture du render pass avec effacement, `SpriteRenderer::render()` pour les draw calls, fermeture ; phase 3 (partie 9, optionnelle), si `request_capture()` a été appelé : un copy pass relit l'image du swapchain vers un tampon de transfert **avant** la soumission (pendant que la texture est encore valide) ; puis soumission, qui présente l'image ; enfin, si une capture était demandée, une attente GPU et l'écriture du PNG.
- **`load_shader()`** : demande à SDL_GPU quels formats le backend accepte, essaie DXIL, MSL puis SPIR-V dans cet ordre, charge le premier fichier trouvé et crée le shader. Le point d'entrée est `main` en DXIL et SPIR-V, mais **`main0` en MSL** : SPIRV-Cross renomme la fonction, car `main` est réservé en Metal.
- **`create_buffer()`, `create_texture()`** : suivent le même schéma. Une zone de transfert visible du CPU est remplie, un command buffer distinct enregistre la copie vers le GPU dans un *copy pass* (les copies sont interdites dans un render pass), puis on attend la fin. Ces attentes sont acceptables au chargement, pas pendant une frame.
- **Constructeur (suite)** : crée le `SpriteRenderer`. Si cela échoue (shaders introuvables, par exemple), le constructeur libère lui-même le périphérique avant de relancer l'exception, car un destructeur ne s'exécute pas quand son constructeur lève une exception.
- **Destructeur** : attend que le GPU ait fini, libère le `SpriteRenderer` **avant** le périphérique (ses ressources en dépendent), libère la fenêtre, puis détruit le périphérique.

**À savoir** : le backend peut être forcé avec la variable d'environnement `SDL_GPU_DRIVER`, sans modifier le code. Depuis la partie 10, toutes les fonctions `create_*` et `load_shader()` acceptent un **nom optionnel** (`nullptr` par défaut) qui étiquette la ressource pour un outil de capture comme RenderDoc ou le débogueur Metal de Xcode ; il ne coûte rien à l'exécution.

### `src/moteur/include/moteur/screenshot.hpp` et `screenshot.cpp`

**Rôle** (partie 9) : convertir des pixels bruts relus du GPU en fichier PNG, pour la capture d'image de `Renderer::request_capture()`.

**Contenu**

- `write_capture_png(chemin, pixels, largeur, hauteur, format)` : convertit BGRA en RGB si `format` l'exige (le swapchain peut être `B8G8R8A8_UNORM` selon le backend), force l'alpha à 255 (celui du swapchain n'a pas de sens une fois présenté), puis écrit avec `stbi_write_png`. Fonction libre, séparée de `renderer.cpp`, pour ne pas mêler l'inclusion de `stb_image_write` (qui définit son implémentation ici) au reste du rendu.

### `src/moteur/include/moteur/gpu_resource.hpp`

**Rôle** : faire en sorte qu'une ressource GPU se libère toute seule, et pouvoir la nommer pour le débogage.

**Contenu**

- Le modèle `GpuResource<T, Release>` et les alias `GpuBuffer`, `GpuTexture`, `GpuSampler`, `GpuShader`, `GpuTransferBuffer`, `GpuGraphicsPipeline`. Chaque objet possède un pointeur SDL_GPU et libère la ressource dans son destructeur. Il est **déplaçable mais non copiable**, donc une ressource ne peut pas être libérée deux fois.
- **`NameProperty`** (partie 10) : crée les propriétés SDL nécessaires pour nommer une ressource à sa création (un buffer, une texture, un sampler, un shader ou un pipeline), puis les détruit à la fin de sa portée. Avec un nom nul, `id()` vaut 0, ce que toute fonction `SDL_CreateGPU*()` interprète comme « aucune propriété » : nommer une ressource reste entièrement facultatif. **Il faut garder l'objet en vie jusqu'à l'appel `SDL_CreateGPU*()` qui lit la propriété**, pas seulement jusqu'à la ligne qui remplit `info.props`.

**Règle de durée de vie** : la ressource mémorise le périphérique pour se libérer, donc **le `Renderer` doit vivre plus longtemps que toutes les ressources créées par lui**. En pratique : déclarer les ressources après l'`Application` dans la même portée, ou dans des objets que l'`Application` survit. Le contraire provoque un accès à un périphérique déjà détruit.

### `src/moteur/include/moteur/sprite_renderer.hpp` et `sprite_renderer.cpp`

**Rôle** : dessiner des rectangles texturés. C'est aujourd'hui le seul système de dessin du moteur.

**Contenu**

- `SpriteRenderer::draw(région, ancre, échelle, options)` **enregistre un sprite d'atlas** de façon que **son pivot tombe exactement sur l'ancre**. Les marges rognées sont restituées et un retournement met le sprite en miroir autour de son pivot. Le rectangle de texture vient de la région (`options.uv_rect` est ignoré).
- `SpriteRenderer::draw(texture, position, taille, options)` **enregistre** un sprite. `position` est le coin haut-gauche et `taille` l'étendue, en pixels (origine en haut à gauche de la fenêtre, Y vers le bas). Rien n'est envoyé au GPU à ce moment.
- `SpriteOptions` : rectangle de texture (`uv_rect`, toute la texture par défaut), teinte (`tint`, blanc par défaut), profondeur (`depth`, 0 par défaut, le plus grand est dessiné en dernier donc au-dessus) et retournements (`flip_x`, `flip_y`).
- `set_batching(false)` : chaque sprite reçoit son propre draw call. L'image est identique, seul le nombre de draw calls change. C'est un outil de **mesure et de comparaison**, pas un réglage de jeu.
- `set_view_projection(matrice)` : la transformation appliquée aux sprites de la frame, en général `camera.view_projection()`. Elle vaut pour toute la frame (le dernier appel l'emporte) et est oubliée à la fin de la frame. **Sans elle, les sprites sont placés directement en pixels d'écran** (origine en haut à gauche, Y vers le bas), comme avant.
- Le constructeur charge les shaders `sprite.vert` et `sprite.frag`, crée l'échantillonneur `NEAREST`, le buffer d'indices statique (16 384 quads) et le pipeline avec mélange en **alpha pré-multiplié** (`ONE`, `ONE_MINUS_SRC_ALPHA`) : les textures contiennent une couleur déjà multipliée par l'alpha. Chaque ressource est nommée (`sprite.sampler`, `sprite.indices`, `sprite pipeline`, `sprite.vertices` pour le buffer dynamique créé dans `ensure_capacity()`), pour les retrouver dans une capture RenderDoc ou Metal.
- `prepare()` (avant le render pass) : demande au batcher de trier et de construire les sommets, agrandit au besoin le buffer de sommets (il double, et l'agrandissement est écrit dans les logs), écrit les sommets dans la zone de transfert, puis les copie vers le GPU dans un *copy pass*. Les deux opérations utilisent `cycle = true` : si le GPU lit encore le buffer de la frame précédente, SDL en donne un autre au lieu d'attendre. Seule la partie utilisée est envoyée.
- `render()` (dans le render pass) : lie le pipeline et les buffers, envoie **une seule matrice vue-projection** pour la frame, puis fait **un draw call par lot**. La texture n'est liée que quand elle change. Un lot au-delà des premiers quads atteint ses sommets grâce au décalage de sommet de base du draw call.
- `Renderer` en est **ami** (`friend`) : seul lui appelle `prepare()`, `render()` et `clear()`, au bon moment de la frame.

**Coût mesuré** (Release, Windows) : environ 28 ns de CPU par sprite, dont un draw call ne représente qu'environ 15 à 17 ns quand chaque sprite a le sien.

### `src/moteur/include/moteur/sprite_batcher.hpp` et `sprite_batcher.cpp`

**Rôle** : préparer ce que le GPU va recevoir, **sans aucune dépendance au GPU** : c'est du C++ pur, donc entièrement testable.

**Contenu**

- `SpriteVertex` : un sommet tel qu'il est envoyé au GPU, 20 octets (position, coordonnées de texture, teinte sur 4 octets). Un `static_assert` garantit qu'il n'y a pas de remplissage.
- `pack_color(couleur)` : convertit une couleur de composantes dans [0, 1] en 4 octets, le rouge dans l'octet de poids faible.
- `SpriteDesc` : un sprite demandé par le jeu. La texture n'est qu'une **identité** (un pointeur opaque) : les sprites qui la partagent peuvent être dessinés ensemble.
- `SpriteRun` : un lot, c'est-à-dire des sprites consécutifs qui partagent une texture, donc un draw call.
- `SpriteBatcher` : `begin()` vide la frame, `add()` enregistre un sprite, `finish()` fait le travail :
  1. **tri par profondeur**, croissante, stable, mais seulement si les sprites n'ont pas été enregistrés dans l'ordre (un suivi de la profondeur maximale le détecte à coût nul) ;
  2. **construction de 4 sommets par sprite**, avec les retournements appliqués aux coordonnées de texture ;
  3. **découpe en lots** : un nouveau lot commence quand la texture change ou que la limite de 16 384 sprites est atteinte.

**Le tri** est un tri par base (radix, 4 passes de 8 bits) sur des clés entières dérivées de la profondeur flottante. Il déplace 8 octets par sprite au lieu des 56 de la structure, et sa mémoire de travail est conservée d'une frame à l'autre. Il a été comparé à `std::stable_sort` : environ 5 fois moins cher à 40 000 sprites en ordre mélangé.

**Pourquoi 16 384** : avec des indices sur 16 bits, un draw call adresse au plus 65 536 sommets, soit 16 384 quads.

### `src/moteur/include/moteur/fixed_timestep.hpp` et `fixed_timestep.cpp`

**Rôle** : transformer des durées de frame variables en un nombre entier de pas de logique fixes. C'était du code de la boucle, il en est extrait pour être testable sans fenêtre ni GPU.

**Contenu** : `FixedTimestep(pas, temps_max)`. `advance(durée)` ajoute le temps écoulé (plafonné à `temps_max`, les durées négatives comptent pour zéro) et renvoie combien de mises à jour exécuter. `alpha()` donne l'avancement entre deux mises à jour, dans [0, 1). Lève `std::invalid_argument` si un paramètre n'est pas positif.

### `src/moteur/include/moteur/frame_stats.hpp` et `frame_stats.cpp`

**Rôle** : résumer une série de mesures (temps CPU par frame, en millisecondes).

**Contenu** : `FrameStats(capacité)` garde les dernières valeurs dans un tampon circulaire (65 536 par défaut). `mean()`, `max()` et `percentile(p)` (rang le plus proche) les résument, et renvoient 0 s'il n'y a aucune valeur. Le maximum et le percentile 99 comptent autant que la moyenne : ce sont les pics qui font saccader une image, pas la moyenne.

### `src/moteur/include/moteur/camera.hpp` et `camera.cpp`

**Rôle** : décider quelle partie du monde on voit, et à quelle taille. Logique pure, sans GPU, entièrement testée.

**Contenu**

- `Rect` : un rectangle (`min`, `max`), par exemple la partie visible du monde.
- `window_to_pixels(point, taille en points, taille en pixels)` : la conversion de la souris des points vers les pixels. Elle ne divise jamais par zéro.
- `Camera2D` :

| Élément | Explication |
|---|---|
| `set_viewport(taille)` | Taille de la fenêtre en pixels. À régler à chaque frame d'après le renderer, pour que le redimensionnement fonctionne. |
| `set_position()`, `set_zoom()` | `position` est le point du monde affiché au **centre** de la fenêtre. `zoom` est le nombre de pixels d'écran par pixel du monde (2 = tout deux fois plus grand). Un zoom nul, négatif ou invalide lève `std::invalid_argument`. |
| `set_pixel_snapping()` | Alignement sur les pixels, activé par défaut : la translation écran est arrondie à un pixel entier, donc le pixel art n'est jamais dessiné à une position fractionnaire. Sans lui, un mouvement doux fait scintiller les texels et peut ouvrir des coutures d'un pixel entre les tuiles. |
| `world_to_screen()`, `screen_to_world()` | Les conversions, avec `écran = monde × zoom + translation`. Le dessin et le picking utilisent la même translation arrondie. |
| `visible_rect()` | La partie du monde à l'écran, pour ne dessiner que ce qui se voit. |
| `view_projection()` | La matrice pour le GPU (pixels du monde vers l'espace de découpage, Y vers le bas). |
| `begin_update()`, `interpolated(alpha)` | Interpolation : `begin_update()` mémorise la position au début d'un pas de logique, `interpolated(alpha)` renvoie une copie placée entre les deux positions. À utiliser pour dessiner **et** pointer. |

La caméra **ne tourne pas**.

### `src/moteur/include/moteur/iso.hpp` et `iso.cpp`

**Rôle** : la projection isométrique 2:1 entre une grille de tuiles et le monde. Logique pure, testée.

**Contenu**

- `IsoProjection(largeur de tuile, hauteur de tuile)`, par exemple 64×32 ou 128×64. Une taille non positive lève une exception.
- **Convention** : la tuile (i, j) couvre le carré [i, i+1) × [j, j+1) de la grille ; x grandit vers le bas droit de l'écran, y vers le bas gauche. Une tuile est un losange dont le sommet du haut est en `to_world(i, j)`.
- `to_world(tuile, hauteur)` : `monde.x = (tx − ty) × largeur / 2`, `monde.y = (tx + ty) × hauteur_tuile / 2 − hauteur`. Le paramètre `hauteur`, en pixels, remonte le point à l'écran.
- `from_world(monde)` : l'inverse pour un point au sol.
- `tile_at(monde)` : la tuile sous un point, par exemple sous la souris. Les coordonnées négatives arrondissent vers le bas (`floor`), pas vers zéro.
- `tile_sprite_position(tuile)` : le coin haut-gauche de la boîte dans laquelle dessiner l'image de la tuile ; `tile_center(tuile)` : le milieu du losange.
- `tiles_in(rectangle du monde, marge)` : le rectangle de tuiles qui contient toutes celles qui touchent le rectangle. Un rectangle d'écran devient un losange dans la grille, donc c'est la boîte englobante de ses quatre coins. La **marge** sert aux images plus hautes qu'une tuile (murs, arbres), qui dépassent au-dessus de leur case.

### `src/moteur/include/moteur/debug_ui.hpp` et `debug_ui.cpp`

**Rôle** : Dear ImGui pour les menus et fenêtres de debug, dessiné par-dessus les sprites.

**Contenu**

- Créé par le `Renderer` quand `ApplicationConfig::debug_ui` est vrai (`renderer.debug_ui()` renvoie sinon `nullptr`). Il charge une police TrueType (`debug_ui_font`, Inter pour le bac à sable : la police intégrée d'ImGui n'a pas les accents) et initialise les backends SDL3 (entrées) et SDL_GPU (rendu).
- `Application::run()` lui passe chaque événement (`process_event`) ; si ImGui l'utilise (`captures` : souris au-dessus d'une de ses fenêtres, champ de texte actif), le jeu ne le reçoit pas. `new_frame()` est appelé juste avant `Game::render()`, qui peut donc appeler les fonctions `ImGui::`.
- `Renderer::end_frame()` appelle `prepare()` avant le render pass (envoi des sommets) et `render()` à la fin du render pass, après les sprites : l'interface est toujours au-dessus, en pixels de fenêtre.
- Pas de fichier `imgui.ini` : la disposition est fixée par le code.

**À savoir** : l'interface passe par un render pass déjà ouvert, donc ne coûte ni copie ni passe en plus. Sans `debug_ui`, rien d'ImGui ne tourne : les mesures et les captures en ligne de commande sont inchangées.

### `src/moteur/include/moteur/tilemap.hpp` et `tilemap.cpp`

**Rôle** : la carte d'un niveau, sous forme de grille de cases, indépendante du rendu. Logique pure, testée. Servira aussi aux collisions et au pathfinding (jalon 4), même quand le rendu sera en 3D.

**Contenu**

- `TileId` (16 bits) et `kNoTile` (0, case vide).
- `TileType` : nom du sprite (vide si le jeu dessine la tuile autrement), `walkable`, `opaque`.
- `Tileset` : `add(type)` renvoie l'identifiant du nouveau type (à partir de 1) ; `type(id)` lève une exception pour `kNoTile` ou un identifiant inconnu.
- `TileMap(largeur, hauteur, calques)` : toutes les cases vides au départ. Une taille ou un nombre de calques non positif lève une exception. La case (i, j) de la carte est la tuile (i, j) d'`IsoProjection`.
- `at(calque, case)`, `set(calque, case, id)` (exception hors de la carte ou pour un calque inexistant), `fill(calque, id)`, `contains(case)`.
- `clip(plage)` : la partie d'une `TileRange` qui tombe dans la carte. `map.clip(iso.tiles_in(camera.visible_rect(), marge))` donne les tuiles à dessiner.
- `walkable(tileset, case)` : faux hors de la carte ou si une tuile d'un des calques n'est pas praticable.

**Pourquoi les propriétés sont dans le `Tileset`** : la carte reste un tableau de petits nombres (2 octets par case et par calque), et changer une propriété d'un type change toutes ses cases.

### `src/moteur/include/moteur/animation.hpp` et `animation.cpp`

**Rôle** : jouer des séquences d'images de façon déterministe, pilotées par le pas fixe, avec vitesse variable et événements. Logique pure, testée.

**Contenu**

- `PlayMode` : `Once` (s'arrête sur la dernière image), `Loop`, `PingPong` (aller-retour sans répéter les images d'extrémité).
- `AnimationClip(nom, images, mode, événements)` : chaque image est un nom de sprite et une durée en **ticks entiers** ; un événement est un nom sur une image. Le constructeur refuse un clip vide, une image de moins d'un tick ou un événement sur une image inexistante. `cycle_ticks()` donne la durée d'un cycle.
- `AnimationPlayer` : `play(clip)`, `restart()`, `set_speed(multiplicateur)` (0 = pause, négatif refusé), `advance(ticks, &événements)` une fois par tick, `region()` (le sprite à dessiner), `frame_index()`, `finished()` (clip `Once` seulement), `time()` / `set_time()`. Le clip doit vivre plus longtemps que le lecteur.
- **Arithmétique entière** : temps en millièmes de tick (`int64`), vitesse en millièmes (`kSpeedOne = 1000`). Pas de dérive, résultat identique sur les deux OS.
- **Événements** : chaque `advance` ajoute à la liste fournie les événements des images dont le début tombe dans (temps avant, temps après] ; la première image compte au premier `advance`. Chaque événement se déclenche donc exactement une fois, même quand un grand pas saute des images ou des boucles. La liste n'est pas vidée par le lecteur.
- `AnimationLibrary::load(chemin)` / `parse(texte, nom)` : lit un JSON versionné (format décrit en commentaire dans l'en-tête). `clip(nom)` lève une exception qui nomme le fichier et le clip ; `check_regions(atlas)` vérifie que chaque image existe dans l'atlas.

### `assets/animations.json`

**Rôle** : les clips du bac à sable. Pour l'instant un seul, `walk` : les huit images `walk_00` à `walk_07` de l'atlas `test`, 6 ticks chacune (10 images par seconde à 60 ticks par seconde), avec un événement `step` sur les images 1 et 5.

### `src/moteur/include/moteur/atlas_builder.hpp` et `atlas_builder.cpp`

**Rôle** : préparer un atlas à partir d'images en mémoire, **sans aucun accès aux fichiers ni au GPU**. C'est le cœur de l'outil d'empaquetage, séparé pour être testé.

**Contenu**

- `AtlasInput` : une image (en alpha normal) et son nom, unique ; éventuellement un pivot, sinon le pivot par défaut est le bas au milieu de l'image d'origine.
- `AtlasOptions` : taille maximale d'une page (2 048), marge (1 pixel), rognage (activé).
- `AtlasFrame` : où une image s'est retrouvée (page, position, taille rognée), ainsi que sa taille d'origine, le décalage de la partie rognée et le pivot, en pixels de l'image d'origine.
- `AtlasPage` : une page, en RGBA8.
- `build_atlas(entrées, options)` : renvoie les pages et les images, triées par nom. Elle lève `std::invalid_argument` pour un nom vide ou en double, une image dont le nombre de pixels ne correspond pas à sa taille, ou de mauvaises options, et `std::runtime_error` (avec le nom de l'image) si une image est plus grande qu'une page.

**Comment elle travaille**

1. **Tri des entrées par nom**, octet par octet, donc sans dépendre de la langue du système ni de l'ordre reçu.
2. **Rognage** : la boîte englobante des pixels dont l'alpha n'est pas nul. Une image entièrement transparente devient un pixel transparent.
3. **Ordre d'empaquetage entièrement déterminé** : plus haut d'abord, puis plus large, puis par nom.
4. **Empaquetage** avec `stb_rect_pack`, dont le tri interne (`qsort`) est **remplacé par un tri stable maison** : `qsort` range les éléments égaux dans un ordre qui change d'une bibliothèque C à l'autre, donc deux machines auraient produit deux atlas différents.
5. **Taille des pages** : pour chaque page, elle essaie les tailles en puissances de deux (la plus petite surface d'abord, la plus carrée à surface égale) et garde la première qui contient tout ce qui reste. Seul ce qui dépasse la taille maximale est réparti sur plusieurs pages pleines.
6. **Dessin des pages** : chaque image est copiée avec sa marge, chaque pixel de la marge étant une copie du pixel de bord le plus proche (extrusion).

**Résultat** : il ne dépend que des images, de leurs noms et des options, jamais de l'ordre des entrées ni de la plateforme.

### `src/moteur/include/moteur/sprite_region.hpp` et `sprite_region.cpp`

**Rôle** : décrire un sprite d'atlas et calculer où le dessiner. Logique pure, testée.

**Contenu**

- `SpriteRegion` : la page (une `Texture`), le rectangle de texture (`uv_rect`, aux bords des texels), la taille de l'image rognée, la taille de l'image d'origine, le décalage de la partie rognée dans l'origine et le pivot.
- `place_region(région, ancre, échelle, flip_x, flip_y)` : la position du coin haut-gauche et la taille à dessiner pour que **le pivot tombe sur l'ancre**. Les marges rognées sont restituées par le décalage. Avec un retournement, l'image d'origine est mise en miroir : la position `x` devient `largeur d'origine − x`, donc le décalage et le pivot sont mis en miroir avec elle et un personnage retourné garde ses pieds là où ils étaient.

### `src/moteur/include/moteur/texture_atlas.hpp` et `texture_atlas.cpp`

**Rôle** : charger un atlas produit par l'outil d'empaquetage.

**Contenu**

- `TextureAtlas::load(renderer, chemin du JSON)` : lit le JSON avec `nlohmann-json`, charge chaque page PNG (à côté du JSON) et crée sa texture (nommée d'après son fichier, par exemple `world_0.png`, pour les captures de débogage), puis construit une région par sprite. Il vérifie que la version du format est connue (1), que chaque page a la taille déclarée, que chaque sprite désigne une page qui existe et **tient dans sa page**, et que tous les champs sont présents. **Toute erreur nomme le fichier de l'atlas** et le problème.
- `region(nom)` : la région d'un sprite. Un nom inconnu lève une exception qui nomme l'atlas, le sprite et le nombre de sprites que l'atlas contient. La référence reste valide tant que l'atlas vit.
- `contains(nom)`, `names()` (triés), `page_count()`.
- Un atlas se déplace mais ne se copie pas. Les pages sont gardées dans des `unique_ptr` pour que les pointeurs des régions restent valides.

**Coordonnées de texture** : elles sont calculées aux **bords** des texels (`x / largeur de page`, `(x + w) / largeur de page`), pas à leurs centres : la région couvre exactement ses propres texels.

### `src/moteur/include/moteur/utf8.hpp` et `utf8.cpp`

**Rôle** : décoder du texte UTF-8 en points de code Unicode (`char32_t`). Logique pure, testée.

**Contenu** : `decode_utf8(texte)` lit les octets un par un. Un caractère ASCII passe tel quel ; une séquence de 2 à 4 octets est décodée normalement. Toute séquence invalide (octet de continuation isolé, séquence tronquée en fin de texte, encodage surlong, substitut UTF-16 U+D800–U+DFFF) est remplacée par un `U+FFFD` (caractère de remplacement), un octet à la fois, ce qui permet au reste du texte de continuer à se décoder normalement.

**Pourquoi ce fichier existe** : lire du texte français octet par octet casse les lettres accentuées et les guillemets, codés sur 2 ou 3 octets.

### `src/moteur/include/moteur/text_layout.hpp` et `text_layout.cpp`

**Rôle** : calculer la position de chaque glyphe d'un texte (retour à la ligne, alignement), **sans connaître ni police ni GPU**. Logique pure, testée : elle prend en paramètres une fonction d'avancement (`char32_t -> largeur`) et une fonction de crénage, ce qui permet de la tester avec de fausses métriques (par exemple une police imaginaire à chasse fixe).

**Contenu**

- `GlyphPlacement` : un glyphe positionné (`x`, `y`), sans la chasse ni l'ascendant de la police, qui restent à ajouter par l'appelant.
- `TextLine` : les glyphes d'une ligne et sa largeur naturelle (avant tout décalage d'alignement).
- `layout_text(texte, hauteur de ligne, largeur max, alignement, avance, crénage)` : le cœur de la mise en page.
  - **Retour à la ligne** : les mots (séparés par des espaces, qui se regroupent en un seul séparateur) sont placés glouton, ligne par ligne ; un `\n` force une nouvelle ligne ; un mot plus large que la largeur maximale déborde plutôt que d'être coupé.
  - **Espace insécable simplifiée** : un signe seul parmi `: ; ! ?` est rattaché au mot précédent (avec l'espace) avant le remplissage des lignes, ce qui empêche une ligne de commencer par ce signe.
  - **Alignement** : la boîte de référence est la plus grande valeur entre la largeur maximale et la ligne la plus large ; à gauche, rien ne bouge, au centre et à droite chaque ligne est décalée dans cette boîte.

### `src/moteur/include/moteur/font.hpp` et `font.cpp`

**Rôle** : charger une police TrueType et en dessiner du texte. C'est la seule brique de cette partie qui dépend du GPU.

**Contenu**

| Élément | Explication |
|---|---|
| `TextOptions` | Couleur (une seule, le texte enrichi est volontairement repoussé mais la structure peut l'accueillir), alignement, largeur maximale (retour à la ligne si non nulle), profondeur. |
| `Font::default_charset()` | Latin de base (0x20–0x7E), Latin-1 (0xA0–0xFF, accents et « »), plus œ/Œ, tirets demi et cadratin, guillemets courbes : environ 236 caractères. |
| `Font::load(renderer, chemin, taille en pixels, jeu de caractères)` | Lit le fichier avec `SDL_LoadFile`, initialise `stb_truetype`, rasterise chaque caractère du jeu à la taille demandée, et **empaquette les glyphes avec l'outil d'atlas de la partie 5** (`build_atlas`), réutilisé tel quel. Un caractère sans encre (l'espace) garde ses métriques d'avancement mais n'entre pas dans l'atlas. Chaque page est nommée `font glyphs #N` pour les captures de débogage. |
| `measure()`, `draw()` | Partagent la même fonction privée `resolve()`, qui appelle `layout_text()` avec les métriques réelles de la police : elles ne peuvent donc pas donner des résultats incohérents entre eux. |

**Pourquoi rasteriser au chargement, pas à l'exécution** : le jeu de caractères est fixe et connu à l'avance (le français tient dans Latin-1), donc un atlas dynamique rempli glyphe par glyphe n'apporte rien ici. Ce sera à revoir pour des écritures plus larges.

**Pourquoi une seule taille** : agrandir le rendu d'une police plus petite le rend flou. Charger la police à la taille physique finale évite le problème, au prix de devoir recharger la police pour changer de taille.

**Format de la texture de glyphes** : `stb_truetype` produit une image en niveaux de gris (la couverture). Elle est stockée comme du blanc avec cette couverture en alpha, ce qui la fait passer par le même pipeline de sprites que toutes les autres textures (y compris l'alpha pré-multiplié de la partie 5).

**Un piège MSVC découvert ici, mais qui dépasse le texte** : voir `cmake/Warnings.cmake` ci-dessous.

### `cmake/Warnings.cmake` (mise à jour : encodage source)

En plus du niveau d'avertissements (voir la partie 3), ce fichier active désormais **`/utf-8`** pour MSVC, sur toute la compilation Windows.

**Le problème que ce drapeau corrige** : sans lui, MSVC lit les fichiers source dans la page de codes du système, pas en UTF-8. Un caractère accentué écrit directement dans une chaîne de caractères est alors corrompu, **y compris dans un littéral `u8"..."`**, dont le standard C++ garantit pourtant l'encodage UTF-8 : le compilateur doit d'abord décoder les octets du fichier source avant de ré-encoder en UTF-8, et s'il se trompe de page de codes à cette première étape, le résultat est faux. Ce n'est pas propre aux tests de texte : n'importe quel fichier futur qui écrirait un message ou un nom contenant un caractère français serait concerné, silencieusement.

**Portée** : Clang et GCC ne sont pas concernés (ils supposent déjà une source UTF-8).

### `assets/fonts/Inter-Regular.ttf` et `Inter-OFL.txt`

**Rôle** : la police utilisée par `Font`. Inter, sous licence **SIL Open Font License 1.1** (le texte de la licence est à côté du fichier). Récupérée depuis le dépôt Google Fonts.

**À savoir** : c'est un fichier de police **variable** (plusieurs graisses et un axe optique dans un seul fichier). `stb_truetype` ne lit pas les axes de variation, seulement le tracé par défaut stocké dans la table `glyf` : il en ressort une police statique, dans son style par défaut. C'est un usage courant et sans défaut visible, mais si un rendu paraît un jour trop fin ou trop épais, c'est la première chose à vérifier.

### `tools/atlas_packer/` (outil d'empaquetage)

**Rôle** : un programme en ligne de commande qui transforme un dossier de PNG en pages et en description JSON.

**Utilisation**

```
atlas_packer --input <dossier> --output <dossier> --name <nom>
             [--page-size N] [--padding N] [--no-trim]
```

**Ce qu'il fait**

- Parcourt le dossier d'entrée (sous-dossiers compris) et prend chaque `.png`. Le nom d'un sprite est son **chemin relatif sans extension, avec des `/`** : `monsters/skeleton/walk_00.png` devient `monsters/skeleton/walk_00`.
- Lit un `pivots.json` facultatif dans ce dossier (`{"nom": [x, y]}`, en pixels de l'image d'origine) et **avertit** si un nom n'est pas une image.
- Appelle `build_atlas()`, **supprime les pages d'une exécution précédente** (elles pouvaient être plus nombreuses), puis écrit `<nom>_0.png`, `<nom>_1.png`… et `<nom>.json`.
- Écrit les fichiers avec `std::ofstream` et `std::filesystem` (les encodeurs de stb ouvrent les fichiers avec `fopen`, qui gère mal l'UTF-8 sous Windows).

**Format du JSON** (version 1) : `version`, `pages` (`file`, `width`, `height`) et `frames` (par nom : `page`, `x`, `y`, `w`, `h`, `source_w`, `source_h`, `offset_x`, `offset_y`, `pivot_x`, `pivot_y`). Les clés sont triées : le fichier est identique d'une exécution à l'autre. L'encodeur PNG de stb est lui aussi déterministe.

### `cmake/Atlas.cmake`

**Rôle** : lancer l'outil d'empaquetage au build, seulement quand c'est utile.

**Contenu** : la fonction `moteur_add_atlas(cible NAME nom SOURCES dossier)`. Elle liste les PNG et le `pivots.json` du dossier (avec `CONFIGURE_DEPENDS`, donc les ajouts et retraits sont vus sans reconfigurer), ajoute une commande dont le résultat est `<nom>.json` dans le dossier `assets/` de l'exécutable, et fait dépendre la cible de cette commande.

**Un piège traité** : quand un fichier est **retiré**, il ne reste aucune dépendance plus récente que le résultat, donc l'empaquetage ne se relançait pas et l'atlas gardait l'image supprimée. La commande dépend donc aussi d'un petit fichier qui **liste les entrées** et n'est réécrit que si cette liste change.

**À savoir** : contrairement aux shaders, l'outil est notre propre programme. Il se compile et se lance de la même façon sur Windows et sur Mac, sans fichiers pré-générés.

### `art/` (sources d'art)

**Rôle** : les images que l'on dessine, **distinctes des ressources produites** : elles ne sont jamais chargées directement, elles passent par l'outil d'empaquetage.

**Contenu** : `art/world/` (les trois losanges du sol isométrique, 64×32, et `pivots.json` qui place leur pivot au sommet, en (32, 0)) et `art/test/` (19 images de test : quatre orbes rognables, des barres de tailles différentes, un carré, un point d'un pixel, un anneau à bords doux et **huit images de marche 48×64 asymétriques**, avec une ombre semi-transparente). L'asymétrie des personnages (bras gauche long et vert, bras droit court et rouge) sert à repérer un retournement fautif.

### `src/moteur/include/moteur/paths.hpp` et `paths.cpp`

**Rôle** : savoir où chercher les fichiers du programme.

**Contenu** : `base_path()` renvoie le dossier de l'exécutable (avec un séparateur final), `asset_path("nom.png")` renvoie `<dossier de l'exécutable>/assets/nom.png`. Le chargement des shaders utilise aussi `base_path()`. `read_text_file(chemin)` lit tout un fichier avec `SDL_LoadFile` (chemins UTF-8 sous Windows) ; il sert au chargement des atlas et des animations.

**Pourquoi relatif à l'exécutable** : le programme fonctionne quel que soit le dossier de travail. Sur Mac, dans un bundle `.app`, l'emplacement des ressources sera différent : ce sera à adapter dans `base_path()` au moment du packaging, sans toucher au reste du code.

### `src/moteur/include/moteur/image.hpp` et `image.cpp`

**Rôle** : lire un fichier PNG en mémoire.

**Contenu**

- `Image` : largeur, hauteur et pixels en **RGBA 8 bits**, alpha non pré-multiplié, lignes stockées de haut en bas.
- `load_image(chemin)` : lit le fichier avec `SDL_LoadFile` (qui gère les chemins UTF-8 sous Windows, contrairement au `fopen` de stb), le décode avec stb_image et renvoie une `Image`. Lève une exception si le fichier manque ou n'est pas un PNG valide.
- Seul le format PNG est activé (`STBI_ONLY_PNG`), ce qui allège le code compilé.
- `premultiply_alpha(image)` : multiplie la couleur de chaque pixel par son alpha, en arrondissant au plus proche. Les pixels opaques sont laissés tels quels, et les pixels transparents deviennent noirs. C'est la forme que le GPU mélange et filtre correctement : avec un alpha normal, filtrer un pixel voisin d'un pixel transparent y traîne la couleur (sans signification) de ce dernier et laisse un halo. Le renderer l'applique à la création d'une texture ; les fichiers sur disque restent en alpha normal.

**À savoir** : c'est dans ce fichier que l'implémentation de stb est compilée (`STB_IMAGE_IMPLEMENTATION`). Il ne faut la définir qu'à un seul endroit.

### `apps/bac_a_sable/main.cpp`

**Rôle** : programme d'essai qui utilise le moteur.

**Contenu**

- Classe `TestScene`, qui implémente `Game` : **une** scène de test, choisie par ses `Options` (celles de la ligne de commande). Elle compte les ticks et les frames. Lancée depuis la ligne de commande (`standalone`), Échap et `--run-seconds` quittent le programme ; lancée depuis le menu, ils demandent seulement l'arrêt du test (`stop_requested()`).
- Classe `Sandbox`, qui implémente `Game` : le programme lancé **sans argument** (ou avec `--menu`). Barre de menus ImGui (**DEBUG > Tests moteur** en sous-menu, avec « Toutes les scènes... » puis chaque scène ; **DEBUG > Accueil**), écran d'accueil, page de sélection (description, réglages et bouton **Lancer** de chaque scène) et panneau du test en cours (**Arrêter le test**, **Accueil**). Échap remonte d'un cran (test → sélection → accueil). Une scène est créée et détruite dans `update()`, jamais pendant une frame : le chargement attend le GPU, et les textures d'une scène sont utilisées par la frame en cours d'enregistrement. Si une scène ne peut pas démarrer, l'erreur s'affiche sur la page de sélection.
- **Ressources** : il ne reste que la texture, chargée depuis `sprite.png` et libérée automatiquement. Le bac à sable ne crée plus ni pipeline, ni buffer, ni échantillonneur : c'est le moteur qui les possède.
- **Sprite principal** : dessiné ×8 (un texel de l'image couvre 8×8 pixels à l'écran), au centre de la fenêtre.
- **Mouvement** : `update()` calcule le décalage du sprite par rapport au centre de la fenêtre (sinus, amplitude ±300 px en x et ±60 px en y) et garde l'état précédent. `render()` interpole entre les deux avec `alpha`.
- **Dessin** : `render()` se contente d'appeler `renderer.sprites().draw(...)`. Toute la partie GPU est dans le moteur.
- **Test de charge** : `--sprites N` ajoute N petits sprites (32×32 pixels), chacun avec une teinte différente, qui rebondissent dans la fenêtre. Leurs positions de départ viennent d'un générateur pseudo-aléatoire à graine fixe (`Random`), donc la scène est **identique à chaque lancement et sur chaque OS**.
- Option `--no-batching` : un draw call par sprite, pour comparer (l'image est identique).
- Option `--depth` : les petits sprites reçoivent une profondeur égale à leur hauteur dans la fenêtre, et le sprite principal, enregistré **en premier**, la profondeur maximale. Il n'est donc au-dessus qu'à condition que le tri fonctionne : c'est un test du tri.
- Option `--freeze-after N` : après N pas de logique, plus rien ne bouge. Deux lancements donnent alors la même image, qu'on peut comparer pixel par pixel.
- **Scène isométrique** : `--iso` affiche une carte de tuiles vue à travers une `Camera2D`, un petit repère à l'origine du monde (l'image de test) et la tuile sous la souris surlignée. Seules les tuiles visibles sont envoyées (`tiles_in` + `visible_rect`). Les tuiles et le surlignage viennent de l'**atlas `world`** et sont placées par leur pivot (le sommet du losange, fixé par `pivots.json`) : une seule page, donc 2 draw calls au total, même avec `--interleave`.
- **Scène d'atlas** : `--atlas` affiche les 19 sprites de l'atlas `test` dans une grille, chacun à son pivot (un petit carré rouge marque l'ancre), puis le personnage `walk_03` de quatre façons à des positions fixes : normal, retourné, ×2 et retourné ×3. Un script compare ensuite l'écran, pixel par pixel, aux PNG sources.
- **Commandes** : flèches, ZQSD ou WASD pour déplacer la caméra (vitesse constante à l'écran, quel que soit le zoom), molette pour zoomer par paliers entiers de 1 à 8. Les entrées sont les événements SDL bruts.
- Options de la scène : `--map N` (taille de la carte, 60 par défaut), `--zoom Z`, `--camera X Y` (position du monde au centre de la fenêtre, en pixels), `--mouse X Y` (souris simulée en pixels de fenêtre, `-1 -1` pour aucune), `--interleave` (alterner les deux textures de sol tuile par tuile, ce qui casse le batching), `--no-input` (ignorer clavier et souris réels, sauf Échap, pour des mesures reproductibles).
- À la fermeture en mode `--iso`, le programme écrit la tuile qui était sous la souris (`hovered tile: i j`), ce qui permet de la comparer à une valeur calculée à part.
- **Scène de texte** : `--text` charge `Font` et affiche une phrase française (accents, œ, guillemets), un paragraphe avec retour à la ligne (largeur maximale 420 pixels), une démonstration des trois alignements dans une boîte de 300 pixels, et un compteur de FPS simplifié (recalculé une fois par seconde) en haut à droite. À la construction, le programme écrit sur la sortie standard la taille mesurée de la phrase et du paragraphe (`measured sentence: ...`, `measured paragraph: ...`), pour comparer à la boîte de pixels réellement allumés à l'écran.
- **Scène de démonstration** (partie 9) : `--demo` réunit une `TileMap` (100×100 par défaut, `--map` pour changer) à deux calques, le sol en damier de `--iso` et des **murs procéduraux** (`is_wall()` : quadrillage de lignes de grille avec des trous tous les 4 tuiles, dessinés avec la texture générique teintée, faute d'art dédié, et non praticables), des **créatures** (3 000 par défaut, `--sprites`, teintées, positions, directions — l'une des 8 directions de la grille — et allures tirées d'un générateur à graine fixe, `--seed`) et une **superposition de statistiques** (FPS, temps de frame, sprites, lots/draw calls, nombre d'événements `step` reçus) en haut à droite. Les créatures jouent le clip `walk` d'`assets/animations.json` à une vitesse liée à leur allure, chacune à sa propre phase, retournées quand elles vont vers la gauche de l'écran, et font demi-tour devant un mur ou le bord de la carte (`TileMap::walkable`). Tri en profondeur des murs et créatures par `tuile.x + tuile.y`, qui suit l'ordre de la projection isométrique. **Réduit par rapport à la scène prévue au départ du jalon** : un seul cycle de marche retourné selon la direction, au lieu de 8 directions (le moteur 2D ne servant plus qu'à l'UI et aux effets une fois le jeu passé en 3D).
- Option `--seed N` : graine du générateur pseudo-aléatoire de `--demo` (position et direction des créatures).
- Option `--capture chemin.png` (partie 9) : avec `--freeze-after N`, écrit la première frame gelée en PNG (`Renderer::request_capture()`), pour une comparaison de pixels automatique et reproductible. Fonctionne sur n'importe quelle scène du bac à sable, pas seulement `--demo`.
- Option `--run-seconds N` : le programme se ferme seul après N secondes de simulation. Elle sert de test automatique.
- Option `--no-vsync` : désactive le VSync, pour mesurer les FPS sans être limité par l'écran.
- Option `--still` : le sprite reste immobile au centre, pour mesurer les pixels à l'écran.
- Option `--report` : écrit à la fermeture un résumé des temps CPU par frame (moyenne, percentile 99, maximum, phases).
- Affiche la version de SDL, puis un résumé (temps simulé, ticks, frames) à la fermeture.
- Les erreurs d'initialisation sont attrapées et renvoient le code 1.

**À savoir (partie 9)** : `SpriteRenderer::set_view_projection()` s'applique à toute la frame (un seul uniforme de projection par `render()`) : impossible de mélanger des sprites en espace monde (la carte) et en espace écran (le texte de statistiques) dans la même frame. `render_demo()` contourne ça en convertissant l'ancre d'écran voulue via `camera.screen_to_world()` avant de dessiner le texte, avec une profondeur énorme pour rester devant tout le reste — au prix d'un texte qui change de taille avec le zoom. Une vraie couche d'UI en espace écran demanderait un second passage `prepare()`/`render()` par frame, non fait ici (voir `JALON_2_RENDU_2D.md`, partie 9). Autre piège rencontré : `renderer.stats()` n'est valide qu'entre `end_frame()` et le `begin_frame()` suivant, donc lue dans `render()` elle est déjà à zéro ; `update()` (qui tourne juste avant le `begin_frame()` de l'itération) la met en cache dans `last_stats_` pour que `render_demo()` s'en serve.

**Évolution prévue** : il sert de terrain d'expérimentation, pas de futur jeu. Le batching de sprites, la caméra et les atlas arrivent au jalon 2.

### `tests/` (tests unitaires)

**Rôle** : vérifier automatiquement la logique qui n'a pas besoin de GPU ni de fenêtre.

**Contenu**

- `CMakeLists.txt` : crée l'exécutable `moteur_tests` (lié à la bibliothèque `moteur` et à doctest) et l'enregistre auprès de CTest.
- `main.cpp` : définit le point d'entrée de doctest (`DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`). Il ne faut le définir qu'à un seul endroit.
- `test_fixed_timestep.cpp` : nombre de pas selon le temps écoulé, reste conservé d'une frame à l'autre, `alpha` toujours dans [0, 1), plafond de rattrapage, temps négatif, même durée simulée quelle que soit la cadence, paramètres invalides.
- `test_frame_stats.cpp` : moyenne, maximum, percentiles (rang le plus proche), indépendance de l'ordre d'insertion, pic rare que la moyenne cache, tampon circulaire, remise à zéro, capacité nulle.
- `test_sprite_batcher.cpp` : conversion de couleur, quatre sommets d'un sprite (positions, coordonnées de texture, teinte), retournements, regroupement par texture et ordre d'enregistrement, tri par profondeur, stabilité à profondeur égale, ordre déjà croissant, priorité de la profondeur sur la texture, plafond de sprites par lot, batching désactivé, remise à zéro entre deux frames, profondeurs négatives, et une **comparaison avec un tri de référence sur 3 000 profondeurs aléatoires** (négatives, nulles, égales, très différentes).
- `test_atlas_builder.cpp` : une image copiée exactement, pages en puissances de deux, rognage et décalage, sans rognage, image transparente, extrusion des bords et des coins, marge nulle, pivot par défaut et explicite (exprimé dans l'image d'origine), tri par nom, **300 images aléatoires sans superposition ni dépassement**, **contenu de chaque pixel de 40 images retrouvé dans la page**, **résultat indépendant de l'ordre des entrées** (trois ordres, avec beaucoup d'images de même taille), plusieurs pages, image trop grande refusée avec son nom, entrées invalides, page compacte (16 images donnent exactement 128×128), atlas vide. Un benchmark, ignoré par défaut, mesure la vitesse (`moteur_tests -tc="atlas packing benchmark" --no-skip`).
- `test_sprite_region.cpp` : pivot sur l'ancre, marges restituées, échelle autour du pivot, retournement en miroir autour du pivot (horizontal et vertical), sprite centré qui ne bouge pas, retourner deux fois.
- `test_image.cpp` : alpha pré-multiplié (pixels opaques inchangés, transparents mis à zéro, arrondi au plus proche, image vide).
- `test_camera.cpp` : la position au centre de la fenêtre, zoom, sens de l'axe Y, aller-retour monde / écran / monde (avec et sans alignement), alignement sur les pixels (y compris avec une fenêtre de taille impaire), rectangle visible, redimensionnement, interpolation, coins de la fenêtre en espace de découpage, accord entre la matrice et `world_to_screen`, zoom invalide, conversion points / pixels.
- `test_tilemap.cpp` : numérotation du `Tileset`, calques indépendants, `fill`, refus des cases hors de la carte, `clip` d'une plage à l'intérieur, à cheval ou hors de la carte, plage visible d'une caméra dans un coin, `walkable` sur plusieurs calques.
- `test_animation.cpp` : clips invalides, durée d'un cycle, image affichée à chaque tick (boucle, une fois, aller-retour), vitesses ×2, ×0,5 et 0, vitesse négative refusée, événements exactement une fois (y compris avec un pas de 20 ticks et une vitesse de ×0,7 sur 80 ticks), clip `Once` après la fin, `set_time`, lecture du JSON et messages d'erreur.
- `test_iso.cpp` : les coins de tuile sur le réseau 2:1, la hauteur, `from_world` inverse de `to_world`, d'autres tailles de tuile, le milieu et les coins d'une tuile, les coordonnées négatives, la position de la boîte d'image, `tiles_in` (propriété vérifiée par force brute sur 121 × 121 tuiles), la marge, la taille d'une plage d'écran, et les tailles invalides.

**Les valeurs de test sont des puissances de deux** (1/64, 1/4, etc.), exactes en virgule flottante : les résultats ne dépendent pas d'arrondis, donc sont identiques sur les deux OS.

**Lancer les tests** : `ctest` depuis le dossier de build, ou directement `moteur_tests`. Chaque test a été vérifié : casser volontairement le plafond de rattrapage fait échouer deux tests, casser la gestion des profondeurs négatives du tri en fait échouer deux autres, fausser l'inverse de la projection isométrique en fait échouer cinq, et ignorer l'alignement sur les pixels en fait échouer deux.

### `shaders/sprite.vert.hlsl` et `shaders/sprite.frag.hlsl`

**Rôle** : les sources des shaders, écrites une seule fois en HLSL et converties pour chaque backend.

**Convention de nommage** : `<nom>.vert.hlsl`, `<nom>.frag.hlsl` ou `<nom>.comp.hlsl`. Le suffixe détermine l'étage. Le nom sans `.hlsl` (par exemple `sprite.vert`) est celui qu'on passe à `Renderer::load_shader()`.

**Contenu**

- **Vertex shader** : reçoit la position en pixels (`TEXCOORD0`), les coordonnées de texture (`TEXCOORD1`) et la teinte (`TEXCOORD2`, 4 octets lus comme des flottants dans [0, 1]). Il transforme la position avec la matrice `view_projection`, un **buffer d'uniformes** déclaré `register(b0, space1)` et envoyé une seule fois par frame.
- **Fragment shader** : échantillonne la texture avec l'échantillonneur, puis **multiplie le résultat par la teinte** : le blanc laisse la texture inchangée. Les textures étant pré-multipliées, la teinte l'est aussi : son alpha atténue tout le pixel, sa couleur ne multiplie que la couleur. La texture et l'échantillonneur sont déclarés `register(t0, space2)` et `register(s0, space2)`.
- Le point d'entrée s'appelle `main` dans les deux cas.

**Conventions de liaison de SDL_GPU** (à respecter dans tout nouveau shader) :

| Étage | Textures, échantillonneurs, buffers de stockage | Buffers d'uniformes |
|---|---|---|
| Vertex | `space0` | `space1` |
| Fragment | `space2` | `space3` |

Les attributs de sommets se relient par l'index : `TEXCOORDn` correspond à l'attribut de `location = n` dans le pipeline. Le nombre de samplers et d'uniformes déclaré dans `ShaderInfo` doit correspondre à ce que le shader utilise.

**Piège** : en cas d'erreur, le compilateur affiche `hlsl.hlsl` comme nom de fichier au lieu du vrai nom, avec le bon numéro de ligne.

### `assets/sprite.png`

**Rôle** : image de test de 32×32 pixels, avec transparence. Elle est copiée à côté de l'exécutable à chaque build.

**Contenu** : un disque à contour noir divisé en quatre quadrants de couleurs différentes (rouge en haut à gauche, vert en haut à droite, bleu en bas à gauche, jaune en bas à droite), entouré d'un halo blanc semi-transparent, sur fond transparent. Chaque élément sert à un test : les quadrants révèlent un retournement de l'image, le halo teste le blending, le fond transparent teste l'alpha.

### `art/world/tile_a.png`, `tile_b.png` et `tile_highlight.png`

**Rôle** : les images de la scène isométrique, des losanges de **64×32 pixels** (rapport 2:1) sur fond transparent, avec un contour d'un pixel. Ce sont des **sources d'art** : elles n'étaient à l'origine que des assets copiés, elles sont désormais empaquetées dans l'atlas `world`. Une fois rognés, les losanges font 62×32 pixels (les colonnes extrêmes sont vides) ; leur pivot, fixé par `art/world/pivots.json`, reste le sommet du losange en (32, 0) dans l'image d'origine.

**Contenu** : `tile_a` et `tile_b` sont deux teintes de vert, pour faire un damier ; `tile_highlight` est jaune, dessiné à 60 % d'opacité par-dessus la tuile sous la souris. Le losange est défini par |x − 32| / 32 + |y − 16| / 16 ≤ 1, calculé au centre de chaque pixel : les losanges voisins, décalés de (32, 16), **recouvrent le plan sans aucun trou**, ce qui est la condition pour qu'il n'y ait pas de couture.

### `shaders/generated/msl/*.msl`

**Rôle** : les shaders convertis en MSL (langage de Metal), **générés automatiquement puis versionnés**. Le Mac les copie tels quels au build, car il ne peut pas les produire lui-même.

**À ne pas éditer à la main.** Après toute modification d'un `.hlsl`, régénérer avec la cible `export_msl_shaders` sous Windows, puis committer le résultat. Le point d'entrée y est `main0`.

---

## 5. Fichiers de dépôt et de documentation

### `.gitignore`

**Rôle** : liste ce que Git ne doit pas suivre.

**Contenu** : dossiers de build (`build/`, `cmake-build-*/`, `out/`), paquets vcpkg installés (`vcpkg_installed/`), fichiers d'IDE (`.idea/`, `.vs/`, `.vscode/`, `*.user`), fichiers système (`.DS_Store`, `Thumbs.db`).

**Règle** : tout ce qui se régénère (build, dépendances) est ignoré. Seuls les fichiers sources et de configuration sont versionnés.

### `.gitattributes`

**Rôle** : rendre les fins de ligne cohérentes entre Windows (CRLF) et macOS (LF), et éviter des diffs parasites.

**Contenu**

- `* text=auto` : Git normalise les fins de ligne des fichiers texte.
- `*.sh` en LF et `*.bat` en CRLF : ces scripts exigent une fin de ligne précise pour fonctionner.
- Extensions binaires (`png`, `jpg`, `ogg`, `wav`) marquées `binary` : Git ne les modifie jamais.

**Quand le modifier** : ajout de nouveaux formats binaires (polices, modèles, etc.).

### `README.md`

**Rôle** : première page lue par quelqu'un (ou toi dans six mois) qui clone le projet.

**Contenu** : prérequis par OS, étapes de build avec CLion et en ligne de commande, structure du projet, choix de configuration (triplets, cible macOS, architecture).

**Règle** : tout ce qui est nécessaire pour construire depuis un clone propre doit y figurer. C'est l'un des critères de fin du jalon 1.

---

## 6. Dossiers générés (à ne pas modifier)

Ces éléments apparaissent après la configuration et ne sont pas versionnés.

| Élément | Origine | Contenu |
|---|---|---|
| `build/<preset>/` | CMake / preset | Fichiers Ninja, objets compilés, exécutable final |
| `build/<preset>/vcpkg_installed/` ou `vcpkg_installed/` | vcpkg | SDL3 compilé et ses en-têtes, plus l'outil `shadercross` et DXC sur Windows |
| `build/<preset>/shader_tools/` | `Shaders.cmake` | Copie privée de `shadercross.exe` et de ses DLL |
| `<dossier de l'exécutable>/shaders/` | Build | Shaders compilés (`.dxil`) ou copiés (`.msl`), chargés au démarrage |
| `<dossier de l'exécutable>/assets/` | Build | Copie du dossier `assets/` |
| `compile_commands.json` | CMake | Commandes de compilation, lues par CLion et clangd |
| `cmake-build-*/` | CLion | Dossiers de build propres à CLion selon le profil |
| `.idea/` | CLion | Paramètres du projet dans l'IDE |

En cas de comportement incohérent, **supprimer le dossier de build et reconfigurer** règle la majorité des problèmes.

---

## 7. Fichiers à venir

Ils seront ajoutés au fil des jalons.

| Fichier / dossier | Quand | Rôle |
|---|---|---|
| `src/moteur/core/`, `platform/`, `renderer/` | Quand le nombre de fichiers le justifiera | Sous-dossiers pour ranger la boucle de jeu, la fenêtre et les entrées, le rendu (les fichiers sont pour l'instant à plat dans `src/moteur/`) |
| `third_party/` | Si besoin | Dépendances absentes de vcpkg |
