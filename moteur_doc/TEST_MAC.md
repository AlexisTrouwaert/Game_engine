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
- [ ] Un clone de vcpkg, avec la variable `VCPKG_ROOT` définie, **ou** le vcpkg intégré à CLion (dans ce cas, passer `-DCMAKE_TOOLCHAIN_FILE=<chemin>/scripts/buildsystems/vcpkg.cmake` à CMake).
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

| Étape | Résultat | Remarques |
|---|---|---|
| 2. Compilation | | |
| 3. Tests unitaires | | |
| 4. Démarrage et backend | | |
| 5. Sprite, batching, tri | | |
| 6. Caméra, picking, souris Retina | | |
| 7. Atlas et déterminisme | | |
| 8. Texte | | |
| 9. Performance | | |

## Après le Mac : les tests qui restent à faire

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

Le jalon 1 et les parties 2, 3, 4, 5, 8 et 10 du jalon 2 sont faites. Restent les parties 6, 7 et 9 ([JALON_2_RENDU_2D.md](JALON_2_RENDU_2D.md)). Le jeu étant en **3D**, la recommandation est de **réduire les parties 6 et 7** à ce qui sert en 3D, ou de passer directement à la définition du jalon 3D. Chaque nouvelle partie doit ensuite être ajoutée à cette checklist.
