# Jalon 6 - Monde et déplacement

Retour à la [roadmap générale](ROADMAP.md). Jalon précédent : [Jalon 5 - Animation 3D](JALON_5_ANIMATION_3D.md). Description des fichiers existants : [Fichiers du projet](FICHIERS_DU_PROJET.md).

## Objectif

Donner au monde des **règles physiques simples** et aux personnages les moyens de s'y **déplacer** : des **collisions** sur le plan du sol (murs de la carte, obstacles, personnages entre eux), un **pathfinding** qui mène le héros là où l'on clique et les monstres jusqu'au héros, des **requêtes spatiales** (qui est dans ce rayon, ce cône, sur cette ligne ; est-ce que je vois ce point), des **effets de particules** (feu, étincelles, sang, poussière, magie) et un **brouillard de guerre** (ce qui a été exploré, ce qui est en vue).

À la fin du jalon, la **tranche jouable** doit montrer : le héros qui **contourne les murs** pour aller où l'on clique (même dans une autre salle), qui **glisse le long des murs** au clavier au lieu de s'arrêter net ; des monstres qui **repèrent** le héros, le **poursuivent** par le chemin le plus court en se **bousculant** sans se traverser, et l'**entourent** au lieu de s'empiler ; des **étincelles** à chaque coup, des **flammes** sur les braseros, un **nuage** à la mort ; une carte **sombre** hors de ce qui a été exploré, des monstres **cachés** derrière les murs, et les murs qui **s'effacent** quand ils masquent le héros.

**Pourquoi maintenant ?** Les jalons 4 et 5 ont donné un monde en entités et des personnages qui marchent vraiment (vitesse de lecture accordée au déplacement, pas sur les événements). Il leur manque de savoir **où** marcher. Le jalon 3 a préparé les billboards « qui accueilleront les particules », et la `TileMap` du jalon 2 porte déjà les propriétés `walkable` et `opaque` prévues pour ce jalon. Côté jeu, la décision « déplacement au clic ou au clavier » ([DECISIONS_DE_CONCEPTION.md](../../ARPG/ARPG_doc/DECISIONS_DE_CONCEPTION.md)) demande un pathfinding, et l'IA des monstres (ARPG, étape 2) a besoin de poursuivre et de voir.

## Prérequis

- Le [jalon 5](JALON_5_ANIMATION_3D.md) est terminé (sous Windows ; les vérifications Mac restent dans [TEST_MAC.md](TEST_MAC.md)).
- Lire, pour le pathfinding : les pages d'Amit Patel (*Red Blob Games*) sur **A\***, les **grilles** et les **flow fields** ; pour la visibilité : son article sur la **visibilité 2D** et l'article *Symmetric Shadowcasting* d'Albert Ford *(à vérifier : titres exacts)*.
- Selon la décision de la partie 4 : la documentation de **Recast & Detour** (`recastnavigation`).
- Selon la décision de la partie 3 : la documentation de **Box2D v3** (partie *character mover*).

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
2. [La grille du monde et les cartes de test](#2-la-grille-du-monde-et-les-cartes-de-test)
3. [Collisions sur le plan du sol](#3-collisions-sur-le-plan-du-sol)
4. [Pathfinding](#4-pathfinding)
5. [Déplacement des personnages](#5-déplacement-des-personnages)
6. [Requêtes spatiales et lignes de vue](#6-requêtes-spatiales-et-lignes-de-vue)
7. [Effets de particules](#7-effets-de-particules)
8. [Brouillard de guerre et visibilité](#8-brouillard-de-guerre-et-visibilité)
9. [Outils de debug et scène de test](#9-outils-de-debug-et-scène-de-test)
10. [Performances](#10-performances)
11. [Le monde dans la tranche jouable](#11-le-monde-dans-la-tranche-jouable)
12. [Critères de fin de jalon](#12-critères-de-fin-de-jalon)
13. [Risques principaux](#13-risques-principaux)
14. [Décisions à consigner](#décisions-à-consigner)

---

## 1. Vue d'ensemble et ordre de travail

### Où on part

Après le jalon 5 :

- **Carte** : `TileMap` (jalon 2) : une grille de `TileId` en calques, sans GPU ; les propriétés sont dans le `Tileset` (`TileType::walkable`, `TileType::opaque`, cette dernière jamais lue). `walkable(tileset, case)` est vrai si aucun calque ne bloque. Pas de format de fichier : les cartes sont construites en code.
- **Échelle** : **1 case = 1 m** (décision du jalon 3) ; la case (i, j) couvre x ∈ [i, i+1[, z ∈ [j, j+1[ ; le sol est à y = 0.
- **Démo 3D** : carte de 100 × 100 cases, murs de 1,5 m en salles de 10 × 10 avec des ouvertures (`demo_wall`), 300 créatures. Le déplacement teste **un point** : la case de la position suivante est-elle praticable ? Sinon le héros **s'arrête** (« pas encore de pathfinding ») et les créatures font demi-tour. Les personnages se **traversent**.
- **Tranche** : clic sur le sol = destination en ligne droite, maintenu = suit le pointeur ; clavier = un pas devant soi ; clic sur une créature hors de portée = marcher jusqu'à elle. Les créatures errent sans voir le héros.
- **Billboards** (jalon 3) : `renderer.billboards()`, face à la caméra ou debout, HDR, mode additif, triés du plus loin au plus proche, une texture par lot.
- **Monde** : `moteur::World` (EnTT, un registre par scène), `Transform`, `Parent`, `BoneAttachment` (points nommés `right_hand`, `head`…), `collect()` au dessin. La logique tourne à **pas fixe** (60 Hz par défaut) et doit rester **déterministe** : les rejeux d'entrées et les captures de référence en dépendent.

Les principes restent ceux de la roadmap : le moteur ne connaît rien de l'ARPG (il connaît des **cases**, des **cercles**, des **chemins**, des **émetteurs**, pas des « monstres » ni des « sorts »), et ce qui touche à la logique est **déterministe**. Les particules, purement visuelles, sont la seule exception.

### Dépendances entre les parties

```
2. Grille du monde et cartes de test
               |
      +--------+------------------+
      |                           |
3. Collisions              6. Requêtes spatiales
      |                     et lignes de vue
4. Pathfinding                    |
      |                    8. Brouillard de guerre
5. Déplacement                    |
      |                           |
      +-------------+-------------+
                    |                     7. Particules (indépendantes,
     9. Outils de debug et scène de test     peuvent se faire à tout moment)
                    |
            10. Performances
                    |
            11. Tranche jouable
```

- La **grille** vient d'abord : collisions, chemins et visibilité lisent la même carte, et il faut des cartes de test (couloirs, culs-de-sac, salles) écrites dans un fichier plutôt qu'en code.
- Les **collisions** (un cercle contre la grille) précèdent le **pathfinding** : le chemin doit être praticable pour un cercle de cette taille, pas pour un point.
- Le **déplacement** réunit les deux : suivre un chemin, éviter les autres, glisser sur les murs, et donner la vitesse réelle à l'animation.
- Les **requêtes** (cercle, cône, rayon sur la grille) servent la visibilité, la tranche (portée des coups) et plus tard le jeu (zones d'effet, projectiles).
- Les **particules** ne dépendent de rien d'autre que des billboards : elles peuvent s'intercaler quand on veut changer d'air.

### Ce qui peut se faire en pause du moteur

Tout ce jalon, sauf les particules et l'affichage du brouillard, est de la **logique pure**, testable en ligne de commande (c'est d'ailleurs listé dans [PREPARATION_SANS_MOTEUR.md](../../ARPG/ARPG_doc/PREPARATION_SANS_MOTEUR.md) côté ARPG) :

- **Lecture d'une carte** depuis un fichier texte, sortie ASCII pour la regarder.
- **Collisions** cercle contre grille et cercle contre cercle, glissement, cas des coins.
- **A\*** sur la grille, lissage du chemin, chemin vers le point praticable le plus proche ; **flow field** ; tests avec des cartes ASCII et des chemins attendus.
- **Lignes de vue** et **champ de vision** (shadowcasting), comparés à des cartes ASCII attendues.
- **Génération de cartes** (côté jeu, plus tard) : elle produira une `TileMap`, le moteur n'a rien à savoir de plus.

### Estimation indicative

Pour un dev solo à temps partiel. À prendre comme un ordre de grandeur, pas comme un engagement.

| Partie | Estimation |
|---|---|
| 2. Grille du monde et cartes de test | 1 semaine |
| 3. Collisions | 2 semaines |
| 4. Pathfinding | 2 à 3 semaines |
| 5. Déplacement des personnages | 2 semaines |
| 6. Requêtes spatiales et lignes de vue | 1 semaine |
| 7. Particules | 2 à 3 semaines |
| 8. Brouillard de guerre et visibilité | 2 semaines |
| 9. Outils de debug et scène de test | 1 semaine |
| 10. Performances | 1 semaine |
| 11. Tranche jouable | 1 à 2 semaines |
| **Total** | **environ 3 à 4 mois** |

### Liens avec les autres jalons

- **Animation** (jalon 5) : le déplacement **décide**, l'animation **suit** (`Animator::set_move_speed` avec la vitesse réellement parcourue, déjà en place dans la tranche) ; un personnage bloqué contre un mur doit donc passer au repos tout seul. Les particules partent d'un **point d'attache** (étincelles sur l'épée, `right_hand`) ou d'un **événement** (poussière au pas, sang à l'impact).
- **Rendu 3D** (jalon 3) : la **texture de profondeur** de la passe de scène permet des particules « douces » (qui ne coupent pas net le sol) ; le brouillard s'applique dans le shader des maillages et des billboards ; l'effacement des murs devant le héros touche les instances de `MeshBatcher`.
- **Outils et données** (jalon 7) : l'**éditeur de cartes** écrira le format de fichier choisi ici ; les **émetteurs de particules** en JSON profiteront du rechargement à chaud (déjà assuré par le gestionnaire d'assets) ; la **sauvegarde** écrira les cases explorées.
- **Côté ARPG** : la **génération procédurale** des zones produira une `TileMap` ; l'**IA des monstres** (agro, poursuite, attaque) s'écrira avec les briques de ce jalon (champ de vision, flow field, requêtes) ; les **projectiles** et les **zones d'effet** utiliseront les requêtes spatiales.

### Questions générales

- **Un monde plat ?** Le gameplay se passe sur **un plan** (y = 0), les décors (murs, ruines, escaliers décoratifs) sont en 3D par-dessus. C'est ce que font la plupart des ARPG isométriques pour la logique, même quand le décor a du relief. Recommandation : **plan unique** pour ce jalon ; des **niveaux** (ponts, étages, pentes jouables) seraient une extension (une hauteur par case, ou plusieurs grilles reliées), à décider le jour où une zone en a besoin.
- **Physique ou règles de déplacement ?** Un ARPG n'a pas besoin de physique (masses, rebonds, empilements) : des cercles qui avancent, glissent sur les murs et se repoussent suffisent. La roadmap garde la physique 3D (Jolt) « plus tard, si le gameplay en a besoin » (cadavres qui volent, objets qui tombent : cosmétique, donc hors de la logique).
- **Déterminisme** : positions et vitesses restent en `float` (comme aux jalons précédents), mais la logique de ce jalon s'interdit ce qui varie d'une machine à l'autre : pas de `sin`/`cos`/`atan2`/`exp` de la bibliothèque standard dans les décisions (les quatre opérations et `sqrt` sont exactement arrondies en IEEE 754, les fonctions transcendantes non), un **ordre de traitement fixe** (entités triées, voisins parcourus dans un ordre défini), des **coûts entiers** pour les chemins et des **égalités départagées** par une règle écrite. Voir les pièges de la partie 3.

---

## 2. La grille du monde et les cartes de test

### But

Une **grille de jeu** unique que lisent les collisions, les chemins et la visibilité, et des **cartes de test** écrites dans des fichiers lisibles à l'œil, chargées par le gestionnaire d'assets et rechargées à chaud.

### Tâches

- [x] Une vue « logique » de la carte : pour chaque case, **praticable** et **opaque** combinés sur tous les calques, calculés une fois (`NavGrid` ou nom à choisir), et un **numéro de version** qui change quand une case change (porte ouverte, mur détruit) pour invalider chemins et champs de vision.
- [x] Un **format de fichier** de carte (voir les questions), un asset `assets.tile_map(...)` avec rechargement à chaud, et une sortie ASCII (`to_ascii`) pour les tests et le debug.
- [x] Des **cartes de test** versionnées dans `assets/maps/` : couloir en zigzag, cul-de-sac, salle à plusieurs portes, labyrinthe, champ de piliers, passage d'une case de large, grande arène ouverte, île inaccessible.
- [x] Construire le **décor 3D** d'une carte (sol, murs, piliers) à partir de ses tuiles, dans la scène de test (le code de la démo 3D sert de point de départ ; un kit de modules 3D sera pour plus tard).
- [x] Les **obstacles qui ne sont pas des tuiles** (brasero, colonne ronde, coffre) : décider s'ils bouchent des cases ou s'ils sont des formes à part (voir la partie 3).

### Questions à se poser

- **Quel format pour les cartes de test ?** Options :
  - **Du code**, comme aujourd'hui : rien à écrire, mais illisible et pas éditable sans recompiler.
  - **Un JSON avec une carte ASCII** : une légende (`"#": "mur"`, `".": "sol"`, `"o": "pilier"`), des lignes de caractères, et des points nommés (`"depart"`, `"spawn"`). Lisible, éditable à la main, diffable dans Git.
  - **Tiled** (`.tmj`, JSON ; éditeur libre) : un vrai éditeur de cartes tout de suite, mais un format riche dont on n'utiliserait qu'une partie, et le jalon 7 prévoit un éditeur maison.
  
  Recommandation : **JSON + ASCII**, versionné, chargé comme asset. Le jalon 7 (éditeur) écrira le même format ou en ajoutera un binaire si les cartes deviennent grandes ; Tiled reste une option si l'éditeur maison tarde.
- **La grille de jeu est-elle la `TileMap` ?** Recommandation : **oui**, une case = une tuile de 1 m. Une grille plus fine (50 cm) n'est utile que si les murs doivent être plus fins qu'une case ; les collisions des personnages, elles, sont **continues** (des cercles), pas des cases (partie 3). La `TileMap` ne dépend pas de la taille d'une case : on pourra affiner plus tard.
- **Murs fins (entre deux cases) ou murs pleins (une case) ?** Les murs pleins sont le plus simple pour les collisions, les chemins et la visibilité. Recommandation : **murs pleins** ; les décors plus fins (grilles, balustrades) seront des obstacles à part, ou une case non praticable mais non opaque.
- **Que contient une case en plus ?** Praticable et opaque viennent du `Tileset`. On peut vouloir plus tard : un **coût** de passage (boue, eau peu profonde), une **hauteur** (si niveaux), une **zone** (pour l'IA). Recommandation : **praticable et opaque seulement** ; un coût par case est simple à ajouter à l'A\* le jour où il sert.

### Pièges connus

- Les cartes ASCII ont l'axe des lignes vers le **bas** ; la grille a z vers le « sud ». Décider une fois que la ligne 0 est z = 0 (ou l'inverse), l'écrire dans le format et le vérifier sur une carte asymétrique.
- Les fins de ligne Windows (`\r\n`) dans une carte ASCII : une colonne de plus sur certaines lignes. Retirer le `\r`, refuser les lignes de longueurs différentes avec un message qui donne la ligne.
- Une carte rechargée à chaud **pendant** qu'un personnage est dans un mur nouvellement posé : le décoincer (partie 3) plutôt que planter.

### Validation

Les cartes de test se chargent, s'affichent en 3D et en ASCII, et se rechargent à chaud ; des tests unitaires lisent chaque carte et vérifient quelques cases et points nommés.

### Implémentation réalisée (partie 2)

- **`MapData`** (`map_data.hpp/.cpp`) : une carte lue d'un JSON versionné, format en commentaire dans l'en-tête : `tiles` (types par nom : `walkable`, `opaque`, `region`), `legend` (un caractère = une tuile par calque, et un point nommé facultatif), `rows` (la ligne 0 est z = 0, la colonne 0 est x = 0), `description`. Ids des tuiles dans l'ordre alphabétique des noms. Erreurs avec le fichier et, pour une case, sa colonne et sa ligne ; `\r` retiré ; deux caractères de même sens refusés, pour que **`to_ascii()`** réécrive exactement les lignes lues. Points nommés : toutes leurs cases, dans l'ordre de lecture (`points(nom)`, `point(nom)`).
- **Asset** `assets.map(chemin)` (type 11 de DEBUG > Assets), rechargé à chaud ; `MapData::revision()` augmente à chaque rechargement, pour que la scène reconstruise ce qu'elle a tiré de la carte.
- **`NavGrid`** (`nav_grid.hpp/.cpp`) : praticable et opaque par case, tous calques réunis ; une case **sans aucune tuile est un trou** (non praticable, transparente) ; hors de la grille, rien n'est praticable et tout est opaque. `refresh(map, tileset, case)` et `set(case, ...)` ne changent `version()` que si la case change. `cell_at()` et `cell_centre()` passent du plan aux cases. `to_ascii()` : `.` `#` `x` `%`.
- **Cartes de test** dans `assets/maps/` (versionnées) : `zigzag`, `cul_de_sac`, `salle_portes` (portes de 1 à 4 cases), `labyrinthe` (parfait, 15 × 10 couloirs), `piliers`, `passage_etroit` (1 et 2 cases), `arene` (64 × 64, barrières basses, herbes hautes), `ile` (inaccessible) ; plus `tranche` (partie 11). Le JSON est la source (écrit une fois par un script, puis modifiable à la main).
- **Scène « Monde »** (`world_test.cpp`, DEBUG > Tests moteur, `--world [carte]`, `--menu-test 11`) : la carte en 3D (sol en deux tons par blocs de 16 × 16, trous laissés vides au-dessus d'un fond sombre, murs de 1,5 m, piliers de 2,5 m reconnus au nom de leur tuile, barrières basses, herbes hautes), les points nommés (carrés lumineux et noms), les axes de l'origine (la ligne 0 du fichier est bien loin de la caméra, vers z = 0), la case survolée (indices, tuiles par calque, propriétés), la grille en caractères, un **clic droit** pour poser ou retirer un mur (nouvelle version de la grille), et le rechargement à chaud (fichier modifié : scène reconstruite).
- **Obstacles qui ne sont pas des tuiles** : décidé, des **tuiles** tant qu'ils occupent une case (le brasero de la tranche bouche sa case) ; un obstacle plus fin pourra être un `Collider` immobile (`push_weight = 0`) de la partie 3.
- Tests : `test_map_data.cpp` (lecture, ordre des ids, points, réécriture, erreurs, `NavGrid`, versions, et **chaque carte de `assets/maps/`** relue et réécrite à l'identique, départ praticable).

---

## 3. Collisions sur le plan du sol

### But

Les personnages sont des **cercles** sur le plan : ils ne traversent ni les murs ni les obstacles, **glissent** le long des murs au lieu de s'arrêter, et ne se **traversent** pas entre eux. Le tout déterministe et assez rapide pour quelques centaines de personnages.

### Tâches

- [x] Composant **`Collider`** : rayon (en m), **couches** et **masque** (qui bloque qui : le héros traverse-t-il les projectiles ? les monstres volants traversent-ils les piliers ?), et un drapeau **statique** pour les obstacles.
- [x] **Cercle contre grille** : déplacer un cercle d'un vecteur en tenant compte des cases non praticables (cases et coins), avec **glissement** le long du mur (la composante du mouvement qui longe le mur est gardée).
- [x] **Cercle contre cercle** : séparer deux personnages qui se chevauchent ; décider qui pousse qui (voir les questions).
- [x] **Obstacles ronds ou rectangulaires** qui ne sont pas des tuiles (piliers, braseros, coffres), s'ils sont retenus à la partie 2.
- [x] Une **grille de hachage spatial** (les entités rangées par case ou par groupe de cases) pour ne tester que les voisins ; elle sert aussi aux requêtes de la partie 6.
- [x] **Décoincer** un personnage qui se retrouve dans un mur (apparition, carte changée, poussée) : le ramener à la position praticable la plus proche.
- [x] Tests unitaires : glissement contre un mur droit, arrivée dans un coin rentrant et sortant, couloir exactement de la largeur du cercle, deux cercles face à face, dix cercles qui se poussent dans un couloir, résultat identique en changeant l'ordre de création des entités (après tri).

### Questions à se poser

- **Écrire les collisions ou prendre une bibliothèque ?** Options :
  - **À la main** : cercle contre cases (une dizaine de cases autour du cercle, chacune un carré), cercle contre cercle, une grille de hachage. Quelques centaines de lignes, déterministe par construction (ordre fixe), et la grille de la carte est déjà là.
  - **Box2D v3** (MIT, dans vcpkg *(à vérifier : version du port)*) : moteur physique 2D qui annonce un **déterminisme multiplateforme** et propose depuis la 3.1 un *character mover* (déplacer une forme en glissant sur les obstacles) *(à vérifier)*. Mais c'est un moteur de **corps rigides** : il faudrait recopier les murs de la carte en formes Box2D, garder deux mondes synchronisés (EnTT et Box2D), et désactiver la dynamique dont on ne veut pas (masses, rebonds).
  
  Recommandation : **à la main**, malgré la préférence pour l'open source : le problème est petit et propre à la grille, la même grille sert au pathfinding et à la visibilité, et on garde la main sur l'ordre des calculs (le déterminisme). Box2D reste la porte de sortie si le jeu veut un jour de vraies interactions physiques sur le plan (repousser des objets, rebonds de projectiles).
- **Collisions continues ou discrètes ?** À 60 Hz, un personnage à 8 m/s fait 13 cm par tick : moins qu'un rayon, donc pas de traversée de mur. Un **projectile** rapide (40 m/s = 67 cm par tick) peut sauter un coin de mur : les projectiles passent par un **lancer de rayon** sur la grille (partie 6), pas par les collisions des personnages. Recommandation : **discrètes** pour les personnages (un pas par tick, découpé en sous-pas si le déplacement dépasse le rayon), **rayons** pour les projectiles.
- **Qui pousse qui entre personnages ?** Options : séparation **symétrique** (chacun recule de la moitié) ; **masse** ou **priorité** (le héros ne se fait pas pousser par un sbire ; un gros monstre pousse les petits) ; **personnages immobiles** traités comme des obstacles (un monstre qui attaque ne se fait pas déplacer). Recommandation : séparation **pondérée** par un « poids de poussée » entier du `Collider` (0 = inamovible pendant ce tick), symétrique à poids égal ; le jeu fixe les poids (héros lourd, sbire léger, monstre qui frappe inamovible).
- **Séparation dure ou douce ?** Dure : on résout entièrement le chevauchement chaque tick (les foules « tremblent » quand elles sont serrées). Douce : on corrige une fraction par tick (un léger chevauchement possible, mouvement plus fluide). Recommandation : **douce** entre personnages (une part du chevauchement par tick, quelques itérations), **dure** contre les murs (jamais dans un mur).
- **Forme des personnages** : cercles seulement ? Recommandation : **oui**. Les gros monstres longs (serpent, chariot) seraient plusieurs cercles, plus tard.

### Pièges connus

- **Le coin d'une case** : un cercle qui glisse le long d'un mur arrive sur un coin sortant. Tester contre le **carré** de la case (point le plus proche du carré), pas contre deux demi-plans, sinon le cercle accroche ou traverse le coin.
- **Deux murs en angle rentrant** : corriger contre un mur peut pousser dans l'autre. Résoudre en plusieurs passes (ou contre la case la plus proche d'abord), et vérifier à la fin que le cercle n'est dans aucune case pleine.
- **Ordre de traitement** : résoudre les paires de cercles dans l'ordre d'itération d'EnTT donne des résultats qui dépendent de l'ordre de création et de destruction des entités. Trier les paires (par identifiant d'entité, par exemple) avant de résoudre.
- **Deux cercles exactement au même point** : la direction de séparation est indéfinie (division par zéro). Choisir une direction fixe à partir des identifiants, pas au hasard.
- **Un passage d'une case de large** est infranchissable pour un cercle de rayon 0,5 : un rayon de **0,5 exactement** ne passe que si le calcul tolère le contact. Choisir les rayons en conséquence (0,3 à 0,4 pour un humain) et le tester.
- **Position interpolée** : la collision s'applique au tick ; l'image interpole entre deux positions valides. Ne jamais corriger une position au dessin.

### Validation

Dans la scène de test, un cercle piloté au clavier glisse le long des murs et des piliers sans jamais y entrer ni accrocher les coins ; cinquante personnages lâchés dans une salle se répartissent sans se chevaucher (à la tolérance près) ; un rejeu d'entrées redonne les mêmes positions au bit près ; les tests unitaires passent.

### Implémentation réalisée (partie 3)

- **`Collider`** (`collision.hpp/.cpp`) : rayon, couche et masque (deux colliders se repoussent si chacun est dans le masque de l'autre), **poids de poussée** entier (0 : immobile pendant ce tick), `blocked_by_walls`.
- **Cercle contre grille** : `circle_fits`, `push_out_of_walls` (contre le **carré** de chaque case pleine, point le plus proche, quatre passes ; centre dans un mur : par la face la plus proche ; encore coincé : `nearest_fit`, la place libre la plus proche dans un rayon de 4 cases), `move_circle` (pas de la moitié du rayon au plus, sortie des murs après chaque pas : le glissement et le contour des coins en découlent). Une tolérance de 10⁻⁵ m laisse passer un cercle qui touche exactement (rayon 0,5 dans un couloir d'une case).
- **Lancer de cercle** `segment_clear` (distance segment-carré exacte : extrémités et coins), ne parcourant que les cases proches du segment ligne par ligne ; il sert au lissage des chemins et au clic maintenu.
- **`SpatialHash`** (`spatial_hash.hpp/.cpp`) : seaux de 2 m, remplis une fois par tick par `separate_colliders` (dans l'ordre des identifiants), servant aussi aux requêtes de la partie 6.
- **`separate_colliders`** : paires parcourues dans un ordre fixe (index trié, chaque paire une fois), séparation **douce** (60 % du chevauchement par itération, 2 itérations) partagée selon les poids, direction fixe tirée des identifiants pour deux cercles confondus, puis sortie des murs ; seules les positions changées sont écrites (`registry.patch`). Rend des statistiques (colliders, paires testées, chevauchements).
- Le `Collider` et le `Mover` ont leur éditeur dans l'**inspecteur** d'entités.
- Tests (`test_world_grid.cpp`) : coins rentrants et sortants, glissement, sortie d'un mur épais, `nearest_fit`, `segment_clear`, poussée par poids et immobile, **même résultat dans l'autre ordre de création**.

---

## 4. Pathfinding

### But

Trouver un **chemin praticable** pour un cercle d'une taille donnée, du héros jusqu'au point cliqué (même dans une autre salle), et mener **des centaines de monstres** jusqu'au héros sans faire des centaines de recherches par tick.

### Tâches

- [x] **A\*** sur la grille, à huit directions, coûts **entiers**, sans couper les coins (une diagonale exige les deux cases voisines praticables), avec une **limite de nœuds** par recherche.
- [x] **Taille des personnages** : une carte de **dégagement** (distance au mur le plus proche, en cases), pour qu'un gros monstre ne prenne pas un couloir trop étroit pour lui.
- [x] **Lissage** du chemin : retirer les points intermédiaires quand la ligne droite est praticable pour le cercle (lancer de cercle sur la grille), pour éviter la marche « en escalier » des grilles.
- [x] **Destination non praticable** (clic dans un mur, sur une île) : aller au point **atteignable le plus proche** ; destination hors de portée de la limite de nœuds : aller au plus près trouvé.
- [x] **Flow field** (carte des distances de Dijkstra depuis une cible, puis direction vers la case voisine la plus proche) : une seule carte pour tous les monstres qui poursuivent le héros, recalculée quand le héros change de case (ou moins souvent), limitée à un rayon autour de lui.
- [x] **Budget** : nombre de recherches A\* par tick plafonné, les demandes en trop attendent le tick suivant (dans un ordre fixe) ; temps mesuré.
- [x] **Invalidation** : un chemin ou un flow field calculé sur une ancienne **version** de la grille est refait.
- [x] Tests unitaires sur les cartes de test : longueur du chemin attendue, pas de coin coupé, gros personnage qui contourne le couloir étroit, île inaccessible, chemin identique d'une exécution à l'autre (égalités départagées), flow field cohérent avec les distances A\*.

### Questions à se poser

- **A\* sur la grille ou navmesh ?** Options :
  - **A\* sur la grille** (à la main) : la carte est déjà une grille, les tests sont des cartes ASCII, le dégagement gère la taille, les coûts entiers rendent tout déterministe. Coût : beaucoup de nœuds sur une grande carte ouverte (un chemin de 100 m explore quelques milliers de cases : de l'ordre de la milliseconde *(à mesurer)*).
  - **Navmesh avec Recast & Detour** (`recastnavigation`, licence zlib, dans vcpkg *(à vérifier : version)*) : Recast construit un maillage de navigation à partir de triangles, Detour y cherche les chemins (rapides, lisses, peu de nœuds) et **DetourCrowd** gère des foules (évitement, file de demandes). C'est la référence des jeux 3D, et il gère les **niveaux** et les pentes. Mais il faut lui donner des triangles (la grille convertie), reconstruire des tuiles quand la carte change, il travaille en flottants sans garantie de déterminisme entre plateformes *(à vérifier)*, et une partie de sa valeur (le relief, les étages) ne sert pas sur un plan unique.
  - **Hiérarchique** (HPA\* : la carte découpée en blocs, un graphe entre leurs portes) : pour les très grandes cartes ; une optimisation de l'A\* sur grille, pas un autre choix.
  
  Recommandation : **A\* sur la grille**, avec **flow field** pour les poursuites de groupe, à la main : il colle à la grille de la carte et des collisions, reste déterministe, et sa logique est aussi celle que la préparation ARPG prévoit de tester. **Recast & Detour** devient le bon choix le jour où le jeu a des **niveaux** (ponts, étages) : l'interface (`trouver un chemin de A à B pour un rayon r`) doit permettre de le brancher sans toucher au jeu. HPA\* seulement si la partie 10 montre des recherches trop longues.
- **Huit ou quatre directions ? Coûts ?** Huit directions, coût **10** en ligne droite et **14** en diagonale (≈ 10 √2), heuristique **octile** avec les mêmes entiers : chemins proches de l'optimum, aucun flottant dans la recherche. Le lissage retire ensuite les zigzags.
- **Combien de tailles de personnages ?** Recommandation : le dégagement par case donne **toutes** les tailles d'un coup (un personnage de rayon r passe où le dégagement ≥ r) ; pas de classes de taille figées.
- **Flow field ou A\* par monstre ?** Dans un ARPG, des dizaines de monstres courent vers **la même cible** : un flow field depuis le héros sert à tous pour le prix d'une recherche. Les monstres qui vont ailleurs (fuir, patrouiller, rejoindre un point) prennent un A\*. Recommandation : **les deux**, le jeu choisit par monstre.
- **Évitement entre personnages** : RVO/ORCA (anticipation des trajectoires), ou simple **séparation** (partie 3) + flow field ? Recommandation : **séparation** d'abord ; elle suffit pour des monstres qui entourent le héros. RVO seulement si la tranche montre des bouchons gênants (il existe des bibliothèques libres, RVO2, *(à vérifier : licence Apache 2.0, vcpkg)*).
- **Quand recalculer ?** Le chemin du héros : à chaque nouveau clic (clic maintenu : au plus tous les quelques ticks, ou quand la case visée change). Le flow field : quand le héros **change de case**. Les A\* des monstres : quand leur cible a beaucoup bougé ou que la grille a changé de version.

### Pièges connus

- **Couper les coins** : une diagonale entre deux murs en angle passe « à travers » le coin pour un point, pas pour un cercle. Interdire la diagonale si l'une des deux cases voisines est bloquée (et le dégagement en tient compte).
- **Égalités dans la file de priorité** : à coût égal, l'ordre dépend de l'implémentation du tas ; départager par une règle écrite (par exemple le plus petit `h`, puis l'indice de case), sinon deux machines ou deux compilateurs donnent deux chemins.
- **Recherche sans fin** vers une destination inaccessible : sans limite de nœuds, l'A\* explore toute la carte à chaque clic sur une île. Limiter, et retenir la meilleure case trouvée.
- **Le lissage qui coupe un mur** : vérifier la ligne droite pour le **cercle** (deux lignes décalées du rayon, ou un lancer de cercle), pas pour le centre seul.
- **Flow field et minima locaux** : sur une carte de distances correcte il n'y en a pas, mais des monstres poussés hors de la carte calculée (au-delà du rayon) n'ont plus de direction : prévoir un repli (A\* ou aller droit).
- **Un chemin vers un monstre qui bouge** : viser la case de la cible au moment du calcul, et recalculer quand elle s'en éloigne, plutôt qu'à chaque tick.

### Validation

Dans la scène de test : un clic dans une autre salle mène le héros par les portes, sans coupe de coin ni escalier visible ; un clic dans un mur l'amène au plus près ; un gros personnage évite le passage étroit ; deux cents monstres convergent vers le héros par le flow field ; les temps de recherche sont affichés et dans le budget ; les tests unitaires passent.

### Implémentation réalisée (partie 4)

- **`ClearanceMap`** (`pathfinding.hpp/.cpp`) : par case praticable, le rayon du plus grand cercle centré au milieu de la case sans toucher de case pleine (jusqu'à 4 m), cherché anneau par anneau avec arrêt dès que l'anneau suivant ne peut pas faire mieux (401 × 401 : 43 ms en tout cherchant, **3,7 ms** avec l'arrêt) ; `update_around(case)` recalcule le voisinage d'une case changée (testé identique à un calcul complet). Un cercle de rayon r passe où le dégagement ≥ r : toutes les tailles d'un coup.
- **`find_path`** : A\* à huit directions, coûts 10 / 14, heuristique octile, diagonale seulement si les deux cases droites voisines passent, file départagée par (f, h, indice), tableaux de travail gardés d'une recherche à l'autre (tamponnés, pas effacés), **limite de nœuds** (20 000 par défaut). Destination inaccessible ou trop loin : statut `Partial` et la case atteinte la plus proche (plus petit h) ; départ dans un mur : la case libre la plus proche. Le dernier point est la destination exacte quand le cercle y tient. **Lissage** glouton par `segment_clear` sur 24 points au plus.
- **`FlowField`** : Dijkstra depuis la cible avec les mêmes coûts et la même règle des coins, limité par un coût maximal ; `next(case)` et `direction(position)` (vers le milieu de la case suivante, vers la cible exacte sur sa case). Une carte par taille de créature (la scène « Monde » en a deux : 0,35 et 0,7 m).
- **Budget** : `plan_paths` planifie au plus N recherches par tick (8 dans les scènes), dans l'ordre des identifiants ; les autres attendent (état `Waiting`). **Invalidation** : version de la grille sur la carte de dégagement et les champs ; un mur posé replanifie les chemins en cours.
- Mesures (Release, Windows) : labyrinthe de 401 × 401 cases traversé d'un coin à l'autre, **39 221 cases en 3,9 ms** ; flow field sur tout le labyrinthe (79 201 cases), **5,8 ms** ; en jeu, le champ de poursuite est limité à 30 ou 40 cases et recalculé quand le héros change de case (moins de 0,01 ms en moyenne par tick).
- Tests : contournement d'un pilier sans coupe de coin, chemin lissé plus court en points et de même coût, même chemin d'une fois à l'autre, gros cercle refusé par le passage étroit, île (`Partial`, à côté de la barrière), flow field cohérent avec l'A\*.

---

## 5. Déplacement des personnages

### But

Un **système de déplacement** commun au héros et aux monstres : suivre un chemin ou une direction, à une vitesse donnée, en respectant les collisions, tourner vers la direction de marche, s'arrêter proprement, et laisser l'animation suivre la vitesse **réellement parcourue**.

### Tâches

- [x] Composant de déplacement (`Mover` ou nom à choisir) : vitesse maximale, accélération (ou aucune), vitesse de rotation, mode (**aller à un point** par chemin, **suivre un flow field**, **direction directe** pour le clavier ou la manette, **à l'arrêt**), état (en route, arrivé, bloqué, sans chemin).
- [x] **Suivi de chemin** : viser le point suivant, passer au suivant quand il est atteint ou dépassé, ralentir à l'arrivée sans osciller autour du point.
- [x] **Déplacement direct** (clavier) avec **glissement** sur les murs (partie 3), au lieu du « un pas devant soi, sinon on s'arrête » actuel.
- [x] **Orientation** : tourner vers la direction de marche à vitesse limitée, sans `atan2` dans la logique (rotation calculée par vecteurs), ou décider que l'orientation n'est que visuelle (voir les questions).
- [x] **Bloqué** : détecter un personnage qui n'avance plus (coincé par la foule ou par un autre personnage dans un couloir) et le signaler au jeu (redemander un chemin, attendre, attaquer ce qui bloque).
- [x] Un **ordre de systèmes** dans le tick, écrit et documenté : décisions du jeu → chemins → déplacements voulus → collisions → positions finales → animation (vitesse réelle) → événements.
- [x] Brancher la démo 3D et la tranche sur ce système (le `Walker` de la démo disparaît). *La tranche, oui ; la démo 3D sans héros garde son `Walker`, pour que ses captures de référence ne changent pas.*

### Questions à se poser

- **Accélération ou vitesse immédiate ?** Les ARPG répondent au clic **tout de suite** (pas d'inertie perçue). Recommandation : vitesse **immédiate** pour le héros, une accélération courte facultative pour les monstres lourds ; c'est une donnée du composant.
- **Orientation dans la logique ou au dessin ?** Si l'orientation sert à la logique (attaque en cône devant soi, dos exposé), elle doit être calculée au tick, de façon déterministe. Recommandation : **au tick**, stockée comme un **vecteur unitaire** sur le plan (pas un angle), le quaternion du `Transform` en étant déduit ; rotation limitée par tick calculée sans fonction trigonométrique de la bibliothèque standard.
- **Le clic maintenu** (le héros suit le pointeur) : un A\* par tick est inutile ; recommandation : direct tant que la ligne droite vers le pointeur est libre (lancer de cercle), A\* quand elle ne l'est pas, au plus tous les quelques ticks.
- **Qui donne la vitesse à l'animation ?** La distance réellement parcourue pendant le tick (après collisions) divisée par la durée du tick : un personnage poussé contre un mur ralentit, un personnage bloqué passe au repos. C'est déjà ce que fait la tranche ; le système le fournit pour tous.

### Pièges connus

- **Oscillation à l'arrivée** : viser un point avec une vitesse fixe fait dépasser puis revenir. Arriver exactement (dernier pas raccourci) ou accepter une tolérance.
- **Un point du chemin atteint par la foule** : un monstre poussé hors de son chemin vise encore un point derrière lui. Passer au point suivant dès que celui-ci est **visible** et plus proche, ou dès que le point courant est derrière.
- **Glissement qui tourne en rond** contre un pilier : le glissement le long d'un cercle peut ramener au même point ; détecter l'absence de progrès (état « bloqué »).
- **L'animation qui tremble** quand la séparation pousse un personnage de quelques millimètres par tick : seuil de vitesse en dessous duquel l'animation reste au repos, ou vitesse lissée sur quelques ticks pour l'animation seulement.

### Validation

Le héros va où l'on clique, glisse le long des murs au clavier, s'arrête net à l'arrivée sans osciller, passe au repos contre un mur ; les monstres suivent le flow field et entourent le héros ; les pieds ne glissent pas (vitesse d'animation = vitesse réelle) ; captures et rejeux stables.

### Implémentation réalisée (partie 5)

- **`Mover`** (`movement.hpp/.cpp`) : vitesse, part de rotation par seconde, mode (`Stop`, `ToPoint`, `Direct`, `FollowField` avec distance d'arrêt et numéro de champ), chemin et point suivant, état (`Idle`, `Waiting`, `Moving`, `Arrived`, `Blocked`, `NoPath`), **orientation en vecteur unitaire** (tournée par mélange normalisé, sans trigonométrie), **vitesse réellement parcourue**. `go_to`, `move`, `follow_field`, `stop`.
- Systèmes, dans l'ordre écrit en tête du fichier : `plan_paths` → `move_movers` (suivi du chemin avec raccourci quand le point d'après est en ligne droite, arrivée exacte sans oscillation, direct avec glissement, flow field) → `separate_colliders` → `finish_movers` (vitesse réelle, bloqué après 30 ticks sous 20 % de la vitesse : un `ToPoint` replanifie). `facing_rotation` donne le quaternion autour de y par les formules de l'angle moitié (racines seulement).
- La scène « Monde » et la tranche passent par ces systèmes ; la démo 3D sans héros garde son `Walker` (ses captures de référence ne changent pas).
- Tests : un mover contourne le pilier et arrive au point exact en moins de 3 s sans jamais entrer dans un mur ; en direct contre un coin, il glisse puis s'arrête (vitesse réelle presque nulle) ; `facing_rotation` tourne bien +z vers l'orientation.

---

## 6. Requêtes spatiales et lignes de vue

### But

Répondre vite et de façon déterministe aux questions du jeu : **qui est dans ce cercle, ce cône, ce rectangle ?** **Ce rayon touche-t-il un mur, et où ?** **Ce point voit-il celui-là ?**

### Tâches

- [x] Requêtes sur la grille de hachage de la partie 3 : **cercle**, **cône** (demi-angle donné par son cosinus, pas par un angle), **rectangle orienté** (coup de balayage) ; résultats **triés** (par distance puis identifiant) et filtrés par couche.
- [x] **Lancer de rayon sur la grille** (DDA d'Amanatides et Woo) : première case opaque ou non praticable touchée, point et normale d'impact ; pour les projectiles et la ligne de vue.
- [x] **Lancer de cercle** sur la grille (le rayon épaissi du rayon d'un personnage) : pour le lissage des chemins (partie 4) et les projectiles larges.
- [x] **Ligne de vue** entre deux points (opacité), **symétrique** (si A voit B, B voit A).
- [x] Tests unitaires : cas aux bords des cases, rayon exactement le long d'une ligne de grille, rayon qui passe par un coin, cône à 180°, ordre des résultats.

### Questions à se poser

- **Faut-il une structure à part pour les requêtes ?** La grille de hachage des collisions suffit (cases de 1 m ou de 2 m) ; un arbre (quadtree, BVH) n'apporte rien pour des cercles de taille proche sur un plan. Recommandation : **une seule grille de hachage**, remplie une fois par tick après les déplacements.
- **Rayon qui passe exactement par un coin** entre deux murs en diagonale : passe ou ne passe pas ? Recommandation : **ne passe pas** pour la ligne de vue et les projectiles (pas de vue « à travers » un coin), et la même règle partout.
- **Qui lit ces requêtes ?** Le jeu (portée des coups, zones d'effet, projectiles), la visibilité (partie 8), l'IA. Le moteur ne décide rien avec : il répond.

### Pièges connus

- Le DDA qui démarre **sur** une ligne de grille ou dans une case pleine : définir le résultat (touche tout de suite) et le tester.
- Les cônes définis par un angle en degrés convertis par `cos` au tick : calculer le cosinus **une fois** dans les données (ou au chargement), pas à chaque requête dans la logique.

### Validation

Les requêtes de la tranche (portée d'un coup, monstres qui voient le héros) passent par ces fonctions ; leurs tests passent ; un overlay de la scène de test montre un rayon, son impact et les entités d'un cône.

### Implémentation réalisée (partie 6)

- **Requêtes** de `SpatialHash` : `query_circle`, `query_cone` (demi-angle par son cosinus ; le rayon de l'entité élargit le test côté cosinus), `query_rect` (balayage devant soi), filtrées par couche, **triées par distance puis identifiant**.
- **`raycast`** (`visibility.hpp/.cpp`) : parcours de grille d'Amanatides et Woo, arrêt sur ce qui est opaque (`RayBlock::Opaque`) ou non praticable (`Unwalkable`, pour les projectiles), point, normale et distance d'impact ; **rien ne passe par un coin** dont une des deux cases bloque. **`line_of_sight`** ignore les cases des deux extrémités (une créature dans les herbes hautes voit dehors) et est **symétrique par construction** (les deux sens doivent être libres). Le lancer de cercle est `segment_clear` (partie 3).
- La tranche s'en sert pour la vue des créatures ; la scène « Monde » montre le cône devant le héros (créatures touchées entourées) et un rayon vers le pointeur avec son impact.
- Tests : tri à égalité de distance, filtrage par couche, cône plein (cos = −1), rectangle ; mur, barrière (vue mais pas traversée), coin, symétrie sur des centaines de paires.

---

## 7. Effets de particules

### But

Des **effets visuels** légers et nombreux (étincelles, flammes, fumée, sang, poussière, éclats magiques) décrits **en données**, lancés par le jeu à un point, sur une entité ou sur un point d'attache, dessinés avec les billboards du jalon 3.

### Tâches

- [x] **Description d'un effet** en JSON (asset, rechargé à chaud) : un ou plusieurs **émetteurs**, chacun avec : texture ou région d'atlas (et images d'animation), **débit** et/ou **salves**, durée de l'émetteur (boucle ou une fois), **durée de vie** des particules (min-max), forme d'émission (point, cercle, sphère, cône, ligne), vitesse initiale, gravité, freinage, **couleur et taille au fil de la vie** (quelques clés), rotation, additif ou non, orientation (caméra, debout, à plat sur le sol, dans le sens de la vitesse), et une **lumière ponctuelle** facultative qui suit l'effet.
- [x] **Système de particules** sur le CPU : pool de particules à taille fixe, mise à jour **par image** (pas au tick, voir les questions), émetteurs attachés à une entité ou à un point d'attache (`BoneAttachment` du jalon 5), arrêt doux (l'émetteur s'arrête, les particules finissent leur vie).
- [x] **Dessin** : billboards existants, ou un chemin dédié si le tri et l'envoi de milliers de quads le demandent ; **atlas d'effets** commun pour ne pas couper les lots (le jalon 3 l'annonçait) ; orientation **à plat** (décalques au sol : flaque, cercle magique) si le billboard ne l'a pas.
- [ ] **Particules douces** (fondu près du sol et des murs, avec la texture de profondeur) : à décider (voir les questions). *Repoussées : voir l'implémentation.*
- [x] **Budget** : nombre maximal de particules (global et par effet), effets hors de la vue mis à jour sans être dessinés (ou pas mis à jour du tout), réduction du débit si le budget est dépassé.
- [x] **Textures d'effets** libres de droits (voir les questions), créditées.
- [x] Une fenêtre DEBUG **Particules** : effets actifs, particules vivantes, budget, rechargement, bouton pour lancer un effet sous le pointeur. *Dans le panneau de la scène « Monde » (compteurs, effet sous le pointeur) ; le rechargement passe par le rechargement à chaud et DEBUG > Assets.*
- [x] Effets de test : feu de brasero (boucle, avec lumière), étincelles d'impact (salve), sang (salve, gravité), poussière de pas (petite salve au sol), fumée de mort, traînée magique attachée à la main.

### Questions à se poser

- **CPU ou GPU ?** Des particules sur le GPU (compute shader) en tiennent des centaines de milliers ; sur le CPU, quelques dizaines de milliers. Un ARPG chargé (des dizaines de sorts à l'écran) reste dans les dizaines de milliers *(à mesurer)*. Recommandation : **CPU**, simple à déboguer et à écrire, avec une structure (tableaux par attribut) qui permettra de passer au GPU plus tard si la partie 10 le demande.
- **Au tick ou à l'image ?** Les particules sont **purement visuelles** : elles n'influencent jamais la logique. Recommandation : **à l'image** (temps réel écoulé), avec leur **propre générateur aléatoire** (jamais celui de la logique), lancées par la logique mais vivant hors d'elle. Les rejeux et les captures restent stables si le générateur des particules a une graine fixe et que les captures se font à temps figé.
- **Bibliothèque ?** Il existe peu de systèmes de particules libres et indépendants d'un moteur en C++ (*Effekseer*, licence MIT, avec un éditeur, est le plus connu *(à vérifier : licence, support de SDL_GPU, taille)*). Recommandation : **maison**, sur les billboards existants : le besoin (émetteurs simples, courbes de couleur et de taille) est modeste, et Effekseer apporterait son propre rendu à intégrer à nos passes. À reconsidérer si les effets du jeu deviennent ambitieux (rubans, maillages animés, distorsion).
- **Particules douces ?** Sans elles, une fumée posée sur le sol se coupe en ligne droite. Recommandation : **oui**, elles ne coûtent qu'une lecture de la texture de profondeur dans le shader des billboards ; à vérifier avec le MSAA (la profondeur résolue).
- **Textures d'effets** : pistes *(à vérifier : licences)* : **Kenney** *Particle Pack* (CC0), textures générées dans Blender ou par un petit script (disque flou, étincelle étirée, fumée par bruit). Recommandation : **Kenney Particle Pack** + quelques textures **générées par script** (versionnées, reproductibles).
- **Ombres des particules ?** Recommandation : **non** (ni projetées ni reçues), sauf la lumière ponctuelle facultative qui, elle, éclaire la scène.

### Pièges connus

- **Tri** : des milliers de particules transparentes triées chaque image coûtent cher, et des effets qui se croisent clignotent si le tri change d'une image à l'autre. Les particules **additives** n'ont pas besoin de tri (l'addition est commutative) : les dessiner à part, sans tri.
- **Une lumière par flamme** : 30 braseros = 30 lumières ponctuelles sur un budget de 32 (jalon 3). Une lumière par **effet**, avec priorité, et le budget d'ombres ponctuelles (4) à part.
- **Effet attaché à une entité détruite** (le monstre meurt, son effet de feu doit finir) : détacher l'émetteur à la destruction et le laisser s'éteindre sur place.
- **Pas de temps énorme** (fenêtre déplacée, point d'arrêt) : limiter le temps d'une image pour les particules, sinon une salve part à travers la carte.
- **Surdessin** (overdraw) : de grandes particules de fumée qui couvrent l'écran coûtent plus que des milliers de petites. Mesurer avec un effet plein écran.

### Validation

Les effets de test se lancent depuis la fenêtre DEBUG, suivent leur entité ou leur os, se rechargent à chaud quand on modifie leur JSON ; 20 000 particules restent dans le budget (partie 10) ; aucune particule ne coupe le sol net ; captures inchangées tant qu'aucun effet n'y apparaît.

### Implémentation réalisée (partie 7)

- **`ParticleEffect`** (`particles.hpp/.cpp`) : description JSON versionnée (format en commentaire dans l'en-tête) : texture et images (`frames`, `animate`), salve, débit, durée, boucle, durée de vie, forme (point, sphère, disque, cône), vitesse, gravité, freinage, **courbes** de taille et de couleur (clés linéaires), rotation, additif, orientation (`camera`, `upright`, **`flat`**), **lumière** facultative (couleur, intensité, portée, scintillement). Asset `assets.particle_effect(chemin)` (type 12), rechargé à chaud : les effets en cours prennent les nouvelles valeurs.
- **`ParticleSystem`** : un par scène, sur le CPU, **à l'image** (temps réel borné à 0,1 s) avec **son propre générateur** (xorshift), jamais celui de la logique ; `play`, `move` (un effet qui suit un personnage), `stop` (les particules finissent leur vie), `kill` ; effets finis oubliés seuls ; **budget global** (20 000) au-delà duquel les naissances sont refusées et comptées ; effets hors de la vue non dessinés ; `add_lights` donne les lumières des effets les plus proches. Pour les captures figées, la scène le fait avancer au tick.
- **Billboards** : orientation `Flat` (décalques au sol), **rotation** dans leur plan ; **les billboards additifs sont dessinés après les autres, groupés par texture** (dans l'ordre d'apparition des textures) : 20 000 particules passaient par 10 540 draw calls, elles en prennent **23**. Les billboards qui couvrent restent triés du plus loin au plus proche ; captures des démos inchangées.
- **Textures** générées par `tools/textures/make_particle_textures.py` (versionnées dans `assets/particles/`) : lueur, étincelle, fumée (bruit), quatre flammes, sang, anneau. **Décision changée** : pas de pack Kenney, rien à télécharger ni à créditer, et chaque texture se refait à l'identique.
- **Effets** de `assets/effects/` : `fire` (boucle, lumière), `brazier_fire` (sans lumière, le brasero a la sienne), `sparks`, `blood` (gouttes et flaque au sol), `dust`, `death_smoke`, `magic_trail`, `magic_circle` (anneau au sol qui tourne). Convention notée : en additif, l'alpha est l'opacité (prémultipliée) et ne doit pas valoir 0 tant que la particule brille.
- **Pas fait** : les **particules douces** (la profondeur de la passe de scène n'est pas lisible pendant qu'on y dessine : il faudrait d'abord la copier ; repoussé, la fumée posée au sol coupe encore net) ; pas de fenêtre DEBUG à part : les compteurs et le lancement d'un effet sous le pointeur (E) sont dans le panneau de la scène « Monde » ; l'attache à un os passe par `move()` avec `World::attach_point_matrix` (pas de composant dédié).
- Mesures : mise à jour de 20 000 particules, **0,17 ms** ; tout compris dans la scène (simulation, billboards, tri, envoi), **environ 3,5 ms de CPU par image** pour 19 000 particules (`--world-fires 800`), dont 2,6 ms de préparation des billboards : correct pour un pic de combat, à reprendre (GPU, ou tri allégé) si le jeu en demande plus.
- Tests (`test_particles.cpp`) : lecture et refus, salve qui vit sa durée puis s'oublie, débit en boucle, budget et refus, arrêt doux, effets de `assets/effects/` relus (et leurs textures présentes).

---

## 8. Brouillard de guerre et visibilité

### But

Savoir **ce que le héros voit** (en vue directe, dans un rayon, murs compris) et **ce qu'il a exploré** ; montrer les deux à l'écran (zones jamais vues sombres, zones hors de vue atténuées, monstres hors de vue cachés), et ne pas laisser un mur **cacher le héros**.

### Tâches

- [x] **Champ de vision** sur la grille depuis un point, dans un rayon, avec la propriété `opaque` : **shadowcasting** (voir les questions), résultat symétrique et déterministe ; recalculé quand l'observateur change de case ou que la grille change de version.
- [x] **Carte d'exploration** : cases déjà vues (un bit par case), qui ne s'effacent pas ; prête à être sauvegardée (jalon 7).
- [x] **Affichage** : une petite texture (un texel par case : exploré, en vue, avec un dégradé) envoyée au GPU quand elle change, lue dans les shaders des maillages et des billboards pour assombrir et désaturer ; bords adoucis (filtrage bilinéaire, ou flou léger) pour ne pas voir les cases.
- [x] **Entités hors de vue** : le jeu décide ce qui se cache (monstres, effets), le moteur donne la réponse (« cette case est-elle en vue ? ») et un moyen simple de cacher (le `Hidden` existant) ou d'atténuer.
- [x] **Murs qui masquent le héros** : les murs (ou décors) entre la caméra et le héros deviennent transparents ou se « découpent » (voir les questions).
- [x] Tests unitaires : champ de vision sur des cartes ASCII attendues (pilier, couloir, porte, coin), symétrie sur des tirages de points, exploration qui s'accumule.

### Questions à se poser

- **Quel algorithme de champ de vision ?** Options : **lancer de rayons** vers le bord du rayon (simple, trous et asymétries) ; **shadowcasting récursif** (classique des roguelikes, rapide) ; **shadowcasting symétrique** (variante d'Albert Ford : si A voit B alors B voit A, pas d'artefacts dans les couloirs) *(à vérifier)* ; **visibilité continue** par polygones (exacte au bord des murs, plus complexe). Recommandation : **shadowcasting symétrique** sur la grille : la symétrie compte pour le jeu (un monstre que je vois me voit), le résultat est en cases, comme la carte.
- **Le brouillard influence-t-il la logique ?** Le **champ de vision** des monstres oui (agro), celui du héros sert à l'affichage. Les deux utilisent la même fonction, au tick, déterministe. L'**affichage** (texture, fondu) est au dessin.
- **Que montrer des zones explorées hors de vue ?** Options : le décor tel qu'il était (ARPG classique), atténué ; ou noir. Recommandation : **exploré = décor atténué** (sombre et désaturé), monstres cachés ; **jamais vu = noir** (ou presque). Les réglages (couleurs, rayon) en données.
- **Murs devant le héros : comment ?** Options : rendre **transparents** les murs dont la boîte coupe le segment caméra-héros (tri et transparence à gérer) ; **tramage** (dithering) des pixels proches du héros à l'écran, dans le shader des maillages (pas de transparence, pas de tri, juste un `discard` selon un motif) ; **découpe** en cercle autour du héros. Recommandation : **tramage en cercle autour du héros à l'écran**, appliqué seulement aux maillages marqués « peuvent masquer » (murs, grands décors) et seulement s'ils sont **plus proches de la caméra** que le héros. C'est ce que font beaucoup de jeux isométriques, et cela ne touche que le shader et un drapeau d'instance.
- **Minicarte ?** Elle appartient à l'interface du jeu ; le moteur fournit la carte d'exploration et la grille. Pas dans ce jalon.

### Pièges connus

- **Murs vus de l'intérieur** : le shadowcasting éclaire les cases **opaques** en bordure (on voit le mur) mais pas derrière ; vérifier que les murs eux-mêmes apparaissent explorés, sinon la carte explorée montre des salles sans murs.
- **Grandes cartes** : une texture d'un texel par case (400 × 400 = 160 Ko) se met à jour par morceaux, pas en entier à chaque pas.
- **Le tramage et les ombres** : un mur tramé pour la caméra doit continuer à projeter son ombre (la passe d'ombre ne trame pas).
- **Des monstres qui « apparaissent »** quand ils entrent en vue : un fondu de quelques images plutôt qu'un saut, côté affichage.
- **Visibilité qui varie avec la position dans la case** : calculée depuis la **case** de l'observateur (pas sa position exacte), elle saute d'un coup quand il change de case ; c'est acceptable et stable, mais à choisir en connaissance de cause (le centre de la case comme origine).

### Validation

Dans la scène de test et la tranche : les salles jamais vues sont noires, celles déjà vues sont atténuées, un monstre derrière un mur est caché puis apparaît en fondu quand il entre en vue, les tests du champ de vision passent, et le héros reste visible quand il passe derrière un mur.

### Implémentation réalisée (partie 8)

- **`FieldOfView`** : *shadowcasting* symétrique d'Albert Ford, pentes en **fractions entières** (64 bits), disque de rayon r, murs du bord vus ; **`ExploredMap`** : un octet par case, cases vues une fois pour toutes, prête pour la sauvegarde.
- **Affichage** : `MeshRenderer::set_fog` (`FogOfWar` : coin, taille d'une case, luminosité du jamais vu et de l'exploré, saturation hors de vue) et `set_fog_cells` (0 jamais vu, 128 exploré, 255 en vue) : une texture **R8**, un texel par case, envoyée par une passe de copie quand elle change, filtrée (bords doux), lue par `mesh.frag` **et** `billboard.frag` (une lueur dans une salle jamais vue ne perce pas le noir). Éteint, le rendu est identique au bit près (captures inchangées).
- **Murs devant le héros** : `Material::fades` (instance marquée `emissive.w = −1`) et `MeshRenderer::set_cutout` : dans `mesh.frag`, les pixels des maillages marqués, **plus proches de la caméra que le héros d'au moins 1 m**, et à moins du rayon (1,6 m) de la ligne caméra-héros, sont écartés selon un motif ordonné 4 × 4 (aucun tri, aucune transparence ; la passe d'ombre n'est pas touchée). Murs, piliers, herbes hautes et arbres de la tranche.
- **Corrigé après l'essai à la main** (2026-10-03) : un mur collé par le héros n'était troué qu'en partie (le choix « devant le héros » se faisait pixel par pixel : le dessus disparaissait, pas les côtés, et l'on voyait les faces à travers) ; il se fait maintenant **par objet**, sur la place de l'instance (`origin`, passée par `mesh.vert`). Les faces des murs de bordure tournées vers la caméra clignotaient en noir : le brouillard était lu exactement sur la limite entre la case du mur et le dehors ; il est lu **0,3 m à l'intérieur de la surface**, et la texture est bornée au bord au lieu de valoir « jamais vu » dehors.
- Les créatures hors de vue reçoivent `Hidden` (et leurs barres de vie disparaissent avec elles).
- **Pas fait** : le fondu d'une créature qui entre en vue (elle apparaît d'un coup).
- Coût : champ de vision de rayon 20 dans une salle ouverte, **0,015 ms** ; recalculé seulement quand le héros change de case.
- Tests : murs vus, rien derrière, vue par une porte, rayon, exploration cumulée ; symétrie de la vue sur une carte à piliers et herbes.

---

## 9. Outils de debug et scène de test

### But

**Voir** ce que font les systèmes du jalon : la grille, les collisions, les chemins, le flow field, les requêtes, la visibilité et les particules, sans lire de nombres.

### Tâches

- [x] Overlays avec `DebugLineRenderer` (jalon 3) et des quads au sol : **cases praticables/opaques**, **dégagement** (couleurs), **cercles** des colliders, **chemins** (bruts et lissés), **flow field** (flèches), **champ de vision** (cases en vue), **requêtes** (cercle, cône, rayon et impact), **grille de hachage** (occupation des cases). *Tout sauf l'occupation de la grille de hachage.*
- [x] Une fenêtre DEBUG **Monde** (ou une par système) : cases sous le pointeur (indices, propriétés, dégagement, distance du flow field), temps des systèmes du tick (collisions, chemins, flow field, visibilité), compteurs (recherches A\* par tick, nœuds explorés, paires de collisions testées, particules), choix des overlays. *Dans le panneau de la scène.*
- [x] Une scène de test **« Monde »** dans le menu DEBUG (`--world`, `--map nom`) : une carte de test au choix, un héros piloté (clic et clavier), des monstres qu'on ajoute sous le pointeur, le flow field visible, un bouton pour poser/retirer un mur (changement de version de la grille), les effets de particules à lancer.
- [x] Le `Mover` et le `Collider` dans l'**inspecteur** d'entités (jalon 4).

### Pièges connus

- Les overlays de grille sur une carte de 400 × 400 : 160 000 cases, trop de lignes. Ne dessiner que les cases **visibles** (le frustum, comme le reste).

### Validation

Chaque overlay s'active depuis la scène « Monde » et montre ce qu'on attend sur les cartes de test ; les compteurs et les temps sont lisibles dans la fenêtre DEBUG.

### Implémentation réalisée (partie 9)

- La scène **« Monde »** (DEBUG > Tests moteur) réunit tout le jalon : héros au clic (chemin lissé ; maintenu : suit le pointeur en ligne droite quand c'est libre) et au clavier (glisse), monstres (M : six sous le pointeur, B : un gros) qui voient le héros, le poursuivent par les flow fields, se repoussent, l'entourent et frappent (étincelles), brouillard, murs tramés, feux sur les points « but », effet au choix sous le pointeur (E), traînée.
- **Overlays** (cases à cocher) : grille (cases bloquées, opaques ou non), dégagement (carré coloré), cercles des colliders (blanc le héros, jaune un monstre qui frappe), chemins restants, flow field (flèches), champ de vision, cône de requête, rayon et impact ; seules les cases autour de la vue sont dessinées.
- Le panneau affiche les **temps moyens des systèmes** (IA, chemins, flow fields, déplacements, collisions, vision, particules), les paires testées, les A\* du tick, les particules, les cases explorées, et la case survolée (dégagement, coût du flow field). **Choix** : un panneau de scène plutôt qu'une fenêtre DEBUG à part, puisque tout appartient à la scène ; `Collider` et `Mover` sont dans l'**inspecteur**.
- Sans menu : `--world-auto` (le héros marche vers « but », des monstres aux points « monstres »), `--world-crowd N`, `--world-fires N`, `--world-map nom`, avec `--report`, `--capture` et `--freeze-after`.

---

## 10. Performances

### But

Mesurer et noter le coût des systèmes du jalon sur une scène chargée, et vérifier qu'ils tiennent dans le budget du tick (16,7 ms par image à 60 Hz, dont la logique ne doit prendre qu'une petite part).

### Tâches

- [x] Scène de charge (`--world --crowd N`) : N monstres qui poursuivent le héros sur une grande carte (400 × 400), séparation et collisions actives, flow field recalculé à chaque changement de case du héros.
- [x] Mesures (fenêtre DEBUG et `--report`) : temps de collisions, de chemins (A\* par tick, pire cas), de flow field, de champ de vision, de particules (mise à jour, envoi), en Release, sous Windows ; le Mac dans TEST_MAC.md.
- [x] Une recherche A\* **pire cas** (traversée d'une grande carte en labyrinthe) chronométrée, pour fixer la limite de nœuds et le nombre de recherches par tick.
- [x] Un effet **plein écran** (fumée) et 20 000 particules additives, pour le surdessin et le coût du CPU. *20 000 particules mesurées (`--world-fires 800`) ; le surdessin d'une fumée plein écran n'a pas été mesuré à part (le GPU reste loin de la limite sur la machine de développement).*
- [x] Comparer la démo 3D avant et après le jalon (même méthode qu'au jalon 5 : ancien commit construit à part).

### Questions à se poser

- **Quels budgets ?** Proposition à confirmer par les mesures : **500 monstres** qui poursuivent et se repoussent pour **1 ms** de logique en tout ; un A\* pire cas sous **1 ms** (sinon découpé sur plusieurs ticks) ; **20 000 particules** pour **1 ms** de CPU ; champ de vision de rayon 20 sous **0,1 ms**.
- **Multithreading ?** Comme au jalon 5 : seulement si les mesures le demandent ; les flow fields et les A\* se prêtent bien au travail en tâche de fond, mais il faut alors garder le déterminisme (résultat utilisé au même tick, quelle que soit la vitesse de la machine).

### Validation

Les mesures sont notées dans la section « Implémentation réalisée » et dans les décisions ; les budgets sont tenus ou les écarts expliqués ; la démo 3D ne ralentit pas.

### Implémentation réalisée (partie 10)

Release, Windows, RTX 4070 Ti SUPER, carte `arene` (64 × 64), monstres qui poursuivent tous le héros et finissent par l'entourer (`--world-crowd N --report --run-seconds 10`) :

| Monstres | IA | Chemins | Champs | Déplacements | Collisions | Vision | **Total par tick** | CPU par image |
|---|---|---|---|---|---|---|---|---|
| 200 | 0,008 | 0,010 | 0,002 | 0,036 | 0,116 | 0,007 | **0,18 ms** | 0,58 ms |
| 500 | 0,016 | 0,013 | 0,001 | 0,076 | 0,306 | 0,015 | **0,43 ms** | 0,84 ms |
| 1 000 | 0,027 | 0,019 | 0,001 | 0,139 | 0,740 | 0,029 | **0,95 ms** | 1,21 ms |

- Cible tenue : **500 monstres pour 0,43 ms** de logique (la cible était 1 ms). Une première mesure donnait 0,1 ms d'IA même sans monstre : la scène cherchait le fichier des étincelles sur le disque à chaque tick (`exists` puis `particle_effect`) ; l'asset est maintenant gardé. Les collisions dominent au-delà (foule serrée autour du héros : beaucoup de paires proches).
- Pire cas d'A\* : 3,9 ms pour un labyrinthe de 401 × 401 traversé ; la limite de 20 000 nœuds le borne vers 2 ms, et le clic n'en lance qu'un. Flow field complet du même labyrinthe : 5,8 ms (jamais en jeu : rayon limité). Dégagement d'une carte de 401 × 401 : 3,7 ms au chargement.
- Particules : voir la partie 7 (0,17 ms de simulation pour 20 000, environ 3,5 ms de CPU tout compris).
- Démo 3D (`--demo3d --seed 42 --report --no-vsync`) : **1,21 ms** de CPU par image (1,19 au jalon 5) ; tranche : **1,13 ms** (1,10), chargement 826 ms (770 : la carte, le dégagement et les effets).
- **Pas de multithreading** : rien ne le demande.

---

## 11. Le monde dans la tranche jouable

### But

Que la tranche jouable du jalon 5 profite de tout le jalon : un héros qui va **où l'on clique**, des monstres qui **chassent**, des **effets**, et un monde qu'on **découvre**.

### Contenu

- La carte de la tranche devient une **carte de test en fichier** (salles, couloirs, portes, piliers), avec des points nommés pour le départ du héros et les groupes de monstres.
- **Héros** : clic = A\* lissé jusqu'au point (ou au plus près) ; clic maintenu = suit le pointeur en contournant ; clavier ou manette = déplacement direct avec glissement ; clic sur un monstre hors de portée = chemin jusqu'à lui, coup à l'arrivée.
- **Monstres** (logique de la tranche, pas du moteur) : au repos ou en errance jusqu'à ce que le héros entre dans leur **champ de vision** (et dans un rayon), puis **poursuite** par le flow field, **attaque** quand ils sont à portée (le héros prend des coups : une barre de vie qui descend suffit, sans mort du héros si cela complique), retour au calme s'ils le perdent de vue longtemps.
- **Collisions** : le héros et les monstres se repoussent, les monstres qui frappent sont inamovibles ; personne ne traverse les murs.
- **Effets** : étincelles à chaque impact (événement « impact » du jalon 5), poussière aux pas, sang ou fumée à la mort, flammes et lumière sur les braseros, traînée sur l'épée pendant l'attaque (point d'attache).
- **Brouillard** : salles noires jusqu'à leur découverte, atténuées hors de vue, monstres cachés hors de vue ; murs tramés quand le héros passe derrière.
- **Référence** : nouvelle capture et nouveau rejeu d'entrées de la tranche (son contenu change) ; la démo 3D et la démo 2D gardent les leurs si leur comportement ne change pas, sinon de nouvelles, en expliquant l'écart.

### Tâches

- [x] Écrire la carte de la tranche et la charger comme asset.
- [x] Brancher le héros et les monstres sur `Mover`, `Collider`, les chemins et le flow field.
- [x] Écrire la petite IA de la tranche (états : calme, poursuite, attaque, retour) avec le champ de vision et les requêtes.
- [x] Ajouter les effets et le brouillard.
- [x] Refaire le rejeu de référence (`make_slice_replay.py`) et la capture ; vérifier les autres captures.

### Validation

La tranche se joue à la main : le héros contourne les murs, les monstres le repèrent, le poursuivent, l'entourent et le frappent, les effets partent aux bons moments, le brouillard se lève salle par salle ; le rejeu de référence redonne la même capture deux fois de suite.

### Implémentation réalisée (partie 11)

- La tranche (`Demo3D` avec `Options::hero`) lit **`assets/maps/tranche.json`**, écrit par `tools/maps/make_slice_map.py` avec les mêmes salles que la démo construite en code, chaque brasero étant une tuile qui bouche sa case, et le départ un point nommé.
- **Héros** : `Collider` (poids 4) et `Mover` ; clic = chemin lissé (ou jusqu'à la créature cliquée, frappée dès qu'elle est à portée), maintenu = suit le pointeur, clavier ou manette = direct avec glissement ; tourné vers sa cible quand il frappe (orientation du `Mover`).
- **Créatures** (logique de la tranche, `Hunter`) : errent tout droit et font demi-tour quand elles sont bloquées ; voient le héros à 9 m par ligne de vue, le poursuivent par le flow field, abandonnent 4 s après l'avoir perdu de vue ; à portée, elles deviennent immobiles et frappent toutes les 1,5 s (l'attaque `1H_Melee_Attack_Chop` des squelettes, le coup portant à son événement « impact » si le héros est encore à portée) ; le héros perd 4 % (jamais en dessous de 5 %) et se soigne lentement. Mortes, elles perdent leur `Collider` et s'effacent dans la fumée.
- **Effets** : flammes sur les braseros, étincelles aux coups du héros, sang sur le héros, poussière à ses pas (événements « pas »), fumée à la mort. La traînée sur l'épée n'a pas été ajoutée (l'effet `magic_trail` existe, la scène « Monde » le montre sur le héros).
- **Brouillard** : salles noires jusqu'à leur découverte, atténuées hors de vue, créatures et barres cachées hors de vue ; murs et arbres tramés devant le héros.
- **Captures** : démo 2D `bd91e8b2c616`, démo 3D `fdc076d9c3ae` et son rejeu `952477744ffb` **inchangées** ; nouvelle capture de la tranche (`--states --replay-input tests/data/slice_replay.json --freeze-after 340 --run-seconds 9 --pixel-size 1280 720 --capture`) **`f5421305c50a`**, trois lancements sur trois (les particules avancent au tick quand la scène doit se figer pour une capture). Comme avant, profil de touches par défaut (le script de capture met de côté celui du joueur).
- Build Debug (couche de validation D3D12) : aucun message, scène « Monde » et tranche, sans anticrénelage, en FXAA et en MSAA 4×. MSL régénérés (`mesh.frag`, `billboard.vert`, `billboard.frag`).

---

## 12. Critères de fin de jalon

Le jalon est terminé quand **tout** ce qui suit est vrai :

- [x] Les cartes se lisent depuis des fichiers, passent par le **gestionnaire d'assets** et se rechargent à chaud ; une carte changée en cours de jeu (version) invalide chemins et champs de vision. *Partie 2 ; la version de la grille invalide dégagement, champs et vision (parties 4 et 8).*
- [x] Les personnages sont des **cercles** qui ne traversent ni les murs, ni les obstacles, ni les autres personnages, et **glissent** le long des murs. *Partie 3.*
- [x] Le **pathfinding** mène un personnage de toute taille par le plus court chemin praticable, lissé, au plus près si la destination est inaccessible ; un **flow field** mène une foule vers une cible. *Partie 4.*
- [x] Les **requêtes spatiales** (cercle, cône, rectangle, rayon, ligne de vue) répondent de façon déterministe et servent à la tranche. *Partie 6.*
- [x] Les **particules** sont décrites en JSON, rechargées à chaud, attachables à une entité ou à un os, et tiennent le budget mesuré. *Partie 7 ; sans les particules douces, repoussées.*
- [x] Le **brouillard de guerre** (exploré, en vue) s'affiche, cache les entités hors de vue, et les murs ne masquent plus le héros. *Partie 8.*
- [x] La scène de test « Monde », les overlays et la fenêtre DEBUG sont dans le menu DEBUG. *Partie 9 (le panneau de la scène tient lieu de fenêtre).*
- [x] Les budgets de la partie 10 sont mesurés et notés. *Partie 10.*
- [x] Toute la logique (collisions, chemins, flow field, requêtes, champ de vision) est **déterministe** : un rejeu d'entrées redonne la même capture ; aucune particule ne touche à la logique. *Trois captures de la tranche sur trois identiques ; ordre trié, coûts entiers, pas de trigonométrie dans les décisions.*
- [x] Aucun avertissement de compilation, aucun message de la couche de validation du GPU. *Sous Windows (D3D12, Debug) ; Metal dans TEST_MAC.md.*
- [x] La logique pure a ses tests unitaires (cartes ASCII) ; les captures de référence des jalons précédents sont inchangées ou leurs changements expliqués. *351 tests ; démos 2D et 3D inchangées ; la tranche a sa nouvelle capture (`f5421305c50a`).*
- [x] Les nouvelles bibliothèques et les nouveaux assets (textures d'effets) sont dans `credits.json` (fenêtre « À propos »). *Aucune nouvelle bibliothèque ; les textures d'effets sont générées par le projet (rien à créditer).*
- [x] Les décisions de la section [Décisions à consigner](#décisions-à-consigner) sont remplies.
- [x] La documentation des nouveaux fichiers est ajoutée à [FICHIERS_DU_PROJET.md](FICHIERS_DU_PROJET.md), et les vérifications Mac sont regroupées dans [TEST_MAC.md](TEST_MAC.md).

---

## 13. Risques principaux

| Risque | Impact | Parade |
|---|---|---|
| Collisions qui accrochent les coins ou laissent entrer dans les murs | Personnages coincés, sensation de contrôle mauvaise | Test contre le carré de chaque case, plusieurs passes, vérification finale ; tests unitaires des coins ; décoincement |
| Logique non déterministe (ordre d'EnTT, égalités, fonctions trigonométriques) | Rejeux et captures qui divergent, impossibles à déboguer | Ordre trié, coûts entiers, règle d'égalité écrite, vecteurs au lieu d'angles ; test « même résultat après réordonnancement » |
| A\* trop lent sur les grandes cartes ou les destinations inaccessibles | Saccades au clic | Limite de nœuds, meilleure case retenue, recherches plafonnées par tick ; HPA\* si les mesures l'exigent |
| Foules qui se bloquent dans les couloirs | Monstres qui n'arrivent jamais, ou qui tremblent | Séparation douce, poids de poussée, état « bloqué » remonté au jeu ; RVO seulement si nécessaire |
| Monde plat insuffisant pour les zones du jeu (ponts, étages) | Refonte des chemins et des collisions | Interface de chemin indépendante de la grille ; Recast & Detour en réserve ; décision repoussée au premier besoin réel |
| Particules trop coûteuses (tri, surdessin, lumières) | Images perdues dans les combats chargés | Additives sans tri, budget global, une lumière par effet, mesure du plein écran ; GPU plus tard |
| Brouillard ou tramage qui coûtent dans tous les shaders | Perte de performance générale | Une lecture de texture par pixel ; tramage seulement sur les maillages marqués |
| Dérive du périmètre (physique, RVO, niveaux, éditeur, IA complète) | Jalon sans fin | Hors périmètre sauf besoin de la tranche ; l'éditeur au jalon 7, l'IA au jeu |

---

## Décisions à consigner

À remplir au fil du jalon. Chaque ligne correspond à une question posée plus haut.

| Sujet | Décision | Raison |
|---|---|---|
| Monde plat ou niveaux | **Plan unique** (y = 0) pour la logique, décor 3D par-dessus | Recommandation validée (2026-10-03) ; des niveaux viendront avec une zone qui en a besoin (Recast & Detour en réserve) |
| Format des cartes de test (code, JSON + ASCII, Tiled) | **JSON + carte en caractères** (`MapData`), asset rechargé à chaud, réécrit à l'identique par `to_ascii()` | Lisible et diffable ; l'éditeur du jalon 7 écrira le même format |
| Grille de jeu = `TileMap` à 1 m, murs pleins, propriétés d'une case | **Oui** : 1 case = 1 m, murs d'une case, praticable et opaque seulement ; une case **sans tuile est un trou** ; les obstacles d'une case sont des tuiles (brasero) | Une seule grille pour collisions, chemins et vision ; collisions des personnages continues (cercles) |
| Collisions : maison ou Box2D | **Maison** (`collision.hpp`) | Petit problème collé à la grille ; garde la main sur l'ordre des calculs. Préférence de l'utilisateur précisée : une bibliothèque seulement pour ce qu'on ne peut raisonnablement pas écrire (2026-10-03) |
| Collisions continues ou discrètes, projectiles | **Discrètes** par pas de la moitié du rayon ; les projectiles par **rayon** sur la grille (`raycast`, `Unwalkable`) | Aucun personnage ne traverse un mur à 60 Hz ; un projectile rapide ne saute pas de coin |
| Qui pousse qui, séparation dure ou douce | **Poids de poussée** entier (0 : immobile ce tick), séparation **douce** entre personnages (60 %, 2 itérations), **dure** contre les murs | Le héros ne se fait pas bousculer, un monstre qui frappe tient sa place ; pas de tremblement des foules |
| Pathfinding : A\* sur grille, navmesh (Recast & Detour) ou hiérarchique | **A\* sur la grille** + **flow fields**, maison | Déterministe, collé à la grille ; 3,9 ms au pire sur 401 × 401, borné par la limite de nœuds ; HPA\* inutile à ce jour |
| Directions et coûts de l'A\*, tailles des personnages (dégagement) | **8 directions, 10 / 14, octile**, pas de coupe de coin ; égalités par (f, h, indice) ; **carte de dégagement** (toutes les tailles), lissage par lancer de cercle | Aucun flottant dans la recherche ; chemins lisses sans escalier |
| Flow field ou A\* par monstre ; évitement (séparation, RVO) | **Les deux** (le jeu choisit), un champ par taille ; **séparation** seulement, pas de RVO | Les monstres entourent le héros sans bouchon gênant dans les cartes de test |
| Accélération, orientation (au tick, en vecteur), clic maintenu | **Vitesse immédiate** ; orientation **au tick, vecteur unitaire** tourné sans trigonométrie, quaternion par l'angle moitié ; clic maintenu en **ligne droite si libre**, sinon un chemin tous les 10 ticks | Réponse immédiate au clic ; déterministe ; peu de recherches |
| Structure des requêtes spatiales ; rayon par un coin | **Une grille de hachage** (seaux de 2 m) remplie par les collisions ; résultats triés (distance, identifiant) ; **rien ne passe par un coin** dont une case bloque | Une structure pour deux usages ; même réponse partout |
| Particules : CPU ou GPU, au tick ou à l'image, maison ou bibliothèque | **CPU, à l'image, maison**, générateur à part ; **au tick** seulement pour une capture figée ; budget global 20 000 | Simple, sans effet sur la logique ; 0,17 ms de simulation pour 20 000 |
| Particules douces ; textures d'effets et licences ; ombres | Particules douces **repoussées** (copie de la profondeur nécessaire) ; textures **générées par script** (pas de pack Kenney) ; **pas d'ombres**, une lumière facultative par effet ; additifs groupés par texture | Rien à télécharger ni à créditer ; 23 draw calls au lieu de 10 540 pour 20 000 particules |
| Champ de vision : algorithme | **Shadowcasting symétrique** (Albert Ford), pentes en fractions entières | Symétrie (un monstre que je vois me voit), aucun flottant ; 0,015 ms en rayon 20 |
| Affichage du brouillard (exploré, en vue, jamais vu) | Texture **R8 d'un texel par case**, filtrée, lue par les maillages et les billboards : jamais vu noir, exploré sombre et grisé, entités cachées hors de vue | Une lecture de texture par pixel ; rendu identique quand il est éteint |
| Murs qui masquent le héros (transparence, tramage, découpe) | **Tramage** ordonné 4 × 4 autour de la ligne caméra-héros, sur les maillages `Material::fades` plus proches que le héros d'au moins 1 m | Ni tri ni transparence ; les ombres restent ; murs, piliers, herbes et arbres |
| Budgets (monstres, A\*, particules, champ de vision) ; multithreading | **500 monstres en 0,43 ms** de logique par tick (1 000 : 0,95 ms) ; A\* limité à 20 000 nœuds, 8 par tick ; 20 000 particules ; **pas de multithreading** | Mesures de la partie 10 (2026-10-03) |
