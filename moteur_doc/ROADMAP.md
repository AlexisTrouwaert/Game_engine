# Roadmap - Moteur

Moteur de jeu 2D/isométrique maison, pensé pour supporter un ARPG. Cibles : Windows et macOS.

## Stack

- Langage : C++20
- Build : CMake + vcpkg
- Fenêtre / input / audio / GPU : SDL3 (SDL_GPU)
- Shaders : HLSL + SDL_shadercross (SPIR-V / DXIL / MSL)
- Maths : GLM
- ECS : EnTT
- UI de debug : Dear ImGui
- Données : JSON (nlohmann/json)
- IDE : CLion

## Principes

- Le moteur est indépendant du gameplay : il ne connaît rien de l'ARPG.
- Solo-joueur d'abord, pas de réseau.
- Chaque jalon doit tourner à l'identique sur Windows et macOS. Ce qui reste à valider sur Mac : voir [TEST_MAC.md](TEST_MAC.md).
- Un jalon est terminé quand il est démontrable, pas quand le code est écrit.

## Jalons

### 1. Fondations
Documentation détaillée : [Jalon 1 - Fondations](JALON_1_FONDATIONS.md)

- [ ] Squelette CMake + vcpkg, build sur Windows et macOS
- [ ] Fenêtre SDL3 et boucle de jeu (pas de temps fixe)
- [ ] Chaîne de compilation des shaders (SDL_shadercross)
- [ ] Affichage d'un sprite texturé avec SDL_GPU

### 2. Rendu 2D
Documentation détaillée : [Jalon 2 - Rendu 2D](JALON_2_RENDU_2D.md)

- [ ] Sprite batching
- [ ] Caméra (isométrique / 2D)
- [ ] Tilemaps
- [ ] Atlas de textures et animations de sprites
- [ ] Texte et polices

### 3. Systèmes de base
- [ ] Intégration ECS (EnTT)
- [ ] Gestion des inputs (clavier, souris, manette)
- [ ] Gestionnaire d'assets (chargement, cache)
- [ ] Gestion de scènes / états de jeu
- [ ] Audio (effets, musique)
- [ ] Intégration Dear ImGui (debug)

### 4. Monde et déplacement
- [ ] Collisions 2D
- [ ] Pathfinding (A*, grille / navmesh)
- [ ] Éclairage 2D et effets de particules
- [ ] Brouillard de guerre / visibilité

### 5. Outils et données
- [ ] Chargement de données JSON (hot reload)
- [ ] Sauvegarde / chargement de l'état
- [ ] Outils de debug (console, profiler, overlays)
- [ ] Éditeur de cartes minimal

### 6. Consolidation
- [ ] Profilage et optimisation
- [ ] Packaging Windows et macOS (signature / notarisation)
- [ ] Documentation de l'API du moteur

## Plus tard (hors périmètre initial)

- Réseau / multijoueur
- **Rendu 3D** : décidé, le jeu sera en 3D (modèles 3D, caméra isométrique fixe). Jalon dédié à définir après le rendu 2D, qui servira surtout à l'interface, aux icônes et aux effets
- Éditeur visuel complet
- Support d'autres plateformes
