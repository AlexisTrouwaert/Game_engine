# Référence des formats de fichiers

Retour à la [roadmap générale](ROADMAP.md). Écrit pour le [jalon 8, partie 9](JALON_8_CONSOLIDATION.md#9-documentation-de-lapi). Description des fichiers du code : [Fichiers du projet](FICHIERS_DU_PROJET.md).

Ce document décrit **chaque fichier que le moteur lit ou écrit**, pour qui écrit ces fichiers à la main ou les produit avec un outil, sans lire les sources. Pour chaque format : son rôle, où il se trouve, sa version et comment elle évolue, chaque clé (type, unité, défaut, contraintes), les anciennes clés encore lues, ce que fait le lecteur d'une valeur inconnue ou fausse, et un exemple minimal valide.

**Le lecteur fait foi.** Chaque clé citée ici est lue ou écrite par le code indiqué dans le titre de sa section ; en cas de doute, c'est le code qui a raison, et ce document est à corriger.

## Sommaire

- [Conventions communes](#conventions-communes)
- [Vue d'ensemble](#vue-densemble)
- [Anciennes clés françaises encore lues](#anciennes-clés-françaises-encore-lues)
- [1. Cartes](#1-cartes)
- [2. Tables de données](#2-tables-de-données)
  - [2.1 Sources JSON](#21-sources-json)
  - [2.2 Forme compilée `.mdat`](#22-forme-compilée-mdat)
  - [2.3 Contrôle de la structure CBOR](#23-contrôle-de-la-structure-cbor)
- [3. Descriptions d'animation](#3-descriptions-danimation)
  - [3.1 Le fichier](#31-le-fichier)
  - [3.2 Le graphe d'animation](#32-le-graphe-danimation)
- [4. Effets de particules](#4-effets-de-particules)
- [5. Liaisons de touches](#5-liaisons-de-touches)
  - [5.1 Forme texte des sources](#51-forme-texte-des-sources)
  - [5.2 Fichier des liaisons par défaut](#52-fichier-des-liaisons-par-défaut)
  - [5.3 Fichier des liaisons du joueur](#53-fichier-des-liaisons-du-joueur)
  - [5.4 Enregistrement des entrées (rejeu)](#54-enregistrement-des-entrées-rejeu)
- [6. Sauvegardes](#6-sauvegardes)
  - [6.1 Le conteneur `.sav`](#61-le-conteneur-sav)
  - [6.2 En-tête et corps](#62-en-tête-et-corps)
  - [6.3 Versions et migrations](#63-versions-et-migrations)
  - [6.4 Emplacements, écriture atomique, copie de secours](#64-emplacements-écriture-atomique-copie-de-secours)
  - [6.5 Section du monde (composants du moteur)](#65-section-du-monde-composants-du-moteur)
- [7. Archives `.pak`](#7-archives-pak)
- [8. Préparation des assets](#8-préparation-des-assets)
  - [8.1 Manifeste de livraison](#81-manifeste-de-livraison)
  - [8.2 Motifs (globs)](#82-motifs-globs)
  - [8.3 Sortie et `cook_state.json`](#83-sortie-et-cook_statejson)
- [9. Réglages du joueur et fichiers du programme](#9-réglages-du-joueur-et-fichiers-du-programme)
  - [9.1 Le dossier des préférences](#91-le-dossier-des-préférences)
  - [9.2 `display.json`](#92-displayjson)
  - [9.3 `audio.json`](#93-audiojson)
  - [9.4 `variables.cfg`](#94-variablescfg)
  - [9.5 Rapport de mesure (JSON, version 2)](#95-rapport-de-mesure-json-version-2)
  - [9.6 Crédits (`credits.json`, `credits_moteur.json`)](#96-crédits-creditsjson-credits_moteurjson)
- [10. Rapports de plantage](#10-rapports-de-plantage)
- [11. Textures KTX2 acceptées](#11-textures-ktx2-acceptées)
- [12. Modèles glTF acceptés](#12-modèles-gltf-acceptés)
- [13. Valeurs décrites par la réflexion](#13-valeurs-décrites-par-la-réflexion)
- [14. Préfabriqués `.prefab.json`](#14-préfabriqués-prefabjson)
- [15. Identité des assets : `.meta` et registre](#15-identité-des-assets--meta-et-registre)
- [16. Modèles préparés `.mmdl`](#16-modèles-préparés-mmdl)
- [17. Documents livrés en CBOR](#17-documents-livrés-en-cbor)
- [18. Maillages de navigation `.mnav`](#18-maillages-de-navigation-mnav)
- [19. Niveaux v3 `.level.json`](#19-niveaux-v3-leveljson)
- [20. Niveaux préparés `.mlvl` et `.mlvc`](#20-niveaux-préparés-mlvl-et-mlvc)
- [Annexe : sommes de contrôle](#annexe--sommes-de-contrôle)

---

## Conventions communes

- **JSON** : texte UTF-8, lu par nlohmann::json (JSON strict : ni commentaires, ni virgule finale). Les objets sont lus **triés par clé** : l'ordre des clés dans le fichier ne compte jamais (là où un ordre compte, le format utilise une liste). Une clé en double garde la dernière valeur.
- **Clés inconnues** : ignorées en silence, sauf dans les tables de données (avertissement, voir [2.1](#21-sources-json)) et le manifeste de livraison (rien n'est signalé, mais les anciennes clés françaises donnent un avertissement). Les clés qui commencent par `_` (`"_note"`) servent de commentaires : elles sont **retirées** dans les tables, et simplement ignorées ailleurs puisqu'aucun lecteur ne les connaît.
- **Champ `version`** : un entier en tête de chaque format JSON. Un lecteur refuse une version qu'il ne connaît pas (le détail par format est donné), sauf les crédits, qui ne la lisent pas.
- **Binaire** : entiers **petit-boutistes** (little-endian), sans remplissage entre les champs.
- **Chemins d'assets** : relatifs au dossier des assets du programme (`assets/`), séparés par `/`, casse comprise (`models/characters/kaykit_adventurers/Knight.glb`). C'est aussi la clé d'un asset (son nom dans le gestionnaire d'assets et dans les messages).
- **Unités** : mètres, secondes, degrés dans les fichiers (le code convertit) ; **ticks** de logique pour les fondus d'animation (60 par seconde par défaut : `ApplicationConfig::tick_rate`).
- **Erreurs** : un lecteur qui refuse un fichier lance `std::runtime_error` dont le message nomme le fichier et l'endroit (`Map 'maps/a.json': column 3, row 2: 'x' is not in the legend`). Les messages sont en anglais (décision du 2026-10-16).

## Vue d'ensemble

| Format | Fichier type | Version | Lu par | Écrit par |
|---|---|---|---|---|
| Carte | `assets/maps/*.json` | 1 et 2 | `MapData::parse` (`map_data.cpp`) | `MapDocument::to_json` (`map_document.cpp`, éditeur) |
| Table de données (source) | `assets/data/<table>/*.json` | 1 | `DataTableBase::read_json` (`data_table.cpp`), `DataReader` (`data_reader.cpp`) | à la main |
| Table compilée | `<dossier>/<table>.mdat` | 1 | `DataTableBase::decode` | `DataTableBase::encode`, `DataTables::compile` |
| Description d'animation | `assets/animations/*.json` | 1 | `AnimationSet::parse` (`animation_set.cpp`), `AnimationGraph::parse` (`animation_graph.cpp`) | à la main |
| Effet de particules | `assets/effects/*.json` | 1 | `ParticleEffect::parse` (`particles.cpp`) | à la main |
| Liaisons de touches | `assets/input/*.json` | 1 | `Input::load_bindings` (`input.cpp`) | à la main |
| Liaisons du joueur | choisi par le jeu (préférences) | 1 | `Input::apply_user_bindings` | `Input::user_bindings_json` |
| Enregistrement d'entrées | `--record-input`, `--replay-input` | 1 | `input_recording_from_json` | `input_recording_to_json` |
| Sauvegarde | `<dossier>/<nom>.sav` | format 1, sections versionnées | `SaveGame::decode` (`save_file.cpp`) | `SaveGame::encode`, `SaveSlots::write` |
| Archive | `archives/*.pak` | 1 | `Archive::open` (`archive.cpp`) | `write_archive` |
| Manifeste de livraison | `assets/livraison_tranche.json` | 1 | `cook` (`cook.cpp`) | à la main |
| État de la préparation | `<sortie>/cook_state.json` | 1 | `cook` | `cook` |
| Réglages d'affichage | préférences, `display.json` | 1 | `DisplaySettings::from_json` (`display.cpp`) | `DisplaySettings::to_json` |
| Réglages du son | préférences, `audio.json` | 1 | `Audio::apply_settings_json` (`audio.cpp`) | `Audio::settings_json` |
| Variables archivées | préférences, `variables.cfg` | — (texte) | `Variables::set_pending` (`variables.cpp`) | `Variables::archived_text` |
| Rapport de mesure | `--report-json FICHIER` | 2 | `tools/measures/measure.py` | `Application::run` (`application.cpp`) |
| Crédits | `credits.json`, `credits_moteur.json` | — | `Credits::add_file` (`credits.cpp`) | à la main |
| Rapport de plantage | préférences, `crash_reports/<date>.txt` et `<date>.dmp` | — | `tools/crash/read_report.py` | `crash.cpp` |
| Texture KTX2 | `*.ktx2` | KTX 2.0 | `decode_ktx2` (`ktx_texture.cpp`) | outils externes ; `transcode_ktx2_for_shipping` |
| Modèle glTF | `*.gltf` + `.bin` + images, `*.glb` | glTF 2.0 | `load_gltf`, `parse_gltf` (`model.cpp`) | outils externes (Blender) |
| Préfabriqué | `*.prefab.json` | 1 | `Prefab::parse` (`prefab.cpp`) | à la main, `ARPG/assets_source/commun/prefab_moteur.py` |
| Identité d'un asset | `<asset>.meta` | 1 | `parse_asset_meta` (`asset_registry.cpp`) | `AssetRegistry::scan`, `tools/assets/assets.py` |
| Registre figé | `assets/asset_registry.mreg` (jeu livré) | 1 | `AssetRegistry::decode` | `AssetRegistry::encode` (préparation) |
| Modèle préparé | `.imported/<clé>.mmdl` | 2 | `decode_prepared_model` (`model_import.cpp`) | `encode_prepared_model` (import, préparation) |
| Signature d'import | `.imported/<clé>.mmdl.files` | — | `Assets` (développement) | `Assets` |
| Document livré | `*.json` des archives (CBOR) | celle du document | `parse_document` (`document.cpp`) | `shipped_document` (préparation) |
| Maillage de navigation | `*.mnav` | 1 | `NavMesh::load` (`navigation.cpp`) | `NavMesh::save` |
| Niveau (source) | `assets/levels/*.level.json` | 3 | `LevelDocument::parse` (`level.cpp`) | `LevelDocument::to_json` (éditeur), `bac_a_sable --migrate-map` |
| Niveau préparé | `*.level.mlvl` et `*.level.chunks/X_Z.mlvc` | 1 | `decode_level_header`, `decode_level_chunk` (`level_bake.cpp`) | la préparation (`bake_level`) |

## Anciennes clés françaises encore lues

Décision du 2026-10-17 ([revue des jalons 1 à 7](REVUE_JALONS_1_A_7.md)) : les formats de fichiers sont passés aux **clés anglaises**, celles de l'API. Les anciennes clés restent lues le temps de migrer les fichiers existants ; les fichiers du projet sont déjà migrés. **N'écrivez que les clés anglaises.**

| Format | Ancien (encore lu) | Nouveau | Comportement |
|---|---|---|---|
| Carte, directions des connecteurs | `nord`, `est`, `sud`, `ouest` | `north`, `east`, `south`, `west` | lu en silence ; l'éditeur réécrit en anglais |
| Carte, point d'entrée | `"point": "depart"` | `"point": "start"` | lu comme `start`, avec un avertissement au journal (`point 'depart' is deprecated`) |
| Réglages d'affichage | fichier `affichage.json` | `display.json` | lu si `display.json` manque, puis réécrit sous le nouveau nom (l'ancien fichier reste sur le disque) |
| Réglages d'affichage, clés | `ecran`, `synchronisation`, `limite_images`, `resolution_rendu`, `anticrenelage`, `ombres` (`mode` n'a pas changé) | `display`, `vsync`, `frame_limit`, `render_scale`, `anti_aliasing`, `shadows` | lu en silence ; la clé anglaise l'emporte si les deux sont là |
| Réglages d'affichage, valeurs | `fenetre`, `plein_ecran` ; `basses`, `moyennes`, `hautes` | `windowed`, `fullscreen` ; `low`, `medium`, `high` | lu en silence |
| Manifeste de livraison | `inclure`, `exclure`, `donnees`, `verifier`, `cartes`, `effets`, `nom` | `include`, `exclude`, `data`, `check`, `maps`, `effects`, `name` | lu, avec un avertissement de la préparation (`key 'inclure' is deprecated, write 'include'`) |
| Crédits, option de build | `"build": "DEVELOPPEMENT"` | `"build": "DEVELOPMENT"` | lu en silence |
| Sauvegardes, énumérations du `mover` | nombres (sauvegardes de développement du jalon 7) | noms (`"to_point"`...) | lu en silence |

Pas de lecture des anciennes clés pour le **rapport de mesure** (la version 1 avait des clés françaises ; la version 2 est produite à chaque mesure) ni pour les **options de l'outil de préparation** (`--manifeste`, `--sortie` sont devenus `--manifest`, `--output`).

Restent volontairement en français les **données du jeu** : champs des tables de la tranche (`vie`, `vitesse`), noms de tuiles, de points, de types d'objets et de leurs `props`, noms des états d'animation de la tranche. Le moteur ne les interprète pas.

---

## 1. Cartes

**Rôle** : une carte (ou un morceau de carte, *chunk*) dessinée en caractères, une case par caractère : types de tuiles, couches, points nommés, objets posés et connecteurs. **Lecteur** : `MapData::parse` (`src/moteur/map_data.cpp`). **Écrivain** : `MapDocument::to_json` (`src/moteur/map_document.cpp`), utilisé par l'éditeur. **Fichiers** : `assets/maps/*.json` (exemples : `arene.json`, `cul_de_sac.json` en version 1, `tranche.json` en version 2).

### Version

`MapData::kVersion = 2`. Les versions **1 et 2** sont lues ; toute autre (ou un `version` absent, ou un document qui n'est pas un objet) est refusée (`expected an object with "version": 1 or 2`). La version 2 ajoute `chunk`, `objects` et `connectors`, tous facultatifs ; l'éditeur écrit toujours la version 2. Un fichier version 1 qui contient l'une de ces trois clés est refusé, même vide (`"objects" needs "version": 2`) : elle n'est ni lue ni ignorée. Il n'y a pas de migration à faire : un fichier version 1 est un fichier version 2 valide sans ces clés.

### Repère

La case de la colonne `i`, ligne `j` est la tuile `(i, j)` : elle couvre `x` dans `[i, i + 1)` et `z` dans `[j, j + 1)`, cases de **1 m**. La ligne 0 est `z = 0`, la colonne 0 est `x = 0`. Le nord est vers `z = 0`.

### Clés

| Clé | Type | Défaut | Contraintes, sens |
|---|---|---|---|
| `version` | entier | — (exigé) | 1 ou 2 |
| `description` | texte | `""` | montré par les outils |
| `chunk` | booléen | `false` | (v2) un morceau assemblé par le jeu : pas besoin de point `start` |
| `tiles` | objet | — (exigé) | nom de tuile → réglages (ci-dessous). Les identifiants de tuile suivent l'**ordre alphabétique** des noms, à partir de 1 (0 = pas de tuile) |
| `legend` | objet | — (exigé) | un caractère → ce que contient la case (ci-dessous) |
| `rows` | liste de textes | — (exigé) | les lignes de la carte, toutes de la même longueur ; un `\r` final est ignoré |
| `objects` | liste d'objets | `[]` | (v2) objets posés, voir plus bas |
| `connectors` | liste d'objets | `[]` | (v2) connecteurs, voir plus bas |

**Réglages d'une tuile** (`tiles.<nom>`, un objet, `{}` pour tous les défauts) :

| Clé | Type | Défaut | Sens |
|---|---|---|---|
| `walkable` | booléen | `true` | on peut s'y tenir (collisions, chemins) |
| `opaque` | booléen | `false` | arrête la vue (visibilité) |
| `region` | texte | `""` | nom d'un sprite dans un atlas (rendu 2D) ; vide si le jeu dessine autrement |

Une case est **praticable** si elle a au moins une tuile et que toutes ses tuiles sont `walkable` ; **opaque** si l'une de ses tuiles est `opaque`. Une case sans tuile est un trou (non praticable).

**Légende** (`legend.<caractère>`) : la clé est **un seul octet** (un caractère ASCII ; un caractère accentué, sur deux octets en UTF-8, est refusé). La valeur est :

- soit une **liste de noms de tuiles**, une par couche à partir de la couche 0 (`["floor", "wall"]`) ; `"-"` laisse la couche vide (une couche plus haute a une tuile) ; `[]` : rien, un trou ;
- soit un **objet** `{"tiles": [...], "point": "<nom>"}` : les mêmes tuiles, et un **point nommé** sur chaque case dessinée avec ce caractère.

Les tuiles sont toujours une liste, même d'un seul nom : `".": "floor"` est refusé (`the tiles must be a list of names`), il faut écrire `".": ["floor"]`.

La carte a autant de couches que la plus longue liste de la légende. Le point d'entrée d'une carte entière s'appelle `start` (`moteur::kStartPoint`) ; l'éditeur l'exige quand `chunk` est faux (le lecteur, lui, ne l'exige pas). Les autres noms de points sont libres (`but`, `brasero`).

**Objet posé** (`objects[i]`, v2) : une description que le jeu transforme en entité, pas une entité.

| Clé | Type | Défaut | Sens |
|---|---|---|---|
| `id` | texte | — (exigé) | unique dans la carte, non vide ; l'éditeur donne `o1`, `o2`... |
| `type` | texte | — (exigé) | ce que le jeu en construit, non vide |
| `x`, `z` | nombre | — (exigés) | position au sol, mètres |
| `y` | nombre | `0` | hauteur, mètres |
| `rotation` | nombre | `0` | degrés autour de l'axe vertical |
| `scale` | nombre | `1` | échelle uniforme |
| `props` | objet | `{}` | propriétés propres au type, lues par le jeu (doit être un objet) |

**Connecteur** (`connectors[i]`, v2) : là où un morceau en rejoint un autre.

| Clé | Type | Défaut | Sens |
|---|---|---|---|
| `name` | texte | — (exigé) | l'identifie dans la carte |
| `x`, `z` | entier | — (exigés) | la case, qui doit être dans la carte |
| `direction` | texte | `"north"` | `north`, `east`, `south`, `west` (anciens noms : `nord`, `est`, `sud`, `ouest`) |
| `kind` | texte | `""` | texte libre pour le jeu (`door`...) |

### Ce que fait le lecteur

Refus (`std::runtime_error` qui nomme le fichier, et la colonne et la ligne pour une case) : JSON invalide, version inconnue, `chunk`, `objects` ou `connectors` dans un fichier version 1, `tiles`, `legend` ou `rows` absent, tuile au nom vide, tuile inconnue dans la légende, clé de légende qui n'est pas un caractère, tuiles de légende qui ne sont pas une liste, **deux caractères qui veulent dire la même chose** (mêmes tuiles et même point : la carte doit pouvoir se redessiner), plus de 255 caractères de légende, carte vide, lignes de longueurs différentes, caractère absent de la légende ; objet à `id` vide ou en double, à `type` vide, `props` qui n'est pas un objet ; connecteur de direction inconnue ou hors de la carte ; une valeur du mauvais type JSON (message de nlohmann). Les clés inconnues sont ignorées. Le point `depart` est lu comme `start` avec un avertissement.

L'éditeur (`MapDocument::validate`) ajoute des contrôles qui ne bloquent pas la lecture : point `start` absent (erreur, sauf `chunk`), départ sur une case bloquée, cases praticables inatteignables depuis le départ, objet hors de la carte, connecteur qui n'est pas sur une case praticable du bord (avertissements).

### Écriture par l'éditeur

`version` 2, `description`, `chunk` seulement s'il est vrai, tuiles triées par nom avec seulement leurs réglages différents des défauts, légende triée par caractère, lignes, puis `objects` et `connectors` s'il y en a (`y`, `rotation`, `scale`, `props` d'un objet seulement s'ils diffèrent des défauts). Les nombres sont écrits au plus court qui se relit à l'identique (`0.55657434`). Une case dont la combinaison (tuiles, point) n'a pas de caractère en reçoit un nouveau, pris dans l'ordre `.#abc...z ABC...Z 0...9` puis des symboles.

### Exemples minimaux

Version 1, une pièce et son point d'entrée :

```json
{
  "version": 1,
  "tiles": { "floor": {}, "wall": { "walkable": false, "opaque": true } },
  "legend": {
    ".": ["floor"],
    "#": ["floor", "wall"],
    "@": { "tiles": ["floor"], "point": "start" }
  },
  "rows": [
    "#####",
    "#@..#",
    "#####"
  ]
}
```

Version 2, un morceau avec un objet et une porte au nord :

```json
{
  "version": 2,
  "chunk": true,
  "tiles": { "floor": {}, "wall": { "walkable": false, "opaque": true } },
  "legend": { ".": ["floor"], "#": ["floor", "wall"] },
  "rows": [
    "#.#",
    "#.#",
    "###"
  ],
  "objects": [
    { "id": "o1", "type": "torch", "x": 1.5, "z": 1.5, "rotation": 90, "props": { "radius": 4 } }
  ],
  "connectors": [
    { "name": "north_door", "x": 1, "z": 0, "direction": "north", "kind": "door" }
  ]
}
```

---

## 2. Tables de données

**Rôle** : les données du jeu (monstres, objets, compétences...) décrites en JSON, vérifiées au chargement, référencées entre elles par identifiant, rechargées à chaud, et compilées en binaire pour le jeu livré. **Le moteur ne connaît aucun champ** : le jeu enregistre chaque table (`DataTables::add<T>(nom, dossier, lecture, liens)`) avec une fonction qui lit une ligne par un `DataReader`. **Lecteurs** : `DataTableBase::read_json`, `DataTableBase::decode` (`src/moteur/data_table.cpp`), `DataReader` (`src/moteur/data_reader.cpp`). Exemple de table : `personnages` du bac à sable et de la tranche (`apps/tranche/sandbox_data.cpp`, `assets/data/personnages/tranche.json`).

### 2.1 Sources JSON

**Où** : une table est un **dossier** (`assets/data/<table>/`) ; tous ses fichiers `*.json` (pas les sous-dossiers) sont lus dans l'ordre de leurs noms et leurs lignes mises ensemble.

**Version** : chaque fichier doit avoir `"version": 1` (`kTableVersion`). Un fichier sans, ou d'une autre version, est écarté avec une erreur (`expected an object with "version": 1`).

**Clés d'un fichier** :

| Clé | Type | Défaut | Sens |
|---|---|---|---|
| `version` | entier | — (exigé) | 1 |
| `defaults` | objet | `{}` | champs de départ des lignes **de ce fichier** qui n'ont pas de `base` |
| `rows` | objet | — (exigé) | identifiant → ligne (un objet) |
| `_...` | toute valeur | — | commentaire, ignoré |

Toute autre clé au niveau du fichier : **avertissement** `unknown field at file level`.

**Règles des lignes** :

- l'identifiant (la clé dans `rows`) est non vide et **unique dans tout le dossier** (sinon erreur `identifier already defined in <fichier>`) ; une clé de `rows` qui commence par `_` est un commentaire ;
- une ligne est un objet ; avec `"base": "<identifiant>"`, elle part de la ligne résolue de cette base (d'abord résolue elle-même, plusieurs niveaux possibles, dans n'importe quel fichier de la table ; un cycle est refusé : `circular inheritance`) ; sans `base`, elle part des `defaults` de son fichier ;
- ses propres champs remplacent ensuite ceux de départ **au premier niveau** : un objet dans une ligne remplace l'objet de la base en entier, il n'est pas fusionné ;
- les clés de premier niveau qui commencent par `_` sont retirées de la ligne résolue (celles des objets imbriqués restent, mais `DataReader` ne les signale pas).

**Ordre** : au premier chargement, les lignes prennent leur index dans l'ordre de leurs identifiants ; un rechargement à chaud garde l'index de chaque ligne existante et ajoute les nouvelles à la fin. Un rechargement qui **retire** une ligne est refusé (`row removed: reload refused`) : il faut relancer.

**Lecture d'une ligne par le jeu** (`DataReader`) : chaque accès nomme sa clé ; une clé absente ou `null` est une erreur si elle est exigée (pas de valeur par défaut donnée par le jeu), sinon la valeur par défaut est prise. Les types :

| Accès | Type JSON attendu | Valeur fausse |
|---|---|---|
| `number(clé, min, max[, défaut])` | nombre fini dans `[min, max]` | erreur ; hors bornes : erreur, valeur ramenée dans les bornes |
| `integer(clé, min, max[, défaut])` | **entier** (`3.0` est refusé) dans `[min, max]` | erreur |
| `boolean(clé[, défaut])` | `true` / `false` | erreur |
| `text(clé[, défaut])` | texte | erreur |
| `choice(clé, {noms}[, défaut])` | un des textes donnés | erreur `'x' is not one of: ...` |
| `vec2`, `vec3` | liste de 2 ou 3 nombres finis | erreur |
| `color` | liste de 3 ou 4 nombres (alpha 1 si 3), sans conversion de couleur | erreur |
| `texts` | liste de textes | erreur (élément faux écarté) |
| `reference<T>(clé)` | identifiant (texte) d'une ligne d'une table de type `T` | résolue après la lecture de toutes les tables : `'x' does not exist in the table <nom>` ; vide : erreur sauf facultative |
| `references<T>(clé)` | liste d'identifiants | idem, pour chaque élément |
| `object(clé)`, `objects(clé)` | objet, liste d'objets | erreur |

Après la lecture, tout champ **jamais lu** (hors `_...`) donne un **avertissement** `unknown field (typo?)`. Les messages situent la valeur : `data/personnages/tranche.json > squelette > vitesse: 40 out of range [0, 30]`.

**Effet des erreurs** : au chargement complet, une ligne ou un fichier en erreur est écarté et l'erreur journalisée ; si la fonction du jeu signale une erreur, la table n'est pas chargée. Un **rechargement à chaud** qui a la moindre erreur est refusé (l'ancien contenu reste). La **compilation** (préparation des assets) échoue à la moindre erreur.

**Exemple : la table `personnages`** (champs du jeu, en français par décision ; lus par `parse_character`) :

| Clé | Accès | Bornes | Défaut |
|---|---|---|---|
| `nom` | texte | — | exigé |
| `modele` | texte | se termine par `.glb` ou `.gltf` | exigé |
| `taille` | nombre (m) | 0,1 à 20 | exigé |
| `rayon` | nombre (m) | 0,05 à 5 | exigé |
| `vitesse` | nombre (m/s) | 0 à 30 | exigé |
| `poids` | entier | 0 à 100 | exigé |
| `vie` | entier | 1 à 1 000 000 | exigé |
| `degats` | entier | 0 à 1 000 000 | exigé |
| `portee` | nombre (m) | 0 à 50 | exigé |
| `vue` | nombre (m) | 0 à 100 | `0` |
| `cadence` | nombre (s) | 0,05 à 60 | `1` |
| `effet_touche`, `effet_mort` | texte | vide, ou `effects/<nom>.json` | `""` |

Exemple minimal valide (extrait de `assets/data/personnages/tranche.json`) :

```json
{
  "version": 1,
  "_note": "keys starting with _ are comments",
  "defaults": { "vue": 9.0 },
  "rows": {
    "squelette": {
      "nom": "Squelette",
      "modele": "models/characters/kaykit_skeletons/Skeleton_Warrior.glb",
      "taille": 1.5, "rayon": 0.35, "vitesse": 2.0, "poids": 1,
      "vie": 100, "degats": 4, "portee": 0.2
    },
    "squelette_guerrier": { "base": "squelette", "nom": "Squelette guerrier" }
  }
}
```

`squelette_guerrier` reçoit tous les champs de `squelette` (y compris `vue`, venu des `defaults` de `squelette`) et remplace `nom`.

### 2.2 Forme compilée `.mdat`

**Rôle** : la forme binaire d'une table, déjà résolue (défauts et héritage appliqués, commentaires retirés), pour le jeu livré. Écrite par `DataTables::compile` (appelée par la préparation des assets à travers le rappel du jeu), un fichier `<table>.mdat` par table dans le dossier des tables de la sortie (`assets/data/` dans la tranche). Lue quand le jeu choisit `DataTables::set_source(DataSource::Compiled, dossier)` : le fichier est cherché dans les archives montées, puis sur le disque.

| Position | Taille | Champ |
|---|---|---|
| 0 | 4 | magie `MDAT` (ASCII) |
| 4 | 4 | version du format compilé, u32 = **1** (`kBinaryVersion`) |
| 8 | reste | charge utile **CBOR** (RFC 8949) : une table (map), voir ci-dessous |

Charge utile (clés écrites dans l'ordre alphabétique par nlohmann) :

| Clé | Type CBOR | Sens |
|---|---|---|
| `table` | texte | nom de la table ; doit être celui de la table qui lit |
| `source_hash` | entier non signé 64 bits | FNV-1a 64 des clés et des octets des fichiers JSON d'origine (dit si la forme compilée est périmée) |
| `ids` | liste de textes | identifiants, **triés** |
| `rows` | liste de maps | chaque ligne résolue, dans l'ordre de `ids` |
| `row_files` | liste de textes | le fichier source de chaque ligne (pour les messages) |
| `files` | liste de textes | les fichiers JSON de la table (clés d'assets), triés |

`ids`, `rows` et `row_files` ont la même longueur. Les lignes sont relues ensuite par la même fonction du jeu que le JSON (mêmes contrôles, mêmes avertissements).

**Versions** : le format compilé n'a **pas de migration** : une autre version est refusée (`unknown compiled format version N (expected 1): recompile the data`), il suffit de recompiler depuis les sources. Toute erreur (magie absente, fichier de moins de 8 octets, autre table, structure CBOR refusée, clé manquante, longueurs différentes) donne une erreur sur `<table>.mdat` et une table vide.

### 2.3 Contrôle de la structure CBOR

Avant de confier des octets CBOR à nlohmann (tables compilées et sauvegardes CBOR), le moteur les parcourt sans récursion (`check_cbor_structure`, `src/moteur/cbor_check.cpp`) : tout doit tenir dans les octets, et l'imbrication des **listes, maps et étiquettes** est limitée à **128 niveaux** (`kMaxCborDepth`), le document lui-même ne comptant pas. 128 niveaux sont acceptés, 129 refusés (`CBOR: nested deeper than 128 levels`). Raison : nlohmann lit le CBOR par récursion et une imbrication de quelques centaines de kilo-octets débordait la pile (trouvé par fuzzing, jalon 8 partie 8). Sont aussi refusés : document tronqué, `break` (0xFF) isolé, argument réservé (28 à 30), conteneur annoncé plus grand que les octets restants. Les longueurs indéfinies et les étiquettes passent le contrôle (nlohmann refuse ensuite les étiquettes).

Conséquence pour les données : la charge utile d'une table occupe déjà 3 niveaux (map, liste `rows`, ligne), une sauvegarde 4 (corps, `sections`, section, `data`) ; il reste donc environ 125 niveaux d'imbrication aux données du jeu. Le JSON texte n'a pas cette limite (le lecteur JSON de nlohmann n'est pas récursif).

---

## 3. Descriptions d'animation

**Rôle** : ce que le fichier glTF d'un personnage ne dit pas : vitesses au sol des clips, fondus, événements, espaces de mélange, masques, points d'attache, et le graphe d'animation. **Lecteurs** : `AnimationSet::parse` (`src/moteur/animation_set.cpp`) et `AnimationGraph::parse` (`src/moteur/animation_graph.cpp`). **Fichiers** : `assets/animations/*.json` (exemple : `kaykit.json`). Les noms de clips et d'articulations ne sont **pas vérifiés** contre un squelette à la lecture : l'`Animator` les résout quand il joue.

(Le fichier `assets/animations.json`, à la racine, est un autre format : les animations de sprites 2D, hors de ce document.)

### 3.1 Le fichier

**Version** : `AnimationSet::kVersion = 1`, seule lue ; une autre, ou une absence, est refusée (`format version N is not supported (expected 1)`).

| Clé | Type | Défaut | Sens, contraintes |
|---|---|---|---|
| `version` | entier | — (exigé) | 1 |
| `fade_ticks` | entier | `0` | fondu par défaut entre deux clips, en ticks ; ≥ 0 |
| `clips` | objet | `{}` | nom de clip → réglages (ci-dessous) ; un clip absent a les défauts |
| `blend_spaces` | objet | `{}` | nom → liste d'au moins **deux** noms de clips, placés sur l'axe de la vitesse par leur `ground_speed` |
| `masks` | objet | `{}` | nom → masque d'articulations (ci-dessous) |
| `upper_mask` | texte | `""` | le masque de la couche 1 (haut du corps) ; doit être dans `masks` |
| `attach_points` | objet | `{}` | nom (`right_hand`) → point d'attache (ci-dessous) |
| `graph` | objet | aucun graphe | le graphe d'animation, [3.2](#32-le-graphe-danimation) |

**Clip** (`clips.<nom>`) :

| Clé | Type | Défaut | Sens |
|---|---|---|---|
| `ground_speed` | nombre | `0` | mètres (du fichier) par seconde à vitesse 1, pour un clip qui avance sur place ; ≥ 0 ; 0 : ne bouge pas |
| `fade_ticks` | entier | `-1` | fondu vers ce clip, en ticks ; -1 : celui du fichier |
| `phase` | nombre | `0` | dans `[0, 1)` : décalage du clip dans le cycle d'un espace de mélange (pieds en phase) |
| `events` | liste | `[]` | événements, triés par temps à la lecture |

**Événement** (`events[i]`) : `time` (nombre, **exigé**, secondes depuis le début du clip, ≥ 0 ; au plus la durée du clip, vérifié quand il joue), `name` (texte, **exigé**, non vide : ce que reçoit le jeu), `always` (booléen, défaut `false` : se déclenche quel que soit le poids du clip, tant qu'il pèse ; sinon seulement s'il pèse la moitié ou plus).

**Espace de mélange** : deux clips de la liste ne peuvent pas avoir la même `ground_speed` (refus) ; un clip sans `ground_speed` est placé à 0 (un repos).

**Masque** (`masks.<nom>`) : `joint` (texte, **exigé** : cette articulation et toutes celles en dessous), `ramp` (entier, défaut `0`, ≥ 0 : les `ramp` premières articulations en dessous reçoivent 1/(ramp+1), 2/(ramp+1)...).

**Point d'attache** (`attach_points.<nom>`) : `joint` (texte, **exigé**), `position` (3 nombres, mètres du fichier, dans le repère de l'articulation, défaut `[0, 0, 0]`), `rotation` (3 angles en **degrés**, autour des axes fixes X, puis Y, puis Z, défaut aucun).

**Refus** (`Animation set '<fichier>': ...`) : JSON invalide, version fausse, `fade_ticks` négatif (fichier) ou inférieur à -1 (clip), `ground_speed` négative, `phase` hors de `[0, 1)`, événement sans nom ou à temps négatif, `ramp` négatif, `upper_mask` absent de `masks`, `position` ou `rotation` qui n'a pas 3 nombres, espace de mélange de moins de deux clips ou à deux vitesses égales, graphe invalide, valeur du mauvais type JSON. Les clés inconnues sont ignorées.

### 3.2 Le graphe d'animation

Sous la clé `graph` : quel mouvement chaque couche joue, selon des paramètres que le jeu règle (`Animator::set_parameter`, `Animator::fire`).

| Clé | Type | Défaut | Sens |
|---|---|---|---|
| `parameters` | objet | `{}` | nom → type : `"float"`, `"int"` (valeurs arrondies), `"bool"` (0 ou 1), `"trigger"` (un événement, levé pour une seule évaluation) |
| `layers` | liste | — (exigé) | **une ou deux** couches : la couche 0 pilote le corps entier, la couche 1 le haut du corps (avec `upper_mask`) |

**Couche** (`layers[i]`) :

| Clé | Type | Défaut | Sens |
|---|---|---|---|
| `name` | texte | `body` (couche 0), `upper` (couche 1) | pour les outils |
| `states` | objet | — (exigé, au moins un) | nom d'état → état |
| `start` | texte | — (exigé) | l'état d'entrée, doit être dans `states` |
| `transitions` | liste | `[]` | essayées par `priority` décroissante, puis dans l'ordre du fichier |

**État** (`states.<nom>`) :

| Clé | Type | Défaut | Sens |
|---|---|---|---|
| `motion` | texte | `""` | un clip, ou un espace de mélange de ce fichier ; vide : la couche ne joue rien (elle s'efface) |
| `loop` | booléen | `true` | `false` : joué une fois, puis la condition `end` est vraie |
| `restart` | booléen | `false` | y entrer rejoue le mouvement depuis le début même s'il joue déjà |
| `interruptible` | booléen | `true` | `false` : jusqu'à la fin de son clip, seules les transitions `interrupt` en sortent |
| `match_speed` | booléen | `false` | la vitesse de lecture suit la vitesse de déplacement (voir `PlayOptions::match_speed`) |

**Transition** (`transitions[i]`) :

| Clé | Type | Défaut | Sens |
|---|---|---|---|
| `from` | texte | — (exigé) | un état de la couche, ou `"*"` (n'importe lequel, sauf la cible elle-même) |
| `to` | texte | — (exigé) | un état de la couche |
| `when` | objet | toujours vraie | la condition, ci-dessous |
| `fade_ticks` | entier | `-1` | fondu en ticks ; -1 : celui du clip cible (ou le défaut du fichier si la cible ne joue rien) ; ≥ -1 |
| `priority` | entier | `0` | plus haute d'abord |
| `interrupt` | booléen | `false` | peut quitter un état non `interruptible` avant la fin de son clip |
| `self` | booléen | `false` | avec `"from": "*"`, peut aussi rentrer dans l'état courant (rejoue son mouvement) |

**Conditions** (structurées, jamais du texte à analyser) ; un objet qui contient exactement une de ces formes, essayées dans cet ordre :

| Forme | Sens |
|---|---|
| `{"all": [c1, c2, ...]}` | toutes vraies (liste non vide) |
| `{"any": [c1, c2, ...]}` | au moins une (liste non vide) |
| `{"not": c}` | le contraire (un déclencheur sous un `not` n'est pas consommé) |
| `{"end": true}` | le clip de la couche, joué une fois, est fini (ou la couche s'efface) ; seule la valeur `true` est permise |
| `{"param": "nom"}` | un `bool` ou un `trigger` levé ; refusé pour un nombre |
| `{"param": "nom", "op": "...", "value": v}` | comparaison, `op` parmi `==`, `!=`, `<`, `<=`, `>`, `>=` (exacte, en `double`) ; un `bool` ne prend que `==` / `!=` avec `true` / `false` ; un `trigger` ne prend jamais d'`op` |

**Évaluation** : les couches tour à tour, le corps d'abord ; la première transition vraie est prise ; un déclencheur est pris au plus une fois par couche (un `mort` fait tomber le corps **et** lâcher le haut) ; une couche enchaîne au plus 4 transitions par évaluation et n'entre au plus qu'une fois dans le même état.

**Refus** : `graph > layers[1] > transitions[0] > when > param: 'attaq' is not a parameter`, type de paramètre inconnu, zéro ou plus de deux couches, couche sans état, `start` / `from` / `to` inconnus, condition sans forme connue, `op` inconnu, `fade_ticks` < -1, mauvais type de `value`. Les mouvements (`motion`) ne sont pas vérifiés à la lecture (`Animator::start_graph` le fait).

### Exemple minimal

```json
{
  "version": 1,
  "fade_ticks": 12,
  "clips": {
    "Walking_A": { "ground_speed": 0.82, "events": [ { "time": 0.533, "name": "step_right" } ] },
    "1H_Melee_Attack_Chop": { "fade_ticks": 6, "events": [ { "time": 0.6, "name": "impact", "always": true } ] }
  },
  "blend_spaces": { "locomotion": ["Idle", "Walking_A"] },
  "masks": { "upper_body": { "joint": "spine", "ramp": 2 } },
  "upper_mask": "upper_body",
  "attach_points": { "right_hand": { "joint": "handslot.r", "position": [0, 0.0333, 0], "rotation": [0, 180, 0] } },
  "graph": {
    "parameters": { "attack": "trigger" },
    "layers": [
      { "start": "locomotion", "states": { "locomotion": { "motion": "locomotion" } } },
      {
        "start": "idle",
        "states": { "idle": {}, "attack": { "motion": "1H_Melee_Attack_Chop", "loop": false } },
        "transitions": [
          { "from": "idle", "to": "attack", "when": { "param": "attack" } },
          { "from": "attack", "to": "idle", "when": { "end": true } }
        ]
      }
    ]
  }
}
```

Le plus petit fichier valide est `{"version": 1}` (aucun réglage, pas de graphe).

---

## 4. Effets de particules

**Rôle** : effets légers et purement visuels (étincelles, flammes, fumée, sang), dessinés en billboards, en temps réel (images, pas ticks), sans effet sur la logique. **Lecteur** : `ParticleEffect::parse` (`src/moteur/particles.cpp`). **Fichiers** : `assets/effects/*.json`. **Version** : `ParticleEffect::kVersion = 1`, seule lue (`expected an object with "version": 1`).

| Clé | Type | Défaut | Sens |
|---|---|---|---|
| `version` | entier | — (exigé) | 1 |
| `emitters` | liste | — (exigée, non vide) | les émetteurs, joués ensemble |

**Émetteur** (`emitters[i]`) ; temps en secondes, distances en mètres, à l'échelle 1 (l'échelle donnée à `ParticleSystem::play` multiplie tailles, vitesses, rayons, gravité et portée de la lumière) :

| Clé | Type | Défaut | Sens, contraintes |
|---|---|---|---|
| `texture` | texte | — (exigé) | chemin d'asset de la texture (sRGB, alpha prémultiplié, chargée avec mipmaps) ; la préparation des assets l'embarque |
| `frames` | `[colonnes, lignes]` | `[1, 1]` | images dans la texture ; chaque valeur ramenée à 1 au moins |
| `animate` | booléen | `false` | `true` : les images dans l'ordre sur la vie ; `false` : une au hasard par particule |
| `burst` | entier | `0` | particules d'un coup au départ (et à chaque cycle en boucle) ; négatif → 0 |
| `rate` | nombre | `0` | particules par seconde pendant que l'émetteur tourne ; négatif → 0 |
| `duration` | nombre | `0` | secondes de fonctionnement (0 : la salve seule) ; en boucle, la durée d'un cycle ; négatif → 0 |
| `loop` | booléen | `false` | recommence jusqu'à `ParticleSystem::stop` |
| `lifetime` | plage | `[1, 1]` | vie d'une particule, au hasard entre les deux (au moins 0,01 s) |
| `shape` | texte | `"point"` | `point`, `sphere` (dans une boule de `radius`, vers l'extérieur), `disc` (disque horizontal de `radius`), `cone` |
| `radius` | nombre | `0` | taille de la forme, ou distance le long du cône |
| `direction` | 3 nombres | `[0, 1, 0]` | normalisée ; axe du cône, ou direction pour `point` et `disc` ; nulle → `[0, 1, 0]` |
| `angle` | nombre | `0` | demi-angle du cône, degrés, ramené dans `[0, 180]` |
| `speed` | plage | `[0, 0]` | vitesse de départ, m/s |
| `gravity` | nombre | `0` | m/s² vers le bas (négatif : monte) |
| `drag` | nombre | `0` | part de la vitesse perdue par seconde ; négatif → 0 |
| `size` | courbe de nombres | `0.1` | largeur et hauteur sur la vie, mètres |
| `color` | courbe de couleurs | blanc `[1, 1, 1, 1]` | RGBA linéaire, peut dépasser 1 ; `a` : opacité |
| `spin` | plage | `[0, 0]` | rotation, radians par seconde (sans effet en `upright`) |
| `additive` | booléen | `false` | mélange additif |
| `facing` | texte | `"camera"` | `camera`, `upright` (debout, tourné vers la caméra), `flat` (à plat) |
| `soft` | nombre | `0` | mètres de fondu avant la surface derrière (0 : bord dur) ; ≥ 0 |
| `light` | objet | aucune | une lumière ponctuelle portée par l'émetteur (ci-dessous) |

**Plage** (`lifetime`, `speed`, `spin`) : un nombre (`0.5`, min = max) ou deux (`[min, max]`).

**Courbe** (`size`, `color`) : soit une **valeur unique**, une constante sur toute la vie (`"size": 0.1`, `"color": [1, 0.5, 0.2, 1]`, quatre nombres pour une couleur) ; soit une liste de clés `[[temps, valeur], ...]`, temps dans `[0, 1]` de la vie (ramené dans l'intervalle), **croissants** (deux temps égaux sont permis ; sinon refus `times must go up`), valeurs linéaires entre deux clés, la première avant la première clé, la dernière après la dernière. Une liste dont le premier élément n'est pas une liste est lue comme une valeur unique : `"size": [0.2]` est refusé (`"size" needs a single value or keys [[time, value], ...]`).

**Lumière** (`light`) : `color` (3 nombres, RGB linéaire, défaut `[1, 1, 1]`), `intensity` (défaut `1`), `range` (mètres, défaut `5`), `flicker` (part de l'intensité qui vacille, ramenée dans `[0, 1]`, défaut `0`).

**Refus** (`Effect '<fichier>': ...`) : JSON invalide, version fausse, aucun émetteur, émetteur sans `texture`, émetteur qui n'émet rien (ni `burst`, ni `rate` avec une `duration` ou `loop`), `shape` ou `facing` inconnus, courbe vide (`[]`) ou à temps décroissants, valeur (unique ou d'une clé) qui n'est pas un nombre pour `size` ou une couleur de 4 nombres pour `color`, `soft` négatif, valeur du mauvais type. Les clés inconnues sont ignorées ; les valeurs numériques négatives sans sens sont ramenées comme indiqué.

### Exemple minimal

```json
{
  "version": 1,
  "emitters": [
    {
      "texture": "particles/spark.png",
      "burst": 26, "lifetime": [0.2, 0.45], "shape": "sphere", "radius": 0.05,
      "speed": [2.5, 6.0], "gravity": 9.8, "drag": 3.0,
      "size": [[0, 0.09], [1, 0.02]],
      "color": [[0, [8, 5, 2, 1.0]], [1, [2, 0.5, 0.1, 0]]],
      "additive": true,
      "light": { "color": [1.0, 0.8, 0.5], "intensity": 4, "range": 3 }
    }
  ]
}
```

(Le premier émetteur de `assets/effects/sparks.json`.) Le plus petit : `{"version": 1, "emitters": [{"texture": "particles/spark.png", "burst": 10}]}`.

---

## 5. Liaisons de touches

**Rôle** : relier les **actions** que le jeu déclare (`Input::add_button`, `Input::add_axis`) aux contrôles physiques, clavier, souris et manette ensemble, en un ou plusieurs **profils** (« déplacement au clic », « déplacement au clavier »). **Lecteur et écrivain** : `src/moteur/input.cpp`. **Fichiers** : les défauts du jeu dans ses assets (`assets/input/demo3d.json`, `assets/input/world.json`), et un fichier du joueur dans les préférences, au nom choisi par le jeu (bac à sable : `demo3d_bindings.json`).

### 5.1 Forme texte des sources

Une source est un texte `appareil:nom` (`parse_input_source`, réécrite par `input_source_text`) :

| Forme | Sens | Exemples |
|---|---|---|
| `key:<nom>` | une touche par sa **position** (nom de *scancode* SDL, comme sur un clavier QWERTY américain : `key:Q` est la touche marquée A sur un AZERTY) | `key:W`, `key:Space`, `key:Escape`, `key:Return`, `key:Tab`, `key:Up`, `key:Keypad Enter` |
| `mouse:<bouton>` | un bouton de la souris : `left`, `right`, `middle`, `x1`, `x2` | `mouse:left` |
| `wheel:up`, `wheel:down` | un cran de la molette (un appui et un relâchement à la fois) | `wheel:up` |
| `pad:<bouton>` | un bouton de manette (nom SDL ; positions : `a` est le bouton du bas, Croix sur une manette PlayStation) : `a`, `b`, `x`, `y`, `back`, `start`, `leftstick`, `rightstick`, `leftshoulder`, `rightshoulder`, `dpup`, `dpdown`, `dpleft`, `dpright`... | `pad:a`, `pad:dpup` |
| `pad:<axe>+`, `pad:<axe>-` | une direction d'un stick ou d'une gâchette utilisée comme un bouton, enfoncée au-delà de `Input::axis_press_threshold` : axes `leftx`, `lefty`, `rightx`, `righty`, `lefttrigger`, `righttrigger` | `pad:lefty-`, `pad:righttrigger+` |

L'appareil (`key`, `mouse`, `wheel`, `pad`) ne tient pas compte de la casse, les noms de souris et de molette non plus. Les noms de touches et de manette sont résolus par SDL (`SDL_GetScancodeFromName`, `SDL_GetGamepadButtonFromString`, `SDL_GetGamepadAxisFromString`) : écrire la forme que le moteur réécrit (`key:Space`, pas `key:space`). Une source inconnue refuse le fichier (`profile 'clic', action 'skill_1': unknown source 'key:Foo'`).

### 5.2 Fichier des liaisons par défaut

**Version** : `version` doit valoir l'entier 1 (`format version N is not supported (expected 1)`).

| Clé | Type | Défaut | Sens |
|---|---|---|---|
| `version` | entier | — (exigé) | 1 |
| `profile` | texte | le premier profil par ordre alphabétique | le profil choisi ; doit être dans `profiles` |
| `profiles` | objet | — (au moins un profil) | nom de profil → profil |

**Profil** (`profiles.<nom>`) :

| Clé | Type | Défaut | Sens |
|---|---|---|---|
| `label` | texte | le nom du profil | ce que le jeu montre au joueur |
| `buttons` | objet | `{}` | nom d'action bouton → liste de sources |
| `axes` | objet | `{}` | nom d'action axe → liaison d'axe (ci-dessous) |

**Liaison d'axe** (`axes.<action>`) : `up`, `down`, `left`, `right` (listes de sources, chacune `[]` par défaut) et `stick` (`"left"` ou `"right"`, la manette ; absent : aucun stick).

**Ce que fait le lecteur** : refus (`Bindings '<fichier>': ...`) pour un JSON invalide, une autre version, aucun profil, un `profile` absent des profils, une source inconnue, un `stick` autre que `left` / `right`, une liste de sources qui n'est pas une liste. Une action liée que le jeu n'a pas déclarée (ou un bouton lié sous `axes`, ou l'inverse) est **notée au journal** et ignorée (`'x' is not a button action of the game`) : une faute de frappe, ou une liaison d'une autre version du jeu. Une action déclarée absente du profil n'a simplement aucune source. Les clés inconnues sont ignorées.

Exemple minimal :

```json
{
  "version": 1,
  "profile": "click",
  "profiles": {
    "click": {
      "label": "Déplacement au clic",
      "buttons": { "move_to": ["mouse:left"], "skill_1": ["key:Q", "pad:a"] },
      "axes": { "move": { "up": ["key:W"], "down": ["key:S"], "left": ["key:A"], "right": ["key:D"], "stick": "left" } }
    }
  }
}
```

### 5.3 Fichier des liaisons du joueur

Même format, appliqué **par-dessus** les défauts (`Input::apply_user_bindings`, après `load_bindings`) : chaque liaison qu'il donne remplace celle du défaut pour cette action, son `profile` choisit le profil. Il ne contient d'ordinaire que quelques entrées. Différences avec les défauts : aucun profil n'est exigé ; un profil absent des défauts, ou un `profile` inconnu, est ignoré avec une ligne au journal (pas de refus) ; les erreurs de forme (JSON, version, source inconnue) refusent quand même le fichier, et le jeu garde alors les défauts. Le moteur ne choisit ni le nom ni l'emplacement du fichier : le jeu l'écrit avec `Input::user_bindings_json()` (indenté de 2 espaces), qui contient `version`, `profile` si le joueur en a choisi un, et `profiles` avec seulement les liaisons changées (un axe est toujours écrit avec ses quatre listes).

```json
{
  "version": 1,
  "profile": "zqsd",
  "profiles": { "zqsd": { "buttons": { "skill_1": ["key:A"] } } }
}
```

### 5.4 Enregistrement des entrées (rejeu)

**Rôle** : ce que la logique a lu de l'`Input`, tick après tick, pour rejouer une partie à l'identique (tests, captures de référence, rapports de bogue). **Lecteur et écrivain** : `input_recording_from_json`, `input_recording_to_json` (`input.cpp`) ; fichiers donnés par `ApplicationConfig::record_input_path` et `replay_input_path` (options `--record-input`, `--replay-input` des programmes ; exemple : `tests/data/slice_replay.json`). **Version** : 1 (autre : refus). JSON compact, une ligne.

| Clé | Type | Sens |
|---|---|---|
| `version` | entier | 1 |
| `actions` | liste de textes | les noms des actions ; les entrées des images suivent cet ordre (un enregistrement survit à l'ajout d'actions au jeu) |
| `axes` | liste de booléens | par action : un axe plutôt qu'un bouton |
| `frames` | liste d'objets | une par tick, depuis le premier |

**Image** (`frames[i]`), seul ce qui n'est pas au repos est écrit :

| Clé | Type | Sens |
|---|---|---|
| `p` | `[x, y]` | le pointeur, en pixels de la fenêtre ; négatif : hors de la fenêtre (exigé) |
| `b` | liste de `[action, enfoncé, appuis, relâchements]` | boutons : index d'action, 0 ou 1, nombres d'appuis et de relâchements depuis le tick précédent |
| `a` | liste de `[action, x, y]` | axes non nuls |
| `c` | liste de textes | commandes de jeu de la console lancées au début de ce tick |

Un index d'action hors de la liste refuse le fichier (`an action index is out of range`).

```json
{"version": 1, "actions": ["move_to"], "axes": [false], "frames": [{"p": [640.0, 360.0]}, {"p": [640.0, 360.0], "b": [[0, 1, 1, 0]]}]}
```

---

## 6. Sauvegardes

**Rôle** : la partie du joueur : un **en-tête** (ce qu'un menu montre) et des **sections** nommées, une par système, chacune avec sa propre version. Le même contenu s'écrit en **JSON** lisible (développement) ou en **CBOR** compact (jeu livré) ; la lecture prend les deux. **Lecteur et écrivain** : `SaveGame`, `SaveSlots`, `SaveMigrations` (`src/moteur/save_file.cpp`) ; état du monde : `ComponentSerializers` (`src/moteur/world_save.cpp`).

### 6.1 Le conteneur `.sav`

| Position | Taille | Champ |
|---|---|---|
| 0 | 4 | magie `MSAV` (ASCII) |
| 4 | 4 | version du conteneur, u32 = **1** (`SaveGame::kFormatVersion`) |
| 8 | 1 | encodage, u8 : `0` JSON, `1` CBOR (`SaveEncoding`) |
| 9 | 4 | longueur de l'en-tête `H`, u32 |
| 13 | `H` | en-tête, dans l'encodage choisi |
| 13 + `H` | 4 | longueur du corps `B`, u32 |
| 17 + `H` | `B` | corps, dans l'encodage choisi |
| 17 + `H` + `B` | 4 | CRC-32 de **tous les octets qui précèdent**, u32 |

Taille totale : exactement 21 + `H` + `B` octets (un octet de plus ou de moins : `save file truncated`). En JSON, en-tête et corps sont indentés d'une tabulation par niveau ; tous les textes doivent être de l'UTF-8 valide.

**Refus**, jamais de lecture partielle (`SaveGame::decode` ne lance pas d'exception, il rend un message) : moins de 21 octets ou magie absente (`not a save file`), conteneur plus récent que le jeu (`save file from a newer format (N): update the game` ; les versions plus anciennes sont acceptées), encodage autre que 0 ou 1 (`unknown encoding`), longueurs qui ne tombent pas juste (`save file truncated`), somme de contrôle fausse (`save file damaged (checksum mismatch)`), contenu illisible (`unreadable header: ...`, `unreadable content: ...`, y compris le [contrôle CBOR](#23-contrôle-de-la-structure-cbor)), section impossible à migrer. L'en-tête se lit seul (`SaveGame::decode_header`, pour lister les emplacements), mais toujours après le contrôle de la somme du fichier entier.

### 6.2 En-tête et corps

**En-tête** (rempli par le jeu ; le moteur n'y met rien lui-même ; une clé absente prend son défaut) :

| Clé | Type | Défaut | Sens |
|---|---|---|---|
| `game_version` | texte | `""` | version du jeu, pour les messages |
| `time` | entier | `0` | date d'écriture, secondes depuis 1970 (UTC) ; ordonne la liste des emplacements |
| `play_seconds` | nombre | `0` | temps de jeu, secondes |
| `summary` | objet | `{}` | ce que le jeu veut montrer (lieu, niveau, personnage) |

**Corps** : `{"sections": {<nom>: {"version": <entier>, "data": <toute valeur>}, ...}}`. `sections` et, dans chaque section, `version` et `data` sont exigés. Les noms de sections sont ceux du jeu (bac à sable : `monde`, `scene`).

Exemple (encodage JSON ; en-tête puis corps) :

```json
{
	"game_version": "0.3",
	"play_seconds": 754.5,
	"summary": {"lieu": "Crypte"},
	"time": 1792252800
}
```

```json
{
	"sections": {
		"monde": {"data": [], "version": 1}
	}
}
```

### 6.3 Versions et migrations

Deux niveaux de version :

- **le conteneur** (`kFormatVersion`, aujourd'hui 1) : la disposition des octets ; un conteneur plus récent est refusé ;
- **chaque section** : sa `version` part de 1. Le jeu enregistre au démarrage une fonction par pas (`SaveMigrations::add(section, de, fonction)` : de la version `n` à `n + 1`, qui réécrit `data` en place). La version courante d'une section est la cible du dernier pas (1 sans pas). À la lecture avec les migrations, chaque section est montée pas à pas jusqu'à la version courante ; une section **plus récente** que le jeu (`newer than the game`), un **pas manquant** (`no migration from version N`) ou un pas qui lance une exception refusent le fichier entier. Sans migrations données, les sections sont lues telles quelles.

Le jeu écrit chaque section avec la version courante (`SaveGame::set_section(nom, migrations.current(nom), data)`).

### 6.4 Emplacements, écriture atomique, copie de secours

`SaveSlots` gère un dossier (choisi par le jeu ; la tranche : `sauvegardes/` des préférences) :

| Fichier | Sens |
|---|---|
| `<nom>.sav` | l'emplacement ; `<nom>` est le nom de l'emplacement, en UTF-8, tel quel |
| `<nom>.sav.bak` | la sauvegarde précédente, gardée par l'écriture |
| `<nom>.png` | la vignette, écrite par le jeu |

**Écriture atomique** (`write_file_atomic`, `file_io.cpp`) : le contenu va dans `<nom>.sav.tmp`, est vidé sur le disque (`FlushFileBuffers` sous Windows, `fsync` sous macOS), puis prend la place de `<nom>.sav` en une opération (`ReplaceFileW` sous Windows, l'ancien fichier devenant `<nom>.sav.bak` ; sous macOS, un lien physique garde l'ancien en `<nom>.sav.bak`, puis `rename`). Si `<nom>.sav` n'existait pas, le fichier temporaire est simplement renommé. Un échec (disque plein, dossier en lecture seule) laisse le fichier précédent intact et donne la raison. **Lecture** : si `<nom>.sav` est absent ou abîmé, `<nom>.sav.bak` est lu à la place (le jeu peut le savoir : `from_backup`). La liste (`SaveSlots::list`) montre aussi les emplacements illisibles, la plus récente d'abord (`time`, puis le nom).

### 6.5 Section du monde (composants du moteur)

`ComponentSerializers::save` écrit les entités qui ont un `PersistentId`, dans l'ordre de leurs identifiants : une liste `[{"id": <entier ≠ 0>, "c": {<clé de composant>: <valeur>, ...}}, ...]`. Le jeu range cette liste dans la section de son choix et ajoute ses propres composants. Depuis le jalon 9 (partie 3), chaque composant décrit est écrit **par sa description** (réflexion, [13](#13-valeurs-décrites-par-la-réflexion)) : un objet de ses champs marqués « sauvegardés », sous le nom de son type dans `component_types()`. Composants du moteur et leurs valeurs (les noms de composants ne sont jamais renommés ; un champ renommé garde la lecture de son ancien nom) :

| Clé | Composant | Valeur (objet) | Formes plus anciennes encore lues |
|---|---|---|---|
| `transform` | `Transform` | `position` `[x, y, z]`, `rotation` `[w, x, y, z]` (ordre de glm), `scale` `[x, y, z]` | `p`, `r`, `s` |
| `name` | `Name` | `value` (texte) | le texte seul |
| `hidden` | `Hidden` | `{}` (étiquette sans donnée) | `true` |
| `parent` | `Parent` | `entity` : identifiant persistant du parent (un parent absent de la sauvegarde est un problème signalé ; `0` : aucun) | l'identifiant seul |
| `bone_attachment` | `BoneAttachment` | `point` (texte) | le texte seul |
| `light` | `LightSource` | `color` `[r, g, b]`, `intensity`, `range`, `casts_shadows`, `shadow_near` | `[[r, g, b], intensité, portée, ombres]` |
| `collider` | `Collider` | `radius`, `layer`, `mask`, `push_weight`, `blocked_by_walls` | `[rayon, couche, masque, poids, murs]` |
| `mover` | `Mover` | `speed`, `turn_rate`, `stop_distance`, `field`, `mode`, `goal`, `direction`, `path` (liste de `[x, z]`), `next_point`, `path_status`, `needs_path`, `state`, `facing`, `actual_speed`, `tick_start`, `slow_ticks` | les clés courtes du jalon 7 : `turn`, `stop`, `next`, `status`, `actual`, `start`, `slow` |
| `animator` | `Animator` | l'état interne de l'animateur (`Animator::save_state`) : à ne pas écrire à la main ; la fonction de création du jeu doit créer l'`Animator` | |

Les **énumérations** sont écrites par leur nom (`mode` : `stop`, `to_point`, `direct`, `follow_field` ; `path_status` : `found`, `partial`, `none` ; `state` : `idle`, `waiting`, `moving`, `arrived`, `blocked`, `no_path`) ; les nombres des sauvegardes de développement du jalon 7 sont encore lus ; un nom inconnu est un problème de ce composant. Les nombres à virgule sont écrits exactement (relus bit pour bit). Un champ inconnu (retiré depuis l'écriture de la sauvegarde) est ignoré sans avertissement. Les champs `DataRef` (une ligne de table) sont écrits par identifiant et, avec `ComponentSerializers::set_tables()`, rattachés à leur ligne à la lecture (une ligne absente est un problème de ce composant).

**Chargement** (`ComponentSerializers::load`) : le jeu recrée chaque entité depuis son enregistrement, puis les composants reçoivent leurs valeurs, puis les références entre entités sont résolues par identifiant persistant. Les problèmes sont **listés une ligne chacun**, et le reste est chargé ; aucun ne lance d'exception. Problèmes : liste d'entités qui n'est pas une liste (rien n'est chargé) ; enregistrement qui n'est pas un objet, sans `id` ou dont l'`id` n'est pas un entier positif ou nul (`record N: no "id" (a non-negative integer)`) ; enregistrement sans `c` ou dont le `c` n'est pas un objet (`entity N: no "c" (an object of components)`) ; entité que le jeu ne sait pas recréer ; composant inconnu ; référence à une entité absente ; valeur fausse. Un enregistrement abîmé (`id` ou `c` absent ou du mauvais type) est sauté : son entité n'est pas recréée.

---

## 7. Archives `.pak`

**Rôle** : les assets et les shaders du jeu livré, regroupés en quelques fichiers, lus par `read_file()` à travers un système de fichiers virtuel. **Lecteur** : `Archive::open`, `Archive::read` (`src/moteur/archive.cpp`). **Écrivain** : `write_archive`, appelé par la préparation des assets. **Fichiers** : `archives/*.pak` à côté de l'exécutable (`ApplicationConfig::archives_directory`, défaut `"archives"`) ; la tranche : `audio.pak`, `personnages.pak`, `common.pak`. **Version** : `Archive::kVersion = 1` ; une autre est refusée (`version N, 1 expected`) : pas de migration, on prépare de nouveau.

### Disposition

En-tête (24 octets) :

| Position | Taille | Champ |
|---|---|---|
| 0 | 4 | magie `MPAK` (ASCII) |
| 4 | 4 | version, u32 = 1 |
| 8 | 4 | nombre d'entrées `N`, u32 |
| 12 | 4 | taille de l'index en octets `I`, u32 |
| 16 | 8 | FNV-1a 64 bits des `I` octets de l'index, u64 |

Index (à la position 24, `I` octets) : `N` entrées à la suite, **triées par chemin** (ordre des octets UTF-8), chacune de 31 + `L` octets :

| Position dans l'entrée | Taille | Champ |
|---|---|---|
| 0 | 2 | longueur du chemin `L`, u16 (65 535 au plus) |
| 2 | `L` | chemin, UTF-8, séparateur `/`, casse gardée (`assets/maps/tranche.json`, `shaders/...`) |
| 2 + `L` | 8 | position des octets stockés dans le fichier, u64 |
| 10 + `L` | 8 | taille stockée (compressée ou non), u64 |
| 18 + `L` | 8 | taille d'origine (ce que rend la lecture), u64 |
| 26 + `L` | 1 | compression, u8 : `0` aucune, `1` Zstandard |
| 27 + `L` | 4 | CRC-32 des octets **d'origine**, u32 |

Données : chaque entrée commence à une position **alignée sur 4 096 octets** (`Archive::kAlignment`), dans l'ordre de l'index ; la première après `aligner(24 + I)` ; l'espace entre deux entrées est rempli de zéros ; le fichier finit juste après la dernière entrée.

**Règles d'écriture** : les entrées sont triées par chemin (ordre stable : la modification d'un asset ne décale pas les autres, ce qui sert aux mises à jour par blocs de Steam) ; deux chemins égaux, ou qui ne diffèrent que par la **casse**, sont refusés (`'A' and 'a' collide`), parce que Windows et macOS ne les distingueraient pas sur le disque (le contrôle ne compare que deux chemins voisins après le tri : voir la remarque en fin de document) ; chaque entrée est compressée en **Zstandard niveau 19** si elle le demande et si elle rétrécit, sinon stockée telle quelle ; les extensions déjà compressées (`.ktx2`, `.ogg`, `.mp3`, `.flac`, `.png`, `.jpg`, `.jpeg`, toute casse) ne sont pas compressées (`archive_should_compress`) ; le fichier est écrit de façon atomique.

**Lecture** : à l'ouverture, l'en-tête et l'index seulement ; refus avec message si le fichier n'est pas une archive (`not an archive of the engine`), d'une autre version, si l'index sort du fichier ou si son empreinte ne correspond pas (`damaged index`), si une entrée déborde, si l'index n'est pas **strictement trié** par chemin, un chemin en double compris (`index not sorted ('a' after 'b')` : la recherche d'une entrée se fait par dichotomie), si une entrée a une compression autre que 0 ou 1 (`'x': unknown compression N`), ou si le fichier est plus court que ce que l'index annonce (`truncated (X bytes, Y expected)` : une copie interrompue). Chaque lecture d'entrée ouvre son propre descripteur (sûr entre fils), compare la taille d'origine à ce que les trames Zstandard peuvent contenir avant d'allouer, décompresse, puis contrôle le **CRC-32** (`damaged (CRC32)`). Les chemins sont comparés **exactement**, casse comprise. Deux chemins qui ne diffèrent que par la casse (`A` et `a`, triés et distincts) ne sont pas refusés à l'ouverture : seul l'écrivain les refuse.

**Montage** : `mount_archives(dossier)` monte tous les `*.pak` du dossier dans l'ordre de leurs noms ; une recherche commence par la **dernière** archive montée, si bien qu'une archive de correctif dont le nom vient après remplace une entrée. Un chemin de fichier n'est cherché dans les archives que s'il est sous le dossier du programme ; sa clé est le chemin relatif à ce dossier, normalisé (`a/./b`, `a/b/../c`), avec des `/`. En version livrée, une fois des archives montées, rien d'autre n'est lu ; une archive abîmée arrête le programme avec un message qui conseille de réinstaller.

---

## 8. Préparation des assets

**Rôle** : transformer les sources de développement en données prêtes à charger, puis les ranger en archives (`moteur_cook`, `tranche_cook` ; `cook_main` et `cook` dans `src/moteur/cook.cpp`). Ligne de commande :

```
moteur_cook --sources DOSSIER... --manifest FICHIER --output DOSSIER [--shaders DOSSIER] [--force] [--orphans]
```

`--sources` : un ou plusieurs dossiers d'assets, dans l'ordre (un fichier d'un dossier suivant remplace celui d'un précédent). `--force` : tout refaire. `--orphans` : lister les sources que rien ne livre. Code de sortie 0 sans erreur, 1 avec, 2 pour une ligne de commande fausse.

### 8.1 Manifeste de livraison

Un fichier JSON des assets du jeu, **non livré lui-même** (exemple : `assets/livraison_tranche.json`). **Version** : `version` doit valoir 1, sinon la préparation s'arrête (`version 1 expected`).

| Clé | Type | Défaut | Sens | Ancienne clé |
|---|---|---|---|---|
| `version` | entier | — (exigé) | 1 | — |
| `include` | liste de motifs | `[]` | fichiers à livrer, relatifs aux dossiers de sources | `inclure` |
| `exclude` | liste de motifs | `[]` | retirés de ce que `include` choisit | `exclure` |
| `data` | texte | `""` (pas de tables) | dossier des tables de données : leur JSON reste derrière, les `.mdat` compilés vont dans ce dossier de la sortie | `donnees` |
| `check` | objet | ci-dessous | dossiers dont les fichiers sont relus avec leur lecteur | `verifier` |
| `archives` | liste d'objets | `[]` | groupes d'archives, ci-dessous | — |

`check` : `maps` (défaut `"maps/"`, ancienne clé `cartes`), `effects` (défaut `"effects/"`, ancienne clé `effets`), `animations` (défaut `"animations/"`) ; un `.json` dont la clé commence par ce préfixe est relu par `MapData::parse`, `ParticleEffect::parse` ou `AnimationSet::parse`.

Groupe d'archive (`archives[i]`) : `name` (texte, défaut `"unnamed"`, ancienne clé `nom` : le fichier `<name>.pak`) et `include` (liste de motifs, ancienne clé `inclure`). Chaque fichier préparé va dans le **premier** groupe dont un motif correspond (motifs appliqués à la clé sans le préfixe `assets/`), le reste et les shaders dans `common.pak`.

Une ancienne clé française donne un avertissement par emploi (`livraison_tranche.json: key 'inclure' is deprecated, write 'include'`) ; si la clé anglaise est là aussi, elle seule est lue. Un motif d'`include` qui ne choisit rien donne un avertissement (`'x' matches no file`). Les autres clés sont ignorées.

**Ce qui est livré en plus** : les **dépendances**, sans tenir compte d'`exclude` : images d'un glTF dans leurs propres fichiers (avec une texture `KHR_texture_basisu`, la version KTX2 seulement, les PNG de repli restent derrière), textures des effets de particules, et pour un préfabriqué ses bases, ses préfabriqués imbriqués et les assets de ses composants, **par identifiant** ([15.4](#154-préparation-et-outil)) ; une dépendance absente est une **erreur**. Sont toujours écartés : le manifeste lui-même (s'il est à la racine d'un dossier de sources), tout le dossier `data` et les `.meta` (le [registre figé](#153-registre-figé-asset_registrymreg) les remplace).

**Traitement** : les KTX2 en Basis Universal (UASTC, ETC1S) aux côtés multiples de 4 deviennent du **BC7** (ou **BC5** s'ils ont deux canaux), supercompressés en Zstandard ; chaque modèle glTF est **importé** avec les réglages de son `.meta` et livré sous sa forme préparée seulement (`.imported/<clé>.mmdl`, [16](#16-modèles-préparés-mmdl)), ses textures intégrées transcodées de même ; chaque `.json` est écrit en **CBOR** sous son nom ([17](#17-documents-livrés-en-cbor)) ; les autres fichiers sont copiés ; les tables sont compilées par le rappel du jeu ; les shaders sont copiés ; le registre des assets livrés est figé dans `asset_registry.mreg` ; chaque fichier de la sortie est **relu** par le lecteur du moteur (modèles préparés, KTX2, PNG / JPEG, WAV / OGG / FLAC, cartes, effets, descriptions d'animation, préfabriqués avec leurs bases et leurs assets, tout autre `.json`, qui doit être du CBOR) ; un modèle, un son, un environnement ou une police (`models/`, `audio/`, `environments/`, `*.ttf`) qu'aucune entrée de crédits (`file` ou `license_file` commençant par `assets/`, couvrant son dossier) ne couvre donne un **avertissement**. Les archives ne sont écrites que s'il n'y a eu **aucune erreur**.

Exemple minimal :

```json
{
  "version": 1,
  "include": ["maps/tranche.json", "effects/*.json", "audio/**", "credits.json"],
  "exclude": ["**/*.blend"],
  "data": "data",
  "archives": [ { "name": "audio", "include": ["audio/**"] } ]
}
```

### 8.2 Motifs (globs)

`glob_match` compare un motif à une clé (chemin relatif, `/`, casse comprise) :

| Élément | Sens |
|---|---|
| `**/` | zéro, un ou plusieurs dossiers (`**/*.blend` : tout `.blend`, à la racine compris) |
| `**` sans `/` après | n'importe quels caractères, `/` compris ; la suite du motif doit encore correspondre : `audio/**` en fin de motif prend tout ce qui est dessous, `audio/**.ogg` seulement les `.ogg` dessous, à toute profondeur |
| `*` | n'importe quels caractères **dans un nom** (ne traverse pas `/`) |
| autre caractère | lui-même, exactement (`?` et `[...]` n'ont pas de sens spécial) |

`audio/**.ogg` et `audio/**/*.ogg` choisissent les mêmes fichiers.

### 8.3 Sortie et `cook_state.json`

Dans le dossier `--output` :

| Chemin | Contenu |
|---|---|
| `assets/` | les fichiers préparés, à leurs clés (et les `.mdat` dans le dossier `data`) |
| `shaders/` | les shaders compilés copiés |
| `archives/<name>.pak`, `archives/common.pak` | les archives (dossier vidé puis réécrit à chaque préparation sans erreur) |
| `cook_state.json` | l'état de la préparation incrémentale |

Les fichiers de `assets/` qu'aucune source ne produit plus sont supprimés.

**`cook_state.json`** (écrit à chaque préparation, de façon atomique, indenté d'un espace) :

| Clé | Type | Sens |
|---|---|---|
| `version` | entier | version de l'outil (`kCookVersion` = 2 ; 2 : modèles livrés préparés) ; une autre valeur fait tout refaire |
| `files` | objet | clé de fichier livré → empreinte : le FNV-1a 64 bits des octets de la source, amorcé avec la version de l'outil, écrit en **texte décimal** ; pour un modèle préparé (clé `.imported/<clé>.mmdl`), l'empreinte couvre aussi ses tampons, ses réglages d'import et la version des modèles préparés ; `"tables"` pour un `.mdat` compilé, `"registry"` pour le registre figé |
| `transcoded` | liste de textes | les clés des textures (`.ktx2`) dont la sortie a été transcodée en BC7 / BC5 ; les autres ont été copiées telles quelles |

Un fichier n'est refait que si son empreinte a changé ou si sa sortie manque. Une texture n'est jamais reprise d'une préparation précédente si `transcoded` manque (un état écrit avant cette clé) : toutes les textures sont alors préparées de nouveau, une fois. Un `transcoded` illisible rend tout l'état illisible. Un `cook_state.json` illisible est signalé au journal et tout est refait (toujours juste, seulement plus lent). Ce fichier est un détail de l'outil : il ne se lit ni ne s'écrit à la main.

```json
{
 "files": {
  "animations/kaykit.json": "1059289548811934333",
  "data/personnages.mdat": "tables",
  "models/polyhaven/boulder_01/textures/boulder_01_diff_1k.ktx2": "8820476311392871610"
 },
 "transcoded": [
  "models/polyhaven/boulder_01/textures/boulder_01_diff_1k.ktx2"
 ],
 "version": 2
}
```

---

## 9. Réglages du joueur et fichiers du programme

### 9.1 Le dossier des préférences

Le dossier de l'utilisateur (`SDL_GetPrefPath(organisation, application)` ; sous Windows `%APPDATA%\<organisation>\<application>\`, par exemple `%APPDATA%\moteur\tranche\`). Ce que le moteur y range :

| Fichier | Format | Écrit |
|---|---|---|
| `display.json` | [9.2](#92-displayjson) | à chaque `Application::apply_display` (si `ApplicationConfig::display_settings`) |
| `audio.json` | [9.3](#93-audiojson) | à la fin, si les réglages ont changé |
| `variables.cfg` | [9.4](#94-variablescfg) | à la fin |
| `log.txt`, `log.1.txt` | journal texte (`HH:MM:SS.mmm [niveau] message`), le précédent en `.1` | pendant l'exécution |
| `crash_reports/` | [10](#10-rapports-de-plantage) | à un plantage |
| `profiles/profile.json` | capture du profiler au format Chrome Trace (commande `profile stop`, à ouvrir dans ui.perfetto.dev) | sur demande |
| `imgui.ini` | réglages de Dear ImGui | développement seulement |

Le jeu y ajoute les siens (liaisons du joueur, dossier des sauvegardes). Les anciens noms (`affichage.json`, `journal.txt`, `plantages/`) restent sur le disque mais ne sont plus écrits ; seul `affichage.json` est encore lu.

### 9.2 `display.json`

**Rôle** : les options d'affichage du joueur, appliquées sans redémarrer. **Lecteur** : `DisplaySettings::from_json`, **écrivain** : `DisplaySettings::to_json` (`src/moteur/display.cpp`). Le fichier s'appelait `affichage.json` jusqu'au 2026-10-17 : si `display.json` manque, `affichage.json` est encore lu à sa place (`Application`, `application.cpp`), puis les réglages sont réécrits sous le nouveau nom ; l'ancien fichier n'est ni réécrit ni supprimé.

| Clé | Type | Défaut | Sens, contraintes | Ancienne clé (valeurs) |
|---|---|---|---|---|
| `version` | entier | `1` si absent | `1` seulement (`DisplaySettings::kVersion`) ; une autre valeur, ou une valeur qui n'est pas un entier, donne les réglages par défaut avec un avertissement (`display settings: version N is not supported (expected 1), defaults used`) ; absent (les premiers fichiers) : lu comme 1 | — |
| `mode` | texte | `"windowed"` | `windowed` (fenêtré) ou `fullscreen` (plein écran fenêtré, sans bordure, à la résolution de l'écran ; pas de plein écran exclusif) | `mode` (`fenetre`, `plein_ecran`) |
| `display` | entier | `0` | index de l'écran (0 : le principal) ; ramené à 0 au moins, puis aux écrans branchés à l'application | `ecran` |
| `vsync` | booléen | `ApplicationConfig::vsync` (`true` par défaut) | attend le rafraîchissement de l'écran | `synchronisation` |
| `frame_limit` | entier | `0` | sans `vsync`, images par seconde au plus (0 : aucune limite) ; ramené dans `[0, 1000]` | `limite_images` |
| `render_scale` | nombre | `1` | résolution de la 3D, ramenée dans `[0.5, 1]` (l'interface reste nette) ; écrite arrondie au centième | `resolution_rendu` |
| `anti_aliasing` | texte | `"fxaa"` | `none`, `fxaa`, `msaa2`, `msaa4` | `anticrenelage` (mêmes valeurs) |
| `shadows` | texte | `"high"` | `low` (carte du soleil 1 024, 1 lumière ponctuelle ombrée, 256 par face), `medium` (2 048, 2, 512), `high` (8 192, 8, 2 048 : ce que le jeu demande) | `ombres` (`basses`, `moyennes`, `hautes`) |

**Les défauts** sont ceux que donne le jeu, pas seulement ceux du tableau : une clé absente garde la valeur de départ, et `vsync` part de `ApplicationConfig::vsync` (les autres clés partent des valeurs de `DisplaySettings`, celles du tableau).

**Le lecteur ne lance jamais d'exception** : un texte qui n'est pas du JSON, ou pas un objet, donne les défauts et un avertissement ; une version inconnue aussi (voir `version`) ; une valeur de texte inconnue (`mode`, `shadows`, `anti_aliasing`) garde le défaut avec un avertissement (`unknown window mode 'x'`) ; un nombre hors bornes est ramené dans les bornes ; une valeur du mauvais type JSON (`"vsync": 1`, `"display": "1"`, `"render_scale": "0.9"`) garde le défaut avec un avertissement qui nomme la clé (`display settings: 'vsync' should be true or false, not 1; default kept`). Si la clé anglaise est là mais du mauvais type, l'ancienne clé française n'est pas lue à sa place. Les avertissements vont au journal (`Display: ...`). Écriture : indentée de 2 espaces, clés triées.

Exemple (un fichier réel de la tranche) :

```json
{
  "anti_aliasing": "fxaa",
  "display": 0,
  "frame_limit": 0,
  "mode": "fullscreen",
  "render_scale": 0.9,
  "shadows": "high",
  "version": 1,
  "vsync": false
}
```

### 9.3 `audio.json`

**Lecteur et écrivain** : `Audio::apply_settings_json`, `Audio::settings_json` (`src/moteur/audio.cpp`). **Version** : `version` doit valoir 1, sinon le fichier est refusé et les volumes par défaut gardés (journalisé).

| Clé | Type | Sens |
|---|---|---|
| `version` | entier | 1 |
| `master` | nombre | volume général, ramené dans `[0, 1]` |
| `volumes` | objet | par groupe : `music`, `effects`, `ambience`, `interface`, chacun ramené dans `[0, 1]` |
| `mute_in_background` | booléen | couper le son quand la fenêtre n'a pas le focus |

Chaque clé est facultative (absente : la valeur courante est gardée). Une valeur du mauvais type refuse le fichier.

```json
{
  "version": 1,
  "master": 0.8,
  "volumes": { "music": 0.6, "effects": 1.0, "ambience": 0.7, "interface": 1.0 },
  "mute_in_background": true
}
```

### 9.4 `variables.cfg`

**Rôle** : les valeurs des **variables de console archivées** (déclarées avec `Variable::Archive`) qui diffèrent de leur défaut. **Lecteur** : `Variables::set_pending` au démarrage ; **écrivain** : `Variables::archived_text` à la fin (`src/moteur/variables.cpp`, appelés par `application.cpp`). Pas de version : un format texte, une commande par ligne.

```
set debug.stats "1"
set save.auto "60"
```

**Écriture** : une ligne `set <nom> "<valeur>"` par variable archivée différente de son défaut, triées par nom, puis les lignes gardées pour des variables que cette exécution n'a pas déclarées (elles ne sont pas perdues). La valeur est toujours entre guillemets, **échappée** : `\` devient `\\`, `"` devient `\"`, un saut de ligne devient `\n` et un retour chariot `\r` (une barre oblique inverse et une lettre) ; tout texte tient ainsi sur sa ligne.

**Lecture** : ligne par ligne, `\r` final retiré ; une ligne vide (ou d'espaces) et une ligne dont le premier caractère visible est `#` sont sautées sans message. Le premier mot doit être `set` et il faut un deuxième mot, le nom ; sinon la ligne est ignorée avec une ligne au journal (`Variables: line ignored: ...`). Le reste de la ligne, espaces de tête retirés, est la valeur :

- sans guillemet en tête, la valeur est le reste de la ligne tel quel ;
- entre guillemets, `\\`, `\"`, `\n` et `\r` sont rendus ; une autre barre oblique inverse reste elle-même (un chemin Windows écrit avant les échappements) ; le guillemet fermant doit finir la ligne, sinon la ligne est ignorée avec une ligne au journal (`badly quoted value, line ignored`).

Seules les variables **archivées** sont lues de ce fichier : une variable déclarée sans `Variable::Archive` qu'il nomme est ignorée avec une ligne au journal (`Variables: 'x' is not kept in the preferences: ignored`). La valeur d'une variable archivée déjà déclarée est appliquée tout de suite ; celle d'un nom pas encore déclaré est gardée et appliquée à sa déclaration (avec le même contrôle). Valeurs : booléen `1`, `0`, `on`, `off`, `true`, `false`, `oui`, `non` ; entier ou nombre dans les bornes de la variable (refusé, pas ramené) ; texte quelconque. Une valeur refusée garde la valeur de la variable, avec une ligne au journal (`Variables: <nom>: <raison> (value ignored)`).

(La version livrée accepte aussi des lignes `+set nom valeur` sur la ligne de commande, et seulement celles-là.)

### 9.5 Rapport de mesure (JSON, version 2)

**Rôle** : la mesure d'une exécution (scènes de mesure, budgets du jalon 8 partie 4). Écrit à la fin de la boucle par `Application::run` (`src/moteur/application.cpp`) dans `ApplicationConfig::report_json_path` (option `--report-json FICHIER` du bac à sable et de la tranche) ; lu par `tools/measures/measure.py`. **Version 2** : clés anglaises (la version 1 avait des clés françaises et n'est plus produite ni lue). Indenté de 2 espaces. Les 60 premières images ne sont pas mesurées (mise en route).

| Clé | Type | Sens |
|---|---|---|
| `version` | entier | 2 |
| `engine` | texte | `moteur 0.8.0 (<commit>, <date>, <configuration>)` |
| `platform` | texte | nom de l'OS selon SDL (`Windows`, `macOS`) |
| `gpu` | texte | pilote graphique de SDL et nom du GPU |
| `window_pixels` | `[largeur, hauteur]` | taille de rendu, pixels |
| `vsync` | booléen | la synchronisation **appliquée** (réglages du joueur, ou configuration) |
| `frames_measured` | entier | images mesurées |
| `frames_skipped_at_start` | entier | 60 |
| `ticks` | entier | ticks de logique joués |
| `cpu_ms` | objet | temps CPU par image, ms : `mean`, `p50`, `p99`, `max` |
| `interval_ms` | objet | intervalle entre deux images, ms : `mean`, `p50`, `p99`, `max` |
| `phases_ms` | objet | moyennes par image, ms : `logic`, `record`, `submit` |
| `frames_over` | objet | images au-delà d'un seuil : `cpu_16_7_ms`, `cpu_33_3_ms`, `interval_16_7_ms` (seuil 16,67 + 0,5 ms), `interval_33_3_ms` |
| `zones` | liste | zones du profiler : `name`, `mean`, `p99`, `max` (ms), `per_frame` (appels par image) |
| `per_frame` | objet | moyennes par image : `sprites`, `draw_calls`, `point_shadow_draw_calls`, `point_shadows_redrawn`, `meshes_drawn`, `triangles`, `scene_draw_calls`, `shadow_draw_calls`, `kib_uploaded` |
| `gpu_ms` | objet | seulement avec la mesure GPU (`gpu_timing`) : `uploads`, `shadow`, `point_shadows`, `scene`, `compose`, `total` (ms par image, approchés) |
| `memory_mib` | objet | Mio : `process`, `process_peak`, `gpu_textures`, `gpu_targets`, `gpu_buffers`, `gpu_peak` |
| `values` | objet | valeurs du jeu (`Application::report_value(nom, valeur)`), nom → nombre ; noms au choix du jeu |

### 9.6 Crédits (`credits.json`, `credits_moteur.json`)

**Rôle** : ce dont le programme est fait et qui a fait ce qu'il montre, pour son écran « À propos » : bibliothèques et polices avec leurs licences, auteurs des assets. **Lecteur** : `Credits::add_file` / `Credits::load` (`src/moteur/credits.cpp`). **Fichiers** : `credits_moteur.json` (le moteur, dans `assets_moteur/`), puis `credits.json` (le jeu), tous deux trouvés dans les assets du programme ; les listes des fichiers sont mises bout à bout dans cet ordre. Pas de version lue (les fichiers du projet portent `"version": 1` en tête, ignoré).

Listes facultatives au niveau du fichier : `libraries` (code tiers), `fonts`, `models` (modèles et leurs textures), `environments` (cartes d'environnement HDR), `sounds` (sons et musique). Chaque entrée est un objet dont **chaque champ est facultatif** (texte, vide si absent) :

| Clé | Sens |
|---|---|
| `name` | ce qui est crédité |
| `version` | sa version, telle qu'écrite (dans une entrée ; à ne pas confondre avec la clé de tête du fichier) |
| `license` | nom ou identifiant SPDX de la licence (`zlib`, `OFL-1.1`, `CC0`) |
| `copyright` | la ligne de copyright |
| `authors` | qui l'a fait, en un texte |
| `source` | d'où il vient (`Poly Haven`, une boutique, « fait pour le jeu ») |
| `url` | lien vers le projet ou la page de l'asset |
| `license_file` | texte complet de la licence, chemin relatif au dossier de l'exécutable (`licenses/imgui.txt`) |
| `file` | l'asset, chemin relatif au dossier de l'exécutable (montré présent ou manquant) |
| `platform` | `windows` ou `macos` : l'entrée n'est gardée que sur cet OS |
| `build` | `DEVELOPMENT` (ancien nom `DEVELOPPEMENT`) : gardée seulement hors version livrée (Dear ImGui, efsw, PIX) ; `MOTEUR_TRACY` : seulement avec le profiler Tracy ; **toute autre valeur écarte l'entrée** |

**Erreurs** : jamais d'exception ; un fichier absent ou illisible ajoute une ligne à `Credits::error` (`Cannot read the credits (<fichier>): ...`) ; les listes de ce fichier déjà ajoutées avant le problème restent. Une valeur qui n'est pas un texte rend le fichier illisible à partir de là. La préparation des assets se sert de `file` et `license_file` (commençant par `assets/`) pour vérifier que chaque asset livré est couvert ([8.1](#81-manifeste-de-livraison)).

```json
{
  "version": 1,
  "libraries": [
    { "name": "Dear ImGui", "version": "1.92.9", "license": "MIT", "url": "https://github.com/ocornut/imgui",
      "license_file": "licenses/imgui.txt", "build": "DEVELOPMENT" }
  ],
  "models": [
    { "name": "Wine Barrel 01", "authors": "James Ray Cock", "license": "CC0", "source": "Poly Haven",
      "file": "assets/models/polyhaven/wine_barrel_01/wine_barrel_01_1k.gltf" }
  ]
}
```

---

## 10. Rapports de plantage

**Rôle** : qu'un plantage du jeu livré laisse de quoi le comprendre. **Écrivain** : `src/moteur/crash.cpp` (`install_crash_reporter`, appelé par `Application` si `ApplicationConfig::crash_reports`) ; **lecteur** : `tools/crash/read_report.py RAPPORT SYMBOLES` (ajoute fonction, fichier et ligne à chaque ligne de pile, avec dbghelp sous Windows et `atos` sous macOS). **Où** : `crash_reports/` du dossier des préférences (créé à l'installation), un rapport par événement, nommé d'après l'heure locale à la seconde : `2026-10-05_20-48-03.txt` et, sous Windows, `2026-10-05_20-48-03.dmp`. Le `.txt` et le `.dmp` d'un même événement ont toujours le même nom. Les noms sont **uniques** : si un rapport de la même seconde existe déjà (une perte du GPU, puis un plantage en sortant), le suivant reçoit `_2`, puis `_3`... (`2026-10-05_20-48-03_2.txt`), jusqu'à 1 000 par seconde. Un rapport existant n'est jamais écrasé (création exclusive : `CREATE_NEW` sous Windows, `O_EXCL` sous macOS) ; un nom dont seul le `.dmp` existe est aussi considéré comme pris. Pas de version : le rapport est fait pour être lu, et le script s'appuie sur les lignes de pile.

**Événements** : exception non gérée (Windows : filtre d'exceptions ; macOS : signaux `SIGSEGV`, `SIGBUS`, `SIGILL`, `SIGFPE`, `SIGABRT`), `abort` et assertion de la version livrée, `std::terminate` (dont une exception C++ non attrapée), et les erreurs fatales signalées par le moteur (`report_fatal_error` : perte du périphérique GPU). Ensuite un message au joueur (`ApplicationConfig::crash_message`, `{report}` remplacé par le chemin du rapport ; supprimé si la variable d'environnement `MOTEUR_CRASH_SILENT` existe).

**Rapport texte** (UTF-8, en anglais ; exemple d'une violation d'accès provoquée, abrégé) :

```
Crash report
program: Tranche jouable
engine: moteur 0.8.0 (7cba197+, 2026-10-04, Shipping)
system: Windows, 16 logical cores, 32535 MB of memory
gpu: direct3d12, NVIDIA GeForce RTX 4070 Ti SUPER
event: crash
code: 0xc0000005 (access violation)
address: 0x7ff6b5af0b0f
access: write at 0x0

--- stack (module+offset; function when the symbols are there)
#00 tranche.exe+0x60b0f moteur::crash_now (...\src\moteur\crash.cpp:458)
#01 tranche.exe+0x332e
#02 KERNEL32.DLL+0x2cd87 BaseThreadInitThunk+0x17

--- log (last lines)
21:07:02.141 [info] moteur 0.8.0 (7cba197+, 2026-10-04, Shipping), SDL 3.4.16, Windows
...
```

| Ligne | Sens |
|---|---|
| `Crash report` | première ligne, toujours |
| `program:` | titre du programme (`ApplicationConfig::title`) |
| lignes de contexte `clé: valeur` | dans l'ordre où elles ont été données (`set_crash_context`) : `engine` (version, commit, date, configuration), `system` (OS, cœurs logiques, mémoire en Mo), `gpu` (pilote SDL et nom du GPU, une fois le renderer créé), `assertion` (le texte d'une assertion qui a échoué) ; un jeu peut en ajouter ; 4 096 octets au plus en tout |
| `event:` | `crash` (exception), `std::terminate`, `abort (assertion, or an error of the C library)`, `uncaught C++ exception: <message>`, `signal N (SIGSEGV) at 0x...` (macOS), `GPU device lost: <raison>` |
| `code:` | Windows, exception : code et nom (`access violation`, `stack overflow`, `integer division by zero`, `illegal instruction`, `uncaught C++ exception`, `stop (abort or terminate)`, `exception`) |
| `address:` | Windows, exception : adresse de l'instruction fautive |
| `access:` | Windows, violation d'accès : `read` ou `write` et l'adresse visée |
| `--- stack ...` | 64 cadres au plus. Windows : `#NN module+0xDÉCALAGE`, suivi de `fonction (fichier:ligne)` ou `fonction+0xDÉPLACEMENT` quand les symboles sont là. macOS : `#NN module+0xDÉCALAGE (base 0x..., address 0x...) symbole`, en-tête `--- stack (module+offset; atos -o <symbols> -l <base> <address>)` |
| `--- log (last lines)` | les **200** dernières lignes du journal, chacune coupée à 239 octets |

**Minidump** (Windows) : écrit par `MiniDumpWriteDump` avec `MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules` : fils, piles, modules (environ 170 Ko), pas toute la mémoire. Il se lit avec Visual Studio ou WinDbg et les symboles archivés de la même version (PDB). macOS n'écrit pas de `.dmp` : le système écrit son propre rapport (`.ips`).

---

## 11. Textures KTX2 acceptées

**Lecteur** : `decode_ktx2` et `transcode_ktx2_for_shipping` (`src/moteur/ktx_texture.cpp`), avec libktx. Avant libktx, `validate_header` vérifie l'en-tête et **refuse** tout ce que le moteur ne gère pas, et toute taille qui ne tombe pas juste (libktx alloue les tailles déclarées avant de contrôler quoi que ce soit : un fichier abîmé lui faisait demander 4 Go, trouvé par fuzzing). Message : `KTX2 texture '<nom>': damaged or unsupported header: <raison>`.

**Champs de l'en-tête KTX 2.0 contrôlés** (spécification KTX 2.0, section 3 ; 80 octets, puis l'index des niveaux) :

| Position | Taille | Champ | Accepté |
|---|---|---|---|
| 0 | 12 | identifiant `«KTX 20»\r\n\x1A\n` | exigé (sinon `not a KTX2 file`) |
| 12 | 4 | `vkFormat` | `0` (Basis Universal), `37` / `43` (R8G8B8A8 UNORM / SRGB), `141` (BC5 UNORM), `145` / `146` (BC7 UNORM / SRGB) |
| 16 | 4 | `typeSize` | non contrôlé |
| 20 | 4 | `pixelWidth` | 1 à 16 384 |
| 24 | 4 | `pixelHeight` | 1 à 16 384 |
| 28 | 4 | `pixelDepth` | 0 (pas de texture 3D) |
| 32 | 4 | `layerCount` | 0 (pas de tableau) |
| 36 | 4 | `faceCount` | 1 (pas de cube map) |
| 40 | 4 | `levelCount` | 0 ou 1 à `1 + log2(max(largeur, hauteur))` (0 compte pour 1) |
| 44 | 4 | `supercompressionScheme` | 0 aucune, 1 BasisLZ (seulement avec `vkFormat` 0), 2 Zstandard, 3 ZLIB |
| 48, 52 | 4 + 4 | position et longueur du descripteur (DFD) | dans le fichier |
| 56, 60 | 4 + 4 | position et longueur des clés et valeurs | dans le fichier |
| 64, 72 | 8 + 8 | position et longueur des données globales | dans le fichier |
| 80 | 24 par niveau | index des niveaux : `byteOffset`, `byteLength`, `uncompressedByteLength` (u64 chacun) | voir ci-dessous |

**Chaque niveau de mipmap** doit être dans le fichier, rangé **avant** le niveau plus grand qui le précède dans l'index (la spécification range les niveaux du plus petit au plus grand ; libktx compte dessus), et avoir la **taille exacte** de son format : 16 octets par bloc de 4 × 4 pixels (arrondis au bloc supérieur) en UASTC, BC7 et BC5, 4 octets par pixel en RGBA8 ; `uncompressedByteLength` doit valoir cette taille (0 en BasisLZ, comme le veut la spécification) et, sans supercompression, `byteLength` aussi.

**Au chargement** : une texture Basis Universal (UASTC ou ETC1S) est transcodée pour le GPU : **BC7** (ou **BC5** pour deux canaux) si ses côtés sont multiples de 4 et que le GPU les lit, sinon **RGBA8** (deux canaux : `(x, y, 0, 255)`). Une texture déjà en BC7, BC5 ou RGBA8 est envoyée telle quelle, refusée si le GPU ne lit pas son format. **sRGB ou linéaire** est choisi au chargement par l'usage (`TextureSettings::srgb` : couleur ou donnée), pas par le format du fichier. La version livrée reçoit des KTX2 déjà transcodés en BC7 / BC5 UNORM, supercompressés en Zstandard (niveau 18), par la préparation ; un UASTC aux côtés non multiples de 4 reste tel quel.

---

## 12. Modèles glTF acceptés

**Lecteur** : `load_gltf` (fichier) et `parse_gltf` (mémoire) dans `src/moteur/model.cpp`, avec cgltf. Fichiers `.gltf` (avec leurs `.bin` et images à côté, ou en URI `data:` base64) et `.glb`, **glTF 2.0** seulement (glTF 1.0 refusé). Le fichier est validé (`cgltf_validate`). Seul ce que le moteur utilise est lu.

| Élément | Pris en charge | Remarques |
|---|---|---|
| Scène | la scène par défaut, sinon la première, sinon tous les nœuds racines | les nœuds hors de cette scène sont ignorés |
| Primitives | **triangles** seulement | une autre topologie est sautée, avec une ligne au journal ; sans indices, les sommets dans l'ordre ; un triangle final incomplet est ignoré |
| Attributs | `POSITION` (exigé), `NORMAL` (calculées, lissées, si absentes), `TEXCOORD_0`, `TANGENT` (calculées en MikkTSpace si absentes), `JOINTS_n` / `WEIGHTS_n` | entiers normalisés acceptés ; accesseurs creux (*sparse*) lus par cgltf ; un indice hors des sommets est refusé |
| Matériaux | PBR métal / rugosité : couleur de base (facteur, texture sRGB), métal et rugosité, normale (échelle), occlusion (force), émission (facteur, texture sRGB, `KHR_materials_emissive_strength`), double face | |
| Alpha | `OPAQUE` ; **`MASK`** avec `alphaCutoff` (défaut glTF 0,5 ; une coupure de 0 devient 1/512, la plus petite qui coupe) ; **`BLEND`** : avertissement au journal (`material 'x' is BLEND (transparent), not supported yet: drawn opaque`), dessiné opaque | |
| Textures | PNG, JPEG (stb_image) ; **KTX2** par `KHR_texture_basisu` (la version KTX2 préférée, l'image de repli sinon) | voir [11](#11-textures-ktx2-acceptées) ; `KHR_texture_transform` et les jeux de coordonnées autres que 0 ne sont pas lus |
| Squelettes (*skins*) | au moins **1** articulation (sinon `skin 'x' has no joints`), **256** au plus ; matrices de liaison inverses (identité si absentes ; moins de matrices que d'articulations : refus) ; articulations dans la scène par défaut | les **4** influences les plus fortes par sommet (avec `JOINTS_1` / `WEIGHTS_1`), poids normalisés ; un sommet sans poids suit la première articulation (journal) ; `JOINTS_n` doit être un `VEC4` et couvrir tous les sommets ; un sommet qui vise une articulation hors du squelette est refusé |
| Maillage non pondéré sous une articulation | suit l'articulation la plus proche au-dessus de lui | |
| Animations | canaux `translation`, `rotation`, `scale` des nœuds du squelette ; interpolations `LINEAR`, `STEP` (paires de clés linéaires à 0,1 ms d'écart), `CUBICSPLINE` (échantillonnée à 60 Hz et à chaque clé) | les temps commencent à la première clé du clip ; durée minimale 1/60 s ; les canaux de poids (*morph targets*) sont sautés avec une ligne au journal ; un clip qui ne bouge aucune articulation est sauté ; des noms de clips en double reçoivent ` (2)`, ` (3)`... |
| **Limites de temps** | chaque temps de clé **fini** et d'au plus **3 600 s** en valeur absolue (`an animation sampler has a time beyond 3600 s`), strictement croissant (`times that do not increase`), assez de valeurs pour les temps (trois par clé en `CUBICSPLINE`) | protège l'échantillonnage à 60 Hz (trouvé par fuzzing) |
| Extensions **requises** | `KHR_texture_basisu` (exigée par la spécification quand une texture n'a que sa version KTX2, sans image de repli) et `KHR_materials_emissive_strength` ; toute autre extension de `extensionsRequired` refuse le fichier en la nommant (`required extensions not supported: KHR_draco_mesh_compression` : Draco, meshopt, maillages quantifiés...) | les extensions facultatives inconnues sont ignorées |

**Refus** (`Model '<fichier>': ...`) : fichier tronqué, pas du glTF, JSON invalide, glTF invalide, fichier référencé manquant ou illisible, image sans données ni URI ou à URI `data:` qui n'est pas en base64, extension requise non prise en charge, squelette ou animation mal formés, et, quand les maillages sont demandés, aucune primitive triangulaire dans la scène par défaut (`no triangle mesh in the default scene`). Avec `GltfOptions::meshes = false` (fichier chargé pour ses seules animations), un fichier sans maillage est accepté.

---

## 13. Valeurs décrites par la réflexion

**Lecteur et écrivain** : `read_json`, `read_fields`, `write_json` (`src/moteur/reflection.cpp`, en-têtes `reflection.hpp` et `reflection_io.hpp`), jalon 9 partie 3. Toute donnée d'un type **décrit** (`MOTEUR_REFLECT`) s'écrit de la même façon, qu'elle soit dans une table de données (`add_reflected_table`), une sauvegarde (`ComponentSerializers::add_reflected`), un préfabriqué ([14](#14-préfabriqués-prefabjson)) ou un niveau :

| Type C++ | JSON |
|---|---|
| `bool` | `true` / `false` |
| entiers (8 à 64 bits) | un entier, dans les bornes du champ et du type (`300 out of range [0, 255]`) |
| `float`, `double` | un nombre fini, dans les bornes du champ |
| `std::string` | une chaîne |
| `glm::vec2/3/4`, `glm::ivec2/3` | une liste de 2, 3 ou 4 nombres (une couleur `vec4`, champ marqué couleur, peut omettre son alpha : 1) |
| `glm::quat` | `[w, x, y, z]` |
| énumération décrite | son **nom** ; l'entier d'une sauvegarde de développement plus ancienne est encore lu |
| structure décrite | un objet de ses champs ; les clés qui commencent par `_` sont des commentaires |
| `std::vector` | une liste |
| `std::optional` | `null` quand vide, sinon la valeur |
| `DataRef<T>` | l'identifiant de la ligne |
| `entt::entity` | sauvegarde : l'identifiant persistant (`0` : aucune) ; préfabriqué : l'identifiant de l'entité dans le fichier (`2`, ou `"3/2"` dans un préfabriqué imbriqué) |
| `Asset<T>` | `{"id": "<UUID>", "path": "models/props/lampadaire.glb"}` : l'**identifiant** de l'asset ([15](#15-identité-des-assets--meta-et-registre)), son chemin à côté pour la lecture humaine ; un chemin seul (écrit à la main, ou un asset sans `.meta`) ou un identifiant seul sont lus aussi ; `null` ou `""` : aucun |

Lecture : un champ absent garde sa valeur par défaut (celle de la structure), sauf s'il est **requis** (`required field missing`) ; un champ peut avoir d'**anciens noms** (alias), lus quand le nom actuel manque (les deux présents : avertissement, le nom actuel gagne) ; une clé qui n'est aucun champ est un avertissement (`unknown field (typo?)`) dans les données, ignorée dans les sauvegardes. Les messages sont ceux de `DataReader` (chemin `fichier > ligne > champ`, bornes). Seuls les champs marqués pour l'usage sont lus et écrits (`kFieldData` pour les données, `kFieldSave` pour les sauvegardes).

**Schémas** : `json_schema()` donne un schéma JSON (brouillon 2020-12) d'un type décrit ; une table lue par la réflexion le porte (`DataTableBase::schema()`, pour la forme du fichier entier : `version`, `defaults`, `rows`, et `base` dans une ligne) ; `bac_a_sable --write-schemas DOSSIER` écrit `<table>.schema.json` pour chaque table décrite (à associer aux fichiers `data/<table>/*.json` dans l'éditeur de texte : `json.schemas` de VS Code).

---

## 14. Préfabriqués `.prefab.json`

**Lecteur** : `Prefab::parse` (`src/moteur/prefab.cpp`), par `Assets::prefab()` ; instanciation par `instantiate()` (jalon 9, partie 3). Un modèle d'entités : une racine, des enfants, leurs composants (par leur nom dans `component_types()`, valeurs comme en [13](#13-valeurs-décrites-par-la-réflexion), champs marqués « données »).

```json
{
  "version": 1,
  "base": "prefabs/porte.prefab.json",
  "overrides": {"2": {"light": {"intensity": 20}}, "4/2": {"light": {"color": [0.2, 0.4, 1]}}},
  "removed": ["3"],
  "entities": [
    {"id": 5, "parent": 1, "name": "bannière", "components": {"transform": {"position": [0, 2, 0]}}},
    {"id": 6, "parent": 1, "prefab": "prefabs/lampadaire.prefab.json",
     "components": {"transform": {"position": [-1, 0, 0]}},
     "overrides": {"2": {"light": {"intensity": 5}}}}
  ]
}
```

| Clé | Type | Rôle |
|---|---|---|
| `version` | entier | `1` (une autre version est refusée) |
| `base` | chemin | facultatif : ce préfabriqué est une **variante** de celui-là ; il en part (entités, composants, surcharges déjà fusionnées) |
| `overrides` | objet | variante seulement : `chemin d'entité → composant → champs`, fusionnés champ par champ dans ceux de la base (un objet fusionne, une liste ou une valeur remplace) |
| `removed` | liste | variante seulement : entités de la base retirées, avec tout ce qui est sous elles |
| `entities` | liste | les entités du fichier (ajoutées à celles de la base pour une variante) |
| `entities[].id` | entier > 0 | identifiant **stable** dans le fichier (les surcharges, niveaux et sauvegardes y renvoient) ; unique, y compris face à la base |
| `entities[].parent` | identifiant | l'entité parente (du fichier ou de la base) ; **exactement une** entité n'en a pas : la racine |
| `entities[].name` | texte | facultatif : donné en composant `Name` à l'instanciation (sauf si l'entité a déjà un `name`) |
| `entities[].components` | objet | `nom du composant → valeur` (seuls les champs qui changent sont nécessaires) |
| `entities[].prefab` | chemin | facultatif : l'entité est une **instance imbriquée** de ce préfabriqué ; sa racine devient cette entité (ses `components` par-dessus ceux de la racine), ses entités prennent le chemin `id/identifiant intérieur` (`"6/2"`) |
| `entities[].overrides` | objet | instance imbriquée seulement : `chemin intérieur → composant → champs` |

**Ce que fait le lecteur** : la base et les préfabriqués imbriqués sont chargés (et gardés) par le gestionnaire d'assets ; un cycle (`Prefab cycle: a > b > a`) est refusé. Tout est fusionné, puis chaque composant est lu **une fois** par sa description (l'instanciation ne fait que copier) ; les champs d'asset (le modèle d'un `model`) sont chargés et tenus par le préfabriqué. Un composant de nom inconnu est un **avertissement** (il est laissé de côté) ; tout le reste est une **erreur** et le préfabriqué est refusé avec toutes ses erreurs : JSON invalide, autre version, `overrides` ou `removed` sans `base`, identifiant non entier, nul ou déjà pris, parent inconnu, plusieurs racines ou aucune, cycle de parents, entité de surcharge inconnue, valeur fausse, référence d'entité vers une entité qui n'existe pas (une variante qui retire une entité référencée doit aussi effacer la référence). Rechargé à chaud avec ses dépendances (son fichier, ses bases, ses imbriqués) ; les entités déjà créées ne changent pas.

**Instanciation** (`instantiate(world, prefab, placement)`) : une entité par entité du préfabriqué, ses composants, son `Parent` (la racine : celui du placement, s'il en donne un), son `Name` ; la racine reçoit la transformation du placement **composée** avec la sienne ; `moving` lui donne un `PreviousTransform`. Les champs d'entité sont reliés aux nouvelles entités. `placement.overrides` (même forme que `overrides`) change des champs pour cette instance seulement : c'est ce qu'un niveau garde de chaque instance.

Composants du moteur utilisables dans un préfabriqué (en plus de ceux du jeu) : `transform`, `name`, `hidden`, `parent` (rarement : la hiérarchie le donne), `bone_attachment`, `light`, `halo`, `collider` (avec `height`, la hauteur de la capsule dans un niveau 3D), `mover`, `model` (`model` : référence du glTF ; `hidden_parts` : indices dans les parties du modèle), `static_collision` (la pièce fait partie de la géométrie de collision du niveau : `from_model`, la collision de son modèle, et `boxes`, des boîtes `{"centre", "half_size", "top", "sides"}` dans son espace, genres `"walkable"`, `"wall"`, `"obstacle"`, `"void"`).

Sauvegardes : le `mover` écrit avant la partie 5 du jalon 9 (`goal`, `path`, `tick_start` sur le plan : `[x, z]`) se lit encore, en points 3D `[x, 0, z]` (`upgrade_mover`).

---

## 15. Identité des assets : `.meta` et registre

**Rôle** (jalon 9, partie 4) : chaque asset a un **identifiant permanent** (UUID v4), écrit dans un fichier voisin `<asset>.meta` ; ce qui désigne un asset (préfabriqués, niveaux, champs `Asset<T>`) le fait par cet identifiant : renommer ou déplacer un fichier avec son `.meta` ne casse rien. **Lecteur** : `parse_asset_meta`, `AssetRegistry::scan` (`src/moteur/asset_registry.cpp`). **Écrivains** : `asset_meta_text` (le moteur en développement), `tools/assets/assets.py`.

**Ce qui est un asset** (`is_identified_asset`) : tout fichier des dossiers d'assets, sauf les `.meta`, les tampons glTF (`.bin`, partie de leur modèle), les tables de données (`data/`, désignées par leurs lignes), la documentation (`.md`, `.txt`) et les fichiers et dossiers cachés (nom commençant par `.` : `.git`, `.imported`…).

### 15.1 Fichier `.meta`

```json
{
  "id": "6f1c2a4e-9b7d-4c1e-8a2f-3d5e7f901b2c",
  "import": {"lods": 2, "textures": "ktx2"},
  "version": 1
}
```

| Clé | Type | Rôle |
|---|---|---|
| `version` | entier | `1` (une autre version est refusée) |
| `id` | texte | l'identifiant, UUID (`8-4-4-4-12` chiffres hexadécimaux, casse indifférente, écrit en minuscules) ; **jamais recalculé** |
| `import` | objet | facultatif : les **réglages d'import** de l'asset ; pour un modèle, ceux du tableau ci-dessous ; une clé inconnue ou fausse est un avertissement et garde sa valeur par défaut ; les clés qui commencent par `_` sont des commentaires |

Écriture : clés triées, deux espaces, une fin de ligne (comme `assets.py`) ; écrit de façon atomique.

Réglages d'import d'un modèle (`ModelImportSettings`, `model_import.hpp`) :

| Clé | Défaut | Bornes | Sens |
|---|---|---|---|
| `lods` | 3 | 0 à 8 | niveaux de détail faits au-delà du maillage complet (0 : aucun) |
| `lod_reduction` | 0.5 | 0.05 à 0.95 | triangles d'un niveau sur ceux du précédent |
| `lod_error` | 0.02 | 0 à 1 | l'erreur la plus grande d'un niveau, en part de la taille du modèle (diagonale de sa boîte) |
| `lod_min_triangles` | 96 | ≥ 0 | une partie de moins de triangles n'a pas de niveau de détail |
| `optimize` | `true` | — | triangles dans l'ordre du cache de sommets du GPU, sommets dans l'ordre de leur premier usage |
| `textures` | `"ktx2"` | `"ktx2"`, `"keep"` | images intégrées au modèle encodées en KTX2 (UASTC, mipmaps ; deux canaux pour les cartes de normales) ; `"keep"` : telles quelles |
| `collision` | `"named"` | `"named"`, `"visible"`, `"none"` | sa géométrie de collision (jalon 9, partie 5) : les nœuds `COL_...` / `UCX_...` (jamais dessinés, quel que soit le réglage) ; `"visible"` : eux et tous les triangles visibles ; `"none"` : aucune. Le genre d'un nœud vient de son nom (`COL_wall`, `COL_walk` ou `COL_floor`, `COL_obstacle` ou `COL_block`, `COL_void` ou `COL_pit`, sans casse), sinon de la pente : sol jusqu'à 45 degrés, mur au-delà |

### 15.2 Registre en développement

Au démarrage, `Application` parcourt les dossiers d'assets (le dossier des sources, celui du moteur `MOTEUR_ENGINE_ASSETS_DIR`, les dossiers montés `ApplicationConfig::asset_folders`) et construit le registre `identifiant → clé, réglages d'import`. Règles :

- deux dossiers donnent la même clé : le **premier** gagne, l'autre est signalé (`shadowed`) ;
- un asset sans `.meta` en reçoit un (nouvel identifiant), **seulement** quand le programme tourne sur les sources (pas depuis des archives) ;
- deux assets avec le **même identifiant** (une copie faite avec son `.meta`) : celui dont la clé vient **après** dans l'ordre des octets en reçoit un nouveau (`reassigned`), si bien que l'original garde le sien quel que soit l'ordre du disque ;
- un `.meta` sans son asset est signalé (`orphans`) : son asset a été déplacé sans lui ;
- un `.meta` illisible est un problème signalé (l'asset reste sans identité).

Une référence se résout ainsi (`Assets::resolve_reference`) : un identifiant connu donne sa clé actuelle ; inconnu avec un chemin, le chemin est pris avec un avertissement (`unknown identifier X, 'chemin' used by its path`) ; inconnu sans chemin, c'est une erreur. `Assets::reference_to(clé)` écrit `{"id", "path"}` pour un asset identifié, le chemin seul sinon.

### 15.3 Registre figé `asset_registry.mreg`

Le jeu livré ne parcourt pas de dossiers : la préparation écrit le registre des assets **livrés** dans `assets/asset_registry.mreg` (`ApplicationConfig::kRegistryFile`), rangé dans les archives ; le programme le lit au démarrage quand des archives sont montées.

| Position | Taille | Champ |
|---|---|---|
| 0 | 4 | magie `MREG` (ASCII) |
| 4 | 4 | version, u32 = 1 (`kRegistryVersion` ; une autre est refusée : `registry format N (expected 1)`) |
| 8 | reste | CBOR : une liste de lignes `[identifiant (texte), clé (texte), import (objet)]` |

Le CBOR passe d'abord le [contrôle de structure](#23-contrôle-de-la-structure-cbor) ; un identifiant qui n'est pas un UUID ou une ligne mal formée rend le fichier illisible (`unreadable: ...`).

### 15.4 Préparation et outil

À la préparation (section [8](#8-préparation-des-assets)) : les dépendances d'un préfabriqué sont suivies **par identifiant** ; un identifiant inconnu sans chemin est une **erreur** ; un chemin écrit qui n'est plus celui de l'asset est un avertissement (`the asset X is now 'clé' (written 'ancien': save the file again)`) ; deux assets de même identifiant sont une erreur ; les assets livrés sans `.meta` sont comptés dans un avertissement (ils partent sans identité).

`tools/assets/assets.py` : `check` (identifiants en double, `.meta` orphelins, assets sans `.meta`), `meta` (donne un `.meta` à ceux qui n'en ont pas), `move DOSSIER ANCIEN NOUVEAU` (déplace le fichier **et** son `.meta`, puis réécrit les chemins écrits à côté des identifiants), `find DOSSIER... ID`, `refresh` (réécrit ces chemins). Code de sortie 0 sans problème, 1 avec, 2 pour une ligne de commande fausse.

---

## 16. Modèles préparés `.mmdl`

**Rôle** (jalon 9, partie 4) : la forme d'un modèle que le moteur charge à la place du glTF, en développement comme dans le jeu livré : déjà **importé** (niveaux de détail, ordre pour le cache, textures intégrées en KTX2, clips en archive ozz). **Lecteur** : `decode_prepared_model` (`src/moteur/model_import.cpp`). **Écrivain** : `encode_prepared_model`, appelé par l'import (`Assets`, `bac_a_sable --import-assets`) et la préparation.

**Emplacement** : `<cache>/<clé du modèle>.mmdl` (`prepared_model_key` : `models/a.glb` → `.imported/models/a.glb.mmdl`). En développement, le cache est le dossier `.imported/` à côté du dossier des sources (`import_cache_folder` : `moteur/.imported/` pour `moteur/assets/`), ignoré par le dépôt ; il sert aussi pour les modèles des dossiers montés. Dans le jeu livré, le `.mmdl` est dans les archives sous `assets/.imported/<clé>.mmdl`, rangé dans le groupe d'archive de sa source ; le glTF et ses tampons ne sont **pas** livrés.

### 16.1 Disposition

Petit-boutiste. Textes : longueur u32 puis octets UTF-8 ; tableaux : nombre d'éléments u32 puis les éléments bout à bout (types simples, tels qu'en mémoire) ; blobs : taille u32 puis octets.

| Champ | Type |
|---|---|
| magie | `MMDL` (4 octets ASCII) |
| version | u32 = 3 (`kPreparedModelVersion` ; 2 : clips sous forme ozz ; 3 : géométrie de collision ; une autre est refusée : `prepared model format N (expected 3)`) |
| empreinte de la source | u64 (ci-dessous) |
| parties | u32 N, puis par partie : nom, nœud, sommets, indices, poids de peau, **niveaux de détail** (`MeshLod` : premier indice, nombre d'indices, erreur), matrice 4×4, matériau i32, peau i32, articulation i32, décalage d'articulation 4×4, rayon de peau f32 |
| matériaux | u32 N, puis : nom, couleur de base vec4, métal f32, rugosité f32, échelle de normale f32, occlusion f32, émission vec3, deux faces u8, seuil alpha f32, cinq indices d'image i32 (couleur, métal-rugosité, normale, occlusion, émission ; −1 : aucune) |
| images | u32 N, puis : nom, largeur i32, hauteur i32, pixels (blob, vide si KTX2), KTX2 (blob), sRGB u8, carte de normales u8, fichier (texte : une image dans son propre fichier, chargée comme texture à part) |
| squelette | u32 N articulations : nom, parent i32, repos (translation vec3, rotation quat, échelle vec3) |
| peaux | u32 N : nom, articulations (tableau i32), matrices inverses (tableau mat4) |
| clips bruts | u32 N (0 dans un modèle importé : les clips sont dans le champ suivant) |
| clips préparés | blob : la `ClipLibrary` sauvegardée (`ClipLibrary::save`, magie `CLPS`, archive ozz, somme FNV) |
| fichiers | u32 N textes : les fichiers dont le modèle est fait (tampons, images), relatifs à son dossier |
| collision : sommets | tableau de vec3 (espace du modèle) |
| collision : indices | tableau d'u32, trois par triangle (sens trigonométrique vu de l'extérieur) |
| collision : genres | tableau d'u8, un par triangle : `0` sol, `1` mur, `2` obstacle, `3` vide (`SurfaceKind`) |

Le lecteur refuse le fichier, avec le nom du modèle et la position (`clé: <raison> (at byte N)`), pour : une autre magie ou version, une lecture au-delà de la fin, un nombre d'éléments plus grand que ce que les octets restants peuvent contenir, une partie sans triangle, un indice au-delà des sommets, un niveau de détail au-delà des indices (ou plus de 255), une image de taille fausse, un matériau, une peau ou une articulation absents, une articulation avant son parent, une peau de plus de 256 articulations, un clip de durée fausse ou de clés désordonnées, une collision incohérente (indices qui ne font pas des triangles entiers, indice au-delà des sommets, pas un genre par triangle, sommet non fini, genre inconnu), et des octets après le dernier champ. L'archive ozz des clips est contrôlée à son tour par `ClipLibrary::load` (somme, version).

### 16.2 Empreinte et ré-import

L'empreinte (u64, FNV-1a) couvre les octets du glTF et de ses fichiers, les réglages d'import (`ModelImportSettings::hash`) et la version de l'importeur. En développement, `Assets` recharge le `.mmdl` s'il est à jour, sinon importe de nouveau depuis le glTF (sur un fil de travail, un import à la fois par modèle) et réécrit le `.mmdl` ; une modification du glTF ou du `.meta` déclenche le ré-import au rechargement à chaud. Sans source (jeu livré), le `.mmdl` est pris tel quel.

**Signature `.mmdl.files`** (développement seulement) : à côté du `.mmdl`, un JSON `{"stamp": "<empreinte>", "settings": "<empreinte des réglages>", "files": [[chemin, taille, date], ...]}` ; si les fichiers sources ont encore la même taille et la même date, le modèle est à jour **sans relire les sources** (le calcul complet de l'empreinte sinon). Fichier de cache, jamais écrit à la main.

---

## 17. Documents livrés en CBOR

**Rôle** (jalon 9, partie 4) : le jeu livré ne lit aucun JSON. La préparation écrit chaque asset `.json` (cartes, effets, descriptions d'animation, préfabriqués, liaisons, crédits, atlas…) en **CBOR** (RFC 8949) **sous le même nom** (`shipped_document`) ; tous les lecteurs du moteur prennent les deux formes (`parse_document`, `src/moteur/document.cpp`) : le contenu est le même document, seule l'écriture change.

**Distinction** : un document dont le premier octet est entre `0x80` et `0xBF` (CBOR, liste ou map) est lu en CBOR, sinon en JSON (`is_cbor_document`). Le CBOR passe le [contrôle de structure](#23-contrôle-de-la-structure-cbor) (128 niveaux au plus) avant d'être lu. Messages : `invalid CBOR: ...`, `invalid JSON: ...`, précédés du nom du fichier par chaque lecteur.

La préparation relit chaque document livré et refuse celui qui serait encore du JSON (`read-back: 'clé' is still JSON`) ; `tools/package/check_package.py` vérifie sur les archives installées qu'il n'y a ni glTF ni document JSON.

---

## 18. Maillages de navigation `.mnav`

**Rôle** (jalon 9, partie 6) : le maillage de navigation d'un niveau pour une taille de personnage, construit une fois (préparation, éditeur) et relu tel quel. **Lecteur** : `NavMesh::load` ; **écrivain** : `NavMesh::save` (`src/moteur/navigation.cpp`). Petit-boutiste.

| Champ | Contenu |
|---|---|
| magie | `MNAV` |
| version | u32 = 1 (une autre est refusée : `navigation mesh format N (expected 1)`) |
| agent | `NavAgent` : rayon, hauteur, marche, pente (4 f32) |
| réglages | `NavBuildSettings` (9 f32) |
| bornes | deux vec3 : la boîte du niveau |
| tuilage | tuiles en x et en z (i32), polygones au plus par tuile (i32), paramètres de Detour (`dtNavMeshParams` : origine vec3, largeur et profondeur des tuiles f32, tuiles et polygones au plus i32) |
| zones | u32 N, puis par zone : boîte (deux vec3), zone u8 (`NavArea`) |
| liaisons | u32 N, puis : départ vec3, arrivée vec3, rayon f32, deux sens u8, zone u8, identifiant u32 |
| obstacles | u32 N, puis : identifiant u32, boîte (deux vec3) ; puis l'identifiant suivant (u32) |
| portes fermées | u32 N identifiants |
| points d'élagage | u32 N vec3 (`prune()`) |
| tuiles | u32 N, puis chaque tuile : taille u32 et les octets de Detour (son en-tête, sa magie `DNAV` et sa version, contrôlés à l'ajout) |
| somme | u64 : FNV-1a 64 bits de tout ce qui suit la magie et la version |

Refus : magie absente (`not a navigation mesh (MNAV)`), somme fausse (`damaged navigation mesh (checksum)`), autre version, tuilage nul, tuile trop courte ou refusée par Detour, octets après la fin. **Confiance** : Detour lit ses tuiles sans tout vérifier ; le fichier ne vient que de la préparation du jeu (archives contrôlées par CRC-32) et la somme écarte une copie abîmée ; un fichier fabriqué pour nuire n'est pas un cas traité.

---

## 19. Niveaux v3 `.level.json`

**Rôle** (jalon 9, partie 8) : un niveau tel que l'éditeur l'écrit : des préfabriqués placés (pièces de kit, accessoires, lumières), des points nommés, des volumes, un sol peint et les réglages de sa navigation. Texte, pour le versionnement et les fusions. **Lecteur** : `LevelDocument::parse` (`src/moteur/level.cpp`) ; **écrivain** : `LevelDocument::to_json`. Remplace les cartes v1 et v2 (section 1), qui se migrent (`migrate_map`, `bac_a_sable --migrate-map CARTE SORTIE RÈGLES`).

```json
{
  "version": 3,
  "description": "La crypte",
  "chunk_size": 32,
  "next_id": 13,
  "layers": [{"name": "default", "visible": true, "locked": false}, {"name": "decor", "visible": true, "locked": false}],
  "instances": [
    {"id": 1, "prefab": {"id": "6f1c...", "path": "prefabs/kit/mur_4m.prefab.json"}, "layer": "default",
     "transform": {"position": [4, 0, 0], "rotation": [1, 0, 0, 0], "scale": [1, 1, 1]}},
    {"id": 2, "prefab": {"id": "9a0b...", "path": "models/props/lampadaire_gothique.prefab.json"}, "layer": "decor",
     "transform": {"position": [6, 0, 2]}, "overrides": {"2": {"light": {"intensity": 12}}}},
    {"id": 3, "prefab": {"id": "41d2...", "path": "prefabs/apparition.prefab.json"}, "layer": "default",
     "transform": {"position": [9, 0, 5]}, "properties": {"personnage": "squelette_sbire", "vie": 30}}
  ],
  "points": [{"name": "start", "position": [1, 0, 1], "rotation": [1, 0, 0, 0]}],
  "volumes": [{"id": 12, "kind": "nav_area", "min": [10, -1, 4], "max": [14, 1, 8], "properties": {"area": "water"}}],
  "ground": {"origin": [0, 0], "cell_size": 8, "width": 4, "height": 4, "level": 0,
             "prefabs": [{"id": "...", "path": "models/sols/sol_paves_humides_a_8m.prefab.json"}], "cells": [1, 1, 0, 1, ...]},
  "navigation": {"agents": [{"radius": 0.35, "height": 1.8, "max_climb": 0.35, "min_ground_normal_y": 0.7071}],
                 "build": {"cell_size": 0.15, "cell_height": 0.1}},
  "lighting": {"sun_yaw": 60, "sun_elevation": 55, "sun_color": [1, 0.93, 0.8], "sun_intensity": 3,
               "sky_intensity": 1, "exposure": 1, "background": [0.05, 0.06, 0.08]}
}
```

| Clé | Rôle |
|---|---|
| `version` | `3` ; un autre nombre est refusé (`level format N (expected 3; ...)`) |
| `chunk_size` | mètres, de 4 à 1024 : la grille des morceaux (et des tuiles de navigation) |
| `next_id` | le prochain identifiant donné ; jamais rendu plus petit (les sauvegardes renvoient aux identifiants : un identifiant retiré n'est pas redonné) |
| `layers` | les calques (nom unique, `visible`, `locked`) ; un calque inconnu d'une instance est ajouté avec un avertissement |
| `instances[]` | `id` (entier > 0, unique avec les volumes), `prefab` (référence, exigée), `layer`, `transform` (comme le composant `transform`), `overrides` (comme `PrefabPlacement::overrides` : chemin d'entité → composant → champs), `properties` (objet libre : les données du **jeu** pour cette instance, ce qu'une apparition fait apparaître, le butin d'un coffre ; le moteur les transmet sans les lire, partie 10) |
| `points[]` | `name` (unique), `position`, `rotation` (`[w, x, y, z]`) : entrées, apparitions, connecteurs |
| `volumes[]` | `id`, `kind` (libre pour le jeu ; `nav_area` : `properties.area` `water` ou `fire`, une zone de coût de la navigation), `min`, `max`, `properties` (objet) |
| `ground` | `origin` (x, z du coin de la case (0, 0)), `cell_size`, `width`, `height`, `level` (hauteur), `prefabs` (références), `cells` (`width` x `height` entiers, ligne après ligne : 0 aucun, `i` le préfabriqué `prefabs[i - 1]` posé au coin de la case) |
| `navigation` | `agents` (un maillage par taille de créature, champs de `NavAgent`), `build` (champs de `NavBuildSettings` ; `tile_size` prend `chunk_size`, aussi à l'écriture) |
| `lighting` | l'éclairage du niveau (partie 10, champs de `LevelLighting`) : `sun_yaw` et `sun_elevation` (degrés), `sun_color` (sRGB), `sun_intensity`, `sky_intensity` (la lumière du ciel), `exposure`, `background` (sRGB). Le jeu et l'éditeur le donnent au rendu par `apply_lighting()` ; absent : les valeurs par défaut |

Erreurs (le niveau est refusé avec toutes) : identifiant absent, nul ou en double, instance sans préfabriqué, calque ou point sans nom propre, sol dont les cases ne font pas `width` x `height` ou désignent un préfabriqué absent, vecteur ou rotation mal formés, champ hors bornes (réflexion).

**Règles de migration** (un JSON, exemple `assets/levels/tranche.migration.json`) : `ground_tile` et `ground_prefab` (le type de tuile qui est le sol, et ce qui le pose), `tiles` (type de tuile → préfabriqué : une instance par suite de cases le long de x, mise à l'échelle sur x), `objects` (type d'objet → préfabriqué ; angle en degrés converti en rotation, `props` en `properties`), `object_points` (types d'objets qui deviennent des points `<type>_<id>`), `chunk_size`. Les points nommés deviennent des points (au centre de leur case, numérotés s'ils sont plusieurs), les connecteurs des volumes `connector`.

**État d'une partie** (`LevelState`, une section de sauvegarde que le jeu enregistre) : `{"overrides": {"<id>": {chemin: {composant: champs}}}, "removed": [ids]}` : ce que la partie a changé dans les instances, appliqué à chaque fois que leur morceau se charge.

---

## 20. Niveaux préparés `.mlvl` et `.mlvc`

**Rôle** (jalon 9, partie 8) : un niveau prêt à charger par morceaux : un **en-tête** (`<niveau>.mlvl`) et **un fichier par morceau** de la grille qui contient quelque chose (`<niveau>.chunks/<x>_<z>.mlvc`) ; seuls les morceaux autour du héros sont lus. **Écrivains** : la préparation (`bake_level`, `encode_level_header`, `encode_level_chunk` ; `cook` pour chaque `.level.json` livré), l'éditeur. **Lecteurs** : `LevelStreamer` (`decode_level_header`, `decode_level_chunk`). Sans forme préparée (développement), `LevelStreamer::open_any()` prépare le niveau en mémoire depuis sa source.

Les deux commencent par une magie (`MLVL`, `MLVC`) et une version u32 = 2 (2 depuis la partie 10 : les propriétés des instances), et finissent par la somme FNV-1a 64 bits de tout ce qui suit la version. Refus : magie absente (`not a prepared level`, `not a level chunk`), somme fausse (`damaged ... (checksum)`), autre version, contenu illisible, grille fausse (taille nulle, `present` d'une autre taille), collision incohérente, octets après la fin.

| En-tête `.mlvl` | |
|---|---|
| magie, version | `MLVL`, 2 |
| document | un blob (taille u32) CBOR : `description`, `chunk_size`, `origin` (le coin bas du morceau (0, 0), sur un multiple de la taille), `chunks` (`[x, z]`), `bounds`, `points` (`[nom, position, rotation]`), `volumes` (`[id, genre, min, max, propriétés]`), `lighting` (partie 11 : l'éclairage du niveau, comme dans le `.level.json`), `navigation` (par agent : `[agent, réglages, origine, tuiles x, tuiles z]`, de quoi `NavMesh::start_tiled()`), `present` (un octet par morceau, 1 : il a un fichier) |
| somme | u64 |

| Morceau `.mlvc` | |
|---|---|
| magie, version | `MLVC`, 2 |
| coordonnées | x, z (i32) |
| instances | un blob CBOR `{"instances": [[id, clé du préfabriqué, transformation, surcharges, propriétés], ...]}` : celles dont la racine est dans le morceau (id 0 : une tuile du sol, sans identité) |
| collision | sommets (tableau de vec3, monde), indices (u32), genres (u8) : les triangles dont le centre est dans le morceau |
| navigation | u32 N agents, puis une tuile de Detour par agent (blob, vide : aucune) ; la tuile (x, z) du maillage est le morceau (x, z) : celles des voisins se raccordent quand elles s'ajoutent |
| somme | u64 |

**Cache de développement** (partie 11) : sans forme préparée dans les assets, `LevelStreamer::open_any()` écrit l'en-tête et les morceaux préparés dans le cache d'import (`.imported/levels/<niveau>.level.mlvl`, `.chunks/<x>_<z>.mlvc`) avec une signature (`.mlvl.files` : `{"paths": [...], "signature": {"format": 2, "files": [[chemin, taille, date], ...]}}` : le niveau, ses préfabriqués et leurs bases, ses modèles et leurs `.meta`) ; la fois suivante, si la signature est la même, il les relit au lieu de préparer le niveau. Les fichiers sont les mêmes que ceux de la préparation livrée.

---

## Annexe : sommes de contrôle

- **CRC-32** (sauvegardes, entrées d'archive) : le CRC-32 courant (IEEE 802.3, celui de zlib et du ZIP) : polynôme réfléchi `0xEDB88320`, valeur initiale `0xFFFFFFFF`, complément final. `crc32("123456789") = 0xCBF43926`.
- **FNV-1a 64 bits** (index des archives, `source_hash` des tables, empreintes de `cook_state.json`, empreintes des modèles préparés, maillages de navigation, niveaux préparés) : valeur initiale `14695981039346656037` (`0xcbf29ce484222325`), pour chaque octet `h = (h XOR octet) × 1099511628211` (`0x100000001b3`) modulo 2^64. Exception : les empreintes de `cook_state.json` partent de `kCookVersion × 1099511628211` au lieu de la valeur initiale standard (la version de l'outil change toutes les empreintes).

---

## Remarques sur les lecteurs (relevées en écrivant ce document)

Les écarts relevés à la première écriture de ce document (courbe `size` à valeur unique, `version` et types de `display.json`, clés de version 2 dans une carte version 1, tuiles de légende en un seul texte, `**` des motifs, ordre et compression de l'index des archives, valeurs refusées de `variables.cfg`, `KHR_texture_basisu` requis) sont corrigés dans le code ; les sections ci-dessus décrivent le comportement actuel. Restent, à corriger dans le code :

- **Archives, collision de casse** (`archive.cpp`, `write_archive`) : après le tri par octets, seuls deux chemins **voisins** sont comparés sans la casse. Les majuscules passent avant les minuscules : deux chemins qui ne diffèrent que par la casse peuvent être séparés par un troisième (`Audio/a.ogg`, `Maps/b.json`, `audio/a.ogg`). La collision n'est alors pas vue, et l'archive est écrite.
- **Préparation, empreinte d'un échec** (`cook.cpp`, `cook`) : l'empreinte d'un fichier est notée dans `cook_state.json` avant sa préparation. Si le transcodage ou l'écriture échoue et qu'une sortie plus ancienne existe, la préparation suivante trouve la même empreinte et une sortie présente : elle garde la sortie périmée, sans refaire le fichier ni redire l'erreur (`--force` la refait).
