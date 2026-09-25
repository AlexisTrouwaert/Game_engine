# Test sur Mac : checklist

Tout ce qui a été fait jusqu'ici a été construit et vérifié **sur Windows seulement**. Ce document liste ce qu'il faut valider sur le Mac, dans l'ordre, avec les valeurs de référence de Windows pour comparer, puis les tests qui restent à faire ensuite.

Retour à la [roadmap](ROADMAP.md). Description du projet : [FICHIERS_DU_PROJET.md](FICHIERS_DU_PROJET.md).

## Comment s'en servir

- Suivre les étapes dans l'ordre : chacune dépend de la précédente.
- Cocher chaque case, et noter dans les tableaux de la section [Résultats](#résultats-à-remplir) ce qu'on observe.
- **En cas d'échec, s'arrêter et noter l'erreur exacte** (la sortie complète du terminal). Un échec à l'étape 2 rend les suivantes inutiles.
- Tout écart avec Windows n'est pas forcément un bug : voir la colonne « À savoir » de chaque étape.

## Ce qui est le plus à risque

Ces points n'ont **jamais** tourné sur Mac. Ce sont eux qui ont le plus de chances de poser problème :

| Risque | Pourquoi |
|---|---|
| **Le build** (CMake, vcpkg, SDL3 en `arm64-osx`) | Jamais lancé sur Mac |
| **Les shaders en MSL** | Générés sous Windows, jamais compilés par Metal. Le point d'entrée y est `main0` et non `main` |
| **Le format de sommet de la teinte** (`UBYTE4_NORM`) | Jamais utilisé avec Metal |
| **La liaison des ressources** (buffer d'uniformes, texture, sampler) | Les indices Metal sont déduits, pas testés |
| **L'écran Retina** | Une fenêtre de 1280×720 *points* fait 2560×1440 *pixels* ; la souris est en points |
| **Le déterminisme des atlas** | Les fichiers produits doivent être identiques à ceux de Windows |
| **Les chemins des ressources** | Exécuté hors d'un paquet `.app` : à adapter au packaging plus tard |

## 0. Ce qu'il faut sur le Mac

- [ ] macOS avec un Mac **Apple Silicon** (arm64). Le projet cible macOS 13.0 au minimum.
- [ ] Les outils de ligne de commande d'Apple : `xcode-select --install`.
- [ ] CMake (3.25 ou plus) et Ninja : `brew install cmake ninja` (ou ceux de CLion).
- [ ] Peut-être `pkg-config` (`brew install pkg-config`) si vcpkg le réclame pour compiler SDL3.
- [ ] Un clone de vcpkg, avec la variable `VCPKG_ROOT` définie, **ou** le vcpkg intégré à CLion. S'il est à son emplacement par défaut (`~/.vcpkg-clion/vcpkg`), le `CMakeLists.txt` racine le trouve tout seul ; sinon, passer `-DCMAKE_TOOLCHAIN_FILE=<chemin>/scripts/buildsystems/vcpkg.cmake` à CMake.
- [ ] CLion (recommandé), ou un terminal.
- [ ] Xcode complet, pour le débogueur Metal (utile seulement en cas de problème de rendu).

## 1. Récupérer le projet

- [ ] Copier ou cloner le projet sur le Mac. **Il n'y a pas encore de dépôt Git** : à créer, ou copier le dossier `moteur/`.
- [ ] Vérifier que ces fichiers sont bien présents, car **le Mac ne peut pas les produire lui-même** :
  - `moteur/shaders/generated/msl/sprite.vert.msl`
  - `moteur/shaders/generated/msl/sprite.frag.msl`
- [ ] Vérifier que `moteur/art/world/` et `moteur/art/test/` (les images sources) sont là, ainsi que `moteur/assets/sprite.png`.

> **À savoir** : les fichiers MSL ont été régénérés après la dernière modification des shaders (la teinte pré-multipliée). Si un shader (`*.hlsl`) est modifié plus tard, il faut relancer sous Windows la cible `export_msl_shaders` avant de tester sur Mac.

## 2. Configurer et compiler

```bash
cd moteur
cmake --preset macos-debug
cmake --build --preset macos-debug
```

- [ ] La configuration réussit. Le premier passage est **long** : vcpkg compile SDL3, GLM, etc.
- [ ] La compilation réussit **sans avertissement**.
- [ ] Les trois cibles sont produites : `moteur_tests`, `atlas_packer` et `bac_a_sable`.
- [ ] La commande de compilation affiche l'étape **« Copying assets »** et deux étapes **« Packing atlas »** (`world` puis `test`).
- [ ] Le dossier `build/macos-debug/apps/bac_a_sable/` contient `shaders/sprite.vert.msl`, `shaders/sprite.frag.msl`, et `assets/` avec `world.json`, `world_0.png`, `test.json`, `test_0.png` et `sprite.png`.

> **À savoir** : sur Mac, le projet ne cherche **pas** `sdl3-shadercross` (il n'est installé que sous Windows). Si CMake s'arrête en disant qu'un fichier MSL manque, c'est le point 1.

Si la configuration échoue à cause de vcpkg, noter le nom du paquet et l'erreur : c'est probablement un outil système manquant.

## 3. Tests unitaires

```bash
ctest --test-dir build/macos-debug --output-on-failure
```

- [ ] **Les 114 cas passent** (le benchmark est ignoré par défaut : « 1 skipped »).

> **À savoir** : ces tests couvrent surtout de la logique pure, donc ils doivent passer partout. **Un échec ici est significatif** : il indiquerait une vraie différence de comportement entre Windows et macOS (flottants, tri, arrondis). Les plus sensibles sont ceux de l'empaquetage d'atlas, qui vérifient un résultat identique quel que soit l'ordre des entrées.

## 4. Démarrage du bac à sable

```bash
build/macos-debug/apps/bac_a_sable/bac_a_sable --run-seconds 3 --report
```

- [ ] Une fenêtre s'ouvre, sans erreur dans le terminal.
- [ ] La ligne `GPU:` indique **`backend=metal`** et le nom de la carte graphique du Mac.
- [ ] Le programme se termine seul après 3 secondes, avec le code de sortie 0.
- [ ] Le titre de la fenêtre affiche des FPS, cohérents avec la fréquence de l'écran (VSync).
- [ ] Aucun message d'erreur ou de validation Metal.

Si le programme s'arrête avec `fatal: Shader ... not found` ou `SDL_CreateGPUShader failed`, noter le message : c'est le MSL ou son point d'entrée (`main0`).

> **À savoir (partie 10)** : depuis cette partie, les ressources GPU (buffers, textures, pipeline) sont **nommées**, ce qui les rend lisibles dans le débogueur Metal de Xcode (`sprite.vertices`, `sprite.indices`, `sprite pipeline`, `sprite.sampler`, les pages d'atlas par leur nom de fichier). Aussi, une mauvaise utilisation de l'API SDL_GPU (par exemple une ressource non liée avant un dessin) déclenche une **assertion SDL qui bloque le programme** avec une invite, exactement comme les assertions du CRT déjà rencontrées : à lancer avec un délai et à tuer si besoin, pas d'inquiétude si ça arrive une fois pendant les essais.

## 5. Rendu du sprite (jalon 1 et partie 3)

```bash
build/macos-debug/apps/bac_a_sable/bac_a_sable --still --run-seconds 20
```

À regarder à l'œil :

- [ ] Un disque de **quatre quadrants** au centre de la fenêtre : rouge en haut à gauche, vert en haut à droite, bleu en bas à gauche, jaune en bas à droite. **Si les couleurs sont inversées ou l'image est retournée, c'est un bug d'orientation.**
- [ ] Un contour noir et un halo semi-transparent, sur un fond bleu-gris sombre.
- [ ] Les bords sont **nets**, sans flou (filtrage `NEAREST`).
- [ ] Redimensionner la fenêtre ne déforme pas le sprite.

Comparaisons de mise en page (chacune doit donner **la même image** qu'avec la première commande) :

```bash
bac_a_sable --still --run-seconds 20 --no-batching
```

- [ ] `--no-batching` : image identique.

Avec du mouvement et de la charge :

```bash
bac_a_sable --run-seconds 5 --report --sprites 3000
bac_a_sable --run-seconds 5 --report --sprites 3000 --depth
```

- [ ] Des centaines de petits sprites colorés rebondissent, sans erreur.
- [ ] `--depth` : le grand sprite central reste **entier, au-dessus de tous les petits**, alors qu'il est enregistré en premier.
- [ ] La ligne `perf:` indique **1 draw call** pour 3 001 sprites.
- [ ] Avec `--sprites 40000`, le message **« Sprite buffer grown from 16384 to 65536 sprites »** apparaît, sans erreur, et la ligne `perf:` indique 3 draw calls.

## 6. Caméra et scène isométrique (partie 4)

```bash
bac_a_sable --iso --run-seconds 60
```

- [ ] Une carte de losanges verts (damier) s'affiche, avec un petit repère au centre.
- [ ] **Flèches ou ZQSD / WASD** déplacent la caméra ; la vitesse à l'écran ne dépend pas du zoom.
- [ ] **La molette** (ou le trackpad) change le zoom par paliers de 1 à 8. *Sur Mac, le défilement « naturel » peut inverser le sens : ce n'est pas un bug.*
- [ ] Le losange **sous la souris est surligné en jaune**, et c'est bien celui qui est sous le curseur, y compris **près des bords de la fenêtre**.
- [ ] **Aucune couture** : pas de fine ligne de fond entre les losanges, à aucun zoom, même en déplaçant la caméra.
- [ ] Redimensionner la fenêtre garde la carte centrée et le surlignage correct.

Vérification automatique du picking (sans souris réelle). Le centre de la fenêtre doit toujours donner la tuile `30 30`, **quelle que soit la taille de l'écran** :

```bash
bac_a_sable --iso --no-input --run-seconds 3 --mouse <largeur/2> <hauteur/2>
```

où la largeur et la hauteur sont celles **en pixels** (sur un Mac Retina, une fenêtre de 1280×720 points fait **2560×1440 pixels**, donc `--mouse 1280 720`).

- [ ] La dernière ligne affichée est `hovered tile: 30 30`.

> **À savoir : Retina.** L'unité du monde est le **pixel physique**. Sur un écran Retina, les sprites et les tuiles paraissent donc **deux fois plus petits** qu'en points : c'est le comportement décidé, pas un bug. Les valeurs de tuile de la documentation (`9 28` pour `--mouse 10 10`, etc.) supposent une fenêtre de **1280×720 pixels** et ne s'appliquent pas telles quelles sur Retina.

**Test propre à Mac : la souris réelle.** La conversion points → pixels de `Application::to_pixels()` n'a été testée qu'en test unitaire.

- [ ] Sans `--mouse`, promener la souris dans la fenêtre : le losange surligné **suit exactement le curseur**, sans décalage (un décalage proportionnel à la distance au coin haut-gauche indiquerait un facteur 2 oublié).

## 7. Atlas de sprites (partie 5)

```bash
bac_a_sable --atlas --run-seconds 60
```

- [ ] Une grille de formes s'affiche : orbes, barres, carré, anneau et huit personnages de marche, chacun avec un petit **carré rouge à son pivot**.
- [ ] En bas, le personnage `walk_03` apparaît **quatre fois** : normal, retourné, deux fois plus grand, retourné trois fois plus grand.
- [ ] Le personnage a un **bras gauche long et vert, un bras droit court et rouge**. Retourné, ces bras sont **inversés**, et les pieds restent alignés avec le carré rouge.
- [ ] Les bords des sprites n'ont **ni halo sombre ni ligne parasite**.
- [ ] L'ombre sous les pieds est semi-transparente (le fond se voit à travers).

### Déterminisme : les fichiers produits doivent être identiques à ceux de Windows

C'est **le test décisif** de la partie 5. Sur le Mac :

```bash
cd build/macos-debug/apps/bac_a_sable/assets
shasum -a 256 test.json test_0.png world.json world_0.png
```

Valeurs de référence relevées sous Windows (12 premiers caractères du SHA-256, art de test inchangé) :

| Fichier | SHA-256 (début) |
|---|---|
| `test.json` | `636b61cca266` |
| `test_0.png` | `c02006e8b7db` |
| `world.json`, `world_0.png` | **non relevés** : à calculer sous Windows pour comparer (`Get-FileHash <fichier> -Algorithm SHA256`) |

- [ ] `test.json` : mêmes octets que sous Windows.
- [ ] `test_0.png` : mêmes octets que sous Windows.
- [ ] `world.json` et `world_0.png` : mêmes octets que sous Windows.

> **À savoir** : si `test.json` diffère, comparer les deux fichiers avec `diff` : l'écart montrera quelle image a été placée ailleurs. Une différence **seulement dans le PNG** (et pas dans le JSON) viendrait de l'encodeur PNG. Ces valeurs de référence ne valent que si les images de `art/test/` n'ont pas changé.

Erreurs de chargement (optionnel) : abîmer un fichier dans une **copie** du dossier `assets/` (par exemple un JSON tronqué) doit donner un message `fatal: Atlas '...' is not valid: ...` avec le nom du fichier.

## 8. Texte (partie 8)

```bash
bac_a_sable --text --run-seconds 30
```

- [ ] Une phrase française s'affiche en haut à gauche, avec ses accents et ses guillemets lisibles : `Où étaient les œufs d'été ? « Ici. »`.
- [ ] En dessous, un paragraphe se coupe automatiquement en plusieurs lignes (retour à la ligne).
- [ ] Trois lignes « Gauche », « Centre », « Droite » apparaissent alignées différemment dans la même largeur.
- [ ] Un compteur de FPS en haut à droite se met à jour environ une fois par seconde.
- [ ] Aucun bord noir ou blanc parasite autour des lettres (glyphes mal gérés en alpha).
- [ ] La console affiche deux lignes `measured sentence: ...` et `measured paragraph: ...` au démarrage.

> **À savoir** : cette partie ne modifie aucun shader (le texte réutilise le pipeline de sprites existant) : c'est donc l'une des parties les moins risquées sur Mac. Le principal risque propre au texte est indépendant de Metal : le fichier de police est une police **variable**, et `stb_truetype` ignore ses axes de variation pour ne lire que le tracé par défaut. Si le texte paraissait exagérément fin ou épais sur Mac par rapport à Windows, ce serait le premier point à vérifier — mais rien ne devrait différer, puisque `stb_truetype` (comme tout le reste) est compilé depuis les mêmes sources sur les deux OS.

## 9. Performance

Sur un build **Release** (le Debug active la validation du GPU et fausse tout) :

```bash
cmake --preset macos-release
cmake --build --preset macos-release
build/macos-release/apps/bac_a_sable/bac_a_sable --run-seconds 6 --report --no-vsync --sprites 10000
```

Références relevées sous Windows (Direct3D 12, RTX 4070 Ti SUPER, temps en ms, CPU hors attente de l'écran) :

| Sprites | CPU moyen | CPU p99 | Draw calls |
|---|---|---|---|
| 10 001 | 0,43 | 0,79 | 1 |
| 40 001 | 1,26 | 1,78 | 3 |
| 80 001 (VSync, 164 Hz) | 2,72 | 3,80 | 5 |

- [ ] Relever les mêmes lignes sur le Mac (`--sprites 10000`, `40000`, `80000`).
- [ ] **Comparer avec `--no-batching`** : la question ouverte depuis la partie 2 est de savoir si un draw call coûte plus cher sous Metal (environ 15 à 17 ns sous Direct3D 12).

> **À savoir** : cette mesure tranche l'objectif de performance, laissé « provisoire » dans le tableau des décisions du jalon 2.

## 10. Si quelque chose échoue : où regarder

| Symptôme | Piste |
|---|---|
| `SDL_CreateGPUDevice failed` | Le Mac ne prend pas en charge Metal avec SDL_GPU, ou SDL3 n'a pas été compilé avec Metal |
| `Shader '...' not found` | Les fichiers MSL manquent (point 1) ou n'ont pas été copiés à côté de l'exécutable |
| `SDL_CreateGPUShader failed` | Le MSL est refusé : vérifier le point d'entrée `main0` et lire le message de Metal |
| Écran noir, aucune erreur | Liaison des ressources (uniformes, texture, sampler) ou format de sommet : faire une capture d'image avec le **débogueur Metal de Xcode** |
| Sprite retourné ou couleurs inversées | Origine des coordonnées de texture ou de la projection différente sous Metal |
| Surlignage décalé de la souris | Conversion points → pixels (`Application::to_pixels`) |
| Atlas différents de Windows | Comparer avec `diff` ; suspects : l'ordre d'empaquetage, l'encodeur PNG |
| Tests unitaires en échec | Différence de flottants ou de tri entre plateformes : noter quel test |

Pour déboguer un rendu : `SDL_GPU_DRIVER=metal` force le backend (sans effet sur Mac, où il n'y en a qu'un), et le mode debug de SDL_GPU est actif dans un build Debug.

## Résultats à remplir

Relevés le 2026-09-23 sur un MacBook **Apple M4 Pro**, macOS 27, écran Retina 120 Hz, SDL 3.4.16. Les images ont été vérifiées sur des captures `--capture` (2560×1440 pixels).

| Étape | Résultat | Remarques |
|---|---|---|
| 2. Compilation | ✅ | Debug et Release, 0 avertissement. Il a fallu un triplet vcpkg local (`triplets/arm64-osx.cmake`) pour que SDL3 cible macOS 13.0 : sans lui, 582 avertissements du linker (« built for newer macOS version »). Le vcpkg de CLion est trouvé automatiquement |
| 3. Tests unitaires | ✅ | 114 passés, 1 ignoré, en Debug et en Release |
| 4. Démarrage et backend | ✅ | `backend=metal, device=Apple M4 Pro`, sortie 0, ~117 FPS (VSync 120 Hz), aucune erreur Metal |
| 5. Sprite, batching, tri | ✅ | Quadrants dans le bon ordre, bords nets. `--no-batching` : capture **identique octet pour octet**. 3 001 sprites = 1 draw call ; `--depth` : grand sprite entier au-dessus. 40 000 : « grown from 16384 to 65536 », 3 draw calls |
| 6. Caméra, picking, souris Retina | ✅ en partie | `--mouse 1280 720` → `hovered tile: 30 30`. Aucune couture (zoom 1 et 3). **Reste à faire à la main** : clavier, molette, souris réelle, redimensionnement |
| 7. Atlas et déterminisme | ✅ | `test.json` `636b61cca266` et `test_0.png` `c02006e8b7db` : **identiques à Windows**. `world.json` `dca6ef301c52` et `world_0.png` `09b20cd05771` : **identiques à Windows** aussi. Bras et retournement corrects. JSON tronqué → `fatal: Atlas '...' is not valid: ...` |
| 8. Texte | ✅ | Accents, `œ` et guillemets corrects, retour à la ligne, trois alignements ; `measured sentence: 266.36 20`, `measured paragraph: 420 80 (4 lines)` |
| 9. Performance | ✅ | Voir ci-dessous |

Performance en Release (Metal, M4 Pro, temps CPU en ms) :

| Sprites | CPU moyen | CPU p99 | Draw calls | Windows (moyen) |
|---|---|---|---|---|
| 10 001 | 0,46 | 1,05 | 1 | 0,43 |
| 40 001 | 1,34 | 1,94 | 3 | 1,26 |
| 80 001 (VSync, 120 Hz) | 2,66 | 3,65 | 5 | 2,72 |
| 10 001, `--no-batching` | 1,24 | 1,96 | 10 001 | |
| 40 001, `--no-batching` | 2,16 | 3,01 | 40 001 | |

Coût d'un draw call sous Metal, estimé sur la phase `submit` : environ **30 à 80 ns** (27 ns à 40 000 sprites, 80 ns à 10 000 ; mesure bruitée), contre 15 à 17 ns sous Direct3D 12. Le batching reste donc indispensable, mais les chiffres avec batching sont équivalents à ceux de Windows.

## Jalon 3 : Rendu 3D

Tout le jalon 3 a été construit et vérifié **sous Windows seulement** (RTX 4070 Ti SUPER, écran 165 Hz). Ces points ferment le jalon ; la machine visée est un **M3** (M4 préférable), à **60 FPS** minimum. Avant tout : `python3 tools/models/fetch_test_models.py` (les modèles de test ne sont pas dans Git) et vérifier que `shaders/generated/msl/` contient `fxaa.frag.msl`, `billboard.*`, `debug_line.*`, `depth_view.frag.msl`, `shadow*`, `point_shadow.*` (exportés sous Windows).

**Démarrage et formats**

- [ ] Tests unitaires : **198** cas, en Debug et en Release.
- [ ] La ligne `GPU:` : `backend=metal`, et relever `depth=` (Windows : `D32_FLOAT`), `bc7=` et `bc5=`. **Les deux doivent valoir `yes`** : c'est l'hypothèse de la décision sur la compression des textures (BC7 / BC5 sur les deux OS). Si l'un vaut `no`, la décision est à revoir (ASTC).
- [ ] Build Debug : aucun avertissement de compilation, aucune erreur Metal dans la console dans les scènes ci-dessous.

**Rendu**

- [ ] Scène « Rendu 3D » (`--3d`) : modèles Poly Haven et modèle de référence à leur taille, avec leurs textures ; sphère éclairée (dégradé et reflet aux mêmes endroits que sous Windows) ; dégradé de gris identique ; ombres du soleil sans acné ni scintillement (`--sun 235 40`).
- [ ] Caméra sur Retina : la case sous la souris est juste aux quatre coins, à plusieurs zooms ; suivi (F) et déplacement.
- [ ] Nuit et torches : `--3d --night --point-shadows 8 --billboards 200` : ombres des torches, billboards cachés par les maillages, pas d'erreur.
- [ ] Vues de debug : `--view wireframe|normals|albedo|distance`, `--show-bounds --show-lights --show-shadow-frustum`, et surtout **`--debug-texture sun`** et **`--debug-texture points`** (le shader lit la profondeur en `texture2d<float>` sous Metal : risque propre au Mac).
- [ ] Anticrénelage : `--aa fxaa`, `--aa msaa2`, `--aa msaa4`, et la liste du panneau « Rendu » changée pendant que la démo tourne : bords lisses, aucun mode grisé, aucune erreur.
- [ ] Captures du Xcode (débogueur Metal) : les groupes `shadow`, `point shadows`, `scene`, `tonemap` (FXAA), `compose`.

**Démo 3D et performance** (Release, `--no-vsync --report`)

- [ ] `--demo3d` tient la cadence de l'écran (Windows : 165 Hz).
- [ ] `--demo3d --creatures 1000 --decor 10000` : relever le temps CPU (Windows : 2,4 ms) et les FPS ; au moins 60.
- [ ] `--3d --meshes 10000` : relever le temps CPU (Windows : 1,2 ms pour 10 000 objets).
- [ ] 4 torches ombrées : coût relevé (Windows : +0,04 ms de CPU, environ 5 % de FPS).
- [ ] `--gpu-timing` pour chaque `--aa` (Windows, total GPU : 2,04 / 2,45 / 2,30 / 2,40 ms pour aucun / FXAA / MSAA 2× / MSAA 4×).
- [ ] Si les 60 FPS ne tiennent pas : essayer « Résolution de rendu » à 0,75 puis 0,5 et relever.

**Capture comparée à taille égale**

```
bac_a_sable --demo3d --seed 42 --freeze-after 60 --no-input --run-seconds 3 --pixel-size 1280 720 --capture demo3d_mac.png
```

- [ ] Deux lancements donnent le même fichier.
- [ ] Comparer à la capture Windows (`5fef643792bf`, 1280×720) : identique, ou différences expliquées (arrondis des GPU, filtrage). *`--run-seconds` est indispensable : `--freeze-after` fige la scène sans quitter.*

## Jalon 4 : Systèmes de base

Construit et vérifié sous Windows. À faire sur le Mac au fil des parties.

**Partie 2 : gestionnaire d'assets**

- [ ] Tests unitaires : **232** cas, en Debug et en Release (dont `check_asset_case`, qui crée des fichiers dans le dossier temporaire, et `test_ktx_texture`, qui encode des KTX2 avec libktx).
- [ ] EnTT et efsw se compilent (vcpkg), sans avertissement ; « À propos » les liste.
- [ ] Démo 3D : capture comparée à celle de Windows (`--demo3d --seed 42 --freeze-after 60 --no-input --run-seconds 3 --pixel-size 1280 720 --capture`). *Depuis la partie 3, la référence Windows est `d41d301beead` (textures KTX2) ; `--no-ktx2` donne `6fb7a00fa461` (JPEG). L'ancienne `5fef643792bf` du jalon 3 ne vaut plus : le shader recalcule z des normales.*
- [ ] Rechargement à chaud (FSEvents) : la démo 3D lancée depuis le build, remplacer `assets/models/polyhaven/wine_barrel_01/textures/wine_barrel_01_diff_1k.jpg` **dans les sources** par une autre image : le log dit `Assets: texture '...' reloaded` et les tonneaux changent sans relancer. Remettre l'image d'origine.
- [ ] Casse : APFS ne distingue pas la casse par défaut, comme Windows. Le test « check_asset_case refuses a wrong case » doit passer (il vérifie justement qu'une casse fausse est refusée malgré le système de fichiers).
- [ ] Fenêtre DEBUG > Assets : les compteurs bougent en changeant de test, « Libérer les assets inutilisés » fait baisser la mémoire.

**Partie 3 : textures KTX2**

- [ ] vcpkg construit `ktx` avec l'outil `ktx` (fonctionnalité `tools`) sur arm64-osx.
- [ ] `python3 tools/models/fetch_test_models.py` convertit les textures (12 `.ktx2` sous `assets/models/polyhaven/`), ou `python3 tools/textures/convert_gltf_textures.py` après un build. Les `.ktx2` produits sur Mac et sur Windows devraient être identiques (même version de l'outil) : comparer leurs hachages.
- [ ] La ligne `GPU:` dit `bc7=yes, bc5=yes` (hypothèse de la décision du jalon 3).
- [ ] `--demo3d --report` : `Assets: texture 3 in memory, 4.00 MB on the GPU` ; avec `--no-bc` : 16,00 Mo.
- [ ] Scène « Rendu 3D » et `--blender-compare` : modèles identiques à l'œil avec et sans `--no-ktx2` ; aucune erreur Metal en Debug (niveaux BC de 2×2 et 1×1 compris).
- [ ] Le MSL de `mesh.frag` a été réexporté sous Windows (z des normales recalculé) : vérifier que `shaders/generated/msl/mesh.frag.msl` est à jour après le pull.

**Partie 4 : entrées**

- [ ] Capture de la démo 3D : `--no-input` donnait `250dd83ef6bc` sous Windows ; `--replay-input tests/data/demo3d_replay.json` (chemin depuis la racine du dépôt) donnait `858706ad9b36`. *Remplacés par ceux de la partie 5 ci-dessous.* Comparer avec `--pixel-size 1280 720`.
- [ ] Clavier du Mac (AZERTY ou QWERTY) : la ligne d'aide nomme les compétences comme sur les touches (A Z E R T sur un AZERTY, Q W E R T sur un QWERTY), et elles marchent.
- [ ] Profil « zqsd » (panneau « Commandes ») : Z Q S D déplacent la créature ; le choix survit à un redémarrage (fichier dans `~/Library/Application Support/moteur/bac_a_sable/`).
- [ ] Trackpad : la molette (défilement à deux doigts) zoome, sans à-coups excessifs ; le clic droit (deux doigts) lance la compétence.
- [ ] Manette (même modèle que sous Windows si possible) : branchée pendant la démo, reconnue (log `Input: gamepad ... connected`), stick gauche, boutons, débranchée sans plantage.

**Partie 5 : ECS (EnTT)**

- [ ] Tests unitaires : **242** cas, en Debug et en Release (`test_world` : registre sans GPU).
- [ ] Capture de la démo 3D : `--no-input` donnait `331290226414` sous Windows, `--replay-input tests/data/demo3d_replay.json` donnait `debc4a3b811c` (*remplacés par ceux de la partie 6 ci-dessous*) (même commande que la partie 4, avec `--pixel-size 1280 720`). Même hachage attendu sur le Mac : l'ordre de parcours d'EnTT ne dépend que de l'ordre des créations.
- [ ] Temps de collecte (`world collection` en fin de ligne de commande) avec `--creatures 1000 --decor 10000 --run-seconds 8` : Windows 0,67 ms (Release) ; le noter ici.

**Partie 6 : scènes et états de jeu**

- [ ] Tests unitaires : **255** cas, en Debug et en Release (`test_state_stack`).
- [ ] Capture de la démo 3D : `--no-input` donne `fdc076d9c3ae`, `--replay-input tests/data/demo3d_replay.json` donne `952477744ffb` sous Windows (même commande, avec `--pixel-size 1280 720`). Seule la ligne de statistiques a changé : la capture attend que ses chiffres viennent d'une image gelée, et le survol gelé suit le pointeur du dernier tick. Lancer chaque commande trois fois : même hachage.
- [ ] `--states-cycles 100` : « assets and GPU objects stable », « 0 pauses where the world moved », et la mémoire du processus sans tendance (dernière ligne : écart entre les minimums du début et de la fin ; Windows : 192 à 197 Mo). Le noter ici. En Debug, `--states-cycles 5` sans message de la couche de validation Metal.
- [ ] Menu DEBUG > Tests moteur > « États de jeu » : Jouer, Échap met en pause (le monde reste visible, figé, assombri), Échap reprend, « Retour au titre », « Quitter le test ». Avec une manette : Start met en pause et reprend.

**Partie 7 : audio**

Avant : `python3 tools/audio/fetch_test_sounds.py` (sons de test, hors de Git).

- [ ] Tests unitaires : **268** cas, en Debug et en Release (`test_audio` mixe sans périphérique : il ne dépend pas de la carte son).
- [ ] miniaudio se compile et se lie (CoreAudio, AudioToolbox, CoreFoundation), sans avertissement du projet ; « À propos » liste miniaudio et la section « Sons et musiques de test ».
- [ ] Le log de démarrage dit `Audio: <sortie>, <Hz>, <canaux>, <ms> of buffer` : relever le tampon (Windows : 30 ms).
- [ ] `--audio-burst --run-seconds 3` : au plus 21 voix, `output peak 0.9` (Windows : mélange 2,1 à 2,6, limiteur ×0,35 à 0,42).
- [ ] Menu « Audio », à l'oreille : le feu tourne de gauche à droite, plus faible au loin, sans craquement ; les pas ; les clics ; les musiques du titre et du jeu en fondu enchaîné ; l'ambiance ; les volumes et pauses par groupe.
- [ ] « États de jeu » : musique du titre, puis du jeu ; la pause la baisse ; « Retour au titre » la remplace en fondu.
- [ ] Casque (ou AirPods) branché puis débranché pendant la scène « Audio » : pas de plantage, le son passe sur les haut-parleurs (log `Audio: now playing on ...` ou `started again`).
- [ ] La fenêtre perd le focus : le son se coupe, et revient au retour.

**Partie 8 : outils de debug (ImGui)**

- [ ] Tests unitaires : **272** cas, en Debug et en Release (`test_debug_tools`).
- [ ] `--menu-test 6` (démo 3D) : DEBUG > Inspecteur d'entités montre la créature 0 et ses composants (Transform, Marcheur, Santé...), encadrée en jaune dans le monde ; clic du milieu sur une autre créature : l'inspecteur la suit ; changer sa position la déplace, et le panneau dit « Modifié à la main ».
- [ ] Les fenêtres Assets, Entrées, Audio, États de jeu (`--menu-test 8` pour les états) s'ouvrent et se ferment ; au relancement, les mêmes sont ouvertes, aux mêmes places (`imgui.ini` dans `~/Library/Application Support/moteur/bac_a_sable/`).
- [ ] Assets : « Recharger » sur une texture de tonneau écrit `reloaded` dans le log, sans erreur Metal en Debug.
- [ ] Captures de la démo 3D inchangées (`fdc076d9c3ae`, `952477744ffb`), avec le profil de touches par défaut : un profil « zqsd » enregistré change la ligne d'aide, donc la capture.

**Partie 9 : tranche jouable**

- [ ] Capture de référence : `--states --replay-input tests/data/slice_replay.json --freeze-after 340 --run-seconds 9 --pixel-size 1280 720 --capture slice.png` donne `dbf96994a52d` sous Windows (profil de touches par défaut) ; deux lancements, même hachage.
- [ ] Jouer la tranche du titre au retour au titre : menus aux flèches et Entrée, puis à la manette (croix, A) ; le héros au clic, au trackpad, à ZQSD et au stick ; clic sur une créature proche ou A : « Touché ! » et un impact ; pas, ambiance, musiques ; Échap ou Start : pause (musique plus basse, pas et ambiance arrêtés), « Reprendre », « Retour au titre ».
- [ ] `Slice: loaded in ... ms` dans le log (Windows : 440 à 520 ms en Release) et `--report` sans vsync (Windows : 1,22 ms de CPU par frame pour la tranche rejouée, 1,36 ms pour la démo 3D) : les noter ici.
- [ ] `--states-cycles 100` : stable, comme en partie 6 (la tranche charge maintenant des sons en plus).
- [ ] En Debug, la tranche rejouée sans message de la couche de validation Metal.

## Après le Mac : les tests qui restent à faire

- [x] **Casque débranché sous Windows** (jalon 4, partie 7) *(fait le 2026-09-25 : pas de plantage)* : débrancher et rebrancher le casque pendant la scène « Audio » ; pas de plantage, le son suit (log `Audio: now playing on ...` ou `sound device started again`).
- [x] **Écoute** de la scène « Audio » et de la tranche jouable (jalon 4, parties 7 et 9) : placement gauche / droite et distance, fondus, pause *(fait sous Windows le 2026-09-25)*.
- [ ] **La tranche jouable à la manette** (jalon 4, partie 9), du titre au retour au titre : menus à la croix et A, déplacement au stick, A pour frapper, Start pour la pause. *Au clavier-souris : fait le 2026-09-25.*
- [x] **L'inspecteur à la main** (jalon 4, partie 8) : clic du milieu sur une créature de la démo 3D, puis changer sa position *(fait le 2026-09-25)*.

Ces points ne sont **pas vérifiés non plus sous Windows**. Ils ne dépendent pas du Mac.

| À vérifier | Pourquoi ce n'est pas encore fait |
|---|---|
| **La fluidité et le scintillement** du mouvement de la caméra et des sprites | Mes captures d'écran mesurent des positions, pas le rendu perçu : à juger à l'œil |
| **Le temps GPU** | Seul le temps CPU est mesuré. RenderDoc (Windows, fait) et le débogueur Metal (Mac) le permettraient |
| **Le débogueur Metal (Xcode)** | Nécessite le Mac ; côté Windows, une capture RenderDoc a confirmé le nombre de lots attendu (partie 10) |
| **Le filtrage linéaire** et les **bavures entre sprites d'un atlas** | Le moteur n'utilise que `NEAREST`, qui ne peut pas faire baver ; la marge des atlas n'est testée qu'en test unitaire |
| **Les chemins avec accents** dans `atlas_packer` | Testé avec des chemins ASCII seulement |
| **Le texte enrichi, le contour et l'ombre portée** | Explicitement repoussés en partie 8 |
| **Une police plus grande ou un très long texte** | Testé seulement avec 236 glyphes et des textes courts |
| **La taille des atlas PNG** | L'encodeur de stb compresse moins bien que les outils habituels, sans importance sur les volumes actuels |
| **Deux écrans de densités différentes** | Un seul écran utilisé |
| **Le packaging en `.app`** | Le chemin des ressources est relatif à l'exécutable ; dans un paquet Mac, il sera différent |

### Une amélioration qui rendrait tous ces contrôles portables

Mes vérifications visuelles passent aujourd'hui par des scripts Windows qui lisent des pixels à l'écran : **ils ne fonctionnent pas sur Mac**. Sur Mac, les contrôles ci-dessus se font à l'œil. Une **capture d'image intégrée au moteur** (lire la texture du swapchain vers le CPU et l'écrire en PNG, idée déjà décrite dans la [partie 9 du jalon 2](JALON_2_RENDU_2D.md#9-scène-de-démonstration)) permettrait de refaire les mêmes comparaisons pixel par pixel sur les deux OS, et de comparer directement les images de Windows et de Mac. C'est la meilleure façon de fermer cette dette.

## Ce qui reste dans le jalon 2 (rappel)

Le jalon 1 et toutes les parties du jalon 2 sont faites, les parties 6 (animations) et 7 (carte de tuiles) en **version réduite**, le jeu étant en 3D ([JALON_2_RENDU_2D.md](JALON_2_RENDU_2D.md)). Elles ont été ajoutées **après** les relevés ci-dessus : au prochain passage sur le Mac, vérifier :

- [ ] Les tests unitaires : **135** cas (dont 21 nouveaux : `test_animation.cpp`, `test_tilemap.cpp`), en Debug et en Release.
- [ ] `--demo` : les créatures marchent (les jambes bougent, chacune à son rythme), sont retournées quand elles vont vers la gauche, font demi-tour devant les murs, et le compteur « pas (événements d'animation) » augmente.
- [ ] **Menu** : lancer `bac_a_sable` sans argument. vcpkg compile d'abord Dear ImGui (nouvelle dépendance). Vérifier : barre DEBUG > Tests moteur (sous-menu), accents lisibles et nets sur Retina, chaque scène se lance et s'arrête (bouton et Échap), **Accueil** revient à l'accueil, aucune erreur Metal dans la console.
- [ ] **Modèles 3D** : `python3 tools/models/fetch_test_models.py`, puis la scène « Rendu 3D » : le modèle de référence et les quatre modèles Poly Haven s'affichent avec leurs textures, à leur taille ; les shaders `mesh` (PBR), `tonemap` et `shadow` utilisent le MSL exporté ; avec `--sun 235 40`, les ombres partent de la base des objets, sans acné, et ne scintillent pas quand la caméra bouge ; relever le format de la carte d'ombre (D32 ou D16) ; la grille de sphères et les dômes en relief du modèle de référence ont le même aspect que sous Windows ; couleurs comparables à Windows ; le curseur « Résolution de rendu » du panneau change la netteté de la 3D mais pas celle du texte (c'est lui qui doit donner de la marge sur un M3). **Aide > À propos** liste les licences.
- [ ] **Anticrénelage** (jalon 3, partie 12 bis) : dans la démo 3D, `--aa fxaa`, `--aa msaa2` et `--aa msaa4` (et la liste « Anticrénelage » du panneau « Rendu », à changer pendant que la scène tourne) : les bords des murs deviennent lisses, aucune erreur Metal ; aucun mode grisé dans la liste. Relever les temps avec `--gpu-timing --no-vsync --report` pour chaque mode (Windows : total 2,04 / 2,45 / 2,30 / 2,40 ms).
- [ ] `--demo --seed 42 --freeze-after 30 --no-input --capture demo.png` : deux lancements donnent le même fichier. Sous Windows (1280×720) : `bd91e8b2c616`. Le Mac capture en 2560×1440, donc les deux fichiers ne peuvent pas être identiques ; comparer à l'œil.

Ensuite, place au jalon 3D.
