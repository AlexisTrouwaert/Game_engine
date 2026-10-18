# Guide : écrire un jeu avec le moteur

Retour à la [roadmap](ROADMAP.md). Écrit au [jalon 8](JALON_8_CONSOLIDATION.md) (partie 9), pour le moteur **0.8.0**.

Ce guide explique comment les morceaux du moteur s'assemblent pour écrire un jeu, l'ARPG d'abord (caméra isométrique fixe, monde en 3D, interface en 2D), **sans lire les sources du moteur**. Il ne décrit pas chaque fonction : pour cela, voir la **référence de l'API**.

**Référence de l'API** (Doxygen, en anglais, tirée des commentaires `///` des en-têtes publics `src/moteur/include/moteur/*.hpp`) :

- depuis le dossier du moteur : `cmake --build build/windows-release --target moteur_docs`, puis ouvrir `build/windows-release/docs/html/index.html` (la cible existe quand Doxygen est installé et que le moteur est le projet principal) ;
- ou, sans CMake : `python tools/docs/check_docs.py` (la référence et `warnings.txt` dans `build/docs_check/`).

Les **décisions** (le *pourquoi*) sont dans les documents des jalons, cités à chaque chapitre ; l'architecture et le rôle de chaque fichier dans [FICHIERS_DU_PROJET.md](FICHIERS_DU_PROJET.md). Les formats de fichiers (cartes, tables, animations, effets, liaisons, sauvegardes, archives, fichiers du joueur) sont décrits dans [FORMATS.md](FORMATS.md), avec leur version et un exemple minimal ; l'en-tête qui lit chaque format (`map_data.hpp`, `data_table.hpp`, `input.hpp`, `cook.hpp`, `save_file.hpp`, `particles.hpp`, `animation_graph.hpp`, `display.hpp`) le résume aussi.

## Sommaire

0. [Conventions](#0-conventions)
1. [Démarrer un jeu](#1-démarrer-un-jeu)
2. [La boucle](#2-la-boucle)
3. [Le monde](#3-le-monde)
4. [Assets et données](#4-assets-et-données)
5. [Les systèmes](#5-les-systèmes) : rendu, animation, audio, entrées, monde et déplacement, particules, brouillard et environnement
6. [Sauvegarde](#6-sauvegarde)
7. [Outils](#7-outils)
8. [Livrer](#8-livrer)
9. [Vérifier](#9-vérifier)
10. [Ce qui n'est pas encore là](#10-ce-qui-nest-pas-encore-là)

Les exemples à lire avec ce guide :

- [`examples/jeu_minimal/`](../examples/jeu_minimal/) : le plus petit jeu (une fenêtre, un cube, une entrée, un son), construit **de l'extérieur** du moteur, comme l'ARPG ;
- [`apps/tranche/main.cpp`](../apps/tranche/main.cpp) : la tranche jouable comme programme livré ;
- [`apps/tranche/states_demo.cpp`](../apps/tranche/states_demo.cpp) (titre, chargement, pause, options, sauvegardes), [`slice_ui.cpp`](../apps/tranche/slice_ui.cpp) (son interface) et [`demo3d.cpp`](../apps/tranche/demo3d.cpp) (le monde, les déplacements, l'animation, le brouillard, la sauvegarde du monde) : un jeu complet sur le moteur.

---

## 0. Conventions

- **Espace de noms** `moteur`, en-têtes `#include "moteur/<nom>.hpp"`. C++20.
- **Unités** : mètres, secondes, degrés pour les angles de caméra. **Y vers le haut**, le sol est le plan **XZ** ; une case de la grille fait 1 m (`cell_at()`, `cell_centre()` dans `nav_grid.hpp`). Les positions « sur le plan » sont des `glm::vec2` (x, z).
- **Couleurs** : linéaires pour la 3D ; `srgb_to_linear()` convertit une couleur choisie à l'œil.
- **Fil** : tout se fait sur le **fil principal**, sauf ce que la référence dit autrement (`read_file()`, `JobSystem`, `find_path()`).
- **Erreurs** (politique écrite à la partie 2 du [jalon 9](JALON_9_NIVEAU_PRODUCTION.md)) :
  - les **exceptions** sont réservées au **chargement** (un fichier invalide, une fenêtre impossible, un rejeu illisible) : le constructeur de l'`Application`, les étapes d'un `LoadingState`, les lectures de tables et d'assets ; un asset graphique ou sonore cassé est remplacé par un repli visible ou audible et noté au journal ;
  - **aucune exception ne traverse une image** : une exception qui sort du jeu (`on_event`, `update`, `waiting`, `render`) ou d'un service entre deux images est un **bogue**, traité comme un plantage contrôlé (rapport dans `crash_reports/`, `crash_message` montré au joueur, `run()` se termine et `ended_on_error()` est vrai). La logique ne continue jamais sur un état à moitié mis à jour ;
  - un **échec du GPU dans une image** (un tampon de transfert qui ne se projette pas, un tampon qui ne grandit pas) fait **sauter** ce que l'échec concerne pour cette image seulement (noté au journal, `RenderStats::failed_preparations`, `Renderer::frame_failures()`) ; la perte du périphérique termine proprement (chapitre 8.3) ;
  - les **contrats** (préconditions des en-têtes : appel sur le fil principal, `begin_frame` / `end_frame` appariés, référence de données résolue, case dans la carte) sont vérifiés par `MOTEUR_ASSERT` dans les builds de développement (`assert.hpp`, chapitre 7.3) ; `MOTEUR_ASSERT_MAIN_THREAD` pour les services du fil principal ;
  - vérifié par `tools/tests/check_error_policy.py` (dans le script de vérification) : `tranche --test-plantage frame` (image sans ses maillages, la partie continue) et `--test-plantage update` (exception dans `update` : rapport, code de sortie 1).
- **Langue** : le moteur parle au développeur en **anglais** (journal, erreurs de données, rapports) ; il ne contient **aucun texte du joueur** : le jeu fournit ses libellés (options, message de plantage). Décision T1 de la [revue des jalons 1 à 7](REVUE_JALONS_1_A_7.md).
- **Données du jeu** : les clés des formats du moteur sont en anglais ; les champs des tables, les noms d'états d'animation, les dossiers de sauvegarde du jeu sont dans la langue du jeu.

---

## 1. Démarrer un jeu

### 1.1 Le dépôt du jeu : le moteur en sous-module

Le jeu a son propre dépôt git ; le moteur y est un **sous-module** épinglé sur une étiquette de version, ajouté à la construction par `add_subdirectory` (décision de la partie 3 du [jalon 8](JALON_8_CONSOLIDATION.md#3-le-moteur-comme-bibliothèque)) :

```
git submodule add <adresse du dépôt du moteur> extern/moteur
git -C extern/moteur checkout v0.8.0
```

Le `CMakeLists.txt` du jeu (repris du squelette de l'ARPG et de `examples/jeu_minimal/CMakeLists.txt`) :

```cmake
cmake_minimum_required(VERSION 3.25)
set(MOTEUR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern/moteur" CACHE PATH "The engine's folder")

# vcpkg : sans bibliothèque à lui, le jeu lit le manifeste du moteur et ses triplets.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg.json")
    set(VCPKG_MANIFEST_DIR "${MOTEUR_DIR}" CACHE PATH "")
endif()
if(NOT DEFINED VCPKG_OVERLAY_TRIPLETS)
    set(VCPKG_OVERLAY_TRIPLETS "${MOTEUR_DIR}/triplets" CACHE PATH "")
endif()
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

project(arpg VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory("${MOTEUR_DIR}" moteur)

add_executable(arpg src/main.cpp)
moteur_enable_warnings(arpg)
moteur_add_game(arpg NAME "ARPG" PUBLISHER "..." ICON "${CMAKE_CURRENT_SOURCE_DIR}/art/icons/arpg"
                ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/assets")
```

Ce qu'il faut savoir :

- Le jeu ne nomme que la cible **`moteur`** (liée par `moteur_add_game`) et les fonctions CMake du moteur. Le moteur porte lui-même C++20 et `/utf-8` (publics : les sources du jeu en ont besoin).
- Quand le moteur est un sous-dossier, ses programmes (bac à sable, éditeur, tranche, exemples, tests) ne sont **pas** construits (`MOTEUR_BUILD_SANDBOX`, `MOTEUR_BUILD_SLICE`, `MOTEUR_BUILD_EXAMPLES`, `MOTEUR_BUILD_TESTS` éteints). Les options `MOTEUR_SHIPPING`, `MOTEUR_TRACY` et `MOTEUR_WARNINGS_AS_ERRORS` s'appliquent au jeu.
- **vcpkg** : le jour où le jeu a ses propres bibliothèques, il a son `vcpkg.json`, qui doit lister **les dépendances du moteur** (voir `vcpkg.json` du moteur) plus les siennes, avec la même `builtin-baseline`, et un `vcpkg-configuration.json` qui déclare les ports du moteur (`"overlay-ports": ["extern/moteur/ports"]` : ozz-animation). *(La meilleure façon de composer deux manifestes reste à vérifier, partie 3 du jalon 8.)*
- **Presets** : copier ceux de `examples/jeu_minimal/CMakePresets.json` (`windows-debug`, `windows-release`, `windows-shipping`, `macos-*`) : triplet `x64-windows-static-md` en développement, `x64-windows-static` (CRT statique) et `MOTEUR_SHIPPING=ON` pour la version livrée.

### 1.2 `moteur_add_game`

```cmake
moteur_add_game(<cible>
    [NAME <texte>]              # le nom vu par le joueur (défaut : la cible)
    [VERSION <x.y.z>]           # défaut : la version du projet appelant
    [PUBLISHER <texte>]         # l'éditeur (informations de version, installeur)
    [ICON <chemin sans extension>]  # <chemin>.ico sous Windows, <chemin>.icns sous macOS
    [ASSETS <dossier>...]       # les assets du jeu
    [SHADER_DIR <dossier> SHADERS <fichier>...]  # les shaders propres au jeu, s'il en a
    [LICENSES <paquet vcpkg>...]  # les licences des bibliothèques du jeu
    [SHARE_OUTPUT_OF <cible>])  # un second programme du même dossier (un éditeur à côté de son jeu)
```

Ce qu'elle met à côté de l'exécutable, à chaque build :

| Dossier | Contenu |
|---|---|
| `shaders/` | les shaders du moteur, compilés pour l'OS (DXIL sous Windows, MSL versionné sous macOS), puis ceux du jeu |
| `assets/` | `assets_moteur/` (police de l'interface de debug, `credits_moteur.json`), puis les dossiers `ASSETS` du jeu ; un fichier du jeu au même chemin remplace celui du moteur |
| `licenses/` | les textes de licence des bibliothèques du moteur et de celles listées dans `LICENSES` |

Sous Windows, elle ajoute aussi l'**icône**, la **ressource de version** (nom, éditeur, version) et un **manifeste** (Windows 10 / 11, densité par écran, chemins longs, page de code UTF-8). `VERSION` doit être de la forme `x.y.z` ; un `ICON` donné dont le `.ico` manque arrête la configuration. `tools/package/make_icon.py` montre comment produire les trois fichiers (`.png`, `.ico`, `.icns`) d'une icône.

**Crédits** : le jeu a son `credits.json` dans ses assets ; le moteur fournit `credits_moteur.json`. `Credits::load({asset_path("credits_moteur.json"), asset_path("credits.json")})` les lit pour une fenêtre « À propos » (`credits.hpp`). Toute nouvelle bibliothèque ou tout nouvel asset doit y être ajouté ; la préparation des assets avertit pour un asset livré qu'aucune entrée ne couvre.

### 1.3 `Application` et `ApplicationConfig`

`Application` possède la fenêtre, le GPU, les assets, les tables, l'audio, les entrées, la console, le journal et le rapport de plantage. Le jeu remplit un `ApplicationConfig`, crée l'`Application`, puis appelle `run()` avec son `Game`. D'après `examples/jeu_minimal/main.cpp` et `apps/tranche/main.cpp` :

```cpp
int main(int argc, char* argv[]) {
    try {
        moteur::ApplicationConfig config;
        config.title = "ARPG";
        config.organization = "editeur";     // le dossier du joueur : SDL_GetPrefPath(organization, application)
        config.application = "arpg";
        config.display_settings = true;      // les réglages d'affichage du joueur (display.json)
        config.crash_message = "Le jeu a rencontré une erreur...\n{report}";  // dans la langue du joueur
        moteur::Application app(config);     // lève std::runtime_error si SDL, la fenêtre ou le GPU échouent
        MonJeu game(app);
        app.run(game);
        if (app.ended_on_error()) {
            return 1;                         // périphérique GPU perdu, rapporté
        }
    } catch (const std::exception& e) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "ARPG", e.what(), nullptr);
        return 1;
    }
    return 0;
}
```

Les champs les plus utiles (tous ont un défaut ; détails dans la référence) :

| Champ | Rôle |
|---|---|
| `title`, `width`, `height` | la fenêtre (en points ; `pixel_width` / `pixel_height` pour un nombre exact de pixels, utile aux captures) |
| `window_display` | l'écran où la fenêtre s'ouvre (indice ; négatif : le choix du système). La variable d'environnement `MOTEUR_WINDOW_DISPLAY` le remplace : les scripts de vérification la mettent à `1` pour ouvrir leurs fenêtres sur le **second écran** |
| `tick_rate` | ticks de logique par seconde (60 par défaut) ; voir le chapitre 2 |
| `organization`, `application` | nomment le **dossier du joueur** pour toujours : à fixer avant la première version |
| `display_settings` | lit et applique `display.json` (le jeu ; les outils et les vérifications le laissent éteint) |
| `debug_ui` | Dear ImGui et les outils de debug (ignoré dans la version livrée) |
| `assets_source_directory` | le rechargement à chaud (chapitre 4.2) |
| `startup_commands` | lignes de console lancées avant le premier tick (`+set`, chapitre 7) |
| `record_input_path`, `replay_input_path`, `replay_start` | enregistrer ou rejouer les entrées (chapitre 2) |
| `crash_reports`, `crash_message`, `gpu_lost_message` | rapports de plantage et messages du joueur (chapitre 8) |
| `archives_directory` | le dossier des archives `.pak` monté au démarrage (`archives`) |
| `report_json_path`, `report_performance`, `gpu_timing` | mesures (chapitre 7) |

L'`Application` donne ensuite au jeu : `renderer()`, `assets()`, `data()`, `input()`, `audio()`, `console()`, `profiler()`, `debug_tools()`, `display()` / `apply_display()`, `preferences_directory()`, `to_pixels()`, `quit()`, `set_paused()` / `step()`, `restart_clock()`, `report_value()`.

### 1.4 `Game`

Le programme implémente `moteur::Game` :

```cpp
class MonJeu : public moteur::Game {
public:
    void on_event(const SDL_Event& event) override;        // événements bruts (texte, fenêtre), facultatif
    void update(double dt) override;                        // la logique, à pas fixe : dt toujours le même
    bool waiting() override { return false; }               // facultatif : voir LoadingState
    void render(moteur::Renderer& renderer, double alpha) override;  // le dessin, une fois par image
};
```

`update()` lit les actions et change l'état ; `render()` **enregistre** seulement ce qu'il faut dessiner (rien ne part au GPU avant la fin de l'image) et reçoit `alpha`, la position de l'image entre deux ticks. Exemple complet : [`examples/jeu_minimal/main.cpp`](../examples/jeu_minimal/main.cpp).

### 1.5 Les écrans : `StateStack`, `GameState`, `LoadingState`

Un vrai jeu a plusieurs écrans (titre, chargement, partie, pause, options) : `StateStack` (`state_stack.hpp`) est un `Game` qui les empile (décision de la partie 6 du [jalon 4](JALON_4_SYSTEMES_DE_BASE.md)).

- Chaque écran dérive de `GameState` : `enter()` / `exit()`, `covered()` / `uncovered()`, `on_event()`, `update(dt)`, `render(renderer, alpha)`, `name()`.
- `transparent()` : l'état du dessous est dessiné d'abord (une pause sur la partie figée). `blocking()` (vrai par défaut) : les états du dessous ne sont plus mis à jour.
- Seul l'état **du dessus** lit les entrées : pour les autres, l'`Input` est muet pendant leur `update()`.
- `push`, `pop`, `replace`, `reset` sont des **demandes**, appliquées au début du `update()` suivant, entre deux ticks, jamais pendant un `update()` ou un `render()`. Un état remplacé est détruit **après** la construction de son successeur : ce que les deux utilisent reste chargé. Après chaque série de transitions, la pile de l'`Application` libère les assets que plus personne ne tient (`Assets::collect_garbage()`).

```cpp
moteur::StateStack stack(app);
stack.push(std::make_unique<TitreState>(...));
stack.apply_pending();   // installe le premier état avant run()
app.run(stack);
```

La tranche garde sa pile dans son propre `Game` (`StatesDemo`, qui relaie `update`, `render`, `on_event`) : c'est le modèle à suivre si le jeu a de l'état au-dessus des écrans (emplacements de sauvegarde, interface).

**Chargement** : `LoadingState` exécute ses étapes, **une par tick**, puis se remplace par l'état que construit la dernière. Une étape peut attendre (`until`) que les fichiers lus **en arrière-plan** soient prêts ; l'écran reste animé pendant ce temps. D'après `make_loading()` dans `states_demo.cpp` :

```cpp
auto held = std::make_shared<Held>();   // garde ce que les étapes chargent jusqu'à l'état suivant
std::vector<moteur::LoadingState::Step> steps = {
    {"Lecture des fichiers",
     [&assets] { assets.prefetch_model("models/heros.glb"); assets.prefetch_sound("audio/pas.ogg"); },
     [&assets] { return assets.prefetching() == 0; }},          // attend le décodage, l'écran bouge
    {"Modèles", [&assets, held] { held->heros = assets.model("models/heros.glb"); }},
    {"Sons", [&assets, held] { held->pas = assets.sound("audio/pas.ogg"); }},
};
stack.replace(std::make_unique<MonChargement>(std::move(steps), "Construction du monde",
                                              [held] { return std::make_unique<PartieState>(held); }));
```

`MonChargement` dérive de `LoadingState` pour dessiner son écran (`draw(renderer, progress, label)`) et réagir à une étape qui lève (`failed(error)`, qui par défaut dépile). Les ticks où l'étape attend sont des **ticks qui ne comptent pas** (`waiting()`, chapitre 2) : un chargement a toujours le même nombre de ticks comptés, quelle que soit la vitesse du disque.

Décisions : [jalon 4, partie 6](JALON_4_SYSTEMES_DE_BASE.md) (pile d'états), [jalon 8, partie 5](JALON_8_CONSOLIDATION.md#5-optimisation--chargement-en-arrière-plan-pics-et-mémoire) (chargement en arrière-plan).

---

## 2. La boucle

### 2.1 Pas fixe et ticks entiers

`Application::run()` fait tourner la logique à **pas fixe** (`FixedTimestep`) et dessine aussi vite que l'écran le permet. Une image :

1. le rechargement à chaud des assets, entre deux images ;
2. les événements SDL (vers `Input`, puis `Game::on_event`) ;
3. **0, 1 ou plusieurs ticks** : pour chacun, les commandes de console « de jeu » en attente, puis `game.update(dt)`, puis `Input::end_tick()` (ce que le tick a vu est consommé) ;
4. `game.render(renderer, alpha)`, puis l'envoi au GPU ;
5. `Audio::update()` : les sons demandés par les ticks partent.

- `dt` vaut toujours `1.0 / tick_rate()` ; `ApplicationConfig::tick_rate` le fixe (60 par défaut, 1 à 1000), une fois pour tout le programme.
- Après un arrêt (débogueur, fenêtre déplacée), le rattrapage est plafonné par `max_frame_time` (0,25 s). Après un long chargement dans un `update()`, `Application::restart_clock()` (que `StateStack` appelle à chaque transition) évite de rattraper les ticks « dus ».
- **Durées** : toute durée de jeu s'écrit **en secondes** (dans les données) et se convertit en ticks par `moteur::ticks_from_seconds()` ; ne jamais écrire 60 en dur. C'est ce que font les clips, leurs événements et les délais du moteur (décision 6.3 de la [revue](REVUE_JALONS_1_A_7.md)). Les rejeux et sauvegardes supposent la fréquence avec laquelle ils ont été faits.
- **Pause et pas à pas** (outils) : `set_paused()`, `step(n)`, commandes `pause` et `step` ; la variable `time.scale` accélère ou ralentit le temps réel donné aux ticks (les ticks gardent leur durée).

### 2.2 Interpolation : dessiner entre deux ticks

L'image tombe entre deux ticks ; `alpha` (dans `[0, 1)`) dit où. Ce qui bouge se dessine **entre son état au tick précédent et son état au tick courant** :

- **Entités** : le composant `PreviousTransform` (copié par `World::begin_tick()`, premier système de chaque tick) ; `World::submit(renderer, alpha, ...)` interpole tout seul.
- **Caméra** : `Camera3D::begin_update()` au début du tick, `interpolated(alpha)` au dessin.
- **Vos propres valeurs** : `moteur::interpolate(previous, current, t)` (`fixed_timestep.hpp`), **jamais `glm::mix`**. `mix` calcule `a·(1 − t) + b·t`, qui ne rend pas exactement `a` quand rien n'a bougé : une scène immobile tremble d'un arrondi d'une image à l'autre, et les captures ne sont plus identiques ([jalon 3, partie 11](JALON_3_RENDU_3D.md)). Pour un quaternion, `glm::slerp` (`interpolate(Transform, Transform, t)` le fait).

```cpp
void update(double dt) override {
    previous_angle_ = angle_;
    angle_ += input.axis(turn_).x * kTurnSpeed * static_cast<float>(dt);
}
void render(moteur::Renderer& renderer, double alpha) override {
    const float angle = moteur::interpolate(previous_angle_, angle_, static_cast<float>(alpha));
    ...
}
```

### 2.3 Les règles du déterminisme

La logique est **déterministe** : même état de départ, mêmes entrées tick après tick, même partie, sur les deux OS et à n'importe quelle vitesse d'image. Les rejeux, les sauvegardes « identiques au tick près » et les captures de référence en dépendent. Ce que la logique (`update()`) **peut lire** :

- l'état du jeu (composants, tables, son propre générateur aléatoire à graine sauvegardée) ;
- les actions de l'`Input` lues **dans `update()`** (`pressed`, `down`, `axis`, `pointer` du tick) ;
- l'état de l'`Animator` (clip joué, temps en ticks, poids en millièmes, événements de `advance_animators()`) ;
- les résultats des systèmes du moteur appelés dans le tick (chemins, collisions, champs de vision, requêtes spatiales) ;
- les assets **chargés** (jamais un asset en cours de préchargement).

Ce qu'elle **ne lit jamais** :

- **`alpha`**, la caméra **interpolée** ou toute valeur interpolée : pour viser le sol, la tranche utilise la caméra **du tick** (`camera_.ground_point(pointer)` dans `update()`), pas celle de la dernière image dessinée ;
- la **pose** d'un personnage (`AnimationPose`, calculée au dessin, en flottants SIMD) ni la place d'un objet attaché à un os (`BoneAttachment`) ; `World::world_matrix()` d'une entité sur un os est pour les outils ([jalon 5, partie 4](JALON_5_ANIMATION_3D.md)) ;
- les **particules** (purement visuelles, au temps réel, avec leur propre générateur) et l'**audio** (rien n'en revient : ses variations ont leur propre générateur) ([jalon 6, partie 7](JALON_6_MONDE_ET_DEPLACEMENT.md), [jalon 4, partie 7](JALON_4_SYSTEMES_DE_BASE.md)) ;
- le **temps réel** (horloge, durée d'une image) ni le nombre d'images ;
- un résultat calculé sur un **fil de travail** dont l'ordre dépend des fils : le `JobSystem` ne fait jamais tourner la logique (`jobs.hpp`).

Et comment elle **calcule** :

- **Ordre défini** : les systèmes du moteur traitent les entités dans l'ordre de leurs identifiants, les requêtes rendent des résultats triés ; ne jamais faire dépendre un résultat de l'ordre d'un `std::unordered_map`. Pour parcourir une table de données (un tirage de butin), `DataTableBase::sorted()` ([jalon 7, partie 2](JALON_7_OUTILS_ET_DONNEES.md)).
- **Arithmétique simple** : `+ − × ÷` et `sqrt` (arrondis exacts en IEEE 754), **pas de trigonométrie** dans la logique (les bibliothèques mathématiques diffèrent d'un OS à l'autre) : orientations en **vecteurs unitaires** (`Mover::facing`), quaternion par `facing_rotation()` ([jalon 6, parties 3 et 5](JALON_6_MONDE_ET_DEPLACEMENT.md)). Entiers là où cela compte (temps des clips en ticks, poids en millièmes, coûts des chemins).
- **Égalités tranchées par une règle** (l'identifiant le plus petit), jamais au hasard.
- Les **commandes de console qui changent la partie** sont des commandes « de jeu » (mises en file, rejouées), et une variable qui change la logique est déclarée `Variable::Logic` (chapitre 7). Modifier un composant à la main dans l'inspecteur casse le déterminisme (l'inspecteur le signale).

### 2.4 Ticks qui ne comptent pas

`Game::waiting()` (et `GameState::waiting()`) est demandé avant chaque tick : vrai quand ce tick **attend** seulement un travail en arrière-plan. Un tel tick ne lit aucune entrée, ne lance aucune commande, n'est ni enregistré ni rejoué : leur nombre dépend de la machine. `LoadingState` s'en sert ; un jeu qui attend autre chose (une génération de niveau sur le `JobSystem`) fait de même.

### 2.5 Rejeux

- `ApplicationConfig::record_input_path` : à la fin, écrit tout ce que `update()` a lu de l'`Input`, tick après tick, avec les commandes de jeu de chaque tick (`InputRecording`, JSON compact, actions par **nom**).
- `replay_input_path` (et `replay_start`) : rejoue ce fichier à la place des vraies entrées.
- Dans le code : `Input::set_frame(enregistrement, tick)` impose l'état d'un tick enregistré et commence un rejeu (les vrais événements sont ignorés) ; `replaying()` dit si un rejeu est en cours, `stop_replay()` le termine et rend la main aux vraies entrées (toutes les actions au repos, le pointeur hors de la fenêtre).
- Le bac à sable les expose par `--record-input` / `--replay-input` / `--replay-start`, la tranche par `--replay-input` ; avec `--freeze-after N --capture image.png`, on obtient une **capture reproductible** au tick N. `tools/input/make_slice_replay.py` écrit le rejeu de référence de la tranche.
- Un rejeu sert aux tests, aux rapports de bogue et aux mesures (chapitre 9).

Décisions : [jalon 1, partie 4](JALON_1_FONDATIONS.md) (pas fixe), [jalon 5, partie 4](JALON_5_ANIMATION_3D.md) (temps des clips), [jalon 6](JALON_6_MONDE_ET_DEPLACEMENT.md) (logique déterministe), [jalon 7, partie 3](JALON_7_OUTILS_ET_DONNEES.md) (commandes rejouées).

---

## 3. Le monde

### 3.1 `World` : un registre EnTT par scène

Ce qui a une place dans la carte (personnages, objets, lumières, effets attachés) est une **entité** d'un registre **EnTT** ; ses **composants** sont des données simples ; les **systèmes** sont des fonctions sur des vues. Les systèmes propres du moteur (rendu, audio, assets, entrées) et les menus restent des classes ([jalon 4, partie 5](JALON_4_SYSTEMES_DE_BASE.md)).

- Un `World` par scène (`world.hpp`) ; le détruire détruit tout d'un coup. Non copiable, fil principal.
- `world.registry()` : le jeu y crée ses entités et y ajoute **ses propres composants** (`registry.emplace<Sante>(entite, ...)`).
- Détruire par `world.destroy(entity)`, qui détruit aussi les entités attachées (`Parent`) ; `registry().destroy()` laisserait des enfants orphelins.
- Un composant tient des **valeurs** et des **poignées** (assets `Asset<T>`, autres entités), pas de pointeurs (exception : les textures d'un `Material`, qui pointent dans un asset que l'entité doit aussi tenir).

### 3.2 Les composants du moteur

| Composant | Rôle |
|---|---|
| `Transform` | position (m), rotation (quaternion), échelle ; relatif au parent s'il y en a un |
| `PreviousTransform` | marque une entité **qui bouge** : sa place au tick précédent, pour l'interpolation |
| `Parent` | attache à une autre entité (une tête sur un corps) |
| `BoneAttachment` | avec un `Parent` animé : suspend l'entité à un point d'attache ou un os (l'épée dans la main), calculé au dessin |
| `Name` | un nom pour les outils |
| `Hidden` | ni dessiné ni éclairant (une créature hors de vue) |
| `MeshComponent` | un maillage (`Asset<Mesh>`) et son `Material` |
| `ModelComponent` | un modèle glTF (`Asset<Model>`), avec des pièces cachées (`hide(node)`) ; posé si l'entité a un `Animator` |
| `LightSource` | une lumière ponctuelle |
| `Billboard` | un sprite tourné vers la caméra |
| `Animator` | l'animation squelettique (chapitre 5.2) |
| `Mover`, `Collider` | le déplacement et la collision sur le plan du sol (chapitre 5.5) |
| `PersistentId` | l'identité d'une entité dans les sauvegardes (chapitre 6) |

Une entité sans `PreviousTransform` (le décor) garde sa matrice et sa boîte d'une image à l'autre : changer son `Transform`, son `Parent` ou son maillage passe par `registry.patch<>()` ou `replace<>()`, pour que ce cache tombe. (Bogue connu, dans la référence : remplacer le `ModelComponent` d'une entité immobile ne fait pas tomber le cache ; appeler aussi `registry.patch<Transform>(entity)`.)

Créer un personnage, d'après `Demo3D::spawn_animated()` et la création du héros :

```cpp
entt::registry& registry = world_.registry();
const entt::entity heros = registry.create();
registry.emplace<moteur::Transform>(heros, pied);
registry.emplace<moteur::PreviousTransform>(heros, pied);      // il bouge : interpolé
registry.emplace<moteur::ModelComponent>(heros, assets.model("models/heros.glb"), std::vector<std::uint32_t>{});
moteur::Animator animator = moteur::Animator::create(assets.skeleton("models/heros.glb"),
                                                     assets.clips("models/heros.glb"),
                                                     assets.animation_set("animations/heros.json"));
animator.start_graph();
registry.emplace<moteur::Animator>(heros, std::move(animator));
registry.emplace<moteur::Collider>(heros, moteur::Collider{.radius = 0.35f});
registry.emplace<moteur::Mover>(heros, moteur::Mover{.speed = 4.0f});
registry.emplace<moteur::PersistentId>(heros, prochain_id_++);

const entt::entity epee = registry.create();
registry.emplace<moteur::Transform>(epee);
registry.emplace<moteur::Parent>(epee, heros);
registry.emplace<moteur::BoneAttachment>(epee, "right_hand");
registry.emplace<moteur::ModelComponent>(epee, assets.model("models/epee.gltf"), std::vector<std::uint32_t>{});
```

### 3.3 L'ordre d'un tick et la collecte vers le rendu

Le jeu écrit **une fois** l'ordre de ses systèmes dans son `update()` :

```cpp
void PartieState::update(double dt) {
    const float step = static_cast<float>(dt);
    camera_.begin_update();
    world_.begin_tick();                                        // 1. PreviousTransform <- Transform
    appliquer_les_actions();                                    // 2. le jeu : mover.go_to(), animator.fire()...
    moteur::MovementStats stats;
    moteur::plan_paths(registry, grid_, clearance_, 8, stats);  // 3. chemins, 8 au plus par tick
    moteur::move_movers(registry, grid_, step, &field_, stats); // 4. les déplacements, contre les murs
    moteur::separate_colliders(registry, grid_, hash_);         // 5. les personnages se poussent
    moteur::finish_movers(registry, step);                      // 6. vitesse réelle, arrivé, bloqué
    // 7. Animator::set_move_speed() d'après Mover::actual_speed, puis :
    moteur::advance_animators(registry, 1, &events_);           // 8. l'animation, ses événements
    reagir_aux_evenements(events_);                             // 9. le coup porte à l'événement « impact »
}
```

Et chaque image, le monde est envoyé au rendu entre les deux derniers ticks :

```cpp
void PartieState::render(moteur::Renderer& renderer, double alpha) {
    const moteur::Camera3D camera = camera_.interpolated(alpha);
    renderer.meshes().set_camera(camera.view_projection(), camera.position());
    renderer.billboards().set_camera(camera);
    moteur::CollectOptions collect;
    collect.light_focus = camera.target();   // les lumières les plus proches du centre de la vue
    collect.view = camera.frustum();         // les personnages hors de vue ne sont pas posés
    world_.submit(renderer, static_cast<float>(alpha), collect);
}
```

`World::collect(sink, alpha, options)` fait la même chose vers un `WorldSink` à soi (les tests enregistrent ce qui serait dessiné). `world.animation_stats()` dit ce qu'a coûté l'animation de l'image. Exemple réel : `Demo3D::update()` et `Demo3D::render()` dans [`demo3d.cpp`](../apps/tranche/demo3d.cpp).

#### 3.3.1 Le travail sur plusieurs cœurs (jalon 9)

Le moteur répartit lui-même son travail : `move_movers()` calcule les agents en parallèle et écrit les `Transform` dans l'ordre des identifiants, `World::collect()` échantillonne les poses en parallèle, le rendu prépare ses lots en parallèle. Le résultat est identique à un calcul l'un après l'autre (captures et rejeux inchangés).

Un jeu qui veut répartir le sien :

```cpp
// Chaque index n'écrit que sa propre case : l'ordre des fils ne change rien.
std::vector<Menace> menaces(monstres.size());
moteur::jobs().parallel_for(monstres.size(), [&](std::size_t i) { menaces[i] = evaluer(monstres[i]); }, 32);
// Puis, sur le fil principal, dans l'ordre : les effets sur le monde.
for (std::size_t i = 0; i < menaces.size(); ++i) { appliquer(menaces[i]); }

// Des étapes qui dépendent les unes des autres : un graphe, gardé d'une image à l'autre.
graphe_.clear();
const auto vision = graphe_.add("vision", [&] { calculer_vision(); });
graphe_.add("menaces", [&] { calculer_menaces(); }, {vision});
graphe_.add("sons", [&] { choisir_sons(); });
graphe_.run();  // revient quand tout est fini ; relance la première exception
```

Règles : une tâche **ne modifie pas la structure de l'ECS** (pas d'`emplace`, de `destroy`, de `patch` : les signaux) et ne touche ni au GPU ni aux caches d'assets ; elle lit ce qu'elle veut tant que personne ne l'écrit pendant ce temps, et écrit seulement ses propres sorties. **Jamais de réduction de flottants dans un ordre qui dépend des fils** (une somme parallèle de dégâts) : chaque tâche écrit sa case, le fil principal additionne dans l'ordre. `submit(tâche)` est pour l'arrière-plan (chargements) ; le travail de l'image passe devant (`JobPriority::Frame`, ce que font `parallel_for()` et `TaskGraph`).

### 3.4 Réflexion : décrire un type une fois

Un type de données (composant, ligne de table) se **décrit une fois**, à côté de sa déclaration ; la lecture des tables, la sauvegarde, l'inspecteur, les préfabriqués et le schéma JSON en découlent (`reflection.hpp`, [jalon 9, partie 3](JALON_9_NIVEAU_PRODUCTION.md#3-réflexion-des-types-et-préfabriqués) ; décision : description `constexpr`, pas de générateur de code) :

```cpp
struct Sante {
    int vie = 100;
    int maximum = 100;
    float regeneration = 0.0f;
    Element faiblesse = Element::Aucun;
};
MOTEUR_REFLECT_ENUM(Element, "aucun", "feu", "froid");              // les valeurs 0, 1, 2 par leur nom
MOTEUR_REFLECT(Sante,
    MOTEUR_FIELD(vie, {.min = 0, .doc = "points de vie restants", .flags = moteur::kFieldDefault | moteur::kFieldLogic}),
    MOTEUR_FIELD(maximum, {.min = 1, .max = 1000000}),
    MOTEUR_FIELD(regeneration, {.min = 0, .unit = "points/s", .aliases = {"regen"}}),
    MOTEUR_FIELD(faiblesse));

// au démarrage, une fois : le composant a un nom stable (sauvegardes, préfabriqués, niveaux)
moteur::component_types().add<Sante>("sante");
```

- Les macros vont **dans l'espace de noms du type**, après lui. `MOTEUR_FIELD_AS(membre, "clé", ...)` donne une clé différente du nom du membre (les données du jeu sont dans sa langue). Les options (`FieldOptions`, dans cet ordre) : `min`, `max`, `unit`, `doc`, `flags` (`kFieldEdit`, `kFieldSave`, `kFieldData`, `kFieldLogic`, `kFieldPresentation`, `kFieldColor`, `kFieldReadOnly` ; par défaut édité, sauvegardé, lu des données), `required`, `aliases` (anciens noms encore lus : un champ renommé ne casse ni fichiers ni sauvegardes).
- Types de champs : `bool`, entiers, `float`, `double`, `std::string`, `glm` (vecteurs, quaternion), énumérations et structures décrites, `std::vector`, `std::optional`, `DataRef`, `entt::entity`, `Asset<T>`. Un membre d'un autre type n'est simplement pas décrit (un cache, une pose).
- Ce qui en découle : **tables** `moteur::add_reflected_table<MonstreData>(app.data(), "monstres", "data/monstres", verification)` (la vérification reçoit ce que la description ne dit pas : deux champs qui doivent s'accorder) ; **sauvegardes** `serializers.add_registered()` (chapitre 6.2) ; **inspecteur** : tout composant de `component_types()` y est dessiné champ par champ ; **préfabriqués** (3.5) ; **schéma** `json_schema(type_info<T>())`, et `bac_a_sable --write-schemas DOSSIER` pour les tables de la tranche.
- Format des valeurs : [FORMATS.md, 13](FORMATS.md#13-valeurs-décrites-par-la-réflexion). La lecture rend exactement les valeurs d'une lecture à la main (déterminisme) ; l'écriture est exacte.

### 3.5 Préfabriqués

Un **préfabriqué** (`*.prefab.json`, [FORMATS.md, 14](FORMATS.md#14-préfabriqués-prefabjson)) décrit en données un groupe d'entités : un lampadaire et sa flamme, une porte et son levier. Ils s'**imbriquent** (une entité est une instance d'un autre préfabriqué, avec des surcharges) et ont des **variantes** (`"base"` : un préfabriqué qui en change quelques champs, en retire ou en ajoute des entités).

```cpp
const moteur::Asset<moteur::Prefab> lampadaire = app.assets().prefab("prefabs/lampadaire.prefab.json");
moteur::PrefabPlacement ou;
ou.position = {12.0f, 0.0f, 4.0f};
ou.rotation = glm::angleAxis(glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f));
ou.overrides = {{"2", {{"light", {{"intensity", 8.0}}}}}};   // cette instance seulement
const moteur::PrefabInstance instance = moteur::instantiate(world_, *lampadaire, ou);
// instance.root, instance.entities (une par entité du préfabriqué, dans son ordre)
```

Le préfabriqué est lu et vérifié **une fois** (ses composants lus par leur description, ses modèles chargés) ; l'instanciation ne fait que copier. Les composants du jeu y sont utilisables dès qu'ils sont enregistrés dans `component_types()`.

---

## 4. Assets et données

### 4.1 Le gestionnaire d'assets

`app.assets()` (`assets.hpp`) charge **tout fichier** du jeu par son **chemin dans le dossier des assets** (`"models/tonneau/tonneau.gltf"`), une seule fois, partagé par tous ceux qui le demandent.

| Fonction | Asset | Si le fichier manque ou est cassé |
|---|---|---|
| `texture(path, settings)` | KTX2, PNG, JPEG... | damier magenta |
| `model(path)` | glTF (`.gltf`, `.glb`) avec maillages, matériaux, textures ; chargé sous sa forme **importée** (4.6) | cube magenta |
| `prefab(path)` | préfabriqué (`.prefab.json`, 3.5) | **exception** |
| `environment(path)` | `.hdr` équirectangulaire | ciel gris |
| `sound(path)` / `music(path)` | son court décodé / musique en flux | bip / silence |
| `particle_effect(path)` | effet de particules (`.json`) | effet sans émetteur |
| `font(path, taille)`, `atlas(path)`, `skeleton(path)`, `clips(path)`, `animation_set(path)`, `map(path)` | police, atlas, squelette, clips, description d'animation, carte | **exception** (pas de repli sensé) |

- Le résultat est une **poignée** `Asset<T>` (un `entt::resource`, un `shared_ptr` dessous) : `*asset`, `asset->`. Elle se garde dans un composant ; l'asset vit tant qu'une poignée existe.
- **Chemins** : relatifs, séparateur `/` ou `\`, normalisés (`normalize_asset_path()`). Un chemin dont la **casse** diffère du disque est une erreur partout (un système de fichiers sensible à la casse, ou une archive, ne le trouverait pas).
- **Libération** : `collect_garbage()` libère ce que plus personne ne tient, entre deux scènes : charger la scène suivante, lâcher la précédente, collecter (la `StateStack` le fait après chaque transition) ; ce que les deux utilisent n'est jamais rechargé.
- **Chargement en arrière-plan** : `prefetch_model / texture / skeleton / clips / sound` lisent et décodent sur les fils du `JobSystem` ; l'appel habituel qui suit, sur le fil principal, ne fait plus que créer les objets GPU (ou attend le décodage). `prefetching()` compte ce qui reste. Les chargements eux-mêmes se font **entre deux images**, jamais au milieu d'un `render()`.
- `exists(path)` : un asset facultatif (la tranche s'en sert pour les modèles de test non versionnés). `file_path(path)` : le chemin complet, à passer à `read_text_file()`.
- Un objet fait par le jeu (un maillage généré) devient une poignée par `make_asset(...)` ; un maillage simple : `Mesh::create(renderer, make_cube(...), "nom")`.
- Statistiques : `stats()`, commande `assets`, fenêtre DEBUG > Assets.
- **Dossiers montés** : `ApplicationConfig::asset_folders` ajoute les dossiers d'assets d'un autre jeu, lus **en place** (développement ; la version livrée les a dans ses archives) : la tranche utilise ainsi ceux de l'ARPG (`MOTEUR_ARPG_ASSETS_DIR`). Leurs fichiers ont des clés comme les autres ; le premier dossier qui a une clé gagne.

### 4.2 Rechargement à chaud (développement)

Avec `ApplicationConfig::assets_source_directory` (le dossier `assets/` **des sources** du jeu), un fichier modifié est recopié à côté de l'exécutable et ce qui en est fait est rechargé **en place** : les poignées voient le nouveau contenu. Le moteur ne devine pas ce dossier ; le jeu le donne en développement, comme la tranche :

```cmake
if(NOT MOTEUR_SHIPPING)
    target_compile_definitions(arpg PRIVATE ARPG_ASSETS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
endif()
```

```cpp
#if !defined(MOTEUR_SHIPPING)
config.assets_source_directory = ARPG_ASSETS_SOURCE_DIR;
#endif
```

- Ne pas garder de pointeur **dans** un asset (les pièces ou matériaux d'un modèle) d'une image à l'autre.
- Les atlas ne se rechargent pas. Les cartes ont une `revision()` qui augmente : la scène compare pour reconstruire ce qu'elle a tiré de la carte.
- Ce qui n'est pas un asset du gestionnaire s'abonne par `Assets::add_file_listener()` (les tables de données le font déjà).
- Commande `reload <fichier | table>`. La version livrée n'a pas de rechargement à chaud.

### 4.3 Tables de données

Monstres, objets, compétences : des **tables** en JSON, vérifiées au chargement, liées entre elles par identifiant, rechargées pendant le jeu, compilées en binaire pour la version livrée ([jalon 7, partie 2](JALON_7_OUTILS_ET_DONNEES.md)). Le moteur ne connaît aucun champ. Depuis le jalon 9, la façon recommandée est de **décrire la ligne** (chapitre 3.4) et de l'enregistrer par `moteur::add_reflected_table<T>()` : lecture, références, messages et schéma en découlent (la tranche le fait : `apps/tranche/sandbox_data.hpp`). La lecture à la main, ci-dessous, reste possible.

Une table est un **dossier** de fichiers JSON (`assets/data/<table>/*.json`) :

```json
{
  "version": 1,
  "defaults": { "vitesse": 3 },
  "rows": {
    "squelette": { "vie": 30, "vitesse": 2, "competence": "coup_d_epee" },
    "squelette_guerrier": { "base": "squelette", "vie": 50 },
    "_note": "les clés qui commencent par _ sont des commentaires"
  }
}
```

Déclaration et lecture (d'après `apps/tranche/sandbox_data.cpp`) :

```cpp
struct MonstreData {
    std::string nom;
    int vie = 1;
    float vitesse = 3.0f;
    moteur::DataRef<CompetenceData> competence;
};

app.data().add<MonstreData>("monstres", "data/monstres",
    [](moteur::DataReader& row) {                        // une ligne : lire un champ, c'est le vérifier
        MonstreData m;
        m.nom = row.text("nom");
        m.vie = row.integer("vie", 1, 100000);
        m.vitesse = row.number("vitesse", 0.0f, 30.0f, 3.0f);
        m.competence = row.reference<CompetenceData>("competence");
        return m;
    },
    [](MonstreData& m, moteur::DataLinker& linker) {    // une fois toutes les tables lues
        linker.resolve(m.competence, "competence");
    });

if (const moteur::DataIssues& issues = app.data().load_all(app.assets().root()); issues.has_errors()) {
    throw std::runtime_error("données invalides :\n" + issues.text());
}
const MonstreData& squelette = app.data().get<MonstreData>().at("squelette_guerrier");
if (squelette.competence) { /* squelette.competence->... */ }
```

- `DataReader` : `number`, `integer`, `boolean`, `text`, `choice`, `vec2`, `vec3`, `color`, `texts`, `reference<T>`, `references<T>`, `object`, `objects`, `error`, `warning`. Chaque problème est rapporté avec son lieu (« `data/monstres/squelettes.json > squelette_guerrier > vie` »), tous à la fois ; un champ inconnu est un avertissement.
- **Héritage** : `base` part d'une autre ligne (plusieurs niveaux, cycles refusés), sinon des `defaults` du fichier ; un objet de la ligne remplace celui de la base en entier.
- **Indices stables** : les lignes prennent leur indice dans l'ordre des identifiants au premier chargement ; un rechargement garde chaque ligne à son indice. Les `DataRef` et les indices restent valides ; les pointeurs vers les lignes, non. Un rechargement qui retire une ligne est refusé.
- `revision()` d'une table augmente à chaque rechargement réussi.
- **Forme binaire** : `DataTables::compile(root, dossier, issues)` écrit `<nom>.mdat` (CBOR) ; la version livrée lit `set_source(moteur::DataSource::Compiled, app.assets().root() + "data/")` avant `load_all()` (voir `apps/tranche/main.cpp`). La préparation des assets compile les tables par le rappel du jeu (4.4).
- Le bac à sable vérifie et compile ses tables en ligne de commande (`--check-data`, `--compile-data DOSSIER`) ; un jeu peut offrir les mêmes options. Commandes `tables` et `reload <table>`, fenêtre DEBUG > Data.

### 4.4 Préparation des assets (*cooking*)

Le jeu livré ne convertit rien au lancement : la **préparation** (`cook.hpp`) transforme les sources en données prêtes à charger ([jalon 8, partie 6](JALON_8_CONSOLIDATION.md#6-version-livrée--configuration-et-préparation-des-assets)).

Le **manifeste de livraison** (un JSON des assets du jeu, non livré lui-même) dit ce qui part :

```json
{
  "version": 1,
  "include": ["maps/acte1/*.json", "models/characters/**", "effects/*.json", "audio/**",
              "input/arpg.json", "credits.json", "credits_moteur.json"],
  "exclude": ["**/*.blend"],
  "data": "data",
  "archives": [
    {"name": "audio", "include": ["audio/**"]},
    {"name": "personnages", "include": ["models/characters/**", "animations/**"]}
  ]
}
```

Ce que fait `cook()` :

- prend les fichiers du manifeste **et leurs dépendances** (images d'un glTF, avec la préférence KTX2 ; textures d'un effet ; bases, imbriqués et assets d'un préfabriqué, par identifiant) ; une dépendance absente est une erreur ;
- transcode les KTX2 UASTC en **BC7 / BC5** une fois pour toutes ;
- livre chaque modèle sous sa **forme importée** seulement (`.mmdl`, 4.6), chaque `.json` en **CBOR** sous son nom, et le **registre figé** des identifiants : le jeu livré ne lit ni glTF ni JSON ;
- **compile les tables** (dossier `data`) par le rappel du jeu ;
- copie les shaders compilés ;
- **relit** chaque fichier préparé avec le chargeur du moteur (modèle, carte, effet, description d'animation, texture) : ce qui ne charge pas est une erreur maintenant, pas chez le joueur ;
- avertit pour tout asset livré sans entrée de crédits ; liste les sources que rien ne livre (`--orphans`) ;
- ne refait que ce qui a changé (`cook_state.json`) ;
- range tout dans des **archives** (4.5).

Un jeu **avec** tables a son propre outil de préparation, un `main()` qui appelle `cook_main()` avec ses tables (`apps/tranche/cook_main.cpp`) ; sans tables, l'outil `moteur_cook` du moteur suffit :

```cpp
#include "moteur/cook.hpp"

int main(int argc, char* argv[]) {
    return moteur::cook_main(argc, argv, [](const std::string& root, const std::string& directory,
                                             moteur::DataIssues& issues) {
        moteur::DataTables tables;
        enregistrer_les_tables(tables);
        return tables.compile(root, directory, issues);
    });
}
```

```
arpg_cook --sources DOSSIER... --manifest FICHIER --output DOSSIER [--shaders DOSSIER] [--force] [--orphans]
```

C'est `moteur_package` qui l'appelle (chapitre 8). Code de sortie : 0 prêt à livrer, 1 erreurs, 2 mauvaise ligne de commande.

### 4.5 Archives et système de fichiers virtuel

- **Format `.pak`** maison (`archive.hpp`) : table des entrées triée et vérifiée par une empreinte, données alignées sur 4 Ko, **Zstandard** par entrée (sauf ce qui est déjà compressé : KTX2, OGG, MP3, PNG...), CRC32 contrôlé à chaque lecture, deux chemins qui ne diffèrent que par la casse refusés ([jalon 8, partie 7](JALON_8_CONSOLIDATION.md#7-archive-des-assets)).
- La préparation écrit `archives/<nom>.pak` selon les groupes du manifeste, le reste et les shaders dans `common.pak`.
- **Tout ce que le moteur charge passe par `read_file()`** (`paths.hpp`) : un fichier du dossier du programme est cherché d'abord dans les archives montées (la dernière par ordre de nom d'abord : une archive de correctif masque une entrée), puis sur le disque. Le jeu fait de même pour ses propres fichiers (`read_file()`, `read_text_file()`, `file_exists()`).
- `ApplicationConfig::archives_directory` (`archives`) est monté **avant tout chargement**. Une archive abîmée fait lever le constructeur de l'`Application` (message qui conseille de réinstaller). Dans la **version livrée**, une fois des archives présentes, rien d'autre n'est lu du dossier du programme (`set_archives_only()`) : un fichier non préparé n'existe pas. Les fichiers du joueur restent sur le disque.

### 4.6 Identité des assets et import

**Identifiants** ([FORMATS.md, 15](FORMATS.md#15-identité-des-assets--meta-et-registre)) : chaque asset a un identifiant permanent (UUID) dans un fichier voisin `<asset>.meta`, donné au premier lancement en développement. Ce qui désigne un asset dans les données le fait par cet identifiant, avec le chemin à côté pour la lecture : `{"id": "...", "path": "models/props/lampadaire.glb"}`. Les champs `Asset<T>` décrits (3.4) s'écrivent ainsi d'eux-mêmes (`Assets::write_options()`, `reference_to()`) et se lisent par `resolve_reference()` ; un chemin seul reste accepté.

- **Les `.meta` vont avec leurs assets**, dans le dépôt comme sur le disque : un `.meta` perdu, c'est une identité perdue (les références tombent sur leur chemin, avec un avertissement).
- **Déplacer ou renommer** : `python tools/assets/assets.py move DOSSIER ancien nouveau` déplace le fichier et son `.meta` et réécrit les chemins écrits à côté des identifiants ; `check` signale les doublons, les `.meta` orphelins et les assets sans `.meta`. Une copie faite avec son `.meta` (deux fois le même identifiant) reçoit un nouvel identifiant au lancement suivant.
- Le **code** qui désigne un asset par son chemin (`assets.model("models/...")`) reste juste tant que le chemin l'est ; la préparation le vérifie par la relecture.

**Import des modèles** : un glTF n'est pas chargé tel quel : il est **importé** une fois, avec les réglages du `import` de son `.meta` (niveaux de détail, ordre pour le cache, textures intégrées en KTX2 ; tableau en [FORMATS.md, 15.1](FORMATS.md#151-fichier-meta)), et le résultat (`.mmdl`, [FORMATS.md, 16](FORMATS.md#16-modèles-préparés-mmdl)) est gardé dans le cache `.imported/` à côté du dossier des sources. Le chargement suivant le reprend tant que le glTF, ses fichiers et ses réglages n'ont pas changé ; sinon il importe de nouveau (sur un fil de travail) et le rechargement à chaud suit. `bac_a_sable --import-assets` importe tout d'avance (la vérification le fait avant les captures).

```json
{
  "id": "6f1c2a4e-9b7d-4c1e-8a2f-3d5e7f901b2c",
  "import": {"lods": 2, "lod_error": 0.01},
  "version": 1
}
```

**Niveaux de détail** : chaque partie d'un modèle a jusqu'à `lods` versions simplifiées (quadriques d'erreur, bords et coutures gardés) sur les mêmes sommets ; le rendu choisit, pour chaque passe, la plus grossière dont l'erreur vue de la caméra tient sous 1 pixel (`MeshRenderer::set_lod_threshold`, 0 : toujours le maillage complet). La **logique ne lit jamais** un niveau de détail (collisions, sélection : la géométrie complète ou la leur).

**Matériaux** : un `Material` tient ses textures par des poignées `Asset<Texture>` (le rechargement à chaud et le ramasse-miettes ne laissent plus de pointeur pendant) ; le rendu reçoit une vue à pointeurs bruts (`DrawMaterial`, `Material::view()`), valable pour l'image.

Pas encore là : sommets quantifiés sur le GPU et imposteurs pour les foules lointaines (jalon 10, rendu).

---

## 5. Les systèmes

### 5.1 Rendu

`app.renderer()` (`renderer.hpp`) : SDL_GPU (Direct3D 12, Metal), rendu 3D PBR en HDR linéaire, puis l'interface 2D par-dessus. Le jeu ne fait qu'**enregistrer** dans `render()` ; tout part à la fin de l'image.

| Accès | Dessine |
|---|---|
| `renderer.meshes()` (`MeshRenderer`) | maillages et modèles 3D, lumières, ombres, brouillard, murs estompés |
| `renderer.billboards()` (`BillboardRenderer`) | sprites tournés vers la caméra dans la 3D (barres de vie, halos, particules) |
| `renderer.sprites()` | sprites du monde 2D, par-dessus la 3D |
| `renderer.screen_sprites()` | l'**interface**, en pixels de fenêtre, quelle que soit la caméra |
| `renderer.debug_lines()` | lignes de debug (boîtes, rayons) |

- **Caméra** (`Camera3D`) : cible, angles (`set_angles(yaw, pitch)`), perspective ou orthographique, champ (30° retenu), hauteur visible ; `ground_point(pixel)` (le sol sous la souris), `screen_ray()`, `world_to_screen()`, `frustum()`, `follow(goal, dt, half_life)` ; `begin_update()` / `interpolated(alpha)` (chapitre 2).
- **Éclairage** : `meshes.set_camera(view_projection, eye)` chaque image ; `set_sun(direction, couleur, intensité)` (ombres du soleil, `set_shadows(ShadowOptions)`) ; `set_environment(&environnement, intensité)` (un `.hdr` par `assets.environment()`, ou un ciel `Environment::create(renderer, make_sky(512, 256))`) ; jusqu'à **32 lumières ponctuelles** par image (`kMaxPointLights`), dont quelques-unes ombrées (`point_budget`, 4 par défaut). Les `LightSource` du monde sont données par `World::submit()`.
- **Matériaux** (`Material`) : PBR métal / rugosité de glTF, couleurs linéaires ; alpha **testé** (`alphaMode` `MASK`, `alpha_cutoff`) dans la scène et les ombres ; `fades` pour les murs à estomper (5.7). Le mode `BLEND` (transparence) n'est **pas encore dessiné** (jalon 9).
- **Dessin direct** (hors du monde) : `meshes.draw(model, matrice)`, `meshes.draw(mesh, matrice, materiau)`. Regroupement et instanciation automatiques, culling par boîte.
- **Texte** : `Font` (`assets.font(path, taille)`), `font.draw(renderer.screen_sprites(), "texte UTF-8", coin, options)`, `font.measure(...)` avec exactement la même mise en page. Une police par taille ; jeu de caractères latin.
- **Réglages du joueur** : résolution de rendu, anticrénelage (`AntiAliasing::None`, `Fxaa`, `Msaa2`, `Msaa4`), synchronisation, qualité des ombres passent par les réglages d'affichage (chapitre 8.5) ; `Renderer::set_render_scale()` et `set_anti_aliasing()` existent pour les outils.
- **Captures** : `renderer.request_capture("image.png")` (aussi les vignettes de sauvegarde, avec une largeur maximale).
- **Perte du GPU** : `device_lost()` ; l'`Application` écrit alors un rapport et termine proprement (chapitre 8.3).

Il n'y a pas encore de **système d'interface** : la tranche dessine ses menus à la main (`apps/tranche/slice_ui.cpp`). L'interface maison (mode retenu, mise en page, texte SDF, localisation) est au jalon 9 (décision 2.3 de la [revue](REVUE_JALONS_1_A_7.md)). Décisions : [jalon 3](JALON_3_RENDU_3D.md).

### 5.2 Animation

Squelettes et clips glTF, lus par ozz-animation, skinning sur le GPU ([jalon 5](JALON_5_ANIMATION_3D.md)). L'**`Animator`** (composant) est de l'**état de jeu** : temps en ticks entiers, poids en millièmes, mêmes clips sur toutes les machines ; la pose n'est calculée qu'au dessin.

- **Création** : `Animator::create(assets.skeleton(path), assets.clips(path), assets.animation_set(set))` ; l'`AnimationSet` (un `.json`) décrit vitesses au sol, *blend spaces*, masque du haut du corps, fondus, événements, points d'attache, et le **graphe**.
- **Sans graphe** : `animator.play("1H_Melee_Attack_Chop", {.layer = 1, .loop = false})` (couche 0 : tout le corps ; 1 : le haut du corps), `stop()`, `set_speed()` (vitesse d'attaque), `set_move_speed(m/s)` (les pieds ne glissent pas).
- **Avec graphe** (`"graph"` dans l'`AnimationSet`, [jalon 7, partie 9](JALON_7_OUTILS_ET_DONNEES.md)) : `start_graph()`, puis le jeu ne fait que poser des paramètres (`set_parameter("dead", true)`) et lancer des déclencheurs (`fire("attaque")`) ; les états et transitions sont dans les données.
- **Chaque tick** : `advance_animators(registry, 1, &events)` après ce qui choisit les clips ; les **événements** (`AnimatorEvent` : `"impact"`, `"step"`...) se lisent ensuite, exactement une fois, dans le même ordre partout. C'est à l'événement que le coup porte (`Demo3D::animate_characters()`).
- **Attaches** : `BoneAttachment` (3.2), au dessin.
- **Sauvegarde** : `save_state()` / `load_state()` (jamais la pose).

Pas encore là : *root motion* et couches additives (jalon 9, points 5.1 et 5.2 de la [revue](REVUE_JALONS_1_A_7.md)).

### 5.3 Audio

`app.audio()` (`audio.hpp`, miniaudio) : effets placés dans le monde, musique en flux avec fondus, groupes avec volume et pause, limite de voix, limiteur ([jalon 4, partie 7](JALON_4_SYSTEMES_DE_BASE.md)).

```cpp
moteur::PlaySound pas;
pas.position = glm::vec3(x, 0.0f, z);   // placé : atténué et panoramique depuis l'auditeur
pas.volume = 0.6f;
pas.pitch_variation = 0.06f;
pas.volume_variation = 0.15f;
app.audio().play(sons_de_pas_, pas);    // une variante au hasard (std::span d'Asset<Sound>)
app.audio().play_music(assets.music("audio/music/ville.mp3"));
```

- Groupes `SoundGroup::Music`, `Effects`, `Ambience`, `Interface` ; `set_group_volume()`, `set_master_volume()` : les **réglages du joueur**, lus dans `audio.json` des préférences au démarrage et réécrits à la fin s'ils ont changé.
- `play()` met en file ; les sons partent après le dessin. L'auditeur : `set_listener(camera)`.
- `play_stream()` pour une ambiance longue en boucle ; `stop(id, fondu)`, `set_position(id, ...)` pour un son qui suit.
- La simulation ne lit jamais rien de l'audio.

Pas encore là : les **événements sonores en données** (événements nommés, paramètres, bus, instantanés, outil de création ; API calquée sur FMOD Studio) : décision 4.1 de la [revue](REVUE_JALONS_1_A_7.md), jalon 9. Le jeu ne devrait donc pas multiplier les appels directs à des fichiers.

### 5.4 Entrées

`app.input()` (`input.hpp`) : le jeu lit des **actions**, jamais des touches ([jalon 4, partie 4](JALON_4_SYSTEMES_DE_BASE.md)).

```cpp
moteur::Input& input = app.input();
const moteur::ActionId deplacer = input.add_button("move_to");
const moteur::ActionId competence_1 = input.add_button("skill_1");
const moteur::ActionId axe = input.add_axis("move");
input.load_bindings(moteur::read_text_file(app.assets().file_path("input/arpg.json")), "input/arpg.json");
// le fichier du joueur, par-dessus (le jeu choisit son nom et l'écrit avec input.user_bindings_json()) :
input.apply_user_bindings(moteur::read_text_file(fichier_du_joueur), fichier_du_joueur);
```

Le fichier des liaisons (un ou plusieurs **profils**, clavier, souris et manette ensemble) :

```json
{"version": 1, "profile": "clic",
 "profiles": {"clic": {"label": "Déplacement au clic",
                       "buttons": {"move_to": ["mouse:left"], "skill_1": ["key:Q", "pad:x"]},
                       "axes": {"move": {"up": ["key:W"], "down": ["key:S"], "left": ["key:A"],
                                         "right": ["key:D"], "stick": "left"}}}}}
```

- Les touches sont nommées **par position** (`key:Q` est la touche A d'un clavier AZERTY) ; `input_source_label()` et `describe()` donnent ce qu'on montre au joueur.
- **Dans `update()`** : `pressed()`, `down()`, `released()`, `presses()` (plusieurs appuis entre deux ticks), `axis()`, `pointer()` (pixels de fenêtre ; négatif hors fenêtre). Un appui court entre deux ticks n'est jamais perdu, ni compté deux fois.
- `last_device()` : clavier-souris ou manette, pour l'interface.
- **Menu des contrôles** : `profiles()` / `set_profile()` (la disposition) ; `begin_capture()` puis `take_captured()` (la prochaine touche, bouton ou direction de stick pressée, aucune action ne voit rien pendant ce temps ; `cancel_capture()`) ; `capturing()` dit si une capture est en cours ; `set_sources()` / `set_axis_sources()` pour lier (`axis_sources()` lit les touches d'une direction d'axe), `actions_using()` pour signaler une touche déjà prise, `changed_by_player()` pour marquer une action modifiée, `reset_sources()` / `reset_all_sources()`. Le jeu écrit ensuite `user_bindings_json()` dans le dossier du joueur (`write_file_atomic()`). Modèle : `ControlsOptionsState` dans [`states_demo.cpp`](../apps/tranche/states_demo.cpp) (page **Commandes** du menu Options de la tranche, à côté de **Affichage** et **Audio**).
- Le pointeur se convertit en point du sol avec la caméra **du tick** (`camera_.ground_point(input.pointer())`).

Les libellés des touches (`describe()`) sont encore en français dans le moteur : ils passeront par la localisation (jalon 9). La sélection d'objets à la souris contre leur forme (plutôt que « la créature la plus proche du point visé ») est aussi au jalon 9 (point 3.5 de la [revue](REVUE_JALONS_1_A_7.md)).

### 5.5 Monde et déplacement

Le monde de la logique est soit un **plan** (y = 0) découpé en cases de 1 m ([jalon 6](JALON_6_MONDE_ET_DEPLACEMENT.md)), soit, depuis le jalon 9, un **niveau en 3D** : ses triangles de collision (5.5.1).

- **Carte** : `assets.map("maps/acte1.json")` donne une `MapData` (JSON dessiné en caractères : tuiles, légende, points nommés comme `start`, objets posés, connecteurs ; format en tête de `map_data.hpp`). `map->objects()` liste ce que le jeu doit créer (décor, apparitions, lumières), `map->point("start")` le point d'entrée.
- **Grille** : `moteur::NavGrid grid(map->tiles(), map->tileset())` : praticable et opaque par case ; `set()` / `refresh()` pour une porte qui s'ouvre (la `version()` change : chemins et champs à recalculer). `ClearanceMap clearance(grid)` : la place libre autour de chaque case (`update_around()` après un changement).
- **Chemins** : `find_path(grid, clearance, depart, but, options)` (A\* pour un cercle de rayon donné, lissé) ; `FlowField::compute(grid, clearance, cible, rayon, max_cost)` pour une foule qui va au même endroit.
- **Déplacement** : le composant `Mover` porte l'intention (`go_to(point)`, `move(direction)`, `follow_field(distance)`, `stop()`) ; les systèmes du tick (`plan_paths`, `move_movers`, `finish_movers`) écrivent le reste (`state` : `Moving`, `Arrived`, `Blocked`, `NoPath`... ; `facing` ; `actual_speed`, à donner à l'`Animator`). Ordre : chapitre 3.3.
- **Collisions** : `Collider` (cercle, couches et masques, poids de poussée, `blocked_by_walls`) ; `separate_colliders(registry, grid, hash)` pousse les personnages hors des murs et l'un de l'autre, et remplit le `SpatialHash` pour la suite du tick. Fonctions libres : `circle_fits`, `move_circle`, `nearest_fit`, `segment_clear`.
- **Requêtes spatiales** (après `separate_colliders`) : `hash.query_circle()`, `query_cone()` (une attaque en cône), `query_rect()` (un balayage devant soi) : résultats triés. **Rayons et vue** : `raycast(grid, de, a, RayBlock::Opaque | Unwalkable)`, `line_of_sight()`.

Tout est déterministe (ordre des identifiants, pas de trigonométrie). La tranche réunit tout cela dans `Demo3D::move_characters()` et `update_sight()`.

#### 5.5.1 Le niveau en 3D (jalon 9)

- **Géométrie de collision** : chaque triangle a un genre (`SurfaceKind` : `Walkable` sol, `Wall` mur, `Obstacle` obstacle, `Void` vide où l'on tombe). Un modèle la reçoit à l'import ([FORMATS.md, 15.1](FORMATS.md#151-fichier-meta), réglage `collision`) : les nœuds nommés `COL_...` ou `UCX_...` dans Blender (jamais dessinés ; `COL_wall`, `COL_walk`, `COL_obstacle`, `COL_void` donnent le genre, sinon la pente décide), ou aussi les triangles visibles (`"collision": "visible"`). Pour une maquette ou un test : `CollisionMeshData::add_box()`, `add_stairs()`, `add_ramp()`.
- **Scène de collision** : `CollisionScene` (un arbre de volumes englobants) : `add(maillage, matrice, corps)` puis `build()`. Les entités d'un niveau marquées `StaticCollision` (la collision de leur modèle et des boîtes) la remplissent par `gather_static_collision(world, scene)`. Requêtes, toutes déterministes : `raycast()`, `segment_clear()` (une ligne de vue), `ground_at()` (le sol sous un point, sur plusieurs étages), `sphere_contacts()`, `capsule_contacts()`, `box_triangles()`.
- **Personnages** : `move_character(scene, pieds, deplacement, dt, mouvement, forme, reglages)` déplace une capsule debout (`CharacterShape` : rayon, hauteur) : elle glisse le long des murs, monte les marches (`ControllerSettings::step_height`), suit le sol en descendant, refuse les pentes trop raides (`min_ground_normal_y`, un cosinus : pas de trigonométrie), tombe des rebords (gravité) et se pose. Le jeu n'a d'habitude pas à l'appeler : les systèmes du tick le font.
- **Systèmes du tick** dans un niveau 3D : les mêmes que sur la grille, avec la scène : `move_movers(registry, scene, dt, champs, stats, reglages)`, `separate_colliders(registry, scene, hash)` (deux personnages ne se poussent que si leurs hauteurs se recouvrent : l'un sur le pont, l'autre dessous, non), `finish_movers()`. `Collider::height` donne la hauteur de la capsule ; `Mover::goal` et `Mover::path` sont des points 3D, `Mover::grounded` et `vertical_speed` disent s'il tombe. Les requêtes du `SpatialHash` prennent une tranche de hauteurs (`HeightSpan`) : qui est à cet étage.
- **Débogage** : la variable `debug.collision` dessine les triangles par genre et les capsules ; la scène « Level 3D » du bac à sable (`bac_a_sable --level3d`) montre tout cela.

#### 5.5.2 Navigation dans un niveau 3D (jalon 9)

```cpp
moteur::NavMesh mesh;
mesh.build(scene, moteur::NavAgent{.radius = 0.35f, .height = 1.8f});  // une fois par niveau (ou préparé, partie 8)
const glm::vec3 entree = niveau.point("start");
mesh.prune(std::span(&entree, 1));             // écarte le sol inaccessible (dans les blocs fermés)

// Chaque tick :
const moteur::NavMesh* const meshes[] = {&mesh};          // un maillage par taille de créature (Mover::agent)
moteur::plan_paths(registry, meshes, 8, stats);           // les go_to() : chemins sur le maillage
champ.compute(mesh, position_du_heros);                    // une foule qui va au même endroit (quand la cible bouge)
const moteur::NavFlowField* const champs[] = {&champ};
moteur::NavSources nav{meshes, champs, &hash};
moteur::move_movers(registry, scene, dt, nav, stats);
moteur::separate_colliders(registry, scene, hash);
moteur::finish_movers(registry, dt);
```

- `Mover::avoid = true` pour les monstres d'une foule : ils s'écartent de ceux qu'ils vont heurter. Le héros n'en a pas besoin.
- **Portes et sauts** : des `NavLink` donnés à `build()`, une porte fermée par `set_link_enabled(id, false)` ; **zones** (eau, feu) par `NavAreaVolume`, leur coût dans `NavFilter`.
- **Le niveau change** (une caisse, un mur détruit) : `add_obstacle()` / `remove_obstacle()`, ou `rebuild(zone)` après avoir reconstruit la `CollisionScene` ; seules les tuiles touchées sont refaites (quelques millisecondes). Les champs et chemins calculés avant (`version()`) sont à refaire.
- Requêtes : `find_path()`, `nearest_point()`, `walkable_line()`, `height_at()` ; `nav_grid_view()` donne la grille d'un étage.
- Les **assets du niveau** ne changent pas : le maillage vient de leurs triangles de collision (5.5.1). Un mur qui laisse passer la navigation est un mur sans collision.

#### 5.5.3 Vue, sélection et survol en 3D (jalon 9)

- **Lignes de vue** : `line_of_sight(scene, yeux_a, yeux_b)` (symétrique). **Brouillard et champs de vision** d'un étage : `floor_sight_grid(mesh, scene, origine, largeur, hauteur, etage)`, puis `FieldOfView` et `ExploredMap` comme sur la grille du jalon 6.
- **Ce qui se sélectionne** porte un `Selectable` (genre : personnage, objet, interactif ; capsule ou boîte). **Le clic** se décide au tick, par les rayons : `pick(registry, camera.screen_ray(pointeur), &scene, &mesh)` donne le monstre (prioritaire), sinon l'objet, sinon le point du sol (sur le maillage de navigation).
- **Le survol** s'affiche avec le tampon d'identifiants : `renderer.meshes().set_ids(true)` une fois, puis chaque image `renderer.request_pick(pointeur)` ; `world.entity_of_pick(renderer.picked_id())` est l'entité survolée (une ou deux images de retard), `renderer.set_outline(world.pick_id_of(entite))` la détoure. Jamais pour la logique : le clic passe par `pick()`.
- **Les étages au-dessus du héros** : `Cutout::ceiling` (et `ceiling_radius`) retire ce qui s'estompe (`Material::fades`) au-dessus de lui.

#### 5.5.4 Les niveaux (jalon 9)

Un niveau (`assets/levels/*.level.json`, [FORMATS.md, 19](FORMATS.md#19-niveaux-v3-leveljson)) place des préfabriqués : ses pièces de kit portent leur collision (`static_collision`, ou les nœuds `COL_` de leur modèle), d'où viennent la `CollisionScene` et la navigation. Le jeu le charge par morceaux :

```cpp
moteur::LevelStreamer niveau(app.assets(), world_);
niveau.open_any("levels/crypte.level.json");   // forme préparée, ou préparée en mémoire (développement)
niveau.settings().load_all = true;              // une zone fermée : tout, derrière l'écran de chargement
niveau.set_focus(niveau.header().point("start")->position);
niveau.finish();                                 // la dernière étape de l'écran de chargement

// Chaque tick : le chargement avance autour du héros ; la logique utilise ce qui est chargé.
niveau.set_focus(position_du_heros);
niveau.update();
moteur::move_movers(registry, niveau.collision(), dt, {niveau.meshes(), champs, &hash}, stats);
```

- **Monde ouvert** : `load_all = false` ; `load_radius`, `unload_radius` (une marge), `max_chunks` (le budget mémoire), `instantiate_budget_ms` (ce que le fil principal donne chaque mise à jour pour créer les entités). La logique n'agit que là où le niveau est chargé (`loaded(chunk)`, `ready()`).
- **Rejeux et sauvegardes** : `ordered = true` fait les morceaux dans l'ordre de leurs coordonnées, quel que soit l'ordre des lectures en arrière-plan : les entités, les tuiles de navigation et les appels du crochet d'instance sont les mêmes à chaque exécution (la tranche : `load_all` et `ordered`, puis ses créatures créées par identifiant d'apparition). Un jeu qui sauvegarde garde aussi la clé du niveau et `state().to_json()`.
- **En développement**, `open_any()` garde la forme préparée du niveau dans le cache d'import et ne le prépare de nouveau que si lui, ses préfabriqués ou ses modèles ont changé (secondes la première fois, puis rien).
- **Ce qui change pendant la partie** (porte ouverte, coffre vidé) : `niveau.state().set(id, surcharges)` ou `remove(id)` ; `niveau.entity_of(id)` donne l'entité tant que son morceau est chargé ; `LevelInstanceId` marque la racine de chaque instance. Sauvegarder `state().to_json()` dans une section du jeu, le relire par `LevelState::from_json()`.
- **Préparer** : la préparation (`cook`) fait de chaque `.level.json` livré un en-tête et un fichier par morceau ([FORMATS.md, 20](FORMATS.md#20-niveaux-préparés-mlvl-et-mlvc)). **Migrer** une carte du jalon 6 : `bac_a_sable --migrate-map maps/x.json SORTIE regles.json`.
- Les pièces de maquette du moteur (`prefabs/blockout/` : sol, dalle de 8 m, mur, pilier, caisse, marche ; `tools/models/make_blockout.py`) servent à monter un niveau avant ses assets.
- **Les données du jeu d'une instance** (`LevelInstance::properties` : le monstre d'une apparition, le butin d'un coffre) arrivent par le **crochet d'instance** : le jeu donne ses composants à chaque instance au moment où son morceau la crée.

```cpp
niveau.set_instance_hook([this](entt::entity racine, const moteur::ChunkInstance& instance) {
    if (instance.properties.contains("personnage")) {
        apparitions_.push_back({racine, instance.properties["personnage"].get<std::string>()});
    }
});
```

- **L'éclairage du niveau** (soleil, ciel, exposition, fond : `LevelLighting`, écrit par l'éditeur, dans l'en-tête préparé : `niveau.header().lighting`) se donne au rendu chaque image par `moteur::apply_lighting(renderer, niveau.header().lighting, &ciel)` : le jeu et l'éditeur l'appliquent par la même fonction. `night_lighting()` en donne la nuit (la vue de nuit de l'éditeur).
- **Les accessoires d'un jeu** n'arrêtent rien sans `static_collision` : leur pipeline la leur donne (l'ARPG : `assets_source/commun/collisions.py`, des boîtes d'après les bornes de son catalogue, des marches pour un escalier). Une **dalle de sol** se pose au coin de sa case : son préfabriqué décale le modèle d'une demi-case s'il est centré.

Exemple complet : la tranche (`apps/tranche/demo3d.cpp`, fonctions `open_level()`, `level_moves()`, `level_sight()`, `level_steer()`) sur `assets/levels/tranche.level.json`.

### 5.6 Particules

Effets en JSON (`particles.hpp`), sur le CPU, budget de 20 000 particules par scène ([jalon 6, partie 7](JALON_6_MONDE_ET_DEPLACEMENT.md)) :

```cpp
moteur::ParticleSystem particules;                          // un par scène
moteur::EffectId feu = particules.play(assets.particle_effect("effects/feu.json"), position);
particules.update(secondes_reelles);                       // chaque image, au temps réel
particules.draw(renderer.billboards(), assets, &vue);      // dans render() ; vue = camera.frustum()
particules.add_lights(renderer.meshes(), 8, camera.target());
```

- La logique **lance** les effets (un impact, une mort) mais n'en lit jamais rien ; ils ne sont jamais sauvegardés.
- `move()`, `stop()` (l'émetteur s'arrête, les particules finissent), `kill()`, `clear()`, `stats()`.
- Pour une capture reproductible, la tranche les avance au tick quand elle est figée (`Demo3D::update()`).

Pas encore là : particules sur le GPU, rubans, particules maillées, distorsion, éditeur d'effets : jalon 10.

### 5.7 Brouillard de guerre, murs estompés, environnement

- **Vision** (`visibility.hpp`) : `FieldOfView::compute(grid, case, rayon)` (ombres portées symétriques), `visible(case)` ; `ExploredMap(largeur, hauteur)` garde ce qui a été vu (`add(view)`, à sauvegarder).
- **Brouillard** : le jeu donne un octet par case, `0` jamais vu, `128` exploré, `255` en vue : `meshes.set_fog_cells(largeur, hauteur, cases)` ; `meshes.set_fog(FogOfWar{...})` l'allume et règle les luminosités. Les créatures hors de vue reçoivent `Hidden` (le jeu le décide). Voir `Demo3D::update_sight()`.
- **Murs estompés** : les maillages dont `Material::fades` est vrai perdent un disque tramé autour de la ligne caméra-héros : `meshes.set_cutout(Cutout{.enabled = true, .focus = position_du_torse})` chaque image.
- **Environnement** : ciel procédural (`make_sky`, `SkySettings`) ou `.hdr` ; le soleil est une lumière à part (`set_sun`). Le post-traitement (bloom, occlusion ambiante, étalonnage) est au jalon 10.

---

## 6. Sauvegarde

Écrire et relire **tout l'état** de la partie, pour qu'une partie chargée continue **exactement** comme si elle n'avait jamais été interrompue ([jalon 7, parties 5 et 6](JALON_7_OUTILS_ET_DONNEES.md)).

### 6.1 Le fichier : `SaveGame`, `SaveSlots`, `SaveMigrations`

- Un `SaveGame` (`save_file.hpp`) : un **en-tête** (`SaveHeader` : version du jeu, date, temps de jeu, résumé du jeu en JSON, lisible sans le reste pour un menu) et des **sections** nommées (une par système : `"monde"`, `"scene"`...), chacune avec **sa version**.
- **Encodage** : `SaveEncoding::Json` (lisible, comparable : développement) ou `SaveEncoding::Cbor` (compact : jeu livré) ; la lecture prend les deux. Le jeu choisit à l'écriture (la tranche : variable `save.binary`).
- **Fichier** : `MSAV`, version, encodage, en-tête, corps, CRC-32 ; un fichier abîmé est refusé, jamais lu à moitié.
- **Écriture atomique** : `SaveSlots::write()` écrit dans un fichier temporaire, le vide sur le disque, puis remplace le fichier en une étape, en gardant le précédent en `.sav.bak` ; si l'écriture échoue (disque plein), l'ancienne sauvegarde est intacte et l'erreur dit pourquoi. `write_file_atomic()` (`file_io.hpp`) fait la même chose pour tout autre fichier.
- **Emplacements** : `SaveSlots slots(app.preferences_directory() + "sauvegardes/")` : `list()` (les plus récentes d'abord, illisibles comprises), `write()`, `read()` (repli sur la copie de secours), `remove()`, `thumbnail_path()` pour la vignette.
- **Migrations** : `SaveMigrations::add(section, depuis, fonction)` transforme une section de la version n à n + 1 ; `current(section)` est la version à écrire ; `SaveGame::decode()` / `SaveSlots::read()` migrent à la lecture.

```cpp
moteur::SaveMigrations migrations;
migrations.add("monde", 1, [](nlohmann::json& data) { /* v1 -> v2 : renommer un champ */ });

moteur::SaveGame save;
save.header.game_version = "0.1.0";
save.header.time = maintenant_en_secondes_utc;
save.header.play_seconds = temps_de_jeu;
save.header.summary = {{"lieu", "acte 1"}, {"niveau", 12}};
save.set_section("monde", migrations.current("monde"), serializers.save(world.registry()));
std::string error;
if (!slots.write("emplacement_1", save, moteur::SaveEncoding::Cbor, error)) { afficher(error); }

std::optional<moteur::SaveGame> lue = slots.read("emplacement_1", error, &migrations);
```

### 6.2 L'état du monde : composants sérialisables et identifiants persistants

- **Composants décrits** (chapitre 3.4, la façon recommandée) : `serializers.add_engine_components()` puis `serializers.add_registered()` sauvegardent tous les composants de `component_types()` par leur description (les champs `kFieldSave`) ; `serializers.set_tables(&app.data())` rattache leurs `DataRef` à leur ligne à la lecture. C'est tout ce que fait la tranche (`Demo3D::setup_serializers()`). La forme ancienne d'un composant (un tableau, une valeur seule) se lit par la fonction de mise à niveau donnée à `component_types().add<T>(nom, mise_a_niveau)` (`upgrade_from_array`, `upgrade_from_bare`).
- Seules les entités qui ont un **`PersistentId`** sont sauvegardées. L'identifiant vient d'un **compteur du jeu**, jamais réutilisé, lui-même sauvegardé (la tranche : `next_id` dans sa section `"scene"`). Jamais l'identifiant EnTT, qui dépend de l'ordre de création.
- `ComponentSerializers` (`world_save.hpp`), un par jeu, rempli au démarrage :

```cpp
moteur::ComponentSerializers serializers;
serializers.add_engine_components();   // Transform, Name, Hidden, Parent, BoneAttachment, LightSource, Collider, Mover, Animator
serializers.add<Sante>("sante",
    [](const Sante& s, const moteur::SaveContext&) { return nlohmann::json(s.valeur); },
    [](entt::registry& r, entt::entity e, const nlohmann::json& j, const moteur::SaveContext&) {
        r.emplace_or_replace<Sante>(e, j.get<float>());
    });
serializers.add_tag<HerosTag>("heros");
serializers.ignore<CacheVisuel>();     // reconstruit, jamais sauvegardé
```

- **Références entre entités** : écrire `context.id_of(entite)`, relire `context.entity_of(id)` ; toutes les entités existent avant la lecture des composants, l'ordre ne compte pas.
- **Charger** : dans un `World` neuf, `serializers.load(registry, *save.section("monde"), spawn, problems)`. La fonction `spawn` crée l'entité d'un enregistrement **avec ses visuels** (modèle, `Animator` créé avec son squelette et ses clips) ; ses composants reçoivent ensuite leurs valeurs, et `PreviousTransform = Transform` (rien ne glisse à la première image). Les problèmes (composant inconnu, référence manquante) sont listés, le reste est chargé.
- **Ce qu'on sauvegarde** : l'**état**, pas ce qui s'en déduit. Oui : entités persistantes, cases explorées, différences de la grille avec la carte d'origine, générateurs aléatoires, tick. Non : champs de flux, champs de vision, poses, chemins (replanifiés), particules, décor reconstruit depuis la carte.
- `unknown_components(registry)` liste les composants d'entités persistantes ni sauvegardés ni ignorés : un état oublié se voit **avant** de casser une partie (à vérifier dans un test du jeu).
- Les **énumérations** s'écrivent **par nom**, jamais en entier (réordonner une énumération casserait les sauvegardes) ; les clés des sérialiseurs sont stables.
- Aides : `to_json_value()`, `vec2_from_json()`, `vec3_from_json()`, `quat_from_json()` (flottants écrits exactement : relus au bit près).

Exemple complet : `Demo3D::setup_serializers()`, `save()` et `load()` dans [`demo3d.cpp`](../apps/tranche/demo3d.cpp) ; `StatesDemo::save_game()` et `load_game()` dans [`states_demo.cpp`](../apps/tranche/states_demo.cpp) (emplacements, vignette, sauvegarde automatique en alternance `auto_1` / `auto_2`, chargement derrière l'écran de chargement). L'**exactitude** (sauvegardé au tick 120, rechargé, la suite identique) est vérifiée par `tools/tests/check_references.py`.


---

## 7. Outils

Les outils de développement existent dans les builds Debug et Release, **pas** dans la version livrée (chapitre 8.1). Le code du jeu qui les utilise se protège par `#if !defined(MOTEUR_SHIPPING)` (ImGui n'y est pas lié).

### 7.1 Console et variables

- **Console** (`console.hpp`) : ouverte par la touche sous Échap (**²** sur un clavier français), avec `debug_ui`. Commandes intégrées : `help`, `set`, `get`, `reset`, `toggle`, `vars`, `echo`, `clear` ; celles de l'`Application` : `quit`, `pause`, `step`, `reload`, `assets`, `tables`, `exec`, `profile`, `screenshot`, `crash`.
- **Deux genres** de commandes : `Console::Kind::Tool` (immédiate, ne touche jamais la logique) et `Console::Kind::Game` (mise en file, exécutée au début du tick suivant, **enregistrée dans les rejeux**). Une triche est une commande de jeu.

```cpp
app.console().add("spawn", "<monstre> [nombre]", "fait apparaître des monstres", moteur::Console::Kind::Game,
                  [this](const moteur::Console::Args& args, moteur::Console& out) { /* ... */ out.print("ok"); });
```

- **Variables** (`variables.hpp`) : nommées avec des points par système (`jeu.vision`), typées, bornées, avec défaut et aide ; le système garde la référence et la lit où il en a besoin. Drapeaux : `Variable::Archive` (gardée dans `variables.cfg` des préférences ; seules ces variables sont relues du fichier, les autres lignes sont signalées au journal et ignorées), `Variable::Logic` (son changement passe par la file des commandes de jeu, donc rejoué).

```cpp
moteur::Variable& vision = app.console().variables().add_float(
    "jeu.vision", 8.0f, 1.0f, 30.0f, "portée de vue du héros (m)", moteur::Variable::Logic);
```

- **Au lancement** : `ApplicationConfig::startup_commands` reçoit des lignes de console. Le programme du jeu traduit lui-même ses arguments, comme la tranche : `+set nom valeur` devient la ligne `set nom "valeur"` (et le bac à sable accepte aussi `--exec fichier`). Dans la version livrée, **seules les lignes `set` passent** (pas de console, décision de la partie 6 du jalon 8).
- **Journal** (`log_file.hpp`) : tout message (`SDL_Log`...) va aussi dans `log.txt` des préférences (l'ancien en `log.1.txt`, rotation à 4 Mo), et dans la console.

### 7.2 Profiler et mesures

- `MOTEUR_PROFILE("jeu : IA")` (`profiler.hpp`) au début d'une portée : une zone par **système**, pas par entité ; fil principal ; noms littéraux. Fenêtre DEBUG > Profiler (arbre d'une image, statistiques), `profile start` / `profile stop fichier.json` (format Chrome Trace, `ui.perfetto.dev`), variable `profile.enabled`.
- **Tracy** : option CMake `MOTEUR_TRACY` (développement seulement).
- **Rapport de mesure** : `ApplicationConfig::report_json_path` écrit à la fin un JSON (temps CPU et intervalle entre images, centiles, images longues, zones, appels de dessin, temps GPU par passe avec `gpu_timing`, mémoire du processus et du GPU) ; le jeu y ajoute ses valeurs par `app.report_value("chargement_ms", ...)`. `python tools/measures/measure.py [build] [--size L H] [--seconds S] [--output DOSSIER] [--compare DOSSIER] [--scenes fight,crowd,crossing,loading]` lance les scènes de mesure (un rapport par scène et `summary.json`, par défaut dans `build/measures/<date>_<os>`) et signale toute mesure plus de 10 % moins bonne qu'une mesure précédente (budgets : partie 4 du [jalon 8](JALON_8_CONSOLIDATION.md#4-profilage--machine-minimale-scènes-de-mesure-et-budgets)).
- **Toujours mesurer en Release**, sans synchronisation verticale : le Debug active la validation du GPU.

### 7.3 Inspecteur et fenêtres de debug

Avec `ApplicationConfig::debug_ui`, `app.debug_tools()` donne les fenêtres ImGui du moteur : **inspecteur d'entités** (composants éditables), assets, entrées, audio, états, animation, données, console, profiler, et la barre de statistiques (`debug.stats`). Le jeu les met dans son menu et les dessine :

```cpp
#if !defined(MOTEUR_SHIPPING)
if (moteur::DebugTools* tools = app.debug_tools()) {
    tools->watch(world_, "Partie");           // forget(world_) dans le destructeur de la scène
    tools->watch(stack_, "États");
    tools->components().add<Sante>("Santé", [](Sante& s) { return ImGui::SliderFloat("Vie", &s.valeur, 0.0f, 1.0f); });
}
// dans render() : ImGui::BeginMainMenuBar()... tools->menu_items() ... puis tools->draw();
#endif
```

- Un composant **décrit** (chapitre 3.4) et enregistré dans `component_types()` est dessiné par sa description, sans rien écrire (`inspect_reflected()` : bornes, unités, couleurs, énumérations, listes, quaternions en degrés) ; `components().add_reflected<T>("Nom affiché")` lui donne un autre titre.
- `select(entity)` montre l'entité choisie dans le jeu ; les fenêtres ouvertes sont gardées dans `imgui.ini` des préférences.
- **Calques du monde** (`world_debug.hpp`) : `WorldDebugFlags::declare(variables)` crée `debug.grid`, `debug.clearance`, `debug.colliders`, `debug.paths`, `debug.flowfield`, `debug.view` ; `draw_world_debug(...)` les dessine dans la scène qui donne ses sources (grille, champs, vue).
- **Animation** : squelette, axes, pose de liaison, poids (`animation_debug.hpp`, fenêtre DEBUG > Animation).
- **Assertions** (`assert.hpp`) : `MOTEUR_ASSERT` (développement, retirée de la version livrée sans évaluer la condition), `MOTEUR_VERIFY` (toujours : ce qui protège les données du joueur), `MOTEUR_UNREACHABLE`. Un échec écrit fichier et ligne au journal, puis passe par le rapport de plantage. Jamais pour un fichier ou une entrée invalide (ceux-là sont refusés avec un message).

### 7.4 Éditeurs et bac à sable

- **Éditeur de niveaux** (jalon 9, partie 10 ; le socle de l'éditeur de production du jalon 11) : `editeur --level [levels/x.level.json]` (`--new-level` pour un niveau vide, `--self-test` pour son essai automatique), et DEBUG > Engine tests > Level editor dans le bac à sable. À gauche la **bibliothèque** : les préfabriqués et modèles de tous les dossiers d'assets (ceux du moteur, ceux du jeu montés), par dossier ou par famille de kit (lue dans les `.kit.json` et `catalogue.json` : la grille d'aimantation de chaque pièce), filtres (accessoires, sols, lumières, pièces de kit, modèles sans préfabriqué) et recherche, chaque asset avec sa **vignette rendue par le moteur** (`ThumbnailRenderer`, gardée sur disque dans les préférences, refaite quand le fichier change). Un clic choisit un préfabriqué pour l'outil Placer ; on peut aussi le **glisser** dans la vue ; clic droit : « Use as ground » (un sol à peindre), « Make its prefab » (un modèle sans préfabriqué en reçoit un, avec sa collision). Dans la vue : **sélection** au clic (Ctrl : ajoute, Maj : bascule), **poignées** déplacer / tourner / mettre à l'échelle (T, E, R), aimantation à la grille du kit (G) et aux surfaces, Fin pose la sélection sur ce qui est dessous, Ctrl+D duplique, Suppr efface, [ et ] tournent, Ctrl+Z / Ctrl+Y. Outils Peindre et Effacer (B, X) pour le sol, Point (P) pour les points nommés. À droite l'**inspecteur** : position, rotation, échelle, calque, les champs des composants du préfabriqué (la lumière d'un lampadaire : couleur, intensité, portée, ombres, `shadow_near`) gardés comme **surcharges** de l'instance, ses **propriétés** de jeu (JSON) ; les réglages du niveau (éclairage, calques, points, taille des morceaux, agent de navigation). Vue : **même rendu que le jeu** (`apply_lighting()`), nuit (N), maillage de navigation **reconstruit en arrière-plan** après chaque pause dans l'édition, murs et obstacles. **F5** fait jouer le niveau tel qu'il est **par la tranche** (préparé en mémoire, chargé par morceaux, le héros au point `start`, les créatures des apparitions ; F6 : sous le pointeur), Échap revient. `--focus X Z`, `--focus-height H`, `--show-collision` : la vue de départ (vérifier un endroit). Enregistrement atomique dans `assets/levels` des sources ; une copie est gardée si l'on quitte sans enregistrer. Le moteur fournit `EditableLevel` (édition, annuler / refaire, sans GPU), `Gizmo` (les poignées, en pixels et en monde) et les vignettes ; l'éditeur lui-même est `apps/bac_a_sable/level_editor.cpp`.

- **Éditeur de cartes** (les cartes v1 et v2 des jalons 6 et 7, en attendant la tranche en niveau v3) : le programme `editeur` (`editeur [carte | --new] [--top] [--self-test]`), et la même scène dans le bac à sable (DEBUG > Engine tests > Map editor). Tuiles, points, objets posés, morceaux et connecteurs, annuler / refaire, vérifications, essai de la carte depuis le départ (F5) ; enregistrement atomique dans `assets/maps` des sources. Le moteur en fournit le document sans GPU (`MapDocument` : édition, annulation, `validate`, écriture stable) ; l'éditeur lui-même (`apps/bac_a_sable/map_editor.cpp`) et les **types d'objets** (`apps/tranche/map_objects.cpp`, ceux de la tranche) ne sont pas dans le moteur : un jeu qui a ses propres objets s'en inspire en attendant l'éditeur de production (jalon dédié, révision de la [revue](REVUE_JALONS_1_A_7.md)). Décisions : [jalon 7, parties 7 et 8](JALON_7_OUTILS_ET_DONNEES.md).
- **Bac à sable** (`bac_a_sable`) : sans argument, un menu de toutes les scènes de test ; avec des options (`--demo3d`, `--slice` (la tranche sur son niveau ; `--slice-map` : sur la carte des jalons 6 et 7), `--skinned N`, `--world`, `--states`...), une scène directement, pour les scripts, captures et mesures. Liste des options : [`README.md`](../README.md). Il n'est jamais livré. Le code de la tranche qu'il lance avec `--slice` ou `--states` est dans `apps/tranche/` (bibliothèque `tranche_commun`).

---

## 8. Livrer

### 8.1 La configuration livrée (`MOTEUR_SHIPPING`)

Presets `windows-shipping` et `macos-shipping` (option CMake `MOTEUR_SHIPPING`, définition de compilation publique `MOTEUR_SHIPPING` pour le code du jeu). Ce qui est **retiré** (non compilé, pas seulement désactivé) :

- **Dear ImGui** et toutes les fenêtres de `DebugTools` (`Application::debug_tools()` rend toujours null) ;
- le **profiler** (`MOTEUR_PROFILE` vide) et **Tracy** (les deux options s'excluent) ;
- le **rechargement à chaud** (efsw non lié) ;
- la **console** : au démarrage, seules les lignes `set` (`+set`) passent ;
- `MOTEUR_ASSERT` (`MOTEUR_VERIFY` reste) ;
- les outils du dépôt (bac à sable, éditeur, tests) ne sont pas construits ; seuls les jeux le sont ;
- les crédits marqués `"build": "DEVELOPMENT"` (ImGui, efsw, PIX) et leurs licences.

Ce qui est **ajouté** (`cmake/Shipping.cmake`) : optimisation à l'édition de liens (LTO / LTCG), **CRT statique** sous Windows (`/MT`, triplet `x64-windows-static` : aucune DLL de Visual C++ à installer), **symboles à part** (PDB à côté de l'exécutable, dSYM extrait au paquet) pour les rapports de plantage. Le jeu livré lit ses tables **compilées** (`DataSource::Compiled`) et ses assets **préparés**, **depuis les archives seulement**.

Pour attraper tôt le « marche en dev, pas en livré », construire le preset livré et le paquet régulièrement (le script de vérification le fait, chapitre 9).

### 8.2 Les paquets : `moteur_package`

```cmake
add_executable(arpg_cook src/cook_main.cpp)        # l'outil de préparation du jeu (4.4)
target_link_libraries(arpg_cook PRIVATE moteur)
moteur_package(arpg MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/assets/livraison.json" COOK_TOOL arpg_cook
               IDENTIFIER com.editeur.arpg)       # [WINDOWS_APP_ID <GUID>]
```

```
cmake --preset windows-shipping
cmake --build build/windows-shipping --target arpg_package
```

La cible `<jeu>_package` n'existe que dans la configuration livrée. Elle :

1. prépare les assets avec le manifeste (incrémental) ;
2. assemble ce qui s'installe : l'exécutable, `archives/` (assets et shaders préparés, en `.pak`), `licenses/` ; **rien d'autre** : ce que le jeu écrit va dans le dossier du joueur ;
3. **Windows** : signe l'exécutable si `MOTEUR_SIGN_COMMAND` est donné, écrit l'**archive portable** `<jeu>-<version>-windows-x64.zip` et l'**installeur** `<jeu>-<version>-windows-x64-setup.exe` (Inno Setup, signé de la même façon ; sans Inno Setup, un avertissement et pas d'installeur) ;
   **macOS** : construit `<nom>.app` (`Info.plist`, icône, ressources dans `Contents/Resources`), le signe (`MOTEUR_MACOS_SIGN_IDENTITY`, **ad hoc** par défaut), le met dans `<jeu>-<version>-macos-arm64.dmg`, et fait notariser et agrafer les deux si `MOTEUR_MACOS_NOTARY_PROFILE` nomme un profil `notarytool` ;
4. garde l'exécutable et ses **symboles** de cette version dans `symbols/<jeu>-<version>-<os>/`.

Tout arrive dans `build/<preset>/packages/`. Le nom, la version, l'éditeur et l'icône sont ceux de `moteur_add_game`. L'identité de l'installeur Windows est dérivée de l'éditeur et du nom (stable d'une version à l'autre : une mise à jour remplace l'installation précédente), sauf `WINDOWS_APP_ID`.

| Variable CMake (cache) | Rôle |
|---|---|
| `MOTEUR_SIGN_COMMAND` | Windows : la commande qui signe un fichier, son chemin ajouté à la fin (par exemple `signtool sign /fd sha256 /tr <URL d'horodatage> /td sha256 /a`) ; vide : non signé, SmartScreen avertira |
| `MOTEUR_MACOS_SIGN_IDENTITY` | macOS : l'identité `codesign` (`Developer ID Application: ...`) ; `-` : signature ad hoc |
| `MOTEUR_MACOS_NOTARY_PROFILE` | macOS : le profil du trousseau de `xcrun notarytool` (`store-credentials`) ; vide : pas de notarisation, Gatekeeper demandera au joueur de confirmer |

Les comptes de signature (certificat Windows, Apple Developer Program) sont **repoussés** (décision du 2026-10-05) : tout est prêt à s'activer par ces trois variables. Décisions : [jalon 8, parties 10 et 11](JALON_8_CONSOLIDATION.md#10-packaging-windows). Les vérifications macOS du jalon 8 (paquet, rapport de plantage, plein écran) restent à faire : [TEST_MAC.md](TEST_MAC.md).

**Vérifier un paquet Windows** : `python tools/package/check_package.py [build/windows-shipping] [--game arpg] [--replay FICHIER --expect EMPREINTE] [--freeze-after TICK] [--run-seconds S] [--pixel-size L H] [--keep]` : pas de DLL de Visual C++, informations de version, installation silencieuse pour l'utilisateur courant (deux fois : une mise à jour), raccourci et désinstallation, partie jouée, désinstallation qui laisse le dossier du joueur, archive portable identique à l'installation. Avec `--replay` et `--expect`, la partie rejoue l'entrée enregistrée et sa capture doit avoir l'empreinte donnée (les 12 premiers chiffres hexadécimaux de son SHA-256, comme dans `tests/data/references.json` : même image qu'en développement) ; sans rejeu, le jeu tourne `--run-seconds` (3 par défaut) et doit se fermer proprement (code de sortie 0). Pour la tranche (`--game tranche`, le défaut), ce sont son rejeu et sa capture de référence (« slice »), figée au tick 340, 9 s, 1280 × 720. `examples/jeu_minimal` montre un jeu empaqueté (`moteur_package`, cible `jeu_minimal_package`).

### 8.3 Rapports de plantage

- Installés par l'`Application` (`ApplicationConfig::crash_reports`, vrai par défaut) dans `crash_reports/` du dossier du joueur (`crash.hpp`).
- Un plantage (accès invalide, `abort`, assertion échouée, exception que personne n'attrape) écrit, sous un nom daté (`2026-10-05_20-48-03`, suivi de `_2`, `_3`... si un rapport de la même seconde existe déjà ; le `.txt` et le `.dmp` d'un même événement ont le même nom), un **rapport `.txt`** (versions du moteur et du jeu, OS, GPU et pilote, mémoire, événement, pile en module + décalage, 200 dernières lignes du journal) et, sous Windows, un **minidump réduit `.dmp`** ; puis montre au joueur `crash_message` (`{report}` = le chemin du rapport ; défaut en anglais). Sous macOS, le système écrit aussi son rapport.
- **GPU perdu** (pilote réinitialisé, carte retirée) : rapport du même genre, message `gpu_lost_message`, fin propre, `ended_on_error()` vrai.
- Le rapport est pour les développeurs, en anglais. Pour y lire les noms de fonctions : `python tools/crash/read_report.py REPORT.txt SYMBOLS` (`SYMBOLS` : le dossier `packages/symbols/<jeu>-<version>-<os>/`) (dbghelp sous Windows, `atos` sous macOS), avec les symboles **de la même version** : **archiver `packages/symbols/` à chaque version livrée** (des symboles d'un autre build sont inutilisables).
- **Essayer la chaîne** : commande `crash [acces|abort|exception|assertion|gpu|frame]` (`frame` : un échec du GPU dans une image, qui ne doit **pas** terminer le jeu), ou une option du jeu qui appelle `moteur::crash_now(...)` / `renderer().simulate_device_lost()` / `renderer().simulate_frame_failure()` comme `tranche --test-plantage` (qui a aussi `update` : une exception dans la logique). `MOTEUR_CRASH_SILENT` (variable d'environnement) supprime le message pour les vérifications automatiques.
- L'**envoi** automatique des rapports à un service est une décision du jeu (avec le consentement du joueur), avant la première version publique.

### 8.4 Le dossier du joueur

Tout ce que le jeu écrit va dans `app.preferences_directory()`, c'est-à-dire `SDL_GetPrefPath(organization, application)` (sous Windows `%APPDATA%\<organization>\<application>\`). Jamais à côté de l'exécutable : `Program Files` est en lecture seule, et un `.app` signé ne doit pas changer.

| Fichier | Écrit par |
|---|---|
| `log.txt`, `log.1.txt` | le journal |
| `crash_reports/` | les rapports de plantage (`.txt`, `.dmp`) |
| `display.json` | les réglages d'affichage (avec `display_settings` ; l'ancien `affichage.json` est relu puis réécrit) |
| `audio.json` | les volumes du joueur |
| `variables.cfg` | les variables `Archive` qui diffèrent de leur défaut |
| `imgui.ini`, `profiles/profile.json` | les outils de développement seulement |
| ce que le jeu choisit | ses sauvegardes (la tranche : `sauvegardes/`), les liaisons du joueur (la tranche : `demo3d_bindings.json`) |

La désinstallation retire le programme et **laisse** ce dossier.

### 8.5 Réglages d'affichage

`DisplaySettings` (`display.hpp`) : fenêtré ou plein écran fenêtré (`WindowMode`), écran (`displays()`), synchronisation, limite d'images, résolution de rendu (0,5 à 1 ; l'interface reste nette), anticrénelage, qualité des ombres (`ShadowQuality`). Avec `ApplicationConfig::display_settings`, ils sont lus de `display.json` au démarrage ; `app.apply_display(reglages)` les applique **sans redémarrer** et les écrit. Le plein écran exclusif n'est pas proposé (décision de la partie 8 du jalon 8).

Le **menu** et ses **libellés** sont au jeu (le moteur ne donne que des noms techniques, `window_mode_name()`, `shadow_quality_name()`). La tranche montre le modèle : un changement d'écran ou de mode est annulé s'il n'est pas confirmé dans les 15 secondes (`DisplayOptionsState` dans `states_demo.cpp`, avec `ticks_from_seconds(15.0)` ; à côté, `AudioOptionsState` pour les volumes et `ControlsOptionsState` pour les touches).

---

## 9. Vérifier

### 9.1 Le script de vérification

`python tools/ci/verify.py` fait ce que fera la CI (repoussée, décision du 2026-10-05), sur l'OS où il tourne, et s'arrête au premier échec :

1. pour chaque configuration (Debug et Release ; `--quick` : Release seule ; `--configs debug,release` pour choisir) : configuration avec le preset du dépôt et les **avertissements en erreurs**, build, dépendances d'en-têtes (Windows), `ctest`, `--check-data` et `--compile-data` ;
2. `examples/jeu_minimal` configuré avec ses propres presets, construit et lancé 2 s **de l'extérieur**, comme un jeu, dans la dernière configuration de la liste (`--no-example`) ;
3. Windows : le MSL réexporté, échec s'il diffère du dépôt (`--no-msl`) ;
4. les captures de référence, l'exactitude des sauvegardes, l'auto-test de l'éditeur (`tools/tests/check_references.py`, avec `--quick` si le script l'a reçu ; `--no-references`) ;
5. l'analyse statique (`tools/ci/static_analysis.py`, `--no-analysis`) ;
6. les tests unitaires construits avec AddressSanitizer (preset `<os>-asan`) et lancés (`--no-asan`) ;
7. la référence de l'API, sans avertissement de Doxygen (`tools/docs/check_docs.py`, `--no-docs`) ;
8. la configuration livrée (preset `<os>-shipping`) et le paquet de la tranche (`tranche_package`) ; sous Windows, ce paquet installé, joué sur le rejeu de référence et désinstallé (`tools/package/check_package.py`) (`--no-package` ; sauté par `--quick`).

Les étapes 3 à 5 utilisent le build Release : elles sont sautées si `--configs` ne le contient pas.

`--keep-going` va jusqu'au bout pour voir tous les échecs. Le script prépare son environnement (Visual Studio, CMake et Ninja de CLion, vcpkg) ; sous Windows en français, l'utiliser plutôt que des commandes à la main (piège des dépendances d'en-têtes : [README](../README.md), « Build incrémental incohérent »). Décisions : [jalon 8, partie 2](JALON_8_CONSOLIDATION.md#2-intégration-continue-windows-et-macos).

Pour l'ARPG, le même script s'écrira sur ce modèle (le moteur, l'exemple extérieur et la tranche restent vérifiés par celui du moteur).

### 9.2 Tests unitaires

`tests/` (doctest), lancés par `ctest --test-dir build/windows-debug --output-on-failure` ; ils couvrent la logique sans GPU (temps, tables, sauvegardes, chemins, collisions, archives, entrées, console...). Option `MOTEUR_BUILD_TESTS`. Un jeu teste sa logique de la même façon, sans fenêtre : la plupart des systèmes du moteur (registre, `NavGrid`, `DataTables`, `SaveGame`, `Input` par `set_frame()`) se construisent sans `Application`.

### 9.3 Captures de référence

`python tools/tests/check_references.py [build] [--record] [--quick]` : des scènes lancées avec une graine, un rejeu et `--freeze-after N --capture`, dont l'empreinte est comparée à `tests/data/references.json` (par plateforme) ; plus le test d'exactitude des sauvegardes et l'auto-test de l'éditeur. `--record` enregistre les nouvelles empreintes, **après** avoir vérifié que le changement d'image est voulu. Une capture ne doit dépendre d'aucun réglage du joueur : les vérifications laissent `display_settings` éteint et mettent de côté les liaisons du joueur. Elles dépendent du GPU et du pilote : elles restent un contrôle local.

### 9.4 Fuzzing, sanitizers, analyse statique

- **Fuzzing** : presets `windows-fuzz` / `macos-fuzz` (libFuzzer + ASan), un programme par lecteur de fichier dans `tests/fuzz/fuzz.cpp` (`map`, `effect`, `animation`, `save`, `ktx2`, `gltf`, `table`, `bindings`, `image`, `archive`) ; `python tools/fuzz/fuzzer.py [--seconds S] [--targets map,effect] [--parallel] [build]` (build par défaut `build/<os>-fuzz` ; corpus et trouvailles dans `build/fuzz/<cible>/` ; code de sortie 1 si quelque chose est trouvé). Une trouvaille, une fois corrigée, devient un cas de `tests/test_robustness.cpp`. Un lecteur de fichier ajouté par le jeu (son format de niveau) mérite son programme de fuzzing.
- **Sanitizers** : presets `windows-asan` / `macos-asan` (`MOTEUR_ASAN` ; UBSan en plus sous macOS) ; `macos-tsan` (`MOTEUR_TSAN`, ThreadSanitizer : les accès concurrents des tâches ; Clang seulement).
- **Analyse statique** : `python tools/ci/static_analysis.py [build] [--files MOTIF] [--all]` (`clang-tidy` avec `.clang-tidy` : `bugprone`, `clang-analyzer`, `performance` ; les sources du moteur, et avec `--all` aussi `apps/`) ; tout avertissement échoue.
- **Documentation** : `python tools/docs/check_docs.py [en-tête.hpp...] [--output DOSSIER]` : sans en-tête, tout avertissement de Doxygen sur les en-têtes publics échoue ; avec des noms d'en-têtes, seuls leurs avertissements sont montrés (`--output` : où écrire, par défaut `build/docs_check/`).

Décisions : [jalon 8, partie 8](JALON_8_CONSOLIDATION.md#8-robustesse--plantages-affichage-analyses).

---

## 10. Ce qui n'est pas encore là

Prévu au **jalon 9 « Niveau production »** (avant le premier jalon de l'ARPG) et au **jalon 10 « Rendu et effets »** ; le détail et les raisons sont dans la [revue des jalons 1 à 7](REVUE_JALONS_1_A_7.md) :

- **jalon 9** (en cours : règles d'erreur, réflexion, préfabriqués, identifiants d'assets et formats préparés sont faits) : monde en vraie 3D pour la logique (collisions, maillage de navigation, plusieurs niveaux) ; vision et sélection des objets à la souris en 3D ; niveaux par morceaux ; image parallèle ; éditeur de niveaux ;
- **jalon 9 bis** : système d'interface maison et texte SDF ; localisation (textes du joueur, `Input::describe`) ; transparence (`BLEND`) et variantes de matériaux ; *root motion* et couches additives ; événements sonores en données ; MSL compilé sur Mac ;
- **jalon 10** : post-traitement (bloom, occlusion ambiante, étalonnage, TAA), *clustered forward* au-delà de 32 lumières, profondeur inversée, particules sur le GPU, physique (Jolt, à proposer) ;
- **jalon 8, encore ouvert** : la CI hébergée, les comptes de signature, les vérifications macOS ([TEST_MAC.md](TEST_MAC.md)).

Le code de jeu écrit avant ces jalons gagne à passer par les fonctions du moteur plutôt que par des raccourcis (`y = 0` partout, appels de fichiers sonores, textes en dur) : ce sont ces endroits qui changeront.
