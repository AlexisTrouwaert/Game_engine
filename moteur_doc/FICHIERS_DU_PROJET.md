# Fichiers du projet moteur

Explication des fichiers du dossier `moteur/` : à quoi ils servent, pourquoi ils sont faits ainsi et quand les modifier. Ce fichier garde l'**architecture** et le **build** (le *pourquoi*). Le *quoi* de chaque classe et de chaque fonction est dans la **référence de l'API**, générée par Doxygen depuis les en-têtes publics (cible `moteur_docs`, voir `cmake/Docs.cmake`). Comment écrire un jeu : [GUIDE.md](GUIDE.md). Les fichiers que le moteur lit et écrit : [FORMATS.md](FORMATS.md).

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
  CMakeLists.txt                      racine du build (le moteur seul, ou sous-dossier d'un jeu)
  CMakePresets.json                   configurations Windows / macOS : debug, release, shipping, asan, fuzz
  vcpkg.json                          dépendances
  vcpkg-configuration.json            dossier des ports ajoutés (overlay ports)
  ports/ozz-animation/                port vcpkg d'ozz-animation (absent du registre)
  triplets/arm64-osx.cmake            triplet vcpkg du Mac (cible macOS 13.0)
  .clang-tidy                         vérifications de l'analyse statique
  cmake/
    Warnings.cmake                    avertissements, erreurs, AddressSanitizer, fuzzing, /utf-8
    Shipping.cmake                    réglages de la version livrée (LTO, symboles à part, runtime statique)
    Shaders.cmake                     chaîne de compilation des shaders
    Assets.cmake                      copie des assets et des licences à côté de l'exécutable
    Atlas.cmake                       empaquetage des atlas au build
    Game.cmake                        moteur_add_game() : ce qu'il faut à tout programme du moteur
    Package.cmake                     moteur_package() : les paquets des joueurs
    PackageWindows.cmake              paquet Windows (archive portable, installateur)
    PackageMacos.cmake                paquet macOS (.app, .dmg)
    BuildInfo.cmake                   commit et date du build
    Docs.cmake                        référence de l'API (Doxygen, cible moteur_docs)
    windows/                          game.rc.in, game.manifest, installer.iss.in
    macos/Info.plist.in               Info.plist du bundle .app
  src/moteur/                         bibliothèque du moteur
    CMakeLists.txt
    include/moteur/*.hpp              en-têtes publics : ce qu'un jeu inclut (liste ci-dessous)
    private/moteur/                   en-têtes internes : cbor_check, debug_ui, ktx_texture, screenshot
    *.cpp                             implémentations, plus tone_mapper.hpp / .cpp (interne)
    miniaudio.c                       miniaudio et stb_vorbis (C, bibliothèque à part)
  apps/
    tranche/                          la tranche jouable : tranche_commun, programmes tranche et tranche_cook
      CMakeLists.txt
      main.cpp                        le programme tranche (ce que sera l'exécutable de l'ARPG)
      cook_main.cpp                   son outil de préparation (tranche_cook)
      demo3d.hpp, .cpp                le monde de la tranche (scène de démonstration 3D)
      states_demo.hpp, .cpp           ses écrans : titre, chargement, jeu, pause, options, sauvegardes
      slice_ui.hpp, .cpp              son interface (menus et panneaux sans ImGui)
      map_objects.hpp, .cpp           types d'objets des cartes
      sandbox_data.hpp, .cpp          ses tables de données
      sandbox_scene.hpp               interface des scènes du menu, aléatoire, murs des démos
    bac_a_sable/                      outils de développement
      CMakeLists.txt
      main.cpp                        menu, scènes de test, ligne de commande
      camera2d.hpp, .cpp              caméra 2D (2D du jalon 2, bac_a_sable_2d)
      iso.hpp, .cpp                   projection isométrique (bac_a_sable_2d)
      sprite_animation.hpp, .cpp      animations de sprites (bac_a_sable_2d)
      blender_compare.hpp, .cpp       scène comparée avec Blender
      anim_compare.hpp, .cpp          pose comparée avec Blender
      animation_test.hpp, .cpp        scène « Animation »
      audio_test.hpp, .cpp            scène « Audio »
      world_test.hpp, .cpp            scène « World »
      map_editor.hpp, .cpp            éditeur de cartes
      editeur_main.cpp                le programme editeur
  examples/jeu_minimal/               le plus petit jeu : modèle de l'usage du moteur par un jeu
  tools/
    atlas_packer/                     outil d'empaquetage d'atlas (C++)
    cook/                             moteur_cook : préparation des assets d'un jeu sans tables (C++)
    ci/                               verify.py (la vérification), static_analysis.py (clang-tidy)
    tests/                            check_references.py (captures de référence)
    measures/                         measure.py (scènes de mesure)
    fuzz/                             fuzzer.py (fuzzing des lecteurs de fichiers)
    crash/                            read_report.py (rapports de plantage)
    docs/                             Doxyfile.in, mainpage.md, check_docs.py
    package/                          check_package.py (paquet Windows), make_icon.py (icône)
    models/                           modèles de référence, téléchargement des modèles de test
    textures/                         textures KTX2, textures des particules
    audio/                            téléchargement des sons de test
    blender/                          comparaisons avec Blender
    maps/                             carte de la tranche
    input/                            rejeu scripté de la tranche
  art/                                sources d'art : world/, test/ (atlas), icons/ (icône de la tranche)
  assets_moteur/                      assets du moteur, copiés dans tout programme
    fonts/Inter-Regular.ttf           police (SIL Open Font License), et Inter-OFL.txt
    credits_moteur.json               crédits des bibliothèques et de la police du moteur
  assets/                             assets du bac à sable et de la tranche
  shaders/                            sources HLSL du moteur ; generated/msl/ (MSL versionné, pour macOS)
  tests/                              tests unitaires (moteur_tests), fuzz/, data/
  moteur_doc/                         documentation
  .gitignore
  .gitattributes
  README.md
```

**Les en-têtes publics** (`src/moteur/include/moteur/`), par domaine. Chacun a son `.cpp` du même nom dans `src/moteur/`, sauf `aabb`, `gpu_resource` et `material`, entièrement dans leur en-tête ; `animator_graph.cpp` et `animator_state.cpp` complètent `animator.cpp`. La référence de l'API (cible `moteur_docs`) décrit chaque classe et fonction ; la partie 4 dit à quoi sert chaque fichier et pourquoi il est fait ainsi.

| Domaine | En-têtes |
|---|---|
| Programme et plateforme | `application`, `fixed_timestep`, `frame_stats`, `state_stack`, `input`, `display`, `paths`, `file_io`, `log_file`, `crash`, `assert`, `jobs`, `process_memory`, `version` |
| Rendu | `renderer`, `gpu_resource`, `geometry_pool`, `sprite_renderer`, `sprite_batcher`, `sprite_region`, `texture_atlas`, `atlas_builder`, `image`, `utf8`, `text_layout`, `font`, `color`, `camera3d`, `aabb`, `mesh`, `material`, `mesh_batcher`, `mesh_renderer`, `model`, `environment`, `shadow`, `billboard_batcher`, `billboard_renderer`, `debug_lines` |
| Assets et données | `asset_cache`, `assets`, `archive`, `cook`, `credits`, `data_reader`, `data_table`, `save_file`, `world_save` |
| Monde | `world`, `tilemap`, `map_data`, `map_document`, `nav_grid`, `collision`, `spatial_hash`, `pathfinding`, `movement`, `visibility`, `particles`, `world_debug` |
| Animation | `animation_data`, `animation_clock`, `skeleton`, `animator`, `animation_set`, `animation_graph`, `animation_debug` |
| Audio | `audio`, `audio_rules`, `sound` |
| Outils de développement | `console`, `variables`, `profiler`, `debug_tools` |

Quatre familles :

- **Build** : les `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`, `triplets/`, les modules du dossier `cmake/`. Ils décrivent comment construire et livrer le projet.
- **Code** : le moteur (`src/moteur/`), la tranche (`apps/tranche/`), le bac à sable (`apps/bac_a_sable/`), l'exemple (`examples/`), les tests (`tests/`) et les outils C++. Ce que le projet fait.
- **Outils et données** : les scripts Python de `tools/`, l'art, les assets, les shaders.
- **Dépôt et documentation** : `.gitignore`, `.gitattributes`, `.clang-tidy`, `README.md`, `moteur_doc/`, et la référence de l'API générée par Doxygen.

---

## 2. Comment un build fonctionne

```
CMakePresets.json  ──►  choisit générateur, dossier de build, triplet vcpkg, options (shipping, asan, fuzz)
        │
        ▼
CMakeLists.txt (racine)
        │   ├─ trouve la toolchain vcpkg (si le moteur est le projet principal)
        │   ├─ règle C++20, les avertissements, la configuration livrée
        │   ├─ find_package(SDL3) ◄── vcpkg installe les dépendances d'après vcpkg.json
        │   └─ charge les fonctions de cmake/ (shaders, assets, atlas, jeu, paquet)
        │
        ├─► src/moteur/CMakeLists.txt         bibliothèque "moteur" (et "moteur_miniaudio")
        ├─► tools/atlas_packer, tools/cook    outils C++ (atlas, préparation des assets)
        ├─► apps/tranche/CMakeLists.txt       "tranche_commun", programmes "tranche" et "tranche_cook"
        ├─► apps/bac_a_sable/CMakeLists.txt   "bac_a_sable", "editeur" (hors version livrée)
        ├─► examples/jeu_minimal/             "jeu_minimal"
        ├─► tests/, tests/fuzz/               "moteur_tests", programmes de fuzzing
        └─► cmake/Docs.cmake                  cible "moteur_docs" (si Doxygen est là)
```

Chaque programme passe par `moteur_add_game()`, qui lui donne shaders, assets et licences à côté de l'exécutable. Dans la configuration livrée, `moteur_package()` ajoute la cible `<jeu>_package` : préparation des assets, archives, installateur ou `.dmg`.

Deux étapes distinctes :

1. **Configuration** (`cmake --preset ...`) : CMake lit les fichiers, vcpkg télécharge et compile les dépendances, puis CMake génère les fichiers Ninja.
2. **Compilation** (`cmake --build ...`) : Ninja compile le code et produit les exécutables.

Le premier build est long à cause de la compilation des dépendances par vcpkg. Les suivants sont rapides, car vcpkg met le résultat en cache.

**La vérification complète** (configurations debug et release, tests, données, exemple construit de l'extérieur, MSL, captures de référence, analyse statique, ASan, référence de l'API, paquet) tient en une commande : `python tools/ci/verify.py` (voir `tools/ci/`).

---

## 3. Fichiers de build

### `CMakeLists.txt` (racine)

**Rôle** : point d'entrée du build. Il fixe les règles communes et déclare les sous-projets. Le moteur peut être **le projet lui-même** (son bac à sable, ses outils, ses tests) ou **un sous-dossier d'un jeu** (`add_subdirectory()`, jalon 8, partie 3 ; voir `GUIDE.md`) : dans ce cas, le projet du jeu possède la toolchain, vcpkg et les réglages du langage.

**Contenu, dans l'ordre**

| Élément | Explication |
|---|---|
| `cmake_minimum_required(VERSION 3.25)` | Version minimale de CMake. Le 3.25 est requis par la version 6 des presets. |
| Bloc `CMAKE_TOOLCHAIN_FILE` | Seulement quand le moteur est le projet principal. Si aucune toolchain n'est fournie : celle de `VCPKG_ROOT`, sinon celle du vcpkg de CLion (`~/.vcpkg-clion/vcpkg`), que CLion n'injecte que dans les profils qui existaient quand vcpkg a été ajouté. Ce bloc est placé **avant** `project()`, sinon il est ignoré. |
| `option(MOTEUR_TRACY ...)` | Désactivée par défaut. Activée, elle ajoute la fonctionnalité `tracy` du manifeste vcpkg : les zones du profiler vont aussi à Tracy. |
| `option(MOTEUR_SHIPPING ...)` | La configuration **livrée** (jalon 8, partie 6 ; presets `*-shipping`) : sans Dear ImGui, sans rechargement à chaud, sans profiler ni programmes de test. Incompatible avec `MOTEUR_TRACY`. |
| `project(moteur VERSION 0.8.0 LANGUAGES C CXX)` | La version du moteur (lue par le code et par `check_docs.py`). Le C sert à l'implémentation de miniaudio. |
| `CMAKE_CXX_STANDARD 20`, `..._REQUIRED ON`, `CMAKE_CXX_EXTENSIONS OFF`, `CMAKE_EXPORT_COMPILE_COMMANDS ON` | Seulement si le moteur est le projet principal (`PROJECT_IS_TOP_LEVEL`). C++20 standard (`-std=c++20`, pas `gnu++20`), et `compile_commands.json` pour CLion et les outils d'analyse. Le moteur impose aussi C++20 à ceux qui le lient (`target_compile_features`). |
| `MOTEUR_ROOT` | Le dossier du moteur, pour ses fonctions CMake, quel que soit le projet qui les appelle : un chemin ne part jamais de `CMAKE_SOURCE_DIR`. |
| `include(Warnings)`, `include(Shipping)` | Les avertissements (et les analyses) et les réglages de la configuration livrée. |
| `find_package(SDL3 CONFIG REQUIRED)` | Cherche SDL3 (installé par vcpkg). |
| `include(Shaders)`, `include(Assets)`, `include(Atlas)`, `include(Game)`, `include(Package)` | Les fonctions `moteur_add_shaders()`, `moteur_add_assets()`, `moteur_add_licenses()`, `moteur_add_atlas()`, `moteur_add_game()`, `moteur_package()`. `Shaders` doit venir **après** `find_package`, car il a besoin de l'emplacement des paquets vcpkg. |
| `add_subdirectory(...)` | `src/moteur`, puis (hors fuzzing) `tools/atlas_packer` et `tools/cook`, puis `apps/tranche`, `apps/bac_a_sable`, `examples/jeu_minimal`, `tests`, `tests/fuzz` selon les options. L'ordre compte : la bibliothèque avant ce qui la lie, l'outil d'atlas avant les programmes qui l'utilisent au build, `tranche_commun` avant le bac à sable qui la lie. |
| Options des programmes | `MOTEUR_BUILD_SANDBOX` (bac à sable et éditeur), `MOTEUR_BUILD_SLICE` (le programme `tranche`), `MOTEUR_BUILD_EXAMPLES`, `MOTEUR_BUILD_TESTS` : actives quand le moteur est le projet principal, inactives quand il est le sous-dossier d'un jeu. La configuration livrée ne construit ni le bac à sable ni les tests (ils demandent Dear ImGui). `apps/tranche` est lu dès que le bac à sable ou la tranche est construit (le bac à sable a besoin de `tranche_commun`). |
| `include(Docs)` | Seulement pour le projet principal, hors fuzzing : la cible `moteur_docs`. |

**Quand le modifier** : ajout d'une dépendance globale, d'un sous-projet ou d'une option de build.

### `CMakePresets.json`

**Rôle** : décrire des configurations prêtes à l'emploi, pour ne pas retaper les options à la main et obtenir les mêmes résultats sur les deux machines. CLion les affiche comme des profils.

**Contenu**

- `base` (masqué) : générateur **Ninja** et dossier de build `build/<nom du preset>/`. Les autres presets en héritent.
- `windows-debug` / `windows-release` : actifs uniquement sur Windows (`condition` sur `hostSystemName`). Compilateur `cl` (MSVC), triplet `x64-windows-static-md`, toolchain « Visual Studio » de CLion.
- `macos-debug` / `macos-release` : actifs uniquement sur macOS. Architecture `arm64`, cible minimale `13.0`, triplet `arm64-osx` pris dans `triplets/` (`VCPKG_OVERLAY_TRIPLETS`).
- `windows-shipping` / `macos-shipping` : la version livrée (`MOTEUR_SHIPPING`), au-dessus de la Release ; sous Windows, triplet `x64-windows-static` (runtime C++ statique : rien à installer à côté du jeu).
- `windows-asan` / `macos-asan` : AddressSanitizer (et UBSan sur macOS) sur le code du projet (`MOTEUR_ASAN`).
- `windows-fuzz` / `macos-fuzz` : les programmes de fuzzing seulement (`MOTEUR_FUZZ`, les autres programmes coupés).
- `buildPresets` : un preset de compilation par preset de configuration, pour `cmake --build --preset ...`.

**À savoir**

- Le *triplet* vcpkg décrit la combinaison plateforme + architecture + type de lien. `x64-windows-static-md` signifie : bibliothèques **statiques**, runtime C de Microsoft **dynamique** (`/MD`). C'est l'option la plus courante avec MSVC.
- La `condition` permet à chaque OS de ne voir que ses propres presets.
- Aucun chemin absolu n'apparaît : le fichier fonctionne tel quel sur toute machine.
- En ligne de commande sur Windows, `cl` doit être accessible : utiliser un « Developer PowerShell for VS 2022 », ou `tools/ci/verify.py`, qui prépare l'environnement lui-même. Dans CLion, la toolchain Visual Studio le fait.

**Quand le modifier** : ajout d'une configuration, changement de cible macOS, changement de triplet.

### `vcpkg.json`

**Rôle** : le *manifeste* des dépendances. vcpkg le lit et installe ce qui y figure dans un dossier propre au projet.

**Contenu**

| Champ | Explication |
|---|---|
| `name` | Nom du paquet, en minuscules. |
| `version-string` | Version du projet (`0.8.0`, celle de `project()`). Sans effet sur les dépendances. |
| `builtin-baseline` | Identifiant d'un commit du dépôt vcpkg. Il **épingle les versions** de tous les ports : deux machines avec la même baseline compilent les mêmes versions. Valeur actuelle : `5f96cd15fd745122cf27e0524606d6c1efc5fd07`. |
| `dependencies` | `sdl3` ; `glm` (mathématiques) ; `stb` (images) ; `doctest` (tests unitaires) ; `nlohmann-json` (JSON) ; `cgltf` (glTF) ; `mikktspace` (tangentes) ; `entt` (cache d'assets, ECS) ; `efsw` (surveillance des fichiers, rechargement à chaud) ; `miniaudio` (audio) ; `ozz-animation` (animation squelettique, port du dossier `ports/`) ; `zstd` (compression des archives) ; `ktx` avec la fonctionnalité `tools` (textures KTX2, et l'outil `ktx` qui les encode) ; `imgui` avec `sdl3-binding` et `sdlgpu3-binding` (interface de debug ; ses shaders sont précompilés pour chaque backend) ; `sdl3-shadercross` **uniquement sur Windows** (l'outil `shadercross`, DXC et SPIRV-Cross ; il impose la fonctionnalité `vulkan` de SDL3, sans effet sur le backend choisi) ; `winpixevent` **sur Windows avec le runtime C dynamique** (`"windows & !staticcrt"`, donc pas dans la version livrée) : la DLL `WinPixEventRuntime.dll` de Microsoft, copiée à côté de l'exécutable, grâce à laquelle SDL nomme les passes dans RenderDoc et PIX sous Direct3D 12. |
| `features` | `tracy` : le profiler Tracy, installé seulement avec `MOTEUR_TRACY`. |

**Quand le modifier** : ajouter une bibliothèque ou mettre à jour la baseline. Chaque bibliothèque ajoutée va aussi dans les crédits (`assets_moteur/credits_moteur.json`) et dans la liste des licences de `src/moteur/CMakeLists.txt`.

**Piège** : sans baseline, chaque machine peut résoudre une version différente. Ne jamais la retirer.

### `vcpkg-configuration.json` et `ports/ozz-animation/`

**Rôle** : ajouter à vcpkg une bibliothèque qu'il ne connaît pas. `vcpkg-configuration.json` déclare le dossier `ports/` comme **overlay ports** : vcpkg y cherche les ports avant son registre, sur toutes les machines (CLion, presets, Windows et Mac), sans réglage de plus.

`ports/ozz-animation/` (jalon 5) : `vcpkg.json` (version 0.17.0, MIT), `portfile.cmake` (source GitHub épinglée par son SHA-512 ; seules les bibliothèques `ozz_base`, `ozz_animation`, `ozz_animation_offline`, `ozz_geometry` et `ozz_options` sont construites, en statique, sans outils, exemples, tests ni importeurs glTF / FBX ; runtime MSVC suivant le triplet ; avertissements d'ozz non bloquants, parce qu'ozz les traite en erreurs et qu'un compilateur plus récent que le sien ne doit pas casser le build), `ozz-animationConfig.cmake` (ozz installe ses bibliothèques mais aucun paquet CMake : ce fichier déclare les cibles `ozz::base`, `ozz::animation`, `ozz::animation_offline`, `ozz::geometry`, en Debug et Release) et `usage`.

**Quand le modifier** : changer de version d'ozz (`version` dans `vcpkg.json`, puis le SHA-512 de l'archive dans `portfile.cmake`) ; ajouter un autre port absent du registre (un dossier de plus dans `ports/`).

**Piège** : vcpkg réutilise un port déjà construit tant que son contenu ne change pas (hachage du port) ; modifier le port suffit à le reconstruire, pas besoin de vider les dossiers de build.

### `triplets/arm64-osx.cmake`

**Rôle** : le triplet `arm64-osx` de vcpkg, avec en plus la cible macOS minimale du projet (13.0). Sans elle, les dépendances visent la version de macOS de la machine de build et l'éditeur de liens avertit pour chaque fichier objet. Lu par les presets macOS et par `examples/jeu_minimal`.

### `cmake/Warnings.cmake`

**Rôle** : centraliser le niveau d'avertissements et les analyses du compilateur, **uniquement sur notre code** (pas sur le code tiers).

**Contenu**

- `moteur_enable_warnings(<cible>)` : MSVC `/W4 /permissive-` (refuse le code non conforme que MSVC tolère par défaut) ; Clang / GCC `-Wall -Wextra -Wpedantic`. `PRIVATE` : ces options ne se propagent pas aux utilisateurs de la cible.
- `MOTEUR_WARNINGS_AS_ERRORS` (désactivée par défaut, activée par `tools/ci/verify.py`) : `/WX` ou `-Werror`. Le build de tous les jours ne casse donc pas à chaque mise à jour du compilateur, mais la vérification refuse tout avertissement.
- `MOTEUR_ASAN` (presets `*-asan`, jalon 8, partie 8) : AddressSanitizer sur les cibles du projet (et UBSan hors MSVC). Les bibliothèques de vcpkg ne sont pas instrumentées : les annotations des conteneurs sont coupées partout (`_DISABLE_VECTOR_ANNOTATION`, `_DISABLE_STRING_ANNOTATION`), sinon leur désaccord signale de faux débordements.
- `MOTEUR_FUZZ` (presets `*-fuzz`) : active ASan et la couverture de libFuzzer sur le code du projet (`tests/fuzz`).
- **`/utf-8`** pour MSVC, sur toute la compilation Windows. Sans lui, MSVC lit les fichiers source dans la page de codes du système : un caractère accentué d'une chaîne est corrompu, **y compris dans un littéral `u8"..."`**, car le compilateur doit d'abord décoder les octets du fichier source et se trompe de page de codes à cette étape. Clang et GCC supposent déjà une source UTF-8. La bibliothèque `moteur` transmet aussi `/utf-8` aux jeux qui la lient (option `PUBLIC`).

**Quand le modifier** : changer la politique d'avertissements, ajouter une analyse.

### `.clang-tidy`

**Rôle** : les vérifications de l'analyse statique (jalon 8, partie 8), lancée par `tools/ci/static_analysis.py` sur les sources du moteur : `clang-analyzer-*`, `bugprone-*`, `performance-*`, moins quelques vérifications trop bavardes. Choisies pour ce qu'elles trouvent (bogues, copies coûteuses), pas pour le style. Un nouvel avertissement fait échouer la vérification.

### `cmake/Shipping.cmake`

**Rôle** : les réglages du compilateur et de l'éditeur de liens de la configuration livrée (`MOTEUR_SHIPPING`), pour toutes les cibles, celles du moteur et celles du jeu : optimisation entre fichiers (LTO / LTCG) ; informations de debug produites mais **gardées à part** (Windows : un PDB à côté du `.exe` ; macOS : `-g`, le dSYM est extrait au paquet), pour qu'un rapport de plantage donne des noms de fonctions sans alourdir le programme livré ; sous Windows, runtime C++ statique (`/MT`). `moteur_shipping_options(<cible>)` (appelée sur `moteur`) transmet ces options aux programmes qui la lient ; hors configuration livrée, elle ne fait rien.

### `cmake/Shaders.cmake`

**Rôle** : compiler les shaders HLSL au build et les déposer à côté de l'exécutable.

**Contenu**

| Élément | Explication |
|---|---|
| `MOTEUR_ENGINE_SHADERS` | La liste des shaders du moteur : tout programme les reçoit (`moteur_add_game`). |
| Détection de `shadercross` (Windows) | Cherche l'outil dans `vcpkg_installed/<triplet>/tools/sdl3-shadercross`. Arrête la configuration s'il est absent. |
| Dossier `shader_tools/` | Au configure, copie `shadercross.exe`, `dxcompiler.dll` et `dxil.dll` dans `build/<preset>/shader_tools/`, pour que l'outil trouve les bibliothèques dont il dépend. |
| `moteur_add_shaders(cible [NAME nom] SOURCE_DIR dossier SOURCES ...)` | Pour chaque fichier `<nom>.vert\|frag\|comp.hlsl` du dossier, déduit l'étage du nom de fichier et ajoute une commande de compilation. La cible dépend de ces sorties : un shader modifié est recompilé, une erreur fait échouer le build. `NAME` distingue deux jeux de shaders d'une même cible (ceux du moteur et ceux du jeu). |
| Windows | HLSL vers **DXIL**, écrit dans `<dossier de la cible>/shaders/<nom>.dxil`. |
| Inclusions | Un shader peut inclure un fichier de son dossier (`-I`) : une variante définit une macro et inclut son shader de base (`mesh_skinned.vert.hlsl`, `shadow_masked.vert.hlsl`). Tout shader est recompilé quand un fichier `.hlsl` ou `.hlsli` du dossier change : cela coûte peu et n'en laisse jamais un périmé. |
| Cible `export_msl_shaders` | Manuelle, hors build normal (Windows). HLSL vers **MSL**, écrit dans `<dossier>/generated/msl/`. À lancer après toute modification d'un shader, puis à versionner ; `tools/ci/verify.py` vérifie que le MSL versionné est à jour. |
| macOS | Ne compile rien : copie `<dossier>/generated/msl/<nom>.msl` vers `<dossier de la cible>/shaders/`. Arrête la configuration si le fichier MSL est absent. |

**Pourquoi cette différence entre OS** : le port vcpkg `sdl3-shadercross` dépend de `directx-dxc`, qui n'existe pas sur macOS. On génère donc le MSL sur Windows, où tout fonctionne.

**Limite** : le dossier de sortie est celui de la cible, ce qui suppose un générateur mono-configuration (Ninja), comme dans nos presets.

### `cmake/Assets.cmake`

**Rôle** : copier les assets et les licences à côté de l'exécutable.

**Contenu**

- `moteur_add_assets(cible FOLDERS dossier...)` : copie les dossiers, dans l'ordre, dans un seul `assets/` à côté de l'exécutable (un fichier d'un dossier suivant remplace celui de même chemin d'un dossier précédent). La copie a lieu à **chaque build**, ce qui garde les assets à jour même quand seul un fichier image a changé (une copie attachée à l'édition de liens ne s'exécuterait pas dans ce cas).
- `moteur_add_licenses(cible PACKAGES sdl3 imgui ...)` : copie, à chaque build, le texte de licence que vcpkg installe avec chaque paquet (`share/<paquet>/copyright`) dans `licenses/<paquet>.txt`. Un paquet sans fichier de licence arrête la configuration. Les licences MIT et zlib exigent que leur notice accompagne le programme.

**Pourquoi** : le programme cherche ses assets relativement à l'exécutable (`asset_path()`), pas au dossier de travail. Il fonctionne donc quel que soit l'endroit d'où on le lance. Ces deux fonctions ne sont en général pas appelées directement : `moteur_add_game()` le fait.

### `cmake/Atlas.cmake`

**Rôle** : lancer l'outil d'empaquetage d'atlas au build, seulement quand c'est utile.

**Contenu** : la fonction `moteur_add_atlas(cible NAME nom SOURCES dossier)` (dossier relatif à la racine du projet appelant). Elle liste les PNG et le `pivots.json` du dossier (avec `CONFIGURE_DEPENDS`, donc les ajouts et retraits sont vus sans reconfigurer), ajoute une commande dont le résultat est `<nom>.json` dans le dossier `assets/` de l'exécutable, et fait dépendre la cible de cette commande.

**Un piège traité** : quand un fichier est **retiré**, il ne reste aucune dépendance plus récente que le résultat, donc l'empaquetage ne se relançait pas et l'atlas gardait l'image supprimée. La commande dépend donc aussi d'un petit fichier qui **liste les entrées** et n'est réécrit que si cette liste change.

**À savoir** : contrairement aux shaders, l'outil est notre propre programme. Il se compile et se lance de la même façon sur Windows et sur Mac, sans fichiers pré-générés.

### `cmake/Game.cmake`

**Rôle** (jalon 8, partie 3) : `moteur_add_game(cible [NAME] [VERSION] [PUBLISHER] [ICON] [ASSETS ...] [SHADER_DIR ... SHADERS ...] [LICENSES ...] [SHARE_OUTPUT_OF autre])`, ce qu'il faut à **tout programme** construit sur le moteur, à côté de son exécutable :

- la liaison avec `moteur` ;
- les shaders du moteur (compilés pour l'OS) et ceux du jeu, dans `shaders/` ;
- les assets du moteur (`assets_moteur/`) puis ceux du jeu, fusionnés dans `assets/` (un fichier du jeu remplace celui du moteur de même chemin) ;
- les licences des bibliothèques du moteur (`MOTEUR_LICENSE_PACKAGES`) et du jeu, dans `licenses/` ;
- sous Windows, les **ressources** de l'exécutable (`cmake/windows/game.rc.in` et `game.manifest`) : l'icône (`ICON`, chemin sans extension : `.ico` sous Windows, `.icns` sous macOS), les informations de version (nom, éditeur, version, copyright) et un manifeste (Windows 10 / 11, densité par écran, chemins longs, page de codes UTF-8, sans élévation) ;
- sous Windows hors configuration livrée, `WinPixEventRuntime.dll`.

`NAME`, `VERSION` (par défaut celle du projet, au format `x.y.z`), `PUBLISHER` et `ICON` sont gardés sur la cible pour `moteur_package()`. Avec `SHARE_OUTPUT_OF`, le programme est placé dans le dossier d'un autre et partage ses copies (l'éditeur à côté du bac à sable).

**Pourquoi** : un jeu ne doit pas recopier la liste des shaders et des licences du moteur ; tout ce qui change dans le moteur lui arrive par cette fonction.

### `cmake/Package.cmake`, `cmake/PackageWindows.cmake`, `cmake/PackageMacos.cmake`

**Rôle** (jalon 8, parties 10 et 11) : `moteur_package(cible MANIFEST fichier [COOK_TOOL outil] [IDENTIFIER com.editeur.jeu] [WINDOWS_APP_ID GUID])` fabrique la cible `<cible>_package`, **seulement dans la configuration livrée** (`MOTEUR_SHIPPING`) : c'est la seule que reçoivent les joueurs. Elle :

1. prépare les assets du jeu avec le manifeste de livraison (outil de préparation du jeu, `moteur_cook` par défaut ; incrémental), dans `build/<preset>/package/<cible>/cook` ;
2. range ce qui s'installe : l'exécutable, `archives/` (assets et shaders préparés, en archives), `licenses/` ;
3. Windows (`PackageWindows.cmake`) : signe l'exécutable si `MOTEUR_SIGN_COMMAND` est donné, écrit l'archive portable (`<cible>-<version>-windows-x64.zip`) et l'installateur Inno Setup (`<cible>-<version>-windows-x64-setup.exe`, d'après `cmake/windows/installer.iss.in`) ; sans Inno Setup, pas d'installateur (avertissement). macOS (`PackageMacos.cmake`) : construit `<nom>.app` (d'après `cmake/macos/Info.plist.in`, icône, ressources), le signe (`MOTEUR_MACOS_SIGN_IDENTITY`, ad hoc par défaut), le met dans `<cible>-<version>-macos-arm64.dmg`, et fait notariser les deux quand `MOTEUR_MACOS_NOTARY_PROFILE` nomme un profil `notarytool` ;
4. garde le programme et ses symboles (PDB, dSYM) de cette version dans `packages/symbols/`, pour lire les rapports de plantage (`tools/crash/read_report.py`).

Tout arrive dans `build/<preset>/packages/`. Les deux scripts d'OS sont lancés par `cmake -P`.

**À savoir** : rien n'est jamais écrit dans le dossier installé ni dans le bundle : ce que le jeu écrit va dans le dossier du joueur (un `.app` signé dont un fichier change n'a plus de signature valide). L'installateur installe pour tous les utilisateurs ou pour le joueur seul (sans droits d'administrateur), une mise à jour s'installe par-dessus la précédente (même `AppId`), et la désinstallation laisse le dossier du joueur.

### `cmake/windows/` et `cmake/macos/`

**Rôle** : les modèles remplis par CMake. `game.rc.in` (icône et informations de version de l'exécutable), `game.manifest` (le manifeste, voir `Game.cmake`), `installer.iss.in` (l'installateur Inno Setup, enregistré en UTF-8 avec BOM pour qu'Inno Setup le lise en UTF-8) ; `macos/Info.plist.in` (l'`Info.plist` du bundle, vérifié par `plutil -lint`).

### `cmake/BuildInfo.cmake`

**Rôle** (jalon 8, partie 3) : écrire, à chaque build (cible `moteur_build_info`, par `cmake -P`), le commit (avec `+` si l'arbre de travail a des changements) et sa date dans `build_info.cpp`, compilé dans le moteur. Le fichier n'est réécrit que s'ils ont changé : un dépôt inchangé ne recompile rien. Ces informations forment la ligne de `build_description()` (`version.hpp`), écrite en tête du journal et dans les rapports de plantage.

### `cmake/Docs.cmake`

**Rôle** (jalon 8, partie 9) : la cible **`moteur_docs`**, la référence de l'API générée par Doxygen (1.9 ou plus) depuis les commentaires `///` des en-têtes publics, dans `build/<preset>/docs/html/index.html`. La configuration est `tools/docs/Doxyfile.in`, la page d'accueil `tools/docs/mainpage.md`. Tout avertissement (une déclaration publique sans documentation, une commande fausse) fait échouer la cible. Sans Doxygen, la cible n'existe pas (Doxygen est un outil de build, pas une bibliothèque du moteur).

### `src/moteur/CMakeLists.txt`

**Rôle** : définit la **bibliothèque** `moteur`, et à part la bibliothèque C `moteur_miniaudio`.

**Contenu**

| Élément | Explication |
|---|---|
| `find_package(...)`, `find_path(...)` | EnTT, ozz-animation, efsw, KTX, GLM, ImGui, MikkTSpace, nlohmann-json, Threads, zstd ; stb, cgltf et miniaudio sont de simples dossiers d'en-têtes. |
| `moteur_miniaudio` | L'implémentation de miniaudio et de stb_vorbis, en C, **sans les avertissements du projet** (code tiers) ; les frameworks audio de macOS. |
| `add_library(moteur STATIC ...)` | La liste des `.cpp` du moteur. **Chaque nouveau `.cpp` doit être ajouté ici.** |
| `moteur_build_info` | La cible qui écrit `build_info.cpp` (voir `cmake/BuildInfo.cmake`) ; `MOTEUR_VERSION` et `MOTEUR_BUILD_TYPE` sont définis pour le code. |
| `target_include_directories(moteur PUBLIC include PRIVATE private)` | `include/` est **l'interface publique**, ce qu'un jeu inclut (`#include "moteur/renderer.hpp"`) ; `private/` contient les en-têtes propres au moteur (`#include "moteur/ktx_texture.hpp"` ne marche que dans le moteur et ses tests). |
| `target_include_directories(moteur SYSTEM PRIVATE ...)` | stb et cgltf. `SYSTEM` : leurs avertissements sont ignorés. |
| `target_link_libraries(...)` | `PUBLIC` : SDL3, GLM, EnTT, nlohmann-json (les tables de données passent des lignes JSON aux lecteurs du jeu) et, hors configuration livrée, ImGui (les jeux dessinent leurs fenêtres de debug avec). `PRIVATE` : Threads, zstd, MikkTSpace, KTX, miniaudio, ozz, efsw ; sous Windows `psapi` et `dbghelp` (mémoire du processus, rapports de plantage). |
| `MOTEUR_SHIPPING` | Retire `debug_tools.cpp` et `debug_ui.cpp`, ne lie ni ImGui ni efsw, définit `MOTEUR_SHIPPING` pour tous (`PUBLIC`) et applique `moteur_shipping_options()`. Ce code n'est pas compilé du tout dans la version livrée. |
| `MOTEUR_TRACY` | Lie Tracy et définit `MOTEUR_TRACY`. |
| `MOTEUR_LICENSE_PACKAGES` | Les paquets vcpkg dont tout programme du moteur porte la licence (`moteur_add_game`), selon la configuration (ImGui, efsw et WinPixEvent hors version livrée, Tracy avec `MOTEUR_TRACY`). À garder en accord avec `assets_moteur/credits_moteur.json`. |

**`PUBLIC` ou `PRIVATE` ?** `PUBLIC` : la dépendance apparaît dans nos en-têtes publics ou doit être visible par l'utilisateur. `PRIVATE` : détail interne, invisible de l'extérieur. Choisir `PRIVATE` par défaut dès qu'une dépendance n'apparaît pas dans un en-tête public : cela garde les interfaces propres.

**Quand le modifier** : chaque nouveau fichier source du moteur, chaque nouvelle dépendance.

### `apps/tranche/CMakeLists.txt`

**Rôle** (jalon 8, partie 12) : le code de la **tranche jouable**, dans la bibliothèque **`tranche_commun`** (tables, objets des cartes, monde `Demo3D`, écrans `StatesDemo`, interface `SliceUi`), partagée par le programme `tranche` et par le bac à sable (sa scène de la tranche, les captures de référence). Elle est construite dans toutes les configurations, la livrée comprise (Dear ImGui n'y sert qu'aux panneaux de debug, retirés de la version livrée). Hors version livrée, `MOTEUR_ASSETS_SOURCE_DIR` donne le dossier `assets/` des sources (rechargement à chaud, écriture des cartes par l'éditeur).

Avec `MOTEUR_BUILD_SLICE` : le programme **`tranche`** (`moteur_add_game` avec le nom « Tranche jouable », l'éditeur `PUBLISHER "moteur"` et l'icône `art/icons/tranche`), son outil de préparation **`tranche_cook`** (qui compile ses tables), et son paquet (`moteur_package` avec le manifeste `assets/livraison_tranche.json` : cible `tranche_package` dans la configuration livrée).

### `apps/bac_a_sable/CMakeLists.txt`

**Rôle** : les outils de développement, absents de la configuration livrée :

- **`bac_a_sable_2d`** : la 2D du jalon 2 (`camera2d`, `iso`, `sprite_animation`), gardée pour les scènes 2D du bac à sable et leur capture de référence. Elle ne fait plus partie du moteur depuis la revue des jalons 1 à 7 (2.1) : le jeu est en 3D.
- **`bac_a_sable_commun`** : la scène « World » et l'éditeur de cartes (jalon 7, parties 7 et 8), liée à `tranche_commun`.
- l'exécutable **`bac_a_sable`** (`main.cpp` et les scènes de test) et le programme **`editeur`** (`editeur_main.cpp`), placé dans le même dossier et qui partage shaders, assets et licences (`SHARE_OUTPUT_OF`).
- `moteur_add_game(bac_a_sable ASSETS assets)` ; `moteur_add_atlas(...)` pour les atlas `world` et `test`, empaquetés depuis `art/` au build.

**Pourquoi séparé** : le moteur reste réutilisable par l'ARPG, sans emporter le bac à sable ; la tranche est un programme à part, comme le sera l'ARPG.

### `examples/jeu_minimal/CMakeLists.txt` et `CMakePresets.json`

**Rôle** (jalon 8, partie 3) : le plus petit programme construit sur le moteur, et le **modèle de l'usage du moteur par un jeu** (`GUIDE.md`, « Démarrer un jeu »). Il se construit de deux façons : dans le projet du moteur (`MOTEUR_BUILD_EXAMPLES`, rien à faire), ou **seul, de l'extérieur**, comme un jeu qui a le moteur en sous-module Git (`cmake --preset windows-release` dans son dossier, avec ses propres presets). Dans ce cas, `MOTEUR_DIR` désigne le moteur (`../..`), le manifeste vcpkg et les triplets sont ceux du moteur (l'exemple n'a pas de bibliothèque à lui), puis `add_subdirectory()` ajoute le moteur. `moteur_add_game(jeu_minimal NAME "Jeu minimal" VERSION 1.0.0 PUBLISHER "moteur" ...)` et `moteur_package(...)` avec `assets/livraison.json` : l'exemple a son paquet, préparé par `moteur_cook` (il n'a pas de tables). `tools/ci/verify.py` le construit et le lance de l'extérieur.

### `tools/cook/CMakeLists.txt` et `tools/atlas_packer/CMakeLists.txt`

**Rôle** : les deux outils C++ du moteur, construits hors fuzzing. `moteur_cook` (préparation des assets d'un jeu sans tables) et `atlas_packer` (empaquetage d'atlas, avec `stb_image_write`), tous deux liés à `moteur`.

### `tests/CMakeLists.txt` et `tests/fuzz/CMakeLists.txt`

**Rôle** : l'exécutable **`moteur_tests`** (doctest), enregistré auprès de CTest. Les tests de la 2D du bac à sable (`test_animation.cpp`, `test_camera.cpp`, `test_iso.cpp`) n'y sont ajoutés que si `bac_a_sable_2d` est construite. `MOTEUR_SOURCE_ASSETS` et `MOTEUR_TEST_DATA` donnent aux tests les dossiers `assets/` et `tests/data/` des sources ; les en-têtes `private/` du moteur sont visibles, pour tester ses internes. `tests/fuzz/` : un programme de fuzzing par lecteur de fichiers (`fuzz_map`, `fuzz_effect`, `fuzz_animation`, `fuzz_save`, `fuzz_ktx2`, `fuzz_gltf`, `fuzz_table`, `fuzz_bindings`, `fuzz_image`, `fuzz_archive`), tous depuis `fuzz.cpp`, avec libFuzzer (`MOTEUR_FUZZ`).

---

## 4. Code source

**Conventions de toute cette partie**

- **Public ou interne** : un en-tête de `src/moteur/include/moteur/` est **public** (un jeu l'inclut : `#include "moteur/renderer.hpp"`) et documenté pour Doxygen (commentaires `///`) ; un en-tête de `src/moteur/private/moteur/` est **interne** : seuls le moteur et ses tests le voient. Le préfixe `moteur/` évite les collisions de noms avec d'autres bibliothèques et montre d'où vient chaque en-tête.
- Le code du moteur est dans le namespace `moteur` ; celui de la 2D du bac à sable dans `bac_a_sable`.
- Pour la liste des fonctions d'une classe, ses paramètres et ses unités : la **référence de l'API** (`moteur_docs`). Cette partie dit le rôle de chaque fichier, les choix et les pièges.

### `src/moteur/include/moteur/version.hpp` et `version.cpp`

**Rôle** : dire de quoi le programme est fait. `version()` (celle de `project(moteur VERSION ...)` : une version mineure par jalon), `build_commit()` et `build_commit_date()` (écrits au build par `cmake/BuildInfo.cmake`), `build_description()` (la ligne `moteur 0.8.0 (commit, date, configuration)` écrite en tête du journal et dans les rapports de plantage) et `sdl_version()` (la version de SDL réellement liée à l'exécution).

### `src/moteur/include/moteur/application.hpp` et `application.cpp`

**Rôle** : la fenêtre, la boucle de jeu et la propriété de tous les systèmes du moteur.

**Architecture**

- `Game` est l'interface que le programme implémente. `update()` est appelé à **fréquence fixe** avec un `dt` constant (1 / `tick_rate()`) : c'est là que vit la logique, déterministe. `render(renderer, alpha)` est appelé une fois par frame dessinable et sert à **enregistrer** ce qu'il faut dessiner : rien n'est envoyé au GPU tant que la frame n'est pas terminée. `alpha` est l'avancement entre deux ticks, pour l'interpolation. `on_event()` reçoit les événements SDL bruts.
- `Application` possède tout, et le crée et le libère dans un ordre fixe (RAII) : SDL et la fenêtre, le `Renderer`, les assets, les tables de données, la console, le journal, le profiler, les entrées, l'audio et, hors version livrée, les outils de debug (libérés en premier : ils désignent tout le reste). Elle installe le rapporteur de plantages, monte les archives (`archives_directory`), applique les réglages d'affichage du joueur (`display_settings`, `apply_display()`), puis fait tourner la boucle `run(Game&)`.
- `ApplicationConfig` regroupe ce qu'un programme choisit au démarrage : fenêtre, cadence et rattrapage, synchronisation, debug du GPU, mesures (`report_performance`, `report_json_path`, `gpu_timing`), interface de debug, rechargement à chaud (`assets_source_directory`), dossier du joueur (`organization`, `application`), enregistrement et rejeu des entrées, audio, commandes de départ, journal, rapports de plantage et messages au joueur, réglages d'affichage, archives.
- **Le dossier du joueur** : `preferences_directory()` (`SDL_GetPrefPath`). Tout ce qu'un programme écrit y va, jamais à côté de l'exécutable (voir la partie 6).

**Déroulement d'une frame** (`application.cpp`) : mesure du temps (`SDL_GetPerformanceCounter`), calcul des pas de logique par un `FixedTimestep`, rechargements d'assets en attente, événements, `update()` de durée fixe (suivis de `Input::end_tick()`), puis la frame (`begin_frame`, `Game::render`, `end_frame`). C'est le VSync du swapchain qui rythme la boucle, car `begin_frame()` attend l'image suivante ; fenêtre minimisée : rien n'est dessiné et la boucle attend 10 ms.

**Mesures** : chaque frame dessinée est chronométrée en trois phases (événements et mises à jour, enregistrement par `Game::render`, exécution par `end_frame`). L'attente de l'écran, dans `begin_frame`, est **exclue** : c'est de l'attente, pas du travail. Les 60 premières frames sont ignorées (préchauffage). Avec `report_performance`, un résumé (moyenne, percentile 99, maximum, moyenne par phase) est écrit à la fermeture ; avec `report_json_path`, le rapport JSON des scènes de mesure (`tools/measures/measure.py`).

**Points et pixels** : `to_pixels(point)` convertit une position en points de fenêtre (ce que SDL donne pour la souris) en pixels (ce que le rendu dessine). Identique sur un écran normal, différent d'un facteur 2 sur beaucoup de Mac. La fonction libre `window_to_pixels()` est dans `display.hpp`.

### `src/moteur/include/moteur/renderer.hpp` et `renderer.cpp`

**Rôle** : le rendu GPU, au-dessus de SDL_GPU. Le `Renderer` possède le périphérique et tous les systèmes de dessin (`sprites()`, `screen_sprites()`, `meshes()`, `billboards()`, `debug_lines()`, l'interface ImGui).

**Une frame en deux phases** : `begin_frame()` acquiert l'image de la fenêtre (c'est là que la boucle attend l'écran ; il n'ouvre **pas** de render pass, et renvoie `false` si la fenêtre est minimisée). Le jeu **enregistre** ensuite ce qu'il veut dessiner. `end_frame()` fait les envois (copy passes, qui doivent précéder les render passes), puis les passes, chacune nommée par un groupe de debug :

1. **« shadow »** : profondeur des maillages vue du soleil (quand il y a de la 3D, que les ombres sont activées et que le soleil éclaire) ;
2. **« point shadows »** : l'atlas des ombres des lumières ponctuelles ;
3. **« scene »** : la 3D en couleurs linéaires HDR (`kSceneFormat`, `R16G16B16A16_FLOAT`) à la résolution de rendu (`set_render_scale()`, de 0,25 à 1 ; l'interface reste à pleine résolution), avec MSAA si demandé (`set_anti_aliasing()`), puis les billboards et les lignes de debug ;
4. avec FXAA, **« tonemap »** : le tone mapping dans une texture à part, car FXAA travaille sur des couleurs d'écran ;
5. **« compose »**, sur l'écran : la scène tone mappée (ou passée par FXAA ; un écran effacé sans 3D), puis les sprites du monde, l'interface (`screen_sprites()`, toujours en pixels de fenêtre) et ImGui.

Enfin, si `request_capture()` a été demandé : un copy pass relit l'image du swapchain **avant** la soumission (pendant que la texture est encore valide), puis une attente GPU et l'écriture du PNG (`write_capture_png()`). Coûte une attente GPU complète : pour les tests de pixels, jamais à chaque frame.

**Choix et pièges**

- **Backend** : les formats de shaders acceptés (`kShaderFormats` : DXIL sur Windows, MSL et metallib sur macOS, SPIR-V sinon) déterminent le backend (Direct3D 12, Metal, Vulkan). `SDL_GPU_DRIVER` le force sans toucher au code. `load_shader()` essaie DXIL, MSL puis SPIR-V ; le point d'entrée est `main`, mais **`main0` en MSL** (SPIRV-Cross renomme la fonction, `main` étant réservé en Metal).
- **Profondeur** : le format est choisi au démarrage, `D32_FLOAT` si le GPU sait s'en servir comme cible, sinon `D24_UNORM`, sinon `D16_UNORM` (le seul garanti par SDL), et écrit dans le log.
- **Ressources** : les fonctions `create_*` envoient leurs données par une zone de transfert et un copy pass sur un command buffer à part, puis attendent la fin : acceptable au chargement, jamais pendant une frame. Les maillages ne reçoivent pas chacun leur buffer : `create_geometry()` leur donne une tranche d'un grand buffer (`geometry_pool.hpp`). Les textures compressées passent par `create_texture_levels()` (voir `ktx_texture.hpp`). Chaque ressource peut recevoir un **nom** pour RenderDoc ou le débogueur Metal de Xcode, sans coût à l'exécution.
- **Textures** : `create_texture()` **pré-multiplie l'alpha** par défaut (la forme que le GPU mélange et filtre correctement) ; `TextureSettings` choisit le sRGB, les mipmaps (générées par le GPU) et la pré-multiplication. `compressed_formats()` et `set_block_compression()` disent et limitent ce que le GPU lit (BC7, BC5).
- **`stats()`** : les `RenderStats` de la frame qui vient de se terminer, **valides seulement entre `end_frame()` et le `begin_frame()` suivant** : lues dans `Game::render()`, elles sont déjà remises à zéro. Avec `set_gpu_timing()`, les temps GPU approximatifs de chaque partie de la frame.
- **GPU perdu** : `device_lost()` dit si le périphérique a été perdu (pilote réinitialisé, carte retirée) ; l'`Application` écrit alors un rapport et ferme proprement. `simulate_device_lost()` le provoque pour vérifier cette chaîne.
- **Ordre de destruction** : le destructeur attend le GPU, libère les systèmes de dessin **avant** le périphérique (leurs ressources en dépendent), puis la fenêtre. Si la construction échoue, le constructeur libère lui-même le périphérique avant de relancer l'exception, car un destructeur ne s'exécute pas quand son constructeur lève une exception.

### `src/moteur/private/moteur/screenshot.hpp` et `screenshot.cpp` (internes)

**Rôle** (jalon 2, partie 9) : `write_capture_png()` convertit des pixels bruts relus du GPU en fichier PNG, pour `Renderer::request_capture()` : BGRA converti en RGB si le format l'exige (le swapchain peut être `B8G8R8A8_UNORM` selon le backend), alpha forcé à 255 (celui du swapchain n'a pas de sens une fois présenté), réduction facultative à une largeur maximale, écriture atomique à un chemin UTF-8. Séparé de `renderer.cpp`, pour que l'implémentation de `stb_image_write` (définie ici) ne se mêle pas au reste du rendu. Interne : un jeu demande une capture au `Renderer`.

### `src/moteur/include/moteur/gpu_resource.hpp`

**Rôle** : faire en sorte qu'une ressource GPU se libère toute seule, et pouvoir la nommer pour le débogage.

**Contenu**

- Le modèle `GpuResource<T, Release>` et les alias `GpuBuffer`, `GpuTexture`, `GpuSampler`, `GpuShader`, `GpuTransferBuffer`, `GpuGraphicsPipeline`. Chaque objet possède un pointeur SDL_GPU et libère la ressource dans son destructeur. Il est **déplaçable mais non copiable**, donc une ressource ne peut pas être libérée deux fois.
- `gpu_resource_counts()` : le nombre d'objets GPU vivants de chaque sorte (compteurs tenus par `GpuResource`), pour vérifier que rien ne fuit d'une scène à l'autre (jalon 4, partie 6).
- **`NameProperty`** (partie 10) : crée les propriétés SDL nécessaires pour nommer une ressource à sa création (un buffer, une texture, un sampler, un shader ou un pipeline), puis les détruit à la fin de sa portée. Avec un nom nul, `id()` vaut 0, ce que toute fonction `SDL_CreateGPU*()` interprète comme « aucune propriété » : nommer une ressource reste entièrement facultatif. **Il faut garder l'objet en vie jusqu'à l'appel `SDL_CreateGPU*()` qui lit la propriété**, pas seulement jusqu'à la ligne qui remplit `info.props`.

**Règle de durée de vie** : la ressource mémorise le périphérique pour se libérer, donc **le `Renderer` doit vivre plus longtemps que toutes les ressources créées par lui**. En pratique : déclarer les ressources après l'`Application` dans la même portée, ou dans des objets que l'`Application` survit. Le contraire provoque un accès à un périphérique déjà détruit.

### `src/moteur/include/moteur/geometry_pool.hpp` et `geometry_pool.cpp`

**Rôle** (jalon 8, partie 5) : ranger les sommets et les indices des maillages dans de **grands buffers GPU** (pages de 8 Mio) au lieu d'un buffer chacun. `GeometryPool` donne des tranches (`GpuSlice`) d'une page : premier emplacement libre, décalages alignés sur 256 octets (valides pour les liaisons de sommets et d'indices sur tous les backends), tranches voisines réunies quand elles sont rendues. Un maillage de plus d'un quart de page reçoit son propre buffer. Le `Renderer` possède le pool (`create_geometry()`).

**Pourquoi** : chaque buffer créé coûte une ressource à part au GPU (environ 0,3 ms sous Direct3D 12, et une mémoire arrondie à 64 Ko) : les 400 petits buffers du sol de la tranche prenaient 130 ms à créer.

**Piège traité** : une tranche rendue n'est réutilisée qu'après `kReuseDelayFrames` (3) `begin_frame()`, car le GPU peut encore lire les frames précédentes. Une `GpuSlice` garde un pointeur vers son pool : le `Renderer` doit lui survivre, comme pour toute ressource GPU.

### `src/moteur/include/moteur/sprite_renderer.hpp` et `sprite_renderer.cpp`

**Rôle** : dessiner des rectangles texturés en 2D : le monde 2D (`sprites()`) et l'interface (`screen_sprites()`), texte compris.

**Architecture**

- `draw()` **enregistre** un sprite (une texture et un rectangle, ou une région d'atlas placée par **son pivot sur l'ancre**, marges rognées restituées, retournement en miroir autour du pivot) ; rien n'est envoyé au GPU à ce moment. `SpriteOptions` porte la teinte, la **profondeur** (le plus grand est dessiné en dernier, donc au-dessus) et les retournements.
- `set_view_projection(matrice)` vaut pour toute la frame (le dernier appel l'emporte) et est oubliée à la fin. **Sans elle, les sprites sont placés directement en pixels d'écran** (origine en haut à gauche, Y vers le bas). C'est pourquoi le `Renderer` en possède **deux** : le monde (`sprites()`) et l'interface (`screen_sprites()`), tous deux dans la passe « compose », sans profondeur.
- Pipeline en **alpha pré-multiplié** (`ONE`, `ONE_MINUS_SRC_ALPHA`) : les textures contiennent une couleur déjà multipliée par l'alpha. Le pipeline déclare le format de profondeur de sa passe sans le tester ni l'écrire : l'ordre reste celui de la clé `depth`. Le nom donné au constructeur préfixe chaque ressource (`sprite...`, `screen sprite...`), pour les retrouver dans une capture.
- `prepare()` (avant le render pass, appelé par le `Renderer`, qui en est `friend`) : le batcher trie et construit les sommets ; le buffer de sommets double au besoin ; envoi par copy pass avec `cycle = true` (si le GPU lit encore le buffer de la frame précédente, SDL en donne un autre au lieu d'attendre). `render()` : **une seule matrice vue-projection** pour la frame, puis **un draw call par lot**, la texture liée seulement quand elle change.
- `set_batching(false)` : un draw call par sprite. L'image est identique : c'est un outil de **mesure et de comparaison**, pas un réglage de jeu.

**Coût mesuré** (Release, Windows) : environ 28 ns de CPU par sprite, dont un draw call ne représente qu'environ 15 à 17 ns quand chaque sprite a le sien.

### `src/moteur/include/moteur/sprite_batcher.hpp` et `sprite_batcher.cpp`

**Rôle** : préparer ce que le GPU va recevoir, **sans aucune dépendance au GPU** : c'est du C++ pur, donc entièrement testable.

**Contenu**

- `SpriteVertex` : un sommet tel qu'il est envoyé au GPU, 20 octets (position, coordonnées de texture, teinte sur 4 octets). Un `static_assert` garantit qu'il n'y a pas de remplissage.
- `SpriteDesc` : un sprite demandé par le jeu. La texture n'est qu'une **identité** (un pointeur opaque) : les sprites qui la partagent peuvent être dessinés ensemble.
- `SpriteBatcher::finish()` fait le travail :
  1. **tri par profondeur**, croissante, stable, mais seulement si les sprites n'ont pas été enregistrés dans l'ordre (un suivi de la profondeur maximale le détecte à coût nul) ;
  2. **construction de 4 sommets par sprite**, avec les retournements appliqués aux coordonnées de texture ;
  3. **découpe en lots** (`SpriteRun`) : un nouveau lot commence quand la texture change ou que la limite de 16 384 sprites est atteinte.

**Le tri** est un tri par base (radix, 4 passes de 8 bits) sur des clés entières dérivées de la profondeur flottante. Il déplace 8 octets par sprite au lieu des 56 de la structure, et sa mémoire de travail est conservée d'une frame à l'autre. Il a été comparé à `std::stable_sort` : environ 5 fois moins cher à 40 000 sprites en ordre mélangé.

**Pourquoi 16 384** : avec des indices sur 16 bits, un draw call adresse au plus 65 536 sommets, soit 16 384 quads.

### `src/moteur/include/moteur/fixed_timestep.hpp` et `fixed_timestep.cpp`

**Rôle** : transformer des durées de frame variables en un nombre entier de pas de logique fixes. C'était du code de la boucle, il en est extrait pour être testable sans fenêtre ni GPU.

**Contenu** : `FixedTimestep` : `advance(durée)` ajoute le temps écoulé (plafonné, les durées négatives comptent pour zéro) et renvoie combien de mises à jour exécuter ; `alpha()` donne l'avancement entre deux mises à jour, dans [0, 1). `tick_rate()` / `set_tick_rate()` : la fréquence de la logique du processus (de 1 à 1000, 60 par défaut, fixée par l'`Application`) ; `ticks_from_seconds()` : une durée écrite en secondes convertie en ticks, pour qu'un jeu à une autre fréquence garde ses durées.

`interpolate(précédent, actuel, t)` : la valeur à dessiner entre deux pas, calculée `précédent + (actuel − précédent)·t`, **exactement** `précédent` quand rien n'a bougé (ce que `glm::mix` ne garantit pas : une scène figée bougerait d'un arrondi selon `t`, et deux captures identiques différeraient). À utiliser pour toute interpolation entre deux ticks (les caméras le font).

### `src/moteur/include/moteur/frame_stats.hpp` et `frame_stats.cpp`

**Rôle** : résumer une série de mesures (temps CPU par frame, en millisecondes).

**Contenu** : `FrameStats(capacité)` garde les dernières valeurs dans un tampon circulaire (65 536 par défaut). `mean()`, `max()` et `percentile(p)` (rang le plus proche) les résument, et renvoient 0 s'il n'y a aucune valeur. Le maximum et le percentile 99 comptent autant que la moyenne : ce sont les pics qui font saccader une image, pas la moyenne.

### `apps/bac_a_sable/camera2d.hpp` et `camera2d.cpp`

**Rôle** : la caméra 2D des scènes 2D du bac à sable (jalon 2), dans la bibliothèque `bac_a_sable_2d` et le namespace `bac_a_sable`. **Elle ne fait plus partie du moteur** depuis la revue des jalons 1 à 7 (2.1) : le jeu est en 3D (`moteur::Camera3D`). Ses tests restent dans `tests/test_camera.cpp`. La conversion de la souris des points vers les pixels (`window_to_pixels()`), qui était ici, est dans `display.hpp`.

**Ce qu'il faut en retenir** : `position` est le point du monde affiché au **centre** de la fenêtre et `zoom` le nombre de pixels d'écran par pixel du monde. L'**alignement sur les pixels** (`set_pixel_snapping()`, actif par défaut) arrondit la translation écran à un pixel entier : sans lui, un mouvement doux fait scintiller les texels et peut ouvrir des coutures d'un pixel entre les tuiles. Le dessin et le picking utilisent la même translation arrondie. `begin_update()` et `interpolated(alpha)` lissent le mouvement entre deux ticks, pour dessiner **et** pointer. La caméra **ne tourne pas**.

### `apps/bac_a_sable/iso.hpp` et `iso.cpp`

**Rôle** : la projection isométrique 2:1 entre une grille de tuiles et le monde 2D (`IsoProjection`), dans `bac_a_sable_2d` comme la caméra 2D, et pour la même raison. Tests : `tests/test_iso.cpp`.

**Ce qu'il faut en retenir**

- **Convention** : la tuile (i, j) couvre le carré [i, i+1) × [j, j+1) de la grille ; x grandit vers le bas droit de l'écran, y vers le bas gauche. Une tuile est un losange dont le sommet du haut est en `to_world(i, j)` : `monde.x = (tx − ty) × largeur / 2`, `monde.y = (tx + ty) × hauteur_tuile / 2 − hauteur`.
- `tile_at(monde)` arrondit les coordonnées négatives vers le bas (`floor`), pas vers zéro.
- `tiles_in(rectangle du monde, marge)` : un rectangle d'écran devient un losange dans la grille, donc c'est la boîte englobante de ses quatre coins. La **marge** sert aux images plus hautes qu'une tuile (murs, arbres), qui dépassent au-dessus de leur case.

### `src/moteur/private/moteur/debug_ui.hpp` et `debug_ui.cpp` (internes)

**Rôle** : Dear ImGui pour les menus et fenêtres de debug, dessiné par-dessus les sprites. Interne : un jeu n'a qu'à appeler les fonctions `ImGui::` dans son `render()`.

**Architecture**

- Créé par le `Renderer` quand `ApplicationConfig::debug_ui` est vrai (`renderer.debug_ui()` renvoie sinon `nullptr`). Il charge une police TrueType (`debug_ui_font`, Inter pour le bac à sable : la police intégrée d'ImGui n'a pas les accents) et initialise les backends SDL3 (entrées) et SDL_GPU (rendu).
- `Application::run()` lui passe chaque événement ; si ImGui l'utilise (souris au-dessus d'une de ses fenêtres, champ de texte actif), le jeu ne le reçoit pas. `new_frame()` est appelé juste avant `Game::render()`.
- `Renderer::end_frame()` appelle `prepare()` avant le render pass et `render()` à la fin de la passe « compose », après les sprites : l'interface est toujours au-dessus, en pixels de fenêtre, sans copie ni passe en plus.
- `imgui.ini` : aucun par défaut ; l'`Application` le place dans le dossier du joueur quand elle crée les outils de debug, jamais à côté de l'exécutable.

**À savoir** : sans `debug_ui`, rien d'ImGui ne tourne : les mesures et les captures en ligne de commande sont inchangées. Dans la configuration livrée, `debug_ui.cpp` n'est pas compilé du tout.

### `src/moteur/include/moteur/debug_tools.hpp` et `debug_tools.cpp`

**Rôle** : les outils de debug du moteur (jalon 4, partie 8, puis jalon 7), en fenêtres ImGui : `app.debug_tools()`, nul sans `ApplicationConfig::debug_ui` et dans la version livrée. Fenêtres : **Entity Inspector**, **Assets**, **Input**, **Audio**, **Game States**, **Animation**, **Data**, **Console**, **Profiler** ; celles qui sont ouvertes sont gardées dans `imgui.ini` (section `[MoteurTools]`).

**Architecture** : le jeu montre ses mondes et ses piles d'états (`watch()` / `forget()`), met `menu_items()` dans son menu DEBUG et appelle `draw()` dans son `render()`. L'`Application` appelle `between_frames()` : les rechargements et libérations demandés par les boutons ont lieu entre deux frames, jamais pendant. `ComponentInspectors` enregistre la façon de dessiner chaque composant (ceux du moteur avec `add_engine_components()`, ceux du jeu avec `add<T>()`) ; la fonction reçoit une **copie**, remise par `registry.replace`, pour que les signaux d'EnTT (et le cache des entités fixes du monde) voient le changement.

**À savoir** : l'inspecteur n'ajoute, ne retire et ne détruit rien (sauf la case « Hidden ») : un jeu garde des entités et compte sur leurs composants. Modifier un composant rend la simulation non déterministe ; la fenêtre le signale.

### `src/moteur/include/moteur/color.hpp` et `color.cpp`

**Rôle** : les conversions de couleur de la chaîne 3D (jalon 3, partie 6), en C++ pur et testées. Le shader `tonemap.frag.hlsl` fait les mêmes calculs sur le GPU ; ces versions servent de référence aux tests et à convertir des couleurs choisies à l'œil.

**Contenu** : `srgb_to_linear()` et `linear_to_srgb()` (fonctions de transfert sRGB standard, par composante ou sur un `vec3`, bornées à [0, 1]) ; `tonemap_pbr_neutral()` (le tone mapping Khronos PBR Neutral, implémentation de référence de Khronos).

### `src/moteur/tone_mapper.hpp` et `tone_mapper.cpp` (internes)

**Rôle** : la passe plein écran qui convertit l'image HDR linéaire de la scène 3D en couleurs d'écran (exposition, PBR Neutral, encodage sRGB), au début de la passe « compose », ou dans la passe « tonemap » quand FXAA suit. Un triangle plein écran sans tampon de sommets, un échantillonneur linéaire qui agrandit aussi une résolution de rendu réduite. Hors de `include/` : seul le `Renderer` s'en sert. Le même fichier contient `Fxaa` (anticrénelage FXAA sur l'image déjà convertie, jalon 3, partie 12 bis) et `DepthView` (une carte d'ombre à l'écran).

### `shaders/fxaa.frag.hlsl`

**Rôle** : le filtre FXAA (variante « qualité » de FXAA 3.11, réécrite et commentée) : sur une image en couleurs d'écran, repère les bords à fort contraste, les suit jusqu'à leurs extrémités et mélange le pixel avec son voisin de l'autre côté, d'autant plus qu'il est près du bout de la marche d'escalier. Réutilise `tonemap.vert.hlsl` pour le triangle plein écran. MSL exporté.

### `shaders/tonemap.vert.hlsl` et `shaders/tonemap.frag.hlsl`

**Rôle** : le vertex shader fabrique le triangle plein écran à partir de `SV_VertexID` ; le fragment shader lit la scène HDR, applique l'exposition, le tone mapping et l'encodage sRGB (copie fidèle de `color.cpp`). Leur MSL est exporté dans `shaders/generated/msl/`.

### `src/moteur/include/moteur/mesh.hpp` et `mesh.cpp`

**Rôle** : les maillages 3D, sur le CPU puis sur le GPU (jalon 3, partie 4).

**Contenu**

- `Vertex3D` : position, normale, coordonnées de texture, tangente (48 octets). `compute_tangents()` remplit les tangentes par MikkTSpace (v inversé : bitangente vers le haut de l'image, convention des cartes de normales glTF) ; les primitives l'appellent.
- `MeshData` : sommets, indices 32 bits et, pour un maillage skinné, poids des sommets, sur le CPU. Testable sans GPU.
- Primitives centrées sur l'origine (cube, plan, sphère UV). Les triangles sont **antihoraires vus de l'extérieur** (convention glTF).
- `Mesh` : sommets, indices (et poids) sur le GPU, en tranches du pool de géométrie (`GpuSlice`, voir `geometry_pool.hpp`), et la boîte englobante (`Aabb`, `aabb.hpp`). `Mesh::create()` envoie tout et attend le GPU : au chargement, jamais pendant une frame.

**Conventions du monde** : main droite, Y vers le haut, 1 unité = 1 mètre (celles de glTF).

### `src/moteur/include/moteur/material.hpp`

**Rôle** : le matériau PBR du moteur, celui de glTF (métal / rugosité) : facteurs (couleur de base, métal, rugosité, force des normales et de l'occlusion, émissif, double face) et cinq textures facultatives (couleur et émissif en sRGB ; métal / rugosité, normales, occlusion en linéaire). Une texture absente compte comme blanche (plate pour les normales). Depuis la revue des jalons 1 à 7 (3.1), le **test alpha** de glTF (`alphaMode MASK`, `alpha_cutoff` ; 0 : matériau opaque) : un pixel sous le seuil n'est dessiné ni dans la scène ni dans les ombres (`shaders/alpha_test.hlsli`). `casts_shadow` retire un matériau des ombres (une flamme), `fades` estompe en trame un mur qui cacherait le héros.

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

### `shaders/alpha_test.hlsli`, `shaders/shadow_masked*.hlsl` et `shaders/point_shadow_masked*.hlsl`

**Rôle** (revue des jalons 1 à 7, 3.1) : le test alpha des matériaux masqués, le même dans la scène et dans chaque ombre. `alpha_test.hlsli` écarte un pixel dont l'alpha est sous le seuil (seuil 0 : matériau opaque) ; `mesh.frag.hlsl` l'inclut. Les ombres ont leurs variantes : `shadow_masked.vert.hlsl`, `shadow_masked_skinned.vert.hlsl`, `point_shadow_masked.vert.hlsl` et `point_shadow_masked_skinned.vert.hlsl` définissent `MASKED` (et `SKINNED`) et incluent leur shader de base ; `shadow_masked.frag.hlsl` et `point_shadow_masked.frag.hlsl` lisent la texture de couleur et appliquent le test (ce que le seuil retire ne projette pas d'ombre). Le modèle `assets/models/alpha_mask_reference.glb` (écrit par `tools/models/make_alpha_mask_model.py`) le vérifie : un panneau en damier de cases pleines et vides, dont l'ombre doit être le même damier.

### `src/moteur/include/moteur/mesh_renderer.hpp` et `mesh_renderer.cpp`

**Rôle** : dessiner les maillages 3D (jalon 3, parties 4 à 9).

**Architecture**

- **Enregistrer, puis dessiner** : `draw()` enregistre un maillage (ou toutes les pièces d'un `Model`) avec sa matrice monde et son `Material` ; la boîte du maillage est transformée dans le monde à l'enregistrement (`transform_box`), ou donnée déjà calculée pour le décor qui ne bouge pas. La caméra (`set_camera()`) est oubliée à la fin de la frame ; le soleil et l'environnement sont gardés d'une frame à l'autre ; les lumières ponctuelles sont ajoutées à chaque frame (au plus `kMaxPointLights` = 32, les autres comptées dans `RenderStats`).
- **Déroulement d'une frame** : le `Renderer` appelle `prepare()` avant toute passe : cadrage de la carte d'ombre du soleil (`fit_sun_shadow()`), puis, pour chaque passe, `MeshBatcher::build()` (culling contre le frustum de la caméra, ou contre la boîte couverte par la carte d'ombre ; regroupement), et envoi des instances de toutes les passes, à la suite, dans un seul tampon (agrandi en doublant). Ensuite la passe « shadow », la passe « point shadows » et la passe « scene » : **un draw call instancié par lot**. Chaque lot relie sa tranche du tampon d'instances (décalage de la liaison) : l'indice d'instance part de 0 dans le shader, sur tous les backends.
- **Rendu PBR** (Lambert + Cook-Torrance GGX, environnement, soleil, lumières ponctuelles, émissif), en couleurs linéaires HDR. **Les couleurs données (draws, lumières) sont linéaires** : `srgb_to_linear()` convertit une couleur choisie à l'œil. Deux pipelines (simple face ; double face : sans élimination, normale retournée derrière), test et écriture de profondeur (`LESS`), faces avant antihoraires, sans mélange ; échantillonneur trilinéaire et anisotrope (×8). Textures liées seulement quand elles changent.
- **Données** : les uniforms sont poussés une fois par passe (la vue-projection ; les données de la frame : soleil, environnement, lumières, ombres). Tout ce qui est propre à un objet (matrices, couleur, facteurs, émissif, seuil alpha) passe par les **attributs d'instance** (`MeshInstance`). Les structures C++ ne contiennent que des `mat4` et des `vec4`, comme les `cbuffer` HLSL.
- **Ombres des lumières ponctuelles** : les lumières ombrées sont choisies à chaque frame (`select_point_shadows`), chacune a une ligne de 6 cases dans un atlas ; une **signature** (position, portée, et chaque objet dans sa sphère : maillage, matrice, faces) dit si la ligne est encore juste. Une lumière dont rien n'a bougé n'est pas redessinée (`PointShadowSlots`).
- **Débogage et réglages** : le culling se coupe (`set_culling(false)` : tout part au GPU, et l'image ne doit pas changer), des vues de debug remplacent l'éclairage (fil de fer, normales, couleur de base, distance), des boîtes et zones d'ombre vont aux lignes de debug, et `set_cutout()` estompe les murs qui cachent le héros (matériaux `fades`).

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

**Architecture**

- `ModelData` (CPU, testable sans GPU) : les pièces (un `MeshData`, une transformation, un matériau), les matériaux, les images décodées, et pour un personnage le squelette, les skins et les clips (voir `animation_data.hpp`). `load_gltf()` lit un `.gltf` (avec ses fichiers à côté) ou un `.glb`, `parse_gltf()` la même chose depuis la mémoire. Tous les fichiers passent par `read_file()`, donc par les archives montées dans la version livrée.
- Ce qui est lu : les maillages en triangles de la scène par défaut (normales calculées et indices générés s'ils manquent, tangentes par MikkTSpace), la hiérarchie des nœuds (**aplatie** : chaque pièce porte la matrice monde de son nœud), le matériau PBR complet avec ses textures (intégrées, en data URI ou en fichiers ; PNG, JPEG, ou KTX2 avec `KHR_texture_basisu`), le mode alpha `MASK`. Ce qui est refusé : un fichier qui exige une extension non prise en charge (compression Draco, meshopt...). Les primitives qui ne sont pas des triangles sont ignorées avec un message. Toute erreur nomme le fichier.
- `Model` (GPU) : un `Mesh` par pièce, les textures (partagées), les matériaux qui les désignent, la boîte englobante, la mémoire GPU. La **source de textures** facultative de `Model::create()` fournit les images en fichiers séparés : le gestionnaire d'assets y branche son cache, et `GltfOptions::decode_external_images = false` évite alors de les décoder deux fois. Les fichiers lus sont listés, pour le rechargement à chaud.
- `Model::replace_in_place(fresh)` : prend le contenu d'un rechargement en gardant chaque `Mesh` et chaque texture propre au modèle à la même adresse (les scènes gardent des pointeurs vers eux) ; refuse, sans rien changer, un modèle de structure différente.
- **À savoir** : `CGLTF_IMPLEMENTATION` est défini dans ce fichier, et seulement là.

### `tools/models/make_reference_model.py` et `assets/models/reference.glb`

**Rôle** : le **modèle de référence**, qui vérifie toute la chaîne glTF d'un coup d'œil. Script Python sans dépendance, sortie déterministe : `python tools/models/make_reference_model.py` réécrit `assets/models/reference.glb` (8 Ko).

**Contenu du modèle** : un cube de 1 m posé au sol (pour l'échelle), une texture en quatre quadrants sur chaque face (rouge en haut à gauche de l'image, vert en haut à droite, bleu en bas à gauche, blanc en bas à droite : une texture retournée se voit), une flèche rouge vers +X, un repère vert vers +Y et un bleu vers +Z (les couleurs habituelles des axes). La flèche et les repères sont des nœuds **enfants** du cube, placés dans son repère : une hiérarchie ignorée ou mal composée les déplace.

### `tools/models/fetch_test_models.py`, `assets/models/polyhaven/` et `assets/environments/`

**Rôle** : télécharger les modèles 3D de test (Poly Haven, CC0, environ 11 Mo) et l'environnement de test (`studio_small_09_1k.hdr`, 1,6 Mo, HDR équirectangulaire 1K, CC0 : un studio à l'éclairage neutre, pour comparer les matériaux avec Blender). Le bac à sable charge tous les `.hdr` de `assets/environments/`, qui est aussi **hors de Git**. `python tools/models/fetch_test_models.py` passe par l'API officielle de Poly Haven (avec un User-Agent, qu'elle exige), prend la version glTF 1K de chaque modèle avec les fichiers qu'elle référence (`.bin`, textures JPEG), vérifie leurs tailles et ne retélécharge pas ce qui est déjà là. Le dossier `assets/models/polyhaven/` est **hors de Git** (`.gitignore`). Garder la liste du script et `assets/credits.json` en accord.

### `assets_moteur/credits_moteur.json` et `assets/credits.json`

**Rôle** : les crédits, lus par `Credits::load()` (`credits.hpp`) et affichés par la fenêtre « About » du bac à sable. Ils sont en deux fichiers, parce que le moteur et le jeu ne livrent pas les mêmes choses :

- `assets_moteur/credits_moteur.json` : ce que **tout programme du moteur** contient, les bibliothèques (`libraries`) et la police (`fonts`). Il est copié dans chaque programme avec `assets_moteur/` (`moteur_add_game`). À garder en accord avec `MOTEUR_LICENSE_PACKAGES` (`src/moteur/CMakeLists.txt`).
- `assets/credits.json` : les assets du bac à sable et de la tranche (`models`, `environments`, `sounds`). Un jeu a le sien.

Une entrée avec `"platform"` n'est gardée que sur cet OS ; avec `"build"`, seulement quand le moteur est construit avec cette option (`MOTEUR_TRACY`, ou `DEVELOPMENT` pour les outils absents de la version livrée). Les chemins sont relatifs au dossier de l'exécutable. Le format est décrit dans `FORMATS.md`. **Chaque bibliothèque ou asset ajouté au projet doit y être ajouté** ; l'outil de préparation avertit d'un asset livré qu'aucune entrée ne couvre.

### `shaders/skinning.hlsli` et `shaders/*_skinned.vert.hlsl`

**Rôle** (jalon 5) : le skinning linéaire. `skinning.hlsli` lit les palettes de l'image (un storage buffer, `t0` en `space0` : trois lignes `float4` par matrice) et fait la somme pondérée des quatre matrices d'un sommet. `mesh.vert.hlsl`, `shadow.vert.hlsl` et `point_shadow.vert.hlsl` ont une partie `#ifdef SKINNED` (poids en `TEXCOORD13` / `14` pour la scène, `5` / `6` pour les ombres, début de la palette dans le `w` de la ligne émissive de l'instance) ; `mesh_skinned.vert.hlsl`, `shadow_skinned.vert.hlsl` et `point_shadow_skinned.vert.hlsl` définissent `SKINNED` et incluent leur shader de base. MSL exporté comme les autres.

### `shaders/mesh.vert.hlsl` et `shaders/mesh.frag.hlsl`

**Rôle** : les shaders des maillages 3D. Le vertex shader, instancié, reçoit les attributs du sommet (`TEXCOORD0` à `3`) et ceux de l'instance (`TEXCOORD4` à `12` : matrices, couleur, facteurs, émissif) ; il passe la position, la normale et la tangente en espace monde, et le matériau sans interpolation (`nointerpolation`). Le fragment shader fait le **rendu PBR** (voir `mesh_renderer`) : cinq textures de matériau, l'environnement, la carte d'ombre du soleil et l'atlas des ombres ponctuelles (`t0` à `t7`, `space2` ; les deux ombres avec un échantillonneur à comparaison), uniforms de la frame (`b0`, `space3` : entre autres les 48 matrices des faces et, dans `light_color[i].w`, la ligne de l'atlas de chaque lumière, ou −1). Ombres en PCF 3×3 avec décalage le long de la normale. Leur MSL est exporté dans `shaders/generated/msl/` pour le Mac.

### `src/moteur/include/moteur/tilemap.hpp` et `tilemap.cpp`

**Rôle** : la carte d'un niveau, sous forme de grille de cases à plusieurs calques, indépendante du rendu. Logique pure, testée. Elle sert aux collisions, au pathfinding et à la vision (jalon 6, par `NavGrid`), le rendu étant en 3D.

**Architecture** : un `Tileset` décrit les types de tuiles (`TileType` : nom du sprite, `walkable`, `opaque`), numérotés à partir de 1 (`kNoTile` = 0, case vide) ; une `TileMap` ne garde que des `TileId` de 16 bits par case et par calque. `clip()` ramène une plage de cases (`TileRange`) à ce qui tombe dans la carte (la scène isométrique du bac à sable l'utilise avec `IsoProjection::tiles_in()`). `walkable()` est faux hors de la carte ou si une tuile d'un des calques n'est pas praticable.

**Pourquoi les propriétés sont dans le `Tileset`** : la carte reste un tableau de petits nombres (2 octets par case et par calque), et changer une propriété d'un type change toutes ses cases.

### `apps/bac_a_sable/sprite_animation.hpp` et `sprite_animation.cpp`

**Rôle** : les animations d'images des sprites 2D du bac à sable (jalon 2), dans `bac_a_sable_2d` et le namespace `bac_a_sable`. **Elles ne font plus partie du moteur** depuis la revue des jalons 1 à 7 (2.1) : les personnages sont animés en 3D (`moteur::Animator`), et l'interface aura son propre système. Tests : `tests/test_animation.cpp`. `main.cpp` lit `assets/animations.json` avec `AnimationLibrary::load()`.

**Ce qu'il faut en retenir**

- Un clip (`AnimationClip`) est une suite d'images, chacune un nom de sprite et une durée en **ticks entiers**, en lecture `Once`, `Loop` ou `PingPong` (aller-retour sans répéter les images d'extrémité), avec des événements sur des images. `AnimationPlayer` le joue, une fois par tick, à une vitesse variable (0 : pause).
- **Arithmétique entière** : le temps est une `moteur::ClipClock` (`animation_clock.hpp`), en millièmes de tick. Pas de dérive, résultat identique sur les deux OS.
- **Événements** : chaque `advance()` donne les événements des images dont le début tombe dans (temps avant, temps après]. Chacun se déclenche donc exactement une fois, même quand un grand pas saute des images ou des boucles.
- Le fichier JSON est versionné (format en commentaire dans l'en-tête) ; une erreur nomme le fichier et le clip, et `check_regions()` vérifie que chaque image existe dans l'atlas.

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
| `Font::load(renderer, chemin, taille en pixels, jeu de caractères)` | Lit le fichier avec `read_file()`, initialise `stb_truetype`, rasterise chaque caractère du jeu à la taille demandée, et **empaquette les glyphes avec l'outil d'atlas de la partie 5** (`build_atlas`), réutilisé tel quel. Un caractère sans encre (l'espace) garde ses métriques d'avancement mais n'entre pas dans l'atlas. Chaque page est nommée `font glyphs #N` pour les captures de débogage. |
| `measure()`, `draw()` | Partagent la même fonction privée `resolve()`, qui appelle `layout_text()` avec les métriques réelles de la police : elles ne peuvent donc pas donner des résultats incohérents entre eux. |

**Pourquoi rasteriser au chargement, pas à l'exécution** : le jeu de caractères est fixe et connu à l'avance (le français tient dans Latin-1), donc un atlas dynamique rempli glyphe par glyphe n'apporte rien ici. Ce sera à revoir pour des écritures plus larges.

**Pourquoi une seule taille** : agrandir le rendu d'une police plus petite le rend flou. Charger la police à la taille physique finale évite le problème, au prix de devoir recharger la police pour changer de taille.

**Format de la texture de glyphes** : `stb_truetype` produit une image en niveaux de gris (la couverture). Elle est stockée comme du blanc avec cette couverture en alpha, ce qui la fait passer par le même pipeline de sprites que toutes les autres textures (y compris l'alpha pré-multiplié de la partie 5).

**Un piège MSVC découvert ici, mais qui dépasse le texte** : les accents corrompus par la page de codes du compilateur, corrigés par `/utf-8` (voir `cmake/Warnings.cmake`, partie 3).

### `assets_moteur/fonts/Inter-Regular.ttf` et `Inter-OFL.txt`

**Rôle** : la police du moteur (interface de debug, texte du bac à sable et de la tranche). Inter, sous licence **SIL Open Font License 1.1** (le texte de la licence est à côté du fichier). Récupérée depuis le dépôt Google Fonts. Elle est dans `assets_moteur/` (et non plus `assets/fonts/`) parce que tout programme du moteur la reçoit (`moteur_add_game`) ; un programme la charge sous le chemin `fonts/Inter-Regular.ttf` de son dossier `assets/`.

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

### `art/` (sources d'art)

**Rôle** : les images que l'on dessine, **distinctes des ressources produites** : elles ne sont jamais chargées directement, elles passent par l'outil d'empaquetage.

**Contenu** : `art/world/` (les trois losanges du sol isométrique, 64×32, et `pivots.json` qui place leur pivot au sommet, en (32, 0)) et `art/test/` (19 images de test : quatre orbes rognables, des barres de tailles différentes, un carré, un point d'un pixel, un anneau à bords doux et **huit images de marche 48×64 asymétriques**, avec une ombre semi-transparente). L'asymétrie des personnages (bras gauche long et vert, bras droit court et rouge) sert à repérer un retournement fautif. `art/icons/` : l'icône de la tranche (`tranche.png` 1024×1024, la source ; `tranche.ico` pour Windows ; `tranche.icns` pour macOS), dessinée par `tools/package/make_icon.py` en attendant l'art du jeu, et donnée à `moteur_add_game(... ICON .../art/icons/tranche)`.

### `src/moteur/include/moteur/asset_cache.hpp` et `asset_cache.cpp`

**Rôle** : le cache d'un type d'asset, sans GPU (jalon 4, partie 2), testable avec de faux chargeurs.

**Architecture**

- `Asset<T>` : la poignée, `entt::resource<T>` (un `std::shared_ptr`). Se copie, se garde dans un composant. `make_asset(objet)` en fait une pour un objet créé par le jeu (maillage généré), hors de tout cache.
- **La clé d'un fichier** est son chemin normalisé (`/` partout, `.` et `..` résolus, casse gardée) ; un chemin vide, absolu ou qui sort de `assets/` est refusé. Une **casse différente de celle du disque** est refusée aussi, en nommant l'élément fautif : Windows et macOS l'accepteraient, mais pas une archive ni un autre système.
- `AssetCache<T>` charge chaque clé une seule fois, garde la liste des fichiers lus (pour savoir quoi recharger), recharge **en place** (même objet, nouveau contenu ; l'ancien reste si le rechargement échoue) et libère ce qu'aucune poignée ne tient (`collect_garbage()`). Stockage : `entt::resource_cache` ; deux clés de même hachage lèvent une erreur.

### `src/moteur/include/moteur/assets.hpp` et `assets.cpp`

**Rôle** : le gestionnaire d'assets du moteur (`app.assets()`) : tout fichier chargé par le jeu passe par lui.

**Architecture**

- **Un cache par type d'asset** (textures, modèles, environnements, polices, atlas, sons, musiques, squelettes, clips, descriptions d'animation, cartes, effets de particules) : un `Asset<T>` est partagé par tous ceux qui demandent la même clé. Les chemins sont relatifs à `assets/` et lus par `read_file()`, donc dans les archives montées de la version livrée.
- **Remplacements visibles** quand un fichier manque ou est invalide (damier magenta, cube magenta, ciel procédural, bip), avec un log qui nomme le fichier : une scène continue de tourner et le problème se voit. Les types sans remplacement sensé (police, atlas, cartes...) lèvent une exception.
- **Chargement en arrière-plan** (jalon 8, partie 5) : les fonctions `prefetch_*()` lisent et décodent le fichier sur les fils du système de tâches (`jobs.hpp`), sans GPU ni cache ; l'appel normal suivant, sur le fil principal, prend le résultat (en l'attendant s'il le faut) et fait l'envoi au GPU. Les erreurs sont les mêmes que sans préchargement.
- **Rechargement à chaud** (`enable_hot_reload()`, hors version livrée) : surveillance par **efsw** (fil à part), fichiers pris après 200 ms de calme, copiés à côté de l'exécutable puis rechargés en place ; un modèle passe par `Model::replace_in_place`.
- `collect_garbage()` entre deux scènes ; `stats()` et `infos()` (par type : nombre, mémoire GPU, chargements, échecs) pour la fenêtre DEBUG > Assets.

**À savoir** : hors préchargement, le chargement est synchrone et attend le GPU ; jamais au milieu d'une frame.

### `src/moteur/private/moteur/ktx_texture.hpp` et `ktx_texture.cpp` (internes)

**Rôle** : lire les textures KTX2 (jalon 4, partie 3), avec libktx (KTX-Software). Interne depuis la revue des jalons 1 à 7 : un jeu demande une texture au gestionnaire d'assets, qui reconnaît un KTX2 tout seul ; libktx n'apparaît dans aucun en-tête public.

**Contenu**

- `CompressedImage` : format SDL, taille, tous les niveaux (mipmaps) prêts pour le GPU. Données simples, testables sans GPU.
- `is_ktx2()` reconnaît l'identifiant d'un fichier KTX2. `decode_ktx2()` : Basis Universal (UASTC ou ETC1S) transcodé en BC5 (deux canaux : normales), BC7 (le reste) ou RGBA8 (GPU sans ces formats, ou taille non multiple de 4, car BC travaille par blocs de 4×4) ; BC7, BC5 et RGBA8 pris tels quels. La variante sRGB suit `settings.srgb` (le moteur connaît le rôle d'une texture mieux que le fichier). Erreurs avec le nom du fichier.
- `transcode_ktx2_for_shipping()` (jalon 8, partie 6) : pour la version livrée, le Basis Universal transcodé **une fois pour toutes** en BC7 ou BC5, réécrit en KTX2 compressé par Zstandard ; `decode_ktx2()` le prend ensuite tel quel. Appelé par l'outil de préparation (`cook.hpp`).
- `create_compressed_texture()` envoie une `CompressedImage` au GPU, chaque niveau tel quel, par `Renderer::create_texture_levels()`. `Renderer::compressed_formats()` et `set_block_compression()` disent et limitent ce que le GPU lit.

### `src/moteur/include/moteur/input.hpp` et `input.cpp`

**Rôle** : les entrées du joueur sous forme d'**actions** (jalon 4, partie 4) : `app.input()`.

**Architecture**

- **Le jeu lit des actions, jamais des touches** : il déclare ses boutons et ses axes (`add_button`, `add_axis`), puis lit par tick `down`, `pressed`, `released`, `axis`. Un appui bref entre deux ticks est vu par un tick, et un seul. Les liaisons viennent d'un fichier de défauts à plusieurs profils (« clic », « zqsd »...) et du fichier du joueur, qui ne garde que ce qu'il a changé.
- **Sources** (`InputSource`) : une touche **par sa position** (`key:Q` est la touche A d'un AZERTY : les mêmes places sur tous les claviers), un bouton de souris, un cran de molette, un bouton ou une direction d'axe de manette. Le nom affiché est celui du clavier du joueur.
- **Changement des touches par le joueur** : `set_sources()` / `set_axis_sources()` remplacent les sources d'une action, `reset_sources()` / `reset_all_sources()` reviennent aux défauts, `changed_by_player()` dit ce qui diffère des défauts, `actions_using()` trouve les conflits. `begin_capture()`, `capturing()`, `take_captured()` et `cancel_capture()` attendent la prochaine touche pressée pour un écran d'options (la tranche : `ControlsOptionsState`).
- **Enregistrement et rejeu** (`InputRecording`) : les actions tick par tick, par leur nom ; un rejeu de référence redonne la même partie et la même image. `stop_replay()` arrête un rejeu, `replaying()` dit s'il est en cours.
- `Application` ouvre le sous-système manette, transmet les événements (les relâchements même quand ImGui garde l'événement), appelle `end_tick()` après chaque `update()`, et enregistre ou rejoue (`ApplicationConfig::record_input_path`, `replay_input_path`).

### `src/moteur/include/moteur/world.hpp` et `world.cpp`

**Rôle** : le monde d'une scène en **entités** EnTT (jalon 4, partie 5). Une `World` par scène : elle tient le registre (`registry()`), où le jeu ajoute aussi ses propres composants.

**Architecture**

- **Composants du moteur** (des valeurs et des poignées d'assets) : position (`Transform`, et `PreviousTransform` pour une entité qui bouge), hiérarchie (`Parent`, `BoneAttachment`), nom, visibilité (`Hidden`), ce qui se dessine (`MeshComponent`, `ModelComponent`, `Billboard`) et ce qui éclaire (`LightSource`).
- **Systèmes** : `begin_tick()` est le premier système d'un pas fixe (`PreviousTransform` ← `Transform`) ; `collect()` / `submit()` interpolent entre deux ticks et donnent au rendu les maillages, modèles, lumières (les plus proches du point focal) et billboards, **dans l'ordre de création** : l'image ne dépend pas de l'ordre interne d'EnTT. `destroy()` emporte les entités attachées.
- **Cache des entités fixes** : une entité sans `PreviousTransform` ni `Parent` garde sa matrice et ses boîtes (celles de chaque partie d'un modèle comprises). Les signaux d'EnTT l'effacent quand son `Transform` (par `patch` / `replace`), son `Parent`, son maillage ou `Hidden` changent, et quand son maillage ou son modèle est rechargé en place. C'est pourquoi le `Transform` d'une entité fixe se change par `registry.patch<>()` ou `replace<>()` : une écriture directe ne prévient pas le cache.
- **Objets sur les os** (jalon 5, partie 8) : `BoneAttachment`, avec un `Parent` animé, accroche l'entité à un point d'attache (ou un os) de la pose de l'image, sans l'échelle de l'os ; les poses sont calculées une fois par collecte, à la première demande.

### `src/moteur/include/moteur/state_stack.hpp` et `state_stack.cpp`

**Rôle** : les écrans du jeu (titre, chargement, jeu, pause) en **pile d'états** (jalon 4, partie 6).

**Contenu**

- `GameState` : `enter` / `exit` (posé sur la pile, retiré), `covered` / `uncovered` (un état posé par-dessus, retiré), `on_event`, `update`, `render` ; `transparent()` (l'état du dessous est dessiné d'abord) et `blocking()` (celui du dessous ne tourne plus) ; `name()` pour le debug ; `stack()`.
- `StateStack`, qui est un `moteur::Game` : `push`, `pop`, `replace`, `reset` sont des **demandes**, appliquées au début du `update()` suivant (entre deux ticks), jamais pendant un `update` ou un `render`. Le remplaçant est construit avant le départ du remplacé (assets partagés gardés). Mise à jour de bas en haut depuis le premier état bloquant, dessin depuis le premier opaque ; un état figé garde l'`alpha` de sa dernière image. Les événements vont à l'état du dessus ; les états en dessous lisent une `Input` muette (`Input::set_muted`). Construite avec l'`Application`, elle libère les assets inutilisés après chaque lot de transitions et relance l'horloge (`Application::restart_clock`). `visit_drawn` pour les tests.
- `LoadingState` : une liste d'étapes (`label`, fonction), **une par tick**, horloge relancée après chacune, puis la dernière construit l'état qui le remplace ; `progress()`, `label()`, `error()` ; `draw` et `failed` à redéfinir (par défaut : écran vide, et retrait de l'état).

### `src/moteur/include/moteur/audio.hpp` et `audio.cpp`

**Rôle** : le son du jeu (jalon 4, partie 7), avec miniaudio : `app.audio()`.

**Architecture**

- **Une file par frame** : `play()` (un son, ou une variante au hasard, avec groupe, volume, hauteur, position, priorité, boucle, fondu) ne fait que mettre une demande en file ; `update()`, une fois par frame, la vide, fusionne les demandes identiques, choisit les voix (`audio_rules`), déplace les voix placées, libère celles qui ont fini et surveille le périphérique. La logique du jeu ne touche donc jamais le fil audio.
- **Groupes** (musique, effets, ambiance, interface) : volume (curseur, gain au carré) et pause par groupe, volume général, muet sans le focus de la fenêtre. La musique passe d'un morceau à l'autre en fondu enchaîné ; une ambiance se lit en flux.
- Un **limiteur** en fin de mixage (plafond 0,9). Les voix gardent les échantillons qu'elles jouent : un rechargement à chaud est sans danger.
- `mix()` mixe sans périphérique, pour les tests ; `stats()` donne voix, compteurs, crêtes, limiteur, périphérique et tampon (fenêtre DEBUG > Audio).
- `Application` crée l'`Audio` (`ApplicationConfig::audio`), l'appelle à chaque frame, lui passe le focus de la fenêtre, lit et écrit `audio.json` dans le dossier du joueur.

### `src/moteur/include/moteur/audio_rules.hpp` et `audio_rules.cpp`

**Rôle** : les règles de l'audio qui ne demandent aucun périphérique, testées à part.

**Contenu** : `Listener`, `Attenuation`, `place_sound()` (gain et panoramique) ; `VoiceLimits`, `VoiceInfo`, `decide_voice()` (jouer, prendre la place d'une voix, ou ne pas jouer, et pourquoi) ; `merge_requests()` ; `slider_gain()`.

### `src/moteur/include/moteur/sound.hpp` et `sound.cpp`

**Rôle** : les sons comme assets (`assets.sound()`, `assets.music()`).

**Contenu** : `Sound` (échantillons flottants partagés, canaux, fréquence), `Music` (octets compressés partagés, décodés pendant la lecture) ; `decode_sound()`, `open_music()` (erreurs avec le nom du fichier) ; `placeholder_sound()` (le bip d'un son manquant) ; `encode_wav()` (tests).

### `src/moteur/miniaudio.c`

**Rôle** : l'implémentation de miniaudio, avec `stb_vorbis` pour l'OGG (le montage que miniaudio documente), compilée en C dans la bibliothèque `moteur_miniaudio`, sans les avertissements du projet (code tiers).

### `tools/models/fetch_arpg_models.py` et `assets/models/arpg/`

**Rôle** (2026-10-17) : copie les premiers assets du jeu (`../ARPG/assets` : le lampadaire gothique et son `.prop.json`, les sols de pavés humides A et B en dalles de 8 m) dans les assets de test du moteur, hors de Git : chaque `.glb` est déballé en `.gltf` (`.bin` et images à part), puis ses textures sont converties en KTX2 par `tools/textures/convert_gltf_textures.py`. Une copie plus récente que sa source est gardée (`--force` pour tout refaire). La tranche s'en sert (sol, lampadaires : `apps/tranche/demo3d.cpp`, `Demo3D::kPavingA`, `kStreetLamp`) et s'en passe s'ils manquent ; les captures de référence les demandent.

### `tools/models/fetch_test_characters.py` et `assets/models/characters/`

**Rôle** (jalon 5) : télécharger les personnages animés de test dans `assets/models/characters/` (14 Mo, **hors de Git**) : le chevalier du pack KayKit *Adventurers* et le guerrier et le sbire du pack KayKit *Skeletons* (CC0, un même squelette de 41 os, environ 80 clips chacun), une arme de chaque pack dans son propre fichier, les `LICENSE.txt` des packs, et quatre modèles de test de Khronos (`SimpleSkin`, `InterpolationTest`, `RiggedFigure`, `Fox`). Sources épinglées à un commit, taille et SHA-256 de chaque fichier vérifiés ; ce qui est déjà là n'est pas retéléchargé. Crédits dans `assets/credits.json` (section `models`) : `RiggedFigure` et le squelette de `Fox` sont en CC-BY 4.0, leur mention est obligatoire.

### `tools/models/make_skinned_reference_model.py` et `assets/models/skinned_reference.glb`

**Rôle** (jalon 5) : le **modèle de référence animé**, aux valeurs connues d'avance, pour les tests du chargement des squelettes et de la lecture des clips. Script Python sans dépendance (il reprend `Builder` de `make_reference_model.py`), sortie déterministe (8 Ko, versionnée) : une colonne skinnée sur trois os (avec un nœud d'armature qui n'est pas un os, des nœuds listés enfants d'abord, un ordre de `skin.joints` différent de celui des nœuds, une translation du nœud du maillage à ignorer, `JOINTS_0` en 16 bits), un cube rigide accroché à un os, et cinq clips : `Bend` (qui ne commence pas à 0), `Step` (`STEP`), `Cubic` (`CUBICSPLINE`), `Flip` (quaternions de signes opposés) et `Scale`. La docstring donne les positions attendues ; elles ont été vérifiées dans Blender 5.2.

### `src/moteur/include/moteur/animation_data.hpp` et `animation_data.cpp`

**Rôle** (jalon 5) : les squelettes et les clips **tels que le glTF les donne**, en données simples, sans ozz ni GPU : `JointPose` (TRS), `JointData` et `SkeletonData` (os parents d'abord ; `find`, `rest_model_matrices`, `mismatch` qui nomme la première différence entre deux squelettes), `SkinData` (palette et matrices de liaison inverses), `KeyTrack`, `JointTrack` et `ClipData` (clés linéaires depuis 0, une piste par os). `parse_gltf` les remplit (`ModelData::skeleton`, `skins`, `clips`) ; les poids des sommets sont des `VertexSkin` (`mesh.hpp`).

### `src/moteur/include/moteur/skeleton.hpp` et `skeleton.cpp`

**Rôle** (jalon 5) : les squelettes et les clips **prêts à jouer**, construits pour ozz-animation au chargement ; ozz reste dans le `.cpp`. `Skeleton` (vérifie qu'ozz range les os comme `SkeletonData`), `SkeletalClip` (adresse fixe, même après un rechargement), `ClipLibrary` (tous les clips d'un fichier, `clip(nom)`, `mismatch(squelette)`, `replace_in_place`), `PoseSampler` (`sample`, `rest`, et `blend` : plusieurs clips pondérés, avec des poids par os, par le `BlendingJob` d'ozz ; `palette`), `measure_stride` (vitesse au sol d'un clip sur place et instants où les pieds se posent) et `sample_model_pose` (une pose en espace modèle, pour les tests et les outils). Assets : `assets.skeleton(chemin)` et `assets.clips(chemin)`.

### `src/moteur/include/moteur/animation_clock.hpp` et `animation_clock.cpp`

**Rôle** (jalon 5) : `ClipClock`, le temps d'un clip joué, commun à l'`AnimationPlayer` des sprites et à l'`Animator` des squelettes : cycle en ticks, temps et vitesse en millièmes (entiers), lecture unique ou en boucle, marques franchies exactement une fois (les événements).

### `src/moteur/include/moteur/animator.hpp` et `animator.cpp`

**Rôle** (jalon 5) : le composant **`Animator`** (squelette, clips, description ; deux couches, corps entier et haut du corps masqué, qui jouent des clips ou des blend spaces avec des fondus en ticks ; poids en millièmes entiers ; `create`, `play(nom, PlayOptions)`, `stop`, `set_move_speed`, `set_speed`, `clip_name`, `finished`, `playing`, `pose(alpha, requête)` pour le dessin ; les événements des clips en marques de leur horloge), le système **`advance_animators(registry, ticks, &événements)`** (une fois par `update()` ; il liste les **`AnimatorEvent`** franchis, avec la règle du poids), et **`AnimationPose`**, la pose calculée au dessin par `World::collect` (seulement pour les entités visibles, `CollectOptions::view`) et lue par les outils.

### `src/moteur/include/moteur/animation_debug.hpp` et `animation_debug.cpp`

**Rôle** (jalon 5, partie 9) : `draw_skeleton(lignes, monde, squelette, pose, options)`, un squelette en lignes de debug (segments, sphères, axes des os, un os mis en valeur), sur la pose que le monde a dessinée à l'image.

### `apps/bac_a_sable/anim_compare.hpp` et `anim_compare.cpp`

**Rôle** (jalon 5, partie 9) : la scène `--anim-compare [clip secondes]` : le chevalier KayKit seul, dans un clip à un moment, en couleur de base sur fond vert ; avec `--capture`, une image et un JSON pour `tools/blender/compare_pose.py`.

### `tools/blender/compare_pose.py` et `compare_pose_images.py`

**Rôle** (jalon 5, partie 9) : `blender -b --factory-startup --python tools/blender/compare_pose.py -- capture.png.json blender.png` pose le même fichier dans Blender (son import glTF et son animation) et le rend en Workbench avec la même caméra ; `python tools/blender/compare_pose_images.py capture.png blender.png préfixe` compare les silhouettes (recouvrement, pixels d'un seul côté, image de superposition) et les couleurs moyennes.

### `src/moteur/include/moteur/animation_set.hpp` et `animation_set.cpp`

**Rôle** (jalon 5, partie 6) : **`AnimationSet`**, la description des animations d'un personnage lue d'un JSON versionné (format en commentaire dans l'en-tête) : fondu par défaut, réglages par clip (vitesse au sol, fondu, phase, événements), points d'attache (`AttachPoint` : os, position, rotation), blend spaces (triés par vitesse au sol), masques, masque de la couche du haut. Asset : `assets.animation_set(chemin)`, rechargé à chaud.

### `assets/animations/kaykit.json`

**Rôle** (jalon 5, partie 6) : la description des clips KayKit (chevalier, squelettes : même squelette, mêmes clips) : vitesses au sol de `Walking_A` / `_B` et `Running_A` / `_B` mesurées par `measure_stride`, phase de `Running_A` pour que les pieds se posent avec ceux de la marche, fondus (12 ticks par défaut, 6 pour les attaques), événements (pas là où les pieds se posent, impacts des attaques à une main, `death_ground`), blend space `locomotion` (`Idle`, `Walking_A`, `Running_A`), masque `upper_body` (depuis `spine`, rampe de 2). Un test vérifie que les vitesses et les phases correspondent aux clips.

### `src/moteur/include/moteur/map_data.hpp` et `map_data.cpp`

**Rôle** (jalon 6, partie 2) : **`MapData`**, une carte lue d'un JSON versionné (format en commentaire dans l'en-tête) : types de tuiles par nom, légende (un caractère = une tuile par calque et un point nommé facultatif), lignes de caractères (ligne 0 = z 0), description. Donne le `Tileset`, la `TileMap`, les points nommés (`points`, `point`), `tile_id` / `tile_name`, et `to_ascii()` qui réécrit les lignes à l'identique. Asset : `assets.map(chemin)`, rechargé à chaud (`revision()` augmente).

### `src/moteur/include/moteur/nav_grid.hpp` et `nav_grid.cpp`

**Rôle** (jalon 6, partie 2) : **`NavGrid`**, praticable et opaque par case, calques réunis (une case sans tuile est un trou ; dehors : bloqué et opaque), `refresh` / `set` d'une case et `version()` qui change avec elle ; `cell_at()`, `cell_centre()` ; `to_ascii()`.

### `src/moteur/include/moteur/collision.hpp` et `collision.cpp`

**Rôle** (jalon 6, partie 3) : composant **`Collider`** (rayon, couche, masque, poids de poussée, arrêté par les murs) ; `circle_fits`, `push_out_of_walls`, `move_circle` (glissement), `nearest_fit`, `segment_clear` (lancer de cercle) ; le système **`separate_colliders`** (remplit la `SpatialHash`, séparation douce par poids dans un ordre fixe, sortie des murs) ; `plane_position`.

### `src/moteur/include/moteur/spatial_hash.hpp` et `spatial_hash.cpp`

**Rôle** (jalon 6, parties 3 et 6) : **`SpatialHash`**, les cercles du plan rangés en seaux carrés ; `candidates` pour les collisions, `query_circle`, `query_cone`, `query_rect` pour le jeu, triées par distance puis identifiant.

### `src/moteur/include/moteur/pathfinding.hpp` et `pathfinding.cpp`

**Rôle** (jalon 6, partie 4) : **`ClearanceMap`** (dégagement par case, `update_around`), **`find_path`** (A\* à huit directions, coûts entiers, sans coupe de coin, limite de nœuds, destination la plus proche, lissage ; `PathResult`, `PathStatus`), **`FlowField`** (Dijkstra depuis une cible, `next`, `direction`).

### `src/moteur/include/moteur/movement.hpp` et `movement.cpp`

**Rôle** (jalon 6, partie 5) : composant **`Mover`** (vitesse, mode, chemin, état, orientation en vecteur, vitesse réelle) et les systèmes du tick `plan_paths` (budget par tick), `move_movers` (chemin, direct avec glissement, flow fields), `finish_movers` (vitesse réelle, bloqué) ; `facing_rotation`. L'ordre des systèmes d'un tick est écrit en tête de l'en-tête.

### `src/moteur/include/moteur/visibility.hpp` et `visibility.cpp`

**Rôle** (jalon 6, parties 6 et 8) : `raycast` (parcours de grille, opaque ou non praticable, rien par un coin), `line_of_sight` (symétrique), **`FieldOfView`** (shadowcasting symétrique, pentes entières), **`ExploredMap`** (cases vues).

### `src/moteur/include/moteur/particles.hpp` et `particles.cpp`

**Rôle** (jalon 6, partie 7) : **`ParticleEffect`** (description JSON des émetteurs, format en commentaire dans l'en-tête ; asset `assets.particle_effect(chemin)`, rechargé à chaud) et **`ParticleSystem`** (un par scène : `play`, `move`, `stop`, `kill`, `update` au temps réel, `draw` en billboards, `add_lights`, budget global, `stats`).

### `assets/maps/` et `tools/maps/make_slice_map.py`

**Rôle** (jalon 6) : les cartes de test (`zigzag`, `cul_de_sac`, `salle_portes`, `labyrinthe`, `piliers`, `passage_etroit`, `arene`, `ile`) et la carte de la tranche `tranche.json`, écrite d'abord par `python tools/maps/make_slice_map.py` (mêmes salles que la démo 3D construite en code ; version 1 sans objets), puis enrichie dans l'éditeur (version 2 : décor et monstres en objets) et par `tools/maps/place_slice_lamps.py` (un lampadaire par salle : tuile `lampadaire`, point du même nom ; relançable, même résultat). Toutes sont relues par les tests.

### `assets/effects/`, `assets/particles/` et `tools/textures/make_particle_textures.py`

**Rôle** (jalon 6, partie 7) : les effets de particules (`fire`, `brazier_fire`, `sparks`, `blood`, `dust`, `death_smoke`, `magic_trail`, `magic_circle`) et leurs textures, générées par `python tools/textures/make_particle_textures.py` (numpy et Pillow ; graines fixes, rien à créditer).

### `assets/input/world.json`

**Rôle** (jalon 6) : les touches de la scène « World » (clic, ZQSD, flèches, molette, clic droit, M, B, E, Espace, P ; manette).

### `apps/bac_a_sable/world_test.hpp` et `world_test.cpp`

**Rôle** (jalon 6) : la scène de test « World » (DEBUG > Engine tests ; `--world [carte]`, `--menu-test 11`) : une carte de `assets/maps/` en 3D, un héros (chemins, glissement), des monstres (vue, flow fields, collisions, coups), le brouillard, les murs tramés, les effets, les overlays (grille, dégagement, cercles, chemins, flow field, champ de vision, cône, rayon), les temps des systèmes ; un mur posé ou retiré au clic droit, la carte rechargée à chaud. Sans menu : `--world-auto`, `--world-crowd N`, `--world-fires N`, `--world-map nom` (avec `--report`, `--capture`, `--freeze-after`).

### `src/moteur/include/moteur/data_reader.hpp` et `data_reader.cpp`

**Rôle** (jalon 7, partie 2) : **`DataReader`**, la lecture typée d'un objet JSON par le jeu (champs obligatoires ou par défaut, bornes, choix, couleurs, listes, objets imbriqués, références) ; **`DataIssues`** / **`DataIssue`** : les problèmes avec leur chemin `fichier > ligne > champ`, erreurs et avertissements (champs inconnus) ; **`DataRef<T>`** : une référence vers une ligne, par identifiant, résolue à la liaison.

### `src/moteur/include/moteur/data_table.hpp` et `data_table.cpp`

**Rôle** (jalon 7, partie 2) : **`DataTable<T>`** (une table : dossier de JSON, défauts, héritage à un parent, lignes à indices stables, `sorted()`, `find`, `at`, `ref`), **`DataLinker`** (résolution des références), **`DataTables`** (`Application::data()` : enregistrement, `load_all`, rechargement d'un fichier ou d'une table refusé en cas d'erreur ou de ligne retirée, `compile` en `.mdat` CBOR, source JSON ou compilée). Format en commentaire dans l'en-tête.

### `apps/tranche/sandbox_data.hpp` et `sandbox_data.cpp`, `assets/data/`

**Rôle** (jalon 7 ; dans `tranche_commun` depuis le jalon 8, partie 12) : les tables de la tranche, lues aussi par le bac à sable : `personnages` (`CharacterData` : héros et créatures de la tranche), enregistrées par `register_sandbox_tables` ; leurs fichiers dans `assets/data/<table>/`. Le bac à sable les vérifie et les compile (`--check-data`, `--compile-data DOSSIER`, `--compiled-data DOSSIER`, voir son `main.cpp`) ; `tranche_cook` les compile pour la version livrée.

### `src/moteur/include/moteur/console.hpp` et `console.cpp`

**Rôle** (jalon 7, partie 3) : la **console** : commandes d'**outil** (immédiates) et de **jeu** (en file, exécutées au début du tick suivant et enregistrées dans les rejeux), découpage des lignes, historique, complétion, sortie colorée, lignes du journal reçues de tous les fils ; commandes intégrées (`help`, `set`, `get`, `reset`, `toggle`, `vars`, `echo`, `clear`). Les scènes enregistrent les leurs (`add`) et les retirent (`remove`).

### `src/moteur/include/moteur/variables.hpp` et `variables.cpp`

**Rôle** (jalon 7, partie 3) : les **variables** nommées (`fog.enabled`, `time.scale`) : types, bornes, défaut, aide ; `Archive` (gardées dans `variables.cfg` si elles diffèrent du défaut), `Logic` (changées par la file des commandes de jeu) ; valeurs lues avant leur déclaration appliquées à celle-ci.

### `src/moteur/include/moteur/log_file.hpp` et `log_file.cpp`

**Rôle** (jalon 7, partie 3) : le **journal** `log.txt` du dossier du joueur (heure, niveau, texte ; vidé à chaque ligne, pour qu'un plantage le laisse complet ; le précédent gardé en `log.1.txt` ; rotation à la taille), la recopie de chaque message dans la console, et ses dernières lignes gardées pour les rapports de plantage (`record_crash_log_line()`).

### `src/moteur/include/moteur/profiler.hpp` et `profiler.cpp`

**Rôle** (jalon 7, partie 4) : le **profiler** maison : `MOTEUR_PROFILE("zone")`, arbre des zones par image, historique, résumé et totaux, image figée, capture Chrome Trace (`profile start` / `stop`), éteint par `profile.enabled 0` ; avec `MOTEUR_TRACY`, les mêmes zones vont aussi à Tracy.

### `src/moteur/include/moteur/world_debug.hpp` et `world_debug.cpp`

**Rôle** (jalon 7, partie 4) : les **overlays du monde** commandés par des variables (`debug.grid`, `debug.clearance`, `debug.colliders`, `debug.paths`, `debug.flowfield`, `debug.view`), dessinés par toute scène qui donne ses sources.

### `src/moteur/include/moteur/file_io.hpp` et `file_io.cpp`

**Rôle** (jalon 7, partie 5) : **écriture atomique** (`write_file_atomic` : temporaire vidé sur disque puis remplacement, copie `.bak` sur demande, chemins UTF-8), lecture d'un fichier entier, existence, `file_time` (date de modification en ticks de l'horloge des fichiers : un changement d'un autre programme se voit, même dans la seconde), `crc32`.

### `src/moteur/include/moteur/save_file.hpp` et `save_file.cpp`

**Rôle** (jalon 7, partie 5) : **`SaveGame`** (fichier MSAV : en-tête, sections versionnées, JSON ou CBOR, CRC-32, fichier abîmé refusé), **`SaveMigrations`** (passages de version en chaîne), **`SaveSlots`** (emplacements d'un dossier, liste par date, copie de secours, vignettes).

### `src/moteur/include/moteur/world_save.hpp` et `world_save.cpp`

**Rôle** (jalon 7, partie 6) : **`PersistentId`** et **`ComponentSerializers`** (registre des composants à sauvegarder, ceux du moteur, sauvegarde dans l'ordre des identifiants, chargement par une fonction du jeu, composants oubliés signalés), aides JSON (`to_json_value`, `vec3_from_json`…). L'état d'un `Animator` est dans `animator_state.cpp`.

### `src/moteur/include/moteur/map_document.hpp` et `map_document.cpp`

**Rôle** (jalon 7, parties 7 et 8) : **`MapDocument`**, la carte en cours d'édition (tuiles par calque, types, légende, points, objets, connecteurs), ses outils (ligne, rectangle, remplissage, redimensionnement), ses **actions** pour annuler et refaire, ses vérifications et son **écriture stable** au format v2. Sans GPU, testé.

### `src/moteur/include/moteur/animation_graph.hpp` et `animation_graph.cpp`, `src/moteur/animator_graph.cpp`

**Rôle** (jalon 7, partie 9) : le **graphe d'animation** en données (paramètres, couches, états, transitions à conditions structurées), lu dans la clé `graph` d'un `AnimationSet` ; `animator_graph.cpp` : son état et son évaluation dans l'`Animator` (`start_graph`, `set_parameter`, `fire`, `graph_state`, historique), rechargement à chaud par les noms.

### `apps/tranche/map_objects.hpp` et `map_objects.cpp`

**Rôle** (jalon 7, partie 8) : les **types d'objets** des cartes de la tranche et du bac à sable (rocher, arbre, caisse, tonneau, monstre, lumière, effet, marqueur), leurs propriétés typées et leurs défauts, et `build_object` qui en fait des entités (scène « World », éditeur).

### `apps/bac_a_sable/map_editor.hpp` et `map_editor.cpp`, `apps/bac_a_sable/editeur_main.cpp`

**Rôle** (jalon 7, parties 7 et 8) : l'**éditeur de cartes**, scène du bac à sable (DEBUG > Engine tests > Map editor, `--editeur [carte]`) et programme `editeur` (`editeur_main.cpp` : `editeur [carte | --new] [--top] [--run-seconds S] [--capture F] [--self-test] [--no-vsync]`). Vue 3D, outils de tuiles, points, objets et connecteurs, palette, types, vérifications, annuler / refaire, enregistrement atomique dans les sources avec détection d'un fichier changé ailleurs, copie de récupération, menu « Test » (depuis le départ, F5, ou depuis le pointeur, F6) qui joue la carte dans la scène « World », auto-test.

### `tools/tests/check_references.py`, `tests/data/references.json` et `tests/data/demo3d_replay.json`

**Rôle** (jalon 7, partie 11) : les **vérifications de référence** en une commande (`python tools/tests/check_references.py [build] [--record] [--quick]`) : captures de référence (démo 2D, démo 3D, démo 3D sur son rejeu `demo3d_replay.json`, tranche jouable) comparées à `references.json` (par plateforme), **test d'exactitude** des sauvegardes (A = B = C), auto-test de l'éditeur. `--record` écrit les hachages trouvés comme références de la plateforme, une fois le changement expliqué.

### `apps/bac_a_sable/animation_test.hpp` et `animation_test.cpp`

**Rôle** (jalon 5) : la scène de test « Animation » (DEBUG > Engine tests ; `--menu-test 10`) : les personnages de test côte à côte, entités d'un `World` avec un `Animator` chacune, jouant un clip de leur fichier au tick et dessinées entre deux ticks, le squelette en lignes de debug, les parties rigides sur leur os ; lecture, **tick suivant** en pause, vitesse, clip, temps en ticks, pose de repos, **pose de liaison**, **axes des os**, un **os** choisi et la **vue de ses poids**, interpolation (à comparer avec `--tick-rate 10`). Lancée aussi par `--animation`. Avec `--skinned N` (partie 10), un test de charge à la place : N personnages KayKit en grille, mouvements, phases et fondus variés, coût de l'animation affiché et, avec `--report`, imprimé (`--skinned-zoom F`, `--no-shadows`). Devant la rangée, le **chevalier des mélanges** (partie 6) fait le tour d'une ellipse : vitesse de déplacement (blend space `locomotion`), marche sur place pour comparer, fondus vers n'importe quel clip, attaque du haut du corps, poids affichés, dérive du pied posé mesurée ; ses pas et ses coups sonnent sur les événements (partie 7), listés dans le panneau ; il tient une épée d'un fichier à part et une torche qui éclaire sur les points d'attache de ses mains, et deux repères montrent où irait une barre de vie (partie 8).

### `tools/audio/fetch_test_sounds.py`

**Rôle** : télécharger les sons et musiques de test dans `assets/audio/` (10 Mo, hors de Git, tous en CC0 : Kenney et OpenGameArt), en vérifiant le SHA-256 de chaque fichier ; ne garde que les fichiers utilisés des paquets de Kenney, avec leur `License.txt`. Les crédits sont dans `assets/credits.json` (section `sounds`).

### `src/moteur/include/moteur/process_memory.hpp` et `process_memory.cpp`

**Rôle** : `process_memory_bytes()`, la mémoire que le processus s'est réservée (Windows : *private bytes* ; macOS : *physical footprint* ; Linux : mémoire résidente). Pour comparer deux moments d'une même exécution (fuites), pas deux systèmes.

### `assets/input/demo3d.json`

**Rôle** : les touches par défaut de la démo 3D, en deux profils : « clic » (clic gauche pour se déplacer, A Z E R T et clic droit pour les compétences) et « zqsd » (Z Q S D pour se déplacer, A E R F et clic droit), plus la manette. Les touches y sont nommées par leur position sur un QWERTY US (`key:Q` = la touche A d'un AZERTY).

### `tools/input/make_slice_replay.py` et `tests/data/slice_replay.json`

**Rôle** : le rejeu de référence de la tranche jouable (jalon 4, partie 9), écrit par un script plutôt qu'enregistré à la main : 480 ticks (titre, « Jouer », marche, coups, pause, « Reprendre »), actions par leur nom. `python tools/input/make_slice_replay.py` le réécrit. Capture : `bac_a_sable --states --replay-input tests/data/slice_replay.json --freeze-after 340 --run-seconds 9 --pixel-size 1280 720 --capture slice.png` (le programme `tranche` prend les mêmes options, et `tools/package/check_package.py` rejoue ce fichier sur la tranche installée).

### `tools/textures/convert_gltf_textures.py`

**Rôle** : encoder les textures des glTF en KTX2 UASTC et les déclarer avec `KHR_texture_basisu`.

**Usage** : `python tools/textures/convert_gltf_textures.py [fichiers .gltf ou dossiers] [--ktx outil] [--force]` (par défaut, tout `assets/models/`). Rôle de chaque image déduit des matériaux (couleur en sRGB, normales en `--normal-mode`, données en linéaire), `.ktx2` à côté de l'image, image d'origine gardée en repli. L'outil `ktx` est cherché dans les dossiers de build (vcpkg l'installe), puis dans le PATH. Idempotent.

### `tools/ci/verify.py`

**Rôle** (jalon 8, partie 2) : **la vérification du moteur** en une commande, les mêmes étapes que lancera une intégration continue hébergée : `python tools/ci/verify.py [--quick] [--configs debug,release] [--no-references] [--no-msl] [--no-example] [--no-analysis] [--no-asan] [--no-docs] [--no-package] [--keep-going]`.

Pour chaque configuration (debug et release ; `--quick` : release seulement) : configuration avec le preset de l'OS et les **avertissements en erreurs**, build complet, tests unitaires, vérification et compilation des tables de données. Puis, une fois : `examples/jeu_minimal` construit et lancé **de l'extérieur** du moteur, comme un jeu qui l'a en sous-module ; sous Windows, export du MSL comparé à celui versionné ; les captures de référence, le test d'exactitude des sauvegardes et l'auto-test de l'éditeur (`check_references.py`) ; l'analyse statique (`static_analysis.py`) ; les tests sous AddressSanitizer (preset `<os>-asan`) ; la référence de l'API (`check_docs.py`) ; la configuration livrée avec le paquet de la tranche, installé, joué sur le rejeu de référence et désinstallé sous Windows (`check_package.py` ; sauté par `--quick`).

**À savoir** : le script prépare son environnement lui-même (l'environnement de développement de Visual Studio, le CMake et le Ninja de CLion, qui partagent ses dossiers de build, et le vcpkg de `MOTEUR_VCPKG_ROOT`, de CLion ou de `VCPKG_ROOT`, dans cet ordre). Sous Windows, chaque commande tourne dans sa propre console cachée : les builds sont les mêmes depuis un terminal, un IDE ou un programme sans console. Les autres scripts l'importent pour cet environnement. Code de sortie 0 quand tout passe.

### `tools/ci/static_analysis.py`

**Rôle** (jalon 8, partie 8) : l'**analyse statique** du moteur : clang-tidy avec les vérifications de `.clang-tidy`, sur chaque source de `src/moteur` (`--all` : aussi `apps/`), avec les commandes de compilation d'un build (`compile_commands.json`). `python tools/ci/static_analysis.py [BUILD_DIR] [--files MOTIF] [--all]`. Les avertissements sont groupés par vérification ; code de sortie 1 s'il y en a. clang-tidy : celui du PATH, ou celui qu'installe Visual Studio.

### `tools/measures/measure.py`

**Rôle** (jalon 8, partie 4) : les **scènes de mesure** du moteur, les mêmes sous Windows et macOS : `python tools/measures/measure.py [BUILD_DIR] [--size W H] [--seconds S] [--output DOSSIER] [--compare DOSSIER] [--scenes fight,crowd,crossing,loading]`. Chaque scène lance le bac à sable en Release, sans synchronisation, et écrit son rapport (`--report-json`) : `fight` (toutes les créatures autour du héros, feux et lumières : le pire cas d'un combat d'ARPG), `crowd` (800 personnages animés), `crossing` (le héros traverse toute la carte), `loading` (titre, chargement, jeu, pause, titre, dix fois : temps de chargement, mémoire, fuites). `fight` et `crossing` sont relancées avec les temps GPU. `--compare` signale tout chiffre plus mauvais de plus de 10 % (code de sortie 1). Les budgets du jalon 8 sont vérifiés sur la machine minimale (le Mac M3), affichés ailleurs. Rapports dans `build/measures/<date>_<os>/`.

### `tools/fuzz/fuzzer.py`

**Rôle** (jalon 8, partie 8) : le **fuzzing** des lecteurs de fichiers : `python tools/fuzz/fuzzer.py [--seconds S] [--targets map,effect,...] [--parallel] [BUILD_DIR]`. Chaque programme de `tests/fuzz` (cibles `map`, `effect`, `animation`, `save`, `ktx2`, `gltf`, `table`, `bindings`, `image`, `archive`) tourne un moment sur un corpus amorcé par les fichiers du projet (et par une sauvegarde, des tables compilées et une archive que fabrique le build Release) ; ce qui le fait planter est gardé. BUILD_DIR : le build de fuzzing (preset `<os>-fuzz`). Corpus et trouvailles dans `build/fuzz/<cible>/`. Une trouvaille, une fois corrigée, devient un cas de `tests/test_robustness.cpp`. Code de sortie 1 si quelque chose a été trouvé.

### `tools/crash/read_report.py`

**Rôle** (jalon 8, partie 8) : lire un **rapport de plantage** avec les symboles de sa version : chaque ligne `#NN module+0xDECALAGE` de la pile reçoit sa fonction, son fichier et sa ligne. `python tools/crash/read_report.py RAPPORT.txt SYMBOLES`, où SYMBOLES est le dossier archivé avec la version (`build/<preset>/packages/symbols/<jeu>-<version>-<os>/`). Sous Windows, `dbghelp.dll` (fourni avec Windows) ; sous macOS, `atos` (outils en ligne de commande de Xcode). Le script avertit si le commit du rapport n'est pas celui des symboles.

### `tools/docs/`

**Rôle** (jalon 8, partie 9) : la **référence de l'API**. `Doxyfile.in` (la configuration de Doxygen, remplie par `cmake/Docs.cmake` : les en-têtes publics, en anglais, tout avertissement en échec), `mainpage.md` (la page d'accueil de la référence), et `check_docs.py` : `python tools/docs/check_docs.py [EN-TÊTE...] [--output DOSSIER]` lance Doxygen sans passer par CMake et affiche ses avertissements ; avec des noms d'en-têtes, seulement les leurs, pour travailler quelques fichiers à la fois. Code de sortie 1 s'il y a un avertissement (ce qu'exigent la cible `moteur_docs` et `verify.py`).

### `tools/package/check_package.py` et `tools/package/make_icon.py`

**Rôle** (jalon 8, parties 10 à 12) : `check_package.py` vérifie le **paquet Windows** d'un jeu comme un joueur le reçoit : `python tools/package/check_package.py [BUILD_DIR] [--game NOM] [--keep] [--replay FICHIER --expect HACHAGE] [--freeze-after TICK] [--run-seconds S] [--pixel-size W H]`. Le programme ne dépend d'aucune DLL du runtime Visual C++ et a ses informations de version ; l'installateur, lancé en silence pour l'utilisateur seul dans un dossier temporaire, met les fichiers, le raccourci et l'entrée de désinstallation en place, puis s'installe une seconde fois par-dessus (une mise à jour) ; le jeu installé, rejoué, donne **la même capture** que le build de développement (pour la tranche : le rejeu et la capture de référence `slice`) ; la désinstallation retire tout sauf le dossier du joueur ; l'archive portable contient les mêmes fichiers. Les réglages et les touches du joueur sont mis de côté pendant le test. `make_icon.py` dessine l'icône de la tranche (`art/icons/`, avec Pillow).

### `tools/cook/main.cpp`

**Rôle** (jalon 8, partie 6) : `moteur_cook`, l'outil de préparation des assets d'un jeu **sans tables de données** : un simple appel à `moteur::cook_main()`. Un jeu qui a des tables écrit le sien (comme `tranche_cook`). C'est l'outil par défaut de `moteur_package()`.

### `examples/jeu_minimal/`

**Rôle** (jalon 8, partie 3) : le plus petit programme construit sur le moteur : une fenêtre, un cube éclairé que les flèches (ou le stick gauche) font tourner, un bip sur Espace (ou le bouton A), Échap pour quitter. `main.cpp` (`jeu_minimal [--run-seconds S]`) ; `assets/` : ses touches (`input/jeu_minimal.json`), son bip (`audio/bip.wav`), ses crédits (`credits.json`) et son manifeste de livraison (`livraison.json`). Son `CMakeLists.txt` et ses presets montrent comment un jeu consomme le moteur (voir la partie 3 et `GUIDE.md`).

### `src/moteur/include/moteur/paths.hpp` et `paths.cpp`

**Rôle** : savoir où chercher les fichiers du programme, et les lire.

**Architecture**

- `base_path()` est le dossier de l'exécutable (avec un séparateur final), `asset_path("nom.png")` donne `<dossier de l'exécutable>/assets/nom.png`. Les shaders sont cherchés sous `base_path()` aussi.
- **Un seul point de lecture** : `read_file()` (et `read_text_file()`) lit tout un fichier dans un `FileData`, qui libère les octets lui-même. **Tous** les chargements d'assets et de shaders passent par elle. C'est ce qui permet aux **archives** (`archive.hpp`, jalon 8, partie 7) de remplacer le dossier : un fichier du dossier du programme est cherché d'abord dans les archives montées (`mount_archive()`, `mount_archives()` ; une archive montée plus tard cache les entrées d'une plus ancienne : un correctif), puis sur le disque. Dans la version livrée, `set_archives_only(true)` : ce qui n'a pas été préparé n'existe pas ; les fichiers du joueur restent sur le disque.
- `read_file()` passe par `SDL_LoadFile` (chemins UTF-8 sous Windows, ce que `fopen` ne fait pas) et peut être appelée depuis les fils du système de tâches ; monter une archive se fait au démarrage, avant les chargements.

**Pourquoi relatif à l'exécutable** : le programme fonctionne quel que soit le dossier de travail. Dans un bundle `.app`, `SDL_GetBasePath()` désigne `Contents/Resources/`, où le paquet macOS range `archives/` et `licenses/` (voir `cmake/PackageMacos.cmake`).

### `src/moteur/include/moteur/archive.hpp` et `archive.cpp`

**Rôle** (jalon 8, partie 7) : le format d'**archive** du moteur (`MPAK`) : les assets et les shaders du jeu livré, rangés dans quelques fichiers `.pak` au lieu de milliers. `write_archive()` les écrit (l'outil de préparation), `Archive::open()` en lit l'index, et `read_file()` les lit une fois montées (`paths.hpp`).

**Choix**

- Un en-tête, un **index trié par chemin** (chemin, position, tailles, compression, CRC-32), puis les données, chacune **alignée sur 4 096 octets**. Un changement d'un asset ne décale pas le reste : les plateformes comme Steam mettent à jour par blocs.
- Compression **Zstandard** par entrée, sauf pour les formats déjà compressés (`archive_should_compress()`, selon l'extension : `.ktx2`, `.ogg`, `.mp3`, `.png`...).
- Le hachage de l'index est vérifié à l'ouverture, le CRC-32 de chaque entrée à sa lecture : une archive abîmée donne un message, jamais des données fausses. Une archive d'une autre version est refusée.
- Les chemins gardent leur casse et sont comparés exactement ; deux chemins qui ne diffèrent que par la casse sont refusés à l'écriture (Windows et macOS ne les distingueraient pas sur le disque).
- Une `Archive` ouverte ne tient que son index (aucun fichier ouvert) : le fichier doit rester en place. Ses fonctions sont `const` et sûres entre fils.

Le format est décrit dans `FORMATS.md`.

### `src/moteur/include/moteur/cook.hpp` et `cook.cpp`

**Rôle** (jalon 8, partie 6) : la **préparation des assets** (« cooking ») pour le jeu livré, des sources de développement à des données prêtes à charger, pour que le jeu livré ne convertisse rien au démarrage.

**Ce que fait `cook()`**

- choisit les fichiers à livrer d'après un **manifeste de livraison** (JSON, globs `*` et `**`, voir `glob_match()`), plus ce dont ils dépendent (tampons et images d'un glTF, textures d'un effet) ; une dépendance absente est une erreur, un fichier source que rien ne livre est listé (orphelins) ;
- transcode les textures KTX2 en Basis Universal vers BC7 ou BC5, une fois pour toutes (`transcode_ktx2_for_shipping()`) ;
- compile les tables de données en `.mdat`, par une fonction du jeu (le moteur ne connaît pas ses tables) ;
- copie les shaders compilés ;
- **relit chaque fichier préparé avec le chargeur du moteur** (modèle, carte, effet, description d'animation, texture) : ce qui ne se charge pas est une erreur maintenant, pas chez un joueur ;
- avertit des assets livrés qu'aucune entrée des crédits ne couvre (licences) ;
- ne refait que ce qui a changé (une empreinte par fichier dans `cook_state.json` : contenu de la source et version de l'outil) ;
- range le tout dans des **archives** : `archives/<nom>.pak` selon les groupes du manifeste, le reste et les shaders dans `common.pak`. Les archives ne sont écrites que s'il n'y a eu aucune erreur.

`cook_main()` est la ligne de commande d'un outil de préparation (`--sources`, `--manifest`, `--output`, `--shaders`, `--force`, `--orphans`) : `tools/cook/main.cpp` (`moteur_cook`, pour un jeu sans tables) et `apps/tranche/cook_main.cpp` (`tranche_cook`) n'en sont qu'un appel. Les anciennes clés françaises du manifeste (`inclure`, `exclure`, `donnees`...) sont encore lues, avec un avertissement. Le format du manifeste est décrit dans `cook.hpp` et `FORMATS.md`.

### `src/moteur/include/moteur/crash.hpp` et `crash.cpp`

**Rôle** (jalon 8, partie 8) : les **rapports de plantage**. Une fois installé (par l'`Application`, avec `ApplicationConfig::crash_reports`), un plantage (accès mémoire invalide, `abort`, assertion échouée, exception C++ que personne n'attrape) écrit dans `crash_reports/` du dossier du joueur un rapport `.txt` daté (versions du moteur et du jeu, OS, GPU et pilote, mémoire, événement, pile du fil en module + décalage, 200 dernières lignes du journal) et, sous Windows, un petit minidump `.dmp`. Puis il dit au joueur où est le rapport (message du jeu, dans sa langue ; le rapport, lui, est en anglais, pour les développeurs).

**Choix et pièges**

- Le gestionnaire tourne dans un processus abîmé : **tout ce qu'il peut préparer l'est à l'installation** (chemins, lignes de contexte, message), et il en fait le moins possible. Sous Windows : filtre des exceptions non gérées, `SIGABRT`, `std::terminate` ; ailleurs : les signaux `SIGSEGV`, `SIGBUS`, `SIGILL`, `SIGFPE`, `SIGABRT`, sur une pile de signal à part.
- `report_fatal_error()` écrit le même rapport pour une erreur fatale qui n'est pas un plantage (GPU perdu), puis laisse le programme se terminer proprement.
- `set_crash_context()` ajoute des lignes au rapport (le GPU, l'assertion échouée) ; `record_crash_log_line()` garde les dernières lignes du journal (le journal l'appelle).
- Le programme livré n'a pas de symboles : `tools/crash/read_report.py` retrouve fonctions, fichiers et lignes avec les symboles archivés de **cette** version.
- `crash_now()` (et la commande de console `crash`, l'option `--test-plantage` de `tranche`) provoque un plantage pour vérifier la chaîne. `MOTEUR_CRASH_SILENT` supprime le message, pour les vérifications automatiques.

### `src/moteur/include/moteur/assert.hpp` et `assert.cpp`

**Rôle** (revue des jalons 1 à 7) : les **contrats du code**, vérifiés là où ils sont écrits. `MOTEUR_ASSERT` : une erreur de programmation, vérifiée dans les builds de développement (Debug et Release) et **retirée de la version livrée** (la condition n'y est pas même évaluée : elle ne doit rien faire d'autre). `MOTEUR_VERIFY` : vérifié partout, version livrée comprise (ce qui protège les données du joueur, ou ce qui corromprait la mémoire). `MOTEUR_UNREACHABLE` : une branche impossible. Un échec écrit le message, l'expression, le fichier et la ligne au journal, s'arrête dans le débogueur sous Windows, puis `abort()` : le rapport de plantage dit quelle vérification a échoué. **Une assertion n'est jamais une façon de traiter un mauvais fichier ou une mauvaise entrée** : ceux-là sont refusés avec un message.

### `src/moteur/include/moteur/display.hpp` et `display.cpp`

**Rôle** (jalon 8, partie 8) : les **réglages d'affichage du joueur** (`DisplaySettings`) : fenêtré ou plein écran fenêtré, écran, synchronisation, limite d'images, résolution de rendu (0,5 à 1 ; l'interface reste nette), anticrénelage, qualité des ombres. Lus de `display.json` dans le dossier du joueur avec `ApplicationConfig::display_settings` (l'ancien `affichage.json` et ses clés françaises sont encore lus), appliqués **sans redémarrer** par `Application::apply_display()`. Aussi : `displays()` (les écrans branchés), `shadow_caps()` (ce que chaque qualité d'ombres plafonne), les noms techniques des valeurs (le jeu choisit ses libellés), et `window_to_pixels()`.

**Choix** : pas de plein écran exclusif. Avec les modèles de présentation de Windows 10 / 11, une fenêtre sans bord a la même latence, sans l'écran noir au changement de fenêtre, et macOS n'a pas ce mode. Le coût de la 3D se règle par la résolution de rendu. `from_json()` ne lève jamais d'exception : une valeur inconnue ou du mauvais type garde son défaut, avec un avertissement.

### `src/moteur/include/moteur/credits.hpp` et `credits.cpp`

**Rôle** : lire les **crédits** d'un programme (bibliothèques, polices, modèles, environnements, sons) pour son écran « À propos » ou ses crédits : `Credits::load()` lit `credits_moteur.json` (le moteur) puis `credits.json` (le jeu), par `read_text_file()`, donc dans les archives aussi. Une entrée marquée d'un OS ou d'une option de build (`MOTEUR_TRACY`, `DEVELOPMENT`) n'est gardée que là où elle s'applique. Ne lève jamais d'exception : un fichier absent ou faux ajoute une ligne à `error`. Voir aussi `assets_moteur/credits_moteur.json`.

### `src/moteur/include/moteur/jobs.hpp` et `jobs.cpp`

**Rôle** (jalon 8, partie 5) : un petit **système de tâches** (`JobSystem`, et `jobs()`, celui du processus) : une file unique (premier entré, premier sorti), des fils de travail (autant que de cœurs moins un), `submit()`, `wait()` (le fil qui attend exécute d'autres tâches en attendant) et `parallel_for()`. Il sert au chargement en arrière-plan (`Assets::prefetch_*()`) et à découper les longs calculs (le préfiltrage d'un environnement).

**Règles du moteur** : la logique du jeu n'y tourne jamais (l'ordre de ses calculs ne doit pas dépendre des fils : déterminisme des rejeux et des sauvegardes) ; une tâche ne touche ni au GPU, ni aux caches d'assets, ni aux registres ECS : elle calcule des données que le fil principal utilise ensuite ; une tâche qui lève une exception est finie, et `wait()` relance son exception.

### `src/moteur/private/moteur/cbor_check.hpp` et `cbor_check.cpp` (internes)

**Rôle** (jalon 8, partie 8) : vérifier la **structure d'un document CBOR** (tables compilées, sauvegardes) avant de le lire : chaque élément complet dans les octets, imbrication limitée à 128 niveaux. `nlohmann::json::from_cbor` lit les éléments imbriqués par récursion : un fichier abîmé ou fabriqué de quelques centaines de kilo-octets de tableaux imbriqués faisait déborder la pile et tuait le programme avant tout message (trouvé par le fuzzing). Cette vérification est itérative, en une passe.

### `src/moteur/include/moteur/image.hpp` et `image.cpp`

**Rôle** : lire un fichier PNG en mémoire.

**Contenu**

- `Image` : largeur, hauteur et pixels en **RGBA 8 bits**, alpha non pré-multiplié, lignes stockées de haut en bas.
- `decode_image(données, taille, nom)` : décode une image déjà en mémoire (par exemple une texture intégrée à un `.glb`).
- `load_image(chemin)` : lit le fichier avec `read_file()` (qui gère les chemins UTF-8 sous Windows, contrairement au `fopen` de stb, et lit les archives montées), le décode avec stb_image et renvoie une `Image`. Lève une exception si le fichier manque ou n'est pas un PNG valide.
- Seuls le PNG et le JPEG sont activés (`STBI_ONLY_PNG`, `STBI_ONLY_JPEG`), ce qui allège le code compilé. Le JPEG sert aux textures de nombreux modèles glTF.
- `premultiply_alpha(image)` : multiplie la couleur de chaque pixel par son alpha, en arrondissant au plus proche. Les pixels opaques sont laissés tels quels, et les pixels transparents deviennent noirs. C'est la forme que le GPU mélange et filtre correctement : avec un alpha normal, filtrer un pixel voisin d'un pixel transparent y traîne la couleur (sans signification) de ce dernier et laisse un halo. Le renderer l'applique à la création d'une texture ; les fichiers sur disque restent en alpha normal.

**À savoir** : c'est dans ce fichier que l'implémentation de stb est compilée (`STB_IMAGE_IMPLEMENTATION`). Il ne faut la définir qu'à un seul endroit.

### `apps/bac_a_sable/blender_compare.hpp` et `blender_compare.cpp`

**Rôle** : la scène comparée avec Blender (jalon 3, partie 7), dans le menu (« Blender comparison ») ou avec `--blender-compare`. Les quatre modèles Poly Haven et deux sphères (plastique blanc mat, or poli) en rang, posés sur `y = 0`, éclairés **par l'environnement de test seul** (ni soleil, ni lumière ponctuelle, ni sol), vus par une caméra fixe (de face, 15° au-dessus de l'horizon, champ de 30°), sur un fond gris neutre. Avec `--capture image.png`, écrit la 10ᵉ image et `image.png.json` : caméra, environnement, tone mapping, place de chaque objet, matériaux des sphères, dans les conventions du moteur.

### `tools/blender/compare_render.py` et `compare_images.py`

**Rôle** : la comparaison avec Blender.

1. `bac_a_sable --blender-compare --pixel-size 1280 720 --aa msaa4 --run-seconds 2 --capture engine.png`
2. `blender -b --factory-startup --python tools/blender/compare_render.py -- engine.png.json blender.png --gpu` (Blender 4.2 ou plus, pour la vue « Khronos PBR Neutral » ; version de référence : 5.2.2, `C:\Program Files\Blender Foundation\Blender 5.2\blender.exe`, celle du MCP Blender ; Cycles, sur le GPU avec `--gpu`, ou EEVEE avec `--eevee`) : la même scène d'après le JSON (mêmes glTF aux mêmes places, mêmes sphères, environnement seul avec la même intensité, même caméra, même tone mapping), fond transparent. Les positions passent de Y vers le haut à Z vers le haut comme à l'import glTF ; l'environnement n'a pas besoin d'être tourné (même lecture équirectangulaire).
3. `python tools/blender/compare_images.py engine.png blender.png engine.png.json comparaison` (Pillow) : image côte à côte, différence ×4, et la couleur moyenne de chaque objet dans les deux images.

Différences attendues : Blender trace la lumière (les objets s'ombrent eux-mêmes), le moteur approche l'éclairage d'environnement (harmoniques sphériques, image préfiltrée).

### `apps/tranche/demo3d.hpp` et `demo3d.cpp`

**Rôle** : la scène de démonstration du jalon 3 (partie 11), dans le menu (« 3D demo ») ou avec `--demo3d`, devenue le **monde de la tranche jouable**. Elle est dans `tranche_commun` depuis le jalon 8, partie 12. Elle porte aussi les scènes de mesure (`Options::measure`, `--mesure` du bac à sable, voir `tools/measures/`), la sauvegarde et le chargement de l'état du monde (`--save-at`, `--load`) et le monde construit depuis les objets de la carte `tranche.json`.

**Contenu** : la carte 100×100 de la démo 2D construite en 3D (sol fusionné par blocs de 10×10, murs de 1,5 m, un brasero-torche par pièce), le décor (rochers, arbres, caisses, tonneaux glTF), les créatures de la démo 2D (demi-tour devant les murs) avec barres de vie, le soleil et les torches avec ombres, la caméra (déplacement, zoom, suivi). Commandes : les actions de `assets/input/demo3d.json` (profils « clic » et « zqsd », manette). Depuis la partie 5 du jalon 4, le monde est une `moteur::World` : sol, murs et décor sont des entités fixes, chaque créature une entité qui bouge (`Walker` et `Health`, composants propres à la démo) avec un corps et une tête attachés, chaque torche une entité (flamme, lumière, halo), l'anneau de sélection est attaché à la créature choisie, et la marque de destination est cachée (`Hidden`) quand il n'y en a pas. Réglages, tableau des passes et temps de collecte du monde dans le panneau du menu (`draw_controls()`) ; `world collection` en fin de ligne de commande. Options : `--seed`, `--map`, `--creatures`, `--decor`, `--tile-floor`, `--point-shadows`, et celles des captures. `Demo3D::declare_actions()` déclare les actions (et celles des menus) et lit les touches, pour la démo et pour les états qui l'entourent. `Options::hero` (la tranche jouable, jalon 4 partie 9) : un héros à lui, suivi par la caméra, qui frappe les créatures (clic sur une créature à portée, ou compétence 1), avec pas, impacts et ambiance. Depuis la partie 11 du jalon 5, si les personnages de test sont là : le héros est le chevalier KayKit animé (locomotion à sa vitesse, épée d'un fichier à part sur `right_hand`, attaque sur la couche du haut, dégâts à l'événement `impact`, pas aux événements), les créatures des squelettes KayKit animés (touché sur la couche du haut, mort au sol), les barres au-dessus de la boîte de chacun (`Stature`).

### `apps/tranche/states_demo.hpp` et `states_demo.cpp`

**Rôle** : les écrans de la tranche jouable (jalon 4, partie 9, né du test des états de jeu de la partie 6), sur une `moteur::StateStack` ; le programme `tranche` et le bac à sable (« Playable slice (game states) » dans le menu, ou `--states`) les lancent.

**Contenu** : le titre (« Jouer », « Continuer », « Charger », « Options », « Quitter »), le chargement par étapes (`DemoLoadingState`, une barre de progression), le jeu (le monde `Demo3D` en mode héros), la pause (transparente et bloquante : le monde reste dessiné, assombri et figé ; « Reprendre », « Sauvegarder », « Options », « Retour au titre »), les emplacements de sauvegarde (`SlotsState`, trois emplacements avec vignette, dans `sauvegardes/` du dossier du joueur ; sauvegarde automatique toutes les deux minutes de jeu, variable `save.auto`), et les options (`OptionsState` : affichage `DisplayOptionsState`, son `AudioOptionsState`, touches `ControlsOptionsState`). Un changement d'écran ou de mode d'affichage est annulé s'il n'est pas confirmé dans les 15 secondes. Les menus lisent des actions (`menu_up`, `menu_down`, `menu_confirm`) : une manette et un rejeu les pilotent aussi bien que la souris. Ils sont dessinés par `SliceUi`, sans ImGui ; ImGui ne sert qu'au panneau de debug, retiré de la version livrée.

**Vérifications** : `--states-cycles N` (ou le réglage « Automatic cycles » de la page de la scène, dans le bac à sable) : un pilote automatique enchaîne titre, jeu, pause, jeu, pause, titre N fois ; à chaque retour au titre il note les assets, les objets GPU et la mémoire du processus, et vérifie que le monde n'a pas bougé pendant les pauses. `--capture` : une image de la première pause ; avec `--freeze-after N`, celle du jeu gelé après N de ses ticks (la capture de référence, avec `--replay-input tests/data/slice_replay.json`). La mesure du chargement est écrite (`Slice: loaded in ... ms`).

### `apps/tranche/slice_ui.hpp` et `slice_ui.cpp`

**Rôle** (jalon 8, partie 12) : l'interface propre de la tranche, ses menus et ses panneaux dessinés avec le texte et les sprites du moteur (`screen_sprites()`), comme les dessinera un jeu livré. Un panneau se construit de haut en bas, centré (`begin_panel()`, lignes, boutons, cartes, barres, `end_panel()`, qui dessine le fond derrière : les sprites sont triés par profondeur). Les tailles sont en points, multipliées par la densité de l'écran, pour que l'interface garde sa taille sur un écran Retina.

### `apps/tranche/main.cpp`

**Rôle** (jalon 8, partie 12) : le programme **`tranche`**, ce que sera l'exécutable de l'ARPG : titre, chargement, jeu, pause, sauvegardes, options, sans scène de test. Il règle son `ApplicationConfig` (titre, dossier du joueur `moteur/tranche`, réglages d'affichage du joueur, messages au joueur en cas de plantage ou de GPU perdu, en français). Dans la configuration livrée, il n'a ni ImGui, ni rechargement à chaud, ni profiler, et lit ses assets préparés et en archives.

**Ligne de commande** : `tranche [--run-seconds S] [--no-vsync] [--pixel-size W H] [+set NOM VALEUR] [--replay-input FICHIER] [--freeze-after TICKS --capture FICHIER.png] [--states-cycles N] [--report-json FICHIER]`, et `--test-plantage acces|abort|exception|assertion|gpu` pour vérifier les rapports de plantage. Ces options servent aux vérifications de la livraison (la même capture de référence que la tranche du bac à sable, les mesures) ; un joueur n'en a jamais besoin.

### `apps/tranche/cook_main.cpp` et `assets/livraison_tranche.json`

**Rôle** (jalon 8, parties 6 et 12) : **`tranche_cook`**, l'outil de préparation de la tranche : `moteur::cook_main()` avec les tables de la tranche, qu'il compile en binaire. Il est lancé par la cible `tranche_package`. `assets/livraison_tranche.json` est son **manifeste de livraison** (`cook.hpp`) : la carte, les personnages, le tonneau, les animations, les effets, les sons et musiques utilisés, la police, les touches, les crédits ; les tables du dossier `data` ; et deux archives à part, `audio` et `personnages` (le reste va dans `common.pak`).

### `apps/bac_a_sable/audio_test.hpp` et `audio_test.cpp`

**Rôle** : le test de l'audio (jalon 4, partie 7), dans le menu (« Audio ») ou avec `--audio` (`--audio-burst` : une rafale au départ ; avec `--run-seconds`, un bilan en fin de ligne de commande).

**Contenu** : une carte vue de dessus (l'auditeur, les distances d'atténuation, les sources ; un clic y joue un impact), un feu en boucle qui tourne autour de l'auditeur, pas, impacts, clics d'interface, musiques du titre et du jeu, ambiance en flux, volumes et pauses par groupe, rafale de 200 impacts en une seconde ; le panneau montre le périphérique, son tampon, les voix, les compteurs, les crêtes et le limiteur.

### `apps/tranche/sandbox_scene.hpp`

**Rôle** : ce que les scènes du menu partagent. `SandboxScene` (un `moteur::Game` avec `draw_controls()`, `stop_requested()` et `uses_escape()`, qui laisse Échap à la scène comme touche de pause) : le menu tient la scène en cours par cette interface. `Random` : le petit générateur à graine des scènes. `demo_wall(i, j)` : les murs des deux démos (2D et 3D), pour qu'elles aient la même carte.

### `apps/bac_a_sable/main.cpp`

**Rôle** : le programme d'essai du moteur : le menu des scènes de test et toute la ligne de commande du bac à sable. Il sert de terrain d'expérimentation et de banc de mesure, pas de futur jeu (le modèle d'un jeu est la tranche, `apps/tranche/`).

**Architecture**

- Classe `Sandbox` (un `Game`) : le programme lancé **sans argument** (ou avec `--menu` ; `--menu-test N` lance en plus la scène N, comptée depuis 0). Barre de menus ImGui : **DEBUG > Engine tests** (« All scenes... » puis chaque scène), les outils de debug du moteur (`DebugTools::menu_items()`), **DEBUG > Home** ; **Help > About**. Écran d'accueil, page de sélection (description, réglages et bouton **Start** de chaque scène) et panneau de la scène en cours (**Stop the test**, **Home**). Échap remonte d'un cran (scène → sélection → accueil). Une scène est créée et détruite dans `update()`, jamais pendant une frame : le chargement attend le GPU, et les textures d'une scène sont utilisées par la frame en cours d'enregistrement. Si une scène ne peut pas démarrer, l'erreur s'affiche sur la page de sélection.
- Les scènes, dans l'ordre du menu : « Sprites and stress test », « Isometric map », « Sprite atlas », « Text and fonts », « Demo scene », « 3D rendering: first meshes » (toutes dans `TestScene`, choisies par ses `Options`), puis « 3D demo » (`Demo3D`), « Blender comparison », « Playable slice (game states) » (`StatesDemo`), « Audio », « Animation », « World », « Map editor ». Lancée depuis la ligne de commande (`standalone`), Échap et `--run-seconds` quittent le programme ; lancée depuis le menu, ils demandent seulement l'arrêt de la scène.
- **Fenêtre « About »** : les bibliothèques, la police et les assets de test, lus par `Credits::load()` dans `credits_moteur.json` puis `credits.json`, avec le texte complet de chaque licence à la demande (son chemin, relatif au dossier de l'exécutable, est dans l'entrée : `licenses/<paquet>.txt` pour une bibliothèque) et la présence de chaque fichier.
- **Scènes 2D** (jalon 2, avec `bac_a_sable_2d`) : `--sprites N` (test de charge : N sprites qui rebondissent, positions tirées d'un générateur à graine fixe, donc la même scène sur chaque OS), `--no-batching` (un draw call par sprite : même image), `--depth` (le sprite principal, enregistré **en premier**, n'est au-dessus que si le tri fonctionne), `--iso` (carte de tuiles par l'atlas `world`, tuile sous la souris, `--interleave` qui casse le batching), `--atlas` (les sprites de l'atlas `test` à leur pivot, à comparer aux PNG sources), `--text` (accents, retour à la ligne, alignements ; tailles mesurées écrites sur la sortie standard), `--demo` (la démo 2D : carte 100×100, murs, 3 000 créatures qui jouent le clip `walk`, tri isométrique par `tuile.x + tuile.y`).
- **Scène « 3D rendering: first meshes »** (`--3d`, `--ortho`, `--camera X Z`, `--view-height H`) : un sol en damier, des cubes, une sphère, des piliers, tous les modèles glTF de `assets/models/` en rang, la case sous la souris, une boîte « personnage » envoyée au clic et suivie par la caméra.
- **Options communes** : `--capture chemin.png` avec `--freeze-after N` (la première frame gelée en PNG, pour comparer des pixels), `--run-seconds N`, `--no-vsync`, `--tick-rate N` (fréquence de la logique ; à 10, l'interpolation se voit), `--pixel-size W H`, `--report` et `--report-json` (mesures), `--gpu-timing`, `--record-input` / `--replay-input`, `--no-hot-reload`, `--no-audio`. Les données : `--check-data`, `--compile-data`, `--compiled-data`. La tranche : `--states`, `--slice` (le monde seul, sans les écrans), `--mesure` (scène de mesure), `--save-at`, `--load`. Chaque scène a les siennes (voir sa partie).

**À savoir** : au jalon 2, `set_view_projection()` valait pour toute la frame et empêchait de mêler sprites du monde et texte d'écran ; c'est la raison de `screen_sprites()`, le second `SpriteRenderer` en pixels de fenêtre. Autre piège : `renderer.stats()` n'est valide qu'entre `end_frame()` et le `begin_frame()` suivant ; `update()` (qui tourne juste avant le `begin_frame()` de l'itération) la met en cache dans `last_stats_` pour l'affichage.

### `tests/` (tests unitaires)

**Rôle** : vérifier automatiquement la logique qui n'a pas besoin de GPU ni de fenêtre ; ce qui passe par le GPU est vérifié par les captures de référence (`tools/tests/check_references.py`). Le détail de chaque cas est dans le nom du `TEST_CASE`, écrit comme une phrase.

**Contenu**

- `CMakeLists.txt` : voir la partie 3. `main.cpp` : le point d'entrée de doctest (`DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`), défini à un seul endroit.
- Un fichier `test_<domaine>.cpp` par partie du moteur :

| Domaine | Fichiers |
|---|---|
| Programme et plateforme | `test_fixed_timestep`, `test_frame_stats`, `test_state_stack`, `test_input` (actions, profils, fichier du joueur, changement des touches, rejeu), `test_display` (points et pixels, réglages d'affichage), `test_jobs` (système de tâches), `test_user_paths` (dossier du joueur au nom accentué, disque plein, dossier où rien ne s'écrit : les données du joueur restent intactes) |
| Rendu | `test_sprite_batcher`, `test_sprite_region`, `test_atlas_builder`, `test_image`, `test_utf8`, `test_text_layout`, `test_color`, `test_camera3d`, `test_mesh`, `test_mesh_batcher`, `test_billboard_batcher`, `test_debug_lines`, `test_environment`, `test_shadow`, `test_anti_aliasing`, `test_geometry_pool` (tranches, alignement, réutilisation différée), `test_model` |
| Assets et données | `test_asset_cache`, `test_ktx_texture`, `test_archive` (aller-retour, casse, archive abîmée, montage), `test_cook` (globs, clés du manifeste), `test_data_table`, `test_save_file`, `test_world_save`, `test_robustness` (fichiers abîmés, dont ceux trouvés par le fuzzing, `tests/data/fuzz/`) |
| Monde | `test_tilemap`, `test_map_data`, `test_world_grid` (collisions, chemins, requêtes, vision, déplacements, et mesures), `test_particles`, `test_world`, `test_map_document` |
| Animation | `test_ozz`, `test_skeleton`, `test_animator`, `test_blending`, `test_attachment`, `test_animation_debug`, `test_skinning`, `test_animation_graph` |
| Audio | `test_audio` (règles, décodage, mixage sans périphérique) |
| Outils | `test_console`, `test_profiler`, `test_debug_tools` (filtre et inspecteurs de composants) |
| Revue de l'API | `test_review_core`, `test_review_platform`, `test_review_readers`, `test_review_render`, `test_review_world`, `test_review_world2`, `test_review_apps` : les défauts trouvés en documentant l'API (jalon 8, partie 9), chacun gardé comme test de non-régression |
| 2D du bac à sable | `test_camera`, `test_iso`, `test_animation` : compilés seulement quand `bac_a_sable_2d` est construite |

- `tests/data/` : les fichiers des tests (rejeux de référence, `references.json`, fichiers trouvés par le fuzzing).
- `tests/fuzz/fuzz.cpp` : les programmes de fuzzing (un par lecteur de fichiers, choisi par `MOTEUR_FUZZ_TARGET`), lancés par `tools/fuzz/fuzzer.py`. Un lecteur peut refuser un fichier (c'est son rôle) ; un plantage, une boucle sans fin ou un rapport d'AddressSanitizer est un bogue, et le fichier qui l'a causé devient un cas de `test_robustness.cpp`.

**Les valeurs de test sont des puissances de deux** (1/64, 1/4, etc.) partout où c'est possible, exactes en virgule flottante : les résultats ne dépendent pas d'arrondis, donc sont identiques sur les deux OS.

**Lancer les tests** : `ctest` depuis le dossier de build, ou directement `moteur_tests` (`-tc="nom"` pour un seul). Un benchmark, ignoré par défaut, mesure l'empaquetage d'atlas (`moteur_tests -tc="atlas packing benchmark" --no-skip`). Les premiers tests ont été vérifiés en cassant volontairement ce qu'ils protègent (plafond de rattrapage, profondeurs négatives du tri, inverse de la projection isométrique, alignement sur les pixels) : ils échouent, comme ils le doivent.

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

**Contenu** : dossiers de build (`build/`, `out/`, `cmake-build-*/`), paquets vcpkg installés (`vcpkg_installed/`), fichiers d'IDE (`.idea/`, `.vs/`, `.vscode/`, `*.user`), fichiers système (`.DS_Store`, `Thumbs.db`), et les assets de test téléchargés par les scripts de `tools/` (`assets/models/polyhaven/`, `assets/environments/`, `assets/audio/`, `assets/models/characters/`), hors de Git tant que Git LFS n'est pas décidé.

**Règle** : tout ce qui se régénère (build, dépendances, téléchargements) est ignoré. Seuls les fichiers sources et de configuration sont versionnés.

### `.gitattributes`

**Rôle** : rendre les fins de ligne cohérentes entre Windows (CRLF) et macOS (LF), et éviter des diffs parasites.

**Contenu**

- `* text=auto` : Git normalise les fins de ligne des fichiers texte.
- `*.sh` en LF et `*.bat` en CRLF : ces scripts exigent une fin de ligne précise pour fonctionner.
- Extensions binaires (`png`, `jpg`, `ogg`, `wav`, `glb`, `pak`, `ktx2`) marquées `binary` : Git ne les modifie jamais.

**Quand le modifier** : ajout de nouveaux formats binaires.

### `README.md`

**Rôle** : première page lue par quelqu'un (ou toi dans six mois) qui clone le projet.

**Contenu** : prérequis par OS, build avec CLion et en ligne de commande, tests et vérification (`tools/ci/verify.py`), paquets pour les joueurs, menu du bac à sable, mesures, atlas, animations de sprites, modèles, crédits, texte, débogage, problèmes connus (MSVC), shaders, structure du projet, choix de configuration (triplets, cible macOS, architecture). Il dit aussi comment générer la référence de l'API (`moteur_docs`).

**Règle** : tout ce qui est nécessaire pour construire depuis un clone propre doit y figurer.

### `moteur_doc/`

**Rôle** : la documentation en français. Elle se partage en quatre :

| Fichier | Contenu |
|---|---|
| `ROADMAP.md` | la feuille de route : stack, jalons, état |
| `JALON_1_FONDATIONS.md` à `JALON_8_CONSOLIDATION.md` | chaque jalon : objectif, parties, décisions, résultats mesurés |
| `REVUE_JALONS_1_A_7.md` | la revue des jalons 1 à 7 au regard d'un moteur professionnel (jalon 8) ; les tests `tests/test_review_*.cpp` en viennent |
| `GUIDE.md` | comment écrire un jeu avec le moteur, sans lire ses sources |
| `FORMATS.md` | chaque fichier que le moteur lit ou écrit : clés, versions, anciennes clés encore lues |
| `FICHIERS_DU_PROJET.md` | ce fichier : l'architecture et le build, fichier par fichier |
| `TEST_MAC.md` | la liste de ce qu'il faut valider sur le Mac |

**La référence de l'API** (le *quoi* de chaque classe et fonction) n'est pas dans ces fichiers : elle est générée par Doxygen depuis les commentaires `///` des en-têtes publics, avec la cible `moteur_docs` (voir `cmake/Docs.cmake`). Ce fichier-ci garde le *pourquoi* : le rôle de chaque fichier, les choix d'architecture et les pièges.

---

## 6. Dossiers générés (à ne pas modifier)

Ces éléments apparaissent après la configuration, le build ou un outil, et ne sont pas versionnés.

| Élément | Origine | Contenu |
|---|---|---|
| `build/<preset>/` | CMake / preset | Fichiers Ninja, objets compilés, exécutables |
| `build/<preset>/vcpkg_installed/` ou `vcpkg_installed/` | vcpkg | Les bibliothèques compilées et leurs en-têtes, plus l'outil `shadercross` et DXC sur Windows |
| `build/<preset>/shader_tools/` | `Shaders.cmake` | Copie privée de `shadercross.exe` et de ses DLL |
| `build/<preset>/src/moteur/build_info.cpp` | `BuildInfo.cmake` | Le commit et sa date, réécrits seulement quand ils changent |
| `<dossier de l'exécutable>/shaders/` | Build | Shaders compilés (`.dxil`) ou copiés (`.msl`), chargés au démarrage |
| `<dossier de l'exécutable>/assets/` | Build | Copie de `assets_moteur/` puis des assets du programme |
| `<dossier de l'exécutable>/licenses/` | Build | Textes des licences des bibliothèques (`moteur_add_licenses`) |
| `build/<preset>/docs/html/` | cible `moteur_docs` | La référence de l'API (Doxygen) |
| `build/<preset>/package/<jeu>/` | cible `<jeu>_package` | Travail du paquet : `cook/` (assets préparés : `assets/`, `shaders/`, `archives/`, `cook_state.json`), dossier d'installation |
| `build/<preset>/packages/` | cible `<jeu>_package` | Les paquets des joueurs, et `symbols/<jeu>-<version>-<os>/` (programme et symboles de chaque version, pour les rapports de plantage) : **à archiver à chaque version livrée** |
| `build/measures/`, `build/fuzz/`, `build/fuzz_seeds/`, `build/docs_check/` | outils de `tools/` | Rapports de mesure, corpus et trouvailles du fuzzing, avertissements de Doxygen |
| `compile_commands.json` | CMake | Commandes de compilation, lues par CLion, clangd et `static_analysis.py` |
| `cmake-build-*/` | CLion | Dossiers de build propres à CLion selon le profil |
| `.idea/` | CLion | Paramètres du projet dans l'IDE |

En cas de comportement incohérent, **supprimer le dossier de build et reconfigurer** règle la majorité des problèmes.

**Hors du dépôt, le dossier du joueur** (`Application::preferences_directory()`, `SDL_GetPrefPath(organization, application)`) reçoit tout ce qu'un programme écrit : `log.txt` et `log.1.txt` (le journal), `crash_reports/` (rapports de plantage), `display.json` (réglages d'affichage), `audio.json` (volumes), `variables.cfg` (variables archivées), `imgui.ini` et `profiles/profile.json` (outils de développement), et ce que le jeu choisit (la tranche : `sauvegardes/`, `demo3d_bindings.json`). Les anciens noms (`affichage.json`, `journal.txt`, `plantages/`) ne sont plus écrits ; seul `affichage.json` est encore lu. Le détail est dans `GUIDE.md` (dossier du joueur) et `FORMATS.md`.

---

## 7. Fichiers à venir

Ils seront ajoutés au fil des jalons.

| Fichier / dossier | Quand | Rôle |
|---|---|---|
| `src/moteur/core/`, `platform/`, `renderer/` | Quand le nombre de fichiers le justifiera | Sous-dossiers pour ranger la boucle de jeu, la plateforme, le rendu (les fichiers sont pour l'instant à plat dans `src/moteur/`) |
| `third_party/` | Si besoin | Code tiers qu'on ne peut pas construire par un port vcpkg (les bibliothèques absentes du registre passent par `ports/`, comme ozz-animation) |
