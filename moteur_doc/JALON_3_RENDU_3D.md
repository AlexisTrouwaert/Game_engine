# Jalon 3 - Rendu 3D

Retour à la [roadmap générale](ROADMAP.md). Jalon précédent : [Jalon 2 - Rendu 2D](JALON_2_RENDU_2D.md). Description des fichiers existants : [Fichiers du projet](FICHIERS_DU_PROJET.md).

## Objectif

Passer d'un monde **en sprites** à un monde **en 3D** : des modèles chargés depuis des fichiers, une caméra isométrique fixe, un éclairage avec des ombres, des milliers d'objets dessinés efficacement. Le rendu 2D du jalon 2 reste **par-dessus**, pour l'interface, les icônes, les effets et le debug.

À la fin du jalon, une **scène de démonstration 3D** doit tourner à la cadence de l'écran sur Windows et macOS : le sol et les murs construits depuis la `TileMap` du jalon 2, du décor instancié, des créatures qui se déplacent, une lumière avec ses ombres, la case sous la souris mise en évidence, des barres de vie en 2D au-dessus des créatures, et les statistiques dans ImGui. Elle se lance depuis **DEBUG > Tests moteur**, comme les scènes 2D.

**Pourquoi maintenant, avant les systèmes de base ?** Le rendu 3D est le plus gros risque technique du projet : deux backends (Direct3D 12 et Metal), des shaders à produire pour les deux, des conventions de repère et de couleur qui, décidées tard, obligent à reprendre les assets. Les systèmes de base (ECS, entrées, audio) ne dépendent pas du rendu : ils peuvent attendre sans rien bloquer.

## Prérequis

- Le [jalon 2](JALON_2_RENDU_2D.md) est terminé.
- Des bases de mathématiques 3D : matrices 4×4, repères, produit scalaire et vectoriel, changement de repère. Le site *learnopengl.com* explique très bien les concepts (lumière, ombres, profondeur) ; le code est en OpenGL, mais les idées se transposent directement à SDL_GPU.
- Blender installé, au moins pour ouvrir, vérifier et exporter des modèles de test.

## Comment lire ce document

Même structure que les jalons précédents :

- **But** : ce qu'on cherche à obtenir.
- **Tâches** : ce qu'il faut faire, dans l'ordre.
- **Questions à se poser** : les décisions à trancher avant ou pendant le travail. À noter dans la section [Décisions à consigner](#décisions-à-consigner) une fois tranchées.
- **Pièges connus** : erreurs fréquentes.
- **Validation** : comment savoir que la partie est terminée.

Les points marqués *(à vérifier)* sont des informations dont je ne suis pas certain ou qui évoluent vite. Il faut les confirmer dans la documentation actuelle avant de s'appuyer dessus.

## Sommaire

1. [Vue d'ensemble et ordre de travail](#1-vue-densemble-et-ordre-de-travail)
2. [Préparation : passes, profondeur et conventions](#2-préparation--passes-profondeur-et-conventions)
3. [Caméra 3D isométrique](#3-caméra-3d-isométrique)
4. [Maillages et premier cube](#4-maillages-et-premier-cube)
5. [Chargement de modèles (glTF)](#5-chargement-de-modèles-gltf)
6. [Textures et couleur](#6-textures-et-couleur)
7. [Matériaux et éclairage](#7-matériaux-et-éclairage)
8. [Ombres](#8-ombres)
9. [Beaucoup d'objets : instanciation et culling](#9-beaucoup-dobjets--instanciation-et-culling)
10. [Le 2D par-dessus la 3D](#10-le-2d-par-dessus-la-3d)
11. [Scène de démonstration 3D](#11-scène-de-démonstration-3d)
12. [Débogage et performance](#12-débogage-et-performance)
13. [Critères de fin de jalon](#13-critères-de-fin-de-jalon)
14. [Risques principaux](#14-risques-principaux)
15. [Décisions à consigner](#décisions-à-consigner)

---

## 1. Vue d'ensemble et ordre de travail

### Où on part

Après le jalon 2, le moteur sait :

- piloter une frame SDL_GPU : copies, **une** passe de rendu (effacement, sprites, ImGui), soumission, capture PNG ;
- compiler des shaders HLSL en DXIL (Windows) et exporter le MSL pour le Mac ;
- charger des PNG en textures RGBA8 (alpha pré-multiplié, sans mipmaps, filtrage `NEAREST`) ;
- dessiner des dizaines de milliers de sprites triés et regroupés, avec une `Camera2D` et une projection isométrique 2D ;
- décrire une carte (`TileMap`, `Tileset` avec `walkable` et `opaque`) et jouer des animations déterministes (`AnimationPlayer`) ;
- afficher une interface de debug (ImGui) et lancer les scènes de test depuis un menu.

Il manque tout ce qui est propre à la 3D : un **tampon de profondeur**, plusieurs **passes** par frame, des **maillages**, une **caméra 3D**, un **éclairage**, des **mipmaps** et une chaîne de couleur **linéaire**.

### Dépendances entre les parties

```
2. Préparation (passes, profondeur, conventions)
      |
      +---------------------+
      |                     |
3. Caméra 3D          4. Maillages et premier cube
      |                     |
      |               +-----+-----+
      |               |           |
      |          5. glTF     6. Textures et couleur
      |               |           |
      +-------+-------+-----------+
              |
      7. Matériaux et éclairage
              |
      +-------+--------+
      |                |
  8. Ombres     9. Instanciation et culling
      |                |
      +-------+--------+
              |
      10. Le 2D par-dessus
              |
      11. Scène de démonstration
```

- La **préparation** vient d'abord : les conventions (repère, profondeur, couleur) coûtent cher à changer une fois des modèles chargés.
- La **caméra** et les **maillages** sont indépendants : un cube se teste avec une caméra provisoire, la caméra se teste avec des tests unitaires.
- Les **ombres** et l'**instanciation** ont besoin de l'éclairage, mais pas l'une de l'autre.

### Ce qui peut se faire en pause du moteur

Logique pure, testable en ligne de commande :

- **Mathématiques de la caméra** : matrices, rayon depuis un pixel, intersection avec le sol, monde vers écran.
- **Frustum culling** : plans de la caméra, test d'une boîte englobante.
- **Primitives** (cube, sphère, plan) et calcul des boîtes englobantes.
- **Lecture d'un glTF** vers une structure en mémoire, sans GPU.
- **Recherche des modèles de test** et vérification de leurs licences.

### Estimation indicative

Pour un dev solo à temps partiel. À prendre comme un ordre de grandeur, pas comme un engagement.

| Partie | Estimation |
|---|---|
| 2. Préparation | 1 à 2 semaines |
| 3. Caméra 3D | 1 à 2 semaines |
| 4. Maillages et premier cube | 1 à 2 semaines |
| 5. Chargement glTF | 2 à 3 semaines |
| 6. Textures et couleur | 1 à 2 semaines |
| 7. Matériaux et éclairage | 2 à 3 semaines |
| 8. Ombres | 2 à 3 semaines |
| 9. Instanciation et culling | 2 à 3 semaines |
| 10. Le 2D par-dessus | 1 à 2 semaines |
| 11. Scène de démonstration | 1 à 2 semaines |
| **Total** | **environ 4 à 6 mois** |

### Liens avec les autres jalons

- **L'animation squelettique** a son propre jalon (voir la [roadmap](ROADMAP.md)). Ici, les modèles sont statiques. Mais le format de sommets et le chargement glTF doivent **prévoir** les attributs de squelette (`JOINTS_0`, `WEIGHTS_0`) sans les utiliser, et l'`AnimationPlayer` du jalon 2 (ticks entiers, événements) servira à piloter les clips 3D.
- **L'ECS** (jalon 4) : le rendu 3D doit recevoir des listes d'objets (maillage, matériau, transformation), comme la file de sprites, plutôt que des objets qui « se dessinent eux-mêmes ». L'ECS s'y branchera sans rien réécrire.
- **Le gestionnaire d'assets** (jalon 4) : ici, un chargement direct suffit. Le cache et les durées de vie viendront après.
- **Les collisions et le pathfinding** restent sur le **plan du sol** (la `TileMap` et son `walkable`) : pas de physique 3D dans ce jalon.
- **Les particules et effets** appartiennent au jalon « Monde et déplacement ». La partie 10 prépare seulement les sprites dans le monde (*billboards*) qui les accueilleront.

### Questions générales

Tranchées le 2026-09-23 (voir aussi les [décisions](#décisions-à-consigner)) :

- **Machine minimale : Mac M3** (puce de base), le M4 étant préférable si le jeu devient exigeant. **60 FPS au minimum.** Le Mac de développement (M4 Pro) et la RTX 4070 Ti sont nettement plus puissants : **toujours garder de la marge** dans les mesures, et vérifier régulièrement sur un M3 si possible. L'équivalent Windows minimal reste à fixer.
- **Nombre d'entités : pas encore connu.** Objectif **provisoire**, inspiré d'un ARPG chargé à la Path of Exile : 300 personnages animés et 3 000 objets de décor visibles, environ 1 million de triangles, beaucoup de lumières d'effets. À réviser avec les mesures de la partie 9 et le gameplay.
- **Style : réaliste, PBR.** Conséquences : matériaux métal / rugosité du glTF, **cartes de normales**, **éclairage d'environnement** (IBL), rendu en **HDR avec tone mapping**, et beaucoup de textures (la compression GPU devient plus pressante). Ces sujets sont intégrés aux parties 6 et 7.
- **Échelle : 1 unité = 1 mètre, une case de la `TileMap` = 1 mètre** (recommandation, voir la partie 2).
- **Modèles : packs libres pour les tests**, import de modèles faits dans Blender plus tard. Pour du PBR réaliste, les packs *low poly* (Kenney, Quaternius) ne conviennent pas : préférer **Poly Haven** (modèles, textures et environnements HDR, en CC0) et les **modèles d'exemple de Khronos** (glTF Sample Assets, avec des rendus de référence pour valider le PBR, licences variables selon le modèle) *(licences à vérifier modèle par modèle)*.

---

## 2. Préparation : passes, profondeur et conventions

### But

Préparer le renderer à plusieurs passes par frame et fixer les conventions de la 3D **avant** d'écrire la première ligne de rendu 3D.

### La frame, avant et après

Aujourd'hui (`Renderer::end_frame()`) :

```
copies (sprites, ImGui) -> passe unique : effacement, sprites, ImGui -> soumission
```

En 3D :

```
copies (instances, uniforms, sprites, ImGui)
  -> passe d'ombre    : profondeur seule, vue depuis la lumière       (partie 8)
  -> passe 3D         : couleur + profondeur, vue depuis la caméra
  -> passe 2D / UI    : sprites en espace écran, puis ImGui, sans profondeur
  -> soumission
```

Les copies restent **avant** toutes les passes : SDL_GPU interdit de copier dans une passe de rendu (même règle qu'au jalon 2).

### Tâches

- [x] Faire évoluer `end_frame()` vers une suite ordonnée de passes, sans chercher à écrire un « frame graph » générique.
- [x] Créer la **texture de profondeur**, à la taille du swapchain, recréée au redimensionnement. Choisir son format avec `SDL_GPUTextureSupportsFormat` : SDL ne garantit que `D16_UNORM`, et **soit** `D24_UNORM` **soit** `D32_FLOAT`, jamais les deux.
- [x] Régler la dette du jalon 2 : la projection du `SpriteRenderer` vaut pour **toute la frame**. Il faut au moins deux vues de sprites : **écran** (interface, en pixels) et **monde** (avec une caméra). Le texte de statistiques de la démo 2D ne doit plus changer de taille avec le zoom.
- [x] Choisir et écrire les conventions : repère du monde, unités, profondeur du clip, sens des faces avant, culling (voir les questions).
- [ ] Organiser les shaders 3D selon les conventions de registres de SDL_GPU pour HLSL : dans un vertex shader, textures et échantillonneurs en `space0`, uniforms en `space1` ; dans un fragment shader, `space2` et `space3` (voir la documentation de `SDL_CreateGPUShader`). *Convention consignée ; elle s'appliquera au premier shader 3D (partie 4).*
- [x] Ajouter une scène vide « Rendu 3D » dans le menu du bac à sable, pour itérer.

### Questions à se poser

**Repère et unités**
- **Quel axe vers le haut ?** glTF impose : repère **main droite, Y vers le haut, mètres**. Blender travaille en Z vers le haut, mais son export glTF convertit. Suivre glTF évite toute conversion au chargement. Recommandation : main droite, Y vers le haut, 1 unité = 1 m.
- **Comment relier la grille au monde ?** Par exemple : la case (i, j) de la `TileMap` couvre `[i, i+1] × [j, j+1]` sur le plan du sol `y = 0`, soit le point monde `(i, 0, j)` pour son coin. Écrire cette correspondance une fois, dans une fonction testée.
- **Quelle taille de case ? Recommandation : 1 m.** Un personnage fait environ 1,8 m de haut et 0,6 à 0,8 m de large : il tient dans une case, ce qui simplifie les collisions et le pathfinding du jalon 6. Deux grilles différentes existeront sans doute plus tard, et n'ont pas à avoir la même taille : la **grille de jeu** (collisions, pathfinding, visibilité), fine, et les **modules de construction** des niveaux (sols, murs, salles), souvent de 2 ou 4 m dans les kits d'assets. Path of Exile compte en « unités » d'environ 10 cm *(approximation de la communauté, à vérifier)* : une grille plus fine que 1 m reste possible si le gameplay l'exige, la `TileMap` ne dépend pas de sa taille.
- **Quel cadrage ?** Un ARPG montre typiquement 25 à 35 m de largeur de monde à l'écran. Le régler en partie 3, avec la scène de démonstration sous les yeux.

**Profondeur**
- **Quelle plage de profondeur ?** Direct3D 12 et Metal utilisent `[0, 1]` ; GLM, par défaut, produit du `[-1, 1]` (OpenGL). Utiliser les fonctions `*_ZO` de GLM (`glm::perspectiveRH_ZO`, `glm::orthoRH_ZO`) ou `GLM_FORCE_DEPTH_ZERO_TO_ONE` partout.
- **Profondeur inversée (*reverse-Z*) ?** 1 au plus proche, 0 au plus loin, avec `D32_FLOAT` : bien meilleure précision en perspective. En projection orthographique (voir la partie 3), le gain est faible. À trancher avec la projection.

**Passes et anticrénelage**
- **Combien de passes ?** Sur les GPU d'Apple (architecture en tuiles), chaque passe relit et réécrit l'image en mémoire : préférer peu de passes, et des `load_op` / `store_op` justes (`DONT_CARE` pour une profondeur qu'on ne relit pas).
- **Quel anticrénelage ?** Sans lui, les arêtes des modèles sont en escalier. Le **MSAA** (4×) est le plus simple et le plus propre ; il demande une texture de résolution et `SDL_GPUTextureSupportsSampleCount`. Un filtre en post-traitement (FXAA) est l'autre option, plus tard.
- **Quelle résolution de rendu ?** La 3D à la résolution native Retina (2560×1440, quatre fois plus de pixels qu'à 1280×720) coûte cher, et un éclairage PBR se paie **par pixel**. Sur un M3 de base, une résolution de rendu réduite puis agrandie sera probablement nécessaire pour tenir 60 FPS, avec l'interface à la résolution native pour garder un texte net. Le réglage demande une cible de rendu hors écran : il sera mis en place avec la cible HDR de la partie 6, qui en crée une de toute façon.

### Pièges connus

- La profondeur de GLM en `[-1, 1]` : la moitié de la précision perdue, ou des objets coupés près de la caméra, parfois sur un seul backend.
- Oublier de recréer la texture de profondeur au redimensionnement : message de validation ou plantage.
- Supposer que `D24_UNORM` existe partout *(il manquerait sur les GPU d'Apple, à vérifier)* : toujours tester le support.
- Des registres HLSL dans le mauvais `space` : shader accepté sur un backend et rejeté (ou faux) sur l'autre.
- Oublier `export_msl_shaders` après avoir modifié un shader : le Mac utilise une version périmée, sans erreur.
- L'ordre des matrices entre GLM (colonnes) et HLSL (`mul(M, v)` ou `mul(v, M)`) : garder la convention déjà en place pour les sprites.

### Implémentation réalisée (partie 2)

Fichiers : `renderer.hpp` / `renderer.cpp`, `sprite_renderer.hpp` / `sprite_renderer.cpp`, et le bac à sable.

- **Deux passes par frame** dans `Renderer::end_frame()`, après toutes les copies :
  - **« scene »** : couleur effacée + profondeur effacée. Elle reçoit aujourd'hui les sprites du monde (`renderer.sprites()`), demain la 3D.
  - **« overlay »** : couleur conservée (`LOAD`), sans profondeur. Elle reçoit les sprites d'interface (`renderer.screen_sprites()`) puis ImGui.
  - Chaque passe est entourée d'un groupe de debug (`SDL_PushGPUDebugGroup`) à son nom, pour la retrouver dans RenderDoc et Xcode.
- **Texture de profondeur** : son format est choisi une fois, au démarrage, par `pick_depth_format()` : `D32_FLOAT` si le GPU sait l'utiliser comme cible, sinon `D24_UNORM`, sinon `D16_UNORM` (toujours garanti). Le format est écrit dans le log (`GPU: ... depth=D32_FLOAT`) et affiché par la scène « Rendu 3D ». La texture est à la taille du swapchain et recréée dans `begin_frame()` quand cette taille change. Sa sauvegarde en fin de passe est `DONT_CARE` : rien ne la relit, ce qui évite aux GPU en tuiles (Apple) de l'écrire en mémoire.
- **Deux files de sprites** : `sprites()` (le monde, avec la caméra de `set_view_projection()`) et `screen_sprites()` (l'interface, toujours en pixels de fenêtre). Ce sont deux `SpriteRenderer`, qui prennent désormais le format de profondeur de leur passe (leur pipeline doit le déclarer, sans tester ni écrire la profondeur) et un nom qui préfixe leurs ressources GPU (`sprite pipeline`, `screen sprite.vertices`...).
- **Dette du jalon 2 réglée** : le texte de statistiques de la démo passe par `screen_sprites()`. Il ne change plus de taille avec le zoom, et le contournement par `screen_to_world()` a disparu.
- **Scène « Rendu 3D (en construction) »** dans **DEBUG > Tests moteur** (et `--3d` en ligne de commande) : pour l'instant, elle affiche le format de profondeur et la taille de rendu.
- **Conventions** : consignées dans la section [Décisions à consigner](#décisions-à-consigner). GLM utilisait déjà les projections à profondeur `[0, 1]` (`orthoRH_ZO`) : rien à changer.

**Vérifications faites (Windows)**

- **Aucun pixel ne change** dans les scènes 2D : captures identiques avant et après la refonte pour le test de charge (avec et sans `--depth`), `--iso`, `--atlas`, `--text` et `--demo` (`bd91e8b2c616`, alors que son texte a changé de file de sprites).
- Démo au zoom ×3 : le monde est trois fois plus grand, le texte de statistiques garde sa taille et sa place.
- Build Debug (validation GPU active) : quatre redimensionnements (donc quatre textures de profondeur recréées), minimisation, maximisation, aucun message de validation. Format choisi sur la RTX 4070 Ti : `D32_FLOAT`.
- Coût de la seconde passe : 0,45 ms de CPU par frame pour 10 000 sprites, contre 0,43 ms au jalon 2, soit dans le bruit de mesure.
- 135 tests unitaires au vert, en Debug et en Release, sans avertissement.

### Validation

- [x] La frame enchaîne plusieurs passes, et les captures des scènes 2D sont **inchangées** (même hachage qu'avant la refonte, par exemple `bd91e8b2c616` pour la démo).
- [ ] La texture de profondeur est créée dans un format supporté sur les deux OS et survit au redimensionnement, sans message de validation. *Windows vérifié (`D32_FLOAT`) ; format à relever sur Mac.*
- [x] Des sprites en espace écran et en espace monde cohabitent dans la même frame.
- [x] Les conventions sont écrites dans la section [Décisions à consigner](#décisions-à-consigner).

---

## 3. Caméra 3D isométrique

### But

Une caméra qui regarde le monde depuis une orientation fixe, suit une cible, zoome, et convertit entre l'écran et le monde : surtout, **quelle case du sol est sous la souris**.

### L'isométrie en 3D

- La **vraie isométrie** : projection **orthographique**, caméra tournée de 45° autour de l'axe vertical et inclinée d'environ **35,26°** vers le bas. Les trois axes paraissent de même longueur.
- L'« isométrie » **2:1** des jeux 2D (celle du jalon 2) correspond en fait à une inclinaison d'environ **30°** (projection dimétrique) : le losange d'une case est deux fois plus large que haut.
- Beaucoup d'ARPG 3D récents utilisent plutôt une **perspective** au champ étroit, plus plongeante (45 à 60°) : plus de relief, les objets lointains un peu plus petits.

### Tâches

- [x] Créer une classe `Camera3D` : cible, distance, angles (fixes), type de projection, taille de la vue. *Faite en partie 4 pour comparer les projections : cadrage par la hauteur visible à la cible, commun aux deux.*
- [x] Calculer les matrices vue et projection (profondeur `[0, 1]`, voir la partie 2).
- [ ] Construire un **rayon** depuis un pixel de l'écran, et l'intersecter avec le plan du sol : la case de la `TileMap` sous la souris.
- [ ] Projeter un point du monde vers l'écran (pour placer du 2D au-dessus d'un objet, partie 10).
- [ ] Calculer le **frustum** (les six plans de la vue), pour le culling (partie 9), et la zone du sol visible.
- [ ] Interpoler la caméra entre deux pas fixes, comme `Camera2D`.
- [ ] Suivre une cible avec un lissage calculé sur le pas fixe.
- [ ] Tests unitaires : aller-retour monde, écran, rayon ; le point du sol sous le centre de l'écran est la cible ; coins de la fenêtre ; conversion points / pixels ; frustum contre des boîtes connues.

### Questions à se poser

**Projection et angles**
- **Orthographique ou perspective ?** L'orthographique donne des tailles constantes, une lecture tactique claire et une zone visible rectangulaire au sol ; le zoom change la taille de la vue. La perspective donne plus de profondeur et de vie, mais la zone visible au sol devient un trapèze et le zoom joue sur la distance.
- **Quels angles ?** Inclinaison et orientation fixes. La caméra pourra-t-elle **tourner** un jour ? Si oui, l'art et l'occlusion doivent le supporter dès le départ.
- **Plans proche et lointain** : les rapprocher au maximum de la scène pour garder de la précision de profondeur.

**Lisibilité**
- **Que faire quand un mur cache le personnage ?** Rendre le mur transparent, le découper, afficher une silhouette ? À noter dès maintenant ; le traitement peut attendre la démo.
- **Quelle plage de zoom ?** En 3D, pas de contrainte de pixel art : le zoom peut être continu.

**Picking**
- **Le sol plat suffit-il ?** Oui tant que le monde est plat. Avec du relief (escaliers, collines), il faudra intersecter le rayon avec la géométrie ou une carte de hauteurs.
- **Comment cliquer sur un ennemi ?** Rayon contre les boîtes englobantes des objets, ou **tampon d'identifiants** (dessiner l'identifiant de chaque objet dans une texture et lire le pixel sous la souris : exact au pixel, mais une lecture GPU vers CPU).

### Pièges connus

- Construire le rayon avec la position de la souris en **points** au lieu de pixels : décalage sur Retina (même piège qu'au jalon 2).
- Accumuler des angles d'Euler au lieu de reconstruire la vue avec `lookAt` depuis la cible : dérive et blocage d'axes.
- Des plans proche et lointain trop éloignés : scintillement entre surfaces proches (*z-fighting*).
- Un rayon presque parallèle au sol : division par un nombre proche de zéro. Impossible avec une caméra plongeante fixe, mais à protéger.
- Le sens de Y : Y écran vers le bas, Y du clip vers le haut *(SDL_GPU normalise ce sens entre les backends, à vérifier)*.

### Validation

- [ ] La case sous la souris est correcte aux quatre coins de la fenêtre, à plusieurs niveaux de zoom (test unitaire et scène).
- [ ] Un point du sol projeté à l'écran puis relancé en rayon retombe sur lui-même (écart inférieur à 0,001).
- [ ] Même résultat sur les deux OS, écran Retina compris.

---

## 4. Maillages et premier cube

### But

Dessiner des objets 3D : un format de sommets, des tampons, un pipeline avec test de profondeur et élimination des faces arrière.

### Tâches

- [x] Définir le format de sommet : position (`float3`), normale (`float3`), coordonnées de texture (`float2`), et plus tard tangente (`float4`). Noter sa taille en octets. *`Vertex3D`, 32 octets.*
- [x] Créer une classe `Mesh` : tampons de sommets et d'indices sur le GPU, nombre d'indices, **boîte englobante**, sous-parties par matériau. *Sans sous-parties : elles viendront avec les matériaux du glTF (parties 5 et 7).*
- [x] Générer des primitives en code (cube, plan, sphère), pour tester sans fichier.
- [x] Créer le pipeline 3D : test et écriture de profondeur, élimination des faces arrière, sens des faces avant, mélange désactivé pour les objets opaques.
- [x] Envoyer les uniforms : matrice vue-projection une fois par frame, matrice monde (et matrice des normales) par objet. *Les trois matrices sont poussées à chaque draw pour l'instant (192 octets) ; l'instanciation de la partie 9 les regroupera.*
- [x] Ajouter la scène « cube qui tourne » au menu.
- [x] Tests unitaires : primitives (nombre de sommets et d'indices, normales unitaires, boîtes englobantes).

### Questions à se poser

- **Indices 16 ou 32 bits ?** 16 bits limitent un maillage à 65 536 sommets.
- **Un tampon par maillage, ou un grand tampon partagé ?** Le grand tampon réduit les changements de liaison et prépare l'instanciation et le rendu indirect ; le tampon par maillage est plus simple à écrire.
- **Comment transmettre la matrice de chaque objet ?** Un uniform poussé à chaque draw (`SDL_PushGPUVertexUniformData`, simple), ou un tampon de stockage lu par index (prépare l'instanciation de la partie 9).
- **Un format de sommet compact** (normales sur 8 bits, coordonnées de texture en demi-flottants) ? Plus tard, et seulement si la mesure le demande.
- **Objets transparents** (vitres, effets) : ils se trient d'arrière en avant et se dessinent après les opaques, sans écrire la profondeur. Nécessaire dès maintenant, ou à la partie 10 ?

### Pièges connus

- Sens des faces inversé : objet invisible, ou vu de l'intérieur. glTF définit les faces avant dans le sens **antihoraire**.
- Profondeur non effacée en début de passe, ou test désactivé : objets dessinés dans le désordre.
- Transformer les normales par la matrice monde au lieu de sa **transposée inverse** : éclairage faux dès qu'une échelle n'est pas uniforme.
- L'alignement des structures d'uniforms entre C++ et HLSL : un `float3` occupe 16 octets dans un tampon constant.
- Un format de sommet déclaré différemment dans le pipeline et dans le shader : rendu chaotique, parfois sur un seul backend.

### Implémentation réalisée (partie 4)

Fichiers : `mesh.hpp` / `mesh.cpp`, `mesh_renderer.hpp` / `mesh_renderer.cpp`, `camera3d.hpp` / `camera3d.cpp`, `shaders/mesh.vert.hlsl` et `mesh.frag.hlsl` (MSL exporté dans `shaders/generated/msl/`), tests dans `test_mesh.cpp` et `test_camera3d.cpp`.

- **`MeshData`** : un maillage sur le CPU (sommets `Vertex3D` de 32 octets, indices 32 bits), testable sans GPU, avec sa boîte englobante (`Aabb`). **Primitives** : `make_cube`, `make_plane` (au sol, face vers le haut), `make_sphere` (sphère UV). Chaque face du cube a ses propres sommets, pour garder des arêtes nettes.
- **`Mesh`** : les tampons GPU d'un maillage, envoyés une fois (`Mesh::create`), avec le nombre d'indices et la boîte englobante.
- **`MeshRenderer`** (`renderer.meshes()`) : comme les sprites, le jeu **enregistre** des draws (maillage, matrice monde, couleur) avec la caméra de la frame (`set_camera`), et le moteur les exécute au début de la passe « scene », **avant** les sprites du monde. Pipeline : test et écriture de profondeur (`LESS`), faces arrière éliminées, faces avant dans le sens antihoraire (convention glTF), sans mélange. Un draw call par draw pour l'instant ; le tampon lié ne change que quand le maillage change. Les statistiques comptent maintenant les maillages et les triangles (`RenderStats::meshes`, `triangles`).
- **Éclairage provisoire** : une lumière directionnelle et une lumière ambiante (`set_light`), sur une couleur unie. Le PBR arrive en partie 7.
- **Uniforms** : le vertex shader reçoit la vue-projection, la matrice monde et sa **transposée inverse** (pour les normales) ; le fragment shader, la couleur et la lumière. Les structures ne contiennent que des `float4x4` et des `float4`, pour qu'aucune règle d'alignement ne diffère entre C++ et HLSL. Registres : `b0, space1` (vertex) et `b0, space3` (fragment), selon la convention de SDL_GPU.
- **`Camera3D`** (partie 3, en avance) : cible, orientation et inclinaison fixes, projection orthographique ou perspective. Le cadrage se règle par une seule valeur, la **hauteur de monde visible à la cible** : en perspective, la distance de la caméra en découle (`hauteur / 2 / tan(champ / 2)`), si bien que les deux projections cadrent la cible exactement pareil et ne diffèrent que par la perspective. `isometric_pitch()` donne l'angle de la vraie isométrie (35,26°).
- **Scène « Rendu 3D : premiers maillages »** (menu, ou `--3d`, `--perspective`) : un sol de 20×20 cases d'1 m en damier, un mur de cubes d'1 m, un cube qui tourne, une sphère, une boîte à la taille d'un personnage (0,7 × 1,8 × 0,7 m) et quatre piliers de 3 m. **P** change de projection ; flèches ou ZQSD déplacent la cible, la molette change la hauteur visible. Dans le menu, le panneau du test en cours règle la projection, l'inclinaison, l'orientation, la hauteur visible et le champ de vision, avec des boutons « Vraie isométrie » et « Recentrer ».

**Vérifications faites (Windows)**

- 11 cas de tests unitaires : primitives (nombres, boîtes, normales unitaires, **chaque triangle antihoraire et tourné vers l'extérieur**), caméra (cible au centre et dans la plage de profondeur, **même cadrage dans les deux projections**, profondeur croissante avec la distance, rétrécissement en perspective seulement, égalité des trois axes à l'angle isométrique, bornes). 146 tests au vert. Un premier test de la perspective était faux (il mesurait un poteau vertical, dont l'angle de vue change aussi avec la distance) : remplacé par une barre en travers de la vue, comparée à la valeur calculée.
- Rendu : profondeur et faces correctes du premier coup (mur, boîte et sol se masquent comme il faut, l'intérieur des cubes n'est jamais dessiné). 416 maillages, 2 004 triangles, 417 draw calls à 165 FPS.
- Build Debug (validation GPU) : aucun message, dans les deux projections et en redimensionnant la fenêtre.
- Les six scènes 2D gardent exactement les mêmes captures.

### Validation

- [ ] Un cube éclairé simplement (produit scalaire entre la normale et une direction fixe) tourne, avec ses faces correctes, sur les deux OS. *Windows vérifié.*
- [x] Deux cubes qui se traversent s'intersectent correctement (profondeur). *Le cube qui tourne traverse l'air, mais le mur, la boîte et le sol se masquent correctement ; les tests unitaires vérifient l'ordre des profondeurs.*
- [x] Aucun message de la couche de validation.

---

## 5. Chargement de modèles (glTF)

### But

Charger les modèles produits par un outil 3D : géométrie, matériaux, textures, hiérarchie.

### Pourquoi glTF 2.0

Format ouvert de Khronos, souvent appelé « le JPEG de la 3D ». Blender l'exporte nativement. Il décrit les maillages, les matériaux (PBR métal / rugosité), la hiérarchie des nœuds, **les squelettes et les animations** (utiles au jalon d'animation). Deux formes : `.gltf` (JSON + `.bin` + images séparées) et `.glb` (un seul fichier binaire).

### Les bibliothèques disponibles

Versions de la baseline vcpkg actuelle du projet :

| Bibliothèque | Version | Points forts | Points faibles |
|---|---|---|---|
| `cgltf` | 1.15 | Un seul en-tête C, simple, très répandue | API en C, lecture des accesseurs un peu verbeuse |
| `fastgltf` | 0.9.0 | C++ moderne, très rapide | API plus riche à apprendre |
| `tinygltf` | 3.0.0 | C++, charge aussi les images | Plus lente, dépend de JSON et stb |
| `assimp` | 6.0.4 | Tous les formats (FBX, OBJ...) | Lourde, résultats parfois surprenants |

Recommandation : `cgltf` ou `fastgltf`. Un seul format d'entrée (glTF) simplifie tout le reste de la chaîne.

### Tâches

- [x] Choisir la bibliothèque et l'ajouter à `vcpkg.json`. *`cgltf` 1.15.*
- [x] Lire un `.glb` : maillages (positions, normales, coordonnées de texture, indices), matériaux (couleur de base, texture), nœuds (hiérarchie et transformations).
- [x] Convertir vers le format de sommets du moteur ; calculer les normales si elles manquent.
- [x] Charger les textures référencées. Aujourd'hui, `load_image` ne lit que le PNG (`STBI_ONLY_PNG`) : ajouter le JPEG si les modèles en contiennent. *PNG et JPEG, depuis un fichier ou depuis la mémoire (`decode_image`).*
- [x] Nommer le fichier dans toute erreur, comme pour les atlas et les animations.
- [x] Trouver trois ou quatre modèles de test à licence libre, noter leur licence, les ranger (par exemple `art/models/`). *Quatre modèles **Poly Haven** en CC0 (tonneau, lanterne, estoc, rocher), téléchargés par `tools/models/fetch_test_models.py` dans `assets/models/polyhaven/`, hors de Git ; auteurs et licences dans `assets/credits.json` et la fenêtre « À propos ».*
- [x] Tests unitaires sur un petit `.gltf` écrit à la main (un triangle) : nombre de sommets, valeurs lues, erreurs.

### Questions à se poser

- **Lire le glTF à l'exécution, ou le convertir hors ligne** vers un format binaire du moteur (comme `atlas_packer`) ? Hors ligne : chargement instantané, validation au build, optimisation avec `meshoptimizer` (1.2 dans vcpkg). À l'exécution : rien à écrire de plus. Recommandation : **à l'exécution d'abord**, l'outil hors ligne quand le temps de chargement le justifie.
- **Garder la hiérarchie des nœuds, ou tout fusionner** en un maillage par modèle ? La hiérarchie servira à attacher une arme à une main (jalon d'animation).
- **Quelles parties du glTF prendre en charge ?** Les caméras, les lumières (`KHR_lights_punctual`) et les extensions de matériaux peuvent être ignorées au début.
- **Où ranger les modèles ?** Leur poids pose à nouveau la question de **Git LFS**, restée ouverte au jalon 2.
- **Vérifier l'export Blender** : échelle, axe « +Y vers le haut », application des transformations. Faire un modèle de référence (un cube de 1 m avec une flèche vers +X) et le garder.

### Pièges connus

- Lire les accesseurs à la main : *stride*, types normalisés (coordonnées de texture en entiers 16 bits), indices en 8, 16 ou 32 bits. Passer par les fonctions de la bibliothèque.
- Oublier les transformations des nœuds parents : pièces mal placées.
- Les chemins relatifs des `.bin` et des images d'un `.gltf` : préférer le `.glb`.
- Des modèles gratuits énormes (des millions de triangles) : vérifier le budget avant de les adopter.
- Les textures de couleur sont en sRGB, les cartes de normales en linéaire (voir la partie 6).

### Implémentation réalisée (partie 5)

Fichiers : `model.hpp` / `model.cpp`, `image.hpp` / `image.cpp` (JPEG, décodage en mémoire), `mesh_renderer` et `shaders/mesh.frag.hlsl` (texture de couleur), `tools/models/make_reference_model.py`, `assets/models/reference.glb`, tests dans `test_model.cpp`.

- **`ModelData`** : un modèle sur le CPU, testable sans GPU : ses **pièces** (`ModelPart` : un `MeshData`, sa transformation dans le modèle, son matériau), ses **matériaux** (`ModelMaterial` : couleur de base, index de sa texture, et déjà les facteurs métal et rugosité pour la partie 7) et ses **images** décodées.
- **`load_gltf(chemin)` / `parse_gltf(données, nom, dossier)`** avec `cgltf` :
  - `.gltf` (avec ses `.bin` et images à côté) ou `.glb` ; les fichiers externes sont lus par `SDL_LoadFile` (chemins UTF-8 sous Windows), les données intégrées en base64 aussi.
  - Maillages en triangles seulement (les autres primitives sont ignorées avec un message) ; positions obligatoires ; normales lues, ou **calculées** (lissées, pondérées par l'aire) si elles manquent ; premières coordonnées de texture ; indices de toute taille, ou générés s'ils manquent. Tous les types d'attributs sont lus par `cgltf` (flottants, entiers normalisés...).
  - **Hiérarchie aplatie** : chaque maillage utilisé par un nœud de la scène par défaut devient des pièces portant la matrice monde du nœud. Les conventions de glTF sont celles du moteur : rien n'est converti.
  - Matériaux : couleur de base, texture de couleur (intégrée au `.glb`, en data URI ou en fichier externe, PNG ou JPEG), métal, rugosité.
  - **Refus explicite** d'un fichier qui exige une extension non prise en charge (compression Draco ou meshopt, par exemple), plutôt que d'afficher un maillage faux. Validation par `cgltf_validate`.
  - Toute erreur commence par `Model '<fichier>': ...`.
- **`Model`** : le modèle sur le GPU (un `Mesh` par pièce, les textures, la boîte englobante, le nombre de triangles). `Model::load(renderer, chemin)` fait tout.
- **`MeshRenderer`** : un maillage peut maintenant recevoir une texture de couleur, lue en filtrage linéaire et répétée au-delà de [0, 1] ; sans texture, une texture blanche 1×1 la remplace, pour garder un seul shader. `draw(model, monde)` dessine toutes les pièces avec leur matériau.
- **Modèle de référence** généré par `tools/models/make_reference_model.py` (Python seul, sortie déterministe, 8 Ko) : un cube de 1 m posé au sol, une texture en quatre quadrants de couleur sur chaque face (rouge en haut à gauche de l'image), une flèche rouge vers +X, un repère vert vers +Y et un bleu vers +Z, la flèche et les repères étant des **nœuds enfants** du cube. Blender n'étant pas lancé, il a été écrit par script plutôt qu'exporté ; l'export depuis Blender reste à vérifier avec un modèle fait à la main.
- **Scène 3D** : elle charge tous les `.glb` et `.gltf` de `assets/models/` et les pose en rang, chacun sur un centre de case, à son échelle (réduits seulement s'ils dépassent 3,5 m, avec une mention), avec leur nom, leur nombre de triangles et leur temps de chargement affichés au-dessus. Un modèle qui ne se charge pas affiche son erreur à sa place. Nouvelles options : `--camera X Z` (point visé au sol) et `--view-height H` (hauteur visible), pour des captures rapprochées.
- **Caméra retenue** (partie 4) : la perspective à 50° et 30° de champ est devenue le réglage par défaut de `Camera3D` ; `--ortho` et le bouton « Vraie isométrie » gardent la comparaison possible.

**Vérifications faites (Windows)**

- 6 nouveaux cas de tests unitaires (152 au total) : triangle sans normales ni indices (normales calculées), hiérarchie parent / enfant (transformation composée, boîte englobante), matériau avec texture PNG en data URI (pixels décodés vérifiés), indices 16 bits et coordonnées de texture en entiers normalisés, vrai `.glb` construit en mémoire, erreurs (JSON invalide, glTF 1.0, aucun triangle, extension exigée, fichier absent).
- **Le modèle de référence a trouvé une erreur** : la pointe de la flèche était écrite à l'envers dans le script (ses six triangles tournés vers l'intérieur, donc invisibles avec l'élimination des faces arrière). Une vérification du sens des triangles l'a montré avant tout rendu ; corrigée.
- Rendu du modèle de référence, en perspective et en orthographique : texture dans le bon sens sur les trois faces visibles, flèche et repères sur les bonnes faces (hiérarchie correcte), cube de la taille exacte d'une case. Chargement en 4 à 6 ms.
- Build Debug (validation GPU) : aucun message. Les six scènes 2D gardent les mêmes captures.

**Modèles de test externes : Poly Haven.** Choisis pour être entièrement libres et à jour : tout Poly Haven est en **CC0** (domaine public, usage commercial permis, citation non obligatoire mais appréciée), le catalogue est maintenu (plus de 240 modèles, dont des publications de 2024-2025), et chaque modèle vient avec ses textures PBR complètes (couleur, normales, occlusion / rugosité / métal), qui serviront aux parties 6 et 7. Retenus, en 1K :

| Modèle | Auteurs | Ce qu'il teste | Résultat |
|---|---|---|---|
| Wine Barrel 01 | James Ray Cock | bois, 4 pièces, 0,87 m | 10 820 triangles, 15 ms |
| Lantern 01 | Rajil Jose Macatangay | laiton, verre (transmission, ignorée pour l'instant), 0,29 m | 33 902 triangles, 20 ms |
| Antique Estoc | Ulan Cabanilla, James Ray Cock | acier, objet fin et long (1,49 m) | 8 246 triangles, 15 ms |
| Boulder 01 | Rico Cilliers | roche scannée, test de charge | 66 122 triangles, 23 ms |

- **Téléchargement** : `python tools/models/fetch_test_models.py` passe par l'API officielle (qui exige un User-Agent), vérifie la taille de chaque fichier et ne retélécharge pas ce qui est déjà là. Environ 11 Mo, 20 fichiers (`.gltf`, `.bin`, trois textures JPEG par modèle), **hors de Git** (`.gitignore`) tant que Git LFS n'est pas décidé.
- **Résultat** : les quatre s'affichent avec leurs textures dans le bon sens et à leur taille réelle (comparée aux cases de 1 m). Aucun n'exige d'extension ; tous utilisent des fichiers externes (`.bin` et `textures/`), ce qui a validé ce chemin du chargeur, sous-dossiers compris.
- La scène cherche les modèles dans les sous-dossiers de `assets/models/` et n'affiche que le nom du fichier au-dessus de chacun.

**Crédits et licences.** Les licences MIT et zlib des bibliothèques exigent que leur notice accompagne le programme : au build, `moteur_add_licenses()` copie le texte de licence installé par vcpkg pour chaque bibliothèque dans `licenses/` à côté de l'exécutable. `assets/credits.json` décrit les bibliothèques (version, licence, copyright relevés dans les fichiers de vcpkg), la police et les modèles (auteurs, licence, source). Le bac à sable l'affiche dans une fenêtre **« À propos »** (menu **Aide**, ou bouton de l'accueil), avec le texte complet de chaque licence à la demande et la présence de chaque modèle.

### Validation

- [ ] Trois modèles de test différents s'affichent correctement (orientation, échelle, couleurs) sur les deux OS. *Windows vérifié avec les quatre modèles Poly Haven.*
- [x] Le modèle de référence apparaît à la bonne taille et dans le bon sens.
- [x] Un fichier invalide produit une erreur qui nomme le fichier.
- [x] Le temps de chargement est mesuré et noté. *Affiché au-dessus de chaque modèle et écrit dans le log ; 4 à 6 ms pour le modèle de référence.*

---

## 6. Textures et couleur

### But

Des textures correctes en 3D : mipmaps, filtrage, et un **éclairage calculé en couleur linéaire**.

### Pourquoi c'est un sujet maintenant

Au jalon 1, tout est en `UNORM` et le mélange se fait dans l'espace de l'image. C'est juste pour du 2D sans éclairage. Mais les calculs de lumière (additionner deux lumières, multiplier par une couleur) ne sont justes qu'en **valeurs linéaires**. Faits sur des valeurs sRGB, les dégradés sont trop sombres et les lumières s'additionnent mal.

SDL_GPU propose les formats `R8G8B8A8_UNORM_SRGB` et `B8G8R8A8_UNORM_SRGB`, garantis à la fois pour l'échantillonnage et comme cible de rendu : le GPU convertit tout seul à la lecture (sRGB vers linéaire) et à l'écriture (linéaire vers sRGB).

### Tâches

- [x] Générer les **mipmaps** des textures 3D (`SDL_GenerateMipmapsForGPUTexture`) et utiliser un filtrage trilinéaire, avec anisotropie en option. *Anisotropie ×8 activée.*
- [x] Passer la chaîne 3D en linéaire : textures de couleur en `_SRGB`, rendu 3D dans une cible qui convertit (ou en flottant, puis conversion finale).
- [x] Distinguer les textures de **couleur** (sRGB) et de **données** (normales, rugosité : linéaire). *`TextureSettings::srgb` ; seules les textures de couleur sont lues pour l'instant, les autres arrivent en partie 7.*
- [x] Rendre la 3D dans une cible HDR en flottant, puis appliquer le tone mapping vers le swapchain.
- [x] Décider du sort du 2D (voir les questions) et vérifier les scènes 2D existantes. *Le 2D reste tel quel, par-dessus la 3D convertie : captures identiques.*

### Questions à se poser

- **Toute la frame en linéaire, ou seulement la 3D ?** Avec un rendu PBR réaliste, la réponse recommandée est la **cible intermédiaire en flottant** (`R16G16B16A16_FLOAT`) pour la 3D, convertie vers le swapchain par un *tone mapping* (ACES, AgX ou Khronos PBR Neutral), puis le 2D par-dessus comme aujourd'hui. Les lumières PBR dépassent facilement 1,0 : sans HDR, elles saturent. Le 2D garde son aspect actuel.
- **Quel tone mapping ?** *Khronos PBR Neutral* garde les couleurs des matériaux fidèles (utile pour comparer à Blender) ; ACES et AgX donnent un rendu plus « cinéma ». Blender utilise AgX par défaut *(à vérifier selon la version)*.
- **Compression GPU des textures ?** Toujours repoussée depuis le jalon 2 : BC7 (Windows) et ASTC (Mac) diffèrent. En PBR réaliste, un matériau compte 3 à 5 textures (couleur, normales, rugosité / métal, occlusion, émissif) : sans compression, une texture 2048×2048 pèse 16 Mo avec ses mipmaps, et la mémoire d'un M3 de base (8 à 16 Go partagés avec le système) part vite. `ktx` (4.4.2 dans vcpkg) permet un format unique transcodé au chargement. **À ne plus repousser au-delà de ce jalon** si les scènes de test chargent de vrais matériaux.

### Pièges connus

- Une double conversion (texture lue en sRGB puis « corrigée » encore dans le shader) : image délavée ou trop sombre.
- Des cartes de normales chargées en sRGB : un éclairage faux, subtil, difficile à repérer.
- Pas de mipmaps : scintillement et moiré des textures au dézoom.
- Des mipmaps calculées sur des couleurs non pré-multipliées : franges sombres ou claires autour des zones transparentes.

### Implémentation réalisée (partie 6)

Fichiers : `color.hpp` / `color.cpp`, `tone_mapper.hpp` / `tone_mapper.cpp` (internes au moteur), `shaders/tonemap.vert.hlsl` et `tonemap.frag.hlsl`, `renderer`, `mesh_renderer`, `model.cpp`, tests dans `test_color.cpp`.

- **Nouvelle structure de frame** (elle remplace les deux passes de la partie 2) :
  - **« scene »**, seulement si des maillages ont été enregistrés : la 3D, en **couleurs linéaires HDR**, dans une texture hors écran `R16G16B16A16_FLOAT` avec sa profondeur, à la **résolution de rendu**. La couleur d'effacement, donnée en valeurs d'écran comme en 2D, est convertie en linéaire.
  - **« compose »**, sur le swapchain : la scène convertie pour l'écran par un triangle plein écran (ou la couleur d'effacement sans 3D), puis les sprites du monde, l'interface et ImGui. Une seule passe sur l'écran au lieu de deux, ce qui convient aux GPU en tuiles.
  - **Une frame sans 3D se dessine exactement comme avant** : les sprites du monde ne passent jamais par la cible HDR.
- **Conversion pour l'écran** (`ToneMapper`, `tonemap.frag.hlsl`) : exposition, puis tone mapping **Khronos PBR Neutral** (les couleurs ordinaires passent presque inchangées, comme dans Blender et les visionneuses glTF ; les fortes lumières sont compressées vers le blanc), puis **encodage sRGB** dans le shader. Le swapchain reste en UNORM simple : c'est ce qui laisse le 2D intact. Le shader suit à l'identique `color.cpp`, que les tests vérifient.
- **Résolution de rendu** (`set_render_scale`, de 0,25 à 1) : la 3D est rendue à cette fraction de la fenêtre et agrandie par le filtrage linéaire de la conversion ; les sprites et l'interface restent à pleine résolution. **Exposition** (`set_exposure`). Les deux sont réglables dans le panneau de la scène 3D ; `--render-scale S` en ligne de commande.
- **Textures** : `create_texture(image, TextureSettings, nom)` choisit le format sRGB ou non, les **mipmaps** (chaîne complète, générées par le GPU ; en sRGB, le filtrage se fait en linéaire) et la pré-multiplication de l'alpha. Les textures de couleur des modèles sont en sRGB, avec mipmaps, en alpha direct. L'échantillonneur des maillages est trilinéaire et **anisotrope ×8**. L'ancien `create_texture(image, nom)` (sprites, interface) ne change pas.
- **Couleurs en linéaire** : les couleurs données au `MeshRenderer` (couleurs des draws, lumière) sont désormais **linéaires**. `srgb_to_linear()` convertit une couleur choisie à l'œil ; le bac à sable le fait pour toutes les couleurs de sa scène 3D.

**Vérifications faites (Windows)**

- 3 nouveaux cas de tests unitaires (155 au total) : valeurs connues du sRGB (gris moyen 0,5 → 0,214 en linéaire ; gris à 18 % → 0,461), aller-retour exact sur les 256 niveaux, tone mapping (noir inchangé, teinte conservée sous le seuil de compression, sortie croissante et toujours inférieure à 1, désaturation des lumières très fortes).
- **Les six scènes 2D gardent exactement les mêmes captures**, alors que la frame est restructurée.
- Scène 3D : les textures ont retrouvé leurs couleurs (bois et cerclages du tonneau, nuances de la roche) et les dégradés d'éclairage sont plus naturels. Au dézoom maximal (60 m visibles), les modèles texturés restent lisses, sans scintillement. À la résolution de rendu 0,5, la 3D est rendue en 640×360 et agrandie, le texte reste net.
- Mesure (Release, sans VSync, scène 3D complète : 429 draw calls, 121 000 triangles) : 0,36 ms de CPU par frame, plus de 2 300 images par seconde, identique à 100 % et 50 % de résolution sur la RTX 4070 Ti (son GPU n'y est pas limitant ; le réglage vise le M3).
- Build Debug (validation GPU) : aucun message, y compris à résolution réduite et dans la démo 2D.

### Validation

- [ ] Un dégradé de gris éclairé est identique sur les deux OS et conforme à une valeur calculée. *Les conversions sont vérifiées par les tests unitaires ; la comparaison entre OS reste à faire.*
- [x] Aucun moiré au dézoom maximal. *Mipmaps et filtrage anisotrope ; les bords en escalier de la géométrie relèvent de l'anticrénelage (décision ouverte).*
- [x] Les captures des scènes 2D sont inchangées, ou leurs différences expliquées si le 2D passe en linéaire.

---

## 7. Matériaux et éclairage

### But

Des modèles éclairés : une lumière directionnelle (soleil, lune), une lumière ambiante, et des lumières ponctuelles (torches, sorts).

### Tâches

- [x] Définir un matériau PBR, calqué sur celui de glTF : couleur de base, métal, rugosité, normales, occlusion, émissif, chacun avec sa texture facultative.
- [x] Écrire le shader PBR (BRDF de Cook-Torrance avec GGX, comme la référence glTF).
- [x] Ajouter les **tangentes** au format de sommet ; les lire dans le glTF, ou les calculer (`mikktspace` est dans vcpkg, c'est l'algorithme utilisé par Blender).
- [x] **Éclairage d'environnement (IBL)** : charger un environnement HDR (Poly Haven), en tirer une carte d'irradiance (diffus) et une carte spéculaire pré-filtrée par rugosité, plus la table de BRDF. Ces calculs peuvent être faits une fois au chargement, ou hors ligne. *Au chargement, sur le CPU ; irradiance en harmoniques sphériques, table de BRDF remplacée par son approximation analytique. Le chargement des `.hdr` est prêt ; aucun HDRI n'est encore téléchargé : un ciel procédural sert par défaut.*
- [x] Valider le shader contre les modèles de référence de Khronos (par exemple la grille de sphères métal / rugosité). *Grille équivalente générée dans la scène (7×7 sphères), sans téléchargement.*
- [x] Lumière directionnelle et ambiante (ou **hémisphérique** : une couleur de ciel, une couleur de sol). *Soleil + environnement (l'ambiante est remplacée par l'IBL).*
- [x] Lumières ponctuelles : une liste dans un tampon, portée et atténuation. *Jusqu'à 32, dans le tampon d'uniforms de la frame ; atténuation de KHR_lights_punctual.*
- [ ] Trier les objets par pipeline puis par matériau, comme le batch 2D trie par texture. *Pas encore : les liaisons ne sont refaites que quand elles changent, le tri viendra avec l'instanciation (partie 9).*
- [x] Réglages en direct dans ImGui : direction, couleurs, intensités.

### Questions à se poser

- **PBR : tranché** (questions générales). Reste à décider le niveau : le modèle de base de glTF suffit ; les extensions (transmission, *clearcoat*, *sheen*) peuvent attendre.
- **Environnement : un seul par scène, ou plusieurs** (sondes de réflexion par zone : une grotte ne reflète pas le ciel) ? Un seul pour commencer.
- **Combien de lumières ponctuelles à la fois ?** Un ARPG en affiche beaucoup (sorts, projectiles). Une boucle simple sur N lumières tient jusqu'à 16 ou 32 ; au-delà, il faut un découpage de l'écran (*forward+*, *clustered*), à garder pour plus tard.
- **Les cartes de normales** maintenant, ou plus tard ? Elles demandent des tangentes dans le format de sommet.

### Pièges connus

- Mélanger les espaces : la normale en espace monde, la lumière en espace vue.
- Des normales non renormalisées après interpolation entre sommets.
- L'explosion des **variantes de shaders** (une par combinaison de fonctionnalités), chacune à compiler pour deux backends et à exporter en MSL : préférer quelques shaders et des paramètres.
- L'alignement des uniforms (encore).

### Implémentation réalisée (partie 7)

Fichiers : `material.hpp`, `environment.hpp` / `environment.cpp`, `mesh_renderer` (réécrit), `mesh.hpp` / `mesh.cpp` (tangentes), `model.cpp` (matériau complet), `shaders/mesh.vert.hlsl` et `mesh.frag.hlsl` (réécrits), `tools/models/make_reference_model.py` (dôme en relief), tests dans `test_environment.cpp` et `test_mesh.cpp`.

- **Tangentes** : `Vertex3D` passe à 48 octets avec une tangente (`xyz` le long des u croissants, `w` le sens de la bitangente). Lues dans le glTF (`TANGENT`), sinon calculées par **MikkTSpace** (`compute_tangents()`, paquet vcpkg `mikktspace`, licence zlib) : c'est le cas des quatre modèles Poly Haven. Point délicat de convention : glTF met v = 0 en haut de l'image, les cartes de normales ont le vert vers le haut ; MikkTSpace reçoit donc v inversé, comme dans Blender, pour que la bitangente pointe vers le haut de l'image.
- **`Material`** (commun au moteur) : le matériau glTF complet. Facteurs : couleur de base, métal, rugosité, force des normales, force de l'occlusion, émissif, double face. Textures facultatives : couleur (sRGB), métal / rugosité (bleu / vert), normales, occlusion (rouge), émissif (sRGB). Une texture absente vaut blanc (ou « surface plate » pour les normales). Le chargeur glTF lit tout, charge chaque image en sRGB ou en linéaire selon son usage (une même image peut servir aux deux) et applique `KHR_materials_emissive_strength`. La texture ARM de Poly Haven sert à la fois d'occlusion et de métal / rugosité, comme prévu par glTF.
- **Shader PBR** (`mesh.frag.hlsl`), sur le modèle de la référence glTF : diffus de Lambert et spéculaire de Cook-Torrance (distribution GGX, visibilité de Smith corrélée, Fresnel de Schlick), rugosité bornée à 0,04. Lumière = **environnement** (diffus par harmoniques sphériques, spéculaire par l'image préfiltrée au niveau de mipmap de la rugosité, facteur « split-sum » par l'approximation analytique de Karis) atténué par l'occlusion, + **soleil** + **lumières ponctuelles** (carré inverse ramené à zéro à la portée, comme `KHR_lights_punctual`), + émissif. Les matériaux **double face** (tous les modèles Poly Haven) ont leur pipeline sans élimination des faces, et le shader retourne la normale des faces arrière.
- **Environnement** (`environment.hpp`) : une image équirectangulaire de radiance linéaire. `make_sky()` en fabrique une (zénith, horizon, sol), **sans soleil** : il est déjà une lumière directionnelle. `load_environment()` lit un `.hdr` (Radiance, le format de Poly Haven). `project_irradiance()` en tire 9 coefficients d'harmoniques sphériques (le diffus) ; `prefilter_specular()` 6 images de plus en plus floues (rugosité de 0 à 1, de 256×128 à 8×4), par échantillonnage d'importance GGX avec suite de Hammersley et lecture dans une chaîne de mipmaps de la source pour éviter le bruit. `Environment::create()` envoie le tout au GPU (texture `R16G16B16A16_FLOAT`, un niveau par rugosité, via le nouveau `Renderer::create_texture_levels()`). Le ciel procédural se préfiltre en 87 ms (Release).
- **`MeshRenderer`** : `set_camera(vue-projection, œil)`, `set_sun()`, `set_environment()`, `add_light()` (jusqu'à 32 par frame, les suivantes sont comptées dans `RenderStats::dropped_lights`), `draw(maillage, monde, matériau)`. Les données communes à la frame (œil, soleil, harmoniques, lumières) sont envoyées une fois par frame ; les liaisons de textures ne sont refaites que quand elles changent.
- **Modèle de référence** : chaque face du cube porte une carte de normales avec un **dôme en relief**, sans tangentes dans le fichier : il vérifie d'un coup d'œil la convention des normales et le calcul des tangentes.
- **Scène 3D** : grille de 7×7 sphères (métal de l'avant vers l'arrière, rugosité de gauche à droite), 16 torches animées (lumières ponctuelles colorées et petites sphères émissives), ciel procédural ou tout `.hdr` de `assets/environments/`. Le panneau règle l'environnement et son intensité, le soleil (intensité, hauteur, direction), le nombre et l'intensité des torches.

**Vérifications faites (Windows)**

- 8 nouveaux cas de tests unitaires (163 au total) : tangentes (unitaires, perpendiculaires à la normale, le long des u croissants, bitangente vers le haut de l'image, sur les trois primitives), correspondance direction / image équirectangulaire dans les deux sens, irradiance d'un environnement uniforme (exacte) et d'un hémisphère éclairé (1 face au ciel, 0,5 de côté, presque 0 face au sol), préfiltrage (tailles, environnement uniforme inchangé, horizon flouté par la rugosité), ciel procédural. Deux premiers tests étaient mal posés (des niveaux de 4×2 et 2×1 pixels ne peuvent pas séparer le zénith du nadir ; un ciel de 32 rangées n'a pas de rangée assez proche de l'horizon) ; corrigés, pas le code.
- **Grille de sphères** : miroirs métalliques au fond à gauche, reflets de plus en plus flous vers la droite, non-métaux du premier plan passant du reflet net au mat : le comportement attendu de la référence Khronos.
- **Dôme du modèle de référence** : éclairé en haut sur les faces latérales et du côté du soleil sur le dessus, donc un relief, pas un creux : la convention des normales et les tangentes calculées sont justes.
- **Modèles Poly Haven** : bois, cerclages métalliques, relief du rocher, laiton et acier qui reflètent les torches.
- **Mesure** (Release, sans VSync, scène complète : 494 draw calls, 188 000 triangles, soleil + 16 torches + environnement) : 0,39 ms de CPU par frame, plus de 2 280 images par seconde.
- Build Debug (validation GPU) : aucun message. Les six scènes 2D gardent les mêmes captures.

### Validation

- [ ] Une sphère éclairée montre un dégradé et un reflet aux endroits attendus, identiques sur les deux OS. *Windows vérifié.*
- [x] La grille de sphères métal / rugosité de Khronos ressemble à son rendu de référence. *Grille équivalente générée dans la scène.*
- [ ] Les modèles de test ressemblent à leur aperçu dans Blender, sous le même environnement HDR. *Nécessite un HDRI (téléchargement à valider) et Blender.*
- [x] 16 lumières ponctuelles animées tournent à la cadence de l'écran.

---

## 8. Ombres

### But

Les ombres portées de la lumière principale. Au-delà de l'esthétique, elles rendent le monde **lisible** : elles montrent où un personnage ou un projectile touche le sol.

### Le principe : la carte d'ombre

1. Une première passe dessine la scène **depuis la lumière**, profondeur seule, dans une texture (la *shadow map*).
2. La passe principale compare, pour chaque pixel, sa distance à la lumière avec la valeur de la carte : plus loin, il est à l'ombre.

SDL_GPU le permet directement : un échantillonneur avec comparaison (`enable_compare`) sur une texture de profondeur.

**Un avantage de la caméra fixe** : la zone visible est bornée et stable. Une seule carte qui couvre la vue suffit souvent, là où un jeu à la première personne a besoin de plusieurs cartes en cascade.

### Tâches

- [ ] Créer la passe d'ombre : texture de profondeur, pipeline profondeur seule.
- [ ] Calculer la matrice de la lumière : une boîte orthographique qui englobe la zone visible au sol et la hauteur des objets.
- [ ] Échantillonner avec comparaison, et adoucir les bords par un filtrage (*PCF*, 3×3 ou disque de Poisson).
- [ ] **Stabiliser** : aligner la boîte de la lumière sur la grille des texels, sinon les bords d'ombre scintillent quand la caméra bouge.
- [ ] Régler les **biais** (constant et selon la pente) contre l'« acné » ; les exposer dans ImGui.
- [ ] Permettre de désactiver les ombres, pour mesurer leur coût.

### Questions à se poser

- **Quelle résolution de carte ?** 2048 ou 4096 : mémoire et bande passante contre netteté.
- **Une carte ou des cascades ?** Une seule si le zoom reste limité ; des cascades si la caméra peut dézoomer loin.
- **Des ombres pour les lumières ponctuelles ?** Chacune demande six rendus (une carte cubique). Probablement non, ou seulement pour une ou deux lumières importantes.
- **Qui projette une ombre ?** Tous les objets, ou seulement les gros (murs, personnages) ?
- **Des ombres « disque »** sous les personnages, très peu chères, en complément ou en solution de repli ?

### Pièges connus

- L'**acné** (motifs moirés sur les surfaces éclairées) et le ***peter-panning*** (ombre détachée de l'objet) : deux défauts opposés, réglés par les biais.
- Le scintillement des bords en mouvement : boîte de la lumière non alignée sur les texels.
- Une convention de coordonnées de texture différente entre Direct3D 12 et Metal lors de l'échantillonnage : ombre décalée sur un seul OS *(SDL_GPU devrait normaliser, à vérifier)*.
- Oublier que le culling de la passe d'ombre utilise le volume **de la lumière**, pas celui de la caméra : un objet hors écran peut projeter une ombre à l'écran.

### Validation

- [ ] L'ombre d'un poteau tombe au bon endroit sur le sol (position vérifiée par calcul).
- [ ] Les bords d'ombre restent stables quand la caméra se déplace.
- [ ] Aucune acné visible aux angles de lumière prévus.
- [ ] Le coût est mesuré (temps CPU, nombre de draws de la passe d'ombre).

---

## 9. Beaucoup d'objets : instanciation et culling

### But

Dessiner des milliers d'objets (sol, murs, décor, créatures) avec peu de draw calls, et sans dessiner ce qui est hors de la vue. C'est l'équivalent 3D du batching du jalon 2.

### Tâches

- [ ] Créer une file de dessin 3D : le jeu enregistre (maillage, matériau, transformation), le moteur trie et regroupe à la fin de la frame, comme pour les sprites.
- [ ] **Instanciation** : un draw call par couple (maillage, matériau), avec les transformations dans un tampon (attributs par instance, `SDL_GPU_VERTEXINPUTRATE_INSTANCE`, ou tampon de stockage lu avec l'indice d'instance).
- [ ] **Frustum culling** sur le CPU, avec les boîtes englobantes transformées dans le monde.
- [ ] Statistiques par passe (principale, ombre) : objets soumis, visibles, dessinés ; draw calls ; triangles. Dans ImGui et dans `--report`.
- [ ] Test de charge 3D : N objets, comme `--sprites N`, avec mesures.
- [ ] Tests unitaires : frustum contre des boîtes (dedans, dehors, à cheval), regroupement.

### Questions à se poser

- **Attributs d'instance ou tampon de stockage ?** Les attributs marchent partout avec un format fixe ; le tampon de stockage est plus souple (données de taille variable, matériaux par instance).
- **Le culling CPU suffit-il ?** Probablement, aux volumes d'un ARPG. Le culling sur GPU et le dessin indirect (`SDL_DrawGPUIndexedPrimitivesIndirect`) sont pour plus tard.
- **Le sol : une instance par case, ou un maillage fusionné par zone ?** C'est la question des blocs statiques du jalon 2, en 3D. Mesurer avant de fusionner.
- **Des niveaux de détail (LOD) ?** Avec une caméra à distance presque constante, probablement inutiles.
- **Décor statique** : reconstruire ses instances à chaque frame, ou les garder dans un tampon fixe ?

### Pièges connus

- Tri par profondeur ou par matériau : pour les opaques, d'avant en arrière limite le recouvrement, par matériau limite les changements d'état. Les deux se contredisent : mesurer.
- Des boîtes englobantes non transformées (rotation, échelle) : objets éliminés à tort sur les bords.
- Agrandir le tampon d'instances pendant une passe : les copies y sont interdites (même règle qu'au jalon 2).

### Validation

- [ ] L'objectif de performance est atteint en Release sur les deux OS (par exemple 10 000 objets, dont 2 000 visibles).
- [ ] Le nombre de draw calls est égal au nombre de groupes (maillage, matériau).
- [ ] Le nombre d'objets dessinés suit ce qui est visible (compteur).

---

## 10. Le 2D par-dessus la 3D

### But

Réutiliser le moteur 2D du jalon 2 pour l'interface et les effets dans un monde 3D.

### Tâches

- [ ] Dessiner les sprites en espace écran après la 3D, sans profondeur (vues de sprites de la partie 2).
- [ ] Placer un élément 2D au-dessus d'un objet 3D (barre de vie, nom, dégâts flottants) avec la projection monde vers écran (partie 3).
- [ ] Dessiner des sprites **dans** le monde (*billboards*, toujours face à la caméra) avec un test de profondeur contre la 3D : un effet derrière un mur est caché.
- [ ] Afficher du texte (`Font`) au-dessus des objets.
- [ ] Garder ImGui au-dessus de tout.

### Questions à se poser

- **Les billboards passent-ils par le `SpriteBatcher`** (en lui ajoutant une position 3D et un test de profondeur) **ou par un système séparé** ?
- **Une barre de vie a-t-elle une taille constante à l'écran** (2D pur) **ou dans le monde** (elle rapetisse au dézoom) ?
- **Quelle échelle pour l'interface** selon la densité de pixels et la taille de la fenêtre ?

### Pièges connus

- Calculer la position écran avec la caméra non interpolée : la barre de vie « tremble » par rapport au modèle.
- Un objet derrière la caméra ou hors de l'écran : sa projection donne des coordonnées absurdes (composante `w` négative).
- Des billboards transparents non triés : artefacts de mélange.

### Validation

- [ ] Les barres de vie restent collées aux créatures pendant les déplacements de caméra.
- [ ] Un billboard derrière un mur est caché par le mur.

---

## 11. Scène de démonstration 3D

### But

Prouver le jalon, comme la partie 9 du jalon 2, mais en 3D.

### Contenu de la scène

- La `TileMap` 100×100 de la démo 2D : le sol et les murs construits en 3D à partir des mêmes données (`walkable`, `opaque`).
- Du décor instancié : quelques milliers d'objets (rochers, arbres, caisses).
- Des créatures : modèles statiques, déplacées par la même logique que la démo 2D (demi-tour devant les murs, graine fixe).
- Une lumière directionnelle avec ombres, et des torches en lumières ponctuelles.
- La caméra isométrique : déplacement, zoom, suivi d'une créature au choix.
- La case sous la souris surlignée ; un clic envoie une créature vers ce point (en ligne droite : le pathfinding viendra plus tard).
- Des barres de vie en 2D au-dessus des créatures.
- Les statistiques et les réglages dans ImGui (FPS, temps par passe, draw calls, triangles, objets visibles, ombres activées ou non).

### Tâches

- [ ] Ajouter la scène à **DEBUG > Tests moteur**, avec ses réglages sur la page de sélection.
- [ ] Garder des options en ligne de commande pour les mesures et les captures (`--demo3d`, `--seed`, `--freeze-after`, `--capture`, `--report`).
- [ ] Ajouter une option qui **fixe la taille de rendu en pixels**, pour comparer les captures Windows et Mac au pixel près (ce qui règle aussi la dette de la démo 2D).
- [ ] Mesurer et noter les chiffres.

### Validation

- [ ] La scène tourne à la cadence de l'écran sur les deux OS avec l'objectif de performance.
- [ ] Deux lancements avec la même graine donnent la même capture.
- [ ] Les captures Windows et Mac sont comparées à taille égale (identiques, ou différences expliquées).

---

## 12. Débogage et performance

### Outils

| Outil | OS | Usage |
|---|---|---|
| RenderDoc | Windows | Capture d'une frame : passes, profondeur, carte d'ombre, instances |
| Débogueur Metal (Xcode) | Mac | Même chose sous Metal, plus les compteurs du GPU |
| Mode debug SDL_GPU | Les deux | Validation des appels |
| ImGui | Les deux | Réglages et statistiques en direct |
| Vues de debug (à écrire) | Les deux | Fil de fer, normales en couleur, profondeur, carte d'ombre à l'écran, boîtes englobantes |

### Tâches

- [ ] Écrire un petit renderer de **lignes de debug** : boîtes englobantes, frustum, rayon de la souris, axes du repère. Utile dès la partie 3.
- [ ] Ajouter des modes de vue : fil de fer (`SDL_GPU_FILLMODE_LINE`), normales, profondeur, carte d'ombre.
- [ ] Nommer les passes et les ressources, pour les retrouver dans les captures (comme au jalon 2).
- [ ] Étendre `--report` avec les statistiques par passe.

### Questions à se poser

- **Comment mesurer le temps GPU ?** SDL_GPU n'offre **pas de requêtes de temps** (seulement des *fences*, `SDL_QueryGPUFence`). On mesure le temps CPU et le temps total de la frame ; le détail du GPU passe par RenderDoc ou Xcode. Un profileur comme **Tracy** (0.13.1 dans vcpkg) aide côté CPU.
- **Quel budget par passe ?** Le fixer après les premières mesures de la scène de démonstration.

### Validation

- [ ] Une capture RenderDoc montre les passes attendues (ombre, 3D, 2D, ImGui), sous leurs noms.
- [ ] Les vues de debug fonctionnent sur les deux OS.

---

## 13. Critères de fin de jalon

Le jalon est terminé quand **tout** ce qui suit est vrai :

- [ ] La frame enchaîne ses passes (ombre, 3D, 2D, ImGui), avec une profondeur au bon format, et les conventions sont consignées.
- [ ] La **caméra isométrique 3D** gère déplacement, zoom, suivi et case sous la souris, correcte partout (Retina compris).
- [ ] Des **modèles glTF** de test s'affichent correctement, et une erreur de chargement nomme le fichier.
- [ ] La **couleur** est calculée en linéaire, les textures ont des mipmaps, sans moiré.
- [ ] L'**éclairage** (directionnel et ponctuel) et les **ombres** sont stables et réglables.
- [ ] L'**instanciation** et le **culling** tiennent l'objectif de performance.
- [ ] Le **2D** (interface, barres de vie, billboards) s'affiche correctement par-dessus la 3D.
- [ ] La **scène de démonstration 3D** tourne sur les deux OS, et ses captures sont comparées à taille égale.
- [ ] Aucun avertissement de compilation, aucun message de la couche de validation du GPU.
- [ ] La logique pure (caméra, frustum, primitives, lecture glTF) a ses tests unitaires.
- [ ] Les décisions de la section [Décisions à consigner](#décisions-à-consigner) sont remplies.
- [ ] La documentation des nouveaux fichiers est ajoutée à [FICHIERS_DU_PROJET.md](FICHIERS_DU_PROJET.md).

---

## 14. Risques principaux

| Risque | Impact | Parade |
|---|---|---|
| Divergences entre Direct3D 12 et Metal (profondeur, coordonnées de texture, sens des faces, registres HLSL) | Bugs sur un seul OS | Tester sur les deux OS à chaque partie ; captures comparées à taille égale |
| Conventions (repère, couleur) décidées tard | Modèles et assets à reprendre | Les fixer en partie 2 et 6, avant les premiers modèles |
| Chaîne Blender vers glTF mal maîtrisée | Modèles mal orientés, mauvaise échelle | Conventions écrites, modèle de référence |
| Explosion des variantes de shaders | Build lent, MSL à exporter à chaque fois | Peu de shaders, des paramètres |
| Oubli de l'export MSL | Le Mac rend une version périmée, sans erreur | L'exporter à chaque changement de shader ; idéalement, un contrôle au build |
| Coût des pixels sur Retina, avec un éclairage PBR payé par pixel, sur un M3 de base | Moins de 60 FPS sur la machine minimale | Résolution de rendu réglable dès la partie 2 ; mesures avec de la marge ; test sur un M3 réel dès que possible |
| Mémoire des textures PBR (3 à 5 textures par matériau) | Mémoire saturée sur un M3 de 8 Go | Compression GPU au plus tard à la fin de ce jalon (partie 6) |
| Pas de mesure du temps GPU dans SDL_GPU | Goulots d'étranglement invisibles | Captures RenderDoc et Xcode régulières |
| Dérive du périmètre (PBR complet, post-traitement, illumination globale) | Jalon sans fin | Noter, repousser au jalon de consolidation ou plus tard |
| L'animation squelettique glisse dans ce jalon | Jalon trop gros | Jalon dédié ; ici, prévoir seulement les attributs de sommet |
| Poids des modèles et textures dans Git | Dépôt lourd, clones lents | Trancher la question de Git LFS en partie 5 |

---

## Décisions à consigner

À remplir au fil du jalon. Chaque ligne correspond à une question posée plus haut.

| Sujet | Décision | Raison |
|---|---|---|
| Objectif de performance 3D et machine minimale visée | **Mac M3** au minimum (M4 préférable), **60 FPS** minimum. Volumes **provisoires** : 300 personnages animés, 3 000 objets visibles, ~1 M de triangles | Tranché le 2026-09-23 ; volumes à réviser après les mesures de la partie 9 |
| Style visuel 3D (*low poly*, stylisé, réaliste) | **Réaliste, PBR** (métal / rugosité, IBL, HDR) | Tranché le 2026-09-23 |
| Échelle (unités, taille d'une case de la `TileMap`) | 1 unité = 1 m ; une case = 1 m (**recommandé**, à confirmer en partie 2) | Convention glTF et Blender ; un personnage tient dans une case |
| Source des modèles de test et licences | **Poly Haven** (CC0) : Wine Barrel 01, Lantern 01, Antique Estoc, Boulder 01, en 1K ; modèles Blender importés plus tard. Auteurs crédités dans « À propos » | Entièrement libre, à jour, PBR complet ; citation non obligatoire mais faite |
| Repère du monde (main, axe vertical) et correspondance grille / monde | Main droite, **Y vers le haut**, 1 unité = 1 m (celui de glTF). La case (i, j) couvre `[i, i+1] × [j, j+1]` sur le sol `y = 0` | Aucune conversion au chargement des modèles ; Blender convertit à l'export |
| Format de profondeur, profondeur inversée ou non | `D32_FLOAT`, sinon `D24_UNORM`, sinon `D16_UNORM`, choisi au démarrage ; profondeur `[0, 1]` (fonctions `*_ZO` de GLM) ; **pas** de profondeur inversée pour l'instant | SDL ne garantit pas D24 et D32 à la fois ; l'inversion se décidera avec la projection (partie 3) |
| Structure des passes de la frame | « scene » (3D en HDR linéaire, profondeur, résolution de rendu ; seulement s'il y a de la 3D) puis « compose » (swapchain : 3D convertie, sprites du monde, interface, ImGui) ; passe d'ombre à ajouter avant (partie 8) | Peu de passes (GPU en tuiles) ; le 2D reste identique ; l'interface ne dépend jamais de la caméra. Remplace les passes « scene » + « overlay » de la partie 2 |
| Anticrénelage (MSAA, post-traitement, aucun) | | |
| Résolution de rendu de la 3D | Réglable de 25 % à 100 % de la fenêtre (`set_render_scale`), agrandie par filtrage linéaire ; interface toujours à la résolution native | Coût du PBR par pixel sur un M3 de base |
| Projection de la caméra (orthographique ou perspective) et angles | **Perspective**, inclinaison **50°**, champ de vision **30°**, orientation 45° (le long de la diagonale de la grille) ; profondeur non inversée (plan proche à 5 % de la distance, `D32_FLOAT` : précision suffisante) | Choisi le 2026-09-23 après comparaison des deux projections dans la scène 3D ; proche de Path of Exile et Diablo, plus adapté au PBR réaliste |
| Picking (sol, objets : rayon ou tampon d'identifiants) | | |
| Format de sommet et taille des indices | `Vertex3D` : position, normale, coordonnées de texture (32 octets) ; tangentes à ajouter en partie 7 ; indices **32 bits** | Les modèles glTF dépassent vite 65 536 sommets ; une seule taille d'indices simplifie tout |
| Tampons (par maillage ou partagé) et transmission des transformations | Un tampon de sommets et un d'indices par maillage ; matrices poussées en uniforms à chaque draw (**provisoire**) | Le plus simple ; à revoir avec l'instanciation (partie 9) |
| Bibliothèque glTF | `cgltf` 1.15 (MIT) | Un seul en-tête C, simple, répandue ; `fastgltf` reste possible si le temps de chargement l'exige |
| glTF à l'exécution ou conversion hors ligne | À l'exécution | Rien de plus à écrire ; un outil hors ligne viendra si les chargements deviennent lents |
| Hiérarchie des nœuds (gardée ou fusionnée) | Aplatie en pièces portant leur matrice monde | Suffit pour des modèles statiques ; la hiérarchie reviendra avec les squelettes (jalon 5) |
| Stockage des modèles (Git LFS ou non) | **Provisoire** : modèles de test hors de Git, retéléchargés par un script ; seul le modèle de référence (8 Ko) est versionné. Git LFS à décider avec les premiers vrais assets du jeu | Pas de poids inutile dans le dépôt en attendant |
| Espace de couleur (linéaire, HDR, tone mapping) et sort du 2D | 3D en linéaire dans une cible `R16G16B16A16_FLOAT` ; exposition, tone mapping **Khronos PBR Neutral** et encodage sRGB dans le shader ; swapchain UNORM ; 2D inchangé par-dessus ; textures de couleur en `_SRGB` avec mipmaps | Conséquence du PBR ; PBR Neutral garde les couleurs des matériaux fidèles à Blender ; le 2D garde exactement son aspect |
| Compression des textures | | |
| Modèle d'éclairage (PBR ou stylisé) | PBR métal / rugosité de glTF (GGX, Smith corrélé, Schlick) ; IBL : diffus en harmoniques sphériques, spéculaire préfiltré sur le CPU au chargement (6 niveaux), terme split-sum analytique (Karis) ; environnement équirectangulaire | Conséquence du style réaliste ; tout le préfiltrage est du C++ testé, sans shader de calcul |
| Nombre de lumières ponctuelles simultanées | 32 par frame, boucle simple dans le shader (données dans le tampon d'uniforms de la frame) | Suffisant pour les tests ; un découpage de l'écran (*forward+*) viendra si les sorts l'exigent |
| Ombres (résolution, cascades, filtrage) | | |
| Instanciation (attributs ou tampon de stockage) et culling | | |
| Sol : instances par case ou maillage fusionné | | |
| Billboards : batch 2D ou système séparé | | |
