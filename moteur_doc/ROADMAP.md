# Roadmap - Moteur

Moteur de jeu maison, pensé pour supporter un ARPG : monde en **3D** vu par une caméra isométrique fixe, interface et effets en 2D. Cibles : Windows et macOS.

## Stack

- Langage : C++20
- Build : CMake + vcpkg
- Fenêtre / input / audio / GPU : SDL3 (SDL_GPU)
- Shaders : HLSL + SDL_shadercross (SPIR-V / DXIL / MSL)
- Maths : GLM
- ECS : EnTT
- UI de debug : Dear ImGui (intégré)
- Modèles 3D : glTF 2.0 (cgltf)
- Animation squelettique : ozz-animation (port vcpkg du projet, dossier `ports/`)
- Données : JSON (nlohmann/json)
- Assets : cache et poignées d'EnTT (`entt::resource_cache`), surveillance des fichiers avec efsw, textures KTX2 avec libktx (KTX-Software)
- IDE : CLion

## Principes

- Le moteur est indépendant du gameplay : il ne connaît rien de l'ARPG.
- Solo-joueur d'abord, pas de réseau.
- Chaque jalon doit tourner à l'identique sur Windows et macOS. Ce qui reste à valider sur Mac : voir [TEST_MAC.md](TEST_MAC.md).
- Un jalon est terminé quand il est démontrable, pas quand le code est écrit.

**Numérotation** : le 2026-09-23, le rendu 3D est devenu le jalon 3. Les anciens jalons 3 à 6 sont devenus 4, 6, 7 et 8, et l'animation 3D a pris le numéro 5. Les documents des jalons 1 et 2 gardent l'ancienne numérotation dans leurs renvois (« les entrées au jalon 3 » désigne maintenant le jalon 4).

## Jalons

### 1. Fondations
Documentation détaillée : [Jalon 1 - Fondations](JALON_1_FONDATIONS.md)

- [x] Squelette CMake + vcpkg, build sur Windows et macOS
- [x] Fenêtre SDL3 et boucle de jeu (pas de temps fixe)
- [x] Chaîne de compilation des shaders (SDL_shadercross)
- [x] Affichage d'un sprite texturé avec SDL_GPU

### 2. Rendu 2D
Documentation détaillée : [Jalon 2 - Rendu 2D](JALON_2_RENDU_2D.md)

- [x] Sprite batching
- [x] Caméra (isométrique / 2D)
- [x] Tilemaps *(version réduite : structure de carte, sans blocs statiques ni format de fichier)*
- [x] Atlas de textures et animations de sprites *(animations en version réduite : sans 8 directions)*
- [x] Texte et polices

### 3. Rendu 3D
Documentation détaillée : [Jalon 3 - Rendu 3D](JALON_3_RENDU_3D.md)

- [x] Passes multiples, profondeur et conventions (repère, couleur)
- [x] Caméra 3D isométrique et picking du sol
- [x] Maillages et chargement de modèles glTF
- [x] Textures (mipmaps) et couleur linéaire
- [x] Matériaux, éclairage et ombres *(soleil et lumières ponctuelles)*
- [x] Instanciation et culling
- [x] Le 2D (interface, barres de vie, billboards) par-dessus la 3D
- [x] Scène de démonstration 3D
- [x] Anticrénelage configurable *(aucun, FXAA, MSAA 2×, MSAA 4×)*

### 4. Systèmes de base
Documentation détaillée : [Jalon 4 - Systèmes de base](JALON_4_SYSTEMES_DE_BASE.md)

- [x] Intégration ECS (EnTT) *(le monde de la démo 3D en entités ; un registre par scène)*
- [x] Gestion des inputs (clavier, souris, manette) *(actions et profils de touches ; essais à la manette à faire)*
- [x] Gestionnaire d'assets (chargement, cache, rechargement à chaud)
- [x] Compression des textures : KTX2 (UASTC transcodé, ou BC7 / BC5 prêts), décidée au jalon 3
- [x] Gestion de scènes / états de jeu *(pile d'états : titre, chargement, jeu, pause ; 100 cycles sans fuite)*
- [x] Audio (effets, musique) *(miniaudio : effets placés, musique en fondu, groupes, limite de voix, limiteur)*
- [x] Intégration Dear ImGui (debug) *(outils dans DEBUG : inspecteur d'entités, assets, entrées, audio, états ; fenêtres retenues dans `imgui.ini`)*

### 5. Animation 3D
Documentation détaillée : [Jalon 5 - Animation 3D](JALON_5_ANIMATION_3D.md)

- [x] Squelettes et animations glTF *(ozz-animation, construits au chargement depuis cgltf ; lecture au tick, dessin entre deux ticks)*
- [x] Skinning sur le GPU *(palettes en storage buffer, second tampon de sommets ; ombres comprises ; comparé à Blender)*
- [x] Mélange et transitions entre animations *(fondus en ticks, blend space 1D en phase, vitesse accordée au sol, couche du haut du corps masquée ; `AnimationSet` en JSON)*
- [x] Événements d'animation (réutilise l'`AnimationPlayer` du jalon 2 : ticks entiers, événements exactement une fois) *(`ClipClock` commun ; événements dans le fichier de description, règle du poids ≥ 0,5 pendant les mélanges)*
- [x] Attacher des objets à un os (arme dans la main) *(`BoneAttachment`, points d'attache nommés, au dessin sans l'échelle de l'os)*
- [x] Performances de l'animation *(`--skinned N` : 200 personnages pour environ 1 ms de CPU, plafond vers 1 600 à 60 images par seconde sous Windows ; pas de multithreading)*
- [x] Outils de debug de l'animation *(squelette et axes, pose de liaison, vue des poids, fenêtre DEBUG > Animation, scène « Animation », pose comparée à Blender)*

### 6. Monde et déplacement
Documentation détaillée : [Jalon 6 - Monde et déplacement](JALON_6_MONDE_ET_DEPLACEMENT.md)

- [x] Collisions sur le plan du sol (`TileMap`, formes simples) *(cercles contre la grille et entre eux, glissement, poids de poussée ; cartes en JSON dessinées en caractères)*
- [x] Pathfinding (A* sur la grille, ou navmesh : `recastnavigation` est dans vcpkg) *(A\* maison sur la grille, dégagement par taille, lissage, flow fields pour les foules ; 500 monstres pour 0,43 ms)*
- [x] Requêtes spatiales et lignes de vue (cercle, cône, rayon sur la grille) *(grille de hachage, résultats triés, lignes de vue symétriques)*
- [x] Effets de particules (billboards du jalon 3) *(CPU, en JSON, budget de 20 000 ; particules douces repoussées)*
- [x] Brouillard de guerre / visibilité (propriété `opaque` de la `TileMap`) *(shadowcasting symétrique, cases explorées, murs tramés devant le héros)*

### 7. Outils et données
- [ ] Chargement de données JSON (hot reload)
- [ ] Sauvegarde / chargement de l'état
- [ ] Outils de debug (console, profiler, overlays)
- [ ] Éditeur de cartes minimal

### 8. Consolidation
- [ ] Profilage et optimisation
- [ ] Packaging Windows et macOS (signature / notarisation)
- [ ] Documentation de l'API du moteur

## Plus tard (hors périmètre initial)

- Réseau / multijoueur
- Rendu avancé : post-traitement (bloom, occlusion ambiante), illumination globale, culling sur GPU
- Physique 3D (par exemple Jolt, disponible dans vcpkg), si le gameplay en a besoin
- Éditeur visuel complet
- Support d'autres plateformes
