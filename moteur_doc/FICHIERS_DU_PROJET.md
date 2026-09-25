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
      include/moteur/aabb.hpp         en-tête public : boîte englobante alignée sur les axes
      include/moteur/camera3d.hpp     en-tête public : caméra 3D isométrique, picking, frustum (sans GPU)
      include/moteur/color.hpp        en-tête public : sRGB et tone mapping (sans GPU)
      include/moteur/mesh.hpp         en-tête public : maillages 3D, primitives, tampons GPU
      include/moteur/billboard_batcher.hpp    en-tête public : tri et sommets des billboards (sans GPU)
      include/moteur/billboard_renderer.hpp   en-tête public : sprites dans le monde 3D
      include/moteur/mesh_batcher.hpp     en-tête public : culling et regroupement des maillages (sans GPU)
      include/moteur/mesh_renderer.hpp    en-tête public : dessin des maillages 3D (PBR, instancié)
      include/moteur/material.hpp     en-tête public : matériau PBR (glTF)
      include/moteur/environment.hpp  en-tête public : éclairage d'environnement (IBL)
      include/moteur/shadow.hpp       en-tête public : ombres du soleil et des lumières ponctuelles (sans GPU)
      include/moteur/model.hpp        en-tête public : modèles glTF (CPU et GPU)
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
      include/moteur/paths.hpp        en-tête public : chemins des ressources, lecture des fichiers
      include/moteur/asset_cache.hpp  en-tête public : cache d'un type d'asset (sans GPU)
      include/moteur/assets.hpp       en-tête public : gestionnaire d'assets
      include/moteur/ktx_texture.hpp  en-tête public : lecture et transcodage des KTX2 (sans GPU)
      include/moteur/input.hpp        en-tête public : actions, profils de touches, manettes, rejeu
      include/moteur/world.hpp        en-tête public : le monde en entités (EnTT), composants et systèmes du moteur
      include/moteur/state_stack.hpp  en-tête public : pile d'états de jeu, état de chargement
      include/moteur/process_memory.hpp   en-tête public : mémoire du processus (contrôle des fuites)
      include/moteur/audio.hpp        en-tête public : sons, musique, groupes, auditeur (miniaudio)
      include/moteur/audio_rules.hpp  en-tête public : placement des sons et choix des voix (sans périphérique)
      include/moteur/sound.hpp        en-tête public : sons décodés et musiques en flux
      include/moteur/screenshot.hpp   en-tête public : écriture PNG d'une capture GPU
      include/moteur/debug_lines.hpp      en-tête public : lignes de debug (boîtes, frustums, axes)
      include/moteur/debug_ui.hpp     en-tête public : interface de debug (Dear ImGui)
      include/moteur/debug_tools.hpp  en-tête public : outils de debug (inspecteur d'entités, assets, entrées, audio, états)
      version.cpp                     implémentation
      application.cpp                 fenêtre et boucle de jeu
      renderer.cpp                    périphérique GPU, frame, ressources
      screenshot.cpp                  conversion de pixels bruts en PNG (capture)
      debug_lines.cpp                 lignes de debug : construction et dessin
      debug_ui.cpp                    ImGui : contexte, backends SDL3 et SDL_GPU
      debug_tools.cpp                 fenêtres des outils de debug, inscription des composants, imgui.ini
      sprite_renderer.cpp             pipeline de sprites, envoi et draw calls
      sprite_batcher.cpp              tri, sommets et découpe en lots
      state_stack.cpp                 pile d'états : transitions différées, mise à jour, dessin, chargement par étapes
      process_memory.cpp              mémoire du processus selon l'OS (Windows, macOS, Linux)
      audio.cpp                       moteur miniaudio : voix, file de la frame, musique, groupes, limiteur, périphérique
      audio_rules.cpp                 atténuation, panoramique, fusion des demandes, limite de voix
      sound.cpp                       décodage des sons (WAV, FLAC, MP3, OGG), ouverture des musiques, WAV de test
      miniaudio.c                     implémentation de miniaudio et de stb_vorbis (C, bibliothèque à part)
      camera.cpp                      matrices et conversions de la caméra
      iso.cpp                         conversions grille / monde, plage de tuiles
      camera3d.cpp                    matrices, rayon de la souris, frustum, interpolation, suivi
      color.cpp                       conversions sRGB / linéaire, tone mapping PBR Neutral
      tone_mapper.hpp / .cpp          interne : passe plein écran HDR -> écran
      mesh.cpp                        primitives (cube, plan, sphère), boîtes, envoi au GPU
      billboard_batcher.cpp           coins face à la caméra, tri, lots par texture
      billboard_renderer.cpp          pipeline des billboards, envoi des sommets
      mesh_batcher.cpp                frustum culling, lots (maillage, textures), données d'instance
      mesh_renderer.cpp               pipelines 3D, matériaux, lumières, environnement, instances
      environment.cpp                 ciel, lecture .hdr, harmoniques sphériques, préfiltrage
      shadow.cpp                      cadrage de la carte du soleil, faces, sélection et cache des ombres ponctuelles
      model.cpp                       lecture glTF (cgltf), pièces, matériaux, images, envoi au GPU
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
      asset_cache.cpp                 clés d'assets (normalisation, casse sur le disque)
      assets.cpp                      chargeurs, remplacements, libération, rechargement à chaud (efsw)
      ktx_texture.cpp                 KTX2 : libktx, transcodage vers BC7 / BC5 / RGBA8
      input.cpp                       sources, profils JSON, transitions par tick, manettes, enregistrements
      world.cpp                       interpolation, hiérarchie, collecte pour le rendu, cache des entités fixes
      paths.cpp                       dossier de l'exécutable et des assets
  apps/
    bac_a_sable/                      exécutable de test
      CMakeLists.txt
      main.cpp                        menu, scènes de test, ligne de commande
      demo3d.hpp, demo3d.cpp          scène de démonstration 3D
      blender_compare.hpp, .cpp       scène comparée avec Blender
      sandbox_scene.hpp               interface des scènes du menu, aléatoire, murs des démos
      states_demo.hpp, .cpp           tranche jouable (test des états) : titre, chargement, jeu avec héros, pause
      audio_test.hpp, .cpp            test de l'audio : carte des sons, feu qui tourne, rafale, volumes
  tools/
    atlas_packer/                     outil d'empaquetage d'atlas
      CMakeLists.txt
      main.cpp
    models/                           modèle de référence, téléchargement des assets de test
    blender/                          rendu Blender de la scène comparée, comparaison d'images
    input/                            rejeu scripté de la tranche jouable (tests/data/slice_replay.json)
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
    test_camera3d.cpp                 tests de la caméra 3D
    test_color.cpp                    tests des conversions de couleur
    test_environment.cpp              tests de l'éclairage d'environnement
    test_shadow.cpp                   tests du cadrage des ombres
    test_mesh.cpp                     tests des primitives 3D
    test_mesh_batcher.cpp             tests du culling et des lots de maillages
    test_billboard_batcher.cpp        tests des billboards
    test_debug_lines.cpp              tests des lignes de debug
    test_model.cpp                    tests de la lecture glTF
    test_tilemap.cpp                  tests de la carte de tuiles
    test_animation.cpp                tests des animations
    test_asset_cache.cpp              tests du cache d'assets
    test_ktx_texture.cpp              tests du décodage KTX2
    test_input.cpp                    tests des entrées (actions, profils, rejeu)
    test_world.cpp                    tests des systèmes du monde (registre sans GPU)
    test_state_stack.cpp              tests de la pile d'états et de l'état de chargement
    test_audio.cpp                    tests de l'audio (règles, décodage, mixage sans périphérique)
    test_anti_aliasing.cpp            tests des noms des modes d'anticrénelage
    test_atlas_builder.cpp            tests de l'empaquetage d'atlas
    test_sprite_region.cpp            tests du placement des sprites d'atlas
    test_image.cpp                    tests de l'alpha pré-multiplié
    test_utf8.cpp                     tests du décodage UTF-8
    test_text_layout.cpp              tests de la mise en page du texte
  shaders/
    sprite.vert.hlsl                  sources HLSL : sprites
    sprite.frag.hlsl
    mesh.vert.hlsl, mesh.frag.hlsl    maillages 3D (PBR)
    tonemap.vert.hlsl, tonemap.frag.hlsl    passe de composition (tone mapping)
    fxaa.frag.hlsl                    anticrénelage FXAA
    shadow.vert.hlsl, shadow.frag.hlsl      ombre du soleil
    point_shadow.vert.hlsl, point_shadow.frag.hlsl, shadow_clear.vert.hlsl    ombres ponctuelles
    billboard.vert.hlsl, billboard.frag.hlsl    sprites dans le monde 3D
    debug_line.vert.hlsl, debug_line.frag.hlsl, depth_view.frag.hlsl    débogage
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
| `dependencies` | Liste des bibliothèques : `sdl3`, `glm` (mathématiques : matrices et vecteurs), `stb` (décodage des PNG), `doctest` (tests unitaires), `nlohmann-json` (description des atlas et des animations), `cgltf` (lecture des modèles glTF), `entt` (cache et poignées d'assets, puis l'ECS), `efsw` (surveillance des fichiers pour le rechargement à chaud), `ktx` avec la fonctionnalité `tools` (lecture et transcodage des textures KTX2, et l'outil `ktx` qui les encode), `imgui` avec les fonctionnalités `sdl3-binding` et `sdlgpu3-binding` (interface de debug ; ses shaders sont précompilés pour chaque backend, donc rien à ajouter à la chaîne des shaders), et `sdl3-shadercross` **uniquement sur Windows** (`"platform": "windows"`). Ce dernier apporte l'outil `shadercross`, le compilateur DirectX (DXC) et SPIRV-Cross. Il impose aussi la fonctionnalité `vulkan` de SDL3, sans effet sur le backend choisi. Enfin `winpixevent`, **uniquement sur Windows** : la DLL `WinPixEventRuntime.dll` de Microsoft (MIT), copiée à côté de l'exécutable, grâce à laquelle SDL nomme les passes dans les captures RenderDoc et PIX sous Direct3D 12 (sans elle, rien ne change sauf ces noms). |

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
| `ApplicationConfig` | `gpu_timing` (temps GPU par passe, voir `Renderer::set_gpu_timing`), titre, taille de la fenêtre (en points), `pixel_width` / `pixel_height` (si > 0 : la fenêtre est dimensionnée pour avoir exactement ce nombre de pixels, quelle que soit la densité de l'écran, pour comparer des captures entre machines), fréquence de la logique (`fixed_hz`, 60 par défaut), plafond de rattrapage (`max_frame_time`, 0,25 s), `vsync` (activé par défaut), `gpu_debug` (activé hors Release) et `report_performance` (affiche un résumé des temps CPU à la fin, et les compteurs 3D par passe). |
| `Game` | Interface que le programme implémente. `update(dt)` est appelé à fréquence fixe avec un `dt` constant. `render(renderer, alpha)` est appelé une fois par frame dessinable et sert à **enregistrer** ce qu'il faut dessiner (`renderer.sprites().draw(...)`) : rien n'est envoyé au GPU tant que la frame n'est pas terminée. `alpha` est l'avancement entre deux mises à jour, pour l'interpolation. `on_event()` reçoit les événements SDL bruts. |
| `Application` | Crée SDL, la fenêtre, le `Renderer` et le gestionnaire d'assets (`assets()`) dans son constructeur, les libère dans l'ordre inverse dans son destructeur (RAII), et contient la boucle `run(Game&)`. `quit()` demande l'arrêt. Non copiable. `ApplicationConfig::assets_source_directory` active le rechargement à chaud. |
| `to_pixels(point)` | Convertit une position en points de fenêtre (ce que SDL donne pour la souris) en pixels (ce que le rendu dessine). Identique sur un écran normal, différent d'un facteur 2 sur beaucoup de Mac. |

**À savoir** : c'est la première API du moteur. `Game` est ce que l'ARPG devra implémenter.

### `src/moteur/application.cpp`

**Rôle** : implémentation de `Application`.

**Contenu**

- **Constructeur** : `SDL_Init`, `SDL_CreateWindow` (redimensionnable, haute densité de pixels), puis création du `Renderer`. Lève `std::runtime_error` en cas d'échec et nettoie ce qui a déjà été créé. Écrit dans les logs la taille en points et en pixels.
- **Destructeur** : libère le `Renderer` **avant** de détruire la fenêtre, puis appelle `SDL_Quit`.
- **`run()`** : mesure le temps avec `SDL_GetPerformanceCounter`, confie le calcul des pas de logique à un `FixedTimestep`, applique les rechargements d'assets en attente (`Assets::update()`), traite les événements, exécute les `update()` de durée fixe, puis dessine la frame (`begin_frame`, `Game::render`, `end_frame`).
- **Mesures** : chaque frame dessinée est chronométrée en trois phases (événements et mises à jour, enregistrement par `Game::render`, exécution par `end_frame`). L'attente de l'écran, qui a lieu dans `begin_frame`, est **exclue** : c'est de l'attente, pas du travail. Les 60 premières frames sont ignorées du résumé (préchauffage). Une fois par seconde, le titre de la fenêtre affiche les FPS, les ticks par seconde, le temps CPU moyen, le nombre de sprites et de draw calls. Avec `report_performance`, un résumé (moyenne, percentile 99, maximum, moyenne par phase) est écrit dans les logs à la fermeture.
- **Cadence** : c'est le VSync du swapchain qui rythme la boucle, car `begin_frame()` attend l'image suivante. Si la fenêtre est minimisée, rien n'est dessiné et la boucle attend 10 ms pour ne pas tourner à vide.

### `src/moteur/include/moteur/renderer.hpp`

**Rôle** : interface publique du rendu GPU.

**Contenu**

| Élément | Explication |
|---|---|
| `RendererConfig` | `debug` (couche de validation du GPU), `vsync`, et l'interface de debug (`debug_ui`, sa police et sa taille). |
| `ShaderInfo` | Ce qu'un shader déclare : son étage (vertex ou fragment) et le nombre de samplers, uniformes et tampons de stockage qu'il utilise. SDL_GPU s'en sert pour valider les liaisons. |
| `Texture` | Une texture GPU (`GpuTexture`) et sa taille en pixels. |
| `RenderStats` | Compteurs de la dernière frame : sprites dessinés, draw calls (toutes passes) et octets envoyés au GPU ; pour la 3D, par passe : maillages soumis, dessinés (après culling), triangles et draw calls de la passe « scene » (`meshes_submitted`, `meshes`, `triangles`, `mesh_draw_calls`) et de la passe « shadow » (`shadow_casters_submitted`, `shadow_casters`, `shadow_triangles`, `shadow_draw_calls`) ; pour les ombres ponctuelles, les lumières ombrées et redessinées, et les maillages, triangles et draw calls de leur passe (`point_shadow_*`) ; les billboards et leurs draw calls ; les lignes de debug ; avec `gpu_timed`, les temps GPU approximatifs de chaque partie de la frame (`gpu_ms`, indices `GpuTime`). Remis à zéro à chaque `begin_frame()`. |
| `load_shader()` | Charge `shaders/<nom>.<ext>` à côté de l'exécutable, avec l'extension et le point d'entrée du backend actif. Lève une exception si le fichier manque ou si le shader est rejeté. Renvoie un `GpuShader` qui se libère seul. Le shader est nommé `<nom>` pour les outils de capture. |
| `swapchain_format()` | Format des pixels de la fenêtre, nécessaire pour créer un pipeline graphique. |
| `create_buffer(usage, données, taille, nom)` | Crée un buffer GPU (sommets ou indices), y envoie les données et attend la fin. Renvoie un `GpuBuffer`. |
| `create_buffer(usage, taille, nom)` | Crée un buffer vide, sans rien envoyer ni attendre : assez léger pour un buffer qui grandit en cours de route. |
| `create_transfer_buffer()` | Crée une zone de transfert : la mémoire que le CPU écrit avant qu'un copy pass l'envoie au GPU. |
| `create_texture(image, nom)` | Crée une texture RGBA8 à partir d'une `Image`, sans mipmaps, en UNORM (pas de conversion sRGB). **L'alpha est pré-multiplié dans la couleur** avant l'envoi, quel que soit l'alpha de l'image. Renvoie une `Texture`. |
| `create_sampler(filtre, nom)` | Crée un échantillonneur avec le filtrage voulu (`NEAREST` pour le pixel art, `LINEAR` pour un rendu lissé) et renvoie un `GpuSampler`. Les coordonnées hors de [0, 1] sont ramenées au bord. |
| `Renderer` (passes) | Avant la passe « scene » : la passe **« shadow »**, profondeur des maillages vue du soleil, quand il y a de la 3D, que les ombres sont activées et que le soleil éclaire. |
| `Renderer` | Possède le périphérique GPU. Une frame se déroule en deux phases : `begin_frame()` acquiert l'image de la fenêtre (et recrée la cible de la scène 3D et sa profondeur si la taille de rendu a changé), le jeu **enregistre** ce qu'il veut dessiner, puis `end_frame()` envoie les données (copy passes), dessine et soumet. La passe **« scene »**, seulement si des maillages ont été enregistrés, dessine la 3D en couleurs linéaires HDR (`kSceneFormat`, `R16G16B16A16_FLOAT`) à la résolution de rendu ; la passe **« compose »**, sur l'écran, y applique le tone mapping (ou efface l'écran sans 3D), puis dessine les sprites du monde, l'interface et ImGui. **Une frame sans 3D est identique à celle du rendu 2D d'origine.** Chaque passe est nommée par un groupe de debug. Non copiable. |
| `set_render_scale()`, `set_exposure()`, `scene_width()`, `scene_height()` | Résolution de rendu de la 3D (fraction de la fenêtre, de 0,25 à 1 ; l'interface reste à pleine résolution), exposition avant tone mapping, et taille de l'image 3D de la frame. |
| `TextureSettings`, `create_texture(image, réglages, nom)` | Choisit le format sRGB (le GPU convertit en linéaire à la lecture : textures de couleur), les **mipmaps** (chaîne complète, générée par le GPU) et la pré-multiplication de l'alpha. `create_texture(image, nom)` garde le comportement d'origine, celui des sprites. |
| `sprites()` | Le `SpriteRenderer` du **monde 2D**, dessiné dans la passe « compose », par-dessus la 3D, avec la caméra de `set_view_projection()` (en pixels de fenêtre sans elle). |
| `screen_sprites()` | Le `SpriteRenderer` de l'**interface**, dessiné dans la passe « compose » après les sprites du monde, toujours en pixels de fenêtre quelle que soit la caméra : texte et panneaux qui ne doivent ni bouger ni grossir avec le monde. |
| `depth_format()`, `depth_format_name()` | Format de la texture de profondeur de la passe « scene », choisi au démarrage : `D32_FLOAT` si le GPU sait s'en servir comme cible, sinon `D24_UNORM`, sinon `D16_UNORM` (le seul garanti par SDL). Écrit dans le log (`GPU: ... depth=...`). |
| `debug_ui()` | L'interface de debug (ImGui), ou `nullptr` si elle n'est pas activée. |
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
- `gpu_resource_counts()` : le nombre d'objets GPU vivants de chaque sorte (compteurs tenus par `GpuResource`), pour vérifier que rien ne fuit d'une scène à l'autre (jalon 4, partie 6).
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
- Le constructeur `SpriteRenderer(renderer, format de profondeur, nom)` charge les shaders `sprite.vert` et `sprite.frag`, crée l'échantillonneur `NEAREST`, le buffer d'indices statique (16 384 quads) et le pipeline avec mélange en **alpha pré-multiplié** (`ONE`, `ONE_MINUS_SRC_ALPHA`) : les textures contiennent une couleur déjà multipliée par l'alpha. Le **format de profondeur** est celui de la passe où ce renderer dessine (`SDL_GPU_TEXTUREFORMAT_INVALID` pour une passe sans profondeur) : le pipeline doit le déclarer, mais ne teste ni n'écrit la profondeur, l'ordre des sprites reste celui de leur clé `depth`. Le **nom** préfixe chaque ressource (`sprite.sampler`, `sprite.indices`, `sprite pipeline`, `sprite.vertices` pour le buffer dynamique créé dans `ensure_capacity()` ; `screen sprite...` pour celui de l'interface), pour les retrouver dans une capture RenderDoc ou Metal.
- Le `Renderer` en possède **deux** : le monde (`sprites()`) et l'interface (`screen_sprites()`), tous deux dans la passe « compose », sans profondeur.
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

`interpolate(précédent, actuel, t)` : la valeur à dessiner entre deux pas, calculée `précédent + (actuel − précédent)·t`, **exactement** `précédent` quand rien n'a bougé (ce que `glm::mix` ne garantit pas : une scène figée bougerait d'un arrondi selon `t`, et deux captures identiques différeraient). À utiliser pour toute interpolation entre deux ticks (les caméras le font).

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
- `imgui.ini` : aucun par défaut ; `set_settings_file()` le place (l'`Application` le met dans le dossier des préférences quand elle crée les outils de debug). Seules les fenêtres des outils y sont retenues, les autres fenêtres du bac à sable ont `NoSavedSettings`.

**À savoir** : l'interface passe par un render pass déjà ouvert, donc ne coûte ni copie ni passe en plus. Sans `debug_ui`, rien d'ImGui ne tourne : les mesures et les captures en ligne de commande sont inchangées.

### `src/moteur/include/moteur/debug_tools.hpp` et `debug_tools.cpp`

**Rôle** : les outils de debug du moteur (jalon 4, partie 8), en fenêtres ImGui : `app.debug_tools()`, nul sans `ApplicationConfig::debug_ui`.

**Contenu**

- `ComponentInspectors` : `add<T>(nom, fonction)` (la fonction dessine le composant avec ImGui et dit s'il a changé ; elle reçoit une copie, remise par `registry.replace`), `add_tag<T>(nom)`, `name()`, `registered()`, `edit()`, `add_engine_components()` (les composants de `world.hpp`).
- `inspector_filter_matches()` : le filtre de la liste (nom sans casse, ou numéro).
- `DebugTools` : `watch(world, nom)` / `forget(world)`, `watch(stack, nom)` / `forget(stack)`, `select(entité)`, `components()` ; `menu_items()` (à mettre dans le menu DEBUG du jeu), `draw()` (dans son `render()`), `between_frames()` (appelé par l'`Application` : rechargements et libérations demandés par les boutons) ; les fenêtres **Inspecteur d'entités**, **Assets**, **Entrées**, **Audio**, **États de jeu** ; les fenêtres ouvertes dans `imgui.ini` (section `[MoteurTools][Windows]`).

**À savoir** : l'inspecteur n'ajoute, ne retire et ne détruit rien (sauf la case « Cachée ») : un jeu garde des entités et compte sur leurs composants. Modifier un composant rend la simulation non déterministe ; la fenêtre le signale.

### `src/moteur/include/moteur/color.hpp` et `color.cpp`

**Rôle** : les conversions de couleur de la chaîne 3D (jalon 3, partie 6), en C++ pur et testées. Le shader `tonemap.frag.hlsl` fait les mêmes calculs sur le GPU ; ces versions servent de référence aux tests et à convertir des couleurs choisies à l'œil.

**Contenu** : `srgb_to_linear()` et `linear_to_srgb()` (fonctions de transfert sRGB standard, par composante ou sur un `vec3`, bornées à [0, 1]) ; `tonemap_pbr_neutral()` (le tone mapping Khronos PBR Neutral, implémentation de référence de Khronos).

### `src/moteur/tone_mapper.hpp` et `tone_mapper.cpp` (internes)

**Rôle** : la passe plein écran qui convertit l'image HDR linéaire de la scène 3D en couleurs d'écran (exposition, PBR Neutral, encodage sRGB), au début de la passe « compose ». Un triangle plein écran sans tampon de sommets, un échantillonneur linéaire qui agrandit aussi une résolution de rendu réduite. Hors de `include/` : seul le `Renderer` s'en sert. Le même fichier contient `Fxaa` (anticrénelage FXAA sur l'image déjà convertie, jalon 3, partie 12 bis) et `DepthView` (une carte d'ombre à l'écran).

### `shaders/fxaa.frag.hlsl`

**Rôle** : le filtre FXAA (variante « qualité » de FXAA 3.11, réécrite et commentée) : sur une image en couleurs d'écran, repère les bords à fort contraste, les suit jusqu'à leurs extrémités et mélange le pixel avec son voisin de l'autre côté, d'autant plus qu'il est près du bout de la marche d'escalier. Réutilise `tonemap.vert.hlsl` pour le triangle plein écran. MSL exporté.

### `shaders/tonemap.vert.hlsl` et `shaders/tonemap.frag.hlsl`

**Rôle** : le vertex shader fabrique le triangle plein écran à partir de `SV_VertexID` ; le fragment shader lit la scène HDR, applique l'exposition, le tone mapping et l'encodage sRGB (copie fidèle de `color.cpp`). Leur MSL est exporté dans `shaders/generated/msl/`.

### `src/moteur/include/moteur/mesh.hpp` et `mesh.cpp`

**Rôle** : les maillages 3D, sur le CPU puis sur le GPU (jalon 3, partie 4).

**Contenu**

- `Vertex3D` : position, normale, coordonnées de texture, tangente (48 octets). `compute_tangents()` remplit les tangentes par MikkTSpace (v inversé : bitangente vers le haut de l'image, convention des cartes de normales glTF) ; les primitives l'appellent. `Aabb` : boîte alignée sur les axes, vide au départ, `add(point)`, `center()`, `size()`.
- `MeshData` : sommets et indices 32 bits sur le CPU, `triangle_count()`, `bounds()`. Testable sans GPU.
- Primitives centrées sur l'origine : `make_cube(taille)` (une face = quatre sommets à elle, arêtes nettes), `make_plane(taille)` (au sol, face vers le haut), `make_sphere(rayon, segments, anneaux)` (sphère UV, résolution minimale imposée). Les triangles sont **antihoraires vus de l'extérieur** (convention glTF).
- `Mesh` : tampons GPU de sommets et d'indices, nombre d'indices, boîte englobante. `Mesh::create(renderer, données, nom)` envoie tout et attend le GPU (à faire au chargement, jamais pendant une frame).

**Conventions du monde** : main droite, Y vers le haut, 1 unité = 1 mètre (celles de glTF).

### `src/moteur/include/moteur/material.hpp`

**Rôle** : le matériau PBR du moteur, celui de glTF (métal / rugosité) : facteurs (couleur de base, métal, rugosité, force des normales et de l'occlusion, émissif, double face) et cinq textures facultatives (couleur et émissif en sRGB ; métal / rugosité, normales, occlusion en linéaire). Une texture absente compte comme blanche (plate pour les normales).

### `src/moteur/include/moteur/environment.hpp` et `environment.cpp`

**Rôle** : l'éclairage d'environnement (IBL, jalon 3 partie 7), en C++ pur et testé, sauf `Environment` qui envoie le résultat au GPU.

**Contenu**

- `EnvironmentImage` : radiance linéaire en projection équirectangulaire (rangée 0 = zénith), avec `sample(direction)` bilinéaire ; `direction_to_equirect()` / `equirect_to_direction()` (mêmes formules que le shader).
- `make_sky(largeur, hauteur, SkySettings)` : ciel procédural (zénith, horizon, sol), **sans soleil** (c'est la lumière directionnelle). `load_environment(chemin)` : un `.hdr` (Radiance), erreurs nommant le fichier.
- `IrradianceSH` / `project_irradiance()` : le diffus, en 9 coefficients d'harmoniques sphériques ; `evaluate(normale)` donne ce que réfléchit une surface blanche.
- `prefilter_specular(image, largeur, niveaux, échantillons)` : le spéculaire, une image par pas de rugosité, par échantillonnage d'importance GGX (suite de Hammersley), en lisant une chaîne de mipmaps de la source selon l'angle solide de chaque échantillon (moins de bruit).
- `Environment::create()` : préfiltre (256×128 à 8×4, 6 niveaux) et envoie une texture `R16G16B16A16_FLOAT` à mipmaps ; garde les harmoniques.

### `src/moteur/include/moteur/shadow.hpp` et `shadow.cpp`

**Rôle** : où regarde la carte d'ombre du soleil (jalon 3, partie 8), et la tenue des ombres des lumières ponctuelles (partie 8 bis). Logique pure, testée.

**Contenu** : `fit_sun_shadow(caméra, direction du soleil, ShadowSettings)` renvoie un `ShadowFrame` (vue-projection du soleil, centre, rayon, taille d'un texel en mètres). La zone couverte est le sol visible (rayons des coins de l'écran coupés par le sol et par `max_height`), englobée dans une sphère au rayon arrondi au mètre ; son centre est aligné sur les texels vus du soleil, pour que la carte glisse par texels entiers quand la caméra bouge (pas de scintillement des bords d'ombre).

**Lumières ponctuelles** :

- `point_shadow_faces(position, portée, taille de case)` : les vues-projections des six faces du cube (+X, −X, +Y, −Y, +Z, −Z), un peu plus larges que 90° (`kPointShadowMarginTexels` = 2 texels de marge), et la taille d'un texel par mètre de distance. `point_shadow_face(direction)` : la face d'une direction (sa plus grande composante), la même règle que le shader.
- `select_point_shadows(candidates, frustum, point focal, budget)` : les lumières dont la sphère touche la vue, les plus proches du point focal, au plus `budget`.
- `PointShadowSlots` : quelle ligne de l'atlas tient quelle ombre. `assign(signatures)` garde la ligne d'une signature déjà dessinée (rien à redessiner) et donne aux autres une ligne libre (`render = true`). `reset(n)` oublie tout.

### `shaders/point_shadow.vert.hlsl`, `point_shadow.frag.hlsl` et `shadow_clear.vert.hlsl`

**Rôle** : la passe des ombres des lumières ponctuelles. Le vertex shader est celui de l'ombre du soleil, avec la position monde en plus ; le fragment shader écrit **`distance / portée`** comme profondeur (`SV_Depth`, `[[depth(any)]]` en MSL), ce qui donne un biais en mètres. `shadow_clear.vert.hlsl` efface une case de l'atlas : un triangle à la profondeur 1 qui couvre le viewport, sans entrée de sommet (avec le `shadow.frag.hlsl` vide).

### `shaders/shadow.vert.hlsl` et `shaders/shadow.frag.hlsl`

**Rôle** : la passe d'ombre. Le vertex shader, instancié, reçoit la position du sommet (`TEXCOORD0`) et les trois lignes de la matrice monde de l'instance (`TEXCOORD1` à `3`), et projette le point vu du soleil ; le fragment shader est vide (seule la profondeur est écrite, par le matériel), mais SDL_GPU en exige un. **Les emplacements des entrées d'un shader doivent se suivre à partir de 0** : la compilation (HLSL, SPIR-V, puis DXIL ou MSL) les renumérote dans l'ordre, et des emplacements `0, 4, 5, 6` deviendraient `0, 1, 2, 3` (pipeline refusé par D3D12).

### `src/moteur/include/moteur/mesh_renderer.hpp` et `mesh_renderer.cpp`

**Rôle** : dessiner les maillages 3D (jalon 3, parties 4 à 9).

**Contenu**

- `set_camera(vue-projection, œil)` : la caméra de la frame et la position de l'œil (les reflets en dépendent), oubliées à la fin de la frame. Sans elles, rien n'est dessiné.
- `set_sun(direction vers le soleil, couleur, intensité)` et `set_environment(environnement, intensité)` : gardés d'une frame à l'autre. `add_light(PointLight)` : une lumière ponctuelle pour cette frame (jusqu'à `kMaxPointLights` = 32 ; au-delà, comptées dans `RenderStats::dropped_lights`).
- `set_shadows(ShadowOptions)` : ombres du soleil (activées, résolution, décalage normal, biais). Les matériaux `casts_shadow = false` n'y figurent pas.
- `set_culling(bool)` : le frustum culling, actif par défaut ; coupé, tout part au GPU (pour comparer : l'image ne doit pas changer).
- **Débogage** : `set_view(MeshView)` (éclairé, fil de fer, normales, couleur de base, distance ; hors fil de fer, la composition saute le tone mapping) et `set_debug(MeshDebug)` (boîtes des maillages dessinés, zone de l'ombre du soleil, portée des lumières, ajoutées aux lignes de debug après le culling).
- **Ombres des lumières ponctuelles** (`PointLight::casts_shadows`, réglages `point_*` de `ShadowOptions`) : `prepare_point_shadows()` choisit les lumières (`select_point_shadows`), crée l'atlas (6 cases de `point_resolution` par ligne, une ligne par lumière du budget ; une texture 1×1 le remplace tant qu'il n'existe pas), calcule la signature de chaque lumière (position, portée, et chaque objet dans sa sphère : maillage, matrice, faces), demande les lignes à `PointShadowSlots`, et regroupe les objets de chaque face à redessiner. `render_point_shadows()`, dans la passe « point shadows » : par face, viewport et scissor sur sa case, effacement, puis les lots. `invalidate_point_shadows()` oublie le cache.
- **Déroulement d'une frame** : le `Renderer` appelle `prepare()` avant toute passe de rendu : cadrage de la carte d'ombre (`prepare_shadows()`, avec `fit_sun_shadow()`), puis, pour chaque passe, `MeshBatcher::build()` (culling contre le frustum de la caméra, puis contre la boîte couverte par la carte d'ombre ; regroupement), et envoi des instances des deux passes, à la suite, dans un seul tampon (`mesh.instances`, agrandi en doublant). Ensuite `render_shadows()` dans la passe « shadow » et `render()` dans la passe « scene » : **un draw call instancié par lot**. Chaque lot relie sa tranche du tampon d'instances (décalage de la liaison) : l'indice d'instance part de 0 dans le shader, sur tous les backends.
- `draw(maillage, monde, Material)` : le cas général ; la boîte du maillage est transformée dans le monde à l'enregistrement (`transform_box`). `draw(maillage, monde, Material, boîte)` : la même chose avec une boîte déjà calculée, pour le décor qui ne bouge pas. **Rendu PBR** (Lambert + Cook-Torrance GGX, environnement, soleil, lumières ponctuelles, émissif), deux pipelines (simple face, double face : sans élimination, normale retournée derrière). Textures liées seulement quand elles changent ; données de la frame envoyées une fois.
- `draw(maillage, matrice monde, couleur, texture)` : raccourci pour une surface simple (matériau par défaut, non métallique, rugosité 0,5) ; le maillage et la texture doivent vivre jusqu'à la fin de la frame.
- `draw(modèle, matrice monde)` : toutes les pièces d'un `Model`, chacune avec sa transformation et son matériau.
- Exécution dans la passe « scene », en couleurs linéaires HDR (le constructeur reçoit les formats de couleur et de profondeur de cette passe) : pipeline avec test et écriture de profondeur (`LESS`), faces arrière éliminées, faces avant antihoraires, sans mélange. **Les couleurs données (draws, lumière) sont linéaires** : `srgb_to_linear()` convertit une couleur choisie à l'œil. L'échantillonneur est trilinéaire et anisotrope (×8). `has_work()` dit si la frame a de la 3D. Uniforms poussés une fois par passe : la vue-projection (vertex, `b0, space1`) et les données de la frame (fragment, `b0, space3` : soleil, environnement, lumières, ombre). Tout ce qui est propre à un objet (matrices, couleur, facteurs, émissif) passe par les **attributs d'instance** (`MeshInstance`). Les structures C++ ne contiennent que des `mat4` et des `vec4`, comme les `cbuffer` HLSL.
- Statistiques par passe dans `RenderStats` (voir `renderer.hpp`).

### `src/moteur/include/moteur/billboard_batcher.hpp` et `billboard_batcher.cpp`

**Rôle** : préparer les billboards d'une frame (jalon 3, partie 10), sans GPU, donc testable.

**Contenu** : `BillboardDesc` (texture, centre, taille en mètres, rectangle de texture, couleur linéaire pouvant dépasser 1, `additive`, orientation `Camera` ou `Upright`) ; `BillboardView` (œil, droite, haut, avant de la caméra) ; `BillboardVertex` (36 octets : position, uv, couleur pré-multipliée). `finish(vue)` trie du plus lointain au plus proche (tri stable), calcule les quatre coins (`corners()`), pré-multiplie la couleur (alpha à 0 pour un billboard additif) et découpe en `BillboardRun` par texture, 16 384 billboards au plus par lot.

### `src/moteur/include/moteur/billboard_renderer.hpp` et `billboard_renderer.cpp`

**Rôle** : les sprites dans le monde 3D (`renderer.billboards()`), dessinés dans la passe « scene » après les maillages.

**Contenu** : `set_camera(Camera3D)` à chaque frame (la caméra dessinée, interpolée), `draw(texture, centre, taille, BillboardOptions)` (couleur, `additive`, orientation, rectangle de texture). Pipeline : test de profondeur `LESS_OR_EQUAL` sans écriture, mélange pré-multiplié, sans élimination des faces ; échantillonneur linéaire avec mipmaps. Sommets envoyés avant les passes (tampon agrandi en doublant), un index de quad partagé (16 bits). Statistiques : `RenderStats::billboards`, `billboard_draw_calls`.

### `src/moteur/include/moteur/debug_lines.hpp` et `debug_lines.cpp`

**Rôle** : des lignes pour voir ce que fait le moteur (jalon 3, partie 12), avec `renderer.debug_lines()`.

**Contenu** : `DebugLineBuffer` (CPU, testé) : `line`, `box` (12 arêtes), `frustum` (d'une vue-projection à profondeur `[0, 1]`), `axes`, `circle`, `sphere` ; chaque ligne est testée contre la profondeur ou dessinée par-dessus (`on_top`). `DebugLineRenderer` : deux pipelines de lignes (avec et sans test de profondeur, sans écriture), à la fin de la passe « scene » ; caméra donnée par `set_camera()`, sinon celle des maillages ; `RenderStats::debug_lines`. Les shaders `debug_line.vert/.frag.hlsl` projettent et colorent.

### `shaders/depth_view.frag.hlsl`

**Rôle** : montrer une texture de profondeur (carte d'ombre) en gris, avec le triangle plein écran de `tonemap.vert.hlsl` limité à un rectangle (`DepthView`, interne, dans `tone_mapper.hpp`).

### `shaders/billboard.vert.hlsl` et `shaders/billboard.frag.hlsl`

**Rôle** : le vertex shader applique la vue-projection aux coins déjà placés ; le fragment shader multiplie la texture par la couleur pré-multipliée.

### `src/moteur/include/moteur/mesh_batcher.hpp` et `mesh_batcher.cpp`

**Rôle** : préparer ce qu'une passe 3D dessine (jalon 3, partie 9), sans GPU, donc testable (comme `SpriteBatcher`).

**Contenu**

- `MeshDraw` : un draw enregistré (maillage, matrice monde, matériau, boîte dans le monde).
- `MeshInstance` (144 octets, envoyé tel quel) : les trois premières lignes de la matrice monde, celles de sa transposée inverse (calculée sur la partie 3×3), la couleur de base, les facteurs (métal, rugosité, normal map, occlusion) et l'émissif. `make_instance()` la remplit.
- `MeshBatch` : un lot = un draw call instancié : maillage, matériau du premier draw (pour les textures et les faces), première instance, nombre.
- `MeshBatcher::build(draws, frustum, passe)` : garde les draws dont la boîte touche le frustum (et, pour `Pass::Shadow`, qui projettent une ombre), les regroupe par **(maillage, cinq textures, simple ou double face)** pour la passe principale, par **(maillage, faces)** pour l'ombre, et range les instances lot par lot. Ordre : lots simple face, puis double face (un seul changement de pipeline), sinon **dans l'ordre du premier draw enregistré, visible ou non** ; dans un lot, l'ordre d'enregistrement. L'image ne dépend donc ni des adresses mémoire, ni de ce qui est visible (sinon, là où deux objets se touchent à la même profondeur, des pixels pourraient changer quand la caméra bouge). Recherche du groupe : celui du draw précédent, sinon un parcours tant qu'il y a au plus 16 groupes, sinon une table de hachage.
- Compteurs : `submitted()`, `visible()`, `triangles()`.

### `src/moteur/include/moteur/aabb.hpp`

**Rôle** : la boîte englobante alignée sur les axes (`Aabb`), commune aux maillages, aux modèles et à la caméra (culling).

**Contenu** : `min`, `max` (vide tant qu'aucun point n'est ajouté), `add`, `center`, `size`, `corner(i)` ; `transform_box(boîte, matrice)` : la boîte autour des huit coins transformés (plus grande que l'objet en cas de rotation, jamais plus petite).

### `src/moteur/include/moteur/camera3d.hpp` et `camera3d.cpp`

**Rôle** : la caméra 3D d'un ARPG isométrique : elle regarde une cible sous des angles fixes (jalon 3, parties 3 et 4).

**Contenu**

- Cible, orientation (`yaw`, en degrés autour de l'axe vertical ; 0 regarde vers -Z) et inclinaison (`pitch`, sous l'horizon, bornée à [1, 89]).
- `Projection::Orthographic` ou `Projection::Perspective`, et un seul réglage de cadrage : `visible_height`, la hauteur de monde visible **à la cible**. En perspective, la distance de la caméra en découle avec le champ de vision (`field_of_view`, borné à [5, 120]) ; les deux projections cadrent donc la cible de la même façon. En orthographique, la caméra recule de 100 m (seuls les plans proche et lointain en dépendent).
- `position()`, `forward()`, `distance()`, `view()`, `projection_matrix()`, `view_projection()` ; profondeur `[0, 1]` (fonctions `RH_ZO` de GLM). Le plan proche de la perspective est repoussé à 5 % de la distance, pour la précision de la profondeur.
- `isometric_pitch()` : l'angle de la vraie isométrie, `atan(1/√2)`, environ 35,26°.
- Pixels : ceux de la fenêtre, origine en haut à gauche, y vers le bas (convertir la souris avec `Application::to_pixels()`).
- `screen_ray(pixel)` → `Ray` (origine, direction unitaire ; `hit_height(h)` : le point du plan `y = h`, s'il est devant) ; `ground_point(pixel, h)` : le point du sol sous un pixel ; `world_to_screen(point)` : l'inverse, rien derrière la caméra.
- `frustum()` → `Frustum` : six plans (gauche, droite, bas, haut, proche, lointain), normales vers l'intérieur ; `Frustum::from_view_projection(m)` pour toute matrice à profondeur `[0, 1]` ; `contains(point)`, `intersects(Aabb)` (jamais de faux négatif).
- Mouvement lissé, comme `Camera2D` : `begin_update()` au début de chaque pas fixe, `interpolated(alpha)` pour dessiner et piquer. `follow(but, dt, demi_vie)` rapproche la cible de façon exponentielle, indépendante de la fréquence des ticks.

### `src/moteur/include/moteur/model.hpp` et `model.cpp`

**Rôle** : charger les modèles glTF 2.0 (jalon 3, partie 5), avec la bibliothèque `cgltf`.

**Contenu**

- `ModelData` (CPU, testable sans GPU) : pièces (`ModelPart` : nom, `MeshData`, transformation dans le modèle, matériau), matériaux (`ModelMaterial` : couleur de base, index de la texture, métal, rugosité), images décodées (`ModelImage`). `bounds()`, `triangle_count()`.
- `load_gltf(chemin)` : un `.gltf` (avec ses fichiers à côté) ou un `.glb`. `parse_gltf(données, taille, nom, dossier)` : la même chose depuis la mémoire.
- Ce qui est lu : les maillages en triangles de la scène par défaut (positions ; normales, calculées si absentes ; premières coordonnées de texture ; indices, générés si absents), la hiérarchie des nœuds (**aplatie** : chaque pièce porte la matrice monde de son nœud), la couleur de base et sa texture (intégrée, en data URI ou en fichier, PNG ou JPEG), les facteurs métal et rugosité. Les fichiers externes passent par `SDL_LoadFile` (chemins UTF-8 sous Windows).
- Ce qui est refusé : un fichier qui exige une extension non prise en charge (compression Draco, meshopt...). Les primitives qui ne sont pas des triangles sont ignorées avec un message. Toute erreur nomme le fichier.
- `Model` (GPU) : un `Mesh` par pièce, les textures (partagées : `std::shared_ptr<Texture>`), les matériaux qui les désignent, la boîte englobante, le nombre de triangles et la mémoire GPU. `Model::create(renderer, données, nom, source de textures)` et `Model::load(renderer, chemin)`. La **source de textures** (facultative) fournit les images en fichiers séparés : le gestionnaire d'assets y branche son cache, et `GltfOptions::decode_external_images = false` évite alors de les décoder deux fois. `ModelImage::file` et `ModelData::files` donnent les fichiers lus (pour le rechargement à chaud).
- `Model::replace_in_place(fresh)` : prend le contenu d'un rechargement en gardant chaque `Mesh` et chaque texture propre au modèle à la même adresse (les scènes gardent des pointeurs vers eux) ; refuse, sans rien changer, un modèle de structure différente.
- **À savoir** : `CGLTF_IMPLEMENTATION` est défini dans ce fichier, et seulement là.

### `tools/models/make_reference_model.py` et `assets/models/reference.glb`

**Rôle** : le **modèle de référence**, qui vérifie toute la chaîne glTF d'un coup d'œil. Script Python sans dépendance, sortie déterministe : `python tools/models/make_reference_model.py` réécrit `assets/models/reference.glb` (8 Ko).

**Contenu du modèle** : un cube de 1 m posé au sol (pour l'échelle), une texture en quatre quadrants sur chaque face (rouge en haut à gauche de l'image, vert en haut à droite, bleu en bas à gauche, blanc en bas à droite : une texture retournée se voit), une flèche rouge vers +X, un repère vert vers +Y et un bleu vers +Z (les couleurs habituelles des axes). La flèche et les repères sont des nœuds **enfants** du cube, placés dans son repère : une hiérarchie ignorée ou mal composée les déplace.

### `tools/models/fetch_test_models.py`, `assets/models/polyhaven/` et `assets/environments/`

**Rôle** : télécharger les modèles 3D de test (Poly Haven, CC0, environ 11 Mo) et l'environnement de test (`studio_small_09_1k.hdr`, 1,6 Mo, HDR équirectangulaire 1K, CC0 : un studio à l'éclairage neutre, pour comparer les matériaux avec Blender). Le bac à sable charge tous les `.hdr` de `assets/environments/`, qui est aussi **hors de Git**. `python tools/models/fetch_test_models.py` passe par l'API officielle de Poly Haven (avec un User-Agent, qu'elle exige), prend la version glTF 1K de chaque modèle avec les fichiers qu'elle référence (`.bin`, textures JPEG), vérifie leurs tailles et ne retélécharge pas ce qui est déjà là. Le dossier `assets/models/polyhaven/` est **hors de Git** (`.gitignore`). Garder la liste du script et `assets/credits.json` en accord.

### `assets/credits.json`

**Rôle** : les crédits affichés par la fenêtre « À propos » du bac à sable. Quatre listes : `libraries` (nom, version, licence, copyright, site, fichier de licence), `fonts` (même chose), `models` et `environments` (nom, auteurs, licence, source, site, fichier). Une entrée avec `"platform": "windows"` (ou `"macos"`) n'est affichée que sur cet OS. Les chemins sont relatifs au dossier de l'exécutable. **Chaque bibliothèque ou asset ajouté au projet doit y être ajouté.**

### `shaders/mesh.vert.hlsl` et `shaders/mesh.frag.hlsl`

**Rôle** : les shaders des maillages 3D. Le vertex shader, instancié, reçoit les attributs du sommet (`TEXCOORD0` à `3`) et ceux de l'instance (`TEXCOORD4` à `12` : matrices, couleur, facteurs, émissif) ; il passe la position, la normale et la tangente en espace monde, et le matériau sans interpolation (`nointerpolation`). Le fragment shader fait le **rendu PBR** (voir `mesh_renderer`) : cinq textures de matériau, l'environnement, la carte d'ombre du soleil et l'atlas des ombres ponctuelles (`t0` à `t7`, `space2` ; les deux ombres avec un échantillonneur à comparaison), uniforms de la frame (`b0`, `space3` : entre autres les 48 matrices des faces et, dans `light_color[i].w`, la ligne de l'atlas de chaque lumière, ou −1). Ombres en PCF 3×3 avec décalage le long de la normale. Leur MSL est exporté dans `shaders/generated/msl/` pour le Mac.

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

### `cmake/Assets.cmake` (mise à jour : licences)

`moteur_add_licenses(<cible> PACKAGES sdl3 imgui ...)` copie, à chaque build, le texte de licence que vcpkg installe avec chaque paquet (`share/<paquet>/copyright`) dans `licenses/<paquet>.txt` à côté de l'exécutable. Un paquet sans fichier de licence arrête la configuration. Les licences MIT et zlib exigent que leur notice accompagne le programme.

### `cmake/Atlas.cmake`

**Rôle** : lancer l'outil d'empaquetage au build, seulement quand c'est utile.

**Contenu** : la fonction `moteur_add_atlas(cible NAME nom SOURCES dossier)`. Elle liste les PNG et le `pivots.json` du dossier (avec `CONFIGURE_DEPENDS`, donc les ajouts et retraits sont vus sans reconfigurer), ajoute une commande dont le résultat est `<nom>.json` dans le dossier `assets/` de l'exécutable, et fait dépendre la cible de cette commande.

**Un piège traité** : quand un fichier est **retiré**, il ne reste aucune dépendance plus récente que le résultat, donc l'empaquetage ne se relançait pas et l'atlas gardait l'image supprimée. La commande dépend donc aussi d'un petit fichier qui **liste les entrées** et n'est réécrit que si cette liste change.

**À savoir** : contrairement aux shaders, l'outil est notre propre programme. Il se compile et se lance de la même façon sur Windows et sur Mac, sans fichiers pré-générés.

### `art/` (sources d'art)

**Rôle** : les images que l'on dessine, **distinctes des ressources produites** : elles ne sont jamais chargées directement, elles passent par l'outil d'empaquetage.

**Contenu** : `art/world/` (les trois losanges du sol isométrique, 64×32, et `pivots.json` qui place leur pivot au sommet, en (32, 0)) et `art/test/` (19 images de test : quatre orbes rognables, des barres de tailles différentes, un carré, un point d'un pixel, un anneau à bords doux et **huit images de marche 48×64 asymétriques**, avec une ombre semi-transparente). L'asymétrie des personnages (bras gauche long et vert, bras droit court et rouge) sert à repérer un retournement fautif.

### `src/moteur/include/moteur/asset_cache.hpp` et `asset_cache.cpp`

**Rôle** : le cache d'un type d'asset, sans GPU (jalon 4, partie 2), testable avec de faux chargeurs.

**Contenu**

- `Asset<T>` : la poignée, `entt::resource<T>` (un `std::shared_ptr`). Se copie, se garde dans un composant ; `->` et `*` donnent l'asset. `make_asset(objet)` en fait une pour un objet créé par le jeu (maillage généré, texture dessinée), hors de tout cache.
- `normalize_asset_path(chemin)` : la clé d'un fichier (`/` partout, sans `.`, `..` résolus, casse gardée) ; refuse un chemin vide, absolu ou qui sort de `assets/`.
- `check_asset_case(racine, clé)` : refuse une casse différente de celle du disque, en nommant l'élément fautif ; laisse passer un fichier absent (le chargeur le signale).
- `AssetCache<T>(nom du type, remplacement, mesure, remplacement en place)` : `get(clé, chargement)` charge une seule fois (le chargement reçoit la liste des fichiers à remplir, le principal d'abord) ; `reload(clé)` recharge **en place** (même objet, nouveau contenu ; l'ancien reste en cas d'échec) ; `collect_garbage()` libère ce qu'aucune poignée ne tient ; `keys_using(fichier)`, `infos()` (clé, mémoire, utilisateurs, erreur), `error(clé)`, `loads()`, `failures()`. Stockage : `entt::resource_cache` ; deux clés de même hachage lèvent une erreur.

### `src/moteur/include/moteur/assets.hpp` et `assets.cpp`

**Rôle** : le gestionnaire d'assets du moteur (`app.assets()`) : tout fichier chargé par le jeu passe par lui.

**Contenu**

- `texture(chemin, TextureSettings)`, `model(chemin)`, `environment(chemin)` (`.hdr`), `font(chemin, taille)`, `atlas(chemin du .json)`, `animations(chemin)` : un `Asset<T>`, partagé par tous ceux qui demandent la même clé. Les chemins sont relatifs à `assets/`.
- Remplacements visibles quand un fichier manque ou est invalide (damier magenta, cube magenta, ciel procédural), avec un log qui nomme le fichier ; `model_error(chemin)` en donne la raison. Police, atlas et animations lèvent une exception.
- `exists(chemin)`, `file_path(chemin)` : pour les fichiers de test facultatifs.
- `collect_garbage()` : à appeler entre deux scènes.
- `enable_hot_reload(dossier des sources)` et `update()` : surveillance par **efsw** (fil à part), fichiers pris après 200 ms de calme, copiés à côté de l'exécutable puis rechargés en place ; un modèle passe par `Model::replace_in_place`.
- `stats()` et `infos()` : par type, nombre, mémoire GPU, chargements, échecs ; la fenêtre DEBUG > Assets du bac à sable les affiche.

**À savoir** : le chargement est synchrone et attend le GPU ; jamais au milieu d'une frame.

### `src/moteur/include/moteur/ktx_texture.hpp` et `ktx_texture.cpp`

**Rôle** : lire les textures KTX2 (jalon 4, partie 3), avec libktx (KTX-Software).

**Contenu**

- `CompressedImage` : format SDL, taille, tous les niveaux (mipmaps) prêts pour le GPU, et `transcoded`.
- `is_ktx2(octets)` : reconnaît l'identifiant d'un fichier KTX2.
- `decode_ktx2(octets, nom, TextureSettings, CompressedFormats)` : Basis Universal transcodé en BC5 (deux canaux : normales), BC7 (le reste) ou RGBA8 (GPU sans ces formats, ou taille non multiple de 4) ; BC7, BC5 et RGBA8 pris tels quels. La variante sRGB suit `settings.srgb`. Erreurs avec le nom du fichier.
- `Renderer::create_texture(const CompressedImage&)` les envoie ; `Renderer::compressed_formats()` et `set_block_compression()` disent et limitent ce que le GPU lit.

### `src/moteur/include/moteur/input.hpp` et `input.cpp`

**Rôle** : les entrées du joueur sous forme d'**actions** (jalon 4, partie 4) : `app.input()`.

**Contenu**

- `InputSource` et `parse_input_source` / `input_source_text` / `input_source_label` : une touche (par position), un bouton de souris, un cran de molette, un bouton ou un axe de manette ; forme texte du fichier (`key:Q`, `mouse:left`, `wheel:up`, `pad:a`, `pad:righttrigger+`) et nom affiché (la touche du clavier du joueur, « Clic droit », « Manette RB »).
- `Input` : `add_button`, `add_axis`, `clear_actions` ; `load_bindings` (défauts, plusieurs profils) et `apply_user_bindings` (fichier du joueur), `user_bindings_json`, `set_profile`, `describe` ; `process_event`, `set_pointer`, `end_tick`, `release_all`, `set_enabled` (appelés par `Application`) ; `down`, `pressed`, `released`, `presses`, `axis`, `pointer`, `last_device`.
- `InputRecording`, `input_recording_to_json` / `from_json`, `Input::frame` / `set_frame` : enregistrement et rejeu tick par tick, actions par nom.
- `Application` : ouvre le sous-système manette, transmet les événements (les relâchements même quand ImGui garde l'événement), appelle `end_tick()` après chaque `update()`, enregistre ou rejoue (`ApplicationConfig::record_input_path`, `replay_input_path`), donne `preferences_directory()`.

### `src/moteur/include/moteur/world.hpp` et `world.cpp`

**Rôle** : le monde d'une scène en **entités** EnTT (jalon 4, partie 5). Une `World` par scène : elle tient le registre (`registry()`), où le jeu ajoute aussi ses propres composants.

**Contenu**

- **Composants du moteur** (des valeurs et des poignées) : `Transform` (position, rotation en quaternion, échelle ; `matrix()`), `PreviousTransform` (l'entité bouge : son `Transform` au tick précédent), `Parent` (attachée à une autre entité, son `Transform` est alors relatif), `Name`, `Hidden`, `MeshComponent` (poignée de maillage et matériau), `ModelComponent` (poignée de modèle), `LightSource` (lumière ponctuelle), `Billboard` (poignée de texture, taille, options).
- `interpolate(Transform, Transform, t)` : exacte quand rien n'a bougé.
- **Systèmes** : `begin_tick()` (premier système d'un pas fixe : `PreviousTransform` ← `Transform`) ; `collect(WorldSink&, alpha, CollectOptions)` et `submit(Renderer&, alpha, options)` : maillages, modèles, lumières (les `max_lights` plus proches de `light_focus`) et billboards, interpolés, dans l'ordre de création ; `world_matrix` / `world_position` (à travers les parents) ; `destroy` (avec les entités attachées) ; `entity_count`, `cached`.
- **Cache des entités fixes** : une entité sans `PreviousTransform` ni `Parent` garde sa matrice et ses boîtes (celles de chaque partie d'un modèle comprises). Les signaux d'EnTT l'effacent quand son `Transform` (par `patch` / `replace`), son `Parent`, son maillage ou `Hidden` changent.

### `src/moteur/include/moteur/state_stack.hpp` et `state_stack.cpp`

**Rôle** : les écrans du jeu (titre, chargement, jeu, pause) en **pile d'états** (jalon 4, partie 6).

**Contenu**

- `GameState` : `enter` / `exit` (posé sur la pile, retiré), `covered` / `uncovered` (un état posé par-dessus, retiré), `on_event`, `update`, `render` ; `transparent()` (l'état du dessous est dessiné d'abord) et `blocking()` (celui du dessous ne tourne plus) ; `name()` pour le debug ; `stack()`.
- `StateStack`, qui est un `moteur::Game` : `push`, `pop`, `replace`, `reset` sont des **demandes**, appliquées au début du `update()` suivant (entre deux ticks), jamais pendant un `update` ou un `render`. Le remplaçant est construit avant le départ du remplacé (assets partagés gardés). Mise à jour de bas en haut depuis le premier état bloquant, dessin depuis le premier opaque ; un état figé garde l'`alpha` de sa dernière image. Les événements vont à l'état du dessus ; les états en dessous lisent une `Input` muette (`Input::set_muted`). Construite avec l'`Application`, elle libère les assets inutilisés après chaque lot de transitions et relance l'horloge (`Application::restart_clock`). `visit_drawn` pour les tests.
- `LoadingState` : une liste d'étapes (`label`, fonction), **une par tick**, horloge relancée après chacune, puis la dernière construit l'état qui le remplace ; `progress()`, `label()`, `error()` ; `draw` et `failed` à redéfinir (par défaut : écran vide, et retrait de l'état).

### `src/moteur/include/moteur/audio.hpp` et `audio.cpp`

**Rôle** : le son du jeu (jalon 4, partie 7), avec miniaudio : `app.audio()`.

**Contenu**

- `SoundGroup` (musique, effets, ambiance, interface) ; `PlaySound` (groupe, volume, hauteur, variations, position, priorité, boucle, fondu) ; `SoundId`.
- `Audio` : `play` (un son ou une variante au hasard, mis en file), `play_stream` (ambiance en flux), `stop`, `set_position`, `set_volume`, `playing`, `stop_all` ; `play_music` / `stop_music` (fondu enchaîné), `set_music_volume` ; volumes (curseurs, gain au carré) et pauses par groupe, volume général, muet sans le focus, réglages en JSON (`settings_json`, `apply_settings_json`) ; `set_listener` (ou d'après une `Camera3D`), `attenuation`, `limits` ; `update()` (une fois par frame : vide la file, déplace les voix placées, libère les voix finies, surveille le périphérique) ; `mix()` sans périphérique (tests) ; `stats()` (voix, compteurs, crêtes, limiteur, périphérique, tampon).
- Un limiteur en fin de mixage (plafond 0,9). Les voix gardent les échantillons qu'elles jouent (rechargement à chaud sans danger).
- `Application` : crée l'`Audio` (`ApplicationConfig::audio`), l'appelle à chaque frame, lui passe le focus de la fenêtre, lit et écrit `audio.json` dans le dossier des préférences.

### `src/moteur/include/moteur/audio_rules.hpp` et `audio_rules.cpp`

**Rôle** : les règles de l'audio qui ne demandent aucun périphérique, testées à part.

**Contenu** : `Listener`, `Attenuation`, `place_sound()` (gain et panoramique) ; `VoiceLimits`, `VoiceInfo`, `decide_voice()` (jouer, prendre la place d'une voix, ou ne pas jouer, et pourquoi) ; `merge_requests()` ; `slider_gain()`.

### `src/moteur/include/moteur/sound.hpp` et `sound.cpp`

**Rôle** : les sons comme assets (`assets.sound()`, `assets.music()`).

**Contenu** : `Sound` (échantillons flottants partagés, canaux, fréquence), `Music` (octets compressés partagés, décodés pendant la lecture) ; `decode_sound()`, `open_music()` (erreurs avec le nom du fichier) ; `placeholder_sound()` (le bip d'un son manquant) ; `encode_wav()` (tests).

### `src/moteur/miniaudio.c`

**Rôle** : l'implémentation de miniaudio, avec `stb_vorbis` pour l'OGG (le montage que miniaudio documente), compilée en C dans la bibliothèque `moteur_miniaudio`, sans les avertissements du projet (code tiers).

### `tools/audio/fetch_test_sounds.py`

**Rôle** : télécharger les sons et musiques de test dans `assets/audio/` (10 Mo, hors de Git, tous en CC0 : Kenney et OpenGameArt), en vérifiant le SHA-256 de chaque fichier ; ne garde que les fichiers utilisés des paquets de Kenney, avec leur `License.txt`. Les crédits sont dans `assets/credits.json` (section `sounds`).

### `src/moteur/include/moteur/process_memory.hpp` et `process_memory.cpp`

**Rôle** : `process_memory_bytes()`, la mémoire que le processus s'est réservée (Windows : *private bytes* ; macOS : *physical footprint* ; Linux : mémoire résidente). Pour comparer deux moments d'une même exécution (fuites), pas deux systèmes.

### `assets/input/demo3d.json`

**Rôle** : les touches par défaut de la démo 3D, en deux profils : « clic » (clic gauche pour se déplacer, A Z E R T et clic droit pour les compétences) et « zqsd » (Z Q S D pour se déplacer, A E R F et clic droit), plus la manette. Les touches y sont nommées par leur position sur un QWERTY US (`key:Q` = la touche A d'un AZERTY).

### `tools/input/make_slice_replay.py` et `tests/data/slice_replay.json`

**Rôle** : le rejeu de référence de la tranche jouable (jalon 4, partie 9), écrit par un script plutôt qu'enregistré à la main : 480 ticks (titre, « Jouer », marche, coups, pause, « Reprendre »), actions par leur nom. `python tools/input/make_slice_replay.py` le réécrit. Capture : `bac_a_sable --states --replay-input tests/data/slice_replay.json --freeze-after 340 --run-seconds 9 --pixel-size 1280 720 --capture slice.png`.

### `tools/textures/convert_gltf_textures.py`

**Rôle** : encoder les textures des glTF en KTX2 UASTC et les déclarer avec `KHR_texture_basisu`.

**Usage** : `python tools/textures/convert_gltf_textures.py [fichiers .gltf ou dossiers] [--ktx outil] [--force]` (par défaut, tout `assets/models/`). Rôle de chaque image déduit des matériaux (couleur en sRGB, normales en `--normal-mode`, données en linéaire), `.ktx2` à côté de l'image, image d'origine gardée en repli. L'outil `ktx` est cherché dans les dossiers de build (vcpkg l'installe), puis dans le PATH. Idempotent.

### `src/moteur/include/moteur/paths.hpp` et `paths.cpp`

**Rôle** : savoir où chercher les fichiers du programme.

**Contenu** : `base_path()` renvoie le dossier de l'exécutable (avec un séparateur final), `asset_path("nom.png")` renvoie `<dossier de l'exécutable>/assets/nom.png`. Le chargement des shaders utilise aussi `base_path()`. `read_file(chemin)` lit tout un fichier avec `SDL_LoadFile` (chemins UTF-8 sous Windows) dans un `FileData`, qui libère les octets lui-même (`release()` les cède, pour cgltf). **Tous** les chargements d'assets passent par elle (images, environnements, polices, glTF et leurs tampons, atlas, animations), pour qu'une archive puisse remplacer le dossier au packaging. `read_text_file(chemin)` la même chose en chaîne.

**Pourquoi relatif à l'exécutable** : le programme fonctionne quel que soit le dossier de travail. Sur Mac, dans un bundle `.app`, l'emplacement des ressources sera différent : ce sera à adapter dans `base_path()` au moment du packaging, sans toucher au reste du code.

### `src/moteur/include/moteur/image.hpp` et `image.cpp`

**Rôle** : lire un fichier PNG en mémoire.

**Contenu**

- `Image` : largeur, hauteur et pixels en **RGBA 8 bits**, alpha non pré-multiplié, lignes stockées de haut en bas.
- `decode_image(données, taille, nom)` : décode une image déjà en mémoire (par exemple une texture intégrée à un `.glb`).
- `load_image(chemin)` : lit le fichier avec `SDL_LoadFile` (qui gère les chemins UTF-8 sous Windows, contrairement au `fopen` de stb), le décode avec stb_image et renvoie une `Image`. Lève une exception si le fichier manque ou n'est pas un PNG valide.
- Seuls le PNG et le JPEG sont activés (`STBI_ONLY_PNG`, `STBI_ONLY_JPEG`), ce qui allège le code compilé. Le JPEG sert aux textures de nombreux modèles glTF.
- `premultiply_alpha(image)` : multiplie la couleur de chaque pixel par son alpha, en arrondissant au plus proche. Les pixels opaques sont laissés tels quels, et les pixels transparents deviennent noirs. C'est la forme que le GPU mélange et filtre correctement : avec un alpha normal, filtrer un pixel voisin d'un pixel transparent y traîne la couleur (sans signification) de ce dernier et laisse un halo. Le renderer l'applique à la création d'une texture ; les fichiers sur disque restent en alpha normal.

**À savoir** : c'est dans ce fichier que l'implémentation de stb est compilée (`STB_IMAGE_IMPLEMENTATION`). Il ne faut la définir qu'à un seul endroit.

### `apps/bac_a_sable/blender_compare.hpp` et `blender_compare.cpp`

**Rôle** : la scène comparée avec Blender (jalon 3, partie 7), dans le menu (« Comparaison avec Blender ») ou avec `--blender-compare`. Les quatre modèles Poly Haven et deux sphères (plastique blanc mat, or poli) en rang, posés sur `y = 0`, éclairés **par l'environnement de test seul** (ni soleil, ni lumière ponctuelle, ni sol), vus par une caméra fixe (de face, 15° au-dessus de l'horizon, champ de 30°), sur un fond gris neutre. Avec `--capture image.png`, écrit la 10ᵉ image et `image.png.json` : caméra, environnement, tone mapping, place de chaque objet, matériaux des sphères, dans les conventions du moteur.

### `tools/blender/compare_render.py` et `compare_images.py`

**Rôle** : la comparaison avec Blender.

1. `bac_a_sable --blender-compare --pixel-size 1280 720 --aa msaa4 --run-seconds 2 --capture engine.png`
2. `blender -b --factory-startup --python tools/blender/compare_render.py -- engine.png.json blender.png --gpu` (Blender 4.2 ou plus, pour la vue « Khronos PBR Neutral » ; version de référence : 5.2.2, `C:\Program Files\Blender Foundation\Blender 5.2\blender.exe`, celle du MCP Blender ; Cycles, sur le GPU avec `--gpu`, ou EEVEE avec `--eevee`) : la même scène d'après le JSON (mêmes glTF aux mêmes places, mêmes sphères, environnement seul avec la même intensité, même caméra, même tone mapping), fond transparent. Les positions passent de Y vers le haut à Z vers le haut comme à l'import glTF ; l'environnement n'a pas besoin d'être tourné (même lecture équirectangulaire).
3. `python tools/blender/compare_images.py engine.png blender.png engine.png.json comparaison` (Pillow) : image côte à côte, différence ×4, et la couleur moyenne de chaque objet dans les deux images.

Différences attendues : Blender trace la lumière (les objets s'ombrent eux-mêmes), le moteur approche l'éclairage d'environnement (harmoniques sphériques, image préfiltrée).

### `apps/bac_a_sable/demo3d.hpp` et `demo3d.cpp`

**Rôle** : la scène de démonstration du jalon 3 (partie 11), dans le menu (« Démo 3D ») ou avec `--demo3d`.

**Contenu** : la carte 100×100 de la démo 2D construite en 3D (sol fusionné par blocs de 10×10, murs de 1,5 m, un brasero-torche par pièce), le décor (rochers, arbres, caisses, tonneaux glTF), les créatures de la démo 2D (demi-tour devant les murs) avec barres de vie, le soleil et les torches avec ombres, la caméra (déplacement, zoom, suivi). Commandes : les actions de `assets/input/demo3d.json` (profils « clic » et « zqsd », manette). Depuis la partie 5 du jalon 4, le monde est une `moteur::World` : sol, murs et décor sont des entités fixes, chaque créature une entité qui bouge (`Walker` et `Health`, composants propres à la démo) avec un corps et une tête attachés, chaque torche une entité (flamme, lumière, halo), l'anneau de sélection est attaché à la créature choisie, et la marque de destination est cachée (`Hidden`) quand il n'y en a pas. Réglages, tableau des passes et temps de collecte du monde dans le panneau du menu (`draw_controls()`) ; `world collection` en fin de ligne de commande. Options : `--seed`, `--map`, `--creatures`, `--decor`, `--tile-floor`, `--point-shadows`, et celles des captures. `Demo3D::declare_actions()` déclare les actions (et celles des menus) et lit les touches, pour la démo et pour les états qui l'entourent. `Options::hero` (la tranche jouable, jalon 4 partie 9) : un héros à lui, suivi par la caméra, qui frappe les créatures (clic sur une créature à portée, ou compétence 1), avec pas, impacts et ambiance.

### `apps/bac_a_sable/states_demo.hpp` et `states_demo.cpp`

**Rôle** : la tranche jouable (jalon 4, partie 9), née du test des états de jeu (partie 6), dans le menu (« Tranche jouable (états de jeu) ») ou avec `--states`.

**Contenu** : une `StatesDemo` tient une `moteur::StateStack` : écran titre (« Jouer », « Quitter le test », musique du titre), chargement (police et tonneau préchargés, puis construction de la démo 3D, barre de progression ImGui), jeu (la démo 3D ; l'action `pause`, Échap ou Start, pose la pause), pause (transparente et bloquante : le monde reste dessiné, assombri et figé ; « Reprendre », « Retour au titre »). `--states-cycles N` (ou le réglage « Cycles automatiques ») : un pilote automatique enchaîne titre, jeu, pause, jeu, pause, titre N fois ; à chaque retour au titre il note les assets, les objets GPU et la mémoire du processus, et vérifie que le monde n'a pas bougé pendant les pauses ; bilan en fin de ligne de commande. `--capture` : une image de la première pause ; avec `--freeze-after N`, celle du jeu gelé après N de ses ticks (la capture de référence, avec `--replay-input tests/data/slice_replay.json`). Depuis la partie 9 : la démo en mode héros, des menus lus par les actions `menu_up`, `menu_down`, `menu_confirm` (classe `Menu` : le choix en surbrillance, que la souris ne déplace pas), une étape de chargement « Sons », les sons d'interface, `--run-seconds`, et la mesure du chargement (`Slice: loaded in ... ms`).

### `apps/bac_a_sable/audio_test.hpp` et `audio_test.cpp`

**Rôle** : le test de l'audio (jalon 4, partie 7), dans le menu (« Audio ») ou avec `--audio` (`--audio-burst` : une rafale au départ ; avec `--run-seconds`, un bilan en fin de ligne de commande).

**Contenu** : une carte vue de dessus (l'auditeur, les distances d'atténuation, les sources ; un clic y joue un impact), un feu en boucle qui tourne autour de l'auditeur, pas, impacts, clics d'interface, musiques du titre et du jeu, ambiance en flux, volumes et pauses par groupe, rafale de 200 impacts en une seconde ; le panneau montre le périphérique, son tampon, les voix, les compteurs, les crêtes et le limiteur.

### `apps/bac_a_sable/sandbox_scene.hpp`

**Rôle** : ce que les scènes du menu partagent. `SandboxScene` (un `moteur::Game` avec `draw_controls()`, `stop_requested()` et `uses_escape()`, qui laisse Échap à la scène comme touche de pause) : le menu tient la scène en cours par cette interface. `Random` : le petit générateur à graine des scènes. `demo_wall(i, j)` : les murs des deux démos (2D et 3D), pour qu'elles aient la même carte.

### `apps/bac_a_sable/main.cpp`

**Rôle** : programme d'essai qui utilise le moteur.

**Contenu**

- **Scène « Rendu 3D : premiers maillages »** (`--3d`, `--ortho`, `--camera X Z`, `--view-height H`, ou le menu). Elle charge aussi tous les modèles glTF de `assets/models/` et les pose en rang, avec leur nom, leurs triangles et leur temps de chargement au-dessus (ou leur erreur). Contenu d'origine : un sol en damier de 20×20 cases d'1 m, un mur de cubes, un cube qui tourne, une sphère, une boîte à la taille d'un personnage et quatre piliers de 3 m, vus par une `Camera3D`. **P** change de projection, flèches ou ZQSD déplacent la cible, la molette change la hauteur visible. La case sous la souris est surlignée ; un clic gauche envoie la boîte « personnage » vers le point cliqué ; **F** fait suivre le personnage par la caméra ; `--mouse X Y` simule la souris et écrit la case survolée à la fermeture. Le texte (en `screen_sprites()`) rappelle la projection, ses réglages et les statistiques 3D. Dans le menu, le panneau du test en cours (`TestScene::draw_controls()`, en ImGui) règle la projection, les angles, la hauteur visible et le champ de vision.
- Classe `TestScene`, qui implémente `Game` : **une** scène de test, choisie par ses `Options` (celles de la ligne de commande). Elle compte les ticks et les frames. Lancée depuis la ligne de commande (`standalone`), Échap et `--run-seconds` quittent le programme ; lancée depuis le menu, ils demandent seulement l'arrêt du test (`stop_requested()`).
- **Fenêtre « À propos »** (menu **Aide**, ou bouton de l'accueil) : les bibliothèques, la police et les modèles de test, lus dans `assets/credits.json` (structure `Credits`), avec le texte complet de chaque licence à la demande (lu dans `licenses/` ou `assets/fonts/`, avec retour à la ligne) et la présence de chaque modèle. Le bac à sable lie `nlohmann-json` pour lire ce fichier.
- Classe `Sandbox`, qui implémente `Game` : le programme lancé **sans argument** (ou avec `--menu` ; `--menu-test N` lance en plus le test N, compté depuis 0). Barre de menus ImGui (**DEBUG > Tests moteur** en sous-menu, avec « Toutes les scènes... » puis chaque scène ; les outils de debug du moteur, `DebugTools::menu_items()` ; **DEBUG > Accueil**), écran d'accueil, page de sélection (description, réglages et bouton **Lancer** de chaque scène) et panneau du test en cours (**Arrêter le test**, **Accueil**). Échap remonte d'un cran (test → sélection → accueil). Une scène est créée et détruite dans `update()`, jamais pendant une frame : le chargement attend le GPU, et les textures d'une scène sont utilisées par la frame en cours d'enregistrement. Si une scène ne peut pas démarrer, l'erreur s'affiche sur la page de sélection.
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
- `test_input.cpp` : sources lues et réécrites, appui bref vu par un seul tick, appui maintenu, appuis accumulés sans tick, action à deux sources, répétition du système ignorée, crans de molette, diagonales unitaires, zone morte et sens du stick, gâchette comme bouton, profils (et changement qui relâche), fichier du joueur (profil, liaison changée, relu après écriture, profil inconnu ignoré), fichiers invalides nommés, entrées coupées, perte du focus, rejeu par nom d'action et au-delà de sa fin.
- `test_ktx_texture.cpp` : KTX2 fabriqués en mémoire avec libktx : identifiant, UASTC couleur en BC7 (sRGB ou non) avec ses quatre niveaux et leurs tailles de blocs, repli RGBA8 à 12 niveaux au plus de la source, taille non multiple de 4 en RGBA8, carte de normales à deux canaux en BC5 ou avec x et y en rouge et vert, RGBA8 brut pris tel quel, fichiers invalides ou tronqués nommés dans l'erreur.
- `test_asset_cache.cpp` : normalisation des chemins (séparateurs, `.`, `..`, refus des chemins absolus ou qui sortent), casse vérifiée sur un vrai dossier temporaire, un chargement par clé, libération de ce que personne ne tient (et rechargement ensuite), remplacement en cas d'échec ou exception sans remplacement, rechargement en place (même objet) et échec qui garde l'ancien contenu, placeholder réparé par un rechargement, fonction de remplacement qui refuse, fichiers d'un asset, mesure de la mémoire.
- `test_animation.cpp` : clips invalides, durée d'un cycle, image affichée à chaque tick (boucle, une fois, aller-retour), vitesses ×2, ×0,5 et 0, vitesse négative refusée, événements exactement une fois (y compris avec un pas de 20 ticks et une vitesse de ×0,7 sur 80 ticks), clip `Once` après la fin, `set_time`, lecture du JSON et messages d'erreur.
- `test_mesh_batcher.cpp` : un lot par maillage et textures quelles que soient les couleurs, lots à une face avant ceux à deux faces, objets hors du frustum écartés (boîte tournée comprise), ordre des lots indépendant de ce qui est visible, passe d'ombre limitée aux objets qui en projettent et sans textures, contenu des instances (lignes de la matrice, matériau), nombreux groupes, sous-ensemble des draws.
- `test_billboard_batcher.cpp` : billboard face à la caméra (en travers de la vue, à la bonne taille), billboard vertical (arêtes verticales, tourne autour de la verticale), tri du plus lointain au plus proche et lots coupés par texture, couleurs pré-multipliées et additif sans alpha, lot jamais au-delà des indices 16 bits.
- `test_debug_lines.cpp` : les 12 arêtes d'une boîte (chacune sur un axe, longueur totale), les coins d'un frustum, cercles, sphères et axes.
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
