# Jalon 7 - Outils et données

Retour à la [roadmap générale](ROADMAP.md). Jalon précédent : [Jalon 6 - Monde et déplacement](JALON_6_MONDE_ET_DEPLACEMENT.md). Description des fichiers existants : [Fichiers du projet](FICHIERS_DU_PROJET.md).

## Objectif

Donner au jeu les moyens d'être **décrit par des données** et **travaillé sans recompiler** : des **tables de données** en JSON (monstres, objets, compétences, effets de statut) lues, vérifiées et rechargées à chaud ; la **sauvegarde** et le **chargement** de l'état d'une partie, exacts au tick près ; des **outils de debug** qui manquent encore (une **console** de commandes et de variables, un **profiler** des systèmes, des **overlays** et un **journal** dans un fichier) ; et un **éditeur de cartes** minimal, qui peint les tuiles, place les points et les objets, et écrit le format de carte du jalon 6.

À la fin du jalon, la **tranche jouable** doit montrer : des créatures dont les **caractéristiques viennent d'un fichier** (modifié pendant le jeu, elles changent aussitôt) ; une **sauvegarde** depuis le menu de pause et un **chargement** depuis l'écran titre qui reprennent la partie **exactement** où elle était (même image après N ticks qu'une partie jamais interrompue) ; une **console** (touche ²) où l'on tape `god`, `spawn squelette 5`, `tp 40 12`, `fog off` ; un **profiler** qui montre où passent les millisecondes de chaque tick ; et une carte de la tranche **retouchée dans l'éditeur** (une salle ajoutée, des braseros déplacés) puis rejouée sans redémarrer.

**Niveau d'exigence** : ce moteur sert un **jeu livrable**, pas un prototype (rappel de l'utilisateur, 2026-10-03). Chaque recommandation de ce document vise la solution de **production** (celle des grands ARPG quand on la connaît), même si elle coûte plus de travail ; rien n'est repoussé parce que c'est difficile, seulement pour une raison réelle.

**Pourquoi maintenant ?** Les jalons 4 à 6 ont posé les systèmes (assets, ECS, entrées, audio, animation, monde) ; leur réglage se fait encore dans le code ou à la main dans des JSON sans vérification. Côté jeu, la conception des stats ([CONCEPTION_STATS.md](../../ARPG/ARPG_doc/CONCEPTION_STATS.md)) décrit déjà effets et modificateurs **comme des données JSON**, et la bibliothèque `gameplay` prévue en aura besoin. La sauvegarde, elle, ne s'ajoute pas facilement tard : chaque composant doit savoir s'écrire, mieux vaut le décider pendant qu'ils sont encore peu nombreux.

## Prérequis

- Le [jalon 6](JALON_6_MONDE_ET_DEPLACEMENT.md) est terminé (sous Windows ; les particules douces repoussées et les vérifications Mac restent dans sa validation et dans [TEST_MAC.md](TEST_MAC.md)).
- Relire la partie *Données* de [CONCEPTION_STATS.md](../../ARPG/ARPG_doc/CONCEPTION_STATS.md) (format des effets et des modificateurs) : les tables de ce jalon doivent pouvoir les porter, sans que le moteur sache ce qu'est une « résistance ».
- Pour le profiler : la documentation de **Tracy** (si la partie 4 le retient) ; sinon rien.
- Rien d'autre : nlohmann/json, EnTT et Dear ImGui sont déjà là.

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
2. [Tables de données en JSON](#2-tables-de-données-en-json)
3. [Console, variables et journal](#3-console-variables-et-journal)
4. [Profiler et overlays](#4-profiler-et-overlays)
5. [Sauvegarde : fichiers, versions et emplacements](#5-sauvegarde--fichiers-versions-et-emplacements)
6. [Sauvegarde : l'état du monde](#6-sauvegarde--létat-du-monde)
7. [Éditeur de cartes : tuiles et points](#7-éditeur-de-cartes--tuiles-et-points)
8. [Éditeur de cartes : objets posés](#8-éditeur-de-cartes--objets-posés)
9. [Reprises : graphe d'animation en données et particules douces](#9-reprises--graphe-danimation-en-données-et-particules-douces)
10. [Performances et robustesse](#10-performances-et-robustesse)
11. [Outils et données dans la tranche jouable](#11-outils-et-données-dans-la-tranche-jouable)
12. [Critères de fin de jalon](#12-critères-de-fin-de-jalon)
13. [Risques principaux](#13-risques-principaux)
14. [Décisions à consigner](#décisions-à-consigner)

---

## 1. Vue d'ensemble et ordre de travail

### Où on part

Après le jalon 6 :

- **Données** : chaque type de fichier a son lecteur écrit à la main (`AnimationSet`, `MapData`, `ParticleEffect`, liaisons de touches, `credits.json`), avec des messages d'erreur qui nomment le fichier et souvent la ligne fautive. Tous passent par le **gestionnaire d'assets** (cache, poignées partagées, **rechargement à chaud** avec efsw, remplacement en place). Il n'existe pas de « table » générique (une liste de monstres, d'objets), ni de **référence** d'une donnée vers une autre vérifiée au chargement.
- **Debug** : `DebugTools` a six fenêtres (inspecteur d'entités, assets, entrées, audio, états, animation), retenues dans `imgui.ini` ; les scènes de test ont leurs panneaux ; `--report` imprime des moyennes (CPU, phases, passes) ; `--gpu-timing` mesure les passes du GPU ; `FrameStats` garde des échantillons et donne moyenne, maximum, percentiles. Les journaux passent par `SDL_Log`, à l'écran du terminal seulement. **Pas de console**, pas de mesure par système hors des panneaux écrits à la main (scène « Monde »).
- **État** : la logique est **déterministe** (pas fixe, entiers là où cela compte, ordres triés) ; les rejeux d'entrées et les captures de référence le vérifient. L'état d'une partie est éparpillé : composants EnTT (`Transform`, `Animator`, `Mover`, `Collider`, ceux du jeu comme `Health`, `Hunter`), objets des scènes (`ExploredMap`, `FlowField`, générateurs aléatoires), pile d'états. **Rien ne s'écrit sur disque**, à part les réglages (liaisons de touches, audio, `imgui.ini`) dans le dossier des préférences.
- **Cartes** : `MapData` (JSON + carte en caractères), `to_ascii()` qui réécrit exactement les lignes, `NavGrid` avec versions, `TileMap::set` ; la scène « Monde » pose et retire des murs au clic droit. Le décor non-tuile (arbres, caisses, tonneaux de la tranche) est **tiré au hasard** dans le code avec une graine, pas décrit dans la carte.

Les principes restent ceux de la roadmap : le moteur ne connaît rien de l'ARPG. Il fournit des **tables**, des **références**, des **commandes**, des **mesures**, un **format de sauvegarde** et un **éditeur** ; le jeu y met des monstres, des objets et des sorts.

### Dépendances entre les parties

```
3. Console, variables, journal ----+
            |                      |
4. Profiler et overlays            |   (outils utilisés par toutes les autres parties)
                                   |
2. Tables de données --------------+------------------+
            |                                         |
5. Sauvegarde : fichiers                    7. Éditeur : tuiles et points
            |                                         |
6. Sauvegarde : état du monde               8. Éditeur : objets posés
            |                                         |
            +-------------------+---------------------+
                                |
        9. Reprises : graphe d'animation, particules douces
                                |
                     10. Performances et robustesse
                                |
                     11. Tranche jouable
```

- La **console** et le **journal** viennent tôt : ils servent à tout le reste (recharger une table, sauvegarder, ouvrir l'éditeur, lire une erreur après coup).
- Les **tables** précèdent la sauvegarde : une sauvegarde écrit des **identifiants de données** (« squelette_guerrier »), pas des copies.
- La **sauvegarde** se fait en deux temps : le fichier (versions, écriture sûre, emplacements), puis l'état du monde (composants, entités, références entre elles).
- L'**éditeur** peut avancer en parallèle de la sauvegarde ; les **objets posés** (partie 8) ont besoin des tables (« place un `squelette_guerrier` ici »).

### Ce qui peut se faire en pause du moteur

- **Tables** : lecture, vérification (champs, types, bornes, références), messages d'erreur, sur des fichiers de test, en ligne de commande.
- **Sauvegarde** : format, versions et migrations, écriture sûre, sérialisation de composants, aller-retour (écrire, relire, comparer).
- **Console** : analyse des lignes de commande (mots, guillemets, nombres), complétion, historique.
- **Côté ARPG** : les **schémas** des tables (monstres, objets, compétences, effets) à partir de CONCEPTION_STATS.md, et le calculateur en ligne de commande de la bibliothèque `gameplay`, qui lira les mêmes fichiers.

### Estimation indicative

Pour un dev solo à temps partiel. À prendre comme un ordre de grandeur, pas comme un engagement.

| Partie | Estimation |
|---|---|
| 2. Tables de données (et leur forme binaire) | 3 semaines |
| 3. Console, variables, journal | 1 à 2 semaines |
| 4. Profiler, Tracy et overlays | 2 semaines |
| 5. Sauvegarde : fichiers | 1 semaine |
| 6. Sauvegarde : état du monde | 2 à 3 semaines |
| 7. Éditeur : tuiles, points, morceaux | 3 semaines |
| 8. Éditeur : objets posés | 2 à 3 semaines |
| 9. Graphe d'animation en données, particules douces | 2 à 3 semaines |
| 10. Performances et robustesse | 1 semaine |
| 11. Tranche jouable | 1 à 2 semaines |
| **Total** | **environ 4 à 5 mois** |

### Liens avec les autres jalons

- **Animation** (jalon 5) : la décision « graphe d'animation en code, paramètres en JSON, à revoir au jalon 7 » est revue en partie 9 : le graphe passe **en données**. La sauvegarde écrit l'état de l'`Animator` (clips, temps en ticks), jamais la pose : c'était prévu pour cela.
- **Monde** (jalon 6) : les **particules douces**, repoussées au jalon 6, sont faites en partie 9 ; l'éditeur écrit le format de `MapData` (une version 2 ajoute les objets) ; la sauvegarde écrit les cases explorées (`ExploredMap`) et les cases changées de la grille (portes ouvertes, murs détruits).
- **Consolidation** (jalon 8) : le **packaging** emportera la **forme binaire** des tables et des cartes (produite par l'outil de la partie 2) ; le **profiler** de ce jalon servira à l'optimisation ; la documentation de l'API reprendra les formats de fichiers.
- **Côté ARPG** : monstres, objets, compétences, effets de statut, tables de butin et génération de zones seront des **tables** ; la bibliothèque `gameplay` (sans moteur) lira les mêmes fichiers. Le moteur ne fixe pas leurs champs, il fournit le moyen de les décrire et de les vérifier.

### Questions générales

- **Le moteur doit-il connaître les champs des tables ?** Non : il fournit un **lecteur de tables** générique (une table = des lignes identifiées par un nom, chacune un objet JSON) et des **outils de lecture typée** (lire un entier borné, une liste, une référence vers une autre table, avec un message qui nomme le fichier, la ligne et le champ). Le jeu écrit pour chaque table une petite fonction qui transforme un objet JSON en sa structure C++. C'est ce que font déjà `AnimationSet` et `MapData`, en plus systématique.
- **Données ou code ?** Ce qui se **règle** (chiffres, listes, associations) va dans les données ; ce qui **décide** (une IA, une règle de calcul) reste du code. Un langage de script (Lua) n'est pas prévu : il ajouterait une seconde façon d'écrire la logique, et le déterminisme deviendrait plus difficile à garantir.
- **Une seule source de vérité** : chaque donnée a **un** fichier qui la décrit ; l'éditeur et le jeu lisent et écrivent ce même fichier (pas de format « éditeur » et de format « jeu » qui divergent).

---

## 2. Tables de données en JSON

### But

Décrire les données du jeu (monstres, objets, compétences, effets…) dans des **tables JSON** vérifiées au chargement, référencées entre elles par **identifiant**, rechargées à chaud, sans que le moteur connaisse leurs champs.

### Tâches

- [x] **Format d'une table** : un fichier JSON versionné, un objet `rows` dont chaque clé est l'**identifiant** d'une ligne (`"squelette_guerrier": { ... }`), éventuellement un `defaults` commun et un héritage simple (`"base": "squelette"` : les champs non donnés viennent d'une autre ligne).
- [x] **Lecteur générique** (`DataTable` ou nom à choisir) : lignes dans l'ordre des identifiants (stable), `find(id)`, `contains(id)`, application des `defaults` et de l'héritage, refus des identifiants en double, des cycles d'héritage, des clés vides.
- [x] **Lecture typée** (`DataReader` ou nom à choisir) sur un objet JSON : `number(champ, min, max, défaut)`, `integer`, `boolean`, `text`, `choice(champ, {"feu", "froid", ...})`, `list`, `vec3`, `color`, `reference(champ, table)`, avec un message d'erreur qui donne **le fichier, la ligne (identifiant) et le champ** (`monstres.json : squelette_guerrier.vie : 0 est sous le minimum 1`), et un **avertissement** pour les champs inconnus (faute de frappe).
- [x] **Références entre tables** : un identifiant écrit dans une table (`"butin": "table_squelette"`) est **vérifié** quand toutes les tables sont chargées ; une référence cassée est une erreur qui nomme les deux côtés. En mémoire, une référence devient un indice ou un pointeur stable (`DataRef<T>`), pas une chaîne cherchée à chaque usage.
- [x] **Assets** : une table est un asset (rechargée à chaud, remplacée en place si elle est valide, gardée telle quelle avec l'erreur affichée sinon) ; un **registre de types de tables** côté jeu (`register_table<MonsterData>("monstres", parse_monster)`).
- [x] **Rechargement à chaud et entités vivantes** : décider ce qui change quand une table change (voir les questions) et le faire pour la tranche (les créatures prennent leurs nouvelles vitesses et leurs nouveaux points de vie maximum). *Table `personnages` de la tranche, relue à chaque tick.*
- [x] Une fenêtre DEBUG **Données** : les tables chargées, leurs lignes, une ligne affichée champ par champ (après défauts et héritage), les erreurs et avertissements du dernier chargement.
- [x] Une **commande en ligne** (`bac_a_sable --check-data`) qui charge toutes les tables, vérifie tout, imprime les erreurs et rend un code de sortie : pour les vérifier sans lancer le jeu (et plus tard en intégration continue).
- [x] **Forme binaire** (comme les `.bin` de Diablo II, les `.dat` de Path of Exile ou les assets cuits d'Unreal) : un outil (`bac_a_sable --compile-data` ou un exécutable à part) vérifie toutes les tables puis les écrit dans un fichier binaire par table (ou un paquet), avec la version du format et une empreinte des sources ; la version livrée lit **seulement** le binaire (rapide, pas d'analyse JSON, pas de vérification à refaire), la version de développement lit le JSON (rechargement à chaud). Les deux chemins donnent **exactement** les mêmes structures en mémoire (test).
- [x] Tests unitaires : défauts, héritage (y compris sur deux niveaux, cycle refusé), bornes, choix, références cassées, champs inconnus, ordre stable, rechargement, **aller-retour JSON → binaire → mêmes structures**.

### Questions à se poser

- **JSON Schema ou lecture typée en C++ ?** Options :
  - **JSON Schema** (un fichier de schéma par table, validé par une bibliothèque, par exemple `json-schema-validator` pour nlohmann, dans vcpkg *(à vérifier)*) : la description de la table est elle-même une donnée, réutilisable par un éditeur de texte (complétion dans VS Code). Mais il faut **encore** convertir le JSON en structures C++ ensuite (deux descriptions de la même table qui peuvent diverger), et les messages d'erreur des validateurs sont souvent peu lisibles.
  - **Lecture typée en C++** : la fonction qui convertit **est** la vérification ; un seul endroit à tenir à jour ; messages écrits pour nous.
  
  **Ce que font les grands jeux du genre** *(de mémoire, formats internes à vérifier)* : Diablo II décrit ses colonnes dans le code C++ et lit des tableurs convertis en binaire ; Path of Exile livre des tables binaires dont la structure est définie dans son code ; Grim Dawn décrit les champs dans des modèles de son outil ; Unreal vérifie un CSV ou un JSON contre une structure C++. Point commun : la description des champs vit **dans le code ou l'outil**, la vérification se fait à l'**import**, et le jeu livré lit du **binaire**. Aucun ne valide un JSON Schema au lancement.

  Recommandation : **lecture typée en C++** (la description des champs est le code qui convertit), plus la **forme binaire** pour la version livrée (tâche ci-dessus), conformément à la règle « une bibliothèque seulement pour ce qu'on ne peut pas faire nous-mêmes ». Un schéma JSON pourra être **généré** depuis les mêmes descriptions si la complétion dans l'éditeur de texte manque.
- **Un fichier par table, ou un dossier de fichiers par table ?** Un ARPG a des centaines d'objets : un seul fichier devient long et conflictuel. **Décidé** (utilisateur, 2026-10-03) : **un dossier par table** (`data/monstres/*.json`), toutes les lignes des fichiers du dossier réunies (identifiants uniques sur l'ensemble) ; un seul fichier est le cas particulier d'un dossier d'un fichier.
- **Héritage ?** Pratique (« squelette archer = squelette + arc ») mais source de surprises s'il est profond. Chez les grands : Path of Exile (fichiers `.ot`) et Grim Dawn ont un héritage à **un parent**, sur plusieurs niveaux, où l'enfant **remplace** ce qu'il redonne ; Diablo II et les tables d'Unreal n'ont pas d'héritage mais des **références** vers d'autres lignes ; la fusion profonde est rare. Recommandation : **héritage simple** (`base`, une seule ligne parente, plusieurs niveaux permis, cycles refusés), **fusion champ par champ** au premier niveau (un objet imbriqué remplace l'objet du parent en entier, pas de fusion profonde : la règle tient en une phrase).
- **Que fait un rechargement à chaud aux entités vivantes ?** Options : rien (seules les nouvelles entités voient les nouvelles valeurs) ; tout (chaque entité relit sa ligne : mais ses points de vie actuels ?) ; **par champ**, au choix du jeu. **Décidé** (utilisateur, 2026-10-03) : les entités gardent une **référence** vers leur ligne (pas une copie) pour les valeurs **de définition** (vitesse, points de vie maximum, dégâts de base), qui changent donc aussitôt ; les valeurs **d'état** (points de vie actuels) restent dans leurs composants. C'est au jeu de ranger chaque champ d'un côté ou de l'autre.
- **Identifiants : chaînes ou nombres ?** Chaînes dans les fichiers et les sauvegardes (lisibles, stables si l'on réordonne) ; **indices** en mémoire après chargement (rapides). Une sauvegarde écrit la chaîne, jamais l'indice (qui change quand on ajoute une ligne).
- **Où vivent les données ?** Recommandation : `assets/data/<table>/` pour le jeu, `tests/data/tables/` pour les tests du moteur.

### Pièges connus

- **Le `float` des fichiers** : `0.1` n'est pas exactement représentable ; pour ce qui doit être exact (pourcentages de la conception des stats, chances), lire des **entiers** (millièmes, pour mille) ou des fractions décidées par le jeu, pas des flottants comparés à égalité.
- **Ordre des lignes** : un `std::unordered_map` donnerait un ordre différent d'une plateforme à l'autre ; tout ce qui parcourt une table (tirage de butin) doit suivre un **ordre défini** (identifiants triés, ou ordre du fichier).
- **Référence vers une ligne supprimée au rechargement** : une entité vivante pointe vers une ligne qui n'existe plus. Garder l'ancienne table tant qu'une entité y renvoie, ou refuser le rechargement avec un message (recommandé : **refuser** et afficher l'erreur, la partie continue avec l'ancienne version).
- **Messages d'erreur inutilisables** (« type_error.302 ») : toujours remonter le chemin (`fichier > ligne > champ > indice`) ; c'est ce qui fait gagner le temps.

### Validation

Les tables de test se chargent ; chaque erreur de la liste de test donne un message qui nomme le fichier, la ligne et le champ ; `--check-data` rend 0 sur les bonnes tables et 1 sur les mauvaises ; un champ modifié pendant que la tranche tourne change le comportement des créatures en moins d'une seconde.

### Implémentation réalisée (partie 2)

- **`DataReader`** (`data_reader.hpp/.cpp`) : lecture typée d'un objet JSON (`number`, `integer`, `boolean`, `text`, `choice`, `vec2`, `vec3`, `color`, `texts`, `reference<T>`, `references<T>`, `object`, `objects`), valeur obligatoire si aucune valeur par défaut n'est donnée ; chaque problème (absent, mauvais type, hors bornes, choix inconnu) devient une **`DataIssue`** avec son chemin (`fichier > ligne > champ > [indice]`) et la lecture continue pour tout signaler d'un coup ; `finish()` avertit des **champs inconnus** ; les clés `_…` sont des commentaires ; `error()` / `warning()` pour les vérifications propres au jeu.
- **`DataTable<T>`** et **`DataTables`** (`data_table.hpp/.cpp`) : une table = un **dossier** de fichiers JSON (`version`, `defaults`, `rows`), lignes réunies, identifiants uniques sur le dossier, **héritage à un parent** (`base`, plusieurs niveaux, d'un fichier à l'autre, cycles et bases inconnues refusés) puis remplacement champ par champ au premier niveau. Le jeu enregistre ses tables (`add<T>(nom, dossier, lire, lier)`), les **références** (`DataRef<T>`) se résolvent dans une étape de liaison (`DataLinker`) une fois toutes les tables lues, avec un message qui nomme les deux côtés. Une ligne avec une erreur refuse sa table (rien de partiel).
- **Ordre et rechargement** : indices dans l'ordre des identifiants au premier chargement ; un **rechargement garde chaque ligne à son indice** (les nouvelles à la fin, la nouvelle version réordonnée **avant** sa liaison, pour qu'une référence d'une table vers elle-même soit juste), si bien qu'une `DataRef` tenue par une entité voit aussitôt les nouvelles valeurs ; `sorted()` donne l'ordre des identifiants pour ce qui doit être reproductible. Un rechargement qui **retire une ligne** ou qui a **une erreur** est **refusé** : l'ancienne version reste, les problèmes vont au journal et dans la fenêtre.
- **Rechargement à chaud** : `Assets::add_file_listener` prévient `DataTables` de chaque fichier changé ; un fichier d'un dossier de table recharge cette table (un fichier ajouté au dossier aussi).
- **Forme binaire** : `--compile-data DOSSIER` vérifie tout puis écrit un `<table>.mdat` par table (« MDAT », version du format, lignes résolues en **CBOR** avec l'empreinte des sources) ; `DataTables::set_source(Compiled, dossier)` (`--compiled-data DOSSIER`) ne lit plus que le binaire. Le jeu relit les lignes avec **la même fonction** : mêmes structures, au bit près (test). Le choix de la source pour la version livrée se fera au packaging (jalon 8).
- **`--check-data`** : charge et vérifie toutes les tables sans ouvrir de fenêtre, imprime les problèmes, rend 0 ou 1.
- **`Application::data()`** : les tables du jeu. Le bac à sable les enregistre (`sandbox_data.cpp`) et les charge au démarrage ; des données invalides arrêtent le programme avec la liste des problèmes.
- **Fenêtre DEBUG > Données** : tables (lignes, rechargements, bouton « Recharger »), problèmes du dernier chargement, lignes filtrées, une ligne telle que le jeu la lit (après défauts et héritage).
- **La tranche** : table **`personnages`** (`assets/data/personnages/tranche.json` : `chevalier`, `squelette` et ses deux variantes héritées) avec modèle, taille, rayon, vitesse, poids, vie, dégâts, portée, vue, cadence ; composant `Archetype` (la référence et l'allure propre de la créature) ; vitesses, rayons, poids, portée, vue, dégâts et cadence relus **à chaque tick**. Valeurs reprises du code : captures **inchangées** (démo 2D, démo 3D, rejeu, tranche `f5421305c50a`), y compris avec les tables lues depuis leur forme binaire. Essai en direct : vitesse changée pendant le jeu prise aussitôt ; `vie: 0` refusé ligne par ligne, l'ancienne version gardée.
- Tests (`test_data_table.cpp`) : lecture typée et messages, défauts, héritage sur deux niveaux et entre fichiers, références (dont vers sa propre table), erreurs (cycle, base inconnue, identifiant en double, JSON invalide, bornes, champ inconnu, référence cassée), rechargement (indices gardés, nouvelle ligne, ligne retirée refusée, erreur refusée), forme binaire identique, fichier binaire abîmé. **356 tests.**

---

## 3. Console, variables et journal

### But

Pouvoir **taper des commandes** dans le jeu (tricher, inspecter, régler, déclencher), **régler des variables** sans recompiler, et **retrouver après coup** ce qui s'est passé (journal dans un fichier).

### Tâches

- [x] **Commandes** : un registre (`Console::add("spawn", aide, fonction)`), une ligne analysée en mots (guillemets pour les espaces, nombres reconnus), arguments vérifiés avec un message d'usage, réponse affichée.
- [x] **Variables** (*cvars*) : nommées (`fog.enabled`, `debug.colliders`, `time.scale`), typées (booléen, entier, flottant, texte), bornées, avec valeur par défaut et aide ; `set`, `get`, `reset`, liste filtrée ; certaines **gardées** dans les préférences (`archive`), d'autres non.
- [x] **Fenêtre console** (ImGui) : ouverture par une touche (², sous Échap, à choisir), historique des lignes (flèches), **complétion** (Tab) des commandes, des variables et des arguments connus (noms de tables, de lignes, de cartes), sortie défilante colorée par niveau (info, avertissement, erreur), filtre.
- [x] **Commandes du moteur** : `help`, `set`/`get`/`reset`, `reload <asset>` (ou `reload all`), `assets` (statistiques), `time.scale`, `pause`, `step [n]` (avancer de n ticks en pause), `screenshot`, `quit`, `exec <fichier>` (une liste de commandes, par exemple un réglage de test), `profile` (partie 4). Les commandes du **jeu** (`god`, `spawn`, `tp`, `kill all`, `give`) sont enregistrées par le jeu.
- [x] **Commandes au lancement** : `--exec fichier` et `+set nom valeur` sur la ligne de commande, pour les tests et les captures.
- [x] **Journal** : toutes les lignes (`SDL_Log` et la console) écrites aussi dans un **fichier** du dossier des préférences (`journal.txt`, le précédent gardé en `journal.1.txt`), avec l'heure et le niveau ; vidé régulièrement (pas seulement à la fermeture : un plantage doit laisser le journal).
- [x] **Déterminisme** : une commande qui change l'état du jeu (`spawn`, `tp`) passe par la **logique**, au tick suivant, et est **enregistrée dans le rejeu** comme une entrée ; une commande de pure observation (`get`, `assets`) non.
- [x] Tests unitaires : analyse des lignes (guillemets, nombres, erreurs), complétion, bornes des variables, enregistrement et rejeu d'une commande.

### Questions à se poser

- **Variables globales ou rangées par système ?** Recommandation : un **registre unique** de variables nommées avec des points (`fog.enabled`), chaque système déclarant les siennes au démarrage et lisant sa valeur (ou abonné à son changement) ; pas de variables globales C++ éparpillées.
- **Les commandes changent-elles le déterminisme ?** Oui si elles agissent sur le monde. Recommandation : distinguer **commandes de jeu** (mises en file, appliquées au début du tick suivant, enregistrées dans le rejeu au même titre qu'une action) et **commandes d'outil** (immédiates, sans effet sur la logique : affichage, mesures, rechargement d'assets). Un rejeu qui contient des commandes redonne la même partie.
- **La console en version finale ?** Recommandation : présente mais **fermée par défaut** et limitée aux commandes d'outil (le jeu décidera plus tard s'il garde des triches) ; le journal toujours écrit.
- **Format du journal** : texte simple, une ligne par message : `12:04:36.512 [info] Assets: map 'maps/tranche.json' reloaded`. Pas de JSON : il doit se lire d'un œil et se joindre à un rapport de bug.

### Pièges connus

- **La touche de la console** dépend du clavier (² en AZERTY, ` en QWERTY) : la lier par **position** (scancode `Grave`), comme les autres actions, et ne pas laisser le caractère tapé entrer dans la ligne.
- **La console qui vole les entrées du jeu** : quand elle est ouverte, l'`Input` du jeu doit être muet (`set_muted`), sinon taper « zqsd » fait marcher le héros.
- **Journal et fils** : l'audio (miniaudio) et la surveillance des fichiers (efsw) journalisent depuis d'autres fils ; l'écriture du fichier doit être protégée (un mutex, ou une file).
- **Fichier de journal qui grossit sans fin** pendant une longue session de test : rotation à la taille (quelques Mo).

### Validation

La console s'ouvre et se ferme à la touche ², complète les commandes et les variables, garde l'historique ; `spawn`, `tp` et `set fog.enabled 0` agissent au tick suivant et passent dans un rejeu qui redonne la même capture ; le journal du dernier lancement se trouve dans le dossier des préférences, y compris après un arrêt forcé.

### Implémentation réalisée (partie 3)

- **`Console`** (`console.hpp/.cpp`) : registre de commandes (`add(nom, usage, aide, genre, fonction, complétion)`, une commande redonnée remplace l'ancienne : une scène enregistre les siennes à chaque démarrage et les retire en partant), lignes coupées en mots (`split`, guillemets), historique, **complétion** (noms de commandes, de variables, arguments propres à chaque commande), sortie colorée par niveau, lignes du journal reçues de tous les fils (`log_line`, file protégée, vidée par le fil principal). Deux genres : **outil** (immédiat, sans effet sur la logique) et **jeu** (mis en file, exécuté au début du tick suivant ; un `set` d'une variable marquée `Logic` aussi). Commandes intégrées : `help`, `set`, `get`, `reset`, `toggle`, `vars`, `echo`, `clear`.
- **`Variables`** (`variables.hpp/.cpp`) : booléens, entiers, flottants, textes, bornés, avec défaut et aide ; drapeaux `Archive` (gardées dans `variables.cfg` des préférences, seulement si elles diffèrent du défaut) et `Logic` (leur changement passe par la file des commandes de jeu) ; valeurs lues dans le fichier avant que la variable soit déclarée (`set_pending`), appliquées à sa déclaration ; déclarer deux fois rend la même variable.
- **Journal** (`log_file.hpp/.cpp`) : `SDL_SetLogOutputFunction` écrit chaque message aussi dans `journal.txt` (heure, niveau, texte), vidé à chaque ligne, le précédent gardé en `journal.1.txt`, rotation à la taille, fichier ouvert en partage (`_wfsopen`) pour être lu pendant le jeu ; chaque ligne va aussi à la console.
- **`Application`** : la console, ouverte par la touche **²** (scancode `Grave`, l'`Input` du jeu muet pendant qu'elle a le clavier) ; variable `time.scale` ; commandes `quit`, `pause`, `step [n]`, `reload`, `assets`, `tables`, `exec`, `profile`, `screenshot` ; `--exec fichier` et `+set nom valeur` au lancement. Les **commandes de jeu sont enregistrées dans les rejeux** (`InputFrame::commands`, clé `"c"`) et rejouées au même tick.
- Fenêtre **DEBUG > Console** (`DebugTools`).
- Tests (`test_console.cpp`) : découpage et guillemets, complétion, bornes et types des variables, file des commandes de jeu, archive, enregistrement et rejeu d'une commande.

---

## 4. Profiler et overlays

### But

Savoir **où passe le temps** de chaque tick et de chaque image, par **système** et non plus seulement par phase, et le voir **pendant le jeu** (graphes, pics), pour régler la tranche et préparer le jalon 8.

### Tâches

- [x] **Zones mesurées** : une macro ou un objet de portée (`MOTEUR_PROFILE("collisions")`) qui mesure une zone (horloge haute précision), imbriquable ; les mesures d'une image ou d'un tick rangées en arbre ; coût négligeable quand le profiler est éteint, nul dans une version finale si on le décide.
- [x] **Zones du moteur** : boucle (update, rendu), passes du renderer (déjà comptées), collecte du monde, animation (échantillonnage, palettes), collisions, chemins, flow fields, vision, particules, audio, assets (chargements, rechargements). Les jeux et scènes ajoutent les leurs.
- [x] **Fenêtre DEBUG Profiler** : pour la dernière image (ou une image figée), l'arbre des zones avec temps et part ; sur les dernières secondes, un **graphe** du temps d'image avec ses pics, un clic sur un pic montre son arbre ; moyenne, maximum, percentile 99 par zone (`FrameStats` existe).
- [x] **Capture** : `profile start` / `profile stop` dans la console écrit les images d'un intervalle dans un fichier (format lisible par un outil existant : voir les questions).
- [x] **Overlays** : une petite **barre de statistiques** en haut de l'écran activable par variable (`debug.stats` : FPS, ms CPU et GPU, draw calls, entités, mémoire), un **graphe** du temps d'image ; les overlays de monde du jalon 6 (grille, chemins, cercles, champ de vision…) **branchés sur des variables** (`debug.paths 1`) pour toutes les scènes, pas seulement la scène « Monde ».
- [x] **Tracy** : dépendance vcpkg (option `MOTEUR_TRACY`, éteinte par défaut), les macros du moteur appellent aussi Tracy quand elle est allumée ; zones GPU de SDL_GPU si c'est possible *(à vérifier)* ; `credits.json`.
- [x] `--report` imprime aussi les zones principales (moyenne, p99).
- [x] Tests unitaires : imbrication des zones, cumul d'une zone appelée plusieurs fois, image figée, profiler éteint (aucune mesure).

### Questions à se poser

- **Maison ou Tracy ?** Options :
  - **Tracy** (BSD 3, dans vcpkg *(à vérifier : port et version)*) : profiler en temps réel de référence (zones, fils, verrous, mémoire, GPU), avec une application d'affichage séparée et un coût très faible ; il demande l'outil de visualisation à côté, une connexion réseau locale, et ses zones sont des macros à lui.
  - **Maison** : des zones mesurées, un arbre par image, une fenêtre ImGui. Quelques centaines de lignes ; dans le jeu, sans autre programme ; mais ni fils, ni mémoire, ni GPU détaillé.
  
  Recommandation : **les deux**, derrière des macros **à nous** (`MOTEUR_PROFILE`) : le profiler **maison** pour la fenêtre dans le jeu (sert tous les jours, y compris sur le Mac sans rien installer, et alimente `--report`), et **Tracy** branché dès ce jalon (option de compilation) pour les analyses fines : fils, verrous, mémoire, zones GPU, captures longues. C'est l'outil standard de l'industrie pour cela, et on ne le referait pas à ce niveau nous-mêmes. La capture du profiler maison s'écrit aussi au **format Chrome Trace** (Perfetto, Speedscope *(à vérifier)*).
- **Profiler dans la version finale ?** Recommandation : zones compilées en version de développement et en *Release* (mesures du jalon 8), **retirées** d'une version distribuée par une option de compilation.
- **Mesurer le tick ou l'image ?** Les deux : la logique tourne à pas fixe (0 à plusieurs ticks par image), le rendu une fois par image. L'arbre d'une image contient ses ticks.

### Pièges connus

- **Mesurer coûte** : `SDL_GetPerformanceCounter` à chaque zone est rapide, mais des milliers de zones par image (une par entité) faussent ce qu'on mesure. Une zone par **système**, pas par entité.
- **Les pics viennent souvent d'ailleurs** : chargement d'un asset au milieu du jeu, rechargement à chaud, attente du GPU (`SDL_AcquireGPUSwapchainTexture`, VSync). Les compter comme des zones à part, sinon le graphe accuse le mauvais système.
- **Zones mal fermées** (un `return` au milieu) : un objet de portée (RAII), jamais des paires début/fin à la main.

### Validation

La fenêtre Profiler montre l'arbre de la dernière image avec les zones des systèmes du jalon 6 dans la tranche ; un pic provoqué (rechargement d'une carte) se retrouve dans le graphe et son arbre ; une capture `profile start/stop` s'ouvre dans un visualiseur Chrome Trace ; le coût du profiler allumé est mesuré et noté.

### Implémentation réalisée (partie 4)

- **`Profiler`** (`profiler.hpp/.cpp`) : zones imbriquées par image (`MOTEUR_PROFILE("nom")`, objet de portée), sur le fil principal seulement ; historique des 600 dernières images, résumé (moyenne, p99, maximum, appels par image), totaux de la session, image figée, capture `profile start` / `profile stop fichier.json` au format **Chrome Trace** (ui.perfetto.dev). Variable **`profile.enabled`** (partie 10) : éteint, aucune zone n'est mesurée.
- **Tracy** : option CMake `MOTEUR_TRACY` (feature vcpkg `tracy`), `MOTEUR_PROFILE` ouvre aussi une zone Tracy et chaque image est marquée ; credits.json (entrée filtrée par la fenêtre « À propos » selon le build).
- **Zones** : boucle (assets, logique, tick, attente de l'affichage, rendu : enregistrement / envoi, audio), collisions, chemins, A\*, flow field, déplacements, champ de vision, particules, monde : collecte, animation : avance, maillages : préparation, données : rechargement ; la tranche ajoute les siennes (partie 11). `--report` imprime les zones.
- **Fenêtre DEBUG > Profiler** : arbre de l'image (ou d'une image choisie dans le graphe des temps), statistiques par zone ; **barre de statistiques** (`debug.stats`).
- **Overlays du monde** (`world_debug.hpp/.cpp`) : `WorldDebugFlags::declare` crée les variables `debug.grid`, `debug.clearance`, `debug.colliders`, `debug.paths`, `debug.flowfield`, `debug.view`, et `draw_world_debug` les dessine dans toute scène qui donne ses sources (scène « Monde », tranche).
- Tests (`test_profiler.cpp`) : imbrication, cumul d'une zone appelée deux fois, image figée, profiler éteint, export Chrome Trace.

---

## 5. Sauvegarde : fichiers, versions et emplacements

### But

Un **format de sauvegarde** versionné, écrit **sans jamais corrompre** la sauvegarde précédente, dans des **emplacements** (slots) du dossier des préférences, avec ce qu'il faut pour les afficher dans un menu (date, durée de jeu, lieu, image).

### Tâches

- [x] **Format** : un en-tête (magie, version du format, version du jeu, date, durée de jeu, résumé pour le menu) puis des **sections** nommées (une par système : monde, joueur, exploration, quêtes plus tard…), chacune avec sa propre version.
- [x] **Écriture sûre** : écrire dans un fichier temporaire, le vider sur disque, puis le **renommer** sur l'ancien (remplacement atomique) ; garder l'avant-dernière sauvegarde (`.bak`) ; une **somme de contrôle** (CRC32 ou xxHash maison) pour détecter un fichier abîmé.
- [x] **Emplacements** : `saves/slot_1.sav`…, une sauvegarde automatique (`auto.sav`, tournante sur 2 ou 3) ; liste des emplacements avec leur en-tête lu sans charger le reste ; suppression.
- [x] **Migrations** : une fonction par passage de version (`v1 → v2`) appliquée en chaîne au chargement ; une sauvegarde trop récente (d'une version future) refusée avec un message.
- [x] **Vignette** : une petite capture (par exemple 256 × 144) au moment de la sauvegarde, rangée dans le fichier ou à côté, pour le menu.
- [x] Tests unitaires : aller-retour, fichier tronqué ou modifié (somme fausse) refusé proprement, migration v1 → v2, version future refusée, coupure simulée pendant l'écriture (l'ancienne sauvegarde reste lisible).

### Questions à se poser

- **JSON, binaire, ou les deux ?** Options : **JSON** (lisible, comparable avec un outil de diff, facile à déboguer, mais gros et lent pour de grands états) ; **binaire maison** (compact, rapide, illisible) ; **JSON compressé** ; **CBOR / MessagePack** via nlohmann (même modèle de données que le JSON, binaire compact, fourni par nlohmann *(à vérifier : `to_cbor`, `to_msgpack`)*). Recommandation : un **modèle unique** (les sections s'écrivent en `nlohmann::json`) et **deux encodages** : **JSON lisible** en développement (pour comprendre une sauvegarde), **CBOR** (binaire compact, sans dépendance de plus) en version distribuée ; le chargement reconnaît les deux.
- **Sauvegarder où le joueur veut, ou à des points fixes ?** C'est une décision de **jeu** (les ARPG sauvegardent souvent en continu, au changement de zone et à la sortie). Le moteur doit permettre les deux ; la tranche sauvegarde depuis le menu de pause et automatiquement à intervalle.
- **La sauvegarde est-elle triche-résistante ?** Solo d'abord : non. La somme de contrôle détecte l'abîme, pas la triche. Un jeu en ligne ferait tout autrement.
- **Sauvegarder pendant le jeu sans à-coup ?** L'état se **copie** au début d'un tick (rapide), l'encodage et l'écriture peuvent se faire sur un autre fil. Recommandation : tout sur le fil principal d'abord, mesurer (partie 10) ; un fil seulement si l'à-coup se voit.

### Pièges connus

- **Le renommage atomique** n'a pas les mêmes garanties partout : sous Windows, `MoveFileEx` avec remplacement (ou `ReplaceFile`) ; sous macOS, `rename` est atomique sur le même volume *(à vérifier : `std::filesystem::rename` remplace-t-il sous Windows ?)*. Tester la coupure.
- **Les flottants en JSON** : nlohmann écrit les `float` en décimal ; un aller-retour doit redonner **exactement** la même valeur (sinon la partie rechargée diverge). Vérifier l'aller-retour bit à bit (nlohmann écrit le plus court qui relit pareil *(à vérifier)*), ou écrire les flottants de la logique en hexadécimal / entiers.
- **Les chemins non ASCII** (un nom d'utilisateur accentué dans le dossier des préférences) : passer partout par les chemins UTF-8 du moteur.

### Validation

Les tests de fichiers passent (y compris la coupure simulée) ; le menu de la tranche liste les emplacements avec date, durée et vignette ; une sauvegarde JSON s'ouvre dans un éditeur de texte et se lit.

### Implémentation réalisée (partie 5)

- **Écriture sûre** (`file_io.hpp/.cpp`) : `write_file_atomic` écrit un `.tmp`, le vide sur disque, puis remplace (`ReplaceFileW` sous Windows, `rename` après `fsync` ailleurs), avec la copie précédente en `.bak` sur demande ; chemins UTF-8 ; `read_file_bytes`, `path_exists`, `file_time` (horloge du système de fichiers, en ticks de 100 ns ou plus fins : un changement fait dans la même seconde se voit), `crc32`.
- **`SaveGame`** (`save_file.hpp/.cpp`) : fichier « MSAV » = magie, version du format, encodage, en-tête (version du jeu, date, durée de jeu, résumé pour le menu), longueur, corps, **CRC-32** ; sections nommées avec leur version ; **JSON** ou **CBOR** (même modèle, lecture des deux) ; un fichier tronqué, modifié ou d'un format futur est refusé avec la raison, jamais lu à moitié ; l'en-tête se lit sans le corps. **`SaveMigrations`** : une fonction par passage de version d'une section, en chaîne ; une version future refusée.
- **`SaveSlots`** : emplacements d'un dossier (`nom.sav`, `nom.sav.bak`, vignette `nom.png`), liste triée du plus récent au plus ancien (en-têtes seulement, illisibles signalés), lecture avec repli sur la copie de secours, suppression.
- Vignettes (partie 11) : `Renderer::request_capture(chemin, largeur_max)` réduit la capture par blocs entiers et l'écrit atomiquement.
- Tests (`test_save_file.cpp`, `test_robustness.cpp`) : aller-retour JSON et CBOR, coupures à toutes les tailles, octet changé, migration, version future, coupure simulée (l'ancienne sauvegarde reste), copie de secours, chemins accentués.

---

## 6. Sauvegarde : l'état du monde

### But

Écrire et relire **tout l'état de la partie** (entités, composants, objets des systèmes, générateurs aléatoires) de sorte qu'une partie **chargée** se poursuive **exactement** comme si elle n'avait jamais été interrompue.

### Tâches

- [x] **Identité persistante** : un composant `PersistentId` (un entier 64 bits unique dans la partie) pour les entités à sauvegarder ; les références entre entités (`Parent`, une cible d'attaque) s'écrivent par cet identifiant et se rétablissent au chargement. Les entités **non persistantes** (décor reconstruit depuis la carte, effets, marqueurs) ne s'écrivent pas.
- [x] **Sérialiseurs de composants** : un registre (comme celui de l'inspecteur : `serializers.add<Health>("sante", écrire, lire)`) ; ceux du moteur : `Transform`, `PreviousTransform` (ou reconstruit), `Animator` (clips, couches, temps en ticks, vitesses, fondus en cours : **état entier**, jamais la pose), `Mover`, `Collider`, `Hidden`, `Name`, `BoneAttachment`, `LightSource`… ; ceux du jeu par le jeu. Un composant sans sérialiseur sur une entité persistante est **signalé** (oubli).
- [x] **État hors composants** : les systèmes qui en ont un l'écrivent dans leur section : cases explorées (`ExploredMap`), cases changées de la grille (une liste de différences avec la carte d'origine, pas la grille entière), générateurs aléatoires de la logique, numéro de tick, pile d'états de jeu (en jeu ou en pause), chemins en cours (ou replanifiés au chargement : voir les questions).
- [x] **Chargement** : construire la scène depuis la carte (décor), puis créer les entités sauvegardées avec leurs composants, puis rétablir les références, puis l'état des systèmes ; recalculer ce qui se déduit (flow fields, champ de vision, dégagement) plutôt que de l'écrire.
- [x] **Test d'exactitude** : jouer N ticks (rejeu d'entrées), sauvegarder, continuer M ticks, capturer ; recharger la sauvegarde, jouer les mêmes M ticks, capturer : **mêmes images, même hachage**. Le même test sur la tranche, en test automatisé (`--save-at N --load ...` ou un test unitaire sur un monde sans GPU).
- [x] Tests unitaires : aller-retour de chaque sérialiseur du moteur, références entre entités, entité sans sérialiseur signalée, différences de grille.

### Questions à se poser

- **Tout sauvegarder, ou reconstruire ?** Recommandation : **sauvegarder l'état, reconstruire le déductible** : flow fields, dégagement, champ de vision, palettes, poses, caches de la collecte se recalculent ; les chemins en cours se **replanifient** (l'A\* est déterministe : même résultat si la grille et les positions sont les mêmes). Moins de choses écrites, moins de choses qui peuvent diverger.
- **Le décor tiré au hasard** (arbres, caisses de la tranche, tirés avec une graine) : il se **retire** à l'identique depuis la graine (sauvegardée), ou il devient des **objets posés** dans la carte (partie 8). Recommandation : objets posés pour ce qui est fixe ; ce qui change pendant la partie (une caisse brisée) s'écrit en différences.
- **Identifiants persistants : comment les donner ?** Un compteur de la partie (le prochain identifiant est sauvegardé lui aussi) ; jamais l'identifiant EnTT (il dépend de l'ordre de création et du recyclage).
- **Sauvegarder au milieu d'un tick ?** Jamais : seulement **entre deux ticks** (la commande de sauvegarde est mise en file comme les autres commandes de jeu, partie 3).
- **Les particules et les sons** en cours : non sauvegardés (purement visuels et sonores) ; l'ambiance et la musique se relancent selon l'état du jeu.

### Pièges connus

- **L'ordre des entités après chargement** : EnTT les rangera autrement ; tout système qui parcourt dans l'ordre de stockage (au lieu de l'ordre trié du jalon 6) donnerait une autre partie. Le test d'exactitude le révélera : c'est son rôle.
- **Un état oublié** (un compteur dans un système, un temps de recharge dans un composant du jeu) : la partie rechargée diverge quelques secondes plus tard. Le test d'exactitude, sur une **longue** séquence avec des combats, est le seul vrai garde-fou.
- **`PreviousTransform` et l'interpolation** : au chargement, la première image interpole entre une position précédente absente et la position chargée ; mettre `PreviousTransform = Transform` au chargement (pas de glissement à l'écran).
- **Les assets** : une sauvegarde nomme des assets (modèle, carte, table) par leur chemin ; un asset renommé casse les sauvegardes. Les données nommées par identifiant de table plutôt que par chemin de fichier résistent mieux.

### Validation

Le test d'exactitude passe sur la tranche (sauvegarde au milieu d'un combat, rechargement, mêmes captures après plusieurs centaines de ticks) ; les sérialiseurs du moteur ont leurs tests ; une sauvegarde rechargée après la fermeture du programme reprend la partie au même endroit, créatures et brouillard compris.

### Implémentation réalisée (partie 6)

- **`PersistentId`** et **`ComponentSerializers`** (`world_save.hpp/.cpp`) : registre `add<T>(nom, écrire, lire)`, `add_tag`, `ignore` ; ceux du moteur (`add_engine_components` : `Transform`, `Mover`, `Collider`, `Hidden`, `Name`, `BoneAttachment`, `Animator`…) ; `save` écrit les entités persistantes dans l'ordre de leur identifiant, `load` les recrée par une fonction du jeu (le personnage d'après sa ligne de table) puis donne à chaque composant sa valeur ; un composant sans sérialiseur sur une entité persistante est signalé. `PreviousTransform` = `Transform` au chargement.
- **`Animator::save_state` / `load_state`** (`animator_state.cpp`) : couches, mouvements, horloges (`ClipClock::State`), poids et fondus en cours, vitesses, ticks, dernier événement, et l'état du graphe (partie 9) ; jamais la pose.
- **La tranche** (`Demo3D::save` / `load`) : section `monde` (héros et créatures) et section `scene` (ticks, graine, compteurs, éclairs de compétence, cible d'attaque, caméra, case du flow field, **cases explorées** en plages, **différences de la grille** avec la carte, invulnérabilité, brouillard). Le déductible se recalcule (flow field, dégagement, champ de vision, poses). `--save-at N fichier` (`--save-cbor`), `--load fichier`, `--replay-start N`.
- **Test d'exactitude** : la tranche rejouée sans interruption (A), sauvegardée au tick 120 ou 250 (B), puis rechargée et rejouée depuis ce tick (C) donne la **même capture** au tick 400 (`063b80d5f127`) ; l'état logique comparé au tick 400 : aucune différence. Automatisé en partie 11.
- Tests (`test_world_save.cpp`) : aller-retour des sérialiseurs du moteur, références entre entités, composant oublié signalé, plages d'exploration.

---

## 7. Éditeur de cartes : tuiles et points

### But

Un **éditeur de cartes de production** dans le moteur pour peindre les tuiles, placer les points nommés, redimensionner, éditer des **morceaux** de cartes (les salles que la génération procédurale assemblera), et **écrire le fichier** de la carte (le format du jalon 6), avec annuler / refaire, vérifications et essai immédiat. La roadmap disait « minimal » ; on vise l'outil avec lequel les zones du jeu seront réellement faites.

### Tâches

- [x] **Mode éditeur** : une scène (DEBUG > Éditeur de cartes, `--editor carte`) qui ouvre une carte de `assets/maps/`, la montre en 3D comme la scène « Monde » (vue de dessus possible, grille affichée), avec une palette des **types de tuiles** et de la légende.
- [x] **Outils** : pinceau (une case, taille réglable), rectangle plein et creux (des murs de salle d'un coup), remplissage (pot de peinture), gomme (retour au sol), pipette ; par **calque** ; **points nommés** (poser, déplacer, renommer, supprimer).
- [x] **Annuler / refaire** (Ctrl+Z, Ctrl+Y) par commandes (chaque action garde ce qu'elle a changé), sans limite pratique pour une session.
- [x] **Redimensionner** la carte (ajouter ou retirer des lignes et colonnes de chaque côté), avec décalage des points.
- [x] **Types de tuiles et légende** : créer un type (nom, praticable, opaque, région), lui donner un caractère libre ; vérifier que la carte reste écrivable (deux caractères de même sens refusés, comme au chargement).
- [x] **Écriture** : le JSON du jalon 6, lignes de la carte sur une ligne chacune (diffable), via l'écriture sûre de la partie 5 ; la carte ouverte dans le jeu se **recharge à chaud** d'elle-même.
- [x] **Essai** : un bouton (ou une touche) qui lance la scène « Monde » ou la tranche sur la carte en cours, et revient à l'éditeur.
- [x] **Vérifications** affichées : départ manquant, zones inaccessibles depuis le départ (le flow field du jalon 6 depuis le départ, cases jamais atteintes surlignées), points hors de la carte.
- [x] **Morceaux** (*chunks*, salles) : une carte sans départ obligatoire, avec des **connecteurs** (points d'entrée et de sortie typés sur ses bords) ; l'éditeur les crée, les vérifie (connecteurs sur des cases praticables du bord) et les essaie seuls. La génération procédurale du jeu les assemblera ; le moteur fournit le format et l'édition.
- [x] Tests unitaires : outils (rectangle, remplissage, redimensionnement), annuler / refaire, écriture puis relecture identique, connecteurs.

### Questions à se poser

- **Dans le moteur, application à part, ou Tiled ?** Options :
  - **Dans le moteur** (ImGui) : même rendu que le jeu, essai immédiat, aucun format à convertir ; mais une interface à écrire.
  - **Une application à part** (même moteur, un autre exécutable) : sépare l'outil du jeu, mais deux programmes à tenir.
  - **Tiled** (libre) : un éditeur complet tout de suite, mais son format n'est pas le nôtre (conversion), pas de rendu 3D ni d'essai, et les objets 3D (partie 8) lui sont étrangers.
  
  Recommandation : **dans le moteur**, d'abord comme mode du bac à sable, organisé dès le départ comme une **application d'outil** à part entière (son propre état, ses fenêtres ancrées, ses raccourcis, aucun code de la tranche dedans) pour devenir un exécutable `editeur` séparé au jalon 8 sans réécriture. Les studios d'ARPG font leurs zones dans leurs propres outils (ceux de Path of Exile, l'ArtManager de Grim Dawn) pour la même raison : le rendu du jeu, ses données, l'essai immédiat.
- **Le format de carte doit-il changer ?** Pour les tuiles et les points, non : le JSON du jalon 6 suffit. Les **objets posés** demandent une version 2 (partie 8). La **génération procédurale** du jeu produira des cartes du même format (ou des morceaux de cartes assemblés : « salles » éditées à la main puis combinées), ce qui plaide pour que l'éditeur sache aussi éditer des **morceaux** (petites cartes sans départ obligatoire).
- **Vue 3D ou 2D ?** Recommandation : la vue 3D du jeu avec une **caméra de dessus** activable (orthographique, pas de murs qui cachent), et le tramage des murs désactivé dans l'éditeur.

### Pièges connus

- **Peindre en glissant** donne une case par image : à vitesse rapide, des trous. Tracer une **ligne** (Bresenham) entre la case de l'image précédente et l'actuelle.
- **Un annuler par case peinte** rend Ctrl+Z inutilisable : une **action** = le geste entier (du clic au relâchement).
- **Reconstruire tout le décor 3D** à chaque case peinte sur une grande carte : reconstruire seulement les blocs touchés (le sol est déjà en blocs de 16 × 16).
- **Écrire par-dessus une carte modifiée à la main** entre-temps : comparer la date du fichier avant d'écrire, demander.

### Validation

Une salle et un couloir ajoutés à `tranche.json` dans l'éditeur, enregistrés, se rechargent dans le jeu ; le fichier écrit a un diff limité aux lignes changées ; annuler et refaire retrouvent exactement les états ; une zone inaccessible est signalée.

### Implémentation réalisée (parties 7 et 8)

- **`MapDocument`** (`map_document.hpp/.cpp`) : la carte en cours d'édition, sans GPU : tuiles par calque, types, légende (ses caractères gardés, un caractère choisi pour une pile nouvelle), points, objets, connecteurs, description, morceau ; outils `line` (Bresenham), `rect` plein ou creux, `fill`, `resize` (points, objets et connecteurs suivent) ; **actions** (`begin_action` / `end_action` : un geste = un pas), annuler / refaire (500 pas), `clear_history`, `set_description` et `set_chunk` enregistrés ; `validate` (départ manquant, cases inaccessibles depuis le départ par le flow field, points ou objets hors carte, connecteurs hors du bord praticable) ; `to_json` **stable** (une ligne par rangée et par objet, légende triée, nombres les plus courts qui relisent pareil : changer une case change une ligne du fichier).
- **Format de carte v2** (`MapData`) : `objects` (id, type, x, y, z, rotation, échelle, propriétés), `connectors`, `chunk` ; v1 toujours lu.
- **Types d'objets** du jeu (`apps/bac_a_sable/map_objects.hpp/.cpp`) : rocher, arbre, caisse, tonneau, monstre (apparition : ligne de la table personnages, allure, direction, vie), lumière, effet, marqueur, chacun avec ses propriétés typées (nombre, entier, texte, vecteur, couleur, booléen, personnage, effet) et leurs défauts ; `build_object` crée leurs entités (décor, lumière, marqueur), sans rien laisser à moitié si une propriété est mauvaise.
- **L'éditeur** (`apps/bac_a_sable/map_editor.hpp/.cpp`) : une scène du bac à sable (DEBUG > Tests moteur > Éditeur de cartes, `bac_a_sable --editeur [carte]`) et **son propre programme `editeur`** (`editeur [carte | --new] [--top] [--self-test]`), dans une bibliothèque commune (`bac_a_sable_commun` : tables, types d'objets, scène « Monde », éditeur).
  - Vue 3D de la carte (sol par blocs de 16 × 16 teinté par type, murs, piliers, barrières, herbes hautes, objets), caméra inclinée ou **de dessus** (T, orthographique), déplacement au clic droit ou à la molette enfoncée, zoom vers le pointeur, Origine pour toute la carte ; grille, bord (cyan pour un morceau), noms des points et connecteurs.
  - Outils : sélection (V), pinceau (B, taille 1 à 9, traits sans trous), gomme (E), rectangle plein et vide (R, Maj+R), remplissage (F), pipette (I), points (P), objets (O : poser aligné sur une grille réglable, choisir, glisser, Ctrl+clic pour tirer une copie, [ et ] pour tourner, Suppr, Ctrl+D), connecteurs (C, sur une case du bord, tournés vers l'extérieur).
  - Panneaux : calque (toute la pile ou un seul), pile du pinceau, **palette** (la légende), ajout à la légende, **types de tuiles** (nom, praticable, opaque, région), description, propriétés de l'objet choisi selon son type (personnages de la table, effets du dossier), connecteurs, **vérifications** (un clic va à la case), affichage.
  - Ne reconstruit que les **blocs touchés** et les objets changés (tout en cas d'annuler, de redimensionnement ou de type changé).
  - **Fichier** : nouvelle carte, ouvrir, enregistrer (Ctrl+S, écriture atomique dans `assets/maps` **des sources**), enregistrer sous ; un fichier **changé par ailleurs** est signalé et jamais écrasé sans le demander ; des modifications non enregistrées sont gardées dans les préférences à la fermeture et proposées à la réouverture.
  - **Essayer** (F5 depuis le départ, F6 depuis le pointeur) : la scène « Monde » sur la carte telle qu'elle est, enregistrée ou non ; Échap revient.
  - **Auto-test** (`editeur --self-test`) : pinceau, annuler, refaire, objets, enregistrement relu à l'identique, fichier changé par ailleurs, essai : **réussi**, en Release et en Debug.
- **La scène « Monde »** crée les objets des cartes (décor, lumières, effets, apparitions) et peut jouer une carte de l'éditeur (`WorldTest::Options::data`, `start`).
- **La tranche** : son décor et ses 300 créatures sont des objets de `assets/maps/tranche.json` (3 300 objets, écrits une fois par `--export-slice-objects` depuis le tirage de la graine 42) ; même capture qu'avec le tirage. Un objet abîmé (type inconnu, propriété du mauvais type, personnage absent de la table) est ignoré avec un message (partie 10).
- Tests (`test_map_document.cpp`, `test_map_data.cpp`) : outils, actions, annuler / refaire, redimensionnement, écriture stable relue à l'identique (toutes les cartes de `assets/maps`), vérifications, connecteurs, v1 et v2.
- **Pas fait par moi** : retoucher la carte de la tranche dans l'éditeur (une salle ajoutée) : c'est l'essai à la main de la validation.

---

## 8. Éditeur de cartes : objets posés

### But

Placer dans la carte ce qui n'est pas une tuile : **décor** (arbres, caisses, tonneaux, modèles glTF), **lumières**, **effets** (feu), **apparitions** de monstres (une ligne d'une table, un nombre, un rayon), **déclencheurs** simples ; et les écrire dans le fichier de la carte.

### Tâches

- [x] **Format de carte v2** : une liste `objects` : type (`model`, `light`, `effect`, `spawn`, `marker` ; extensible par le jeu), position (x, z, hauteur), rotation (degrés autour de y), échelle, et des **propriétés** propres au type (chemin du modèle, couleur de la lumière, nom de l'effet, identifiant de la table de monstres…). `MapData` lit v1 et v2 ; l'éditeur écrit v2.
- [x] **Types d'objets** déclarés par le jeu (registre : nom, propriétés et leur type, comment le montrer dans l'éditeur, comment le créer dans le monde) ; ceux du moteur : modèle, lumière ponctuelle, effet de particules, marqueur.
- [x] **Outils** : poser depuis une palette (avec aperçu sous le pointeur, aligné sur la grille ou libre), sélectionner (clic, rectangle), **déplacer, tourner, mettre à l'échelle** (poignées simples ou touches), dupliquer, supprimer, éditer les propriétés dans un panneau ; annuler / refaire comme en partie 7.
- [x] **Collisions des objets** : un objet peut **bloquer des cases** (option : il marque ses cases non praticables, comme le brasero) ou porter un `Collider` immobile (cercle) ; décider par type.
- [x] **La tranche** : son décor (aujourd'hui tiré au hasard dans le code) devient des **objets posés** dans `tranche.json` (une fois, par un script qui écrit le tirage actuel, pour garder la même tranche), et ses créatures des **apparitions** qui renvoient à la table des monstres.
- [x] Tests unitaires : lecture v1 et v2, aller-retour des objets, migration d'une carte v1 en v2 (rien de perdu), propriétés inconnues signalées.

### Questions à se poser

- **Les objets sont-ils des entités dans le fichier ?** Recommandation : **non**, des **descriptions** (type + propriétés) que le jeu transforme en entités au chargement ; le fichier ne connaît ni EnTT ni les composants. C'est aussi ce qui permet de sauvegarder une partie par **différences** avec la carte (partie 6).
- **Grille ou position libre ?** Les deux : alignement sur la grille par défaut (demi-case ou case), libre en tenant une touche ; la position est écrite en flottants (mètres), arrondie au centième pour garder des fichiers lisibles et stables.
- **Que reste-t-il au jeu ?** Les types `spawn` (quelle table, combien, quel comportement) et les déclencheurs sont du **jeu** ; le moteur fournit le registre et l'édition des propriétés.

### Pièges connus

- **Des milliers d'objets** (le décor de la tranche en a 3 000) : l'éditeur doit rester fluide (sélection par la grille de hachage, pas un parcours de tout), et le fichier rester lisible (un objet par ligne).
- **Les objets qui bloquent des cases** changent la `NavGrid` : la reconstruire (ou la mettre à jour) à chaque déplacement dans l'éditeur, et à l'identique au chargement dans le jeu.
- **Les arrondis** : un objet déplacé puis remis en place doit réécrire exactement le même texte (sinon diffs parasites).

### Validation

Le décor et les apparitions de la tranche sont dans `tranche.json` ; la tranche chargée depuis ce fichier est la même qu'avant (captures comparées ou différences expliquées) ; un arbre déplacé, une lumière ajoutée et une apparition changée dans l'éditeur se voient en jeu après enregistrement.

### Implémentation réalisée (partie 8)

Voir [la section commune des parties 7 et 8](#implémentation-réalisée-parties-7-et-8) : format v2, types d'objets, outils de l'éditeur, décor et apparitions de la tranche en objets.

---

## 9. Reprises : graphe d'animation en données et particules douces

### But

Faire deux choses laissées en suspens : le **graphe d'animation** (quel clip jouer selon l'état du personnage), en code dans le jeu depuis le jalon 5, passe **en données** ; et les **particules douces**, repoussées au jalon 6, sont faites.

### Tâches

**Graphe d'animation**

- [x] **Machine à états en données** dans l'`AnimationSet` (rechargée à chaud) : **paramètres** nommés que le jeu fixe à chaque tick (`vitesse` en flottant, `en_combat` booléen, `coup` déclencheur consommé une fois), **états** (un clip, un blend space ou un sous-graphe ; couche), **transitions** (condition sur les paramètres, fin de clip, fondu en ticks, priorité, interruption permise ou non), états **« depuis n'importe où »** (touché, mort), **couche du haut** avec son propre graphe.
- [x] Évaluation **au tick, déterministe** (conditions en entiers ou en comparaisons de flottants venus de la logique, ordre des transitions défini), dans l'`Animator` ; les **événements** du jalon 5 inchangés.
- [x] La **tranche** passe ses personnages sur ce graphe (repos, locomotion, attaque, touché, mort) : le code du jeu ne fait plus que fixer des paramètres.
- [x] Une vue DEBUG du graphe : état courant, paramètres, dernières transitions (dans la fenêtre Animation).
- [x] Tests unitaires : transitions par condition, fin de clip, priorité, déclencheur consommé une fois, « depuis n'importe où », même suite d'états d'une fois à l'autre.

**Particules douces**

- [x] **Copie de la profondeur** de la scène après les maillages (ou une passe de profondeur séparée), dans une texture lisible, avant les billboards ; avec le **MSAA**, la profondeur résolue (ou lue par échantillon *(à vérifier : ce que SDL_GPU permet)*).
- [x] `billboard.frag` atténue un billboard qui approche la surface derrière lui (distance de fondu par effet, dans le JSON des émetteurs) ; éteint, rendu identique au bit près.
- [x] Coût mesuré (copie et lecture) ; captures des démos inchangées quand rien ne l'utilise.

### Questions à se poser

- **Format du graphe** : des transitions écrites comme **conditions simples** (`"vitesse > 0.1"`, `"coup"`, `"fin"`) analysées au chargement, ou une liste structurée (`{"param": "vitesse", "op": ">", "value": 0.1}`) ? Recommandation : **liste structurée** (rien à analyser, erreurs nommant le champ, éditable par un outil plus tard), avec des combinaisons « toutes » / « une ».
- **Pourquoi maintenant ?** Le jalon 5 recommandait le code pour quatre ou cinq états ; un ARPG livrable aura des dizaines de monstres, chacun avec ses états, réglés par les données comme le reste (partie 2). Les moteurs du marché (Unreal, Unity) et les ARPG décrivent ces graphes en données dans leurs outils.

### Validation

La tranche joue ses animations par le graphe en données, avec les mêmes clips aux mêmes ticks qu'avant pour les mêmes entrées (ou des écarts expliqués) ; une transition modifiée dans le JSON pendant le jeu change le comportement aussitôt ; une fumée posée au sol ne coupe plus net, et les captures qui n'en ont pas sont inchangées.

### Implémentation réalisée (partie 9)

**Graphe d'animation**

- **`AnimationGraph`** (`animation_graph.hpp/.cpp`), clé `graph` de l'`AnimationSet` : paramètres (`float`, `int`, `bool`, `trigger`), une ou deux couches (le corps, le haut du corps), états (un clip ou un blend space, ou rien ; `loop`, `restart`, `interruptible`, `match_speed`), transitions (`from` un état ou `*`, `to`, `when`, `fade_ticks`, `priority`, `interrupt`, `self`). Conditions **structurées** : `{param, op, value}`, un booléen ou un déclencheur seul, `{end: true}`, `all`, `any`, `not`. Erreurs nommant leur place (`graph > layers[1] > transitions[0] > when > param: 'attaq' is not a parameter`).
- **Évaluation** dans l'`Animator` (`animator_graph.cpp`) : quand un paramètre change de valeur, quand un déclencheur est tiré (`fire` dit s'il a été pris), et au début de chaque tick ; couches dans l'ordre, transitions par priorité puis ordre du fichier, jusqu'à quatre enchaînées par évaluation ; un déclencheur est pris **au plus une fois par couche** (un seul `mort` fait tomber le corps et lâcher le haut du corps) et n'attend pas un tick plus tard. Entrer dans un état vide fond la couche, sauf si elle s'éteint déjà d'elle-même (fin d'un coup). `start_graph()` l'active (sinon le jeu joue ses clips lui-même : scènes de test inchangées). Rechargé à chaud : états et paramètres retrouvés par leur nom ; un fichier dont un état nomme un clip absent met le graphe en attente (message), le personnage continue.
- **Sauvegardé** avec l'`Animator` (valeurs et états par leur nom).
- **La tranche** : `animations/kaykit.json` porte le graphe (locomotion et mort pour le corps ; repos, attaque, touché pour le haut du corps) ; le jeu ne fait plus que `fire("attaque")`, `fire("touche")`, `fire("mort")`. Mêmes clips aux mêmes ticks : capture de la tranche **inchangée** (`f5421305c50a` avant les particules douces) et test d'exactitude réussi.
- **Vue DEBUG** (fenêtre Animation) : état de chaque couche, paramètres, dernières transitions avec leur tick.
- Tests (`test_animation_graph.cpp`) : fichier vérifié, conditions, priorité, déclencheur pris une fois, fin de clip, interruption, « depuis n'importe où » et `self`, même suite d'états d'une fois à l'autre et après sauvegarde, rechargement (seuil changé pris aussitôt, fichier cassé en attente).

**Particules douces**

- Plutôt qu'une copie de la profondeur (SDL_GPU ne résout pas une profondeur multiéchantillonnée), la passe des maillages écrit une **seconde cible** : la distance de la surface le long de la vue, en mètres (`R16_FLOAT`, `Renderer::kDistanceFormat`), **résolue avec le MSAA** comme la couleur.
- La scène est en **deux passes** : les maillages, puis ce qui se mélange par-dessus (billboards, lignes de debug), qui lit la distance ; sans rien à mélanger, la première passe termine la scène seule (la distance n'est alors pas gardée).
- `billboard.frag` atténue un billboard sur `soft` mètres devant la surface derrière lui ; sans `soft`, la texture n'est pas lue et le rendu est **identique au bit près** (démos 2D, 3D et rejeu inchangés). `"soft"` par émetteur dans les effets ; la fumée de mort (0,5 m) et la poussière des pas (0,3 m) l'utilisent.
- **Coût** : la démo 3D a le même temps GPU de scène qu'au jalon 6 (0,54 ms) ; la tranche aussi (partie 10).
- **Nouvelle capture de la tranche** : `f54c9067ab40` (la fumée et la poussière douces) ; avec `soft` à 0 dans les deux effets, elle redonne exactement l'ancienne (`f5421305c50a`). MSL exportés (`export_msl_shaders`), Debug sans message de validation en sans anticrénelage, FXAA et MSAA 4x.

---

## 10. Performances et robustesse

### But

Vérifier que les outils du jalon ne coûtent rien quand on ne s'en sert pas, peu quand on s'en sert, et que les fichiers mauvais ou abîmés ne font jamais planter le jeu.

### Tâches

- [x] **Coûts** : profiler allumé et éteint (CPU par image de la tranche), console fermée, journal écrit ; chargement de toutes les tables ; sauvegarde et chargement de la tranche (temps, taille du fichier en JSON et en CBOR) ; éditeur sur la carte de 100 × 100 avec 3 000 objets (images par seconde, temps d'une peinture).
- [x] **Robustesse** : une série de fichiers abîmés (table invalide, référence cassée, sauvegarde tronquée, carte v2 avec un type d'objet inconnu, JSON vide) : chacun donne un message clair et le jeu continue (ancienne version gardée, emplacement marqué illisible…).
- [x] Comparer la tranche et la démo 3D avant et après le jalon (même méthode qu'aux jalons 5 et 6).

### Questions à se poser

- **Quels budgets ?** Proposition à confirmer : profiler allumé **< 0,05 ms** par image ; sauvegarde de la tranche **< 50 ms** (aucun à-coup visible à la sauvegarde automatique, sinon un fil) ; chargement d'une sauvegarde **< 1 s** au-delà du chargement de la scène ; toutes les tables du jeu (quelques centaines de lignes) **< 100 ms**.

### Validation

Les mesures sont notées ; aucun fichier de la série abîmée ne fait planter ; la tranche et la démo 3D ne ralentissent pas.

### Implémentation réalisée (partie 10)

Release, Windows, RTX 4070 Ti SUPER.

| Mesure | Résultat | Budget |
|---|---|---|
| Profiler allumé / éteint, CPU par image (`--report --no-vsync`) | démo 3D 1,20 / 1,20 ms ; tranche 1,11 / 1,09 ms (écart dans le bruit) | < 0,05 ms |
| Sauvegarde de la tranche (300 personnages, tick 250) | **22 ms** en JSON (1,1 Mo : état 7 ms, encodage 10 ms, écriture 5 ms) ; **19 ms** en CBOR (288 Ko) | < 50 ms |
| Chargement d'une sauvegarde | lecture et décodage 22 ms (JSON) / 14 ms (CBOR) ; le monde rendu en 3,5 ms quand les modèles sont déjà chargés, 0,35 s sinon (modèles des 300 personnages) | < 1 s de plus que la scène |
| Toutes les tables (`personnages`) | 0,3 ms | < 100 ms |
| Éditeur sur la tranche (100 × 100 cases, 3 300 objets) | vue entière construite en 135 ms (une fois) ; un coup de pinceau reconstruit en 2,4 ms ; 2,4 ms par image | fluide |

- **Avant / après le jalon** (commit du jalon 6 construit à part, mêmes scènes, `--report --gpu-timing --no-vsync`) : démo 3D CPU 2,31 → 2,31 ms, GPU 1,22 → 1,22 ms (scène 0,54 → 0,54 : la seconde cible et les deux passes ne coûtent rien de mesurable) ; tranche (rejeu de référence) CPU 1,93 → 1,95 ms, GPU 0,93 → 0,93 ms. Sans `--gpu-timing` : démo 3D 1,20 ms (1,21 au jalon 6), tranche 1,10 ms (1,13).
- **Fichiers abîmés** : aucun ne fait planter.
  - **Lecteurs** (test `test_robustness.cpp`) : cartes, descriptions d'animation et effets vides, tronqués, d'un autre type ou avec un BOM donnent tous un message qui nomme le fichier ; une sauvegarde coupée ou modifiée est refusée en JSON comme en CBOR.
  - **À l'exécution** (copie des assets du build) :
    - objets de carte abîmés : ignorés avec un message ;
    - effet abîmé : remplacé par un effet vide (`Assets`), la partie continue ;
    - table tronquée au démarrage : le programme s'arrête avec le fichier, la ligne et la raison (en jeu, un rechargement abîmé garde l'ancienne version) ;
    - carte de la tranche vide : la tranche ne démarre pas, avec le message (dans les menus, il s'affiche à l'écran) ;
    - sauvegarde tronquée : refusée, avec repli sur sa copie de secours depuis les menus (partie 11).

---

## 11. Outils et données dans la tranche jouable

### But

Que la tranche du jalon 6 profite de tout le jalon.

### Contenu

- **Données** : une table **`monstres`** (squelette guerrier, squelette sbire : modèle, taille, rayon, vitesse, points de vie, dégâts, portée de vue, cadence de coups, effets joués) et une table **`effets_de_coup`** ou équivalent ; le héros lui aussi décrit par une ligne. Modifier un chiffre pendant le jeu change les créatures (vitesse, vie maximale) aussitôt.
- **Console** : `god`, `spawn <monstre> [n]`, `tp <x> <z>`, `kill all`, `heal`, `fog on/off`, `set time.scale 0.5`, `save`, `load`.
- **Profiler** : zones des systèmes du jalon 6 et de l'animation, visibles dans la fenêtre Profiler pendant un combat.
- **Sauvegarde** : « Sauvegarder » dans le menu de pause (emplacements), « Continuer » et « Charger » à l'écran titre, sauvegarde automatique toutes les deux minutes ; le **test d'exactitude** en référence (capture après chargement = capture sans interruption).
- **Carte** : `tranche.json` en v2, décor et apparitions posés ; une salle ajoutée dans l'éditeur.
- **Référence** : nouvelles captures si le contenu change (en expliquant l'écart), et une capture « après chargement » qui doit être identique à la capture sans chargement.

### Tâches

- [x] Écrire les tables de la tranche et y brancher héros et créatures.
- [x] Enregistrer les commandes et les variables de la tranche.
- [x] Brancher la sauvegarde (sérialiseurs des composants de la tranche : `Health`, `Hunter`, `Stature`…) et les menus.
- [x] Passer la carte en v2 (décor et apparitions) et la retoucher dans l'éditeur.
- [x] Refaire le rejeu de référence et les captures ; ajouter le test d'exactitude de la sauvegarde.

### Validation

La tranche se joue du titre au retour au titre en passant par une sauvegarde et un chargement ; le test d'exactitude passe deux fois de suite ; les commandes de la console fonctionnent ; une valeur de table modifiée pendant le jeu se voit.

### Implémentation réalisée (partie 11)

- **Données** : la table `personnages` (créée en partie 2) porte aussi les **effets joués** (`effet_touche`, `effet_mort` : étincelles et fumée des squelettes, sang du chevalier) ; vitesses, rayons, dégâts, portée, vue et cadence relus à chaque tick (une valeur changée pendant le jeu se voit aussitôt).
- **Console** (commandes de jeu de la tranche, en file et dans les rejeux) : `god [0|1]`, `heal`, `tp <x> <z>` (case libre la plus proche), `spawn <personnage> [nombre]` (autour du héros, aux mêmes places sur toutes les machines, avec complétion), `kill all`, `fog on|off` ; `set time.scale 0.5` (moteur) ; `save [emplacement]` et `load [emplacement]` (outils, dans les menus de la tranche). Invulnérabilité et brouillard sont sauvegardés.
- **Profiler** : zones `tranche : entrées`, `chasse`, `déplacements`, `vision`, `animation`, autour de celles du moteur.
- **Sauvegardes** (`StatesDemo`) :
  - **Emplacements** : dans les préférences (`sauvegardes/`) ; « Sauvegarder » dans la pause, avec trois emplacements, chacun avec sa vignette, sa date, sa durée de jeu, la vie du héros et les créatures debout. La vignette est prise à l'ouverture de la pause, sans le menu, et réduite à 320 pixels.
  - **Écran titre** : « Jouer » (toujours le premier : les rejeux le confirment), « Continuer » (la sauvegarde lisible la plus récente), « Charger » (tous les emplacements, illisibles signalés, copie de secours lue si besoin), « Quitter ».
  - **Sauvegarde automatique** : toutes les deux minutes de jeu, dans deux emplacements tour à tour (`save.auto` en secondes, 0 : jamais).
  - **Format** : JSON, ou CBOR avec `save.binary`.
  - **Échec** : une sauvegarde illisible ou d'une autre graine ramène au titre avec la raison.
- **Carte** : `tranche.json` en v2, décor et apparitions posés (partie 8).
- **Références automatisées** : `python tools/tests/check_references.py [build] [--record] [--quick]` lance les captures de référence (comparées à `tests/data/references.json`, par plateforme), le **test d'exactitude** (sauvegarde aux ticks 120 et 250, A = B = C) et l'auto-test de l'éditeur ; code de sortie 0 si tout correspond. Résultat : **tout correspond**, deux fois de suite.
- **Captures** : démo 2D `bd91e8b2c616`, démo 3D `fdc076d9c3ae`, rejeu `952477744ffb` **inchangées** ; tranche **`f54c9067ab40`** (particules douces, partie 9) ; exactitude `063b80d5f127`.
- **Pas fait par moi** : la tranche jouée à la main du titre au titre en passant par une sauvegarde et un chargement ; une salle ajoutée dans l'éditeur.

---

## 12. Critères de fin de jalon

Le jalon est terminé quand **tout** ce qui suit est vrai :

- [x] Les **tables** se lisent depuis des dossiers JSON, avec défauts, héritage simple et références vérifiées ; chaque erreur nomme le fichier, la ligne et le champ ; elles se rechargent à chaud ; `--check-data` les vérifie sans lancer le jeu ; leur **forme binaire** donne les mêmes structures.
- [x] La **console** (commandes, variables, complétion, historique) et le **journal** dans un fichier fonctionnent ; les commandes qui touchent au monde passent par la logique et par les rejeux.
- [x] Le **profiler** montre les zones des systèmes par image et par tick, avec un graphe des pics, et écrit une capture lisible par un visualiseur ; **Tracy** se branche par une option ; les overlays du monde sont commandés par des variables.
- [x] La **sauvegarde** écrit des fichiers versionnés, sûrs (écriture atomique, somme de contrôle, sauvegarde précédente gardée), migrables, en JSON et en binaire compact ; le menu montre les emplacements.
- [x] Une partie **chargée** se poursuit **exactement** comme une partie jamais interrompue (test d'exactitude automatisé sur la tranche).
- [ ] L'**éditeur de cartes** peint les tuiles, place les points et les objets, édite des morceaux avec connecteurs, annule et refait, vérifie, enregistre au format v2 et lance un essai *(fait et vérifié par son auto-test)* ; la carte de la tranche y a été retouchée *(essai à la main de l'utilisateur)*.
- [x] Le **graphe d'animation** est en données (machine à états de l'`AnimationSet`) et la tranche l'utilise ; les **particules douces** fonctionnent.
- [x] Les coûts de la partie 10 sont mesurés et notés ; aucun fichier abîmé ne fait planter le jeu.
- [x] Aucun avertissement de compilation, aucun message de la couche de validation du GPU.
- [x] La logique pure (tables, lecture typée, console, profiler, sauvegarde, outils de l'éditeur) a ses tests unitaires ; les captures de référence des jalons précédents sont inchangées ou leurs changements expliqués.
- [x] Les nouvelles bibliothèques et les nouveaux assets sont dans `credits.json` (fenêtre « À propos »).
- [x] Les décisions de la section [Décisions à consigner](#décisions-à-consigner) sont remplies.
- [x] La documentation des nouveaux fichiers est ajoutée à [FICHIERS_DU_PROJET.md](FICHIERS_DU_PROJET.md), et les vérifications Mac sont regroupées dans [TEST_MAC.md](TEST_MAC.md) *(à faire sur le Mac)*.

---

## 13. Risques principaux

| Risque | Impact | Parade |
|---|---|---|
| Une partie rechargée qui diverge (état oublié, ordre d'EnTT) | Sauvegardes qui « trichent », bugs impossibles à reproduire | Test d'exactitude automatisé sur une longue séquence avec combats ; composants sans sérialiseur signalés ; ordres triés partout |
| Sauvegarde corrompue par une coupure ou un plantage | Perte de progression du joueur | Écriture dans un temporaire puis renommage atomique, sauvegarde précédente gardée, somme de contrôle ; coupure simulée en test |
| Format de sauvegarde ou de carte qui évolue sans migration | Anciennes sauvegardes et cartes illisibles | Version par fichier et par section, migrations en chaîne testées |
| Tables trop libres (champs mal tapés acceptés) | Erreurs silencieuses dans l'équilibrage | Lecture typée stricte, champs inconnus signalés, `--check-data` |
| Éditeur qui grossit sans fin | Jalon interminable | Périmètre écrit (tuiles, points, morceaux, objets, annuler, vérifier, enregistrer, essayer) ; le reste quand le jeu en a besoin, pas avant |
| Graphe d'animation en données trop riche (un langage) | Jalon long, graphe difficile à déboguer | Conditions structurées simples, vue DEBUG du graphe, tests ; pas d'expressions libres |
| Profiler qui fausse ce qu'il mesure | Mauvaises décisions d'optimisation | Zones par système, coût mesuré, éteint par défaut |
| Commandes de console qui cassent le déterminisme | Rejeux et captures qui divergent | Commandes de jeu en file, au tick suivant, enregistrées dans les rejeux ; commandes d'outil sans effet sur la logique |
| Rechargement à chaud d'une table pendant une partie | Entités qui pointent vers des lignes disparues | Rechargement refusé avec message si une ligne utilisée disparaît ; valeurs de définition par référence, d'état dans les composants |

---

## Décisions à consigner

Remplies le 2026-10-03 avant le début du travail (questionnaire) ; à compléter au fil du jalon.

| Sujet | Décision | Raison |
|---|---|---|
| Le moteur connaît-il les champs des tables ; données ou code ; script | Le moteur fournit **tables, lecture typée, références** ; le jeu décrit ses champs. Ce qui se **règle** en données, ce qui **décide** en code ; **pas de langage de script** | Recommandation, sans objection de l'utilisateur (2026-10-03) ; un seul langage pour la logique, déterminisme préservé |
| JSON Schema ou lecture typée en C++ ; forme binaire livrée | **Lecture typée en C++** (la conversion est la vérification ; erreurs fichier > ligne > champ) ; **JSON en développement, tables compilées en binaire pour la version livrée** | Validé par l'utilisateur (2026-10-03) ; ce que font Diablo II, Path of Exile, Unreal |
| Un fichier ou un dossier par table ; héritage | **Dossier par table** ; héritage à **un parent** (`base`), plusieurs niveaux, cycles refusés, un champ redonné **remplace** celui du parent | Validé par l'utilisateur (2026-10-03) ; comme Path of Exile et Grim Dawn |
| Rechargement à chaud et entités vivantes ; identifiants | Valeurs **de définition par référence** vers la ligne (changent aussitôt), **état** dans les composants ; ligne utilisée supprimée : rechargement refusé avec message. Identifiants **chaînes** dans les fichiers et les sauvegardes, **indices** en mémoire | Validé par l'utilisateur (2026-10-03) (rechargement) ; Recommandation, sans objection de l'utilisateur (2026-10-03) (identifiants) |
| Variables (registre unique), commandes de jeu et d'outil, console en version finale | **Registre unique** de variables nommées (`fog.enabled`) ; commandes **de jeu en file, au tick suivant, enregistrées dans les rejeux** ; commandes d'outil immédiates ; console **présente mais fermée** en version livrée, **outils seulement** | Validé par l'utilisateur (2026-10-03) (commandes, console) ; Recommandation, sans objection de l'utilisateur (2026-10-03) (registre) |
| Format et rotation du journal | Texte, une ligne par message (heure, niveau, texte), `journal.txt` dans les préférences, précédent gardé, rotation à la taille, protégé entre fils | Recommandation, sans objection de l'utilisateur (2026-10-03) |
| Profiler maison ou Tracy ; format des captures | **Les deux** derrière des macros à nous : profiler **maison** (fenêtre, graphe des pics, `--report`, export **Chrome Trace**) et **Tracy** en option de compilation | Validé par l'utilisateur (2026-10-03) |
| Profiler dans la version distribuée | Compilé en développement et en Release de mesure, **retiré** de la version distribuée par une option | Recommandation, sans objection de l'utilisateur (2026-10-03) |
| Format de sauvegarde (JSON, binaire, CBOR) ; somme de contrôle | Un modèle (sections `nlohmann::json`), **JSON en développement, CBOR en version livrée**, lecture des deux ; sections versionnées, migrations en chaîne ; **somme de contrôle** | Validé par l'utilisateur (2026-10-03) |
| Écriture atomique (Windows, macOS) et emplacements | Fichier temporaire vidé sur disque puis **renommé** (remplacement atomique propre à chaque OS), sauvegarde précédente gardée ; emplacements + sauvegarde automatique tournante, vignette | Recommandation, sans objection de l'utilisateur (2026-10-03) |
| Ce qui est sauvegardé et ce qui est reconstruit ; identifiants persistants | **L'état** (entités persistantes, composants, grille en différences, exploré, aléatoire, tick) ; **le déductible recalculé** (flow fields, vision, poses, chemins replanifiés) ; `PersistentId` d'un compteur de la partie ; **test d'exactitude** automatisé | Validé par l'utilisateur (2026-10-03) (état) ; Recommandation, sans objection de l'utilisateur (2026-10-03) (identifiants) |
| Éditeur dans le moteur, application à part ou Tiled ; vue | **Dans le moteur**, organisé comme une **application d'outil** à part entière ; son exécutable `editeur` existe **dès ce jalon** (même code que la scène du bac à sable, bibliothèque commune) ; éditeur **de production** (morceaux et connecteurs compris) ; vue du jeu avec caméra de dessus, sans tramage des murs | Validé par l'utilisateur (2026-10-03) ; l'exécutable avancé du jalon 8 au jalon 7 (il ne coûtait qu'une cible de plus) |
| Format de carte v2 (objets) ; objets qui bloquent des cases | Le fichier décrit des **objets (type + propriétés)**, le jeu en fait des entités ; **chaque type choisit** de bloquer ses cases ou de porter un cercle immobile | Validé par l'utilisateur (2026-10-03) |
| Graphe d'animation : format des transitions | Graphe **en données** dans l'`AnimationSet` ; transitions en **conditions structurées** (`param`, `op`, `value`, combinées par toutes / une / non) ; un déclencheur est pris **au plus une fois par couche** et n'attend pas | Validé par l'utilisateur (2026-10-03) ; un ARPG livrable aura des dizaines de monstres. Le déclencheur par couche : un seul « mort » fait tomber le corps et lâcher le haut du corps (décidé à l'implémentation, 2026-10-04) |
| Particules douces : copie de la profondeur, MSAA | La passe des maillages écrit la **distance le long de la vue** dans une seconde cible, **résolue avec le MSAA** ; les billboards la lisent dans une seconde passe | Validé par l'utilisateur (2026-10-03) sous la forme « copie de la profondeur résolue » ; SDL_GPU ne résout pas une profondeur multiéchantillonnée : la seconde cible donne le même résultat, sans coût mesurable (2026-10-04) |
| Budgets (profiler, sauvegarde, chargement, tables) | Profiler allumé **< 0,05 ms** par image ; sauvegarde **< 50 ms** (sinon un fil) ; chargement d'une sauvegarde **< 1 s** en plus de la scène ; toutes les tables **< 100 ms** | Validé par l'utilisateur (2026-10-03) |
