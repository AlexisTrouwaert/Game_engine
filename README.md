# Moteur

Moteur de jeu 2D maison (C++20, SDL3). Voir la documentation dans `../moteur_doc/`.

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
4. Compiler et lancer la cible `bac_a_sable`.

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

## Menu du bac à sable

Lancé **sans argument**, `bac_a_sable` s'ouvre sur un menu (Dear ImGui) :

- une barre de menus en haut, avec **DEBUG > Tests moteur**, un sous-menu qui lance directement chaque scène de test ou ouvre la page de sélection (« Toutes les scènes... ») ; **DEBUG > Accueil** quitte le debug ;
- l'**accueil**, où viendra le jeu ;
- la **page de sélection** : chaque scène, sa description, ses réglages (nombre de sprites, taille de carte, graine...) et un bouton **Lancer** ;
- pendant un test, un panneau en bas à gauche : **Arrêter le test** (ou Échap) revient à la sélection, **Accueil** revient à l'accueil.

Avec des options de scène (`--demo`, `--sprites N`, `--iso`...), le programme lance directement la scène, **sans interface**, comme avant : c'est ce qu'utilisent les scripts, les mesures et les captures. `--menu` force le menu en gardant les autres options globales (par exemple `--menu --no-vsync`).

## Mesurer les performances

Mesurer en **Release** : le build Debug active la validation du GPU et fausse les chiffres.

```
cmake --preset windows-release
cmake --build --preset windows-release
build/windows-release/apps/bac_a_sable/bac_a_sable --run-seconds 6 --report --sprites 10000 --no-vsync
```

- `--sprites N` ajoute N sprites qui rebondissent (scène identique à chaque lancement).
- `--report` écrit à la fermeture les temps CPU par frame (moyenne, percentile 99, maximum, par phase).
- `--no-vsync` retire la limite de l'écran : sans lui, les FPS sont plafonnés à la fréquence de l'écran.
- `--no-batching` fait un draw call par sprite, pour comparer (l'image est identique).
- `--depth` enregistre les sprites dans le désordre et les fait trier par hauteur : mesure le coût du tri.
- `--freeze-after N` fige la scène après N pas de logique, pour comparer deux captures pixel par pixel.
- `--iso` affiche la scène isométrique (carte de tuiles, caméra, tuile sous la souris) ; avec `--map N`, `--zoom Z`, `--camera X Y`, `--mouse X Y` et `--interleave`. Flèches ou ZQSD/WASD pour se déplacer, molette pour zoomer.
- `--no-input` ignore le clavier et la souris réels (sauf Échap) : indispensable pour des mesures reproductibles, sinon une molette ou une touche pendant l'essai fausse le zoom.
- `--atlas` affiche tous les sprites de l'atlas `test`, chacun à son pivot, puis un personnage de quatre façons (normal, retourné, ×2, retourné ×3).
- `--text` affiche une phrase française, un paragraphe avec retour à la ligne, les trois alignements, et un compteur de FPS.
- `--demo` réunit une carte de tuiles (`TileMap`) avec des murs procéduraux, des créatures qui marchent avec leur animation et font demi-tour devant les murs (`--seed` pour leur position/direction) et une superposition de statistiques (FPS, sprites, lots/draw calls, événements d'animation). Voir `moteur_doc/JALON_2_RENDU_2D.md`, parties 6, 7 et 9.
- `--render-scale S` (avec `--3d`) : rend la 3D à la fraction S de la fenêtre (0,25 à 1), l'interface restant nette.
- `--3d` affiche la scène « Rendu 3D » du jalon 3 : un sol, des cubes, une sphère et des piliers vus par la caméra 3D, en perspective (inclinaison 50°, champ 30° : le réglage retenu ; `--ortho` pour commencer en orthographique). **P** change de projection, flèches ou ZQSD déplacent la caméra, la molette zoome.
- `--capture chemin.png` (avec `--freeze-after N`) écrit la frame gelée en PNG, sur n'importe quelle scène : pour des comparaisons de pixels automatiques et reproductibles (même graine, même capture).

## Atlas de sprites

Les images à dessiner sont dans `art/<atlas>/` (des PNG, éventuellement rangés en sous-dossiers). Au build, l'outil `atlas_packer` les empaquette dans `<exécutable>/assets/<atlas>.json` et `<atlas>_0.png`, `<atlas>_1.png`… Le nom d'un sprite est son chemin dans le dossier, sans l'extension : `art/monsters/walk_00.png` s'appelle `monsters/walk_00`.

- **Pivot** : par défaut en bas au milieu de l'image. Pour le changer, un fichier `pivots.json` dans le dossier de l'atlas : `{ "nom": [x, y] }`, en pixels de l'image d'origine.
- **Déclarer un atlas** dans le `CMakeLists.txt` d'un programme : `moteur_add_atlas(cible NAME nom SOURCES art/nom)`.
- **Ajouter, modifier ou retirer une image** relance l'empaquetage de cet atlas au build suivant, et de lui seul.
- **Charger et dessiner** : `auto atlas = moteur::TextureAtlas::load(renderer, moteur::asset_path("nom.json"));` puis `sprites.draw(atlas.region("nom_du_sprite"), ancre);`.

L'outil peut aussi se lancer à la main : `atlas_packer --input art/test --output sortie --name test`.

Les textures sont **pré-multipliées** au chargement (`create_texture`) : les PNG restent en alpha normal, on n'a rien à faire.

Si `cmake --preset` utilise le mauvais vcpkg (celui de Visual Studio, plus ancien), ajouter `-DCMAKE_TOOLCHAIN_FILE=<chemin du vcpkg>/scripts/buildsystems/vcpkg.cmake`.

## Animations

Les clips sont décrits dans `assets/animations.json` (format en commentaire dans `animation.hpp`) : des noms de sprites d'atlas, une durée en ticks de simulation, un mode (`once`, `loop`, `ping_pong`) et des événements sur certaines images.

```
auto library = moteur::AnimationLibrary::load(moteur::asset_path("animations.json"));
library.check_regions(atlas);                    // chaque image existe dans l'atlas
moteur::AnimationPlayer player(library.clip("walk"));
player.set_speed(1.5);                           // vitesse d'attaque, de marche...
player.advance(1, &events);                      // une fois par tick, dans update()
sprites.draw(atlas.region(player.region()), ancre);
```

Les durées sont des ticks entiers : la lecture est identique sur toutes les machines, et chaque événement se déclenche exactement une fois.

## Carte de tuiles

`moteur::TileMap` est une grille de cases en calques, chaque case contenant un `TileId` dont le `Tileset` donne le sprite et les propriétés (`walkable`, `opaque`). `map.clip(iso.tiles_in(camera.visible_rect(), marge))` donne les tuiles à dessiner ; `map.walkable(tileset, case)` sert aux déplacements.

## Modèles 3D (glTF)

Déposer des fichiers `.glb` ou `.gltf` dans `assets/models/` : la scène « Rendu 3D » les affiche tous, en rang, avec leur nombre de triangles et leur temps de chargement. Dans le code :

```
auto model = moteur::Model::load(renderer, moteur::asset_path("models/reference.glb"));
renderer.meshes().draw(model, matrice_monde);
```

**Modèles de test** : `python tools/models/fetch_test_models.py` télécharge quatre modèles de Poly Haven (CC0, environ 11 Mo) dans `assets/models/polyhaven/`, hors de Git. À relancer sur chaque nouvelle machine.

**Éclairage** : rendu PBR (le matériau de glTF, `moteur::Material`), avec un soleil, jusqu'à 32 lumières ponctuelles par frame et un éclairage d'environnement : un ciel procédural, ou toute image `.hdr` déposée dans `assets/environments/` (la scène « Rendu 3D » les propose dans son panneau).

Conventions : celles de glTF (main droite, Y vers le haut, mètres). La 3D est calculée en **couleurs linéaires**, rendue en HDR puis convertie pour l'écran (tone mapping PBR Neutral) : les couleurs passées au `MeshRenderer` sont linéaires, `moteur::srgb_to_linear()` convertit une couleur choisie à l'œil. Les textures de couleur des modèles sont en sRGB avec mipmaps. Depuis Blender, exporter en glTF 2.0 avec « +Y vers le haut » (option par défaut). Le modèle de référence `assets/models/reference.glb` (cube de 1 m, flèches des axes, texture en quadrants) se régénère avec `python tools/models/make_reference_model.py`.

## Crédits et licences

`assets/credits.json` liste les bibliothèques, la police et les assets, avec leurs auteurs et licences ; le bac à sable les affiche dans **Aide > À propos**, avec le texte complet de chaque licence. Au build, les textes de licence des bibliothèques sont copiés dans `licenses/` à côté de l'exécutable (`moteur_add_licenses()` dans `cmake/Assets.cmake`). **Toute nouvelle bibliothèque ou tout nouvel asset doit être ajouté à `credits.json`** (et, pour une bibliothèque, à `moteur_add_licenses()`).

## Texte

`moteur::Font::load(renderer, chemin, taille en pixels)` charge une police TrueType (`assets/fonts/Inter-Regular.ttf`, SIL Open Font License) et rasterise un jeu de caractères latin (accents et guillemets français compris) dans un atlas, avec le même outil que les sprites. `font.draw(sprites, "texte UTF-8", ancre, options)` dessine ; `font.measure(...)` mesure sans dessiner, avec exactement le même calcul de mise en page.

Une seule taille par `Font` : pour rester net, une police se charge à sa taille physique finale plutôt que d'être agrandie après coup.

## Débogage

Le moteur intègre **Dear ImGui** (`moteur::DebugUi`) pour les fenêtres de debug : il suffit de mettre `ApplicationConfig::debug_ui` et d'appeler les fonctions `ImGui::` depuis `Game::render()`. L'interface est dessinée par-dessus les sprites, en pixels de fenêtre, quelle que soit la caméra. Les clics sur une de ses fenêtres ne parviennent pas au jeu.

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

**Prévention** : configurer et construire depuis le **même environnement**. Depuis CLion, c'est le cas. Depuis un terminal, se placer sous la page de codes de CLion avant de lancer CMake (`chcp 850` sur un Windows en français).

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
CMakeLists.txt        racine
CMakePresets.json     presets Windows et macOS
vcpkg.json            dépendances (mode manifeste, baseline épinglée)
cmake/                modules CMake (avertissements, shaders)
src/moteur/           bibliothèque du moteur
apps/bac_a_sable/     exécutable de test
tests/                tests unitaires
tools/atlas_packer/   outil d'empaquetage d'atlas
art/                  sources d'art (PNG), empaquetées en atlas au build
shaders/              sources HLSL
shaders/generated/    MSL pré-généré pour macOS (versionné)
assets/               images de test, copiées à côté de l'exécutable à chaque build
```

## Choix de configuration

- Triplets vcpkg : `x64-windows-static-md` (Windows) et `arm64-osx` (macOS). SDL3 est lié statiquement. Sur macOS, `triplets/arm64-osx.cmake` remplace le triplet de vcpkg pour compiler les dépendances pour macOS 13.0, comme le projet (sinon le linker avertit sur chaque fichier de SDL3).
- Cible macOS minimale : 13.0 (`CMAKE_OSX_DEPLOYMENT_TARGET` dans `CMakePresets.json`).
- Apple Silicon (arm64) uniquement.
