# Moteur

Moteur de jeu maison (C++20, SDL3 / SDL_GPU) : monde en 3D vu par une caméra isométrique fixe, interface en 2D. Voir la documentation dans `moteur_doc/` :

- [`GUIDE.md`](moteur_doc/GUIDE.md) : écrire un jeu avec le moteur ;
- [`FORMATS.md`](moteur_doc/FORMATS.md) : la référence des formats de fichiers (données, sauvegardes, fichiers du joueur) ;
- [`FICHIERS_DU_PROJET.md`](moteur_doc/FICHIERS_DU_PROJET.md) : l'architecture et le rôle de chaque fichier ;
- `JALON_*.md` : les décisions de chaque jalon, et [`ROADMAP.md`](moteur_doc/ROADMAP.md).

La référence de l'API (les en-têtes publics) est générée par Doxygen : `cmake --build --preset windows-debug --target moteur_docs` l'écrit dans `build/windows-debug/docs/html/index.html` (la cible n'existe que si Doxygen 1.9 ou plus est installé).

## Prérequis

| | Windows | macOS |
|---|---|---|
| Compilateur | Visual Studio 2022 avec la charge « Développement Desktop en C++ » (MSVC) | Xcode Command Line Tools (`xcode-select --install`) |
| CMake | 3.25 ou plus (fourni par CLion) | idem |
| Générateur | Ninja (fourni par CLion) | idem |
| vcpkg | intégré à CLion, ou `VCPKG_ROOT` en ligne de commande | idem |

## Construire avec CLion

1. Ouvrir le dossier `moteur/`.
2. Activer les profils issus des presets (`windows-debug` ou `macos-debug`).
3. Vérifier dans `Settings > Build, Execution, Deployment > Vcpkg` que l'intégration vcpkg est activée.
4. Compiler et lancer la cible `bac_a_sable` (les scènes de test), ou `tranche` (la tranche jouable).

Le premier build est long : vcpkg compile SDL3.

## Construire en ligne de commande

Définir `VCPKG_ROOT` vers un clone de vcpkg. Sur Windows, utiliser un « Developer PowerShell for VS 2022 » pour que MSVC soit disponible.

```
cmake --preset windows-debug
cmake --build --preset windows-debug
```

```
cmake --preset macos-debug
cmake --build --preset macos-debug
```

## Tests

Les tests unitaires (doctest) couvrent la logique qui n'a pas besoin de GPU. Ils sont compilés par défaut (option CMake `MOTEUR_BUILD_TESTS`).

```
ctest --test-dir build/windows-debug --output-on-failure
```

La vérification complète (configurations Debug et Release, tests, données, exemple externe, MSL, captures de référence, analyse statique, AddressSanitizer, référence de l'API, paquet livré) tient en une commande :

```
python tools/ci/verify.py
```

`--quick` ne fait que la Release et saute le paquet ; `--configs`, `--no-references`, `--no-msl`, `--no-example`, `--no-analysis`, `--no-asan`, `--no-docs`, `--no-package` et `--keep-going` sont décrits en tête du script et dans `moteur_doc/GUIDE.md` (chapitre 9).

## Paquets pour les joueurs

Les paquets viennent de la configuration livrée (`MOTEUR_SHIPPING` : ni ImGui, ni rechargement à chaud, ni profiler ; assets préparés et regroupés en archives) :

```
cmake --preset windows-shipping
cmake --build build/windows-shipping --target tranche_package
```

Sous Windows : un installeur (Inno Setup) et une archive portable ; sous macOS : un `.app` dans une `.dmg`. Tout arrive dans `build/<preset>/packages/`, avec les symboles de la version (`symbols/`) pour lire les rapports de plantage (`tools/crash/read_report.py`). `examples/jeu_minimal` a aussi le sien (`jeu_minimal_package`). Voir `moteur_doc/GUIDE.md`, chapitre 8.

## Menu du bac à sable

Lancé **sans argument**, `bac_a_sable` s'ouvre sur un menu (Dear ImGui) :

- une barre de menus en haut, avec **DEBUG > Engine tests**, un sous-menu qui lance directement chaque scène de test ou ouvre la page de sélection (« All scenes... ») ; le menu **DEBUG** donne aussi les fenêtres de debug du moteur (inspecteur, assets, entrées, audio, états) ; **DEBUG > Home** quitte le debug ; **Help > About** affiche les crédits ;
- l'**accueil** (« Home »), où viendra le jeu ;
- la **page de sélection** : chaque scène, sa description, ses réglages (nombre de sprites, taille de carte, graine...) et un bouton **Start** ;
- pendant un test, un panneau en bas à gauche : **Stop the test** (ou Échap) revient à la sélection, **Home** revient à l'accueil.

L'interface du bac à sable est en anglais, comme le code ; celle de la tranche jouable est en français.

Avec des options de scène (`--demo`, `--sprites N`, `--iso`...), le programme lance directement la scène, **sans interface**, comme avant : c'est ce qu'utilisent les scripts, les mesures et les captures. `--slice` lance la tranche jouable (son code est dans `apps/tranche/`, bibliothèque `tranche_commun`, partagée avec le programme `tranche`). `--menu` force le menu en gardant les autres options globales (par exemple `--menu --no-vsync`).

## Mesurer les performances

Mesurer en **Release** : le build Debug active la validation du GPU et fausse les chiffres.

```
cmake --preset windows-release
cmake --build --preset windows-release
build/windows-release/apps/bac_a_sable/bac_a_sable --run-seconds 6 --report --sprites 10000 --no-vsync
```

- `--sprites N` ajoute N sprites qui rebondissent (scène identique à chaque lancement).
- `--report` écrit à la fermeture les temps CPU par frame (moyenne, percentile 99, maximum, par phase), et pour la 3D, par passe (scène, ombre) : maillages soumis et dessinés, triangles, draw calls.
- `--no-vsync` retire la limite de l'écran : sans lui, les FPS sont plafonnés à la fréquence de l'écran.
- `--no-batching` fait un draw call par sprite, pour comparer (l'image est identique).
- `--depth` enregistre les sprites dans le désordre et les fait trier par hauteur : mesure le coût du tri.
- `--freeze-after N` fige la scène après N pas de logique, pour comparer deux captures pixel par pixel.
- `--iso` affiche la scène isométrique (carte de tuiles, caméra, tuile sous la souris) ; avec `--map N`, `--zoom Z`, `--camera X Y`, `--mouse X Y` et `--interleave`. Flèches ou ZQSD/WASD pour se déplacer, molette pour zoomer.
- `--no-input` ignore le clavier et la souris réels (sauf Échap) : indispensable pour des mesures reproductibles, sinon une molette ou une touche pendant l'essai fausse le zoom.
- `--atlas` affiche tous les sprites de l'atlas `test`, chacun à son pivot, puis un personnage de quatre façons (normal, retourné, ×2, retourné ×3).
- `--text` affiche une phrase française, un paragraphe avec retour à la ligne, les trois alignements, et un compteur de FPS.
- `--demo` réunit une carte de tuiles (`TileMap`) avec des murs procéduraux, des créatures qui marchent avec leur animation et font demi-tour devant les murs (`--seed` pour leur position/direction) et une superposition de statistiques (FPS, sprites, lots/draw calls, événements d'animation). Voir `moteur_doc/JALON_2_RENDU_2D.md`, parties 6, 7 et 9.
- `--sun ORIENTATION HAUTEUR` (avec `--3d`) : place le soleil, en degrés (par exemple `--sun 235 40` pour des ombres qui tombent vers la caméra).
- `--demo3d` : la scène de démonstration 3D (la carte de la démo 2D en 3D, décor, créatures, torches, barres de vie). Avec `--seed N`, `--map N`, `--creatures N` (300), `--decor N` (3 000), `--tile-floor` (une instance par case de sol au lieu de blocs fusionnés, pour comparer). Clic droit : choisir une créature, clic gauche : l'envoyer, Tab : la suivante, F : la suivre. Capture reproductible : `--demo3d --seed 42 --freeze-after 60 --no-input --capture demo3d.png`.
- `--blender-compare` : les modèles de test et deux sphères éclairés par l'environnement de test seul, la scène que `tools/blender/compare_render.py` rend dans Blender. Avec `--capture image.png`, écrit aussi `image.png.json` pour Blender (voir `moteur_doc/FICHIERS_DU_PROJET.md`).
- Débogage (avec `--3d` ou `--demo3d`) : `--view wireframe|normals|albedo|distance`, `--debug-texture sun|points` (carte d'ombre à l'écran), `--show-bounds`, `--show-lights`, `--show-shadow-frustum`, `--show-ray` (scène `--3d`), et `--gpu-timing` (temps GPU approximatifs par passe dans `--report` ; ralentit le rendu). Anticrénelage : `--aa none|fxaa|msaa2|msaa4` (aussi dans la section « Rendering » du panneau). Les mêmes réglages sont dans la section « Debug » du panneau d'un test lancé depuis le menu.
- `--pixel-size L H` : la fenêtre a exactement L×H pixels, quelle que soit la densité de l'écran (pour comparer des captures Windows et Mac Retina, par exemple `--pixel-size 1280 720`).
- `--billboards N` (avec `--3d`) : N halos lumineux en plus au-dessus du sol, pour mesurer les billboards. La scène montre aussi des créatures avec barre de vie, nom et dégâts flottants.
- `--night` (avec `--3d`) : ni soleil ni ciel, seulement les torches. `--point-shadows N` : nombre de torches ombrées par frame (4 par défaut, 0 pour aucune). `--fixed-torches` : les torches ne tournent plus (leurs ombres sont alors gardées d'une frame à l'autre).
- `--meshes N` (avec `--3d`) : test de charge 3D, N petits cubes et sphères en plus (2,5 par m², une sur quatre tourne ; mêmes objets à chaque lancement). `--no-culling` envoie tout au GPU, pour comparer. Exemple : `--3d --meshes 10000 --no-vsync --run-seconds 6 --report`.
- `--render-scale S` (avec `--3d`) : rend la 3D à la fraction S de la fenêtre (0,25 à 1), l'interface restant nette.
- `--3d` affiche la scène « 3D rendering: first meshes » du jalon 3 : un sol, des cubes, une sphère et des piliers vus par la caméra 3D, en perspective (inclinaison 50°, champ 30° : le réglage retenu ; `--ortho` pour commencer en orthographique). **P** change de projection, flèches ou ZQSD déplacent la caméra, la molette zoome. La case sous la souris est surlignée ; un clic gauche y envoie le « personnage » (la boîte jaune), **F** fait suivre le personnage par la caméra. Avec `--mouse X Y`, la souris est simulée et la case survolée est écrite à la fermeture.
- `--capture chemin.png` (avec `--freeze-after N`) écrit la frame gelée en PNG, sur n'importe quelle scène : pour des comparaisons de pixels automatiques et reproductibles (même graine, même capture).

Les scènes de mesure de la tranche (`fight`, `crowd`, `crossing`, `loading`) se lancent avec `python tools/measures/measure.py` (options `--size`, `--seconds`, `--output`, `--compare`, `--scenes`), qui écrit un rapport par scène et un `summary.json`.

## Atlas de sprites

Les images à dessiner sont dans `art/<atlas>/` (des PNG, éventuellement rangés en sous-dossiers). Au build, l'outil `atlas_packer` les empaquette dans `<exécutable>/assets/<atlas>.json` et `<atlas>_0.png`, `<atlas>_1.png`… Le nom d'un sprite est son chemin dans le dossier, sans l'extension : `art/monsters/walk_00.png` s'appelle `monsters/walk_00`.

- **Pivot** : par défaut en bas au milieu de l'image. Pour le changer, un fichier `pivots.json` dans le dossier de l'atlas : `{ "nom": [x, y] }`, en pixels de l'image d'origine.
- **Déclarer un atlas** dans le `CMakeLists.txt` d'un programme : `moteur_add_atlas(cible NAME nom SOURCES art/nom)`.
- **Ajouter, modifier ou retirer une image** relance l'empaquetage de cet atlas au build suivant, et de lui seul.
- **Charger et dessiner** : `auto atlas = moteur::TextureAtlas::load(renderer, moteur::asset_path("nom.json"));` puis `sprites.draw(atlas.region("nom_du_sprite"), ancre);`.

L'outil peut aussi se lancer à la main : `atlas_packer --input art/test --output sortie --name test`.

Les textures sont **pré-multipliées** au chargement (`create_texture`) : les PNG restent en alpha normal, on n'a rien à faire.

Si `cmake --preset` utilise le mauvais vcpkg (celui de Visual Studio, plus ancien), ajouter `-DCMAKE_TOOLCHAIN_FILE=<chemin du vcpkg>/scripts/buildsystems/vcpkg.cmake`.

## Animations de sprites (2D du bac à sable)

Depuis la revue des jalons 1 à 7 (point 2.1), le 2D du jalon 2 (caméra 2D, projection isométrique 2D, animations de sprites) **n'est plus dans le moteur** : il vit dans le bac à sable (`apps/bac_a_sable/camera2d`, `iso`, `sprite_animation`, bibliothèque `bac_a_sable_2d`, espace de noms `bac_a_sable`) pour ses scènes 2D et leur capture de référence. Les personnages s'animent en 3D (`moteur::Animator`).

Les clips sont décrits dans `assets/animations.json` (format en commentaire dans `apps/bac_a_sable/sprite_animation.hpp`) : des noms de sprites d'atlas, une durée en ticks de simulation, un mode (`once`, `loop`, `ping_pong`) et des événements sur certaines images.

```
auto library = bac_a_sable::AnimationLibrary::load(moteur::asset_path("animations.json"));
library.check_regions(atlas);                    // chaque image existe dans l'atlas
bac_a_sable::AnimationPlayer player(library.clip("walk"));
player.set_speed(1.5);                           // vitesse d'attaque, de marche...
player.advance(1, &events);                      // une fois par tick, dans update()
sprites.draw(atlas.region(player.region()), ancre);
```

Les durées sont des ticks entiers : la lecture est identique sur toutes les machines, et chaque événement se déclenche exactement une fois.

## Carte de tuiles

`moteur::TileMap` est une grille de cases en calques, chaque case contenant un `TileId` dont le `Tileset` donne le sprite et les propriétés (`walkable`, `opaque`). Une case fait 1 m de côté sur le sol ; `map.walkable(tileset, case)` sert aux déplacements (voir aussi `NavGrid`). Dans les scènes 2D du bac à sable, `map.clip(iso.tiles_in(camera.visible_rect(), marge))` donne les tuiles à dessiner.

## Modèles 3D (glTF)

Déposer des fichiers `.glb` ou `.gltf` dans `assets/models/` : la scène `--3d` les affiche tous, en rang, avec leur nombre de triangles et leur temps de chargement. Dans le code :

```
auto model = moteur::Model::load(renderer, moteur::asset_path("models/reference.glb"));
renderer.meshes().draw(model, matrice_monde);
```

**Modèles de test** : `python tools/models/fetch_test_models.py` télécharge quatre modèles de Poly Haven (CC0, environ 11 Mo) dans `assets/models/polyhaven/`, hors de Git. À relancer sur chaque nouvelle machine.

**Éclairage** : rendu PBR (le matériau de glTF, `moteur::Material`), avec un soleil qui projette des ombres (réglables avec `MeshRenderer::set_shadows()`), jusqu'à 32 lumières ponctuelles par frame et un éclairage d'environnement : un ciel procédural, ou toute image `.hdr` déposée dans `assets/environments/` (la scène `--3d` les propose dans son panneau).

**Transparence** : les matériaux glTF en `alphaMode` `MASK` sont découpés (alpha testé, `Material::alpha_cutoff`), dans la scène et dans toutes les ombres ; `assets/models/alpha_mask_reference.glb` (régénéré par `python tools/models/make_alpha_mask_model.py`) le vérifie dans la scène `--3d` (`--3d --camera -5.5 5.5 --view-height 3`). Le mode `BLEND` n'est pas encore dessiné : un tel matériau est opaque, et le journal (`log.txt`) le signale.

Conventions : celles de glTF (main droite, Y vers le haut, mètres). La 3D est calculée en **couleurs linéaires**, rendue en HDR puis convertie pour l'écran (tone mapping PBR Neutral) : les couleurs passées au `MeshRenderer` sont linéaires, `moteur::srgb_to_linear()` convertit une couleur choisie à l'œil. Les textures de couleur des modèles sont en sRGB avec mipmaps. Depuis Blender, exporter en glTF 2.0 avec « +Y vers le haut » (option par défaut). Le modèle de référence `assets/models/reference.glb` (cube de 1 m, flèches des axes, texture en quadrants) se régénère avec `python tools/models/make_reference_model.py`.

## Crédits et licences

Les crédits, avec auteurs et licences, sont en deux fichiers : `assets_moteur/credits_moteur.json` pour ce que le moteur apporte (les bibliothèques et la police Inter), `assets/credits.json` pour les assets du jeu et des tests (environnements, modèles, sons...). Le bac à sable les affiche dans **Help > About**, avec le texte complet de chaque licence. Au build, les textes de licence des bibliothèques sont copiés dans `licenses/` à côté de l'exécutable (`moteur_add_licenses()` dans `cmake/Assets.cmake`, appelée par `moteur_add_game()` avec la liste `MOTEUR_LICENSE_PACKAGES` du moteur et les `LICENSES` du jeu). **Toute nouvelle bibliothèque doit être ajoutée à `credits_moteur.json` et à `MOTEUR_LICENSE_PACKAGES` (`src/moteur/CMakeLists.txt`) ; tout nouvel asset, à `credits.json`.**

## Texte

`moteur::Font::load(renderer, chemin, taille en pixels)` charge une police TrueType (`assets_moteur/fonts/Inter-Regular.ttf`, SIL Open Font License, copiée dans `assets/fonts/` à côté de l'exécutable) et rasterise un jeu de caractères latin (accents et guillemets français compris) dans un atlas, avec le même outil que les sprites. `font.draw(sprites, "texte UTF-8", ancre, options)` dessine ; `font.measure(...)` mesure sans dessiner, avec exactement le même calcul de mise en page.

Une seule taille par `Font` : pour rester net, une police se charge à sa taille physique finale plutôt que d'être agrandie après coup.

## Débogage

Le moteur intègre **Dear ImGui** pour les fenêtres de debug (son intégration, `DebugUi`, est un en-tête privé du moteur, `src/moteur/private/moteur/debug_ui.hpp`) : il suffit de mettre `ApplicationConfig::debug_ui` et d'appeler les fonctions `ImGui::` depuis `Game::render()`. La configuration livrée (`MOTEUR_SHIPPING`) n'a pas Dear ImGui. L'interface est dessinée par-dessus les sprites, en pixels de fenêtre, quelle que soit la caméra. Les clics sur une de ses fenêtres ne parviennent pas au jeu.

Les ressources GPU sont **nommées** (`sprite.vertices`, `sprite.indices`, `sprite pipeline`, `sprite.sampler`, les shaders par leur nom de fichier, les pages d'atlas et de police par leur nom) : une capture RenderDoc (Windows) ou le débogueur Metal de Xcode (Mac) les affiche sous ces noms plutôt qu'anonymes. `create_buffer()`, `create_texture()`, `create_sampler()` et `load_shader()` acceptent un nom optionnel pour toute nouvelle ressource.

Une mauvaise utilisation de l'API SDL_GPU (par exemple une ressource non liée avant un dessin) déclenche une **assertion SDL qui bloque le programme** avec une invite. Comme pour les assertions du CRT, lancer le programme avec un délai et le tuer s'il ne se termine pas.

Le mode debug du GPU (activé par défaut hors Release) est confirmé actif : une erreur d'usage volontaire de l'API a bien produit un message. Il vérifie surtout la cohérence des appels (états, liaisons), pas les accès mémoire à l'intérieur d'un draw call (cela demanderait la validation basée sur le GPU, un mode séparé et plus lourd, non activé ici).

## Problèmes connus

### Caractères accentués corrompus dans le code (MSVC)

**Symptôme** : un caractère français écrit directement dans une chaîne de caractères C++ (même un littéral `u8"..."`) ne correspond pas à ce qui est réellement lu à l'exécution.

**Cause** : sans le drapeau `/utf-8`, MSVC lit les fichiers source dans la page de codes du système, pas en UTF-8, et corrompt silencieusement tout caractère non-ASCII écrit en dur dans le code.

**Correctif appliqué** : `/utf-8` est activé pour MSVC dans `cmake/Warnings.cmake`, sur tout le projet. Rien à faire de plus, sauf en cas de nouveau symptôme de ce genre après avoir copié du code depuis un autre projet qui n'aurait pas ce drapeau.

### Build incrémental incohérent (Windows non anglophone)

**Symptôme** : le programme plante ou se comporte étrangement après la modification d'un en-tête, et un **build propre** (`Build > Rebuild Project`, ou supprimer le dossier de build) fait tout disparaître. La sortie de build contient souvent des centaines de lignes « Remarque : inclusion du fichier : … ».

**Cause** : Ninja suit les dépendances d'en-têtes en lisant les lignes `/showIncludes` du compilateur. MSVC les écrit dans la langue de Windows, avec des **espaces insécables** dont l'octet dépend de la page de codes de la console. CMake mémorise ce texte à la configuration ; si le build tourne sous une autre page de codes, Ninja ne reconnaît plus les lignes, n'enregistre **aucune dépendance d'en-tête** et ne recompile plus les fichiers concernés, sans aucun message d'erreur.

**Prévention** : configurer et construire depuis le **même environnement**, avec les **mêmes CMake et Ninja**. Depuis CLion, c'est le cas. Depuis un terminal, utiliser `tools/ci/verify.py`, qui prend les outils de CLion et lance chaque commande dans sa propre console ; à la main, se placer sous la page de codes de CLion avant de lancer CMake (`chcp 850` sur un Windows en français) et ne pas utiliser le CMake ni le Ninja de Visual Studio (plus anciens) sur un dossier de CLion : le Ninja 1.12 de Visual Studio efface le journal du Ninja 1.13 de CLion, et tout est reconstruit.

**Vérification** : chercher `msvc_deps_prefix` dans `CMakeFiles/rules.ninja` du dossier de build, et comparer avec ce que `cl /showIncludes` affiche dans la console utilisée. Ou modifier un en-tête et lancer `cmake --build . -- -n` : les fichiers qui l'incluent doivent apparaître.

## Shaders

Les shaders sont écrits en HLSL dans `shaders/` et compilés par `shadercross` (SDL_shadercross).

- **Windows** : compilés en DXIL à chaque build, automatiquement.
- **macOS** : le port vcpkg de shadercross n'est pas disponible (il dépend de DXC, absent sur Mac). Le build copie les fichiers MSL déjà générés dans `shaders/generated/msl/`.
- **Après avoir modifié ou ajouté un shader**, sous Windows : construire la cible `export_msl_shaders` puis committer les fichiers de `shaders/generated/msl/`, sinon le Mac utilisera une version périmée (ou ne compilera pas, pour un nouveau shader).

```
cmake --build --preset windows-debug --target export_msl_shaders
```

## Structure

```
CMakeLists.txt          racine
CMakePresets.json       presets Windows et macOS (debug, release, shipping, asan, fuzz)
vcpkg.json              dépendances (mode manifeste, baseline épinglée) ; ports/ et triplets/ les complètent
cmake/                  modules CMake (avertissements, shaders, atlas, assets, moteur_add_game,
                        moteur_package, référence Doxygen) ; cmake/windows/ et cmake/macos/ : ressources
                        Windows (icône, version, manifeste, installeur) et Info.plist du .app
src/moteur/             bibliothèque du moteur : include/moteur/ (en-têtes publics),
                        private/moteur/ (en-têtes privés)
apps/bac_a_sable/       les scènes de test et l'éditeur de cartes (programmes bac_a_sable et editeur)
apps/tranche/           la tranche jouable (programmes tranche et tranche_cook, bibliothèque tranche_commun)
examples/jeu_minimal/   le plus petit jeu construit sur le moteur, aussi construit de l'extérieur
tests/                  tests unitaires (doctest)
tools/atlas_packer/     outil d'empaquetage d'atlas
tools/cook/             outil de préparation des assets (moteur_cook)
tools/ci/               verify.py (la vérification), static_analysis.py (clang-tidy)
tools/docs/             Doxyfile.in, mainpage.md, check_docs.py (référence de l'API)
tools/package/          check_package.py (installer, jouer, désinstaller), make_icon.py
tools/crash/            read_report.py (lire un rapport de plantage avec les symboles)
tools/measures/         measure.py (scènes de mesure)
tools/fuzz/             fuzzer.py (fuzzing des lecteurs de fichiers)
tools/tests/            check_references.py (captures de référence)
tools/models/, tools/textures/, tools/audio/, tools/blender/, tools/maps/, tools/input/
                        scripts de génération, de téléchargement et de comparaison
art/                    sources d'art (PNG), empaquetées en atlas au build ; art/icons/ : icône de la tranche
shaders/                sources HLSL
shaders/generated/      MSL pré-généré pour macOS (versionné)
assets_moteur/          assets du moteur (police, crédits), copiés avant ceux du jeu
assets/                 assets du jeu et des tests, copiés à côté de l'exécutable à chaque build
moteur_doc/             documentation
```

## Choix de configuration

- Triplets vcpkg : `x64-windows-static-md` (Windows) et `arm64-osx` (macOS). SDL3 est lié statiquement. Sur macOS, `triplets/arm64-osx.cmake` remplace le triplet de vcpkg pour compiler les dépendances pour macOS 13.0, comme le projet (sinon le linker avertit sur chaque fichier de SDL3).
- Cible macOS minimale : 13.0 (`CMAKE_OSX_DEPLOYMENT_TARGET` dans `CMakePresets.json`).
- Apple Silicon (arm64) uniquement.
