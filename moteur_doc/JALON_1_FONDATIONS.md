# Jalon 1 - Fondations

Retour à la [roadmap générale](ROADMAP.md).

## Objectif

Une fenêtre SDL3 qui affiche un **sprite texturé** avec **SDL_GPU**, construite à partir d'un projet **CMake + vcpkg** et exécutée à l'identique sur **Windows et macOS**.

Ce jalon valide toute la chaîne technique (build, deux OS, shaders, GPU) avant d'écrire la moindre ligne de gameplay. S'il fonctionne sur les deux machines, le reste du moteur repose sur des bases saines.

## Comment lire ce document

Chaque partie suit la même structure :

- **But** : ce qu'on cherche à obtenir.
- **Tâches** : ce qu'il faut faire, dans l'ordre.
- **Questions à se poser** : les décisions à trancher avant ou pendant le travail. Ce sont les points où on se trompe le plus souvent. À noter dans la section [Décisions à consigner](#décisions-à-consigner) une fois tranchées.
- **Pièges connus** : erreurs fréquentes.
- **Validation** : comment savoir que la partie est terminée.

Les points marqués *(à vérifier)* sont des informations dont je ne suis pas certain à 100 % ou qui évoluent vite. Il faut les confirmer dans la documentation actuelle avant de s'appuyer dessus.

## Sommaire

1. [Vue d'ensemble et ordre de travail](#1-vue-densemble-et-ordre-de-travail)
2. [Environnement et outils](#2-environnement-et-outils)
3. [Squelette CMake + vcpkg](#3-squelette-cmake--vcpkg)
   - [Squelette réalisé et choix pris](#squelette-réalisé-et-choix-pris)
   - [Documentation des fichiers du projet](FICHIERS_DU_PROJET.md)
4. [Fenêtre SDL3 et boucle de jeu](#4-fenêtre-sdl3-et-boucle-de-jeu)
5. [Périphérique GPU et premier écran](#5-périphérique-gpu-et-premier-écran)
6. [Chaîne de compilation des shaders](#6-chaîne-de-compilation-des-shaders)
7. [Sprite texturé](#7-sprite-texturé)
8. [Débogage et outils](#8-débogage-et-outils)
9. [Critères de fin de jalon](#9-critères-de-fin-de-jalon)
10. [Risques principaux](#10-risques-principaux)
11. [Décisions à consigner](#décisions-à-consigner)

---

## 1. Vue d'ensemble et ordre de travail

### Dépendances entre les parties

```
2. Environnement
      |
3. Squelette CMake + vcpkg
      |
4. Fenêtre + boucle de jeu
      |
5. Périphérique GPU + écran coloré
      |
      +--------------------+
      |                    |
6. Chaîne de shaders   7a. Chargement d'image (indépendant des shaders)
      |                    |
      +--------------------+
      |
7b. Pipeline + sprite affiché
```

### Ordre recommandé

1. Faire compiler un programme vide sur **les deux OS** avant tout le reste. C'est la partie la plus ennuyeuse et la plus rentable.
2. Ouvrir une fenêtre et boucler proprement.
3. Effacer l'écran avec une couleur via SDL_GPU (preuve que le GPU répond).
4. Afficher un triangle en couleur unie (valide les shaders sans texture).
5. Passer au quad texturé.

À chaque étape, tester sur Windows **et** Mac avant de continuer. Ne jamais accumuler deux étapes non testées sur le second OS.

### Questions générales

- Quel est le premier OS de développement ? (Recommandation : celui où tu es le plus à l'aise, puis vérifier l'autre à chaque étape.)
- Comment synchronises-tu le code entre les deux machines ? (Git est indispensable, voir partie 2.)
- Quelle est la limite de temps du jalon ? Si une partie dépasse largement son estimation, est-ce le signe qu'il faut simplifier ?

---

## 2. Environnement et outils

### But

Avoir sur les deux machines une chaîne de compilation identique dans son comportement.

### Tâches

- [ ] Installer et configurer **Git** sur les deux machines, créer le dépôt.
- [ ] **Windows** : installer Visual Studio Build Tools (compilateur MSVC), CMake, Ninja, vcpkg.
- [ ] **macOS** : installer les Xcode Command Line Tools (`xcode-select --install`), CMake, Ninja, vcpkg. Installer Xcode complet (nécessaire plus tard pour le débogueur Metal et la signature).
- [ ] Installer **CLion** sur les deux machines et vérifier qu'il détecte la toolchain.
- [ ] Installer **RenderDoc** (Windows) pour le débogage GPU.
- [ ] Noter les versions de chaque outil (voir partie [Décisions à consigner](#décisions-à-consigner)).

### Questions à se poser

- **Quel compilateur sur Windows ?** MSVC (meilleur débogage, ABI standard Windows) ou Clang/MinGW ? Le choix affecte les triplets vcpkg et les binaires prébuilt disponibles.
- **Quelle version minimale de CMake ?** Il faut une version récente pour les presets et un bon support de vcpkg (viser 3.25 ou plus).
- **Où installer vcpkg ?** Un dossier commun hors du projet (réutilisable) ou un sous-module Git dans le projet (reproductible) ?
- **Comment gérer les fins de ligne** entre Windows et Mac ? Sans réglage (`.gitattributes`, `core.autocrlf`), tu auras des diffs parasites.
- **Quelle version minimale de macOS cibler ?** Cela fixe `CMAKE_OSX_DEPLOYMENT_TARGET` et les Mac supportés.
- **Apple Silicon uniquement ou aussi Intel ?** (Recommandation : arm64 seul au départ ; un binaire universel peut venir plus tard.)

### Pièges connus

- Sur Windows, lancer CMake depuis un terminal qui n'a pas l'environnement MSVC chargé (utiliser le « Developer PowerShell » ou laisser CLion gérer).
- Deux versions de CMake installées et mélangées.
- Un vcpkg cloné à deux endroits avec des versions de ports différentes : le build diffère entre les machines.

### Validation

- [ ] Un « Hello World » C++20 se compile et s'exécute via CLion sur Windows.
- [ ] Le même « Hello World » se compile et s'exécute via CLion sur Mac.
- [ ] Le dépôt Git se clone proprement sur les deux machines.

---

## 3. Squelette CMake + vcpkg

### But

Un projet dont la configuration et le build sont **reproductibles** sur les deux OS avec une seule commande, sans installation manuelle de bibliothèque.

### Structure de dossiers prévue à terme

Le squelette actuel n'en contient qu'une partie : voir [Squelette réalisé et choix pris](#squelette-réalisé-et-choix-pris). Les éléments marqués « à venir » seront ajoutés quand ils serviront.

```
moteur/
  CMakeLists.txt           # racine
  CMakePresets.json        # presets windows / macos
  vcpkg.json               # dépendances (mode manifeste)
  .gitignore
  .gitattributes
  README.md
  cmake/                   # modules CMake utilitaires (avertissements, shaders)
  src/moteur/              # bibliothèque du moteur
    include/moteur/        # en-têtes publics
    core/                  # à venir : application, boucle, temps, log
    platform/              # à venir : fenêtre, entrées (enveloppes SDL)
    renderer/              # à venir : GPU, pipelines, textures
  apps/bac_a_sable/        # exécutable de test
  shaders/                 # à venir : sources HLSL
  assets/                  # à venir : textures de test
  third_party/             # à venir si besoin : dépendances hors vcpkg
  tests/                   # à venir : tests unitaires
```

### Tâches

- [x] Créer `CMakeLists.txt` racine (C++20, sortie des binaires, avertissements activés).
- [x] Créer `vcpkg.json` en **mode manifeste** avec la dépendance SDL3.
- [x] Créer `CMakePresets.json` avec un preset par OS (Windows, macOS) qui référence la toolchain vcpkg.
- [x] Créer `.gitignore` (dossiers de build, `.idea/`, binaires) et `.gitattributes`.
- [ ] Configurer CLion pour utiliser les presets.
- [ ] Compiler un exécutable qui lie SDL3 et affiche sa version au démarrage.

### Questions à se poser

- **Mode manifeste ou mode classique de vcpkg ?** Le mode manifeste (`vcpkg.json` dans le projet) épingle les dépendances avec le code. C'est le choix recommandé.
- **Comment épingler les versions ?** Une `builtin-baseline` dans `vcpkg.json` garantit que les deux machines résolvent les mêmes versions. Sans elle, deux machines peuvent compiler deux versions différentes de SDL.
- **Lien statique ou dynamique pour SDL3 ?** Statique : un seul exécutable, distribution simple. Dynamique : plus de fichiers à livrer (DLL / dylib), mais mises à jour de SDL indépendantes. Sur Mac, attention à l'emplacement des bibliothèques dynamiques dans un bundle `.app`.
- **Quels triplets vcpkg ?** `x64-windows` (ou `x64-windows-static`), `arm64-osx`. Le triplet fixe aussi le mode statique/dynamique.
- **Une seule cible CMake ou plusieurs (bibliothèque + exécutable) ?** Séparer dès maintenant une bibliothèque `moteur` d'un exécutable `bac_a_sable` facilite les tests et la réutilisation par l'ARPG. Mais cela ajoute de la structure.
- **Où l'ARPG viendra-t-il se brancher ?** Le moteur sera-t-il une bibliothèque consommée par le projet `ARPG` (via `add_subdirectory` ou un package) ? Cette décision influence la structure CMake, à réfléchir dès maintenant même si on ne l'implémente pas.
- **Quels avertissements du compilateur activer ?** (`/W4` sur MSVC, `-Wall -Wextra` sur Clang.) Plus tôt on les active, moins il y a de dette.
- **Les avertissements sont-ils des erreurs ?** Pratique pour garder le code propre, pénible avec du code tiers. À réserver à ton propre code.
- **Comment sont nommés les dossiers et les cibles ?** Convention à fixer une fois (snake_case, PascalCase...).
- **SDL_shadercross vient-il de vcpkg ou se compile-t-il à part ?** *(à vérifier)* : il pourrait ne pas être disponible en port vcpkg. Voir partie 6.
- **Comment charger une image ?** SDL_image (port vcpkg `sdl3-image`, *à vérifier*) ou `stb_image` (un seul en-tête, aucune dépendance) ? Voir partie 7.

### Pièges connus

- Oublier de committer `vcpkg.json` ou la baseline : le projet ne se reconstruit pas sur l'autre machine.
- Le premier build vcpkg est long (compilation de SDL3 et de ses dépendances). Ne pas croire à un blocage.
- Sur Mac, mélanger les architectures (un vcpkg en x64 sous Rosetta et un CMake en arm64).
- Des chemins avec espaces ou accents (ton dossier s'appelle « Moteur + ARPG ») qui cassent certains scripts. À surveiller dans les commandes CMake personnalisées : toujours guillemeter les chemins.
- Utiliser des chemins absolus dans les presets qui ne sont valides que sur une machine.

### Squelette réalisé et choix pris

Le squelette est créé dans `moteur/`. Il a été **écrit mais pas encore compilé** : le premier build dans CLion reste à faire (voir la validation ci-dessous).

Le détail de chaque fichier (rôle, contenu, quand le modifier) est dans la [documentation des fichiers du projet](FICHIERS_DU_PROJET.md).

#### Ce qu'il contient

```
moteur/
  CMakeLists.txt
  CMakePresets.json
  vcpkg.json
  cmake/Warnings.cmake
  src/moteur/                       bibliothèque "moteur"
    CMakeLists.txt
    include/moteur/version.hpp
    version.cpp
  apps/bac_a_sable/                 exécutable de test
    CMakeLists.txt
    main.cpp
  .gitignore
  .gitattributes
  README.md
```

Le résultat attendu est un exécutable qui affiche `SDL 3.x.x`. Cela prouve que CMake, vcpkg et le lien avec SDL3 fonctionnent. Le périmètre s'arrête volontairement là : la fenêtre, la boucle et le GPU viennent dans les parties suivantes.

#### Choix pris et raisons

| Sujet | Choix | Raison |
|---|---|---|
| Compilateur Windows | MSVC (Visual Studio 2022, charge « Développement Desktop en C++ ») | Meilleur support de vcpkg, du débogage et des outils DirectX que MinGW |
| Version de CMake | Minimum 3.25 ; CLion fournit la 4.3.1 | Requis par la version 6 des presets |
| Générateur | Ninja | Rapide, identique sur les deux OS, fourni par CLion |
| vcpkg | Version intégrée à CLion ; `VCPKG_ROOT` en repli pour la ligne de commande | Aucune installation manuelle |
| Mode vcpkg | Manifeste (`vcpkg.json`) avec baseline épinglée | Mêmes versions de SDL3 sur les deux machines |
| Lien SDL3 | Statique | Un seul exécutable, pas de DLL ni de dylib à livrer, packaging Mac plus simple |
| Triplet Windows | `x64-windows-static-md` | Bibliothèques statiques avec le runtime C dynamique (`/MD`) |
| Triplet macOS | `arm64-osx` | Apple Silicon uniquement |
| Cible macOS | 13.0 minimum | Modifiable dans les presets |
| Structure CMake | Bibliothèque `moteur` + exécutable `bac_a_sable` | L'ARPG pourra réutiliser la bibliothèque sans le bac à sable |
| Édition de liens SDL | `PUBLIC` sur la bibliothèque | Les utilisateurs de `moteur` récupèrent SDL3 automatiquement |
| Standard | C++20, extensions du compilateur désactivées | Code plus portable entre MSVC et Clang |
| Avertissements | `/W4` (MSVC), `-Wall -Wextra -Wpedantic` (Clang), sans « warnings as errors » | Rester strict sur notre code sans bloquer le build à chaque mise à jour du compilateur |
| Langue | Code (identifiants, commentaires) en anglais ; documentation en français | Cohérence avec SDL et l'écosystème C++ |
| Git | Le dépôt sera créé plus tard, à la main | `.gitignore` et `.gitattributes` sont déjà prêts |

#### Ce qui reste à faire pour clore la partie 3

- Activer l'intégration vcpkg de CLion (`Settings > Build, Execution, Deployment > Vcpkg`).
- Activer le profil du preset `windows-debug` avec la toolchain Visual Studio.
- Compiler et lancer `bac_a_sable` sur Windows.
- Refaire les mêmes étapes sur le Mac avec `macos-debug`.

#### Questions encore ouvertes

- **Le preset Windows force `CMAKE_CXX_COMPILER=cl`.** Si CLion ne trouve pas le compilateur en utilisant ce preset, la question devient : garder ce réglage ou laisser la toolchain de CLion décider ?
- **La baseline vcpkg est celle du jour de création.** Quand la mettre à jour ? *(Suggestion : uniquement entre deux jalons, pour éviter qu'une mise à jour casse un jalon en cours.)*
- **Faut-il activer les tests dès maintenant ?** Un dossier `tests/` avec Catch2 ou doctest peut être ajouté dès le jalon 2. Le faire trop tard rend l'ajout plus pénible.

### Validation

- [ ] `cmake --preset windows-debug` puis `cmake --build --preset windows-debug` réussit sur Windows depuis un clone propre.
- [ ] `cmake --preset macos-debug` puis `cmake --build --preset macos-debug` réussit sur Mac depuis un clone propre.
- [ ] L'exécutable affiche la version de SDL3 sur les deux OS.
- [ ] CLion ouvre le projet sur les deux machines sans configuration manuelle.

---

## 4. Fenêtre SDL3 et boucle de jeu

### But

Une fenêtre stable, une boucle de jeu à **pas de temps fixe** pour la logique, et une gestion propre des événements et de la fermeture.

### Tâches

- [x] Initialiser SDL (`SDL_Init` avec le sous-système vidéo).
- [x] Créer la fenêtre (`SDL_CreateWindow`) avec les bons drapeaux.
- [x] Écrire la boucle principale : événements, mise à jour, rendu.
- [x] Implémenter le **pas de temps fixe** avec accumulateur.
- [x] Gérer la fermeture propre (`SDL_EVENT_QUIT`, libération des ressources dans l'ordre inverse).
- [ ] Mettre en place un système de **journalisation** minimal (niveaux, sortie console).
- [ ] Gérer le redimensionnement et l'échelle d'affichage (écrans Retina).
- [x] Afficher les FPS et le temps de frame (titre de fenêtre pour commencer).

### Le pas de temps fixe

Principe : la logique de jeu avance toujours par petits pas identiques (par exemple 1/60 s), quel que soit le rythme du rendu. On accumule le temps réel écoulé et on exécute autant de mises à jour fixes que nécessaire. Le rendu se fait ensuite une fois par frame.

```
accumulateur += temps_ecoule
tant que accumulateur >= pas_fixe:
    mettre_a_jour(pas_fixe)
    accumulateur -= pas_fixe
rendre(interpolation = accumulateur / pas_fixe)
```

Pourquoi c'est important pour l'ARPG : la simulation (combats, calculs de dégâts, IA) doit donner les mêmes résultats quelle que soit la vitesse de la machine. Cela rend aussi les tests et la reproductibilité bien plus simples.

### Questions à se poser

- **Quelle fréquence de mise à jour fixe ?** 60 Hz est classique. 30 Hz est plus léger pour une logique simple. 120 Hz pour une précision accrue. Le choix impacte le coût CPU et la précision des collisions.
- **Comment éviter la « spirale de la mort » ?** Si une frame est très longue (pause, débogueur), l'accumulateur explose et le jeu tente de rattraper des centaines de pas. Il faut **plafonner** le temps écoulé par frame. Quel plafond choisir ?
- **Interpoler le rendu ou non ?** Sans interpolation, le mouvement peut saccader quand le rendu n'est pas multiple de la logique. Avec, il faut conserver l'état précédent et l'état courant. À décider maintenant, car cela change la structure des données du jeu.
- **Comment mesurer le temps ?** `SDL_GetPerformanceCounter` et `SDL_GetPerformanceFrequency` offrent une horloge haute résolution. Éviter les horloges non monotones.
- **VSync activé ou non ?** Le VSync évite le déchirement d'image et économise le GPU. Sans VSync, il faut limiter les FPS soi-même. Configurable par l'utilisateur ? *(se règle côté SDL_GPU, voir partie 5)*
- **Boucle classique ou « main callbacks » de SDL3 ?** SDL3 propose `SDL_AppInit / SDL_AppIterate / SDL_AppEvent / SDL_AppQuit`. Cela simplifie certaines plateformes mais impose une structure. Une boucle `while` classique donne plus de contrôle et se lit mieux pour apprendre.
- **Où placer la limite entre moteur et application ?** Une classe `Application` qui possède la boucle et appelle des fonctions de l'utilisateur ? Une interface `Jeu` avec `mise_a_jour` et `rendu` ? C'est la première décision d'API de ton moteur : réfléchis à ce que l'ARPG devra implémenter.
- **Threads ?** Tout dans un seul thread au départ. Faut-il tout de même concevoir pour un futur thread de rendu ou de chargement ? (Recommandation : non, ne pas anticiper.)
- **Gestion des erreurs : exceptions, codes de retour, ou `std::expected` ?** À décider avant que le code ne se répande. C++20 n'a pas `std::expected` en standard (C++23) : l'utiliser implique une bibliothèque ou une implémentation maison.
- **Comment journaliser ?** Bibliothèque (spdlog) ou petit utilitaire maison ? Écrire dans un fichier ? Les logs de SDL (`SDL_Log`) suffisent au début.
- **Quelle taille de fenêtre par défaut et faut-il la mémoriser ?** Mode fenêtré, plein écran, plein écran fenêtré sans bordure ?
- **Que se passe-t-il quand la fenêtre est réduite ou perd le focus ?** Continuer la logique ? Mettre en pause ? Ne plus rendre ?

### Pièges connus

- **Écrans haute densité (Retina)** : la taille de la fenêtre en points n'est pas la taille en pixels. Sans le drapeau `SDL_WINDOW_HIGH_PIXEL_DENSITY`, l'image est floue sur Mac. Il faut alors utiliser la taille **en pixels** pour le rendu et non celle de la fenêtre.
- Utiliser un `float` pour accumuler le temps : la précision se dégrade. Préférer des entiers (compteurs de ticks) ou des `double`.
- Faire du travail lourd dans la gestion d'événements.
- Ne pas libérer les ressources dans le bon ordre à la fermeture (GPU avant fenêtre, etc.) : plantages à la sortie.
- Le débogueur en pause pendant qu'on est dans la boucle : sans plafond, le jeu « saute » au redémarrage.

### Implémentation réalisée

Code dans `src/moteur/application.cpp` et `include/moteur/application.hpp` (voir la [documentation des fichiers](FICHIERS_DU_PROJET.md)). Testé sur Windows uniquement.

**Structure**
- `Application` possède SDL et la fenêtre (RAII : le constructeur les crée, le destructeur les libère dans l'ordre inverse) et contient la boucle `run()`.
- `Game` est l'interface que le programme implémente : `update(dt)` à fréquence fixe, `render(alpha)` une fois par frame, `on_event()` pour les événements bruts.
- Le bac à sable (`apps/bac_a_sable/main.cpp`) implémente `Game`. L'option `--run-seconds N` le fait se fermer seul, ce qui permet un test automatique.

**Décisions prises**
- Logique à **60 Hz**, avec un plafond de **0,25 s** de rattrapage par frame.
- Boucle `while` classique, sans les *main callbacks* de SDL3.
- Une erreur d'initialisation (SDL ou fenêtre) lève une exception `std::runtime_error`, attrapée dans `main`.
- `alpha` est transmis à `render()` pour permettre l'interpolation, mais le bac à sable ne l'utilise pas encore.
- Fenêtre redimensionnable avec `SDL_WINDOW_HIGH_PIXEL_DENSITY`. La taille en points et en pixels est écrite dans les logs.
- Les FPS et ticks par seconde sont affichés dans le titre de la fenêtre.

**Mesure** : avec `--run-seconds 3`, le programme a simulé 3,017 s en 181 ticks (60 Hz × 3,017 s = 181) avec 1960 frames, puis s'est fermé avec le code 0.

**Provisoire**
- `SDL_Delay(1)` en fin de boucle évite de saturer le CPU. Il sera remplacé par le VSync du swapchain (partie 5). En attendant, le rendu tourne à plusieurs centaines de FPS.
- `on_event()` expose l'événement SDL brut. Une abstraction des entrées le remplacera au jalon 3.
- La journalisation se limite à `SDL_Log`, sans niveaux configurables.
- Le redimensionnement n'est pas encore géré : rien ne dépend de la taille tant qu'il n'y a pas de rendu.

### Validation

- [ ] La fenêtre s'ouvre, se redimensionne et se ferme proprement sur Windows et Mac.
- [x] La logique tourne à fréquence fixe (compteur de pas affiché, stable). *Vérifié sur Windows.*
- [ ] Mettre la fenêtre en pause 5 secondes ne provoque pas de rattrapage brutal.
- [ ] Les FPS sont affichés et cohérents avec le VSync.
- [ ] Sur Mac Retina, la taille en pixels est bien différente de la taille en points et est correctement gérée.

---

## 5. Périphérique GPU et premier écran

Cette partie n'apparaît pas telle quelle dans la roadmap, mais elle est indispensable avant les shaders : sans périphérique GPU fonctionnel, rien ne s'affiche.

### But

Créer le périphérique SDL_GPU, l'associer à la fenêtre et **effacer l'écran avec une couleur** chaque frame.

### Vocabulaire SDL_GPU

| Élément | Rôle |
|---|---|
| Device (`SDL_GPUDevice`) | Représente le GPU et le backend choisi (Metal, Vulkan, D3D12) |
| Swapchain | Les images présentées à la fenêtre |
| Command buffer | Liste de commandes qu'on enregistre puis qu'on soumet au GPU |
| Render pass | Une séquence de dessin vers une ou plusieurs cibles |
| Copy pass | Une séquence de copies (envoi de données CPU vers GPU) |
| Pipeline graphique | L'état complet de dessin : shaders, format des sommets, blending, etc. |
| Transfer buffer | Zone mémoire intermédiaire pour envoyer des données au GPU |

### Structure d'une frame

```
1. Acquérir un command buffer
2. Acquérir la texture du swapchain
3. Commencer un render pass (avec la couleur d'effacement)
4. (dessiner)
5. Terminer le render pass
6. Soumettre le command buffer
```

### Tâches

- [x] Créer le périphérique en déclarant les formats de shaders acceptés (SPIR-V, DXIL, MSL).
- [x] Activer le **mode debug** du GPU en configuration de développement.
- [x] Associer la fenêtre au périphérique (`SDL_ClaimWindowForGPUDevice`).
- [x] Afficher dans les logs le **backend choisi** (Metal, Vulkan, D3D12) et le nom du GPU.
- [x] Écrire la boucle d'une frame : command buffer, swapchain, render pass avec effacement, soumission.
- [x] Gérer le cas où la texture du swapchain n'est pas disponible (fenêtre minimisée).
- [x] Encapsuler dans une classe `Renderer` qui possède le device.

### Questions à se poser

- **Quels formats de shaders déclarer ?** SPIR-V (Vulkan), DXIL (D3D12), MSL (Metal). Sur Windows, veux-tu compiler pour D3D12 seul ou aussi Vulkan ? Sur Mac, ce sera Metal. Chaque format supplémentaire alourdit la chaîne de shaders (partie 6). *(Recommandation : Windows en D3D12, Mac en Metal.)*
- **Faut-il forcer un backend ?** Laisser SDL choisir, ou permettre de forcer via une variable ou une option pour comparer ? Utile pour repérer un bug spécifique à un backend.
- **Que faire si le GPU n'est pas compatible ?** Message d'erreur clair ? Repli ?
- **Comment gérer le VSync et le mode de présentation ?** SDL_GPU permet de choisir entre VSync, immédiat, mailbox selon la compatibilité (`SDL_SetGPUSwapchainParameters`). Lesquels exposer ?
- **Quel format de couleur pour le swapchain ?** SDR standard ou espace linéaire/sRGB ? Lié à la question de l'espace colorimétrique (voir partie 7).
- **Combien de frames en vol (frames in flight) ?** SDL_GPU gère une partie de cela, mais le nombre influe sur la latence et la synchronisation des ressources dynamiques.
- **Comment envelopper les ressources GPU ?** Des classes RAII (constructeur crée, destructeur libère) ou des identifiants gérés par le moteur ? RAII est plus simple ; attention aux copies et aux durées de vie (une ressource ne doit pas être libérée pendant qu'un command buffer la référence encore).
- **Quel niveau d'abstraction viser ?** Une mince couche autour de SDL_GPU (peu de code, très lié à SDL) ou une couche de rendu indépendante de l'API ? *(Recommandation : mince couche au départ ; on abstrait quand le besoin apparaît.)*

### Pièges connus

- Oublier de libérer les ressources GPU avant de détruire le device (avertissements en mode debug).
- Ne pas gérer une fenêtre minimisée : l'acquisition du swapchain peut retourner « aucune texture », et il faut simplement passer la frame.
- Soumettre un command buffer sans passe de rendu valide.
- Sur Windows, D3D12 peut demander des couches de débogage installées pour le mode debug *(à vérifier)*.

### Implémentation réalisée

Code dans `src/moteur/renderer.cpp` et `include/moteur/renderer.hpp` (voir la [documentation des fichiers](FICHIERS_DU_PROJET.md)). Testé sur Windows uniquement.

**Structure**
- `Renderer` possède le `SDL_GPUDevice`, l'associe à la fenêtre et pilote une frame en trois temps : `begin_frame()`, dessin, `end_frame()`.
- `begin_frame()` acquiert un command buffer, attend et acquiert l'image du swapchain (`SDL_WaitAndAcquireGPUSwapchainTexture`, qui bloque jusqu'au VSync), puis ouvre un render pass qui efface l'écran. Il renvoie `false` si rien ne peut être dessiné.
- `Application` crée le `Renderer` après la fenêtre et le libère avant elle. `Game::render(Renderer&, alpha)` est appelé à l'intérieur du render pass déjà ouvert.
- Le bac à sable fait pulser la couleur d'arrière-plan, ce qui prouve visuellement que la fenêtre est bien redessinée.

**Décisions prises**
- **Un backend par OS** : Direct3D 12 sur Windows (shaders DXIL), Metal sur macOS (MSL et metallib). Ajouter `SDL_GPU_SHADERFORMAT_SPIRV` permettrait aussi Vulkan, mais imposerait un format de shader de plus dans la chaîne de la partie 6.
- **VSync par défaut**, avec option `vsync = false` : le renderer choisit alors le mode `immediate` ou `mailbox` s'il est supporté, sinon il reste en VSync.
- **Mode debug du GPU** activé quand `NDEBUG` n'est pas défini (build Debug), désactivé en Release.
- **Fenêtre minimisée** : `begin_frame()` renvoie `false` et le command buffer est soumis (jamais annulé, la doc SDL l'interdit après l'acquisition du swapchain). La boucle attend 10 ms pour ne pas tourner à vide.
- **Forcer un backend** : SDL le permet nativement avec la variable d'environnement `SDL_GPU_DRIVER` (hint `SDL_HINT_GPU_DRIVER`), sans code de notre côté.
- Couche mince autour de SDL_GPU : pas d'abstraction indépendante de l'API pour l'instant.

**Mesure sur Windows**
- Logs : `backend=direct3d12, device=NVIDIA GeForce RTX 4070 Ti SUPER, present=vsync, debug=on`.
- 498 frames en 3,017 s, soit environ 165 FPS. Cela correspond au VSync sur un écran à environ 165 Hz (à confirmer avec ton écran). Les 181 ticks de logique restent inchangés.
- Lecture du pixel central de la fenêtre à trois instants : RGB(47,68,51), puis (61,30,74), puis (25,38,102). La couleur pulse donc bien.
- Fenêtre minimisée : le processus reste vivant, consomme environ 0,1 s de CPU par seconde, et se ferme avec le code 0 après restauration.
- Compilation sans aucun avertissement.

### Validation

- [ ] La fenêtre se remplit d'une couleur unie via SDL_GPU sur Windows (D3D12) et Mac (Metal). *Windows vérifié, Mac à faire.*
- [x] Le backend et le GPU sont affichés dans les logs.
- [ ] Redimensionner et minimiser la fenêtre ne plante pas. *Minimisation vérifiée sur Windows, redimensionnement pas encore testé.*
- [ ] Aucun avertissement de la couche de validation en mode debug à la fermeture. *Aucun message vu sur Windows ; à confirmer que la couche de débogage D3D12 est bien installée.*

---

## 6. Chaîne de compilation des shaders

### But

Écrire un shader **une seule fois** et obtenir automatiquement la version adaptée à chaque backend (SPIR-V, DXIL, MSL) lors du build.

### Contexte

SDL_GPU n'a pas de langage de shader unique : chaque backend attend son propre format. Le projet officiel **SDL_shadercross** permet d'écrire en **HLSL** (ou d'utiliser du SPIR-V) et de le convertir en SPIR-V, DXIL et MSL.

### Tâches

- [ ] Obtenir et compiler **SDL_shadercross** (ligne de commande `shadercross`) sur Windows et Mac. *Windows fait via vcpkg. Sur Mac, ce n'est pas possible par vcpkg : MSL pré-généré à la place (voir plus bas).*
- [x] Écrire un vertex shader et un fragment shader HLSL minimaux (triangle en couleur unie).
- [x] Ajouter une étape CMake qui compile les shaders à chaque build (commande personnalisée avec dépendances).
- [x] Générer les sorties dans un dossier connu à côté de l'exécutable.
- [x] Écrire le code de chargement qui choisit le bon fichier selon le backend actif.
- [x] Afficher un triangle coloré à l'écran.

### Questions à se poser

- **Quel langage de shader écrire ?** HLSL est le choix naturel avec shadercross. GLSL est possible via SPIR-V, mais la chaîne est moins directe. Le choix engage tout le reste du moteur.
- **Où obtenir shadercross ?** Compilation depuis les sources (dépôt `libsdl-org/SDL_shadercross`), binaire prébuilt, ou port vcpkg *(à vérifier)* ? Sur Windows, la conversion vers DXIL demande le compilateur DirectX (DXC). Sur Mac, la génération de MSL est faite mais la compilation Metal finale peut demander les outils Xcode. *(à vérifier)*
- **Compile-t-on au build ou au démarrage ?** Au build : démarrage rapide, mais il faut la chaîne d'outils sur chaque machine. Au démarrage : plus flexible (rechargement à chaud), mais plus lent et il faut embarquer le compilateur. *(Recommandation : au build pour ce jalon ; le rechargement à chaud est une amélioration ultérieure.)*
- **Fichiers séparés ou intégrés à l'exécutable ?** Charger depuis un dossier `shaders/` est simple ; intégrer les octets dans le binaire évite les problèmes de chemins mais complique le build.
- **Comment CMake sait-il quand recompiler ?** Il faut déclarer les fichiers source comme dépendances de la commande. Sans cela, modifier un shader ne le recompile pas.
- **Quelles conventions de liaison des ressources ?** SDL_GPU impose une organisation précise des textures, échantillonneurs, tampons de stockage et uniformes selon l'étage (vertex / fragment) : c'est un point d'erreur classique. Lire la documentation de `SDL_CreateGPUShader` *(à vérifier)* et écrire la règle dans un fichier de conventions.
- **Comment nommer et organiser les shaders ?** Un fichier par étage (`sprite.vert.hlsl`, `sprite.frag.hlsl`) ? Un dossier par fonctionnalité ?
- **Comment gérer les erreurs de compilation de shader ?** Le build doit échouer avec un message lisible. Que faire à l'exécution si un shader est absent ?
- **Comment fournir des données au shader ?** Uniformes poussés (`SDL_PushGPUVertexUniformData`), tampons de sommets, tampons de stockage : chacun a ses limites de taille. Lequel utiliser pour la matrice de projection ?

### Pièges connus

- Les conventions de liaison (sets, registres, espaces) diffèrent d'un étage à l'autre : un shader qui compile mais n'affiche rien est souvent un problème de liaison.
- Les chemins avec espaces dans les commandes CMake personnalisées (voir partie 3).
- Sur Mac, une sémantique HLSL non supportée ou une fonctionnalité de shader que Metal n'accepte pas se voit tard, pas à la compilation HLSL.
- Oublier de recompiler les shaders après un changement d'OS ou de machine : garder un dossier de sortie par configuration.
- Les erreurs ne s'affichent pas toujours : activer le mode debug du GPU et lire les logs.

### Implémentation réalisée

Code dans `cmake/Shaders.cmake`, `shaders/`, `renderer.cpp` (`load_shader`) et le bac à sable (voir la [documentation des fichiers](FICHIERS_DU_PROJET.md)). Testé sur Windows uniquement.

**La chaîne**

```
shaders/triangle.vert.hlsl ─┐
shaders/triangle.frag.hlsl ─┤   Windows : shadercross (au build)
                            ├──────────────────────────────────►  apps/bac_a_sable/shaders/*.dxil
                            │
                            │   Windows : cible manuelle export_msl_shaders
                            └──────────────────────────────────►  shaders/generated/msl/*.msl  (versionné)
                                                                     │
                                            macOS : copie au build   ▼
                                                              apps/bac_a_sable/shaders/*.msl
```

Au démarrage, `Renderer::load_shader("triangle.vert", ...)` demande à SDL_GPU les formats supportés par le backend actif, puis charge le fichier `shaders/<nom>.<ext>` correspondant (`dxil`, `msl` ou `spv`) avec le bon point d'entrée.

**Décisions prises**
- **HLSL** comme langage unique, converti par **SDL_shadercross**.
- **Compilation au build**, pas au démarrage : démarrage rapide, et une erreur de shader casse le build au lieu de casser le jeu.
- **shadercross via vcpkg** : le port `sdl3-shadercross` fournit l'outil, avec DXC et SPIRV-Cross. La dépendance est déclarée avec `"platform": "windows"` dans `vcpkg.json`.
- **Sur macOS, pas de vcpkg pour shadercross** : le port dépend de `directx-dxc`, qui n'est supporté que sur Windows et Linux x64. Solution retenue : les fichiers MSL sont générés sur Windows (cible `export_msl_shaders`), versionnés dans `shaders/generated/msl/`, et simplement copiés sur Mac.
- **Point d'entrée** : `main` en DXIL et SPIR-V, mais **`main0` en MSL**. SPIRV-Cross renomme la fonction car `main` est réservé en Metal. `load_shader` gère cette différence.
- **Un dossier d'outils privé** (`build/shader_tools/`) : au configure, CMake y copie `shadercross.exe`, `dxcompiler.dll` et `dxil.dll`, pour que l'outil trouve ses bibliothèques.
- **Triangle sans vertex buffer** : le vertex shader lit positions et couleurs dans des tableaux constants d'après `SV_VertexID`. Cela valide les shaders sans dépendre d'un chargement de données.
- **Aucune ressource liée** (ni texture ni uniforme) pour ce premier shader : les conventions de liaison de SDL_GPU seront à valider avec le sprite (partie 7).

**Mesures sur Windows**
- Le shader DXIL généré est accepté par Direct3D 12 : le pipeline se crée sans erreur, la signature du DXIL est donc valide.
- Pixels lus dans la fenêtre : sommet bas-gauche RGB(242,3,10) (rouge), bas-droite RGB(3,242,10) (vert), haut RGB(7,7,240) (bleu), centre RGB(85,85,85) (mélange à parts égales). Le fond pulsant reste visible autour. L'axe Y est donc bien orienté vers le haut dans l'espace de projection.
- Sans modification, `ninja` ne recompile rien. Après modification d'un `.hlsl`, seul ce shader est recompilé.
- Une erreur volontaire (`colour` au lieu de `color`) fait échouer le build avec la ligne fautive et la suggestion du compilateur. Le nom de fichier affiché est `hlsl.hlsl`, pas le vrai nom : à garder en tête pour retrouver le shader concerné.
- Le fichier MSL généré contient bien `vertex main0_out main0(...)` et `fragment main0_out main0(...)`.

**Ce qui n'est pas vérifié**
- **Tout le côté Mac** : la compilation du MSL par Metal, le nom `main0`, la copie des fichiers au build.
- Le risque principal est que les shaders soient modifiés sur le Mac sans régénérer le MSL : la cible `export_msl_shaders` n'existe que sous Windows.

### Validation

- [x] Modifier un fichier `.hlsl` puis relancer le build recompile bien le shader.
- [x] Les trois formats (SPIR-V, DXIL, MSL) sont produits, ou au moins ceux des backends visés. *DXIL au build sur Windows, MSL exporté ; le SPIR-V n'est pas produit puisque Vulkan n'est pas ciblé.*
- [ ] Un triangle en couleur unie s'affiche sur Windows et sur Mac avec **les mêmes fichiers source**. *Windows vérifié (triangle en dégradé rouge, vert, bleu), Mac à faire.*
- [x] Une erreur volontaire dans un shader fait échouer le build avec un message compréhensible.

---

## 7. Sprite texturé

### But

Afficher une image (PNG) sur un quad positionné en coordonnées de pixels, avec transparence.

### Étapes

**7a. Chargement de l'image (CPU)**
- Décoder un PNG en mémoire (pixels RGBA 8 bits).

**7b. Ressources GPU**
- Créer une texture GPU, un échantillonneur, un tampon de sommets (et éventuellement d'indices).
- Envoyer les pixels vers la texture via un transfer buffer et un copy pass.

**7c. Pipeline et dessin**
- Créer le pipeline graphique (format des sommets, shaders, blending).
- Lier la texture et l'échantillonneur, pousser la matrice de projection, dessiner.

### Tâches

- [x] Ajouter un asset de test (petit PNG avec transparence).
- [x] Charger l'image depuis le disque, avec un chemin qui fonctionne sur les deux OS. *Chemin relatif à l'exécutable ; Mac non testé.*
- [x] Créer la texture GPU et y envoyer les pixels (transfer buffer, copy pass).
- [x] Créer un échantillonneur adapté (filtrage, adressage).
- [x] Définir le format des sommets (position, coordonnées de texture, éventuellement couleur).
- [x] Créer les sommets et indices d'un quad.
- [x] Écrire le shader : projection de la position, échantillonnage de la texture.
- [x] Créer le pipeline avec le **blending alpha**.
- [x] Écrire une matrice de projection orthographique en pixels.
- [x] Dessiner le sprite, et vérifier qu'il bouge (par exemple animé par la logique à pas fixe).

### Questions à se poser

**Chargement d'image**
- **`stb_image` ou SDL_image ?** `stb_image` : un seul fichier d'en-tête, sans dépendance, PNG/JPEG et plus. SDL_image : plus de formats et l'intégration SDL, mais une dépendance de plus. Le choix est peu engageant mais autant le faire proprement.
- **Comment résoudre le chemin des assets ?** Relatif au dossier de travail (fragile) ou relatif à l'exécutable (`SDL_GetBasePath`) ? Sur Mac, dans un bundle `.app`, les ressources sont dans un dossier précis : un chemin qui marche en développement peut casser une fois empaqueté.
- **Comment CMake copie-t-il les assets à côté de l'exécutable ?** À chaque build, ou via un lien symbolique en développement ?
- **Que faire si l'image est introuvable ?** Crash, texture par défaut (damier magenta), message d'erreur ?

**Format et couleurs**
- **Quel format de texture ?** RGBA 8 bits est la valeur sûre. Que fait-on du sRGB ?
- **Espace colorimétrique : linéaire ou sRGB ?** Sujet subtil : si les textures sont en sRGB et le mélange se fait en espace linéaire, les couleurs et la transparence changent d'apparence. À décider avant d'ajouter éclairage et particules, car changer ensuite est pénible.
- **Alpha pré-multiplié ou non ?** Le pré-multiplié donne de meilleurs résultats pour le blending et le filtrage, mais impose de convertir les images ou d'adapter le shader. Détermine la formule de blending du pipeline.
- **Mipmaps ?** Inutiles pour du pixel art à échelle fixe, utiles si on redimensionne beaucoup. Pas nécessaires pour ce jalon.

**Style visuel (engage le reste du moteur)**
- **Pixel art ou art haute résolution ?** Détermine le filtrage : `NEAREST` (bords nets, pixel art) ou `LINEAR` (lissé). Détermine aussi la résolution interne et la mise à l'échelle.
- **Résolution de référence ?** Rendu à une résolution fixe puis mise à l'échelle, ou rendu natif à la résolution de la fenêtre ?
- **Sprites pré-rendus ou 3D vers 2D ?** Le style « Diablo 2 » (sprites pré-rendus en 8 directions) a des conséquences sur la taille des atlas et sur le volume d'assets.

**Géométrie et coordonnées**
- **Quelle unité de monde ?** Pixels, ou unités de monde converties en pixels ? Avec un rendu isométrique à venir, cette décision se prend tôt.
- **Origine des coordonnées ?** Coin haut-gauche (comme les écrans) ou centre / bas-gauche ? Vérifier la convention de SDL_GPU entre backends, qui harmonise mais doit être testée sur les deux OS. *(à vérifier)*
- **Quad avec indices ou 6 sommets ?** Pour un sprite unique, peu d'importance. Mais le sprite batching (jalon 2) utilisera un grand tampon de sommets dynamique : autant réfléchir à la structure dès maintenant.
- **Quel format pour les sommets ?** Position 2D ou 3D ? Une profondeur (Z) sera utile pour le tri isométrique.
- **Comment fournir la matrice de projection ?** Uniforme poussé par frame (simple). Le point à trancher est de savoir qui la calcule (une future classe caméra ?).
- **Comment gérer le redimensionnement ?** La projection dépend de la taille de la fenêtre : la recalculer à chaque changement.

**Transfert de données**
- **Un transfer buffer par envoi ou un tampon réutilisé ?** Pour un sprite, on peut en créer un et le libérer. Pour un moteur, on aura un système de chargement : à garder en tête.
- **Quand faire les copies ?** Dans un copy pass séparé avant le render pass. Une copie ne peut pas se faire pendant un render pass.
- **Que faire des ressources temporaires ?** Libérer le transfer buffer après soumission (pas avant que le GPU ait fini).

### Pièges connus

- Transparence noire au lieu de transparente : blending non activé, ou alpha pré-multiplié mal géré.
- Sprite invisible : matrice de projection incorrecte, ordre des sommets (culling des faces arrière), mauvaise liaison de la texture.
- Sprite retourné verticalement : origine des coordonnées de texture différente selon l'API. Vérifier sur les deux OS.
- Image floue en pixel art : mauvais filtrage (`LINEAR` au lieu de `NEAREST`) ou coordonnées de texture non alignées sur les pixels.
- Bord des sprites qui « saigne » (couleur d'un voisin dans un atlas) : sujet du jalon 2, à garder en tête.
- Libérer un transfer buffer trop tôt.
- Différence de rendu entre Windows et Mac due à l'espace colorimétrique du swapchain.

### Implémentation réalisée

Code dans `image.cpp`, `paths.cpp`, `renderer.cpp` (création de ressources), `shaders/sprite.*.hlsl`, `assets/sprite.png` et le bac à sable (voir la [documentation des fichiers](FICHIERS_DU_PROJET.md)). Testé sur Windows uniquement. Le triangle de la partie 6 est remplacé par le sprite.

**Ce qui est affiché** : une image de 32×32 pixels (un disque à quatre quadrants colorés, contour noir, halo semi-transparent, fond transparent) agrandie ×8, qui se déplace en oscillant autour du centre de la fenêtre.

**Décisions prises**
- **Chargement d'image : stb_image** (paquet vcpkg `stb`), limité au PNG. Une seule dépendance sans bibliothèque à lier. La lecture du fichier passe par `SDL_LoadFile` pour gérer les chemins UTF-8 sous Windows.
- **Chemins des assets : relatifs à l'exécutable** (`SDL_GetBasePath`), jamais au dossier de travail. Le dossier `assets/` est copié à côté de l'exécutable à **chaque build**.
- **Image manquante : exception** avec le chemin complet dans le message. Une texture de remplacement (damier magenta) pourra venir plus tard.
- **Mathématiques : GLM** (paquet vcpkg `glm`), avec `orthoRH_ZO` (profondeur de 0 à 1, comme Direct3D et Metal).
- **Unité du monde : le pixel physique**, origine en haut à gauche, axe Y vers le bas. Sur un écran Retina, un sprite de 256 pixels paraîtra donc deux fois plus petit qu'en points. Ce sera à revoir avec la caméra du jalon 2.
- **Projection recalculée à chaque frame** d'après la taille de l'image du swapchain : le redimensionnement de la fenêtre ne déforme rien.
- **Filtrage `NEAREST`** : bords nets, adapté au pixel art. Coordonnées hors de [0, 1] ramenées au bord.
- **Pas de mipmaps** (un seul niveau).
- **Format de texture : RGBA8 en UNORM, sans conversion sRGB**, et swapchain SDR standard. Les couleurs de l'image sont donc écrites telles quelles à l'écran, et le mélange se fait dans l'espace de l'image. Passer à un mélange en espace linéaire demandera de changer ensemble le format de texture et celui du swapchain.
- **Alpha non pré-multiplié** (« straight ») : `SRC_ALPHA` et `ONE_MINUS_SRC_ALPHA` pour la couleur, `ONE` et `ONE_MINUS_SRC_ALPHA` pour l'alpha. Le pré-multiplié améliorera le filtrage avec les atlas (jalon 2).
- **Quad indexé** : 4 sommets et 6 indices de 16 bits, plutôt que 6 sommets. C'est la structure qui servira au batching.
- **Position des sommets en pixels autour de l'origine du sprite**, plus une matrice `mvp` (projection × translation) envoyée en **uniforme** à chaque frame. La translation est calculée côté CPU.
- **Envoi des données en attendant le GPU** (`SDL_WaitForGPUIdle`) : acceptable au chargement, à ne pas utiliser pendant une frame. Un système de chargement asynchrone viendra si besoin.
- **Conventions de liaison de SDL_GPU** vérifiées dans la documentation de l'API, puis appliquées aux shaders : uniformes vertex en `space1`, texture et sampler du fragment en `space2` (tableau dans la [documentation des fichiers](FICHIERS_DU_PROJET.md), section sur `sprite.vert.hlsl`).

**Mesures sur Windows** (captures de la fenêtre, pixels lus à l'écran)

| Élément | Attendu | Mesuré |
|---|---|---|
| Quadrant haut-gauche | (220, 50, 47) rouge | (220, 50, 47) |
| Quadrant haut-droite | (60, 180, 75) vert | (60, 180, 75) |
| Quadrant bas-gauche | (50, 90, 220) bleu | (50, 90, 220) |
| Quadrant bas-droite | (240, 200, 40) jaune | (240, 200, 40) |
| Coin transparent | fond (51, 64, 89) | (51, 64, 89) |
| Contour | noir | (0, 0, 0) |
| Halo (alpha 50 %) | mélange ≈ (153, 160, 172) | (153, 160, 172) |

- **Orientation** : les quatre quadrants sont aux bons endroits, l'image n'est donc ni retournée ni inversée.
- **Netteté** : deux pixels d'un même texel sont identiques, et la frontière rouge/vert est franche entre deux pixels voisins (aucun dégradé intermédiaire).
- **Redimensionnement** : la fenêtre passe de 1280×720 à 984×661, le sprite garde exactement la même largeur (240 pixels visibles) et reste centré.
- **Mouvement** : le sprite garde une taille constante de 240×240 pixels visibles, avec un décalage horizontal entre −279 et +299 (amplitude ±300) et vertical entre −33 et +41 (amplitude ±60).
- Compilation sans avertissement, aucun message de validation du GPU, sortie avec le code 0.

**Ce qui n'est pas vérifié**
- **Tout le côté Mac** : le MSL généré (attributs, uniforme en `buffer(0)`, texture et sampler en indice 0) paraît cohérent, mais ni Metal ni le nom `main0` ni la liaison des buffers n'ont été testés.
- **La fluidité du mouvement** : je n'ai pas de moyen de la mesurer, seulement de vérifier les positions. À juger à l'œil, en regardant si le sprite glisse ou saccade.
- **Mesure de la capture** : ma première analyse du mouvement était faussée par des pixels parasites au bord de la capture d'écran, pas par le moteur ; elle a été refaite en ignorant une marge de 12 pixels.

### Validation

- [ ] Un sprite avec transparence s'affiche à la bonne position et à la bonne taille sur Windows et Mac. *Windows vérifié, Mac à faire.*
- [ ] Le rendu est **identique** (au pixel près pour du pixel art) entre les deux OS. *À comparer avec le Mac.*
- [x] Redimensionner la fenêtre ne déforme pas le sprite.
- [ ] Le sprite peut être déplacé par la logique à pas fixe sans saccade visible. *Positions vérifiées ; la fluidité reste à juger à l'œil.*
- [ ] Aucune fuite ni avertissement de la couche de validation à la fermeture. *Aucun message vu sur Windows ; à confirmer que la couche de débogage D3D12 est bien installée.*

---

## 8. Débogage et outils

### Outils

| Outil | OS | Usage |
|---|---|---|
| Mode debug SDL_GPU | Les deux | Couches de validation, messages d'erreur du backend |
| RenderDoc | Windows | Capturer une frame, inspecter textures, tampons, états du pipeline |
| Debugger GPU Metal (Xcode) | Mac | Capturer une frame Metal, inspecter les ressources et shaders |
| Débogueur CLion | Les deux | Points d'arrêt, inspection du code CPU |
| Logs SDL (`SDL_Log`) | Les deux | Traces générales |

### Questions à se poser

- **Comment lancer une capture de frame ?** RenderDoc fonctionne bien avec D3D12 et Vulkan. Pour capturer sur Mac, il faut lancer via Xcode. Prévoir 30 minutes de mise en place au premier essai.
- **Comment nommer les ressources GPU** pour les retrouver dans les captures ? SDL_GPU permet de nommer des objets et des groupes de commandes *(à vérifier)* : une bonne habitude dès le départ.
- **Que faire en cas d'écran noir ?** Se donner une liste de contrôle : (1) le device est-il créé ? (2) le command buffer est-il soumis ? (3) le pipeline est-il valide ? (4) les liaisons sont-elles correctes ? (5) que dit la couche de validation ?
- **Faut-il des assertions maison ?** Une macro `ASSERT` qui s'arrête avec un message aide énormément à repérer les erreurs de logique tôt.

### Validation

- [ ] Une frame est capturée dans RenderDoc sur Windows et le quad est visible dans l'inspecteur.
- [ ] Une frame est capturée avec le débogueur Metal sur Mac.

---

## 9. Critères de fin de jalon

Le jalon est terminé quand **tout** ce qui suit est vrai :

- [ ] Depuis un clone propre du dépôt, la commande de configuration puis de build fonctionne sur **Windows et sur Mac** sans étape manuelle non documentée.
- [ ] Une fenêtre s'ouvre, se redimensionne et se ferme proprement.
- [ ] La logique tourne à pas fixe, avec plafond de rattrapage.
- [ ] Les shaders sont compilés automatiquement lors du build.
- [ ] Un sprite texturé avec transparence est affiché correctement sur les deux OS.
- [ ] Aucun avertissement de la couche de validation GPU en mode debug.
- [ ] Le processus de build est **documenté** (un fichier `README` dans `moteur/` décrit les étapes pour chaque OS).
- [ ] Les décisions de la section [Décisions à consigner](#décisions-à-consigner) sont remplies.

---

## 10. Risques principaux

| Risque | Impact | Parade |
|---|---|---|
| Chaîne de shaders difficile à mettre en place sur un des OS | Bloque tout le rendu | La mettre en place tôt (triangle avant sprite) et tester sur les deux OS immédiatement |
| Conventions de liaison des ressources mal comprises | Écran noir difficile à diagnostiquer | Relire la doc de `SDL_CreateGPUShader`, s'aider d'un exemple officiel, capturer avec RenderDoc |
| SDL_GPU étant récent, peu de tutoriels | Temps perdu | S'appuyer sur les exemples et la documentation officiels de SDL |
| SDL_shadercross indisponible en paquet vcpkg | Étape de build manuelle | Décider tôt : dépôt cloné en `third_party/`, ou binaire versionné |
| Divergence de comportement entre D3D12 et Metal | Bugs visibles sur un seul OS | Tester sur les deux à chaque étape, garder le rendu simple |
| Dérive du périmètre (batching, caméra, atlas...) | Jalon sans fin | Ces sujets appartiennent au jalon 2 : les noter, ne pas les faire |
| Chemins avec espaces ou accents | Scripts de build cassés | Tout guillemeter, et envisager un chemin de dépôt sans espace pour le code |

---

## Décisions à consigner

À remplir au fur et à mesure du jalon. Chaque ligne correspond à une question posée plus haut.

| Sujet | Décision | Raison |
|---|---|---|
| Compilateur Windows | MSVC (Visual Studio 2022) | Meilleur support de vcpkg, du débogage et de DirectX |
| Version minimale de CMake | 3.25 (CLion fournit la 4.3.1) | Requis par les presets version 6 |
| Emplacement de vcpkg (global ou sous-module) | Vcpkg intégré à CLion, `VCPKG_ROOT` en repli | Aucune installation manuelle |
| Version minimale de macOS et architecture ciblée | macOS 13.0, arm64 | Apple Silicon uniquement |
| Mode manifeste vcpkg et baseline | Manifeste, baseline `5f96cd1` | Mêmes versions sur les deux machines |
| Lien SDL3 : statique ou dynamique | Statique | Un seul exécutable, packaging plus simple |
| Triplets vcpkg | `x64-windows-static-md`, `arm64-osx` | Statique avec runtime C dynamique sur Windows |
| Structure CMake : bibliothèque + exécutable | Bibliothèque `moteur` + exécutable `bac_a_sable` | Réutilisation par l'ARPG |
| Niveau d'avertissements et politique « erreur » | `/W4` et `-Wall -Wextra -Wpedantic`, pas de « warnings as errors » | Strict sans bloquer le build |
| Langue du code et de la documentation | Code en anglais, documentation en français | Cohérence avec l'écosystème C++ |
| Dépôt Git | À créer plus tard, à la main | `.gitignore` et `.gitattributes` prêts |
| Fréquence de la logique (Hz) | 60 Hz | Valeur classique, suffisante pour un ARPG |
| Plafond de rattrapage par frame | 0,25 s | Évite la spirale de rattrapage après une pause |
| Interpolation du rendu : oui ou non | `alpha` fourni à `render()`, pas encore utilisé | Permet de l'ajouter sans changer l'interface |
| Boucle classique ou main callbacks SDL | Boucle `while` classique | Plus de contrôle, plus lisible pour apprendre |
| Gestion des erreurs (exceptions / codes / expected) | Exceptions pour l'initialisation | Simple, pas de `std::expected` en C++20 |
| Journalisation | `SDL_Log` pour l'instant | Suffisant au début, à revoir plus tard |
| Backends visés (Windows / Mac) | Direct3D 12 (Windows), Metal (macOS) | Un seul format de shader à produire par OS |
| Mode de VSync exposé | VSync par défaut, option `vsync = false` (immediate ou mailbox) | Simple, toujours supporté |
| Langage de shader | HLSL | Langage natif de shadercross, une seule source pour tous les backends |
| Origine de shadercross et intégration au build | Port vcpkg `sdl3-shadercross` (Windows) ; MSL pré-généré et versionné pour macOS | `directx-dxc` n'existe pas sur macOS |
| Compilation des shaders : au build ou au démarrage | Au build | Une erreur de shader casse le build, pas le jeu |
| Bibliothèque de chargement d'image | stb_image (PNG seulement) | Sans dépendance à lier ; lecture du fichier par SDL pour les chemins UTF-8 |
| Résolution des chemins d'assets | Relatifs à l'exécutable, copiés à chaque build | Indépendant du dossier de travail |
| Style visuel : filtrage et résolution de référence | Pixel art : filtrage `NEAREST`, rendu à la résolution native de la fenêtre | Bords nets. La résolution de référence reste à décider avec la caméra (jalon 2) |
| Espace colorimétrique (linéaire ou sRGB) | Textures RGBA8 UNORM, swapchain SDR standard, mélange dans l'espace de l'image | Simple ; à changer ensemble textures et swapchain si on passe au linéaire |
| Alpha pré-multiplié ou non | Non pré-multiplié | Plus simple pour l'instant ; à revoir avec les atlas |
| Unité et origine des coordonnées du monde | Pixel physique, origine en haut à gauche, Y vers le bas | Cohérent avec les coordonnées d'écran ; à revoir avec la caméra isométrique |
