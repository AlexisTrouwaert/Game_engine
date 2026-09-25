# Jalon 4 - Systèmes de base

Retour à la [roadmap générale](ROADMAP.md). Jalon précédent : [Jalon 3 - Rendu 3D](JALON_3_RENDU_3D.md). Description des fichiers existants : [Fichiers du projet](FICHIERS_DU_PROJET.md).

## Objectif

Donner au moteur les **systèmes qui ne sont pas du rendu** mais dont tout jeu a besoin : charger et partager les assets, lire les commandes du joueur sans dépendre du périphérique, décrire le monde en entités et composants, passer d'un écran à l'autre (menu, jeu, pause), jouer des sons et de la musique, et inspecter tout cela pendant que le jeu tourne.

À la fin du jalon, la **démo 3D** du jalon 3 doit devenir une **petite tranche jouable** : un écran de titre, puis la carte où un personnage se déplace **au clavier et à la souris, ou à la manette**, avec des créatures et des objets qui sont des **entités**, des textures compressées en **KTX2**, des **bruits de pas et d'impact** placés dans le monde, une **musique** qui change entre le menu et le jeu, une **pause**, et un **inspecteur d'entités** dans ImGui. Elle se lance, comme les autres, depuis **DEBUG > Tests moteur**.

**Pourquoi maintenant ?** Le rendu 3D est en place : ce qui manque pour écrire du gameplay, ce sont les systèmes. L'animation 3D (jalon 5) chargera des squelettes et des clips (le gestionnaire d'assets), les attachera à des personnages (l'ECS) et les déclenchera depuis des actions du joueur (les entrées) : elle a besoin de tout ce jalon.

## Prérequis

- Le [jalon 3](JALON_3_RENDU_3D.md) est terminé (sous Windows ; les vérifications Mac restent listées dans [TEST_MAC.md](TEST_MAC.md)).
- Lire la documentation d'EnTT (le wiki du dépôt `skypjack/entt`, en particulier *Crash Course: entity-component system*) : registre, vues, groupes, signaux.
- Une manette pour les tests (Xbox ou PlayStation), sur Windows et sur Mac.

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
2. [Gestionnaire d'assets](#2-gestionnaire-dassets)
3. [Textures compressées (KTX2)](#3-textures-compressées-ktx2)
4. [Entrées : actions, clavier, souris, manette](#4-entrées--actions-clavier-souris-manette)
5. [ECS (EnTT)](#5-ecs-entt)
6. [Scènes et états de jeu](#6-scènes-et-états-de-jeu)
7. [Audio](#7-audio)
8. [Outils de debug (ImGui)](#8-outils-de-debug-imgui)
9. [Tranche jouable](#9-tranche-jouable)
10. [Critères de fin de jalon](#10-critères-de-fin-de-jalon)
11. [Risques principaux](#11-risques-principaux)
12. [Décisions à consigner](#décisions-à-consigner)

---

## 1. Vue d'ensemble et ordre de travail

### Où on part

Après le jalon 3, le moteur sait tout dessiner, mais le reste est fait « à la main » :

- **Assets** : chaque scène charge ses fichiers elle-même (`asset_path()`, `load_image()`, `Model::load()`, `load_environment()`), et garde ses `Texture`, `Mesh` et `Model` dans ses propres membres. Le chargement est **synchrone** et attend le GPU (`Renderer::create_texture()`). Deux scènes qui utilisent le même tonneau le chargent deux fois. Les textures sont des PNG et des JPEG décompressés en RGBA8.
- **Entrées** : `Game::on_event()` reçoit l'**événement SDL brut** (commentaire : « Temporary: an input abstraction will replace it »). Chaque scène teste ses touches (`SDLK_F`, la molette, les clics) et convertit la souris avec `Application::to_pixels()`. `DebugUi::captures()` filtre ce qui revient à ImGui. Aucune manette.
- **Monde** : la démo 3D tient ses objets dans des tableaux de structures (`StaticDraw`, `Creature`, `Torch`), avec leur état précédent pour l'interpolation, et un index pour la créature sélectionnée.
- **Scènes** : le bac à sable a déjà un embryon de gestion de scènes, propre à lui : un menu **DEBUG > Tests moteur** qui crée une `SandboxScene` (un `Game` avec `draw_controls()` et `stop_requested()`) et la remplace par une autre.
- **Audio** : rien. SDL3 est initialisé sans le sous-système audio.
- **ImGui** : `DebugUi`, le menu des tests, les panneaux « Rendu » des scènes (statistiques, anticrénelage, lumières, vues de debug).

Les principes restent ceux de la roadmap : le moteur ne connaît rien de l'ARPG, la logique tourne à **pas fixe** et doit rester **déterministe** (même graine, mêmes entrées : même capture, sur les deux OS).

### Dépendances entre les parties

```
2. Gestionnaire d'assets
      |
      +------------------+
      |                  |
3. KTX2           4. Entrées           (indépendantes l'une de l'autre)
      |                  |
      +--------+---------+
               |
          5. ECS (EnTT)
               |
       6. Scènes et états
               |
      +--------+---------+
      |                  |
  7. Audio        8. Outils ImGui
      |                  |
      +--------+---------+
               |
       9. Tranche jouable
```

- Le **gestionnaire d'assets** vient d'abord : tout le reste charge quelque chose (textures, modèles, sons, fichier des touches).
- **KTX2** et les **entrées** sont indépendants : l'un est un nouveau chargeur du gestionnaire, l'autre ne charge qu'un fichier de configuration. Ils peuvent s'intercaler.
- L'**ECS** a besoin des **poignées** d'assets (un composant ne garde pas un `Model` entier, mais une poignée vers lui) et profite des actions (le personnage se déplace sur l'action « se déplacer »).
- Les **scènes** possèdent chacune leur monde (registre EnTT) et leurs assets.
- L'**audio** peut commencer plus tôt en logique pure (mixage, volumes, atténuation), mais ses sons se déclenchent depuis les entités et changent de musique avec les états : il se branche après la partie 6.
- L'**inspecteur d'entités** n'a de sens qu'une fois l'ECS en place.

### Ce qui peut se faire en pause du moteur

Logique pure, testable en ligne de commande :

- **Cache et durées de vie** des assets : clés, poignées, compteurs, déchargement, avec de faux chargeurs sans GPU.
- **Lecture d'un fichier KTX2** et choix du format GPU cible selon ce que le GPU accepte.
- **Table des actions** : lecture du fichier de touches, état « enfoncée / vient d'être enfoncée / relâchée », zone morte des sticks, accumulation entre deux pas fixes.
- **Systèmes de l'ECS** sur un registre sans rendu (déplacement, interpolation, sélection).
- **Pile d'états** : transitions, ordre des appels, états transparents (pause par-dessus le jeu).
- **Atténuation et panoramique** d'un son en fonction de la position de l'auditeur.
- **Recherche des sons et musiques de test**, et vérification de leurs licences.

### Estimation indicative

Pour un dev solo à temps partiel. À prendre comme un ordre de grandeur, pas comme un engagement.

| Partie | Estimation |
|---|---|
| 2. Gestionnaire d'assets | 2 à 3 semaines |
| 3. KTX2 | 1 à 2 semaines |
| 4. Entrées | 1 à 2 semaines |
| 5. ECS | 2 à 3 semaines |
| 6. Scènes et états | 1 à 2 semaines |
| 7. Audio | 2 à 3 semaines |
| 8. Outils ImGui | 1 à 2 semaines |
| 9. Tranche jouable | 1 à 2 semaines |
| **Total** | **environ 3 à 5 mois** |

### Liens avec les autres jalons

- **Animation 3D** (jalon 5) : les squelettes et les clips seront des assets comme les autres ; l'`AnimationPlayer` du jalon 2 deviendra un composant ; les événements d'animation (« le pied touche le sol ») déclencheront des sons. Rien de tout cela ici, mais les poignées d'assets et la file d'événements sonores doivent le permettre.
- **Monde et déplacement** (jalon 6) : les collisions, le pathfinding et les particules seront des **systèmes** de l'ECS. Ici, le personnage se déplace sans collision (ou avec le simple `walkable` de la `TileMap`).
- **Outils et données** (jalon 7) : le **rechargement à chaud des données JSON**, la **sauvegarde** et la **console** y sont prévus. Le rechargement à chaud des assets (question de la partie 2) doit partager son mécanisme de surveillance des fichiers avec eux. La sauvegarde demandera de sérialiser des composants : garder des composants **simples** (des données, pas de pointeurs) dès maintenant la rendra possible.
- **Consolidation** (jalon 8) : le **packaging** transcode les KTX2 UASTC en BC7 / BC5 une fois pour toutes (décision du jalon 3) et regroupera peut-être les assets dans une archive. Le gestionnaire d'assets doit lire les fichiers par une seule fonction, pour qu'une archive puisse la remplacer.

### Questions générales

- **Une seule démo qui grandit, ou une scène de test par système ?** Recommandation : une petite scène de test par partie quand c'est utile (entrées, audio), et la **démo 3D** qui absorbe tout en partie 9, comme au jalon 3.
- **Le bac à sable garde-t-il son menu de tests ?** Oui : c'est l'outil de développement du moteur. La gestion de scènes de la partie 6 est celle **du jeu** ; le menu des tests peut s'appuyer dessus ou rester à part (voir la partie 6).
- **Multithreading ?** Seulement là où c'est isolé : le décodage des assets (partie 2) et le mixage audio (qui tourne de toute façon sur son propre fil). La logique reste sur un seul fil.

---

## 2. Gestionnaire d'assets

### But

Un endroit **unique** où l'on demande un asset par son nom, qui le charge une seule fois, le partage entre ceux qui l'utilisent, sait quand le libérer, et signale proprement un fichier manquant.

### Tâches

- [x] Définir une **poignée** d'asset typée : petite, copiable, stockable dans un composant. *`Asset<T>`, la ressource d'EnTT (voir les décisions).*
- [x] Écrire le **cache** : une clé (le chemin relatif à `assets/`, normalisé) donne toujours la même poignée ; un second chargement du même fichier ne fait rien.
- [x] Brancher les **chargeurs** existants derrière une interface commune : images (`load_image` + `create_texture`), modèles glTF (`Model::load`), environnements HDR (`load_environment`), polices (`Font`), atlas du jalon 2, animations (`animations.json`). Chaque type d'asset a son chargeur ; le cache ne connaît pas les types.
- [x] Gérer les **paramètres** qui changent l'asset produit (une texture en sRGB ou non, avec mipmaps ou non) : ils font partie de la clé.
- [x] Définir les **durées de vie** (voir les questions) et le **déchargement**.
- [x] Passer toutes les lectures de fichiers par **une seule fonction** (`read_file()`), pour qu'une archive puisse la remplacer au packaging. *Sauf les shaders, qui ne sont pas des assets.*
- [x] En cas d'erreur : un **asset de remplacement** visible (texture en damier magenta, cube) et un message qui nomme le fichier, plutôt qu'un plantage.
- [ ] Optionnel selon la décision : **chargement en arrière-plan** (décodage sur un fil de travail, envoi au GPU sur le fil principal). *Pas fait : aucun chargement ne dépasse encore une seconde (voir les décisions).*
- [x] Optionnel selon la décision : **rechargement à chaud**.
- [x] Convertir la démo 3D et la scène « Rendu 3D » au gestionnaire (plus aucun `Model::load` direct dans les scènes). *Toutes les scènes du bac à sable, les scènes 2D et la comparaison avec Blender comprises.*
- [x] Un compteur de mémoire par type d'asset (nombre, octets en GPU), pour le panneau de la partie 8. *Déjà affiché : DEBUG > Assets.*

### Questions à se poser

**Poignées et durées de vie**
- **Poignée ou pointeur partagé ?** `std::shared_ptr` est le plus simple, mais lourd dans un composant, et cache qui garde quoi. Une poignée **indice + génération** (32 + 32 bits) est petite, sûre (une poignée périmée se détecte) et se sérialise. Recommandation : **indice + génération**.
- **Qui décide de décharger ?** Trois options : un **compteur de références** (libéré quand plus personne ne l'utilise, mais un asset peut être rechargé en boucle à chaque changement de pièce) ; des **groupes** liés à une scène (tout ce que la scène a chargé est libéré quand elle se ferme, sauf ce qu'une autre scène tient aussi) ; **jamais** (tout reste en mémoire). Recommandation : **compteur de références, mais déchargement seulement aux changements de scène** (un « ramasse-miettes » explicite). Pas de libération au milieu d'une frame, donc aucune texture détruite pendant que le GPU la lit.

**Chargement**
- **Synchrone ou en arrière-plan ?** Aujourd'hui tout est synchrone, et la démo 3D se charge en moins d'une seconde. Mais les textures PBR en 1K ou 2K, le préfiltrage de l'environnement et bientôt les animations vont allonger les chargements, et un ARPG charge des zones en jeu. Recommandation : **prévoir l'interface asynchrone** (la poignée existe avant l'asset, un état « en cours / prêt / en erreur ») mais l'implémenter d'abord synchrone ; ajouter un fil de décodage si un chargement dépasse une seconde. SDL_GPU interdit d'enregistrer des commandes sur plusieurs fils sans précaution *(à vérifier : règles de SDL_GPU sur les fils)* : l'envoi au GPU reste sur le fil principal.
- **Comment déclarer les paramètres d'une texture (sRGB, mipmaps, normales) ?** Au chargement glTF, le matériau le dit (couleur de base = sRGB, normales = linéaire). Pour une texture isolée : un paramètre à la demande, ou une convention de nom (`_n`, `_orm`). Recommandation : **paramètre à la demande** pour les textures isolées ; le glTF décide pour les siennes.

**Rechargement à chaud**
- **Faut-il recharger un asset modifié sans relancer ?** C'est très confortable pour régler des textures, des modèles ou des sons, et le jalon 7 le prévoit pour les données JSON. SDL ne surveille pas les fichiers : il faut **interroger la date de modification** régulièrement (`std::filesystem::last_write_time`, par exemple deux fois par seconde) ou une bibliothèque de surveillance *(efsw, dans vcpkg, à vérifier)*. Recommandation : **en développement seulement**, par **interrogation des dates**, pour les textures, les modèles et les sons ; la poignée reste la même, seul son contenu change. Le mécanisme sera repris pour le JSON au jalon 7.

### Pièges connus

- Deux clés pour le même fichier (`models/a.gltf` et `models\\a.gltf`, ou une casse différente, que Windows accepte et pas le Mac) : chargé deux fois, ou introuvable sur un seul OS. **Normaliser** la clé, et refuser une casse fausse même sous Windows.
- Détruire une texture encore utilisée par la frame en cours : SDL_GPU garde la ressource jusqu'à la fin de son utilisation *(à vérifier : `SDL_ReleaseGPUTexture` diffère-t-il bien la destruction ?)*, mais les poignées, elles, pointeront sur autre chose. Décharger entre deux frames.
- Un asset de remplacement qui cache une erreur : toujours l'écrire dans le log, et le compter dans le panneau.
- Un glTF qui référence ses textures : ses textures doivent passer par le même cache (deux modèles qui partagent une texture ne la chargent qu'une fois).

### Implémentation réalisée (partie 2)

Fichiers : `asset_cache.hpp` / `asset_cache.cpp` (cache générique, sans GPU), `assets.hpp` / `assets.cpp` (le gestionnaire), `paths.hpp` / `paths.cpp` (`read_file`), `model.hpp` / `model.cpp`, `application.hpp` / `application.cpp`, et le bac à sable. Nouvelles bibliothèques : **EnTT** 3.16 (MIT) et **efsw** 1.7.2 (MIT), toutes deux dans vcpkg et dans « À propos ».

- **`Asset<T>`** est `entt::resource<T>` : un pointeur partagé (`std::shared_ptr`) vers l'asset. Le stockage est un `entt::resource_cache` par type. EnTT sera de toute façon là à la partie 5 : autant s'appuyer sur ce qu'il fournit plutôt que d'écrire nos propres poignées.
- **`AssetCache<T>`** ajoute ce qu'EnTT ne fait pas : la fonction de chargement gardée par asset (pour le recharger), les fichiers dont il est fait (pour savoir quoi recharger quand un fichier change), l'asset de remplacement, la mesure de la mémoire, le compte des chargements et des échecs, et une **vérification des collisions** : EnTT identifie une clé par un hachage FNV-1a de 32 bits ; deux clés de même hachage lèvent une erreur au lieu de se confondre sans bruit.
- **`Assets`** (un par `Application`, `app.assets()`) : `texture(chemin, réglages)`, `model`, `environment`, `font(chemin, taille)`, `atlas`, `animations`. La clé est le chemin normalisé (`normalize_asset_path` : `/` partout, sans `.` ni `..`, jamais absolu, jamais hors de `assets/`), suivi des options qui changent le résultat (`textures/a.png#srgb,mipmaps`, `fonts/Inter-Regular.ttf#24.000000`).
- **Casse** : `check_asset_case` compare chaque élément du chemin aux noms réels sur le disque, et refuse une casse fausse (« 'models' is written 'Models' on disk »), même sous Windows et macOS, qui l'acceptent par défaut.
- **Textures des glTF** partagées : `parse_gltf` peut laisser les images en fichiers séparés non décodées (`GltfOptions::decode_external_images = false`), et `Model::create` les demande alors au cache des textures. Deux modèles qui utilisent la même image n'ont qu'une texture. Les images intégrées (`.glb`, data URI) restent au modèle.
- **Remplacement** : damier magenta et noir pour une texture, cube magenta pour un modèle, ciel procédural pour un environnement ; le log nomme le fichier et la raison. Une police, un atlas ou un fichier d'animations en erreur lèvent une exception (pas de remplacement sensé). Pour les fichiers de test **facultatifs** (tonneau de la démo, fichiers de la comparaison avec Blender), les scènes testent `assets.exists()` et s'en passent, comme avant.
- **Libération** : `collect_garbage()` libère ce qu'aucune poignée ne tient plus (les modèles d'abord, puis les textures qu'ils libèrent). Le menu des tests crée la scène suivante **avant** de détruire la précédente, puis appelle `collect_garbage()` : ce que les deux utilisent n'est jamais rechargé.
- **Rechargement à chaud** : `efsw` surveille le dossier `assets/` **des sources** (sur son propre fil, avec les API natives : `ReadDirectoryChangesW`, FSEvents). `Assets::update()`, appelé par `Application` en début de frame, prend les fichiers qui n'ont pas changé depuis 200 ms, les **copie** à côté de l'exécutable (là où le jeu lit), puis recharge les assets qui en sont faits. Le bac à sable l'active dès que le dossier des sources existe ; `--no-hot-reload` le coupe. Les atlas et les animations ne sont pas rechargés (les scènes gardent des références vers leurs sprites et leurs clips).
- **Rechargement en place** : une poignée garde le même objet ; seul son contenu change. Pour un modèle, `Model::replace_in_place` garde chaque `Mesh` et chaque texture propre au modèle **à la même adresse**, parce que les scènes gardent des pointeurs vers eux (la démo 3D en garde un par objet de décor : le premier essai a planté). Si la structure a changé (nombre de pièces, de matériaux, autre fichier de texture), le rechargement est refusé avec un message : il faut relancer la scène.
- **Mémoire** : `Texture`, `Mesh`, `Environment`, `Model`, `Font` et `TextureAtlas` connaissent leur taille sur le GPU. La fenêtre **DEBUG > Assets** du bac à sable montre, par type, le nombre d'assets, la mémoire, les chargements et les échecs, la liste des assets avec leurs utilisateurs, et un bouton « Libérer les assets inutilisés ».
- **Une seule lecture de fichiers** : `read_file()` (un `FileData` qui libère les octets lui-même) ; images, environnements, polices, glTF et leurs tampons passent par elle.

**Vérifications faites (Windows)**

- Démo 3D `--demo3d --seed 42 --freeze-after 60 --no-input --run-seconds 3 --capture` : **`5fef643792bf`**, inchangée. Démo 2D (`--demo --seed 42 --freeze-after 30`) : **`bd91e8b2c616`**, inchangée.
- Rechargement à chaud : pendant que la démo 3D tourne, la texture de couleur du tonneau remplacée par une image rouge dans `assets/` des sources : les tonneaux deviennent rouges dans la capture prise ensuite, sans relancer. Le `.gltf` réécrit : le modèle est rechargé, sans plantage.
- Modèle cassé (JSON invalide) dans la scène « Rendu 3D » : cube magenta à sa place, message rouge sous le cube, log `ERROR: Assets: model 'models/aaa_broken.gltf' replaced by a placeholder: ... invalid JSON`.
- Build Debug : aucun avertissement ; démo 3D, scène « Rendu 3D » et démo 2D sans aucun message `D3D12 ERROR` / `WARNING`.
- **211** tests unitaires au vert (198 avant) : `test_asset_cache` (normalisation, casse, partage, libération, remplacement, rechargement en place et refusé, fichiers d'un asset, mesure) et `test_model` (images laissées à l'appelant, `replace_in_place` accepté et refusé).

### Validation

- [x] Deux scènes qui utilisent le même modèle ne le chargent qu'une fois (compteur dans le log ou le panneau). *Tests unitaires ; compteurs de la fenêtre DEBUG > Assets.*
- [ ] Au retour dans une scène déjà visitée, les assets gardés ne sont pas rechargés ; ceux qui ne servent plus sont libérés au changement de scène. *Couvert par les tests unitaires ; à regarder dans le menu (fenêtre DEBUG > Assets en changeant de test).*
- [x] Un fichier manquant affiche l'asset de remplacement et un message qui nomme le fichier, sans planter.
- [x] Les tests unitaires couvrent le cache, les poignées périmées et le déchargement, avec de faux chargeurs. *Pas de poignée périmée avec des pointeurs partagés : un asset tenu n'est jamais libéré.*
- [x] Les captures de référence de la démo 3D sont **inchangées** (`5fef643792bf`) après la conversion au gestionnaire.
- [ ] Mac : rechargement à chaud (FSEvents) et refus d'une casse fausse (voir [TEST_MAC.md](TEST_MAC.md#jalon-4--systèmes-de-base)).

---

## 3. Textures compressées (KTX2)

### But

Diviser par 4 la mémoire des textures 3D, comme décidé au jalon 3 : des fichiers **KTX2**, lus par `libktx`, en **UASTC** transcodé au chargement en **BC7** (couleurs, rugosité / métal / occlusion) ou **BC5** (normales), ou déjà en BC7 / BC5.

### Tâches

- [x] Ajouter `ktx` à `vcpkg.json` et à `credits.json` (Apache 2.0). Vérifier qu'il compile sur Windows et sur Mac. *Windows fait ; Mac dans TEST_MAC.md.*
- [x] Un chargeur KTX2 dans le gestionnaire d'assets : lire le fichier, transcoder l'UASTC vers le format cible, créer la texture avec **tous ses niveaux de mipmaps** tels qu'ils sont dans le fichier (plus de mipmaps calculées par le GPU).
- [x] Choisir le format cible avec `SDL_GPUTextureSupportsFormat` (`BC7_RGBA_UNORM` / `_SRGB`, `BC5_RG_UNORM`), avec un repli en RGBA8 si le GPU ne les lit pas, et l'écrire dans le log (la ligne `GPU:` affiche déjà `bc7=` / `bc5=`).
- [x] Pour les normales en BC5 : reconstruire la composante Z dans le shader (`z = sqrt(1 - x² - y²)`). *Pour toutes les cartes de normales, pas seulement BC5 (voir l'implémentation).*
- [x] Un **outil de conversion** (`tools/textures/convert_gltf_textures.py`) : PNG / JPEG vers KTX2 UASTC avec mipmaps, avec les bons réglages selon le rôle de la texture (sRGB ou non, normales). S'appuie sur `ktx create` (KTX-Software 4.4.2, installé par vcpkg).
- [x] Glisser la conversion dans `fetch_test_models.py` : les modèles de test Poly Haven sont convertis après téléchargement, et le glTF pointe vers les `.ktx2` (`KHR_texture_basisu`).
- [x] Mesurer : mémoire des textures avant et après, temps de chargement (transcodage compris), et comparer les captures (écart visuel attendu, faible).

### Questions à se poser

- **Comment un glTF désigne-t-il ses textures KTX2 ?** L'extension standard est `KHR_texture_basisu`, que `cgltf` lit *(à vérifier)*. L'autre option est de garder le glTF intact et de chercher un `.ktx2` à côté de chaque PNG. Recommandation : **l'extension standard**, écrite par l'outil de conversion ; les modèles restent lisibles dans Blender grâce au repli PNG que l'extension permet.
- **Qualité de l'UASTC** : niveau de qualité et compression RDO (fichiers plus petits sur disque, un peu de qualité en moins). À régler avec les mesures.
- **Garder les PNG à côté ?** Oui en développement (sources à retoucher) ; seuls les KTX2 partent au packaging.

### Pièges connus

- Oublier que les mipmaps des fichiers KTX2 sont **déjà** calculées : les recalculer par le GPU échoue sur un format compressé.
- Les normales transcodées en BC7 au lieu de BC5, ou lues comme sRGB : éclairage faux sans erreur visible au premier coup d'œil. Comparer à la capture PNG.
- Des textures dont la taille n'est pas multiple de 4 : les formats BC travaillent par blocs de 4×4 *(à vérifier : ce qu'exigent SDL_GPU et libktx pour les niveaux les plus petits)*.
- Le transcodage UASTC coûte du temps de CPU à chaque chargement : c'est pourquoi le packaging le fera d'avance. Le mesurer quand même.

### Implémentation réalisée (partie 3)

Fichiers : `ktx_texture.hpp` / `ktx_texture.cpp` (décodage, sans GPU), `renderer.hpp` / `renderer.cpp`, `model.hpp` / `model.cpp`, `assets.cpp`, `shaders/mesh.frag.hlsl`, `tools/textures/convert_gltf_textures.py`, `tools/models/fetch_test_models.py`, le bac à sable. Bibliothèque : **KTX-Software 4.4.2** (vcpkg `ktx`, avec la fonctionnalité `tools` pour l'outil `ktx`), Apache 2.0, dans « À propos ».

- **`decode_ktx2(octets, nom, réglages, formats du GPU)`** donne une `CompressedImage` (format SDL, taille, tous les niveaux) :
  - contenu Basis Universal (UASTC, ou ETC1S) : transcodé en **BC5** pour une texture à deux canaux (carte de normales encodée avec `--normal-mode` : x dans RGB, y dans l'alpha), en **BC7** pour le reste ; en **RGBA8** si le GPU n'a pas ce format ou si la taille n'est pas un multiple de 4 (blocs 4×4). En RGBA8, une carte à deux canaux est remise en x dans le rouge et y dans le vert, comme en BC5 ;
  - contenu déjà en BC7, BC5 ou RGBA8 : pris tel quel (le chemin du futur packaging, qui transcodera d'avance) ;
  - la variante sRGB de BC7 et de RGBA8 suit le rôle donné par le moteur (`TextureSettings::srgb`), pas le fichier ; les mipmaps sont celles du fichier ; toute erreur nomme le fichier.
- **`Renderer::create_texture(CompressedImage)`** envoie tous les niveaux ; `Renderer::compressed_formats()` dit ce que le GPU lit (déterminé au démarrage, la ligne `GPU:` du log) ; `set_block_compression(false)` le coupe pour comparer (option **`--no-bc`**).
- **`TextureSettings::normal_map`** : nouveau réglage, qui fait partie de la clé du cache (`#normal`). Le glTF le pose sur les images de `normalTexture`.
- **glTF** : `KHR_texture_basisu` est lu par cgltf ; son image KTX2 est préférée à l'image PNG / JPEG, qui reste le repli (`GltfOptions::prefer_ktx2`, `Assets::set_prefer_ktx2`, option **`--no-ktx2`**). Une image KTX2 intégrée ou lue directement est gardée encodée (`ModelImage::ktx2`) et décodée par `Model::create` pour le GPU présent. Dans le gestionnaire, `Assets::texture` reconnaît un KTX2 à ses premiers octets.
- **Shader** : seuls x et y de la carte de normales sont lus, **z est recalculé** (`sqrt(1 - x² - y²)`), pour **toutes** les cartes de normales : BC5 n'a pas de z, et un seul chemin évite un réglage de plus par matériau. Avec les JPEG, l'image ne change presque pas : 0,09 % des pixels, 4 niveaux au plus.
- **Outil** `convert_gltf_textures.py` : pour chaque image d'un matériau, `ktx create --encode uastc --uastc-quality 2 --zstd 18 --generate-mipmap`, en `_SRGB` pour la couleur de base et l'émission, en linéaire (`--assign-tf linear`) pour les données, avec `--normal-mode --normalize` pour les normales. Le `.ktx2` est écrit à côté de l'image, et le glTF reçoit l'extension (dans `extensionsUsed`, pas dans `extensionsRequired` : Blender l'ouvre toujours). Un `.ktx2` plus récent que son image n'est pas refait ; deux passages donnent le même glTF (vérifié). `fetch_test_models.py` l'appelle après le téléchargement.
- **Piège trouvé** : sans `--assign-tf linear`, `ktx create` prend un JPEG pour du sRGB et « convertit » les valeurs des normales et de la rugosité en linéaire (il le signale par un avertissement).
- `--report` écrit à la fin la mémoire des assets par type.

**Mesures (Windows, RTX 4070 Ti SUPER, Release)**

| | JPEG (`--no-ktx2`) | KTX2 en RGBA8 (`--no-bc`) | KTX2 en BC7 / BC5 |
|---|---|---|---|
| Textures de la démo 3D (tonneau : 3 textures 1K) | 16,0 Mo | 16,0 Mo | **4,0 Mo** |
| Textures de la scène « Rendu 3D » (4 modèles + référence, 14 textures) | 64,0 Mo | — | **16,0 Mo** |
| Chargement : lanterne / épée / rocher / tonneau | 63 / 52 / 127 / 42 ms | — | 106 / 71 / 138 / 78 ms |
| Capture de la démo 3D (`--seed 42 --freeze-after 60`) | `6fb7a00fa461` | `be76d71df36e` | **`d41d301beead`** (nouvelle référence, stable sur deux lancements) |

- **Mémoire divisée par 4 exactement**, comme prévu.
- **Chargement** : 10 à 40 ms de plus par modèle (décompression Zstandard et transcodage UASTC de trois textures 1K). C'est ce que le transcodage d'avance du packaging supprimera ; acceptable en développement.
- **Image** : comparée aux JPEG dans la scène de comparaison avec Blender (les quatre modèles en gros plan), différence invisible à l'œil. 8 % des pixels bougent, 0,05 % de plus de 8 niveaux, au plus 77 niveaux sur quelques texels de l'épée ; écart moyen 0,1 niveau. Dans la démo 3D : 1,4 % des pixels, au plus 18 niveaux.
- **Sur disque**, les KTX2 UASTC sont plus gros que les JPEG (1,2 Mo contre 196 Ko pour la couleur du tonneau) : l'UASTC vise la qualité et la mémoire GPU, pas la taille du fichier. La compression RDO (`--uastc-rdo`) les réduirait si la taille du jeu devient un sujet.
- Démo 2D : `bd91e8b2c616`, inchangée.

**Vérifications faites (Windows)**

- Build Debug sans avertissement ; démo 3D (BC, `--no-bc`, `--no-ktx2`), comparaison avec Blender et scène « Rendu 3D » sans aucun message `D3D12 ERROR` / `WARNING` : les niveaux de 2×2 et 1×1 des textures BC passent.
- **218** tests unitaires (211 avant) : `test_ktx_texture` fabrique de vrais KTX2 en mémoire avec libktx (UASTC en couleur et carte de normales à deux canaux, RGBA8 brut) et vérifie formats, niveaux, tailles des blocs, repli RGBA8 fidèle à la source, séparation de x et y, taille non multiple de 4, fichiers invalides ou tronqués.
- **Incident de build** : un « Run-Time Check Failure #2 - Stack around the variable 'mask' was corrupted » est apparu dans la démo 3D en Debug. Cause : l'objet `demo3d.cpp.obj` du dossier Debug de CLion n'avait **aucune dépendance d'en-tête** enregistrée dans Ninja : il n'avait pas été recompilé après l'ajout de `TextureSettings::normal_map`, et gardait l'ancienne taille de la structure. Deux objets étaient concernés (`demo3d` et `blender_compare`), sans doute compilés un jour depuis une console à une autre page de code : MSVC en français écrit « Remarque : inclusion du fichier : » avec une espace insécable dont l'octet dépend de la page de code, et Ninja ne reconnaît plus la ligne. Corrigé en les recompilant ; le build Release avait toutes ses dépendances. Contrôle : `ninja -t deps` ne doit lister aucun objet à `#deps 0`.

### Validation

- [x] Les modèles de test s'affichent depuis des KTX2, avec un écart visuel faible par rapport aux PNG (captures comparées, écart mesuré).
- [x] La mémoire des textures de la démo 3D baisse d'environ 4 fois (chiffres avant / après dans ce document). *16 → 4 Mo.*
- [x] Le repli RGBA8 fonctionne (forcé par une option de ligne de commande). *`--no-bc`.*
- [ ] Mac : BC7 et BC5 acceptés par `SDL_GPUTextureSupportsFormat` (voir [TEST_MAC.md](TEST_MAC.md#jalon-4--systèmes-de-base)).

---

## 4. Entrées : actions, clavier, souris, manette

### But

Que le jeu demande « le joueur attaque-t-il ? » et « dans quelle direction se déplace-t-il ? » au lieu de « la touche A est-elle enfoncée ? ». Les **actions** sont reliées à des touches, des boutons de souris et des boutons ou sticks de manette par un **fichier de configuration** que le joueur pourra modifier.

### Deux sortes d'actions

- **Boutons** (attaquer, utiliser la potion 1, ouvrir l'inventaire) : enfoncé, vient d'être enfoncé, vient d'être relâché.
- **Axes** (se déplacer, déplacer la caméra) : une valeur en 1D ou 2D, venant d'un stick, de quatre touches, ou de la souris.

Dans un ARPG, la **souris** a un rôle à part : sa **position** désigne un point du monde (le picking du sol du jalon 3). Le déplacement au clic (« se déplacer vers le point sous la souris ») et le déplacement au stick sont deux façons de produire la même intention.

### Tâches

- [x] Une classe `Input` (dans le moteur) qui reçoit les événements SDL, tient l'état des périphériques, et les convertit en actions. `Game::on_event()` reste disponible pour les cas bruts (saisie de texte), mais le gameplay ne l'utilise plus.
- [x] Les **actions** sont déclarées par le jeu (le moteur ne connaît pas « attaquer ») : un nom, un type (bouton ou axe), des liaisons par défaut.
- [ ] **Clavier** : liaisons par touche physique (voir les questions), modificateurs (Maj, Ctrl). *Touches physiques faites ; les modificateurs (Maj + clic...) attendront un besoin du jeu.*
- [x] **Souris** : boutons, molette, position en pixels (convertie avec `to_pixels`), mouvement.
- [x] **Manette** : `SDL_Gamepad` (ouverture et fermeture quand on branche et débranche), boutons, gâchettes, sticks avec **zone morte**, vibration optionnelle. *Sans vibration.*
- [x] Brancher les entrées sur le **pas fixe** : un appui bref entre deux pas fixes ne doit pas être perdu, et un appui ne doit pas compter deux fois si deux pas fixes tombent dans la même frame (voir les pièges).
- [x] Respecter ImGui : quand `DebugUi::captures()` dit qu'un événement lui revient, les actions ne le voient pas. *Sauf les relâchements, qui passent toujours.*
- [x] Le **fichier de liaisons** : des valeurs par défaut dans `assets/`, et une copie du joueur dans son dossier de préférences (`SDL_GetPrefPath`), qui les remplace.
- [x] Détecter le **dernier périphérique utilisé** (clavier-souris ou manette), pour que l'interface affiche les bonnes icônes plus tard et que le curseur se cache à la manette. *Détecté, et l'aide de la démo s'y adapte ; le curseur n'est pas encore caché.*
- [x] Une scène de test « Entrées » : l'état de chaque action, en direct, et les périphériques branchés. *Dans le panneau de la démo 3D (section « Commandes ») plutôt qu'une scène à part.*
- [ ] Convertir la démo 3D et la scène « Rendu 3D » aux actions (caméra, sélection, clic pour se déplacer, suivi). *Démo 3D faite ; la scène « Rendu 3D » garde ses événements bruts (scène d'essai du rendu, la tranche jouable partira de la démo).*
- [x] L'option `--no-input` du bac à sable passe par `Input` (plus aucune entrée réelle, mais des entrées simulées restent possibles pour les tests). *Pour la démo 3D ; les entrées simulées sont les rejeux (`--replay-input`).*

### Questions à se poser

- **Format du fichier des touches ?** **JSON** (nlohmann, déjà utilisé), lisible et modifiable à la main. Recommandation : JSON, un objet par action avec la liste de ses liaisons ; version du format dans le fichier, pour pouvoir le faire évoluer.
- **Touche physique ou caractère ?** Sur un clavier AZERTY, « Z » d'un QWERTY est à la place de « W ». SDL3 distingue le **scancode** (la position physique) du **keycode** (le caractère). Pour des déplacements, le scancode garde la même disposition sur tous les claviers ; pour « I pour l'inventaire », le keycode correspond à la lettre écrite. Recommandation : **scancodes** pour les liaisons, et affichage du nom de la touche selon la disposition réelle (`SDL_GetKeyFromScancode`).
- **Les entrées font-elles partie de la simulation déterministe ?** Oui : pour rejouer une partie (tests, bugs), il suffit d'enregistrer les **actions** de chaque pas fixe. Recommandation : l'état des actions est **échantillonné une fois par pas fixe**, et ce qu'on donne à `update()` peut être enregistré et rejoué (utile dès maintenant pour des captures de référence avec des entrées).
- **Plusieurs manettes, plusieurs joueurs ?** Le jeu est solo : une seule manette active, la dernière utilisée.
- **Liaisons modifiables en jeu dès ce jalon ?** Le fichier suffit ; un écran de configuration viendra avec l'interface du jeu.

### Pièges connus

- **L'appui perdu** : le joueur appuie et relâche entre deux pas fixes (possible à 60 Hz avec une frame lente) ; si l'on lit seulement l'état « enfoncé » au moment du pas, l'appui n'existe jamais. Accumuler les transitions depuis le dernier pas.
- **L'appui compté deux fois** : deux pas fixes dans la même frame voient tous les deux « vient d'être enfoncé ». Le consommer au premier pas.
- **Aucun pas fixe dans la frame** (écran à 165 Hz, logique à 60 Hz) : les transitions doivent attendre le prochain pas, pas disparaître à la frame suivante.
- La **répétition de touche** du système (`event.key.repeat`) : l'ignorer pour les actions.
- La position de la souris en points et non en pixels sur un écran Retina (déjà rencontré au jalon 3) : convertir **une seule fois**, dans `Input`.
- Perte du focus de la fenêtre avec une touche enfoncée : sans précaution, elle reste « enfoncée » pour toujours. Tout relâcher à `SDL_EVENT_WINDOW_FOCUS_LOST`.
- Manettes sur Mac : les boutons « face » s'appellent Sud / Est / Ouest / Nord dans SDL3 (`SDL_GAMEPAD_BUTTON_SOUTH`...), qui correspondent à A / B / X / Y sur une Xbox et à Croix / Rond / Carré / Triangle sur une PlayStation. Lier par position, pas par nom.

### Validation

### Implémentation réalisée (partie 4)

Fichiers : `input.hpp` / `input.cpp` (moteur), `application.hpp` / `application.cpp`, `assets/input/demo3d.json`, la démo 3D et `main.cpp` du bac à sable. Aucune bibliothèque de plus : SDL3 fait déjà le clavier, la souris et les manettes (sa base de manettes reconnues est intégrée).

- **Actions** : le jeu déclare des boutons (`add_button`) et des axes 2D (`add_axis`) par leur nom ; il lit dans `update()` `down`, `pressed`, `released`, `presses` (combien de fois : les crans de molette) et `axis` (longueur au plus 1). `pointer()` donne la souris en pixels.
- **Sources** : `key:<nom>` (touche **physique**, nom SDL de la touche sur un clavier QWERTY US), `mouse:left|right|middle|x1|x2`, `wheel:up|down`, `pad:<bouton>` (a, b, x, y par position, épaules, croix, sticks cliqués...), `pad:<axe>+|-` (gâchette ou direction de stick comme bouton, seuil 0,5). Un axe prend quatre listes de sources (haut, bas, gauche, droite) et/ou un stick, avec une **zone morte radiale** de 0,2 remise à l'échelle (pas de saut en sortant de la zone).
- **Noms affichés** : `describe(action, périphérique)` écrit la touche **telle qu'elle est gravée sur le clavier du joueur** (`SDL_GetKeyFromScancode`) : sur un AZERTY, « A, Z, E, R, T » pour `key:Q W E R T`. Boutons de manette aux noms d'une manette Xbox (A, B, X, Y, LB, RB, LT, RT, croix).
- **Profils** : le fichier des touches (JSON, version 1) en contient plusieurs ; le fichier du joueur (même format, `demo3d_bindings.json` dans `SDL_GetPrefPath("moteur", "bac_a_sable")`) choisit le profil et ne garde que ce qu'il change. Changer de profil dans le panneau l'écrit. Un fichier par défaut invalide lève une erreur qui le nomme ; celui du joueur est ignoré avec un message ; un profil inconnu du joueur aussi.
- **Pas fixe** : les appuis et relâchements s'accumulent jusqu'au prochain `update()`, puis `Application` appelle `end_tick()` : un appui bref n'est jamais perdu, jamais vu deux fois, et une image sans tick le garde pour la suivante. Les répétitions du système sont ignorées ; perdre le focus relâche tout ; débrancher la manette relâche ses boutons. ImGui garde ses événements, **sauf les relâchements** (un bouton enfoncé dans le jeu et relâché sur une fenêtre ne reste pas enfoncé).
- **Enregistrement et rejeu** : `--record-input fichier` écrit, tick par tick, ce que `update()` a lu (état des actions et pointeur ; seulement ce qui n'est pas au repos) ; `--replay-input fichier` le rejoue à la place des vraies entrées. Les actions y sont nommées : un enregistrement reste lisible si le jeu gagne ou perd des actions.
- **Démo 3D** : deux profils dans `assets/input/demo3d.json`, ceux demandés :

| | Profil « clic » (par défaut) | Profil « zqsd » |
|---|---|---|
| Se déplacer | clic gauche (maintenu : suit le pointeur) | Z Q S D |
| Compétences | A Z E R T = 1 à 5, clic droit = 6 | A E R F = 1 à 4, clic droit = 5 |
| Zoom | molette | molette |
| Communs | flèches : caméra ; clic milieu : choisir une créature ; Tab : la suivante ; Espace : la suivre ; P : projection | idem |
| Manette | stick gauche : se déplacer ; A B X Y RB RT : compétences ; stick droit : caméra ; croix haut / bas : zoom ; LB : suivante ; R3 : suivre | idem (5 compétences) |

  La créature choisie joue le personnage ; une compétence affiche « Compétence N » au-dessus d'elle (empilées si plusieurs). Le clic vise le sol avec la caméra **du tick** (et non celle, interpolée, de la dernière image) : le rejeu ne dépend pas de la cadence. La ligne d'aide n'affiche que le périphérique utilisé en dernier. Le panneau « Commandes » : choix du profil, chaque action avec ses touches et son état en direct, dernier périphérique, nombre de manettes, compteurs de compétences.
- **Suivre** passe de F à **Espace** : F est la compétence 4 du profil « zqsd ».

**Vérifications faites (Windows)**

- **232** tests unitaires (218 avant) : `test_input` (sources lues et réécrites, appui bref vu une fois, appui maintenu, appuis accumulés sans tick, deux sources d'une action, répétition ignorée, crans de molette, diagonales unitaires, zone morte et axe y du stick, gâchette comme bouton, profils, fichier du joueur relu, fichiers invalides, entrées coupées, perte du focus, rejeu par nom d'action).
- Un enregistrement de 70 ticks (Tab, Espace, clic gauche maintenu, un cran de molette, compétences 3 et 6 ; `tests/data/demo3d_replay.json`) rejoué trois fois (`--demo3d --seed 42 --freeze-after 60 --run-seconds 3 --replay-input ...`) : **`858706ad9b36`** chaque fois. Premier essai instable : l'estompage des « Compétence N » comptait les ticks même la scène gelée, donc la capture dépendait du nombre de ticks avant l'image capturée ; il compte maintenant les ticks non gelés.
- Démo sans entrée (`--no-input`) : **`250dd83ef6bc`**, stable ; différente de `d41d301beead` **seulement** dans les deux lignes d'aide (y de 103 à 140), qui décrivent maintenant les touches du profil.
- Sur ce PC (clavier AZERTY), l'aide affiche « A, Z, E, R, T » pour les compétences du profil « clic ».
- Build Debug sans avertissement, aucun objet sans dépendances (`ninja -t deps`) ; démo 3D (avec et sans rejeu), « Rendu 3D » et démo 2D sans message `D3D12`.
- **Pas vérifié** : jeu réel à la manette et branchement / débranchement (je n'ai pas de manette sous la main dans cette session), les deux profils joués à la main.

### Validation

- [ ] La démo 3D se joue au clavier-souris **et** à la manette, sans code propre au périphérique dans la scène. *Aucun code propre au périphérique dans la scène ; à essayer à la main, surtout la manette.*
- [ ] Brancher ou débrancher la manette en jeu ne plante pas et est pris en compte. *Code en place, à essayer.*
- [x] Un fichier de liaisons du joueur remplace les valeurs par défaut ; un fichier invalide est signalé et ignoré.
- [x] Les tests unitaires couvrent l'appui perdu, l'appui compté deux fois, la zone morte et la lecture du fichier.
- [x] Une capture avec entrées **rejouées** donne le même hachage à chaque lancement.
- [ ] Mac : clavier AZERTY / QWERTY, manette (voir [TEST_MAC.md](TEST_MAC.md#jalon-4--systèmes-de-base)).

---

## 5. ECS (EnTT)

### But

Décrire les objets du monde comme des **entités** portant des **composants** (des données), traités par des **systèmes** (des fonctions), plutôt que par des classes qui mélangent données et comportement. La démo 3D est le premier client : ses créatures, torches et objets de décor deviennent des entités.

### Rappel du modèle

- Une **entité** est un identifiant.
- Un **composant** est une structure de données simple : `Transform`, `MeshInstance` (poignée de maillage et matériau), `PointLightComponent`, `Health`...
- Un **système** parcourt les entités qui ont certains composants et les met à jour : « déplacer tout ce qui a une `Position` et une `Velocity` », « envoyer au `MeshBatcher` tout ce qui a un `Transform` et un `MeshInstance` ».

EnTT stocke chaque type de composant dans un tableau contigu (*sparse set*) : les parcours sont rapides, et l'ajout ou le retrait d'un composant est bon marché.

### Tâches

- [x] Ajouter `entt` à `vcpkg.json` et à `credits.json` (MIT). *Fait dès la partie 2 (poignées d'assets).*
- [x] Définir les **composants du moteur** (ceux qui ne connaissent rien de l'ARPG) : transformation (avec l'état précédent pour l'interpolation), maillage ou modèle à dessiner, lumière ponctuelle, billboard, nom (pour le debug), et, à voir, une hiérarchie parent / enfant.
- [x] Écrire les **systèmes du moteur** : la copie « état courant → état précédent » au début de chaque pas fixe, et la **collecte pour le rendu** (entités → `MeshBatcher`, lumières, billboards), avec l'interpolation `alpha`.
- [x] Définir où vit le registre (voir les questions) et comment le jeu y ajoute ses propres composants et systèmes.
- [x] Définir l'**ordre des systèmes** dans un pas fixe, écrit à un seul endroit.
- [x] Convertir la **démo 3D** : `StaticDraw`, `Creature` et `Torch` deviennent des entités ; la sélection devient une entité (ou un composant `Selected`). La capture de référence doit rester **identique**, ou l'écart doit être expliqué.
- [x] Mesurer : le coût de la collecte pour 1 000 créatures et 10 000 objets (`--meshes N`), comparé aux tableaux du jalon 3. *Mesuré sur la démo 3D (`--creatures 1000 --decor 10000`), qui existe dans les deux versions ; `--meshes N` appartient à la scène « Rendu 3D », restée sans entités.*

### Questions à se poser

- **Tout le jeu en ECS, ou seulement le monde ?** Recommandation : **seulement le monde** (ce qui a une position dans la carte : personnages, objets, lumières, effets). Les systèmes du moteur (renderer, audio, gestionnaire d'assets, entrées) restent des classes ; l'interface du jeu et les menus aussi. C'est ce que font la plupart des moteurs qui adoptent un ECS après coup, et cela garde le moteur lisible.
- **Un registre par scène, ou un seul ?** Recommandation : **un registre par scène de jeu** (partie 6) : fermer une scène, c'est détruire son registre, sans rien oublier.
- **Hiérarchie (une arme attachée à la main, une torche à un mur) ?** L'attache à un os est prévue au jalon 5. Recommandation : un composant `Parent` simple, et la transformation monde recalculée par un système, **seulement quand un cas en a besoin** ; pas de graphe de scène général.
- **Les composants peuvent-ils contenir des pointeurs ?** Recommandation : **non**, seulement des valeurs et des poignées (assets, autres entités). C'est ce qui rendra la sauvegarde (jalon 7) et l'inspecteur (partie 8) simples.
- **Déterminisme** : l'ordre de parcours d'une vue EnTT dépend de l'ordre des créations et destructions. Il est le même à chaque lancement pour une même suite d'opérations *(à vérifier : aucune dépendance aux adresses ou au hachage)*. Les systèmes qui dépendent de l'ordre (qui frappe en premier) doivent le fixer eux-mêmes (tri par identifiant stable).
- **Signaux d'EnTT** (`on_construct`, `on_destroy`) : utiles pour libérer une ressource liée à une entité. À utiliser avec parcimonie, car ils rendent le flux moins lisible.

### Pièges connus

- Détruire des entités ou ajouter des composants **pendant** qu'on parcourt une vue qui les contient : comportement indéfini selon les cas. Marquer, puis détruire après le parcours.
- Garder une référence vers un composant après un ajout dans le même tableau : le tableau peut se réallouer.
- Recréer une entité : EnTT réutilise les identifiants avec une nouvelle version. Une entité gardée par un autre composant doit être vérifiée (`registry.valid()`).
- Transformer chaque petite structure en composant : trop de composants rend les systèmes difficiles à suivre. Commencer grossier.
- Mettre du gameplay ARPG dans les composants du moteur (`Health` n'est pas au moteur).

### Implémentation réalisée (partie 5)

Fichiers : `world.hpp` / `world.cpp` (moteur), `make_asset` dans `asset_cache.hpp`, `test_world.cpp`, la démo 3D et `main.cpp` du bac à sable. Aucune bibliothèque de plus : EnTT 3.16 est là depuis la partie 2.

- **`moteur::World`** : le registre d'**une** scène (`world.registry()`) et les systèmes du moteur. La scène la possède : la fermer détruit tout son monde d'un coup. Le jeu ajoute ses composants au même registre et écrit ses systèmes comme des fonctions sur des vues.
- **Composants du moteur**, des valeurs et des poignées : `Transform` (position, rotation en **quaternion**, échelle), `PreviousTransform` (l'entité **bouge** : son `Transform` au tick précédent), `Parent`, `Name`, `Hidden`, `MeshComponent` (poignée de maillage et matériau), `ModelComponent` (poignée de modèle), `LightSource`, `Billboard` (poignée de texture). Exception : les textures d'un `Material` restent des pointeurs, vers un asset que l'entité tient aussi (un modèle) ; à revoir si l'inspecteur ou la sauvegarde en ont besoin. Le nom `MeshInstance` était déjà pris par le `MeshBatcher` (l'instance envoyée au GPU), d'où `MeshComponent`.
- **Ordre d'un pas fixe**, écrit dans le `update()` de la scène : `world.begin_tick()` (`PreviousTransform` ← `Transform`), puis les systèmes du jeu dans l'ordre. À chaque image : `world.submit(renderer, alpha, options)`.
- **Collecte** (`collect(WorldSink&, ...)` ; `submit` la branche sur `MeshRenderer` et `BillboardRenderer`, les tests l'enregistrent) : maillages, modèles, lumières, billboards, chacun **dans l'ordre de création** (`storage.reach()` : l'ordre d'EnTT est l'inverse). Lumières : les `max_lights` plus proches de `light_focus`, à égalité la première créée. Les entités qui bougent sont interpolées ; les entités attachées suivent leur parent (le dernier parent calculé est gardé pour le frère suivant : le corps, puis la tête).
- **Entités fixes** (ni `PreviousTransform` ni `Parent`) : matrice et boîte calculées **une fois** et gardées dans un composant interne, boîtes des parties d'un modèle comprises. Les signaux d'EnTT effacent ce cache quand le `Transform` change par `patch` / `replace`, quand le parent, le maillage ou `Hidden` changent, ou quand l'entité se met à bouger : c'est leur seul usage, comme le recommandait la question plus haut. Un `Transform` écrit directement (par `get<>()`) n'est vu que des entités qui bougent : c'est documenté et testé.
- **Hiérarchie** : `Parent` simple, sans graphe de scène. Le `Transform` de l'enfant est relatif ; la matrice monde se calcule en remontant la chaîne. Un enfant dont le parent a disparu n'est pas dessiné, une boucle non plus (profondeur limitée à 64) ; `World::destroy()` détruit les enfants avec le parent ; un parent caché cache ses enfants.
- **Démo 3D** : plus aucun tableau d'objets du monde. Sol, murs, poteaux et décor sont des entités fixes (un tonneau : une entité avec `ModelComponent`). Une créature est une entité qui bouge, avec deux composants **de la démo** (`Walker` : vitesse et destination ; `Health`), un `Name`, et son corps et sa tête **attachés**. Une torche est une entité avec flamme, `LightSource` et halo `Billboard`. La sélection est un `entt::entity` ; l'**anneau** est une entité attachée à la créature choisie (on change son `Parent`), la **marque de destination** une entité fixe déplacée par `replace` et cachée (`Hidden`) quand il n'y a pas de destination. Les « Compétence N » affichées gardent l'entité de leur créature. « La suivante » suit l'ordre de création ; la créature la plus proche du clic, à égalité, est la première créée, comme avant.
- Panneau : nombre d'entités, entités fixes gardées, temps moyen de la collecte ; la ligne de commande écrit `world collection: X ms per frame`.

**Vérifications faites (Windows)**

- **242** tests unitaires (232 avant) : `test_world` (matrice d'un `Transform`, interpolation exacte à l'arrêt, `begin_tick` et interpolation, cache gardé puis effacé par `patch` et par `PreviousTransform`, enfants sur deux niveaux, parent caché, destruction en cascade, orphelin et boucle non dessinés, ordre de création, lumières les plus proches avec égalités, parties d'un modèle gardées, `Hidden`, nombre d'entités).
- **Capture** : la démo convertie donne **`331290226414`** (`--no-input`) et **`debc4a3b811c`** (`--replay-input tests/data/demo3d_replay.json`), stables sur plusieurs lancements, au lieu de `250dd83ef6bc` et `858706ad9b36`. L'écart vient **seulement** de la rotation : `Transform` la garde en quaternion, dont la matrice (`mat4_cast`) diffère de celle de `glm::rotate` d'un arrondi. Preuve : l'ancienne démo (tableaux), modifiée pour calculer ses rotations par quaternion, donne exactement ces deux hachages. Sur l'image, **5 pixels** sur 921 600 changent, d'au plus 2 / 255.
- **Collecte** (temps CPU par image, des entités aux files du renderer ; Release, moyenne sur 8 s, deux lancements) :

| | Tableaux (jalon 3) | Entités |
|---|---|---|
| 300 créatures, 3 000 objets (défaut) | 0,31 à 0,34 ms | **0,25 ms** |
| 1 000 créatures, 10 000 objets | 0,77 à 0,81 ms | **0,66 à 0,68 ms** |

  La première version prenait 0,35 et 0,91 ms (cinq recherches par entité fixe, parent recalculé pour chaque enfant, boîtes des parties des tonneaux recalculées à chaque image). Garder ces boîtes a fait l'essentiel (0,86 → 0,67 ms). L'avance sur les tableaux vient sans doute de ce que l'ancienne démo recalculait `glm::rotate` (un sinus et un cosinus) pour chaque corps et chaque tête à chaque image, ce qu'un quaternion identité évite (non mesuré à part). En **Debug**, la collecte coûte environ 16 ms (EnTT non optimisé, avec ses assertions) : la démo y tourne vers 35 images par seconde.
- Build Debug sans avertissement, aucun objet sans dépendances (`ninja -t deps`), démo 3D avec rejeu en Debug sans message `D3D12`.

### Validation

- [x] La démo 3D ne contient plus de tableaux d'objets du monde : tout passe par le registre. *Restent hors du registre : les « Compétence N » affichées (interface, qui garde l'entité) et la case survolée (curseur).*
- [x] La capture de référence est identique (ou l'écart est expliqué), et les performances de la collecte sont mesurées et comparables à celles du jalon 3. *Écart de 5 pixels expliqué et prouvé ; collecte plus rapide qu'avant.*
- [x] Les systèmes du moteur ont leurs tests unitaires sur un registre sans GPU.
- [ ] Mac : mêmes hachages de capture (voir [TEST_MAC.md](TEST_MAC.md#jalon-4--systèmes-de-base)).

---

## 6. Scènes et états de jeu

### But

Passer proprement d'un écran à l'autre (titre, chargement, jeu, pause, options), en chargeant et en libérant ce qu'il faut, avec une **pile** qui permet de poser un état par-dessus un autre (la pause par-dessus le jeu, qui reste visible mais figé).

### Vocabulaire

- Un **état** (*game state*) est un écran avec sa logique : écran titre, jeu, pause, options.
- Une **scène** (au sens du monde) est ce que l'état « jeu » charge : une carte, son registre d'entités et ses assets.

Les deux notions sont souvent confondues. Recommandation : garder le mot **état** pour la pile, et **scène** (ou « niveau ») pour le monde chargé.

### Tâches

- [x] Une **pile d'états** dans le moteur : pousser, retirer, remplacer ; chaque état reçoit `enter`, `exit`, `update`, `render`, et sait s'il laisse voir (**transparent**) et vivre (**bloquant ou non**) l'état en dessous.
- [x] Les transitions ont lieu **entre deux frames**, jamais au milieu d'un `update`. *Au début du `update()` suivant : entre deux ticks.*
- [x] À la sortie d'un état de jeu, son registre est détruit, puis le gestionnaire d'assets libère ce que plus personne n'utilise (partie 2).
- [x] Un **état de chargement** simple : affiche une progression pendant que les assets de la scène se chargent (même synchrones, pour préparer l'asynchrone).
- [x] Les **actions** (partie 4) vont à l'état du dessus ; un état bloquant ne laisse rien passer en dessous.
- [x] Décider du sort du menu **DEBUG > Tests moteur** (voir les questions). *Il reste à part ; le test des états y est une scène.*
- [x] Une scène de test : titre → jeu → pause → reprise → retour au titre, en boucle, sans fuite (compteurs d'assets et de ressources GPU stables).

### Questions à se poser

- **Pile ou machine à états ?** Une pile couvre la pause et les menus par-dessus le jeu ; une machine à états plate oblige à tout reconstruire. Recommandation : **pile**.
- **Qui porte la boucle de jeu ?** Aujourd'hui, `Application::run()` appelle un seul `Game`. Recommandation : la pile d'états **est** un `Game` (ou le devient), pour que `Application` ne change pas.
- **Le menu des tests du bac à sable utilise-t-il la pile ?** Recommandation : oui, si cela ne coûte rien : chaque test devient un état, et le menu d'ImGui pousse ou remplace. Sinon, le laisser tel quel : ce n'est pas le jeu.
- **Que devient la simulation en pause ?** Elle s'arrête (aucun `update` de l'état du dessous), mais le rendu continue avec l'`alpha` figé.

### Pièges connus

- Changer d'état pendant qu'on parcourt la pile : le faire en fin de frame.
- Libérer les assets d'une scène **avant** d'avoir chargé la suivante, alors qu'elles en partagent : tout est rechargé. Charger la suivante, puis libérer.
- Un état en pause qui continue de recevoir le temps réel et « rattrape » à la reprise : le pas fixe doit repartir à zéro.

### Implémentation réalisée (partie 6)

Fichiers : `state_stack.hpp` / `.cpp` et `process_memory.hpp` / `.cpp` (moteur), compteurs dans `gpu_resource.hpp`, `Input::set_muted`, `Application::restart_clock`, `test_state_stack.cpp`, `states_demo.hpp` / `.cpp` (bac à sable). Aucune bibliothèque de plus.

- **`GameState`** : `enter` / `exit`, `covered` / `uncovered` (un état posé par-dessus, puis retiré : la musique de la pause, partie 9), `on_event`, `update`, `render`, `transparent()`, `blocking()`, et `name()` pour les outils de la partie 8.
- **`StateStack`** est un `Game` : `Application` ne change pas (question « qui porte la boucle »). `push`, `pop`, `replace`, `reset` sont des demandes, appliquées **au début du `update()` suivant** ; celles que demande un `enter()` passent dans le même lot. Un `replace` ou un `reset` reçoit un état **déjà construit** : la scène suivante est chargée avant le départ de la précédente, puis la pile appelle `collect_garbage()` une fois le lot appliqué (piège « libérer avant de charger »).
- **Mise à jour** : de bas en haut, à partir du premier état bloquant en partant du haut. **Dessin** : à partir du premier état opaque. Un état qui ne tourne plus garde l'**`alpha` de sa dernière image** : sinon son monde, figé entre deux ticks, tremblerait. À la reprise, rien à rattraper : l'état n'a pas reçu de temps pendant la pause.
- **Entrées** : les événements SDL vont à l'état du dessus ; les états en dessous qui tournent encore (sous un état non bloquant) lisent une `Input` **muette** (`set_muted` : actions au repos, pointeur dehors), sans que rien ne soit perdu. Les actions restent déclarées une fois pour le jeu : la démo déclare `pause` (Échap, Start) avec les siennes, que son état de jeu et la pause lisent.
- **`LoadingState`** : des étapes nommées, **une par tick**, puis la dernière construit l'état suivant et le met à sa place. Après chaque étape, `Application::restart_clock()` : le temps du chargement n'est pas dû à la logique, qui ne rattrape pas de ticks. Un chargement prend donc toujours le même nombre de ticks, quelle que soit la machine : un rejeu d'entrées reste calé. Ce que les étapes chargent est gardé par un objet que la dernière étape tient aussi ; il part avec l'état de chargement, après la construction du jeu.
- **Fuites** : `gpu_resource_counts()` compte les objets GPU vivants (tenus par `GpuResource`, soit tous ceux qui durent) ; `process_memory_bytes()` donne la mémoire du processus.
- **Scène de test** « États de jeu » (menu, ou `--states`, `--states-cycles N`) : titre, chargement (police, tonneau, puis la démo 3D entière), jeu, pause transparente et bloquante (monde assombri, « Reprendre », « Retour au titre »). Échap appartient à la scène pendant le jeu et la pause (`SandboxScene::uses_escape()`), et arrête le test ailleurs.
- **Captures de la démo 3D rendues stables** : en vérifiant que la partie ne changeait pas les captures, le rejeu a donné trois hachages en trois lancements. Deux causes, antérieures à cette partie : une fois gelée, la démo dessinait la case survolée au pointeur de l'image (en rejeu, il dépend du nombre de ticks passés avant elle), et la ligne de statistiques affiche les chiffres de l'image précédente, qui pouvait être la dernière avant le gel. Maintenant, le survol gelé suit le pointeur du dernier tick vivant, et la capture attend que les statistiques viennent d'une image gelée. Nouveaux hachages : **`fdc076d9c3ae`** (`--no-input`, 3 lancements sur 3) et **`952477744ffb`** (`--replay-input tests/data/demo3d_replay.json`, 8 sur 8). Seule la ligne de statistiques diffère des anciennes captures (zone y = 83 à 99).

**Vérifications faites (Windows)**

- **255** tests unitaires (242 avant) : `test_state_stack` (poussée différée, ordre `built` / `covered` / `enter` / `exit` / `destroyed` / `uncovered` pour push, pop, replace et reset, transitions demandées pendant un tick, état bloquant qui fige le dessous pendant 100 ticks puis reprise sans rattrapage, transparence, `alpha` figé, événements et actions au seul état du dessus, une collecte par lot, sortie de tous les états à la destruction, `replace` depuis `enter()`, chargement étape par étape avec horloge relancée et assets gardés jusqu'à la construction du jeu, échec d'une étape).
- **100 cycles** (`--states-cycles 100`, Release, 114 s) : **0** asset et **51** objets GPU à chaque retour au titre à partir du premier cycle (47 au départ : le premier jeu crée pour de bon les cartes d'ombre et leurs pipelines), **aucune pause où le monde a bougé**, mémoire du processus entre 192 et 197 Mo sans tendance (le minimum reste à 192,5 Mo du cycle 1 au cycle 100).
- **Debug** : `--states-cycles 5` sans message `D3D12`, mêmes compteurs ; build sans avertissement, aucun objet sans dépendances (`ninja -t deps`).
- Capture de la première pause (`--states-cycles 3 --capture`) : la démo 3D figée et assombrie sous la fenêtre de pause.

### Validation

- [x] Titre → jeu → pause → jeu → titre, cent fois (automatisé), sans fuite de mémoire ni de ressources GPU, sans message de validation. *Validation GPU vérifiée sur 5 cycles en Debug.*
- [x] La pause fige la simulation, le monde reste affiché dessous.
- [x] Les tests unitaires couvrent la pile (ordre des appels, transparence, transitions différées).
- [ ] Mac : mêmes vérifications (voir [TEST_MAC.md](TEST_MAC.md#jalon-4--systèmes-de-base)).

---

## 7. Audio

### But

Jouer des **effets** (pas, impacts, sorts, interface) placés dans le monde, et de la **musique** qui s'enchaîne entre les états, avec des volumes par catégorie réglables par le joueur.

### Ce qu'un ARPG demande à l'audio

- **Beaucoup de sons simultanés** : des dizaines de créatures qui frappent, des sorts. Il faut limiter le nombre de voix, et les voix d'un même son (dix impacts identiques dans la même frame ne font pas dix fois plus de bruit).
- Des sons **dans le monde** : volume selon la distance à l'auditeur, gauche / droite selon la position à l'écran. Avec une caméra fixe, un panoramique simple suffit ; pas besoin de vrai son 3D (HRTF).
- La **musique** en flux (*streaming*) depuis le disque, avec des fondus enchaînés.
- Des **groupes** (musique, effets, interface, ambiance) avec leur volume, et une pause de tout sauf l'interface.
- Des **variations** : un pas joue l'un de plusieurs fichiers, avec un peu de hasard sur la hauteur et le volume.

### Tâches

- [x] Choisir la **bibliothèque** (voir les questions), l'ajouter à `vcpkg.json` et à `credits.json`. *miniaudio 0.11.25, choix validé le 2026-09-24.*
- [x] Une classe `Audio` dans le moteur : ouvrir le périphérique de sortie, jouer un son (à une position, ou sans position), jouer une musique, groupes et volumes, arrêt de tout.
- [x] Les **sons** sont des assets (partie 2) : décodés en mémoire pour les effets courts, lus en flux pour la musique.
- [x] L'**auditeur** suit la caméra (ou le personnage : voir les questions) ; les sons placés sont atténués et panoramiqués. *`set_listener(camera)` : le point du sol au centre de la vue ; la démo 3D s'y branchera en partie 9.*
- [x] Une **limite de voix** et une limite par son, avec une règle claire pour les sons qui ne sont pas joués.
- [x] Les **événements sonores** viennent de la logique à pas fixe mais se jouent en fin de frame : la logique pousse « jouer tel son à tel endroit » dans une file, l'audio la vide. La simulation ne dépend jamais de l'audio.
- [x] Changement de **périphérique de sortie** (casque branché ou débranché) sans plantage. *Écrit (suivi du périphérique par défaut, relance chaque seconde) ; **non essayé à la main** : voir la validation.*
- [x] Trouver des **sons et musiques de test** libres, les télécharger par un script (hors de Git, comme les modèles), les créditer.
- [x] Une scène de test « Audio » : jouer chaque son, déplacer une source autour de l'auditeur, volumes par groupe, nombre de voix actives.
- [x] Les volumes du joueur sont sauvegardés avec ses préférences (même dossier que les touches).

### Questions à se poser

**Quelle bibliothèque ?**

| | SDL3 seul | SDL3_mixer 3 | miniaudio |
|---|---|---|---|
| Déjà là | Oui | Non (vcpkg `sdl3-mixer`, *à vérifier*) | Non (vcpkg `miniaudio`, *à vérifier*) |
| Licence | zlib | zlib | domaine public ou MIT-0 |
| Mixage de plusieurs sons | À écrire | Oui | Oui |
| Décodage (WAV, OGG, FLAC, MP3) | WAV seulement | Oui | WAV, FLAC, MP3 ; OGG avec `stb_vorbis` |
| Flux pour la musique | À écrire | Oui | Oui |
| Position, atténuation, panoramique | À écrire | Oui depuis la version 3 *(à vérifier)* | Oui (moteur de haut niveau, spatialisation) |
| Groupes, fondus | À écrire | Pistes et groupes *(à vérifier)* | Groupes de sons, fondus |
| Maturité | — | API entièrement refaite pour SDL3, récente *(à vérifier)* | Très utilisée, stable depuis longtemps |

- **SDL3 seul** : tout le mixage, le décodage, le flux et l'atténuation seraient à écrire. Formateur, mais c'est une partie entière de travail pour refaire ce qui existe.
- **SDL3_mixer** : reste dans la famille SDL, même style d'API, même licence ; sa version 3 a été redessinée (pistes, mixage, position). Son âge est le risque *(à vérifier : date de sortie et stabilité de la 3.0)*.
- **miniaudio** : un seul fichier, aucune dépendance, un moteur de haut niveau qui fait déjà tout ce que la liste ci-dessus demande (sons, groupes, spatialisation, flux, fondus), sur Windows (WASAPI) et Mac (Core Audio). Il ouvre son propre périphérique, sans passer par SDL.

Recommandation : **miniaudio**, pour sa maturité et parce qu'il couvre toute la liste sans code à écrire ; **SDL3_mixer** est une bonne alternative si l'on préfère rester dans SDL, à condition de vérifier que sa version 3 est stable. Écarter « SDL3 seul » pour ce jalon.

**Autres questions**
- **Format des fichiers** : recommandation **OGG Vorbis** pour la musique et les ambiances (compressé, libre), **WAV** pour les effets courts (aucun décodage). FLAC seulement comme source.
- **Où est l'auditeur ?** Avec une caméra isométrique, placer l'auditeur **sur le personnage** (ou le point du sol visé par la caméra) donne des distances qui ont du sens ; placé sur la caméra, tout est à 15 m. Recommandation : **point du sol au centre de la vue**, orienté comme la caméra.
- **Taux d'échantillonnage et latence** : laisser le périphérique décider ; mesurer la latence entre un clic et le son (doit rester sous ~50 ms *(à vérifier)*).

### Pièges connus

- Appeler l'audio depuis le fil audio de la bibliothèque (rappels) : ne rien y faire de lent, aucun accès au registre.
- Un son déclenché à chaque pas fixe au lieu d'une fois (un événement « vient d'être enfoncé » mal géré, un contact répété) : un bourdonnement. La file d'événements sonores permet de le repérer.
- Le son qui continue quand la fenêtre perd le focus, ou pendant la pause : décider et appliquer (les jeux coupent souvent la musique en arrière-plan en option).
- Le périphérique qui disparaît (casque débranché) : certains backends arrêtent tout sans erreur. Tester.
- Des fichiers de test sans licence claire : ne garder que du CC0 ou équivalent, crédité.

### Implémentation réalisée (partie 7)

Fichiers : `audio.hpp` / `.cpp`, `audio_rules.hpp` / `.cpp`, `sound.hpp` / `.cpp`, `miniaudio.c` (moteur), `sound()` et `music()` dans `Assets`, `Application::audio()`, `test_audio.cpp`, `audio_test.hpp` / `.cpp` (bac à sable), `tools/audio/fetch_test_sounds.py`. Bibliothèque ajoutée : **miniaudio** 0.11.25 (vcpkg, un seul en-tête, domaine public ou MIT-0), compilée en C dans une petite bibliothèque à part (`moteur_miniaudio`, sans nos avertissements), avec `stb_vorbis.c` (déjà installé par le paquet `stb`) pour l'OGG. Sur Mac, elle se lie à CoreAudio, AudioToolbox et CoreFoundation.

- **`Audio`** (`app.audio()`) : le moteur de haut niveau de miniaudio (`ma_engine`), dont on n'utilise que le graphe de mixage. Pas son gestionnaire de ressources : les sons passent par **nos** assets (lecture par `read_file`, cache, rechargement à chaud, remplaçant). Quatre **groupes** (musique, effets, ambiance, interface), chacun avec son volume et sa pause, et un volume général.
- **Sons** : `Sound` (effet court, décodé une fois en échantillons flottants ; plus de deux canaux ramenés à deux) et `Music` (musique ou ambiance, gardée compressée en mémoire et décodée pendant la lecture). Leurs données sont en `shared_ptr` : une voix garde celles qu'elle joue, et un rechargement à chaud (qui remplace l'asset en place) ne retire rien sous le fil audio. Un son qui manque est remplacé par un **bip** (entendu plutôt que muet), une musique qui manque par le silence ; le message nomme le fichier.
- **Jouer** : `play(son, PlaySound)` (groupe, volume, hauteur, variations au hasard, position, priorité, boucle, fondu d'entrée) ou `play(variantes, ...)` (l'une au hasard : les pas). `play_stream()` pour une ambiance longue, en flux. `play_music()` / `stop_music()` avec **fondu enchaîné**, `set_music_volume()` pour baisser la musique sous une pause. `stop`, `set_position`, `set_volume`, `playing` par `SoundId`.
- **File** : `play()` ne fait que mettre en file ; `Audio::update()`, appelé par l'`Application` une fois par frame après le dessin, la vide. L'aléatoire des variations a son propre générateur : la simulation ne lit jamais rien de l'audio.
- **Règles** (`audio_rules`, fonctions pures) : `place_sound()` (gain de 1 jusqu'à 4 m, puis carré de la distance restante jusqu'à 0 à 30 m ; panoramique selon le décalage vers la droite de l'écran, complet à 12 m, plafonné à 0,8) ; `merge_requests()` (les demandes d'une frame pour le même fichier n'en font qu'une, la plus forte) ; `decide_voice()` (32 voix, 4 par fichier ; au-delà, prendre la place de la plus faible, c'est-à-dire la priorité la plus basse, puis la plus douce, puis la plus ancienne, si l'on est au moins aussi fort, sinon ne pas jouer ; un son inaudible n'est pas joué). Musique et flux sont hors de ces limites.
- **Limiteur** : ajouté après le premier essai sur le vrai périphérique, où la rafale de 200 impacts montait la crête à **3,3** (les impacts de Kenney sont enregistrés presque à pleine échelle : 20 voix ensemble saturent, limite de voix ou pas). En fin de mixage, sur le fil audio : attaque immédiate, relâchement de 150 ms, plafond à 0,9. Crêtes du mélange et de la sortie, et gain le plus bas, dans les statistiques.
- **Auditeur** : `set_listener(Listener)` ou `set_listener(camera)` (la cible de la caméra, et la droite de l'écran sur le sol).
- **Périphérique** : l'`Application` coupe le son quand la fenêtre perd le focus (réglage du joueur, activé par défaut). Les notifications de miniaudio (périphérique arrêté, basculé vers un autre) lèvent des drapeaux lus sur le fil principal : un basculement est écrit dans le log, un arrêt est suivi d'une relance chaque seconde. Sans périphérique, l'`Audio` reste silencieux et le dit. `AudioConfig::device = false` : aucun périphérique, et `mix()` rend la sortie (tests).
- **Réglages du joueur** : `audio.json` dans `SDL_GetPrefPath` (à côté des touches), JSON version 1 (volume général, volume de chaque groupe, muet en arrière-plan) ; lu au démarrage, écrit à la fin s'il a changé. Les curseurs vont de 0 à 1 et le gain est leur **carré** (`slider_gain`), plus proche de l'oreille.
- **Sons de test** (10 Mo, hors de Git, tous en CC0) : `python tools/audio/fetch_test_sounds.py` (fichiers vérifiés par SHA-256). Kenney « Impact Sounds » (5 pas sur la pierre, 5 impacts métalliques) et « Interface Sounds » (clics), en OGG ; d'OpenGameArt, deux musiques (« The Field Of Dreams » de pauliuw pour le titre, « Town Theme RPG » de cynicmusic pour le jeu), une ambiance (« Forgoten tomb ambience » de kindland), en MP3 tels que publiés, et un feu (« Fire Crackling » d'AntumDeluge) en WAV. Crédités dans `credits.json` (section `sounds`, nouvelle section de « À propos »).
- **Scène « Audio »** (menu, ou `--audio`, `--audio-burst`) : une carte vue de dessus (auditeur, distances d'atténuation, sources ; un clic joue un impact à cet endroit), un feu en boucle qui tourne autour de l'auditeur (rayon et vitesse réglables, gain et panoramique affichés), pas, impacts, clics d'interface, musiques du titre et du jeu (fondus, baisse), ambiance en flux, volumes et pauses par groupe, et la rafale de 200 impacts. Panneau : périphérique, voix, compteurs, crêtes, limiteur.
- **Test des états** (partie 6) : chaque écran a sa musique (titre, jeu), en fondu enchaîné ; la pause baisse la musique et met en pause les effets et l'ambiance jusqu'à la reprise. Les deux musiques restent chargées pendant tout le test.

**Vérifications faites (Windows)**

- **268** tests unitaires (255 avant), dont 13 d'audio, qui mixent vraiment grâce au moteur sans périphérique : placement (gain, panoramique), fusion et choix des voix, décodage WAV et fichier invalide, un son placé à droite puis à gauche entendu de ce côté (énergie des canaux) et plus faible au loin, voix et asset libérés à la fin d'un son, 200 demandes sur 20 frames tenues à 16 voix et 4 par son, limiteur (12 sons en phase qui s'additionnent jusqu'à 10,8 : sortie à 0,9, puis gain revenu), groupes (pause, volume au carré), muet sans le focus, volume général, musique en fondu enchaîné, flux en boucle placé, fichier de réglages.
- **Vrai périphérique** (casque, 48 kHz, stéréo, **30 ms** de tampon WASAPI) : `--audio-burst --run-seconds 3` donne au plus **21 voix** (4 par fichier × 5 impacts, et le feu), crête du mélange 2,1 à 2,6, **sortie à 0,90**, limiteur descendu à ×0,35-0,42, en Release comme en Debug.
- **Rechargement à chaud** du feu **pendant qu'il joue** (deux fois) : `sound 'audio/effects/fire_1.wav' reloaded`, sans plantage.
- `--states-cycles 10` avec les musiques : assets et objets GPU stables (les 2 musiques, 4,5 Mo, restent), mémoire sans tendance. Debug : `--states-cycles 3` et la rafale sans message `D3D12`.
- **Latence** : 30 ms de tampon, plus l'attente de la fin de la frame (un son demandé par un tick part à la fin de sa frame) et, pour un clic, celle du tick suivant : environ **45 ms** en moyenne, jusqu'à **63 ms** au pire à 60 images par seconde. Au-dessus des 50 ms visés dans le pire cas ; la marge viendrait d'un tampon plus court (`periodSizeInMilliseconds`), à essayer si un retard se sent.
- **Non vérifié** : débrancher et rebrancher le casque (à faire à la main), et l'écoute des sons de la scène, qui demande une oreille.

### Validation

- [ ] Des effets placés dans le monde s'entendent à gauche, à droite, plus ou moins fort selon leur position, sans craquement. *Vérifié par les tests (énergie des canaux) ; l'écoute reste à faire : scène « Audio », le feu qui tourne.*
- [x] La musique change entre le titre et le jeu avec un fondu, et se met en pause avec le jeu si c'est la règle choisie. *Règle : la musique baisse, le reste s'arrête. Écouté par l'utilisateur sous Windows (2026-09-25), avec les pas, impacts, ambiance et sons des menus de la tranche.*
- [x] 200 sons déclenchés dans la même seconde ne font pas saturer la sortie et respectent la limite de voix. *21 voix au plus, sortie à 0,90 grâce au limiteur.*
- [x] Débrancher et rebrancher le casque ne plante pas. *Essayé à la main sous Windows par l'utilisateur (2026-09-25) : pas de plantage.*
- [ ] Mac : sortie audio, casque, latence (voir [TEST_MAC.md](TEST_MAC.md#jalon-4--systèmes-de-base)).

---

## 8. Outils de debug (ImGui)

### But

Finir l'intégration d'ImGui commencée aux jalons précédents (`DebugUi`, menu des tests, panneaux « Rendu ») par les outils qu'appellent les nouveaux systèmes, **sans** que le moteur dépende des composants du jeu.

### Tâches

- [x] **Inspecteur d'entités** : liste des entités (avec leur nom s'il existe), filtre, sélection, et l'affichage / la modification de leurs composants. Sélection aussi par clic dans le monde (picking de la démo).
- [x] Un **enregistrement des composants** pour l'inspecteur : chaque type de composant (du moteur ou du jeu) fournit son nom et une fonction qui l'affiche avec ImGui. Le jeu enregistre les siens ; le moteur ne les connaît pas.
- [x] **Navigateur d'assets** : ce qui est chargé, par type, sa taille en mémoire, son compteur de références, son état (prêt, erreur, remplacement), et un bouton « recharger ».
- [x] **Entrées** : l'état des actions et des périphériques (reprend la scène de test de la partie 4).
- [x] **Audio** : voix actives, volumes par groupe, dernier son refusé par la limite.
- [x] **Pile d'états** : les états en cours, du dessous au dessus.
- [x] Ranger le tout dans le menu **DEBUG**, chaque fenêtre ouvrable et fermable, son état retenu entre deux lancements (le fichier `imgui.ini`, à placer dans le dossier de préférences et non à côté de l'exécutable).
- [x] Tout ce qui est ImGui reste désactivable (`ApplicationConfig::debug_ui`) et hors de la version livrée du jeu. *Désactivable : oui. Hors de la version livrée : le jeu livré ne mettra pas `debug_ui` ; retirer ImGui de l'exécutable lui-même (option CMake) est renvoyé au packaging (jalon 8).*

### Questions à se poser

- **Réflexion automatique (`entt::meta`) ou enregistrement à la main ?** `entt::meta` permet de décrire les champs d'un composant une fois pour toutes (utile aussi pour la sauvegarde), mais c'est une API de plus à apprendre. Recommandation : **enregistrement à la main** (une fonction d'affichage par composant) pour ce jalon ; revoir au jalon 7 avec la sauvegarde.
- **Modifier les composants depuis l'inspecteur casse-t-il le déterminisme ?** Oui, et c'est voulu : c'est un outil. Le signaler dans le panneau quand une valeur a été modifiée à la main.

### Implémentation réalisée (partie 8)

Fichiers : `debug_tools.hpp` / `.cpp` (moteur), `test_debug_tools.cpp` ; ajouts à `DebugUi` (`set_settings_file`), `Application` (`debug_tools()`), `AssetCache` / `Assets` (erreur de rechargement, `reload(type, clé)`), `Audio` (dernier son refusé, `active_sounds()`) ; dans le bac à sable, `main.cpp` (menu, `--menu-test N`), `demo3d.cpp`, `states_demo.cpp`. Aucune bibliothèque ajoutée.

- **`DebugTools`** (`app.debug_tools()`, nul sans `debug_ui`) : créé par l'`Application` avec l'interface de debug, jamais sans. Le jeu garde la main sur son interface : il met `menu_items()` dans **son** menu DEBUG et appelle `draw()` dans son `render()`. Les scènes autonomes (lignes de commande, captures) ne dessinent donc aucun outil, même avec `debug_ui` (test des états, audio).
- **Inspecteur d'entités** : le monde regardé (`watch(world, nom)` / `forget(world)`, que la démo 3D fait dans son constructeur et son destructeur), la liste triée par numéro (liste virtuelle `ImGuiListClipper` : 6 512 entités sans coût visible), un filtre (texte du nom sans casse, ou numéro `#12`), « Nommées seulement », puis les composants de l'entité choisie : **tous** ceux qu'elle a, parcourus dans les stockages du registre (`registry.storage()`), inscrits ou non. L'entité choisie est encadrée dans le monde (boîte jaune et axes, lignes de debug par-dessus tout ; ses enfants compris : corps et tête d'une créature).
- **Enregistrement des composants** (`ComponentInspectors`) : `add<T>(nom, fonction)` et `add_tag<T>(nom)`. La fonction dessine ses widgets et dit si elle a changé le composant ; elle travaille sur une **copie**, qui remplace le composant par `registry.replace` : les signaux d'EnTT partent, donc une entité fixe déplacée à la main perd sa matrice gardée (testé). Le moteur inscrit les siens (`Transform` en position / angles en degrés / échelle, `Name`, `Parent` avec un bouton vers le parent, `Hidden`, `MeshComponent` avec son matériau, `ModelComponent`, `LightSource`, `Billboard`, `PreviousTransform` en lecture) ; la démo inscrit `Walker` et `Health`. Un type non inscrit apparaît sous son nom C++, sans contenu.
- **Pas d'ajout, de retrait ni de destruction** depuis l'inspecteur (seulement la case « Cachée ») : le premier jet avait « Retirer » et « Détruire », retirés avant l'essai, car la démo garde des entités (la créature choisie, l'anneau, la destination) et lit leurs composants sans vérifier ; un clic aurait fait planter le jeu. Une modification affiche « Modifié à la main : la simulation n'est plus déterministe ».
- **Sélection depuis le monde** : la démo donne sa créature choisie à l'inspecteur quand elle change (clic du milieu, Tab, et la première au lancement), puis le laisse libre : on peut regarder une autre entité sans que la démo la reprenne à chaque tick.
- **Assets** (l'ancienne fenêtre du bac à sable, passée dans le moteur) : types (nombre, mémoire, chargements, échecs), puis par type un tableau trié : fichier, Mo, utilisateurs, **état** (prêt, « remplacé » par l'asset de remplacement, « erreur » quand le dernier rechargement a échoué et que l'ancien contenu reste ; la raison en info-bulle) et **Recharger** (grisé pour les atlas et les animations, jamais rechargés). Les boutons Recharger et « Libérer les assets inutilisés » ne font que **demander** : l'`Application` exécute entre deux frames (`between_frames()`), car un chargement attend le GPU. (L'ancienne fenêtre libérait en pleine frame.)
- **Entrées** : profil, dernier périphérique, manettes, pointeur, état muet ou rejeu, et chaque action (liaisons, enfoncée ou valeur de l'axe). Le tableau du panneau de la démo y est passé.
- **Audio** : sortie, fréquence, tampon, volumes (général et par groupe, qui sont les réglages du joueur) et pauses, compteurs, **dernier son refusé et pourquoi** (inaudible, limite par fichier, limite de voix), crêtes et limiteur, et les voix actives (nom, groupe, force, placé, boucle, en train de s'arrêter).
- **États de jeu** : chaque pile regardée (`watch(stack)`, le test des états), du dessus au dessous : nom, et ce qui arrive à ce qui est dessous (visible ou caché, figé ou mis à jour), transitions en attente.
- **Fenêtres retenues** : `imgui.ini` dans le dossier des préférences (à côté de `audio.json` et des touches), avec les places des fenêtres d'ImGui et une section `[MoteurTools][Windows]` à nous (quelles fenêtres sont ouvertes), écrite par un gestionnaire de réglages d'ImGui. Piège rencontré : ImGui réécrit son fichier en détruisant son contexte, après la destruction des outils et donc sans leur section ; les outils l'écrivent eux-mêmes en partant, puis le lui retirent. Toutes les autres fenêtres du bac à sable ont `NoSavedSettings` : rien d'autre n'est retenu. À la première ouverture, les fenêtres se placent en cascade le long du bord droit.
- **`--menu-test N`** : ouvre le menu avec son test N (compté depuis 0 dans DEBUG > Tests moteur) déjà lancé. Sert à essayer les outils sans clic, et à les capturer.

**Vérifications faites (Windows)**

- **272** tests unitaires (268 avant) : filtre de l'inspecteur, modification d'un composant par sa copie (signal `on_update` seulement quand il change), entité fixe déplacée qui perd sa matrice gardée et se dessine à sa nouvelle place, erreur de rechargement visible puis effacée, dernier son refusé et voix actives.
- Menu, démo 3D (`--menu-test 6`, 1920×1080) : l'inspecteur montre la créature 0 choisie par la démo, ses composants du moteur et de la démo (Marcheur, Santé) ; les quatre autres fenêtres sur le test des états (`--menu-test 8`) ; `imgui.ini` garde la section des outils d'un lancement à l'autre.
- Debug : démo 3D, test des états et audio, **les cinq fenêtres ouvertes**, 10 s chacun : aucun message de la couche de validation D3D12. Compilation sans avertissement (un premier jet utilisait `sparse_set::type()`, déprécié par EnTT 3.16 au profit de `info()`).
- Captures de la démo 3D inchangées : `fdc076d9c3ae` (`--no-input`), `952477744ffb` (rejeu). Attention : elles supposent le profil de touches par défaut ; un profil choisi par le joueur (« zqsd ») change la ligne d'aide, donc la capture (`3e8448842d7e` avec « zqsd »).
- **Non vérifié à la main** : cliquer une créature dans le monde puis changer sa position dans l'inspecteur (le chemin de la sélection est celui de la démo, déjà vérifié ; le déplacement passe par le même `replace` que le test), et le bouton Recharger sur un fichier réel.

### Validation

- [x] Dans la démo, cliquer sur une créature l'affiche dans l'inspecteur ; changer sa position la déplace. *Essayé à la main par l'utilisateur (2026-09-25).*
- [ ] Le navigateur d'assets montre la baisse de mémoire des KTX2 et l'asset de remplacement d'un fichier manquant. *La mémoire par texture y est (4 Mo en KTX2, 16 Mo avec `--no-bc`) ; le remplacement est affiché « remplacé » avec sa raison. À regarder à la main.*
- [x] Sans `debug_ui`, rien d'ImGui n'est créé. *`DebugTools` n'existe qu'avec l'interface de debug ; les captures en ligne de commande sont inchangées.*

---

## 9. Tranche jouable

### But

Réunir tout le jalon dans la démo 3D, devenue une petite **tranche jouable** : la preuve que les systèmes fonctionnent ensemble.

### Contenu

- **Écran titre** (état) avec sa musique : « Jouer », « Quitter », à la souris et à la manette.
- **Chargement** de la carte (état), avec progression.
- **Jeu** : la carte de la démo 3D ; un **personnage** contrôlé au clic (se déplacer vers le point du sol) ou au stick, que la caméra suit ; les créatures et torches en entités ; textures en KTX2 ; bruits de pas, un son d'impact quand on clique une créature, une ambiance, une musique de jeu.
- **Pause** (état transparent) : le monde reste visible, la musique baisse, « Reprendre » et « Retour au titre ».
- Les outils de debug de la partie 8, et les panneaux « Rendu » existants.

### Tâches

- [x] Assembler les états et la démo.
- [x] Une **capture de référence** avec entrées rejouées (option de ligne de commande), stable sur deux lancements.
- [x] Mesurer : temps de chargement, mémoire GPU, CPU par frame (comparé au jalon 3), et le noter ici.
- [x] Mettre à jour `FICHIERS_DU_PROJET.md`, `credits.json`, `TEST_MAC.md` (section « Jalon 4 »). *`credits.json` n'a pas changé : les sons d'interface et d'impact de Kenney y sont depuis la partie 7.*

### Implémentation réalisée (partie 9)

Fichiers : `states_demo.hpp` / `.cpp` et `demo3d.hpp` / `.cpp` (bac à sable), `assets/input/demo3d.json` (actions des menus), `tools/input/make_slice_replay.py` et `tests/data/slice_replay.json` (rejeu de référence). Rien dans le moteur : la tranche n'a demandé aucun système de plus, ce qui était le but.

- **Le test des états devient la tranche** (menu : « Tranche jouable (états de jeu) », ligne de commande : `--states`) plutôt qu'une scène de plus : titre, chargement et pause y étaient déjà, avec le pilote automatique des cent cycles, qui garde son rôle de contrôle des fuites.
- **Le héros** (`Demo3D::Options::hero`, que la tranche active ; sans lui, la démo du jalon 3 et ses captures ne changent pas) : une entité à lui (corps bleu acier, tête, épée), au début de la première pièce, toujours choisi (Tab et le clic du milieu ne changent plus de créature), la caméra le suit. Il se déplace au clic (maintenu : il suit le pointeur) ou au stick et à ZQSD.
- **Frapper** : un clic sur une créature (à 0,8 case du point visé) la frappe si elle est à portée (2 cases), sinon le héros marche jusqu'à elle ; un clic commencé sur une créature ne fait pas marcher tant qu'il est tenu. La compétence 1 (A, ou A de la manette) frappe la plus proche à portée. Un coup : **son d'impact** placé sur la créature (une des cinq variantes, hauteur ±8 %), « Touché ! » au-dessus d'elle, un quart de sa vie ; à zéro, elle s'arrête. Le compte des coups est dans la ligne d'aide.
- **Sons** : un **pas** toutes les 0,85 case marchée (cinq variantes, placées sous le héros), l'**ambiance** « Forgotten tombs » en flux, en boucle (groupe ambiance : la pause l'arrête), la **musique** du jeu (partie 7), et un clic ou une confirmation d'**interface** dans les menus (groupe interface : on les entend pendant la pause). Le chargement a une étape « Sons » de plus, qui les garde jusqu'à ce que le monde les retrouve dans le cache.
- **Menus aux actions** : `menu_up`, `menu_down`, `menu_confirm` (flèches et Entrée, croix ou stick gauche et A de la manette), déclarées avec celles de la démo par `Demo3D::declare_actions()`, que la tranche appelle dès sa création (le titre existe avant la démo). Une classe `Menu` (une colonne de boutons, l'élément choisi en surbrillance) les lit ; la souris clique toujours, mais **ne déplace pas** le choix, sinon un rejeu dépendrait de l'endroit où est la vraie souris. C'est aussi ce qui rend possible un rejeu du titre au jeu : les clics d'ImGui ne passent pas par `Input`, donc un enregistrement ne peut pas les rejouer.
- **Capture de référence** : `tools/input/make_slice_replay.py` écrit un rejeu scripté de 480 ticks (titre, « Jouer », marche en haut à droite, marche vers les créatures en frappant toutes les 8 ticks, pause, « Reprendre », marche et coups encore). Avec `--freeze-after N`, `--states` passe le gel et la capture à la démo (N compte les ticks **du jeu**) au lieu de capturer la première pause : pas d'ImGui dans l'image, donc rien que la vraie souris puisse survoler.
- **`--run-seconds`** vaut maintenant aussi pour `--states` (le test ne s'arrêtait que par ses cycles).
- **Mesure du chargement** : l'état de chargement note son début, le dernier pas sa fin ; le log dit `Slice: loaded in ... ms` avec la mémoire des assets, les objets GPU et celle du processus, et le panneau du menu montre le dernier.

**Mesures (Windows, RTX 4070 Ti SUPER, Release)**

| Mesure | Valeur |
|---|---|
| Chargement (du titre au jeu : police, tonneau, 10 sons et l'ambiance, construction du monde) | **440 à 520 ms** en Release (1,8 s en Debug) ; le monde se construit en une étape, le reste compte peu |
| Mémoire des assets en jeu | **14,9 Mo** : textures 4,0 Mo (KTX2 en BC7 / BC5 ; 16 Mo sans compression), tonneau 0,6, police 0,1, sons 0,8, musiques et ambiance 9,4 (compressées) ; plus les maillages de la démo, faits en code |
| Objets GPU en jeu | 470 (51 au titre) |
| CPU par frame, démo 3D sans vsync, graine 42, 8 s | jalon 3 (commit `42af19c`, recompilé pour l'occasion) **1,24 ms** ; aujourd'hui **1,36 ms** (moyenne de trois lancements alternés). L'écart est tout entier dans l'enregistrement de la frame (0,34 → 0,47 ms) ; l'envoi au GPU ne change pas (0,88 ms) |
| CPU par frame, tranche rejouée sans vsync | 1,22 ms en moyenne (vue plus rapprochée : 114 maillages dessinés au lieu de 296) |

Le chiffre du jalon 3 noté plus haut dans son document (1,16 ms) datait d'un autre lancement ; recompilé et mesuré à côté, il donne 1,24 ms : comparer sur la même machine au même moment, pas à un chiffre ancien.

**Vérifications faites (Windows)**

- Capture de référence de la tranche : `--states --replay-input tests/data/slice_replay.json --freeze-after 340 --run-seconds 9 --pixel-size 1280 720 --capture` → **`dbf96994a52d`**, deux lancements sur deux ; l'image montre le héros, 11 coups donnés, des « Touché ! », une créature dont la barre a baissé.
- Captures de la démo 3D inchangées (`fdc076d9c3ae`, `952477744ffb`) : le mode héros est éteint par défaut, et déclarer les actions des menus en plus n'a rien changé (elles sont retrouvées par leur nom). Comme pour elles, ces captures supposent le profil de touches par défaut.
- Debug (couche de validation D3D12) : la tranche rejouée et `--states-cycles 3` sans aucun message ; 272 tests.
- `--states-cycles 100` (Release), avec le héros et ses sons : « assets and GPU objects stable » (5 assets et 51 objets GPU au titre, du cycle 1 au cycle 100), 0 pause où le monde a bougé, mémoire du processus 201,5 à 207,6 Mo, plancher des dix derniers cycles +0,7 Mo au-dessus de celui des dix premiers (bruit de l’allocateur, comme en partie 6).
- **Non vérifié à la main** (au moment de la partie ; depuis, le clavier-souris a été essayé, voir la validation) : jouer la tranche soi-même du titre au retour au titre, au clavier-souris puis **à la manette** (menus à la croix, déplacement au stick, A pour frapper, Start pour la pause), et **écouter** pas, impacts, ambiance, musiques et sons de menu.

### Validation

- [ ] La tranche se joue du titre au retour au titre, au clavier-souris et à la manette, sans message de validation en Debug. *Clavier-souris : jouée à la main par l'utilisateur (2026-09-25). Rejouée du titre à la pause et retour au jeu, et trois fois du titre au titre par le pilote automatique, sans message en Debug (cent fois en Release, stable). Reste la manette, que l'utilisateur essaiera plus tard.*
- [x] Sa capture de référence est stable, et les performances restent dans celles du jalon 3. *Capture stable. Performances : +0,12 ms de CPU par frame (1,36 contre 1,24 ms, +10 %), loin du budget de 16,7 ms ; la piste, si cela compte un jour, est la collecte du monde et les barres de vie, qui recalculent les matrices des créatures à chaque frame.*

---

## 10. Critères de fin de jalon

Le jalon est terminé quand **tout** ce qui suit est vrai :

- [x] Tous les assets passent par le **gestionnaire** : cache, poignées, déchargement aux changements de scène, asset de remplacement et message clair en cas d'erreur.
- [x] Les textures 3D sont en **KTX2** (BC7 / BC5), avec un repli, et la baisse de mémoire est mesurée.
- [ ] Le gameplay de la démo n'utilise que des **actions**, au clavier-souris et à la manette ; les liaisons se lisent dans un fichier JSON remplaçable par le joueur. *Fait au clavier-souris ; la manette reste à essayer à la main.*
- [x] Le monde de la démo est fait d'**entités** EnTT, sans perte de performance notable par rapport au jalon 3. *+0,12 ms de CPU par frame (1,36 contre 1,24 ms, les deux mesurés côte à côte, voir la partie 9) : jugé négligeable (décision de l'utilisateur, 2026-09-24) ; sur la machine visée, c'est le GPU qui limitera le rendu. Le CPU reste à surveiller à chaque jalon (IA, chemins, animation : le fil principal est partagé avec le jeu), en particulier au jalon 5 (animation des squelettes sur le CPU ou le GPU).*
- [x] Une **pile d'états** gère titre, chargement, jeu et pause, sans fuite sur cent allers-retours.
- [x] L'**audio** joue effets placés et musique, avec groupes, limite de voix et fondus.
- [x] Les outils **ImGui** (inspecteur, assets, entrées, audio, états) sont dans le menu DEBUG.
- [x] Aucun avertissement de compilation, aucun message de la couche de validation du GPU. *Sous Windows (D3D12) ; Metal dans TEST_MAC.md.*
- [x] La logique pure (cache, poignées, actions, pile d'états, systèmes, atténuation) a ses tests unitaires.
- [x] Les nouvelles bibliothèques et les nouveaux assets sont dans `credits.json` (fenêtre « À propos »).
- [x] Les décisions de la section [Décisions à consigner](#décisions-à-consigner) sont remplies.
- [x] La documentation des nouveaux fichiers est ajoutée à [FICHIERS_DU_PROJET.md](FICHIERS_DU_PROJET.md), et les vérifications Mac sont regroupées dans [TEST_MAC.md](TEST_MAC.md).

---

## 11. Risques principaux

| Risque | Impact | Parade |
|---|---|---|
| Entrées mal reliées au pas fixe | Appuis perdus ou doublés, non-déterminisme | Transitions accumulées puis consommées par pas ; tests unitaires ; captures avec entrées rejouées |
| ECS adopté partout d'un coup | Réécriture longue, moteur moins lisible | ECS limité au monde ; conversion de la seule démo 3D, capture comparée |
| Durées de vie des assets floues | Textures détruites en cours d'usage, ou mémoire qui ne baisse jamais | Déchargement seulement aux changements de scène ; compteurs visibles dans le panneau |
| Chargement asynchrone trop tôt | Bugs de concurrence difficiles | Interface asynchrone, implémentation synchrone tant qu'aucun chargement ne dépasse une seconde |
| Bibliothèque audio mal choisie | Fonctions manquantes (spatialisation, flux) à réécrire | Liste des besoins d'un ARPG avant le choix ; scène de test couvrant toute la liste |
| Casse des noms de fichiers | Fonctionne sous Windows, introuvable sur Mac | Clés normalisées ; casse vérifiée même sous Windows |
| Manettes différentes selon l'OS | Boutons inversés sur un OS | Liaisons par position (Sud, Est...) ; test sur Mac avec la même manette |
| Outils de conversion KTX2 différents selon l'OS | Fichiers différents, captures incomparables | Conversion par un script unique, versions notées ; comparaison des captures |
| Poids des sons et musiques dans Git | Dépôt lourd | Même règle que les modèles : téléchargés par un script, hors de Git |
| Dérive du périmètre (éditeur, sauvegarde, interface du jeu) | Jalon sans fin | Renvoyer au jalon 7 ; ici, seulement ce dont la tranche jouable a besoin |

---

## Décisions à consigner

À remplir au fil du jalon. Chaque ligne correspond à une question posée plus haut.

| Sujet | Décision | Raison |
|---|---|---|
| Poignées d'assets (indice + génération, pointeur partagé) | **`entt::resource<T>`** (pointeur partagé) et `entt::resource_cache`, enrobés par `AssetCache` (rechargement, remplacement, mesure, collisions de hachage détectées). *Révisé le 2026-09-24 : d'abord « indice + génération »* | Demande de l'utilisateur : s'appuyer le plus possible sur l'open source. EnTT arrive de toute façon avec l'ECS ; un asset tenu n'est jamais libéré, donc aucune poignée périmée à détecter. La sauvegarde (jalon 7) écrira la clé de l'asset, pas la poignée |
| Durées de vie et déchargement des assets | Libéré quand plus aucune poignée ne le tient, **seulement** à l'appel de `collect_garbage()`, entre deux scènes (la suivante créée avant que la précédente parte) | Jamais de ressource détruite pendant une frame ; ce que deux scènes partagent n'est pas rechargé |
| Chargement synchrone ou en arrière-plan | **Synchrone** pour l'instant (le plus long : 130 ms pour le rocher Poly Haven) | Un fil de décodage viendra quand un chargement dépassera une seconde (zones du jeu, animations) |
| Rechargement à chaud des assets | **Oui, en développement** : `efsw` (MIT) surveille `assets/` des sources, copie le fichier modifié à côté de l'exécutable et recharge en place (textures, modèles, environnements, polices ; pas les atlas ni les animations). *Révisé le 2026-09-24 : d'abord « par interrogation des dates »* | Bibliothèque open source plutôt que du code maison ; API natives de chaque OS, pas d'interrogation. Le même mécanisme servira au JSON du jalon 7 |
| Déclaration des paramètres d'une texture (sRGB, normales) | Paramètre `TextureSettings` à la demande, qui fait partie de la clé ; le glTF décide pour ses propres textures | Une même image peut servir de couleur et de donnée |
| Désignation des KTX2 dans les glTF (`KHR_texture_basisu` ou fichier voisin) et réglages UASTC | **`KHR_texture_basisu`**, l'image PNG / JPEG gardée en repli (extension non requise) ; UASTC niveau 2, Zstandard 18, mipmaps générées par `ktx create` ; normales en `--normal-mode` (deux canaux, BC5) ; données déclarées linéaires ; **z des normales recalculé dans le shader pour toutes les cartes** | Standard glTF ; Blender ouvre toujours les modèles ; `--no-ktx2` compare les deux. Recalculer z partout coûte presque rien (0,09 % des pixels changent avec les JPEG) et évite un réglage par matériau. RDO et tailles sur disque à revoir avec le packaging |
| Format du fichier des touches et emplacement | **JSON** version 1, des **profils** nommés (actions → listes de sources, axes → quatre directions et/ou un stick) ; défauts dans `assets/input/`, fichier du joueur dans `SDL_GetPrefPath`, qui ne garde que le profil choisi et ce qu'il change. Deux profils pour la démo : « clic » et « zqsd » | Lisible et modifiable à la main ; les deux façons de jouer demandées tiennent dans un seul fichier ; un nouveau défaut profite au joueur tant qu'il ne l'a pas changé |
| Scancodes ou keycodes | **Scancodes** (position physique), nommés comme sur un QWERTY US dans le fichier ; affichés comme sur le clavier du joueur | Z Q S D d'un AZERTY et W A S D d'un QWERTY sont la même liaison ; l'aide reste juste sur chaque clavier |
| Entrées dans la simulation déterministe (échantillonnage, enregistrement) | Transitions accumulées entre deux ticks et consommées par le premier (`end_tick`) ; picking avec la caméra du tick ; enregistrement tick par tick (`--record-input`, `--replay-input`), actions par nom | Appuis jamais perdus ni doublés ; un rejeu donne la même capture |
| Place de l'ECS (tout le jeu ou le monde) et nombre de registres | **Le monde seulement** (ce qui a une place dans la carte) ; **un registre par scène**, tenu par `moteur::World`, que la scène possède ; composants du moteur génériques, ceux du jeu dans le jeu (`Walker`, `Health` dans la démo) | Les systèmes du moteur et l'interface restent des classes lisibles ; fermer une scène détruit tout son monde |
| Hiérarchie des entités | **`Parent` simple** (transformation relative, matrice monde calculée en remontant), pas de graphe de scène ; destruction en cascade par `World::destroy()` | Premier besoin réel dans la démo : corps et tête d'une créature, anneau de sélection. L'attache à un os (jalon 5) s'y ajoutera |
| Rotation d'un `Transform` | **Quaternion** (`glm::quat`), interpolé par slerp | Standard (glTF, animations du jalon 5) ; coûte 5 pixels sur la capture de référence, qui change une fois |
| Entités fixes | Matrice et boîtes **gardées** tant que rien ne change ; changement par `patch` / `replace` (signaux d'EnTT) | Le coût des objets fixes préparés du jalon 3, sans que le jeu ait à y penser |
| Pile d'états ou machine à états ; lien avec le menu des tests | **Pile** (`StateStack`, elle-même un `Game`) ; transitions différées au tick suivant ; actions à l'état du dessus seulement (les autres lisent une `Input` muette) ; chargement par étapes, une par tick. Le **menu des tests reste à part** : le test des états y est une scène qui tient sa propre pile | La pause et les menus se posent sur le jeu sans le reconstruire ; `Application` ne change pas. Convertir le menu (trois écrans qui marchent) n'apportait rien : ce n'est pas le jeu. Une étape par tick rend la durée d'un chargement indépendante de la machine (rejeux) |
| Bibliothèque audio | **miniaudio** 0.11.25 (vcpkg), son `ma_engine` pour le mixage, les groupes, la hauteur et les fondus ; **nos** assets pour les fichiers ; placement, limites de voix et limiteur écrits dans le moteur | Choix validé par l'utilisateur ; open source, mûr, Windows (WASAPI) et Mac (Core Audio). Le placement maison (panoramique d'une caméra fixe) est une fonction pure, testée ; la spatialisation 3D de miniaudio n'apportait que ce qu'un ARPG vu de haut n'entend pas |
| Formats audio | Lus : WAV, FLAC, MP3, OGG Vorbis (`stb_vorbis`). Recommandés pour nos sons : **WAV** pour les effets courts, **OGG** pour la musique et les ambiances ; les sons de test gardent le format publié (OGG, MP3, WAV) | Pas de conversion à maintenir ; miniaudio lit les quatre |
| Position de l'auditeur | Le **point du sol au centre de la vue**, orienté comme l'écran (`set_listener(camera)`) ; panoramique selon la droite de l'écran, pas d'avant ni d'arrière | Recommandation du document ; distances qui ont un sens avec une caméra fixe |
| Sons et musiques de test, licences | Kenney (Impact Sounds, Interface Sounds) et OpenGameArt (pauliuw, cynicmusic, kindland, AntumDeluge), **tous en CC0**, téléchargés par `tools/audio/fetch_test_sounds.py` (SHA-256 vérifiés), hors de Git, crédités | Même règle que les modèles |
| Pause et son | La pause **baisse la musique** (×0,35) et met en pause effets et ambiance ; l'interface continue. Son coupé quand la fenêtre perd le focus (réglage, activé par défaut) | Ce que font la plupart des jeux ; la partie 9 le reprend |
| Saturation | **Limiteur** en fin de mixage (plafond 0,9, relâchement 150 ms), en plus de la limite de voix | La limite de voix seule laissait la crête monter à 3,3 avec des sons enregistrés fort |
| Inspecteur : `entt::meta` ou enregistrement à la main | **À la main** : `ComponentInspectors::add<T>(nom, fonction ImGui)` ; le moteur inscrit ses composants, le jeu les siens ; un type non inscrit est listé par son nom C++. Modification sur une copie remise par `registry.replace` ; ni ajout, ni retrait, ni destruction | Recommandation du document ; une fonction de quelques lignes par composant, sans API de plus. `replace` garde les caches du monde justes. Retirer ou détruire ferait planter un jeu qui garde des entités. `entt::meta` sera revu au jalon 7 avec la sauvegarde |
| Place des outils de debug et état retenu | **Dans le moteur** (`DebugTools`, créé avec `debug_ui`), mais **le jeu** les met dans son menu et les dessine ; fenêtres ouvertes et places dans `imgui.ini` du dossier des préférences | Chaque jeu garde son interface ; les scènes autonomes et les captures n'en montrent aucun ; rien n'est écrit à côté de l'exécutable |
