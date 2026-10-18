# Jalon 9 - Niveau production : monde et données

Retour à la [roadmap générale](ROADMAP.md). Jalon précédent : [Jalon 8 - Consolidation](JALON_8_CONSOLIDATION.md). Jalon suivant : [Jalon 9 bis - Niveau production : présentation](JALON_9BIS_PRESENTATION.md). Origine du contenu : [Revue des jalons 1 à 7](REVUE_JALONS_1_A_7.md) (les points **P2** et les décisions du 2026-10-16). Description des fichiers existants : [Fichiers du projet](FICHIERS_DU_PROJET.md) ; utilisation du moteur : [Guide](GUIDE.md) ; formats : [Formats](FORMATS.md).

## Objectif

Le jalon 9 « Niveau production » met le moteur au niveau d'un moteur de production sur tout ce dont le jeu va dépendre, **avant** que le code de l'ARPG ne s'y appuie. Il est coupé en deux (décision du 2026-10-17) : **ce document, « monde et données »**, puis le [jalon 9 bis « présentation »](JALON_9BIS_PRESENTATION.md) (matériaux, animation, texte, localisation, interface, audio).

Ce jalon-ci donne : un **monde en vraie 3D** (géométrie de niveau assemblée en briques, collisions, navigation sur plusieurs niveaux par Recast & Detour, vision et sélection à la souris en 3D, niveaux découpés en morceaux), des **assets identifiés de façon stable** (UUID dans un `.meta`) et **préparés** pour la version livrée, une **réflexion des types** qui donne d'un coup lecture des données, édition, sauvegarde et schéma, des **préfabriqués** (imbriqués, avec variantes), une **image parallèle** par construction, et le **socle de l'éditeur de niveaux** qui place les vrais assets (briques de kit, sols peints, accessoires comme le lampadaire, lumières).

À la fin du jalon, la **tranche jouable** se joue dans un **niveau 3D** construit dans l'éditeur (au moins deux hauteurs reliées par un escalier ou une rampe) avec les **pavés** et les **lampadaires** de l'ARPG, des monstres qui y trouvent leur chemin et sont sélectionnés sur leur silhouette ; ses données passent par la réflexion, ses assets par identifiant, tout est préparé dans le jeu livré ; sauvegardes, rejeux et captures de référence fonctionnent, le verifier passe. Son interface, son texte et ses sons sont encore ceux du jalon 8 : le jalon 9 bis les refait.

**Niveau d'exigence** : celui fixé le 2026-10-16. *La complexité et la durée de mise en place ne sont pas des critères ; ce qui prime, c'est le moteur final dans une version professionnelle et utilisable.* Chaque recommandation vise la solution des studios quand on la connaît. **Règle des bibliothèques** : maison dès que possible ; une bibliothèque n'entre que sur **proposition** argumentée, et c'est l'utilisateur qui décide. Dans ce jalon, l'utilisateur a choisi **Recast & Detour** pour la navigation ; *meshoptimizer* reste une option à proposer.

**Pourquoi maintenant ?** Le jalon 1 de l'ARPG (personnage, zone, monstres, attaque) repose sur le monde, les données et les assets. Les écrire après coup obligerait à réécrire le jeu : un monde plat à rendre 3D, des chemins de fichiers à remplacer par des identifiants, des lectures de données à la main à remplacer par la réflexion. Les premiers vrais assets de l'ARPG sont arrivés (lampadaire, pavés, kit de clôture gothique, 2026-10-17) : l'éditeur qui les place ne peut plus attendre.

## Prérequis

- Le [jalon 8](JALON_8_CONSOLIDATION.md) est terminé sous Windows et commité (`ce44385`). Restent de son côté, **à l'utilisateur** : les vérifications sur le Mac ([TEST_MAC.md](TEST_MAC.md)), l'installation sur une machine propre et sous Windows 10, la tranche jouée à la manette. Ils ne bloquent pas ce jalon, mais la partie 2 les reprend.
- La **décision de stockage des assets** (T6) : repoussée par l'utilisateur (partie 2).
- Le **Mac M3** pour les mesures (image parallèle, chargement par morceaux) et le verifier.
- Les assets de l'ARPG disponibles pour la tranche : `tools/models/fetch_arpg_models.py` (aujourd'hui) ; après la partie 2, par le dépôt d'assets choisi.

## Comment lire ce document

Même structure que les jalons précédents :

- **But** : ce qu'on cherche à obtenir.
- **Tâches** : ce qu'il faut faire, dans l'ordre.
- **Questions à se poser** : les décisions à trancher avant ou pendant le travail, avec une recommandation. À noter dans la section [Décisions à consigner](#décisions-à-consigner) une fois tranchées.
- **Pièges connus** : erreurs fréquentes.
- **Validation** : comment savoir que la partie est terminée.

Les points marqués *(à vérifier)* sont des informations dont je ne suis pas certain ou qui évoluent vite. Il faut les confirmer avant de s'appuyer dessus.

## Sommaire

1. [Vue d'ensemble et ordre de travail](#1-vue-densemble-et-ordre-de-travail)
2. [Règles de production : erreurs, stockage des assets, reste du jalon 8](#2-règles-de-production--erreurs-stockage-des-assets-reste-du-jalon-8)
3. [Réflexion des types et préfabriqués](#3-réflexion-des-types-et-préfabriqués)
4. [Assets : identifiants stables, import, formats préparés](#4-assets--identifiants-stables-import-formats-préparés)
5. [Monde en 3D : géométrie de niveau, collisions, requêtes](#5-monde-en-3d--géométrie-de-niveau-collisions-requêtes)
6. [Navigation en 3D : maillage de navigation, chemins, foules](#6-navigation-en-3d--maillage-de-navigation-chemins-foules)
7. [Vision, brouillard et sélection à la souris en 3D](#7-vision-brouillard-et-sélection-à-la-souris-en-3d)
8. [Format de niveau v3 et chargement par morceaux](#8-format-de-niveau-v3-et-chargement-par-morceaux)
9. [Image parallèle](#9-image-parallèle)
10. [Éditeur de niveaux et bibliothèque d'assets (socle)](#10-éditeur-de-niveaux-et-bibliothèque-dassets-socle)
11. [La tranche dans un niveau 3D](#11-la-tranche-dans-un-niveau-3d)
12. [Critères de fin de jalon](#12-critères-de-fin-de-jalon)
13. [Risques principaux](#13-risques-principaux)
14. [Décisions à consigner](#décisions-à-consigner)

---

## 1. Vue d'ensemble et ordre de travail

### Où on part

Après le jalon 8, le moteur est une bibliothèque documentée (référence Doxygen sans avertissement, guide, formats), vérifiée par un script complet (Debug, Release, ASan, clang-tidy, fuzzing, captures de référence, paquet installé et joué), livrable sous Windows (installeur) et prêt pour macOS (`.app`, `.dmg`). Mais plusieurs fondations sont encore celles d'un prototype :

- **le monde est plat** pour la logique : grille au sol, `y = 0` dans les collisions, les chemins, la vision, le brouillard, la sélection et l'éditeur ;
- les assets sont désignés par leur **chemin** ; un renommage casse silencieusement ce qui les référence ;
- chaque type de données s'écrit **trois fois** (lecture des tables, inspecteur, sauvegarde) ;
- les cartes, descriptions d'animation et effets sont livrés **en JSON**, les clips reconstruits depuis le glTF au chargement ;
- l'image est **séquentielle** (le système de tâches ne sert qu'au chargement) ;
- les matériaux tiennent des **pointeurs bruts** vers les textures ; pas de transparence triée ni de variantes ;
- l'animation n'a ni **root motion** ni **couches additives** ;
- le texte est un **bitmap à une taille**, en alphabet latin ;
- il n'y a ni **localisation**, ni **système d'interface** (l'interface de la tranche est écrite à la main, `SliceUi`), ni **événements audio** ;
- l'**éditeur de cartes** du jalon 7 ne connaît que des tuiles et des primitives : il ne montre ni les vrais sols ni les vrais accessoires (question du 2026-10-17 : les pavés et les lampadaires ne sont pas utilisables dans l'éditeur).

### Ce qui n'est pas dans ce jalon

- **Jalon 9 bis « Présentation »** ([JALON_9BIS_PRESENTATION.md](JALON_9BIS_PRESENTATION.md)), juste après : matériaux (transparence, variantes, shaders compilés sur le Mac), animation (root motion, couches additives), texte en champ de distance avec HarfBuzz, localisation, système d'interface maison, audio par événements, et la tranche refaite avec eux.
- **Jalon 10 « Rendu et effets »** (en parallèle de l'ARPG, puisqu'il change l'image sans changer l'API du jeu) : rendu *clustered forward*, profondeur inversée, post-traitement (bloom, occlusion ambiante, étalonnage, TAA), particules sur le GPU (rubans, particules maillées, distorsion), physique 3D, et l'éditeur d'effets.
- **Jalon 11 « Éditeur de production »** (nouveau, issu de la revue) : éditeurs de matériaux, d'effets, de graphes d'animation, d'écrans d'interface, l'**outil de création sonore** (décision 4.1), l'édition des tables de données par la réflexion. Ce jalon-ci pose seulement le **socle** de l'éditeur de niveaux (partie 10) et tout ce dont ces éditeurs dépendront (réflexion, identifiants, préfabriqués, formats).
- **CI hébergée** : toujours repoussée (choix de l'utilisateur).

### Dépendances entre les parties

```
2 Règles de production
3 Réflexion, préfabriqués ──> 4 Assets (identifiants, import, préparés)
                                  │
                                  ├──> 5 Monde 3D ──> 6 Navigation 3D ──> 7 Vision, sélection
                                  │         └──────────────┴──> 8 Niveau v3, morceaux
                                  └──> 10 Éditeur de niveaux (après 4, 5, 8 ; s'enrichit de 6)
9 Image parallèle : après 5 et 6 (ce qu'elle parallélise doit exister), avant 11
11 La tranche dans un niveau 3D : à la fin
```

**Ordre recommandé** : 2, 3, 4, puis le **monde** (5, 6, 7, 8) et l'**éditeur** (10) qui lui donne ses niveaux ; 9 ; enfin 11.

### Liens avec l'ARPG

Le [jalon 1 de l'ARPG](../../ARPG/ARPG_doc/ROADMAP.md) (prototype jouable : personnage au clic, une zone, des monstres, attaque, vie et mort, barre de vie) commence **après** le jalon 9 bis. Pendant ce temps, le jeu peut avancer **sans le moteur** ([PREPARATION_SANS_MOTEUR.md](../../ARPG/ARPG_doc/PREPARATION_SANS_MOTEUR.md) : stats, formules, données) et produire ses **assets** (kits de niveaux, accessoires, sols, personnages), que l'éditeur de la partie 10 saura placer.

Ce que le jeu a déjà décidé et qui contraint le moteur ([DECISIONS_DE_CONCEPTION.md](../../ARPG/ARPG_doc/DECISIONS_DE_CONCEPTION.md)) : déplacement au clic **ou** au clavier, touches modifiables (fait au jalon 8) ; solo, **sans fermer la porte au multijoueur** (état séparé du rendu, simulation déterministe à pas fixe, commandes du joueur en actions) ; combat peut-être **piloté par l'animation** (question ouverte du jeu : elle pèse sur le root motion et les événements d'animation, au jalon 9 bis).

### Estimation indicative

Le plus gros jalon du projet avec le 9 bis : **plusieurs mois**. Ordre de grandeur relatif : très gros (5 + 6, 10), gros (3, 4, 8), moyens (7, 9), petits (2, 11 hors essais). Ces chiffres servent à ordonner, pas à couper : la qualité prime.

### Questions générales

- **Un seul jalon ou deux ?** On pourrait couper en « 9 : monde et données » (règles, réflexion, assets, monde, navigation, niveaux, image parallèle, éditeur) et « 9 bis : présentation » (matériaux, animation, texte, localisation, interface, audio). Recommandation : **un seul jalon, en parties commitées une à une** (décision T5 : un commit par partie au moins), parce que la tranche refaite a besoin des deux moitiés pour prouver le jalon, et que l'ARPG en a besoin aussi. Les parties restent livrables séparément.
  **Décidé (2026-10-17)** : **deux jalons** : le **jalon 9 « Monde et données »** (ce document : parties 2 à 10) puis le **jalon 9 bis « Présentation »** ([JALON_9BIS_PRESENTATION.md](JALON_9BIS_PRESENTATION.md) : matériaux, animation, texte, localisation, interface, audio). Chacun a sa tranche refaite et ses critères de fin.
- **Le déterminisme entre plateformes ?** Aujourd'hui la simulation est déterministe sur une même machine et une même version (rejeux, test d'exactitude), avec des flottants. Un monde 3D (maillage de navigation, collisions en 3D) multiplie les calculs flottants. Trois niveaux possibles : (a) même binaire, même machine (l'actuel) ; (b) même binaire sur Windows et macOS (rejeux échangés entre les deux, préalable à un multijoueur *lockstep*) ; (c) virgule fixe partout. Recommandation : **(a), avec les règles qui gardent (b) possible** (pas de fonctions transcendantes dans la logique, ordre des opérations fixe, pas de `-ffast-math`, FMA contrôlé), parce que le multijoueur, s'il vient, se fera plus probablement en serveur faisant autorité qu'en *lockstep* *(à décider avec le jeu)*. **Décision de l'utilisateur.**
  **Décidé (2026-10-17)** : **(a), même machine, porte ouverte** : les règles qui gardent possible le même résultat sous Windows et macOS sont appliquées (pas de trigonométrie dans la logique, ordre fixe, pas de `-ffast-math`, FMA contrôlé), sans en faire une exigence vérifiée.

---

## 2. Règles de production : erreurs, stockage des assets, reste du jalon 8

### But

Fixer les règles que tout le reste du jalon suivra, et décider où vivent les vrais assets maintenant qu'ils arrivent.

### Tâches

- [x] **Politique d'erreur** (T3) écrite dans le guide et appliquée : exceptions **au chargement seulement** ; aucune ne traverse la boucle d'une image (erreur notée et image sautée, ou plantage contrôlé avec rapport). Revue de `Application::run`, des chemins de rendu (`MeshRenderer::prepare`, envois au GPU) et des rappels du jeu ; un test qui fait échouer une copie GPU au milieu d'une image.
- [x] **Contrats** (T2) : `MOTEUR_ASSERT` / `MOTEUR_VERIFY` placés aux préconditions documentées des en-têtes publics (la référence Doxygen les liste : « throws », « must », « precondition »).
- [x] **Stockage des assets** (T6) : **repoussé** par l'utilisateur (voir les questions) ; les assets restent sur sa machine, et le moteur les obtient par `tools/models/fetch_arpg_models.py` (à étendre aux nouveaux assets, ou remplacé par l'import de la partie 4 lisant directement le dossier de l'ARPG).
- [ ] **Code de l'ARPG** : le dossier `ARPG/` n'est pas encore un dépôt git. Recommandation maintenue pour le **code** (pas les assets) : un dépôt, avec le moteur en sous-module (`extern/moteur`, prévu par son `CMakeLists.txt`). À l'utilisateur.
- [ ] **Reste du jalon 8**, à faire quand le Mac et une machine propre sont disponibles : [TEST_MAC.md](TEST_MAC.md) (section jalon 8 et rattrapage), installeur sur machine propre et Windows 10, tranche à la manette.
- [ ] **Pack de langue anglais de Visual Studio** (T4, à l'utilisateur).

### Questions à se poser

- **Où vivent les assets ?** (T6 ; condition posée le 2026-10-16 : historique des versions et sauvegarde hors de la machine). Options :
  - **Git LFS** dans le dépôt de l'ARPG (sources d'art : `.blend`, textures source ; assets exportés) : simple, intégré à GitHub ; quotas de stockage et de bande passante, payants au-delà d'un seuil *(à vérifier)* ;
  - un **dépôt d'assets séparé** (Git LFS lui aussi), branché en sous-module : les dépôts de code restent légers ;
  - **Perforce Helix Core** : la norme des studios pour les gros binaires (verrouillage des fichiers, fichiers de plusieurs Go), gratuit pour quelques utilisateurs *(à vérifier)*, mais un serveur à héberger ;
  - le **dossier local partagé** (décision provisoire du 2026-10-16) **ne remplit pas** la condition (ni historique, ni sauvegarde ailleurs).

  Recommandation : **Git LFS dans un dépôt d'assets de l'ARPG**, hébergé sur GitHub en privé, plus une **sauvegarde** locale régulière du dossier de travail. Les assets de test du moteur passent aussi en LFS : les captures de référence ne dépendent plus de téléchargements. **Décision de l'utilisateur** (coût, hébergement).
  **Décidé (2026-10-17)** : **tout reste en local sur la machine de l'utilisateur, à régler bien plus tard.** La condition du 2026-10-16 (historique, sauvegarde hors de la machine) est donc repoussée elle aussi : risque accepté de perdre des assets en cas de panne du disque. Le jour venu, LFS s'active **avant** de commiter les gros fichiers (voir les pièges).
- **Les assets exportés (`.glb`, KTX2) sont-ils versionnés, ou reconstruits depuis les sources ?** Recommandation : versionner les **sources** (`.blend`, textures source, scripts de pipeline) **et** les exports (l'export de Blender n'est pas toujours reproductible) ; la préparation (partie 4) part des exports.
  **Décidé (2026-10-17)** : **sources et exports**, quand le stockage sera mis en place.

### Pièges connus

- Ajouter Git LFS **après** avoir commité de gros binaires : ils restent dans l'historique (réécriture nécessaire). L'activer avant le premier asset lourd.
- Les fichiers `.blend` compressés changent entièrement à chaque sauvegarde : LFS en garde chaque version ; surveiller le quota.

### Validation

- La règle des erreurs est écrite dans le guide ; aucun `throw` ne peut sortir d'une image (relecture et test).
- Le stockage des assets est consigné comme repoussé ; le moteur obtient les assets de l'ARPG par un seul chemin documenté (script ou import).

### Implémentation réalisée (partie 2)

Fait le 2026-10-17, en autonomie (demande de l'utilisateur : « on part sur le jalon 9 et 9 bis en autonomie »).

- **Politique d'erreur** écrite au chapitre 0 du [guide](GUIDE.md) et appliquée :
  - `Application::run()` : chaque appel au jeu (`on_event`, `waiting`, `update`, `render`) et chaque service entre deux images (rechargement à chaud, outils, audio) passe par `Application::guarded()` ; une exception qui en sort est un **plantage contrôlé** (`report_fatal_error()` avec `crash_message`, rapport et minidump, fin de la boucle, `ended_on_error()`). Après une exception dans `render()`, l'image est quand même terminée (l'image d'échange acquise doit partir). Avant la boucle (rejeu illisible), `run()` lève toujours : c'est du chargement.
  - `Renderer::end_frame()` ne lève plus : la préparation de chaque rendu (maillages, panneaux, lignes, sprites, interface) passe par `prepare_guarded()` ; un échec (tampon de transfert non projeté, tampon qui ne grandit pas) est noté (les 10 premiers puis un sur 1000), compté (`RenderStats::failed_preparations`, `Renderer::frame_failures()`) et ce rendu est **sauté pour cette image** (ni ses passes d'ombre ni son dessin). Les échecs ne laissent aucune passe de copie ouverte (la palette des maillages grandit maintenant avant d'ouvrir la sienne).
  - Essais : `Renderer::simulate_frame_failure()`, commande `crash frame`, `tranche --test-plantage frame` et `--test-plantage update` (un `Game` enveloppe qui lève au 30ᵉ tick). La tranche met de côté les réglages d'affichage du joueur pendant un essai de plantage, comme pour un rejeu.
  - `tools/tests/check_error_policy.py`, ajouté au script de vérification (après les captures de référence) : l'image ratée ne termine pas la partie (code 0, ligne du journal), l'exception dans `update` donne un rapport qui la nomme et le code 1 ; les rapports de l'essai sont effacés.
- **Contrats** : `MOTEUR_ASSERT_MAIN_THREAD(quoi)` et `moteur::on_main_thread()` (`assert.hpp` : le fil qui a construit les objets statiques de la bibliothèque) ; assertions posées aux préconditions documentées : `Renderer::begin_frame` / `end_frame` (fil principal, appariés), chargements et préchargements d'`Assets`, `collect_garbage`, `update`, `World::collect` (et donc `submit`), `Application::run` (fil principal, pas de réentrée), `DataRef::operator*` / `->` (référence résolue), `MapData::cell_symbol` (case dans la carte). Les mauvais usages déjà refusés par une exception (`std::invalid_argument`, `std::logic_error` : allocation vide, variable redéclarée d'un autre type...) le restent.
- **Écran des essais** (demande de l'utilisateur pendant la partie) : `ApplicationConfig::window_display` et la variable d'environnement `MOTEUR_WINDOW_DISPLAY` choisissent l'écran où la fenêtre s'ouvre (créée cachée, centrée sur cet écran, puis montrée) ; le journal dit l'écran (`Window: ... on '<écran>'`). `verify.py`, `check_references.py`, `check_error_policy.py`, `measure.py` et `check_package.py` la mettent à `1` par défaut : **toutes les fenêtres d'essai s'ouvrent sur le second écran** (un seul écran : le choix du système).
- **Stockage des assets** : consigné comme repoussé (décision du 2026-10-17) ; le chemin unique pour les assets de l'ARPG reste `tools/models/fetch_arpg_models.py` jusqu'à l'import de la partie 4.
- **Restent à l'utilisateur** : dépôt git du code de l'ARPG, vérifications Mac du jalon 8 ([TEST_MAC.md](TEST_MAC.md)), machine propre et Windows 10, manette, pack de langue anglais de Visual Studio (T4).

---

## 3. Réflexion des types et préfabriqués

### But

Déclarer les champs d'un type **une seule fois** et en tirer automatiquement : la lecture des tables de données, l'inspecteur de l'éditeur, la sauvegarde, le schéma JSON pour les outils et les différences de versions (décision 7.1 : oui, maison). Puis s'en servir pour les **préfabriqués** : des modèles d'entités (un accessoire avec ses lumières, ses formes de collision, ses effets) décrits en données et instanciés par le jeu et l'éditeur.

### Tâches

- [x] **Description des types** : pour un `struct`, la liste de ses champs (nom, type, valeur par défaut, bornes, unité, documentation, drapeaux : édité, sauvegardé, logique ou présentation). Types de base, `glm`, énumérations (par nom), tableaux, cartes, références d'assets (identifiants, partie 4), références entre lignes de tables.
- [x] **Lecture et écriture JSON / CBOR** génériques depuis cette description, avec les messages d'erreur actuels de `DataReader` (chemin `fichier > ligne > champ`, bornes).
- [x] **Tables de données** : `DataTables` lit ses lignes par la réflexion ; les tables de la tranche passent dessus (la lecture à la main reste possible pendant la migration).
- [x] **Composants** : les composants du moteur (`Transform`, `Mover`, `Collider`, `Animator`…) décrits ; `ComponentSerializers` en tire la sauvegarde ; les migrations de version gardent leur forme.
- [x] **Inspecteur générique** : `DebugTools` montre et édite tout composant décrit (fin des panneaux écrits à la main).
- [x] **Schéma JSON** produit pour chaque table (validation dans un éditeur de texte, outils externes).
- [x] **Préfabriqués** : format en données (une entité racine, des enfants, leurs composants par la réflexion, des références d'assets), instanciation dans `World`, surcharges par instance (position, rotation, quelques champs), et l'éditeur qui sait en placer (partie 10). Le `.prop.json` du lampadaire de l'ARPG (modèle + lumière avec `shadow_near`) en devient un cas particulier, migré.

### Questions à se poser

- **Comment décrire les champs ?** Options : (a) **macros** autour de la déclaration (`MOTEUR_FIELD(...)`), comme `UPROPERTY` ; (b) une **description `constexpr`** à côté du type (liste de pointeurs de membres) ; (c) un **générateur de code** qui lit les en-têtes (avec libclang), comme l'Unreal Header Tool. Recommandation : **(b) avec un peu de (a)** pour l'écrire sans répétition : du C++20 pur, vérifié par le compilateur, sans outil de build supplémentaire ; (c) apporte plus (documentation lue dans les commentaires) mais ajoute une étape et une dépendance. **Décision**.
  **Décidé (2026-10-17)** : **(b), description `constexpr`** à côté du type, avec quelques macros d'aide.
- **Préfabriqués imbriqués et variantes** (un préfabriqué qui en contient un autre, une variante qui en change un champ, comme les *nested prefabs* et *prefab variants* d'Unity) ? Recommandation : **oui dès le format** (imbrication et surcharges), même si l'éditeur ne les expose qu'en partie dans ce jalon : les ajouter après coup casse les fichiers.
  **Décidé (2026-10-17)** : **oui, imbrication et variantes dès le format.**

### Pièges connus

- La réflexion qui oublie les **migrations** : un champ renommé doit garder la lecture de l'ancien nom (comme les anciennes clés françaises du jalon 8).
- Les **pointeurs** et poignées dans les composants : la description doit dire ce qui se sauvegarde (un identifiant d'asset) et ce qui se reconstruit (la poignée chargée).
- Le **déterminisme** : la lecture par réflexion doit donner exactement les mêmes valeurs que la lecture à la main (tests de comparaison pendant la migration, captures inchangées).

### Validation

- Les tables et composants de la tranche passent par la réflexion ; ses captures de référence et le test d'exactitude des sauvegardes sont inchangés.
- Un nouveau composant ajouté à la tranche n'a besoin que de sa description pour être lu, édité dans l'inspecteur et sauvegardé.
- Le lampadaire est un préfabriqué : placé dans la tranche et dans la scène « rue » de l'ARPG depuis le même fichier.

### Implémentation réalisée (partie 3)

Fait le 2026-10-17 (en autonomie).

- **Description des types** (`reflection.hpp`, `reflection.cpp`) : `MOTEUR_REFLECT(Type, MOTEUR_FIELD(membre, {options}), ...)`, `MOTEUR_FIELD_AS` (une clé autre que le nom du membre), `MOTEUR_REFLECT_ENUM(Type, "nom", ...)`, `MOTEUR_REFLECT_NAMED`. Une fonction `constexpr` trouvée par recherche dépendante des arguments, donc écrite dans l'espace de noms du type (celui du jeu aussi), sans générateur ni étape de build (décision). La description `constexpr` devient une fois pour toutes une `TypeInfo` d'exécution (`type_info<T>()`, sûr entre fils) : genre, taille, champs (nom, anciens noms, unité, documentation, bornes, drapeaux `kFieldEdit / Save / Data / Logic / Presentation / Color / ReadOnly`, requis), fonctions de construction, copie, destruction, accès aux éléments des listes et optionnels, liaison des `DataRef`. Types pris : booléens, entiers 8 à 64 bits, `float`, `double`, chaînes, vecteurs et quaternions `glm`, énumérations et structures décrites (imbriquées), `std::vector`, `std::optional`, `DataRef`, `entt::entity`, `Asset<T>`. Registre des types par nom (`find_type`), `ScratchObject` (un objet d'un type décrit, sans connaître son type C++). MSVC : `/Zc:preprocessor` public sur la cible `moteur` (`__VA_OPT__` des macros, aussi dans les jeux).
- **Lecture, écriture, comparaison, liens, schéma** (`reflection_io.hpp`) : `read_json` / `read_fields` avec **les messages de `DataReader`** (chemin `fichier > ligne > champ`, bornes écrites comme lui, champ requis, champ inconnu en avertissement, commentaires `_`), anciens noms (alias), énumérations par nom (et les nombres des sauvegardes plus anciennes) ; `write_json` (exact, `skip_defaults` pour n'écrire que ce qui diffère) ; `values_equal` ; `link_references` (les `DataRef` de toute profondeur) ; `json_schema` (brouillon 2020-12, `$defs`). Le CBOR passe par la même valeur JSON (les tables compilées et les sauvegardes binaires l'utilisent déjà).
- **Tables** : `add_reflected_table<T>(tables, nom, dossier, verification)` ; la table porte son schéma (`DataTableBase::schema()`, pour le fichier entier) ; `DataTables::write_schemas()` et `bac_a_sable --write-schemas DOSSIER`. La table `personnages` de la tranche est décrite (`sandbox_data.hpp`) : seules deux vérifications restent à la main (extension du modèle, chemin des effets).
- **Composants** : `ComponentTypes` / `component_types()` (`component_types.hpp`) : nom stable, description, opérations sur le registre sans le type C++, **fonctions de mise à niveau** des formes plus anciennes (`upgrade_from_array`, `upgrade_from_bare`). Composants du moteur décrits à côté de leur type : `Transform`, `Name`, `Hidden`, `Parent`, `BoneAttachment`, `LightSource`, `Collider`, `Mover` (et ses énumérations), `ModelComponent` (préfabriqués et niveaux seulement). `ComponentSerializers::add_reflected()`, `add_registered()`, `set_tables()` : `add_engine_components()` passe par la réflexion ; les sauvegardes du jalon 7 (tableaux, clés courtes, valeurs seules) se lisent encore. La tranche décrit ses six composants (`Walker`, `Health`, `Stature`, `Archetype`, `HeroTag`, `Hunter`) : `Demo3D::setup_serializers()` tient en quatre lignes ; ses anciennes sauvegardes se lisent.
- **Inspecteur générique** : `inspect_reflected()` (bornes, unités, info-bulles, couleurs linéaires, quaternions en degrés, énumérations en liste, structures et listes en arbre avec ajout et retrait, optionnels, champs en lecture seule grisés) ; tout composant de `component_types()` est montré sans enregistrement ; les panneaux écrits à la main de `Transform`, `Parent`, `BoneAttachment`, `Name`, `Collider`, `Mover`, `LightSource` et des composants de la tranche sont retirés (restent à la main : maillage, modèle, animateur, panneau, transformation précédente, qui ne sont pas des données).
- **Préfabriqués** (`prefab.hpp`, `prefab.cpp`, `Assets::prefab()`, format en [FORMATS.md, 14](FORMATS.md#14-préfabriqués-prefabjson)) : entités à identifiants stables, hiérarchie, composants par leur nom, **imbrication** (`"prefab"` + `"overrides"` par chemin `id/id`), **variantes** (`"base"`, `"overrides"`, `"removed"`, entités ajoutées), fusion champ par champ, références entre entités du préfabriqué (reliées aux nouvelles entités à l'instanciation, réécrites quand un préfabriqué est imbriqué), cycles refusés ; chaque composant lu une fois au chargement (les modèles chargés et tenus) ; `instantiate()` avec placement composé, parent, `PreviousTransform` et **surcharges par instance** (ce que les niveaux garderont). Rechargement à chaud avec les dépendances (bases, imbriqués) ; `AssetType::Prefab` dans les statistiques et l'explorateur d'assets. Les assets d'un champ sont chargés par `Assets::load_reflected()` et écrits par leur chemin (`key_of_reflected()`) : la partie 4 les fera passer par leur identifiant.
- **Tests** : `tests/test_reflection.cpp` (8 cas : description, aller-retour exact JSON et CBOR, messages, alias, table lue et liée, `skip_defaults`, composants du moteur et sauvegardes du jalon 7, schéma) et `tests/test_prefab.cpp` (4 cas : instanciation, imbrication, références entre entités, variante, surcharges par instance, fichiers refusés). 491 tests. Captures de référence et test d'exactitude **inchangés** (la tranche se sauvegarde maintenant par la réflexion). Référence Doxygen sans avertissement (macros de réflexion développées à vide pour Doxygen, détails d'implémentation exclus).
- **Reste de la partie** : le **lampadaire en préfabriqué partagé** par la tranche et l'ARPG demande que les deux désignent le même modèle alors qu'il est copié sous un autre chemin dans le moteur (`fetch_arpg_models.py`) : il se fait avec les identifiants d'assets de la partie 4 (le préfabriqué désigne le modèle par son identifiant). **Fait en partie 4** : la tranche monte le dossier d'assets de l'ARPG et place le même `lampadaire_gothique.prefab.json`.

---

## 4. Assets : identifiants stables, import, formats préparés

### But

Que **renommer ou déplacer** un asset ne casse rien ; qu'un asset source (un `.glb` exporté de Blender, une texture) soit **importé** une fois avec ses réglages ; et que **tout ce que le jeu livré lit soit préparé** en binaire (décisions T7, 3.6, 5.3 et « tout préparé » de la revue).

### Tâches

- [x] **Identifiants stables** : chaque asset reçoit un identifiant permanent dans un fichier voisin (`.meta`, comme Unity ; Godot 4 fait de même), avec ses **réglages d'import** (sRGB ou données, compression, échelle, génération des niveaux de détail…). Les données (tables, cartes, préfabriqués, effets) référencent l'identifiant ; le chemin reste écrit à côté, pour la lecture humaine.
- [x] **Registre** des assets (identifiant → chemin, type, dépendances), construit au démarrage en développement et figé à la préparation ; `Assets` charge par identifiant.
- [x] **Vérification** à la préparation : toute référence vers un identifiant inconnu est une erreur ; les assets que plus rien ne référence sont listés.
- [x] **Renommer / déplacer** : l'éditeur (et un outil en ligne de commande) déplace le fichier et son `.meta` ; rien d'autre ne change.
- [x] **Import** : un `.glb` ou `.gltf` exporté est importé avec ses textures converties en KTX2 (ce que fait aujourd'hui `tools/models/fetch_arpg_models.py` à la main : déballage, KTX2), selon les réglages du `.meta` ; ré-import automatique quand la source change (le rechargement à chaud suit).
- [x] **Maillages préparés** (3.6) : sommets quantifiés, indices réordonnés pour le cache et le recouvrement, **niveaux de détail** générés à la préparation (simplification), choix du niveau à l'affichage selon la taille à l'écran ; imposteurs pour les foules lointaines (au jalon 10 si le rendu les demande).
- [x] **Animations préparées** (5.3) : clips en archives ozz compressées à la préparation ; plus aucune reconstruction depuis le glTF dans le jeu livré.
- [x] **Tout le reste préparé** : niveaux (partie 8), descriptions d'animation, effets, préfabriqués, écrans d'interface, tables de localisation, en binaire (CBOR ou format maison), vérifiés comme les tables du jalon 7 (structure CBOR, profondeur, partie 8 du jalon 8).
- [x] **`Material` tient des `Asset<Texture>`** (3.2) au lieu de pointeurs bruts : le rechargement à chaud et le ramasse-miettes ne peuvent plus laisser de pointeur pendant.

### Questions à se poser

- **Identifiant aléatoire (UUID) ou dérivé du chemin à la création ?** Recommandation : **UUID v4 à la création**, jamais recalculé (un identifiant dérivé du chemin changerait au premier renommage).
  **Décidé (2026-10-17)** : **UUID dans un fichier `.meta` voisin**, avec les réglages d'import.
- **Simplification de maillages et ordre pour le cache : maison ou *meshoptimizer* ?** *meshoptimizer* (MIT) est la référence du domaine. Règle des bibliothèques : **maison par défaut** (quadrique d'erreur de Garland et Heckbert pour la simplification, algorithme de Forsyth ou de Tom Forsyth pour le cache) ; *meshoptimizer* à **proposer** si la version maison n'atteint pas la qualité. **Décision** au moment de la tâche.
- **Les `.meta` dans le dépôt d'assets ?** Oui, toujours avec leur asset (un `.meta` perdu, c'est un identifiant perdu).

### Pièges connus

- Deux copies d'un asset avec le **même identifiant** (copier-coller dans l'explorateur) : le registre doit le détecter et en réattribuer un.
- Les références **dans le code** (`Demo3D::kBarrel`, chemins en dur) échappent à la vérification : le code désigne lui aussi ses assets par identifiant, ou par un chemin vérifié à la préparation.
- Les niveaux de détail qui changent la **silhouette** des ombres ou des collisions : la logique ne lit jamais un niveau de détail.

### Validation

- Renommer un modèle, une texture et un effet de la tranche dans l'éditeur : la tranche se charge et se joue sans changement ; la préparation ne signale rien.
- Le jeu livré ne lit plus aucun JSON ni glTF (vérifié sur le contenu des archives) ; son chargement est mesuré plus court.
- Les niveaux de détail réduisent les triangles de la scène de mesure « foule » (mesure avant / après).

### Implémentation réalisée (partie 4)

Fait le 2026-10-17 (en autonomie).

- **Identifiants** (`asset_registry.hpp`, `asset_registry.cpp`) : `AssetId` (UUID v4 de RFC 9562, tiré d'une source aléatoire du système, lu sans casse, écrit en minuscules), `AssetMeta` et le fichier `<asset>.meta` (`{"id", "import", "version": 1}`), `is_identified_asset()` (tout sauf `.meta`, `.bin`, `data/`, `.md`, `.txt`, fichiers cachés). `AssetRegistry` : `scan()` de plusieurs dossiers (le premier qui a une clé gagne, les autres listés), `.meta` créés pour les assets qui n'en ont pas (en développement sur les sources seulement), **doublons réattribués** à la copie (celle dont la clé vient après, quel que soit l'ordre du disque), `.meta` orphelins listés ; `find` par identifiant ou par clé, `move()` (le fichier et son `.meta`), `encode()` / `decode()` du **registre figé** `asset_registry.mreg` (`MREG`, CBOR contrôlé). Format en [FORMATS.md, 15](FORMATS.md#15-identité-des-assets--meta-et-registre).
- **Le registre dans l'application** : `Application::build_asset_registry()` lit le registre figé des archives (jeu livré) ou parcourt le dossier des sources, celui du moteur et les **dossiers montés** (`ApplicationConfig::asset_folders`, `mount_asset_folder()` dans `paths.hpp` : un autre dossier d'assets lu en place, rechargé à chaud ; la tranche monte celui de l'ARPG). `Assets::resolve_reference()` (identifiant d'abord, chemin en repli avec un avertissement, erreur sans l'un ni l'autre), `reference_to()` ; les champs `Asset<T>` de la réflexion s'écrivent `{"id", "path"}` et se lisent par là ; les préfabriqués suivent leurs dépendances par identifiant.
- **Outil** `tools/assets/assets.py` : `check`, `meta`, `move` (fichier, `.meta`, puis chemins réécrits à côté des identifiants), `find`, `refresh`. Les 39 accessoires de l'ARPG (`.prop.json`) sont devenus des préfabriqués (`ARPG/assets_source/commun/prefab_moteur.py`, kits et catalogue mis à jour) ; la tranche et la scène « rue » de l'ARPG placent le même lampadaire, et les pavés de la tranche sont les `.glb` de l'ARPG (plus de copie par `fetch_arpg_models.py` pour eux).
- **Import des modèles** (`model_import.hpp`, `model_import.cpp`, `mesh_optimize.hpp`, `mesh_optimize.cpp`), **maison** (décision « maison par défaut » tenue : la qualité atteinte ne demande pas *meshoptimizer*) : réglages `ModelImportSettings` lus du `import` du `.meta` (`lods`, `lod_reduction`, `lod_error`, `lod_min_triangles`, `optimize`, `textures`) ; **simplification** par quadriques d'erreur (Garland et Heckbert) sur demi-arêtes, bords et coutures d'UV verrouillés, refus des triangles retournés, aplatis ou dont la normale s'écarte de l'original ; **ordre pour le cache** de sommets (algorithme de Forsyth, taux de défauts mesuré par `average_cache_miss_ratio`) et **ordre de lecture des sommets** ; niveaux de détail en plages d'indices sur les mêmes sommets (`MeshLod`, `MeshData::lods`) ; images intégrées **encodées en KTX2 UASTC** dans le moteur (`encode_ktx2`, mipmaps en boîte, cartes de normales sur deux canaux) ; clips construits une fois en **archive ozz** (`ClipLibrary::save()` / `load()`). Résultat écrit en **`.mmdl`** (`encode_prepared_model`, [FORMATS.md, 16](FORMATS.md#16-modèles-préparés-mmdl)).
- **Cache d'import** : `.imported/` à côté du dossier des sources (ignoré par le dépôt) ; empreinte (sources, réglages, version de l'importeur) dans le `.mmdl`, signature rapide `.mmdl.files` (tailles et dates) pour ne pas relire les sources ; ré-import sur un fil de travail, un seul à la fois par modèle ; `Assets::import_all()` et `bac_a_sable --import-assets` (lancé par `check_references.py` avant les captures). Les textures KTX2 intégrées sont transcodées pendant la lecture en arrière-plan (en parallèle, image par image) ; le fil principal ne fait que les envoyer au GPU (`Model::create` avec textures déjà faites).
- **Choix du niveau de détail** : `MeshRenderer::select_lods()` prend, pour chaque passe, le niveau le plus grossier dont l'erreur projetée tient sous `set_lod_threshold()` pixels (1 par défaut ; 0 : maillages complets) ; la logique n'en lit aucun.
- **Matériaux** (`material.hpp`) : `MaterialFactors` (les nombres), `Material` qui **tient des `Asset<Texture>`**, `DrawMaterial` (pointeurs bruts, pour une image) par `Material::view()` ; `Model::texture_owned` pour les textures propres au modèle.
- **Documents livrés en CBOR** (`document.hpp`, `document.cpp`) : `parse_document()` (JSON ou CBOR contrôlé) dans tous les lecteurs (descriptions d'animation, crédits, atlas, liaisons, cartes, effets, préfabriqués), `shipped_document()` à la préparation ([FORMATS.md, 17](FORMATS.md#17-documents-livrés-en-cbor)).
- **Préparation** (`cook.cpp`, `kCookVersion` = 2) : registre parcouru, doublons en erreur, dépendances des préfabriqués par identifiant (inconnu sans chemin : erreur ; chemin périmé : avertissement), modèles livrés sous leur forme `.mmdl` seulement (textures intégrées transcodées en BC7 / BC5), `.json` en CBOR, registre figé, relecture de tout (préfabriqués avec leurs bases et leurs assets, refus d'un document encore en JSON), `.meta` jamais livrés. `moteur_package(... ASSET_FOLDERS ...)` prépare aussi les dossiers montés ; la tranche livre les assets de l'ARPG qu'elle utilise. `check_package.py` vérifie sur les archives installées qu'il n'y a **ni glTF ni JSON**.
- **Tests** : `test_asset_registry.cpp` (identifiants, `.meta`, parcours, doublons, orphelins, déplacement, registre figé, dossiers montés), `test_mesh_optimize.cpp` (simplification : bornes d'erreur, bords et coutures, pas de triangle retourné ; cache ; ordre des sommets), `test_model_import.cpp` (niveaux de détail et images KTX2 sans autre changement, aller-retour exact du `.mmdl`, fichiers abîmés refusés), `test_document.cpp` ; `test_model.cpp`, `test_ktx_texture.cpp`, `test_world_save.cpp`, `test_particles.cpp` mis à jour. Captures de référence enregistrées de nouveau (la tranche a les pavés et le lampadaire de l'ARPG, et les niveaux de détail) : `demo3d` `ebd194e461cb`, `demo3d_replay` `e30c5b725f33`, `slice` `e892b5096001`, exactitude `201495001d7b` ; 2D inchangée.
- **Mesures** (Windows, RTX 4070 Ti SUPER, 1920 × 1080) : scène « foule » **4,41 → 3,12 millions de triangles** (−29 %) grâce aux niveaux de détail. Chargement de la tranche (développement, cache d'import chaud) **283 ms** contre 245 ms au jalon 8, avec deux assets de plus (pavés et lampadaire de l'ARPG, leurs textures) ; il était à 1 380 ms avant que le transcodage des textures intégrées passe en arrière-plan, puis 395 ms avant qu'il soit parallèle. Combat : moyenne 3,1 ms, p99 4,4 ms. Des images isolées au-delà de 33 ms sont apparues dans 3 mesures sur 8, **dans toutes les zones à la fois** (le processus entier arrêté) ; quatre exécutions de 30 s lancées seules n'en montrent aucune (maximum 9 à 16 ms) : c'est l'activité de la machine pendant la mesure, pas le moteur. Mémoire du processus au combat 322 → 400 Mo, GPU 116 → 157 Mo : les textures de l'ARPG (1K, avec mipmaps).
- **Reporté** (décision prise en autonomie) : **sommets quantifiés** sur le GPU (positions 16 bits, normales octaèdres) au **jalon 10** : cela change le format des sommets de tous les shaders et du rendu, ce qui est le sujet du jalon 10 ; les imposteurs de foule aussi (déjà prévu). Le **renommage dans l'éditeur** (validation) se fera avec l'éditeur de la partie 10 ; l'outil `assets.py move` fait la même chose aujourd'hui ; un déplacement garde l'identité (`test_asset_registry.cpp`) et les préfabriqués désignent leurs assets par identifiant.

---

## 5. Monde en 3D : géométrie de niveau, collisions, requêtes

### But

La **logique** du jeu vit en vraie 3D (décision 6.1) : plusieurs niveaux superposés (ponts, étages, cryptes sous la ville), dénivelés, escaliers et rampes. La grille au sol reste un outil (requêtes rapides, champs de flux par niveau), plus le monde.

### Tâches

- [x] **Géométrie de niveau** : ce qui définit le monde pour la logique. Les maillages de collision des pièces de niveau (marqués à l'import : marchable, mur, obstacle, vide), assemblés en une **scène de collision statique** par niveau ou par morceau (partie 8).
- [x] **Structure d'accélération** pour la scène statique (arbre de volumes englobants), requêtes déterministes (ordre des résultats fixe) : rayon, sphère, capsule, boîte ; « le sol sous ce point » (hauteur et normale).
- [x] **Contrôleur de personnage** : déplacement d'une capsule contre la géométrie (glissement le long des murs, marches d'escalier jusqu'à une hauteur donnée, pentes maximales), puis contre les autres personnages (la poussée actuelle des `Collider`, en 3D).
- [x] **Composants** : `Transform` reste 3D ; `Mover`, `Collider`, `SpatialHash` et les requêtes de la logique passent du plan `(x, z)` à la 3D (ou au plan d'un niveau, avec sa hauteur).
- [x] **Déterminisme** : mêmes règles qu'aujourd'hui (aucune fonction transcendante dans la logique, opérations dans un ordre fixe) ; les rejeux et le test d'exactitude couvrent les nouveaux chemins.
- [x] **Débogage** : affichage de la scène de collision, des capsules, des requêtes (lignes de débogage existantes).

### Questions à se poser

- **D'où vient la géométrie des niveaux ?** (a) des **kits modulaires** faits dans Blender (murs, sols, escaliers, arches, à une grille commune), assemblés dans l'éditeur ; (b) des **niveaux entiers** modélisés dans Blender ; (c) les deux. Recommandation : **(a) d'abord** : c'est ce qui permet l'éditeur (partie 10) et la génération procédurale du jalon 6 de l'ARPG (Path of Exile assemble ses zones à partir de pièces *(à vérifier)*) ; (b) reste possible pour une zone unique (un boss). **Décision de l'utilisateur** (c'est aussi un choix de production artistique).
  **Décidé (2026-10-17)** : **(a), des briques assemblées** : un kit de pièces par ambiance, faites dans Blender à une grille commune (comme le kit de clôture gothique en cours), assemblées dans l'éditeur ; une zone unique peut être modélisée en entier.
- **Physique** : la physique de corps rigides (ragdolls, débris) est au jalon 10 (Jolt, à proposer). Ce jalon ne fait que la **collision cinématique** de la logique (contrôleur de personnage, requêtes), maison. Recommandation : garder cette séparation : la logique ne dépend pas d'une simulation physique.

### Pièges connus

- Les **marches** et les **bords** : un personnage qui tremble au bord d'une marche, ou qui monte sur un muret qu'il ne devrait pas franchir. Paramètres explicites (hauteur de marche, pente) et tests.
- Les **erreurs flottantes** qui s'accumulent en glissant contre un mur : corrections en fin de pas, pas de récursion sans limite.
- Mélanger la géométrie **visible** et la géométrie **de collision** : une décoration fine (grille, lustre) ne bloque rien ; c'est l'import qui décide, pas le rendu.

### Validation

- Un personnage monte un escalier, passe sous un pont puis dessus, tombe d'un rebord, glisse le long d'un mur, sans traverser ni trembler (tests et scène de démonstration).
- Les requêtes donnent les mêmes résultats d'une exécution à l'autre ; le test d'exactitude passe sur un niveau à deux hauteurs.

### Implémentation réalisée (partie 5)

Fait le 2026-10-18 (en autonomie).

- **Géométrie de collision** (`collision_scene.hpp`, `collision_scene.cpp`) : `SurfaceKind` (sol, mur, obstacle, vide) par triangle ; `CollisionMeshData` (sommets, indices, genres ; `add_box`, `add_stairs`, `add_ramp` pour les maquettes et les tests ; `append` avec retournement des triangles sous une matrice miroir ; `validate`). **Import** : les nœuds `COL_...` / `UCX_...` d'un modèle deviennent sa collision et ne sont jamais dessinés ; leur nom donne le genre, sinon la pente ; réglage `collision` du `.meta` (`named`, `visible`, `none`) ; `ModelData::collision`, `Model::collision`, format `.mmdl` v3. **Pièces de niveau** : composant `StaticCollision` (la collision du modèle, des boîtes) et `gather_static_collision(world, scene)` (`level_collision.hpp`).
- **Structure d'accélération** : `CollisionScene`, arbre de volumes englobants (coupe à la médiane le long du plus grand côté, égalités par l'indice : même arbre à chaque construction), parcours itératif. Requêtes `const`, sûres entre fils : `raycast` (Möller-Trumbore, deux faces), `segment_clear` (ligne de vue), `ground_at` (le sol le plus haut sous un point dans une plage de hauteurs, filtré par la pente : plusieurs étages), `sphere_contacts`, `capsule_contacts` (points les plus proches segment-triangle, Ericson), `box_triangles` (axes séparateurs). Résultats triés (profondeur ou distance, puis indice).
- **Contrôleur de personnage** (`character_controller.hpp`) : capsule debout (`CharacterShape`), déplacée par pas d'au plus la moitié de son rayon, poussée hors des murs (contact le plus profond d'abord, `max_passes` au plus), le bas relevé de la hauteur de marche pour que les marches se montent ; sol suivi à une marche au-dessus et `snap_distance` au-dessous (escaliers descendus sans sauter) ; pentes au-delà de `min_ground_normal_y` (un cosinus, 45 degrés par défaut) traitées en murs ; chute (gravité, vitesse limite) et atterrissage ; marche sous un plafond trop bas refusée ; `push_out_of_level`, `character_fits`, `ground_under`. Réglages décrits par la réflexion (`ControllerSettings`).
- **Composants et systèmes** : `Mover::goal`, `Mover::path` et `Mover::tick_start` en 3D (les sauvegardes plus anciennes se lisent : `upgrade_mover`, `[x, z]` devient `[x, 0, z]`), `Mover::grounded` et `vertical_speed` ; `Collider::height` ; `move_movers(registry, scene, ...)` et `separate_colliders(registry, scene, hash, ...)` pour un niveau 3D (deux personnages ne se poussent que si leurs hauteurs se recouvrent), les versions sur grille inchangées ; `SpatialHash` et ses requêtes prennent une tranche de hauteurs (`HeightSpan`). Sans maillage de navigation (partie 6), un `go_to()` dans un niveau 3D va en ligne droite.
- **Débogage** : variable `debug.collision` (triangles par genre près de la caméra, capsules) ; chemins dessinés à leur hauteur. Scène « Level 3D » du bac à sable (`--level3d`, `--level3d-auto` pour un parcours fixe) : escalier, plateforme, pont, rampes de 30 et 60 degrés, rebord, plafond bas ; le héros au clic (rayon de la caméra contre le niveau) ou au clavier ; des marcheurs sur le pont et dessous.
- **Déterminisme** : seulement + − × ÷ et racine carrée, ordre fixe ; test « au bit près » de 600 pas aléatoires rejoués deux fois. Le test d'exactitude de la tranche (sauvegarde et rechargement en cours de partie) reste sur la grille jusqu'à la partie 11 (la tranche dans un niveau 3D) ; les déplacements 3D sont sauvegardés (`grounded`, `vertical_speed`, points 3D) et relus par le test des composants.
- **Tests** : `tests/test_collision_scene.cpp` (9 cas : noms des nœuds ; arbre comparé à la force brute sur 600 triangles, rayons, sphères ; sol sous un point sur deux hauteurs ; murs, glissement, escalier monté et descendu sans tremblement, marche trop haute, plafond bas ; pentes de 30 et 60 degrés, sous et sur le pont, chute d'un rebord ; déterminisme ; mouvements 3D jusqu'au sommet de l'escalier et personnages sur deux étages qui ne se poussent pas ; collision importée et `.mmdl` ; `StaticCollision`). 513 tests. Captures de référence et test d'exactitude **inchangés**.
- **Défaut trouvé en chemin** : dans la tranche, `goal = mover->goal` affectait un point 3D à un `std::optional<glm::vec2>` : glm le tronquait en silence en `(x, y)` au lieu de `(x, z)`, et le marqueur de destination partait ailleurs (seule la capture de référence de la tranche l'a vu). Corrigé, et **les conversions de glm entre tailles de vecteurs sont maintenant explicites** (`GLM_FORCE_EXPLICIT_CTOR`, public sur la cible `moteur` : les jeux aussi) : une seule autre conversion implicite existait (un test), et l'ARPG compile tel quel.

---

## 6. Navigation en 3D : maillage de navigation, chemins, foules

### But

Trouver des chemins dans un monde à plusieurs niveaux : un **maillage de navigation** généré depuis la géométrie du niveau, les chemins dessus, et le déplacement de nombreux monstres (les foules d'un ARPG).

### Tâches

- [x] **Génération du maillage de navigation** depuis la scène de collision, par **Recast** (décidé) : zones marchables selon la taille de l'agent (rayon, hauteur, marche, pente). Plusieurs **tailles d'agent** (le héros, les grands monstres). Une API de navigation du moteur par-dessus, sans type de Recast dans les en-têtes publics.
- [ ] **Génération à la préparation** (et dans l'éditeur, en direct) ; génération **par morceaux** (partie 8), raccordés.
- [x] **Chemins** par **Detour** : A\* sur les polygones, lissage (*funnel*), requêtes « point le plus proche sur le maillage », « est-ce marchable », rayon sur le maillage.
- [x] **Liaisons hors maillage** : sauts, échelles, portes (franchissables selon l'état), coûts par zone (eau, feu).
- [x] **Foules** : les champs de flux actuels, **par niveau** ou sur le maillage ; évitement local entre agents (RVO / ORCA), déterministe ; le budget actuel (500 monstres en 0,43 ms sur la grille) comme cible.
- [x] **Obstacles dynamiques** : une porte fermée, un mur détruit, une caisse poussée : mise à jour locale du maillage ou obstacles découpés.
- [x] La **grille** du jalon 6 devient une vue du maillage (requêtes rapides, brouillard), pas la vérité du monde.

### Questions à se poser

- **Maison ou Recast & Detour ?** Recast & Detour (zlib) est la référence du domaine, utilisée par de nombreux moteurs *(à vérifier lesquels aujourd'hui)*. Règle : **maison par défaut** (l'algorithme est publié et documenté) ; Recast & Detour à **proposer** si la version maison bute (qualité des contours, performance de génération). Recommandation : **maison**, en suivant la structure de Recast (voxels, régions par *watershed*, contours, polygones, détail des hauteurs) pour bénéficier de son retour d'expérience. **Décision** au moment de la tâche.
  **Décidé (2026-10-17)** : **Recast & Detour** (bibliothèque, choix de l'utilisateur) : Recast pour la génération, Detour pour les requêtes et les chemins. Le moteur garde **sa propre API** de navigation par-dessus (le jeu n'inclut jamais Recast), pour pouvoir changer d'implémentation. Paquet vcpkg `recastnavigation` *(à vérifier : nom et version)*, licence zlib, ajouté aux crédits du moteur et à ses licences. À vérifier : le déterminisme de Detour (et de DetourCrowd s'il est utilisé) avec un ordre fixe des agents ; à défaut, l'évitement local reste maison.
- **Le déterminisme des foules** : l'évitement local est sensible à l'ordre de traitement ; ordre fixe des agents, et le parallélisme (partie 9) qui garde cet ordre.

### Pièges connus

- Un maillage qui « fuit » par une fente de la géométrie, ou qui manque une porte étroite : réglages par taille d'agent et visualisation dans l'éditeur.
- Des chemins qui longent les murs au millimètre : marge (rayon de l'agent) et lissage.
- Régénérer tout le maillage pour une porte : coûteux ; découpage par tuiles.

### Validation

- Dans un niveau à deux hauteurs, un monstre rejoint le héros par l'escalier ; 500 monstres l'entourent sans se bloquer, dans le budget.
- Le maillage se régénère dans l'éditeur à chaque modification du niveau, en moins d'une seconde pour une salle *(cible à mesurer)*.

### Implémentation réalisée (partie 6)

Fait le 2026-10-18 (en autonomie ; ajout de la bibliothèque accordé par l'utilisateur le même jour).

- **Bibliothèque** : paquet vcpkg `recastnavigation` 1.6.0 (zlib ; Recast, Detour, DetourCrowd liés en privé), ajouté aux crédits du moteur et à ses licences livrées. Aucun type de Recast ni de Detour dans les en-têtes publics : l'API est celle du moteur (`navigation.hpp`).
- **Maillage de navigation** (`NavMesh`) : construit depuis la `CollisionScene` d'un niveau pour une taille de personnage (`NavAgent` : rayon, hauteur, marche, pente comme cosinus), découpé en **tuiles** (`NavBuildSettings::tile_size`, 24 m) : le chemin de Recast (voxels, filtres des rebords et des plafonds bas, érosion du rayon, régions par *watershed*, contours, polygones, maillage de détail), les triangles `Walkable` assez plats faisant le sol, `Wall` et `Obstacle` bloquant, `Void` ignorés. **Zones** à coût (`NavAreaVolume` : eau, feu), **liaisons hors maillage** (`NavLink` : saut, échelle, porte ; `set_link_enabled()` ferme une porte), **obstacles** (`add_obstacle()` / `remove_obstacle()` : seules les tuiles touchées sont reconstruites), `rebuild(area)` après une modification du niveau, et **élagage** des îlots inaccessibles (`prune(seeds)` : Recast trouve du sol partout où il y a la place, y compris dans une boîte fermée ; les polygones qu'on n'atteint pas depuis les points d'entrée sont écartés, de nouveau après chaque reconstruction).
- **Requêtes** : `find_path()` (A\* de Detour puis lissage *funnel* ; `Found`, `Partial` vers le point atteignable le plus proche, `None` ; un but « atteint » doit l'être à la bonne hauteur, pas sur l'étage du dessous), `nearest_polygon()`, `nearest_point()`, `locate()` (le polygone sous un personnage, gardé d'un tick à l'autre), `walkable_line()`, `height_at()`, `triangles()` (outils) ; `NavFilter` (coût par zone, portes, sauts).
- **Forme sauvegardée** `MNAV` (`save()` / `load()` : réglages, zones, liaisons, obstacles, portes fermées, points d'élagage et tuiles de Detour, somme FNV-1a) pour les niveaux préparés de la partie 8 ([FORMATS.md, 18](FORMATS.md#18-maillages-de-navigation-mnav)).
- **Déplacements** : `plan_paths(registry, meshes, ...)` planifie sur le maillage du `Mover` (`Mover::agent`, un maillage par taille) ; `move_movers(registry, scene, dt, NavSources, ...)` suit les chemins 3D et les **champs de flux sur le maillage** (`NavFlowField` : Dijkstra des polygones depuis la cible, portail vers le polygone suivant, le point visé pris sur le portail près du personnage) ; **évitement local** maison et déterministe (`Mover::avoid` : temps avant la collision avec les six voisins les plus proches, d'après leur dernière vitesse, sans trigonométrie). L'évitement de DetourCrowd n'est pas utilisé : son échantillonnage adaptatif calcule des sinus et des cosinus, contraires à la règle de la logique.
- **La grille comme vue** : `nav_grid_view(mesh, origine, largeur, hauteur, étage)` donne la `NavGrid` d'un étage (brouillard, requêtes rapides) ; `NavGrid(largeur, hauteur)`.
- **Débogage et scène** : `debug.navmesh` ; la scène « Level 3D » a maintenant son maillage (héros au clic par l'escalier ou la rampe du pont, monstres au clavier `M` ou `--level3d-crowd N` en foule, caisses posées au clic droit avec reconstruction locale mesurée).
- **Mesures** (Release, Windows) : niveau de test de 40 x 40 m, 4 tuiles et 64 polygones construits en **15 ms** ; une caisse posée reconstruit ses tuiles en **6 ms** (cible de l'éditeur : moins d'une seconde par salle) ; **500 monstres** qui poursuivent le héros sur le maillage, évitement compris : **1,2 ms par tick** (déplacement, évitement, contrôleur de personnage, séparation ; 4,5 ms avant que l'évitement ne se limite aux six voisins les plus proches), 433 sur 500 à moins de 9 m du héros après 10 s. La grille du jalon 6 faisait 0,43 ms sans contrôleur ni étages ; la partie 9 (image parallèle) répartira ce travail.
- **Tests** : `tests/test_navigation.cpp` (4 cas : étages, escalier, pont, requêtes, grille vue du maillage ; échelle, porte fermée, sauts interdits, zone de feu contournée, obstacle posé et retiré, forme sauvegardée et refus d'un fichier abîmé ; déplacement planifié jusqu'en haut de l'escalier et monstre qui descend par l'escalier ; 500 monstres). 517 tests.
- **Reste, pour la suite** : le maillage **par morceaux** préparé et raccordé (partie 8, la forme `MNAV` est prête) ; la régénération **en direct dans l'éditeur** (partie 10, `rebuild()` est prêt) ; une foule qui **entoure** le héros au lieu de s'entasser du côté d'où elle arrive (places autour de la cible, à régler avec la tranche en partie 11).

---

## 7. Vision, brouillard et sélection à la souris en 3D

### But

Que la **vision** (lignes de vue des monstres et du héros), le **brouillard de guerre** et la **sélection à la souris** tiennent compte de la hauteur ; et remplacer la sélection provisoire (3.5 : la créature la plus proche du point visé au sol) par une vraie sélection, avant le jalon 1 de l'ARPG.

### Tâches

- [x] **Lignes de vue** en 3D contre la scène de collision (rayons), avec la symétrie actuelle (si A voit B, B voit A) ; champ de vision par niveau.
- [x] **Brouillard de guerre** en 3D : exploré et visible par zone du maillage ou par cellule de niveau, sans laisser voir l'étage du dessous à travers le sol.
- [x] **Sélection** : rayon de la caméra contre les **formes de sélection** (capsules des personnages, boîtes des objets) avec **priorités** (monstre, objet au sol, interactif, sol) ; et un **tampon d'identifiants** rendu à l'image pour la précision au pixel du survol (contour du monstre survolé).
- [x] **Point visé au sol** en 3D (déplacement au clic) : rayon contre la géométrie marchable, ramené sur le maillage de navigation.
- [x] **Coupe de l'avant-plan** : les murs et étages qui cachent le héros s'estompent (le tramage actuel), en 3D (ce qui est au-dessus du niveau du héros, ou entre lui et la caméra).

### Questions à se poser

- **Tampon d'identifiants ou seulement des rayons ?** Recommandation : **les deux** : les rayons pour la logique (déterministes, au tick), le tampon pour le survol à l'affichage (précis au pixel, lu avec une image de retard).

### Pièges connus

- Lire le tampon d'identifiants **dans la logique** : il dépend du rendu et du matériel, donc rompt le déterminisme. Il ne sert qu'à l'affichage du survol ; le clic passe par les rayons au tick.
- La lecture du GPU vers le CPU qui bloque l'image : lecture différée d'une image.

### Validation

- Cliquer sur un monstre à moitié caché par un pilier le sélectionne ; survoler montre son contour ; cliquer sur l'étage du dessous à travers un trou du sol fonctionne.
- Un monstre derrière un mur ne voit pas le héros ; un monstre sur le pont au-dessus non plus, sauf ouverture.

### Implémentation réalisée (partie 7)

Fait le 2026-10-18 (en autonomie).

- **Lignes de vue en 3D** (`picking.hpp`) : `line_of_sight(level, a, b)` contre les murs et les sols de la `CollisionScene`, **symétrique** (testée dans les deux sens). **Grille d'un étage** : `floor_sight_grid(navmesh, level, origine, largeur, hauteur, étage, hauteur des yeux)` : praticable d'après le maillage de navigation sur cet étage, opaque là où des murs de cet étage se dressent entre 0,3 m et la hauteur des yeux (et dans l'épaisseur d'un mur plein : un rayon depuis la case en sort par une face vue de dos) ; les champs de vision symétriques et l'exploration du jalon 6 (`FieldOfView`, `ExploredMap`) s'y appliquent tels quels, un étage à la fois (rien ne se voit à travers le sol : l'étage du dessous a sa grille).
- **Sélection par rayons** (la logique, au tick, déterministe) : composant `Selectable` (`selectable` : genre `PickKind` personnage, objet, interactif ; capsule ou boîte ; désactivable), `pick(registry, rayon, niveau, maillage)` : la première rencontre de chaque forme **devant** la géométrie du niveau (un monstre à moitié caché par un pilier est pris par la partie qu'on voit), puis le genre le plus prioritaire, la plus proche, le plus petit identifiant ; sans entité, **le sol** : la première surface du niveau rencontrée, ramenée sur le maillage de navigation (un mur donne le point praticable le plus proche ; à travers un trou du plancher, l'étage du dessous). `ray_capsule()`, `ray_box()`.
- **Tampon d'identifiants** (l'affichage du survol, au pixel près) : `MeshDraw::pick_id` (porté par chaque instance, `normal_matrix[1].w`), donné par `World::submit()` pour chaque entité dessinée (`World::pick_id_of()` : la plus proche entité `Selectable` en remontant ses parents, son arme et son corps désignent le monstre) ; `MeshRenderer::set_ids(true)` ajoute après la scène une **passe d'identifiants** (`mesh_id.*.hlsl` : les lots de la scène, une cible R32_UINT et une profondeur à elles, sans échantillonnage multiple) ; `Renderer::request_pick(pixel)` copie le pixel visé dans un tampon de lecture, lu **une ou deux images plus tard** sans attendre le GPU (`picked_id()`, `World::entity_of_pick()`) ; `Renderer::set_outline(id, couleur, épaisseur)` trace le **contour** de l'entité survolée dans la passe de tonalité. Le clic, lui, passe par les rayons : le tampon dépend du rendu et du matériel, la logique ne le lit jamais.
- **Coupe de l'avant-plan en 3D** : `Cutout::ceiling` et `ceiling_radius` : ce qui s'estompe (`Material::fades`) plus haut que `ceiling` au-dessus du héros, dans un rayon autour de lui, est retiré (les étages et toits au-dessus), avec un bord tramé ; s'ajoute au trou dans les murs entre la caméra et le héros.
- **Scène « Level 3D »** : contour du monstre survolé (tampon), clic sur un monstre (rayons, le héros va à lui), tablier du pont coupé autour du héros ; en pilote automatique, la scène vise un marcheur et journalise si le tampon le retrouve (vérifié : retrouvé à chaque relecture après la première image).
- **Shaders** : `mesh_id.vert`, `mesh_id_skinned.vert`, `mesh_id.frag` ; `mesh.frag` (coupe des étages), `tonemap.frag` (contour) ; MSL exportés de nouveau pour le Mac.
- **Tests** : `tests/test_picking.cpp` (2 cas : formes, priorités, monstre à moitié caché par un pilier, pilier qui cache, objet au pied d'un monstre, monstre mort, sol à travers un trou du plancher et plancher ailleurs ; lignes de vue à travers un mur, du pont vers le dessous, du pont vers le côté, symétrie ; grille d'un étage et champ de vision derrière un mur). 519 tests. Captures de référence et test d'exactitude **inchangés** (le contour est éteint par défaut).
- **Limites connues** : la passe d'identifiants ignore le test alpha des matériaux découpés (un feuillage y est plein) ; elle redessine les maillages de la scène (profondeur et entier seulement), d'où l'option ; le brouillard rendu reste une grille au sol par étage, celle de l'étage du héros (`set_fog_cells`).

---

## 8. Format de niveau v3 et chargement par morceaux

### But

Un format de **niveau** de production (7.x de la revue) : binaire, découpé en **morceaux** chargés à la demande, portant la géométrie 3D, les préfabriqués placés, la navigation et ce que l'éditeur y met ; il remplace les cartes v1 et v2 (JSON et caractères).

### Tâches

- [x] **Format source** (texte, pour le versionnement et les fusions) écrit par l'éditeur : instances de préfabriqués et de pièces de kit (identifiant, transformation, surcharges), matériaux de sol, lumières, volumes (zones, déclencheurs, son d'ambiance), points nommés, couches.
- [x] **Format préparé** (binaire) : par morceau, la scène de collision, le maillage de navigation, les instances regroupées pour le rendu, les références d'assets à charger.
- [x] **Chargement par morceaux** autour du héros (et de la caméra), en arrière-plan (le système de chargement du jalon 8), sans à-coup ; déchargement avec marge ; budget mémoire.
- [x] **Migration** des cartes v1 / v2 (tuiles et caractères) vers v3 : un outil, et la lecture des anciennes tant que des tests en dépendent.
- [x] **Sauvegarde** : ce qui change dans le niveau (porte ouverte, coffre vidé) sauvegardé par identifiant d'instance, indépendamment du morceau chargé.

### Questions à se poser

- **Taille des morceaux et forme** (grille régulière de N mètres, ou découpage par salles) ? Recommandation : **grille régulière** (32 ou 64 m *(à mesurer)*) : simple, prévisible, compatible avec la génération procédurale ; les salles restent une notion de jeu.
- **Monde ouvert ou zones fermées ?** Un ARPG à la Path of Exile charge une **zone** entière à son entrée *(à vérifier)* ; les morceaux servent alors surtout à la mémoire et au rendu. Recommandation : **supporter les deux**, zones fermées d'abord (l'ARPG), chargement continu possible.
  **Décidé (2026-10-17)** : **grille régulière, zones fermées d'abord**, chargement continu possible avec le même format.

### Pièges connus

- Les objets **à cheval** sur deux morceaux : rattachés à un seul (leur origine), visibles depuis les deux.
- La navigation **coupée** entre morceaux : raccordement des maillages à la préparation, testé.
- Les identifiants d'instance qui changent à chaque sauvegarde de l'éditeur : ils sont attribués une fois et gardés.

### Validation

- Un niveau de test plus grand que la mémoire permise par le budget se traverse sans à-coup (aucune image au-delà de 33 ms, scène de mesure « traversée » refaite) ; le chargement initial d'une zone tient le budget du jalon 8 (5 s).
- La tranche en v3 donne, après migration, la même partie qu'en v2 sur les mêmes entrées *(si la géométrie ne change pas ; sinon nouvelles références documentées)*.

### Implémentation réalisée (partie 8)

Fait le 2026-10-18 (en autonomie).

- **Format source** (`level.hpp`, `level.cpp`, [FORMATS.md, 19](FORMATS.md#19-niveaux-v3-leveljson)) : `LevelDocument` : instances de préfabriqués (identifiant **stable**, donné une fois par `add()`, jamais réutilisé : `next_id`), calques, points nommés, volumes (zones, déclencheurs, `nav_area`), sol peint (grille de préfabriqués de sol), réglages de navigation (un agent par taille) ; références d'assets par identifiant ; erreurs avec les chemins de `DataReader`. **État de partie** : `LevelState` (surcharges et retraits par identifiant d'instance, appliqués à chaque chargement du morceau, sauvegardés par le jeu).
- **Format préparé** (`level_bake.hpp`, [FORMATS.md, 20](FORMATS.md#20-niveaux-préparés-mlvl-et-mlvc)) : `bake_level()` : la collision statique de chaque instance (`prefab_collision()` : les `static_collision` des entités du préfabriqué, avec la collision de leur modèle et leurs boîtes, à leur place dans la hiérarchie, surcharges comprises), découpée selon la grille (chaque triangle dans le morceau de son centre) ; pour chaque agent, le maillage de navigation **du niveau entier** construit une fois (`NavMesh::build_tiled()`, une tuile par morceau, élagué depuis les points du niveau), gardé tuile par tuile ; les instances de chaque morceau. Un en-tête `.mlvl` et un fichier `.mlvc` par morceau non vide, scellés par une somme FNV-1a. `NavMesh::start_tiled()`, `tile_data()`, `add_tile()`, `remove_tile()` (Detour raccorde les tuiles voisines à l'ajout).
- **Chargement par morceaux** (`level_streamer.hpp`) : `LevelStreamer` : les morceaux voulus autour d'un point (rayon de chargement, rayon de déchargement plus grand : une marge ; `max_chunks` : le budget mémoire, les plus loin partent d'abord) sont **lus et décodés en arrière-plan**, leurs préfabriqués préchargés, puis **instanciés quelques-uns par mise à jour** (`instantiate_budget_ms`) ; la scène de collision est reconstruite **en arrière-plan** et échangée ; les tuiles de navigation s'ajoutent et se retirent. `load_all` : une zone fermée chargée d'un coup derrière l'écran de chargement (`finish()`). `open_any()` prend la forme préparée, ou prépare le niveau en mémoire depuis sa source en développement (`asset_level_sources()`, `read_level()`). `LevelInstanceId` marque la racine de chaque instance.
- **Préparation** (`cook`) : chaque `.level.json` livré devient son en-tête et ses morceaux (préfabriqués et modèles préparés relus depuis la sortie), relus aussitôt ; ses préfabriqués et leurs modèles sont des dépendances (par identifiant).
- **Migration** des cartes v1 et v2 : `migrate_map()` et `bac_a_sable --migrate-map CARTE SORTIE RÈGLES` : cases en suites le long de x (une instance mise à l'échelle par suite), points, objets en instances ou en points (apparitions), connecteurs en volumes. La carte de la tranche migrée (`assets/levels/tranche_migree.level.json` depuis la partie 11, règles `tranche.migration.json`, pièces de maquette) : 4 228 instances et 499 points, préparée en mémoire en 1,1 s et chargée.
- **Pièces de maquette** du moteur : `tools/models/make_blockout.py` (deux modèles de boîte, préfabriqués `prefabs/blockout/` : sol, dalle de 8 m, mur, pilier, caisse, marche, chacun avec sa boîte de collision).
- **Scène et mesure** : « Level streaming » du bac à sable (`--level-stream`, `--level CLÉ` pour un niveau des assets) : un niveau généré de 512 x 512 m (11 262 instances, 289 morceaux de 32 m, 289 tuiles de navigation, préparé en mémoire en 13 s) traversé par le héros à 12 m/s : au plus **20 morceaux** et 563 instances en mémoire à la fois (le niveau entier n'y est jamais), le héros n'attend jamais son morceau, le chargement prend **1,4 ms** du fil principal au pire par mise à jour ; mesure `streaming` de `measure.py` : p99 **1,05 ms**, image la plus longue 22,8 ms, **aucune au-delà de 33 ms** (validation).
- **Tests** : `tests/test_level.cpp` (3 cas : document écrit et relu, identifiants stables, erreurs refusées ; migration d'une carte v2 et état de partie ; préparation, fichiers relus et abîmés refusés, chemin à travers les morceaux par le trou du mur et coupé quand le morceau du trou est retiré). 522 tests.
- **Restes** : la **tranche passe en v3** à la partie 11 (son niveau construit dans l'éditeur avec les assets de l'ARPG ; la migration en maquette montre seulement que la carte se convertit) ; le **sol peint fondu** entre deux sols est dans le format (grille) mais le fondu au rendu viendra avec les matériaux du jalon 9 bis ; la préparation des niveaux n'est pas incrémentale (refaite à chaque `cook`).

---

## 9. Image parallèle

### But

Que l'image soit **parallèle par construction** (décision de la revue) : un **graphe de tâches** construit sur le système de tâches existant, la préparation du rendu et l'échantillonnage des poses en parallèle, et les systèmes de logique parallèles là où l'ordre des résultats reste fixe.

### Tâches

- [ ] **Graphe de tâches** : tâches avec dépendances, déclarées par image ; le fil principal y participe ; profilage par tâche (Tracy et le profiler maison).
- [ ] **Rendu** : collecte du monde, culling, tri et construction des lots (`MeshBatcher`), ombres par lumière, en parallèle ; l'enregistrement des commandes GPU reste sur le fil du rendu (SDL_GPU *(à vérifier : enregistrement multi-fil)*).
- [ ] **Animation** : échantillonnage et mélange des poses de tous les personnages en parallèle (ozz est sans état partagé).
- [ ] **Logique** : systèmes indépendants en parallèle (déplacement des agents par lots, requêtes), **résultats appliqués dans un ordre fixe** (déterminisme) ; les systèmes qui modifient le monde restent séquentiels ou passent par des tampons de commandes.
- [ ] **Simulation et rendu décalés** (le rendu de l'image N pendant la logique de N+1), si les mesures le justifient sur le Mac M3 *(décision à prendre avec les mesures)*.
- [ ] **ThreadSanitizer** (TSan) sur les tests et la tranche, ajouté au verifier sous macOS et Linux *(à vérifier : TSan n'existe pas sous MSVC)*.

### Questions à se poser

- **Qui découpe le travail ?** Le moteur (chaque système donne ses tâches) ou le jeu ? Recommandation : **le moteur pour ses systèmes**, avec une API pour que le jeu ajoute les siens au graphe.

### Pièges connus

- Le **déterminisme** : une réduction parallèle (somme de flottants) donne un résultat différent selon l'ordre. Toute réduction de la logique se fait dans un ordre fixe.
- Les **accès concurrents** à l'ECS (EnTT n'est pas sûr entre fils pour l'écriture *(à vérifier selon les vues)*) : partitionner les entités, ou des vues en lecture seule.
- Le parallélisme qui ne gagne rien sur peu de cœurs : mesurer sur le Mac M3 (8 cœurs *(à vérifier : 4 performance + 4 efficacité)*).

### Validation

- Scène de mesure « combat » : temps du fil principal réduit d'un facteur mesuré ; captures de référence et test d'exactitude inchangés ; TSan propre.

### Implémentation réalisée (partie 9)

Fait le 2026-10-18 (en autonomie).

- **Priorités et graphe de tâches** (`jobs.hpp`) : deux files, `JobPriority::Frame` (le travail de l'image, pris en premier) et `Background` (chargements, préparations) ; `parallel_for()` passe en `Frame` ; `wait()` n'exécute une tâche d'arrière-plan qu'en attendant une tâche d'arrière-plan (une image n'attend jamais un décodage de texture qu'elle n'a pas demandé). `TaskGraph` : `add(nom, travail, après)` (dépendances vers des tâches ajoutées avant), `run()` (le fil principal y participe en attendant ; la dernière dépendance finie lance la suivante), durée de chaque tâche (`milliseconds()`), une **zone Tracy par tâche** sur le fil qui l'exécute ; une exception arrête les tâches pas encore commencées et revient à `run()`. Le graphe se garde d'une image à l'autre (`clear()` puis `add()`) ; un jeu peut y mettre les siens.
- **Rendu** (`MeshRenderer::prepare`) : ce qui touche au GPU d'abord sur le fil principal (carte d'ombre du soleil, atlas des ombres ponctuelles, choix des lumières), puis un graphe : niveaux de détail (`parallel_for` par lots de 256), lots de la scène et lots du soleil (en même temps, après les niveaux de détail), signature de chaque lumière ponctuelle (une tâche par lumière) ; enfin les faces à redessiner construites en parallèle. Chaque tâche n'écrit que ses propres sorties : le résultat est celui de l'exécution l'une après l'autre. **L'enregistrement des commandes GPU reste sur le fil principal** (décision : 0,9 ms dans le combat ; SDL_GPU permet plusieurs tampons de commandes mais le gain ne justifie pas la complexité aujourd'hui). La collecte du monde reste séquentielle (elle écrit dans l'ECS : caches, poses).
- **Animation** (`World::collect`) : une pré-passe trouve les modèles animés et lesquels sont en vue, crée leurs `AnimationPose` sur le fil principal, puis **échantillonne et mélange toutes les poses en parallèle** (chaque entité a son échantillonneur, chaque tâche sa requête ; squelettes et clips en lecture seule). Un parent échantillonné pour un enfant sur ses os n'est pas refait.
- **Logique** (`move_movers`, grille et 3D) : le calcul de chaque agent en parallèle (lots de 32 ; un agent n'écrit que son `Mover`), les `Transform` écrits ensuite **dans l'ordre des identifiants** (leurs signaux). En 3D, l'orientation que lit l'évitement des voisins est celle **du début du tick**, écrite après tous les calculs : le résultat ne dépend plus de l'ordre (auparavant un agent voyait l'orientation déjà tournée des agents traités avant lui). Les requêtes utilisées en parallèle sont en lecture seule : scène de collision, `closestPointOnPoly` / `findNearestPoly` de Detour (sans le tas de nœuds), table spatiale (`candidates()`). `plan_paths()` reste séquentiel (le tas de nœuds de Detour), la séparation aussi (elle pousse des paires).
- **Simulation et rendu décalés** : **pas faits** (décision avec les mesures) : le fil principal du combat tient en 1,9 ms pour 16,7 ms ; le décalage coûterait une image de latence et une copie de l'état pour rien. À reconsidérer si une mesure sur le Mac M3 le demande.
- **ThreadSanitizer** : option `MOTEUR_TSAN` (Clang seulement, refusée avec ASan ou MSVC) et preset `macos-tsan` ; `verify.py` construit et passe les tests sous TSan sous macOS (`--no-tsan` pour sauter). **Pas encore lancé** : à faire sur le Mac (MSVC n'a pas de TSan).
- **Mesures** (Release, Windows, scène « combat », 30 s, 1920 x 1080) : fil principal **3,13 → 1,89 ms** en moyenne (−40 %), p99 4,29 → 2,49 ms, image la plus longue 8,3 → 3,9 ms ; préparation des maillages **1,66 → 0,57 ms** ; soumission 2,14 → 0,98 ms. Captures de référence, rejeu et test d'exactitude **inchangés**.
- **Tests** : `tests/test_jobs.cpp` (3 cas de plus : graphe en losange exécuté trois fois avec 0, 1 et 4 fils ; exception qui arrête la suite ; tâches de l'image avant celles d'arrière-plan). 525 tests.
- **Restes** : TSan à passer sur le Mac ; mesurer le gain sur les 4 + 4 cœurs du M3.

---

## 10. Éditeur de niveaux et bibliothèque d'assets (socle)

### But

Que les **vrais assets** du jeu (pièces de kit, sols, accessoires comme le lampadaire, lumières) soient **disponibles dans l'éditeur** et que l'éditeur montre **ce que le jeu montre** (question de l'utilisateur, 2026-10-17 : aujourd'hui l'éditeur de cartes ne dessine ni les pavés ni les lampadaires, et « Essayer » lance une autre scène que la tranche). C'est le socle de l'éditeur de production du jalon 11.

### Tâches

- [ ] **Bibliothèque d'assets** : navigateur des assets par dossier et par type (modèles, préfabriqués, matériaux, sols), avec **vignettes** rendues par le moteur ; recherche ; glisser un asset dans le niveau.
- [ ] **Placement en 3D** : préfabriqués et pièces de kit posés, déplacés, tournés, mis à l'échelle (poignées de manipulation), aimantation à la grille du kit et aux surfaces, duplication, sélection multiple, annuler / refaire (celui du jalon 7).
- [ ] **Sols** : peinture de **matériaux de sol** sur les surfaces marchables ou sur une grille de sol (les pavés A et B choisis par l'auteur, plus un placement automatique en dur comme aujourd'hui dans la tranche) ; mélange entre sols *(à cadrer : par cellule ou par masque peint)*.
- [ ] **Lumières et préfabriqués lumineux** : le lampadaire avec sa lumière, réglages (couleur, intensité, portée, ombres, `shadow_near`) dans l'inspecteur générique (partie 3).
- [ ] **Même rendu que le jeu** : l'éditeur dessine avec le code du jeu (matériaux, lumières, ombres, brouillard coupé), pas avec ses propres primitives ; mode « jour / nuit » pour juger l'éclairage.
- [ ] **Navigation et collisions visibles** : le maillage de navigation régénéré en direct (partie 6), les zones non marchables.
- [ ] **« Essayer »** lance **le jeu** sur le niveau ouvert (la tranche pour son niveau), au point choisi, et revient à l'éditeur.
- [ ] Le niveau écrit au **format v3** (partie 8) ; l'ancienne carte de la tranche migrée et rouverte.

### Questions à se poser

- **Faire évoluer l'éditeur de cartes du jalon 7, ou un nouvel éditeur ?** Recommandation : **faire évoluer le programme `editeur`** (sa structure de documents, son annuler, son autotest) vers l'éditeur de niveaux 3D, en remplaçant son rendu par celui du jeu ; il deviendra l'éditeur de production du jalon 11.
- **Interface de l'éditeur : Dear ImGui ou le système d'interface maison (partie 6 du [jalon 9 bis](JALON_9BIS_PRESENTATION.md)) ?** Les studios utilisent souvent leur propre interface d'outils, et beaucoup Dear ImGui pour les outils internes *(à vérifier)*. Recommandation : **Dear ImGui** (avec ses fenêtres ancrables) pour l'éditeur : c'est un outil, absent du jeu livré, et l'interface maison reste concentrée sur le jeu. **Décision**.
  **Décidé (2026-10-17)** : **Dear ImGui.**
- **Le sol : une donnée de la carte ou du code ?** (question du 2026-10-17). Recommandation : **une donnée du niveau** : matériau de sol par cellule ou par zone peinte, choisi par l'auteur ; le placement en dur de la tranche disparaît.
  **Décidé (2026-10-17)** : **peint par l'auteur dans l'éditeur** (donnée du niveau, fondu entre sols).

### Pièges connus

- Un éditeur qui **diverge** du jeu (son propre rendu, ses propres règles) : l'auteur règle un niveau qui ne ressemble pas au jeu. Un seul code de rendu et de règles.
- Les vignettes rendues **à chaque ouverture** : cache sur disque, invalidé par l'empreinte de l'asset.

### Validation

- Dans l'éditeur : poser des lampadaires et peindre les pavés A et B sur un niveau de deux hauteurs, voir l'éclairage de nuit comme en jeu, régénérer la navigation, « Essayer » dans la tranche, revenir ; enregistrer, rouvrir, préparer et livrer ce niveau.

### Implémentation réalisée (partie 10)

Fait le 2026-10-18 (en autonomie).

- **Décision** : un **nouvel éditeur de niveaux** dans le programme `editeur` (`editeur --level [CLÉ]`, `--new-level`), à côté de l'éditeur de cartes du jalon 7 (gardé pour les cartes v1 et v2 tant que la tranche en dépend : partie 11) : les deux documents n'ont rien en commun (cases contre préfabriqués placés), le programme, son annuler par actions, sa copie de secours, son essai et son autotest sont repris. Aussi dans le bac à sable (DEBUG > Engine tests > Level editor).
- **Moteur** : `EditableLevel` (`level_edit.hpp` : tout changement du niveau, annuler / refaire par différences, un pas par action, les changements d'une même chose fusionnés, 500 pas ; signal `LevelChange` pour ne reconstruire que ce qui change), `Gizmo` (`gizmo.hpp` : poignées déplacer / tourner / échelle, en pixels et en monde, aimantation), `ThumbnailRenderer` et `ThumbnailCache` (`thumbnails.hpp` : vignettes rendues par un second `MeshRenderer` hors écran, mappage des tons du jeu, gardées sur disque par empreinte), `LevelLighting` + `apply_lighting()` + `night_lighting()` (l'éclairage devient une donnée du niveau, appliquée par la même fonction dans le jeu et l'éditeur), `LevelInstance::properties` (les données du jeu d'une instance, jusqu'aux morceaux préparés : MLVL / MLVC version 2) et `LevelStreamer::set_instance_hook()` (le jeu donne ses composants aux instances quand elles se créent), `Renderer::submit_pending_uploads()`, `encode_png()`.
- **Bibliothèque** : préfabriqués et modèles de tous les dossiers d'assets (le moteur, l'ARPG monté), par dossier ou par **famille de kit** (lue dans les `.kit.json` et le `catalogue.json` : la grille d'aimantation de chaque pièce), filtres (accessoires, sols, lumières, pièces de kit, modèles sans préfabriqué), recherche ; **vignettes** rendues par le moteur (96 px, deux par image au plus, entre deux images), refaites quand le fichier change ; **glisser** un asset dans la vue, ou le choisir pour l'outil Placer ; « Use as ground » (un sol à peindre), « Make its prefab » (un modèle reçoit son préfabriqué, avec sa collision du modèle).
- **Placement en 3D** : sélection au clic par le tampon d'identifiants de la partie 7 (survol souligné ; Ctrl ajoute, Maj bascule), **poignées** (T, E, R), aimantation à la grille du kit de la pièce ou de l'éditeur et aux **surfaces** (rayon contre la collision du niveau), Fin pose sur ce qui est dessous, duplication, suppression, rotation par pas ([ ]), annuler / refaire.
- **Sols** : outils Peindre et Effacer sur la grille du sol du niveau (pinceau de 1 à 6 cases, la grille grandit où l'on peint), plusieurs sols au choix (les pavés A et B quand leurs préfabriqués existent : partie 11) ; le **fondu** entre sols attend les matériaux du jalon 9 bis.
- **Lumières** : un préfabriqué lumineux se pose comme les autres ; l'**inspecteur générique** (réflexion, partie 3) montre les champs des composants du préfabriqué (lumière : couleur, intensité, portée, ombres, `shadow_near` ; halo ; modèle) et garde ce qui change comme **surcharges** de l'instance (seuls les champs changés ; revenir aux valeurs du préfabriqué) ; les propriétés de jeu en JSON.
- **Même rendu que le jeu** : le monde du niveau dessiné par le rendu du moteur (matériaux, lumières ponctuelles, ombres), l'éclairage du niveau par `apply_lighting()`, **vue de nuit** (N).
- **Navigation et collisions visibles** : la collision de chaque instance (`prefab_collision`) et du sol, une scène reconstruite après chaque changement ; le maillage de navigation **reconstruit en arrière-plan** (tâche, priorité arrière-plan) après une pause de 0,4 s dans l'édition, élagué depuis les points ; murs et obstacles en couleur.
- **« Essayer »** (F5 depuis `start`, F6 sous le pointeur) : le niveau tel qu'il est, préparé en mémoire (`bake_level`), chargé par morceaux par `LevelStreamer` comme en jeu, éclairé comme en jeu. **Depuis la partie 11, c'est la tranche elle-même** qui le joue (son héros, ses créatures venues des apparitions du niveau, de jour ou de nuit) ; Échap revient à l'éditeur.
- **Enregistrement** en v3 (écriture atomique dans `assets/levels` des sources, changement par un autre programme remarqué, copie de secours dans les préférences) ; la carte de la tranche **migrée à nouveau** (les propriétés de ses 3 000 objets en `properties`, plus en fausses surcharges) et rouverte dans l'éditeur : 4 228 instances et le sol construits en **125 ms**.
- **Autotest** (`editeur --level --self-test`, dans `check_references.py` donc dans `verify.py`) : sol peint, une marche (deuxième hauteur), le lampadaire de l'ARPG posé (sa lumière dans le monde), déplacé de 2 m aux poignées avec aimantation, annuler, refaire, sa lumière surchargée, sa vignette, la navigation construite (13 ms), enregistré, rouvert à l'identique, préparé (morceaux et tuiles de navigation), essayé (le héros debout), retour.
- **Tests** : `tests/test_level_edit.cpp` (5 cas : historique et fusion, actions sans effet, réglages et éclairage relus, grille du sol qui grandit, poignées ; propriétés gardées par le niveau et ses morceaux). 530 tests. Références inchangées.
- **Restes** : les **fenêtres ancrables** de Dear ImGui demandent la variante « docking » du paquet vcpkg (un téléchargement : à décider ; les panneaux sont disposés à gauche et à droite en attendant) ; la sélection par rectangle et le copier-coller entre niveaux (éditeur de production, jalon 11) ; les volumes (`nav_area`, déclencheurs) se lisent et se préparent mais ne se dessinent pas encore dans l'éditeur ; le fichier vide `ARPG/assets/models/props/mobilier_rue/facades_gothiques.kit.json` est ignoré (avertissement).

---

## 11. La tranche dans un niveau 3D

### But

Prouver le jalon : la tranche jouable du jalon 8 dans un niveau 3D, sur tout ce qui précède, livrée et vérifiée.

### Contenu

- **Niveau 3D** construit dans l'éditeur (partie 10) avec les assets de l'ARPG : au moins deux hauteurs reliées par un escalier, des briques de kit, des pavés peints, des lampadaires et des braseros en préfabriqués ; zones chargées par morceaux.
- **Monstres** qui trouvent leur chemin sur le maillage de navigation, voient le héros selon les lignes de vue 3D, sont sélectionnés au clic sur leur silhouette.
- **Données** lues par la réflexion, assets par identifiant, tout préparé dans le jeu livré.
- **Rejeux**, test d'exactitude, captures de référence (nouvelles, documentées) ; scènes de mesure refaites et budgets tenus sur le Mac M3.
- L'interface, le texte et les sons restent ceux du jalon 8 : le jalon 9 bis les refait.

### Tâches

- [ ] Refaire la tranche (`apps/tranche`) et son niveau ; mettre à jour le rejeu de référence (`tools/input/make_slice_replay.py`) et les captures.
- [ ] Mesures (`tools/measures/measure.py`) sur le PC et le Mac M3, comparées au jalon 8.
- [ ] Paquet installé et joué (`check_package.py`) sur les deux OS.
- [ ] Guide, formats, référence et [FICHIERS_DU_PROJET.md](FICHIERS_DU_PROJET.md) mis à jour.

### Validation

- Voir les critères de fin de jalon.

---

### Implémentation réalisée (partie 11)

Fait le 2026-10-18 (en autonomie), sous Windows ; le Mac reste à faire (voir les restes).

- **Le niveau** (`assets/levels/tranche.level.json`) : une rue gothique de 12 m entre deux rangées de maisons du kit de l'ARPG (rez-de-chaussée, étage, corniche, toits ; portes, fenêtres, fenêtres éclairées, balcons, lierre), fermée au sud par une grille et un portail en fer forgé, traversée par une arche gothique, avec lampadaires, caisses, tonneaux, une barricade, une charrette renversée ; une place avec braséros ; le **parvis** de la cathédrale (le décor de fond du kit) **0,84 m plus haut**, entouré de grilles, où l'on monte par **trois escaliers de pierre** : la deuxième hauteur ; les **pavés A et B peints** partout (cases de 8 m) ; **40 apparitions** de squelettes (une instance de `prefabs/tranche/apparition.prefab.json` dont les **propriétés** disent qui vient : la table des personnages), le point `start` et le tour de la mesure (`tour_01` à `tour_12`) ; l'éclairage du niveau ; six calques. 618 instances. Son premier état vient de `tools/levels/make_slice_level.py` (le même à chaque fois, pour refaire les références) ; il s'ouvre, se modifie et s'enregistre dans l'éditeur, qui le **joue** (« Essayer »).
- **Collisions des accessoires de l'ARPG** (dans le pipeline de l'ARPG, à la source) : `ARPG/assets_source/commun/collisions.py` donne à chaque accessoire du catalogue sa `static_collision` d'après ses bornes : boîte « mur » (murs et angles des façades : bloque aussi la vue), boîte « obstacle » (caisses, tonneaux, grilles, braséros : la vue passe), dessus marchable (trottoirs), **marches** en gradins (l'escalier : ses fines contremarches ne donnaient pas de volée praticable à la navigation), triangles du modèle (l'arche), rien (lierre, fenêtres, balcons, corniches, toits) ; il fait les préfabriqués des **sols de pavés** (la dalle décalée d'une demi-case : un sol se pose au coin de sa case ; une boîte marchable). `prefab_moteur.write_prefab()` garde cette collision quand un modèle est exporté de nouveau. Le fichier vide `mobilier_rue/facades_gothiques.kit.json` est laissé tel quel (l'éditeur l'ignore) ; la vraie source des grilles et familles est `models/props/catalogue.json`.
- **Moteur** : `StreamingSettings::ordered` (les morceaux faits dans l'ordre de leurs coordonnées, quel que soit l'ordre des lectures en arrière-plan : mêmes entités, mêmes tuiles de navigation, mêmes appels du crochet d'instance à chaque exécution : rejeux, sauvegardes) ; l'**éclairage** dans l'en-tête préparé (`LevelHeader::lighting`) ; le **cache des niveaux préparés** en développement (`LevelStreamer::open_any()` : la forme préparée du cache d'import tant que le niveau, ses préfabriqués et leurs bases, ses modèles et leurs réglages d'import n'ont pas changé, sinon préparée et écrite pour la fois suivante, comme les modèles de la partie 4 ; identique, partie pour partie, à la préparation en mémoire).
- **La tranche** (`apps/tranche/demo3d.cpp`, mode niveau ; `--slice-map` garde la carte des jalons 6 et 7 pour comparer) : le niveau chargé comme **zone fermée** (tout, dans l'ordre) ; le **crochet d'instance** relève les apparitions (créées ensuite par identifiant), le feu des braséros, fait s'effacer les façades devant le héros (`Material::fades`) et ôte l'ombre des verres et flammes ; la logique en **3D** : chemins et déplacements sur le maillage de navigation et la scène de collision (contrôleur de personnage, marches, chutes), un **champ de flux sur le maillage** vers le héros pour la chasse, **lignes de vue 3D** (yeux à 1,5 m : les murs cachent, les caisses non), coups seulement au même étage ; le **brouillard par étage** (`floor_sight_grid`, un par demi-mètre de hauteur, faits au chargement) ; les créatures **choisies au clic par rayons** sur leur capsule (`Selectable`, `pick()`), celle sous le pointeur soulignée (tampon d'identifiants, partie 7) ; la caméra dans l'axe de la rue ; la **sauvegarde** garde le niveau, son état (`LevelState`) et la cible du champ de flux ; les mesures : « combat » au même endroit, « traversée » sur le tour du niveau (le héros frappe ce qui est à portée en passant) ; les commandes `tp` et `spawn` sur le maillage. « Essayer » de l'éditeur la lance sur le niveau ouvert (`Options::baked_level`, `start`, `night`).
- **Références** : la tranche **98609fd4a8ed** (la rue, le chevalier aux prises avec les premiers squelettes ; le rejeu refait par `tools/input/make_slice_replay.py` : remonter la rue, frapper, pause, reprendre, frapper) et l'exactitude **91e6eca81dc9** (A, B et C identiques aux ticks 120 et 250 ; identiques aussi que le niveau soit préparé en mémoire ou relu du cache) ; 2D, démo 3D et son rejeu **inchangés**. Le départ et l'angle de la caméra ont été réglés pour que rien ne cache le héros au début (la grille au sud plutôt qu'un mur de 8,8 m).
- **Mesures** (Release, Windows, 1920 x 1080) : combat : fil principal **2,1 ms**, p99 3,6 ms, GPU 4,4 ms ; traversée : **1,7 ms**, p99 3,1 ms, GPU 5,7 ms ; **aucune image au-delà de 33 ms** ; chargement de la tranche **1,5 s** au pire de 10 cycles (jalon 8 : 0,25 s ; la différence : les 40 modèles du kit et leurs textures ; la première exécution de développement après un changement prépare le niveau en 4,4 s) ; processus 1,0 Go, GPU 450 Mo (budgets 2 Go et 1,5 Go).
- **Paquet** : le manifeste livre le niveau (préparé en son en-tête et ses morceaux) au lieu de la carte, une archive `niveaux`, les préfabriqués et les pièces de maquette dans `decor` (créditées). La préparation suit désormais aussi un champ d'asset écrit par son chemin seul (`"model": "models/x.glb"`, comme les pièces de maquette). `check_package.py` : installé, **joué sur le rejeu, même capture que la référence (98609fd4a8ed)**, désinstallé, le dossier du joueur gardé.
- **Restes** : tout ce qui demande le **Mac** (le vérificateur, ThreadSanitizer, les mesures sur le M3, le `.app` et le `.dmg`) ; le maillage de navigation ne connaît pas encore les volumes `nav_area` dans l'aperçu de l'éditeur (ils sont pris à la préparation).

## 12. Critères de fin de jalon

Le jalon est terminé quand **tout** ce qui suit est vrai :

- [x] La **politique d'erreur** est écrite et respectée ; la décision de stockage des assets est consignée (repoussée).
- [x] Les **types** du moteur et de la tranche sont décrits une fois (réflexion) : tables, inspecteur, sauvegarde et schéma en découlent ; les **préfabriqués** existent, imbriqués et avec variantes (le lampadaire en est un).
- [x] Les assets ont des **identifiants stables** ; renommer ou déplacer ne casse rien ; le jeu livré ne lit que des formats **préparés** (maillages avec niveaux de détail, animations, niveaux, effets).
- [x] Le **monde est en 3D** pour la logique : collisions, navigation sur plusieurs niveaux (Recast & Detour derrière l'API du moteur), lignes de vue, brouillard et sélection à la souris en 3D ; les niveaux sont au **format v3** et chargés par morceaux.
- [ ] L'**image est parallèle** ; captures et test d'exactitude inchangés par le parallélisme ; ThreadSanitizer propre là où il existe. *(fait sous Windows ; TSan à passer sur le Mac)*
- [x] L'**éditeur de niveaux** assemble les briques de kit, place les préfabriqués, peint les sols, montre le rendu du jeu et lance la tranche.
- [ ] La **tranche** dans son niveau 3D se livre, s'installe et se joue sur Windows et macOS ; le verifier passe sur les deux ; les budgets tiennent sur le Mac M3. *(Windows fait ; macOS à faire)*
- [x] Les nouvelles bibliothèques (Recast & Detour) sont dans les crédits et leurs licences dans les paquets.
- [x] La documentation (guide, formats, référence, fichiers du projet) est à jour ; les décisions de ce document sont consignées.

---

## 13. Risques principaux

| Risque | Impact | Parade |
|---|---|---|
| Le monde 3D (parties 5 à 8) touche presque tout le code de logique | Régressions partout, déterminisme cassé | Migration système par système, rejeux et test d'exactitude à chaque étape ; la grille gardée comme vue tant que tout n'est pas passé |
| Recast & Detour non déterministes dans notre usage (ordre des agents, flottants) | Rejeux divergents | Ordre fixe des requêtes et des agents, tests de rejeu sur des niveaux à plusieurs hauteurs ; évitement local maison si DetourCrowd ne convient pas |
| La réflexion change la lecture des données et des sauvegardes | Sauvegardes des joueurs illisibles | Comparaison lecture à la main / par réflexion pendant la migration ; migrations de version testées |
| Le parallélisme introduit des courses et du non-déterminisme | Bugs rares, rejeux divergents | Ordre fixe des résultats, TSan, rejeux à chaque étape |
| Les assets restent sur une seule machine (stockage repoussé) | Perte d'assets si le disque lâche | Risque accepté par l'utilisateur ; au minimum, une copie régulière sur un autre disque est conseillée |
| Les assets de l'ARPG arrivent dans un format que l'import ne connaît pas encore | Assets bloqués | Le pipeline Blender et l'import (partie 4) avancent ensemble ; `fetch_arpg_models.py` reste jusque-là |

---

## Décisions à consigner

Questionnaire du 2026-10-17.

| Sujet | Décision | Raison |
|---|---|---|
| Découpage | **Deux jalons** : 9 « monde et données », puis [9 bis « présentation »](JALON_9BIS_PRESENTATION.md) | Choix de l'utilisateur (2026-10-17) |
| Déterminisme | **Même machine, porte ouverte** : les règles qui gardent possible le même résultat Windows / macOS sont appliquées (pas de trigonométrie dans la logique, ordre fixe, pas de fast-math, FMA contrôlé), sans en faire une exigence vérifiée | Recommandation validée par l'utilisateur (2026-10-17) |
| T6 Stockage des assets | **Tout en local** sur la machine de l'utilisateur, à régler bien plus tard (la condition historique + sauvegarde hors de la machine est repoussée aussi) | Choix de l'utilisateur (2026-10-17) |
| Ce qui se versionne (quand le stockage sera décidé) | **Sources et exports** | Recommandation validée par l'utilisateur (2026-10-17) |
| Réflexion | **Description `constexpr`** à côté du type, avec quelques macros d'aide ; pas de générateur de code | Recommandation validée par l'utilisateur (2026-10-17) |
| Préfabriqués | **Imbrication et variantes dès le format** | Recommandation validée par l'utilisateur (2026-10-17) |
| Identifiants d'assets | **UUID dans un `.meta`** voisin, avec les réglages d'import | Recommandation validée par l'utilisateur (2026-10-17) |
| Géométrie des niveaux | **Briques assemblées** (kits modulaires faits dans Blender, assemblés dans l'éditeur) ; une zone unique peut être modélisée en entier | Recommandation validée par l'utilisateur (2026-10-17), après reformulation |
| Navigation | **Recast & Detour** (bibliothèque), derrière l'API de navigation du moteur | Choix de l'utilisateur (2026-10-17), contre la recommandation « maison » |
| Morceaux de niveau | **Grille régulière, zones fermées d'abord** ; chargement continu possible avec le même format | Recommandation validée par l'utilisateur (2026-10-17) |
| Interface de l'éditeur | **Dear ImGui** | Recommandation validée par l'utilisateur (2026-10-17) |
| Sol des niveaux | **Peint par l'auteur dans l'éditeur** (donnée du niveau, fondu entre sols) | Recommandation validée par l'utilisateur (2026-10-17) |
| Texte, localisation, format des écrans | Voir le [jalon 9 bis](JALON_9BIS_PRESENTATION.md#décisions-à-consigner) | |

