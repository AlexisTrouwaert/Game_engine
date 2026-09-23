# Jalon 2 - Rendu 2D

Retour à la [roadmap générale](ROADMAP.md). Jalon précédent : [Jalon 1 - Fondations](JALON_1_FONDATIONS.md). Description des fichiers existants : [Fichiers du projet](FICHIERS_DU_PROJET.md).

## Objectif

Passer d'**un sprite** à **une scène 2D complète** : des milliers de sprites dessinés efficacement, une caméra (2D et isométrique), une carte de tuiles, des sprites animés issus d'atlas, et du texte.

À la fin du jalon, une **scène de démonstration** doit tourner à la cadence de l'écran sur Windows et macOS : une carte isométrique, plusieurs milliers de sprites animés triés en profondeur, une caméra qui se déplace et zoome, la tuile sous la souris mise en évidence, et un compteur de FPS affiché en texte.

C'est le jalon qui produit le moteur de rendu dont l'ARPG aura réellement besoin : tout ce que le joueur verra passe par ici.

## Prérequis

- Le [jalon 1](JALON_1_FONDATIONS.md) est **validé sur les deux OS**, Mac compris. Aujourd'hui il n'est vérifié que sur Windows. Continuer sans le test Mac accumule de la dette : chaque nouvelle brique du jalon 2 serait à tester deux fois plus tard.

## Comment lire ce document

Même structure que le jalon 1 :

- **But** : ce qu'on cherche à obtenir.
- **Tâches** : ce qu'il faut faire, dans l'ordre.
- **Questions à se poser** : les décisions à trancher avant ou pendant le travail. À noter dans la section [Décisions à consigner](#décisions-à-consigner) une fois tranchées.
- **Pièges connus** : erreurs fréquentes.
- **Validation** : comment savoir que la partie est terminée.

Les points marqués *(à vérifier)* sont des informations dont je ne suis pas certain ou qui évoluent vite. Il faut les confirmer dans la documentation actuelle avant de s'appuyer dessus.

## Sommaire

1. [Vue d'ensemble et ordre de travail](#1-vue-densemble-et-ordre-de-travail)
2. [Préparation et mesures](#2-préparation-et-mesures)
3. [Sprite batching](#3-sprite-batching)
4. [Caméra](#4-caméra)
5. [Atlas de textures](#5-atlas-de-textures)
6. [Animations de sprites](#6-animations-de-sprites)
7. [Tilemaps](#7-tilemaps)
8. [Texte et polices](#8-texte-et-polices)
9. [Scène de démonstration](#9-scène-de-démonstration)
10. [Débogage et performance](#10-débogage-et-performance)
11. [Critères de fin de jalon](#11-critères-de-fin-de-jalon)
12. [Risques principaux](#12-risques-principaux)
13. [Décisions à consigner](#décisions-à-consigner)

---

## 1. Vue d'ensemble et ordre de travail

### Où on part

Après le jalon 1, le moteur sait :

- ouvrir une fenêtre, tourner à pas de temps fixe, et présenter des images en VSync ;
- charger un shader, une image PNG, créer une texture, un échantillonneur, des buffers ;
- dessiner **un** sprite : un pipeline, un quad, une matrice `mvp` envoyée en uniforme, un draw call.

Cette approche ne passe pas à l'échelle : avec 5 000 sprites, elle ferait 5 000 draw calls et 5 000 envois d'uniformes par frame. Tout le jalon consiste à remplacer ça.

### Dépendances entre les parties

```
2. Préparation (refactor, tests, mesures)
      |
3. Sprite batching
      |
      +---------------------+
      |                     |
4. Caméra              5. Atlas de textures
      |                     |
      +----------+----------+
                 |
      +----------+-----------+
      |          |           |
6. Animations  7. Tilemaps  8. Texte
      |          |           |
      +----------+-----------+
                 |
        9. Scène de démonstration
```

- Le **batching** est la fondation : caméra, tuiles, animations et texte s'y branchent tous.
- La **caméra** et les **atlas** sont indépendants l'un de l'autre.
- Les animations, les tuiles et le texte ont besoin d'atlas et de batching, mais pas les uns des autres.

### Ce qui peut se faire en pause du moteur

Comme pour l'ARPG, plusieurs tâches sont de la logique pure, sans GPU, et se testent en ligne de commande. Elles conviennent bien aux moments où on ne veut pas toucher au rendu :

- **Mathématiques de la caméra et de l'isométrie** (conversions, aller-retour), avec tests unitaires.
- **Algorithme d'empaquetage d'atlas** et format des métadonnées.
- **Logique d'animation** (temps, boucles, événements), avec tests unitaires.
- **Structure des données de la carte** et ses tests.
- **Choix de la police**, recherche ou création des images de test.

### Estimation indicative

Pour un dev solo à temps partiel, par partie. À prendre comme un ordre de grandeur, pas comme un engagement.

| Partie | Estimation |
|---|---|
| 2. Préparation et mesures | 1 à 2 semaines |
| 3. Sprite batching | 2 à 3 semaines |
| 4. Caméra | 1 à 2 semaines |
| 5. Atlas de textures | 2 à 3 semaines |
| 6. Animations de sprites | 1 à 2 semaines |
| 7. Tilemaps | 2 à 3 semaines |
| 8. Texte et polices | 2 à 3 semaines |
| 9. Scène de démonstration | 1 semaine |
| **Total** | **environ 3 à 5 mois** |

### Liens avec les autres jalons

Quelques décisions de ce jalon touchent des éléments de la [roadmap](ROADMAP.md) :

- **Les entrées** (clavier, souris, manette) restent au **jalon 3**. Pour piloter la caméra pendant les tests, le bac à sable continue d'utiliser les événements SDL bruts (`Game::on_event`).
- **Une bibliothèque JSON** est nécessaire dès la partie 5 (métadonnées d'atlas). Le chargement de données du jalon 5 (avec rechargement à chaud) s'appuiera sur ce qui est mis en place ici.
- **Un éditeur de cartes** est prévu au jalon 5. Le format de carte choisi en partie 7 (Tiled ou format maison) peut le rendre inutile ou le réduire.
- **Dear ImGui** est prévu au jalon 3. Ici, le texte affiché avec notre propre moteur suffit pour les statistiques.
- **Les tests unitaires** peuvent démarrer ici : c'est le moment prévu par le jalon 1.

### Questions générales

- Quel est l'**objectif de performance** ? Par exemple : 5 000 à 10 000 sprites visibles, à la cadence de l'écran. Attention, sur un écran à 165 Hz, le budget par frame n'est que d'environ **6 ms** (16,6 ms à 60 Hz).
- Quel est le **style visuel** exact ? Sprites 2D dessinés, sprites pré-rendus depuis des modèles 3D (comme Diablo 2), ou animation squelettique 2D ? Ce choix fixe le volume d'images à gérer et donc la pression sur les atlas et la mémoire (voir la partie 5). **Réponse : le jeu sera en 3D (modèles 3D), à traiter dans un jalon 3D ultérieur.** Le rendu 2D de ce jalon servira alors surtout à l'interface, aux icônes, aux effets et aux tests, et certaines parties (animations de sprites, cartes de tuiles) perdent de leur importance : voir la roadmap.
- À quelle résolution l'ARPG doit-il être **conçu** ? (Voir la partie 4.)

---

## 2. Préparation et mesures

### But

Régler la dette du jalon 1 qui gênerait le batching, et se donner de quoi **mesurer** avant d'optimiser. Sans chiffres de départ, on ne saura pas si le batching a servi à quelque chose.

### Tâches

- [ ] Valider le jalon 1 sur **Mac** : build, rendu, MSL (voir la [checklist de fin de jalon 1](JALON_1_FONDATIONS.md#9-critères-de-fin-de-jalon)).
- [x] Sortir du bac à sable la création du pipeline de sprites, pour que le **moteur** en soit propriétaire.
- [x] Envelopper les ressources GPU (texture, buffer, pipeline, sampler) dans des classes **RAII** qui se libèrent seules.
- [x] Séparer dans une frame la **phase de préparation** (copies vers le GPU) et la **passe de rendu** (voir ci-dessous).
- [x] Ajouter un projet de **tests unitaires** (doctest ou Catch2) et une cible `tests`.
- [x] Ajouter des **compteurs de performance** : temps de frame, nombre de sprites, nombre de draw calls.
- [x] Écrire un **test de charge naïf** : N sprites, un draw call chacun. Noter les chiffres de référence.

### Le point d'architecture central : la structure d'une frame

Aujourd'hui, `Renderer::begin_frame()` ouvre tout de suite une passe de rendu, et `Game::render()` dessine dedans. Or **SDL_GPU interdit les copies pendant une passe de rendu** : envoyer les sommets du batch vers le GPU doit se faire dans un *copy pass* séparé, *avant* la passe de rendu.

La frame doit donc devenir :

```
1. Le jeu enregistre ce qu'il veut dessiner (liste de sprites en mémoire CPU)
2. Fin de frame, moteur :
      a. trie et construit les données (sommets)
      b. copy pass : envoie les données au GPU
      c. render pass : efface, dessine les batchs
      d. soumet
```

Cela change l'interface : `Game::render()` ne dessine plus directement, il **remplit une file de dessin**, et le moteur exécute ensuite le travail GPU. C'est un changement d'API structurant, à faire avant d'écrire le batching.

### Questions à se poser

- **Qui possède quoi ?** Les classes RAII doivent connaître le périphérique pour se libérer. Le renderer doit rester vivant plus longtemps que toutes ses ressources. Faut-il un compteur ou une vérification à la destruction du renderer ?
- **À quoi ressemble la file de dessin ?** Un vecteur de commandes de sprites remplis par le jeu, ou un objet `SpriteBatch` que le jeu utilise directement ? Le premier découple le jeu du GPU, le second est plus simple.
- **Où placer la limite entre moteur et jeu ?** L'ARPG écrira ses systèmes de rendu au-dessus. Que doit-il voir : des sprites, des « drawables », ou rien de plus que des composants ?
- **Quel framework de tests ?** doctest (léger, un en-tête) ou Catch2 (plus complet). Les deux sont dans vcpkg. Le choix est peu engageant.
- **Que teste-t-on ?** Seulement ce qui n'a pas besoin du GPU : conversions de coordonnées, empaquetage, temps d'animation, structure de la carte. Cela oriente le découpage du code : la logique doit être séparable du rendu.
- **Les tests font-ils partie du build normal ?** Une option `MOTEUR_BUILD_TESTS` évite d'alourdir le build courant.
- **Que mesurer, et comment ?** Le temps CPU par phase (mise à jour, construction, envoi, dessin) se mesure avec `SDL_GetPerformanceCounter`. Le temps GPU est plus difficile : SDL_GPU n'offre pas d'API simple de mesure, on passe par RenderDoc ou Xcode. Un profileur comme **Tracy** (paquet vcpkg `tracy`) est une option pour plus tard.
- **Quel scénario de charge ?** Nombre de sprites, taille, part de transparence, nombre de textures. Un scénario mal choisi donne des chiffres trompeurs.
- **Quel objectif de performance retenir ?** Voir les questions générales. Il faut un chiffre écrit avant de mesurer.

### Pièges connus

- Refactorer la structure de frame **après** avoir écrit le batching : on réécrit deux fois.
- Faire dépendre les tests du GPU : ils deviennent lents, fragiles et inutilisables sur une machine de CI sans carte graphique.
- Des classes RAII copiables : une copie libère la ressource deux fois. Les rendre non copiables, déplaçables.
- Mesurer en build Debug avec la couche de validation du GPU active : les chiffres sont très pessimistes. Mesurer en Release, mode debug du GPU désactivé.

### Implémentation réalisée

Testé sur Windows uniquement. Détail des fichiers dans la [documentation des fichiers](FICHIERS_DU_PROJET.md).

**Ce qui a changé**

- **La frame a deux phases.** `Renderer::begin_frame()` acquiert seulement l'image de la fenêtre et n'ouvre plus de render pass. Le jeu **enregistre** ses sprites avec `renderer.sprites().draw(...)`. `Renderer::end_frame()` fait ensuite les copies vers le GPU (copy pass), puis le rendu (render pass), puis la soumission. `Game::render()` ne dessine donc plus directement : c'est le changement d'API structurant annoncé plus haut.
- **Le moteur possède le rendu de sprites.** Le pipeline, l'échantillonneur et le quad sont dans un nouveau `SpriteRenderer`. Le bac à sable n'a plus aucun code GPU, hormis sa texture.
- **Ressources GPU en RAII.** `GpuBuffer`, `GpuTexture`, `GpuSampler`, `GpuShader`, `GpuGraphicsPipeline` : déplaçables, non copiables, libérées seules. Les fonctions `create_*` et `load_shader` les renvoient. Cela a supprimé tout le code de libération manuelle du bac à sable.
- **Deux briques extraites de la boucle et testées** : `FixedTimestep` (pas de temps fixe) et `FrameStats` (moyenne, maximum, percentiles). Elles n'ont besoin ni de fenêtre ni de GPU.
- **Tests unitaires** avec doctest : 16 cas, 2 046 vérifications, qui passent en Debug et en Release. Une modification volontaire du plafond de rattrapage fait échouer 2 tests, ce qui prouve qu'ils détectent bien une régression.
- **Mesures intégrées.** Chaque frame est chronométrée en trois phases, sans compter l'attente de l'écran. Le titre de la fenêtre affiche FPS, temps CPU, sprites et draw calls. L'option `--report` écrit un résumé (moyenne, percentile 99, maximum) à la fermeture.
- **Test de charge.** `--sprites N` ajoute N sprites qui rebondissent, avec une graine fixe : la scène est identique à chaque lancement et sur chaque OS.

**Décisions prises**

- **Forme de la file de dessin** : le jeu enregistre via `renderer.sprites().draw(texture, position, taille)` ; le moteur exécute la file. Le batching changera l'exécution, pas l'enregistrement.
- **Framework de tests : doctest** (léger, un en-tête). Tests compilés par défaut, désactivables avec `MOTEUR_BUILD_TESTS=OFF`.
- **Propriété des ressources** : le `Renderer` doit vivre plus longtemps que ses ressources. Cette règle est documentée, pas vérifiée à l'exécution : pas de compteur de ressources vivantes pour l'instant.
- **Mesures** : temps CPU **hors attente de l'écran**, en Release (GPU debug désactivé), 60 premières frames ignorées, moyenne + percentile 99 + maximum.
- **Le sprite est encore dessiné naïvement** (un draw call et un envoi d'uniforme par sprite), volontairement : c'est la référence.

**Chiffres de référence du rendu naïf**

Conditions : Windows, Direct3D 12, NVIDIA GeForce RTX 4070 Ti SUPER, écran à 164 Hz (budget d'environ 6 ms par frame), build Release, 6 secondes par mesure. Tous les sprites utilisent la même texture. Temps en millisecondes.

*Sans VSync* (pour voir le coût réel, sans être plafonné par l'écran) :

| Sprites | FPS | CPU moyen | CPU p99 | CPU max | dont `end_frame` |
|---|---|---|---|---|---|
| 1 | 5 046 | 0,122 | 0,296 | 0,814 | 0,120 |
| 1 001 | 4 327 | 0,157 | 0,339 | 0,691 | 0,153 |
| 5 001 | 2 606 | 0,330 | 0,569 | 0,993 | 0,319 |
| 10 001 | 1 727 | 0,560 | 0,898 | 1,537 | 0,535 |
| 20 001 | 982 | 0,996 | 1,406 | 3,034 | 0,946 |
| 40 001 | 515 | 1,887 | 2,441 | 4,241 | 1,800 |

*Avec VSync* (cadence de l'écran) :

| Sprites | FPS | CPU moyen | CPU p99 | CPU max |
|---|---|---|---|---|
| 1 001 | 165 | 0,552 | 2,127 | 2,967 |
| 10 001 | 165 | 1,008 | 3,466 | 4,812 |
| 20 001 | 164 | 1,373 | 3,068 | 4,692 |

**Ce que ces chiffres disent**

- **Un coût fixe d'environ 0,12 ms par frame** (soumission et présentation), puis **environ 45 ns de CPU par sprite** : (0,560 − 0,122) / 10 000 ≈ 44 ns, et (1,887 − 0,122) / 40 000 ≈ 44 ns. La croissance est linéaire.
- **Le rendu naïf tient déjà 20 000 sprites à la cadence de l'écran** : 1,4 ms de CPU sur un budget de 6 ms. L'objectif « 10 000 sprites » du jalon est **déjà atteint sans batching**, sur cette machine.
- Le rendu est **limité par le CPU** à 40 000 sprites : 515 FPS correspondent à 1,94 ms par frame, pour 1,89 ms de CPU. Je n'ai pas mesuré le temps GPU.

**Précautions de lecture**

- **Une seule machine, et une machine puissante.** Metal (Mac) et des cartes plus modestes peuvent avoir un coût par draw call très différent. Je n'ai aucun chiffre pour eux.
- **Scène très favorable** : une seule texture (aucun changement de texture entre sprites), tous les sprites de la même taille, aucun tri, aucune teinte. Une vraie scène d'ARPG (nombreuses textures, tri en profondeur, effets) coûtera plus cher.
- **Ne pas comparer les modes VSync et sans VSync.** Le CPU moyen est plus élevé avec VSync (0,552 ms contre 0,157 ms à 1 001 sprites). Je n'ai pas vérifié pourquoi ; une hypothèse est que le processeur passe en mode économie d'énergie pendant l'attente de l'écran. Comparer des mesures faites dans le même mode.
- **Le temps GPU n'est pas mesuré.** Un rendu limité par le GPU (beaucoup de surdessin, grosses textures) ne se voit pas dans ces chiffres.

**Conséquence sur la suite du jalon**

Le batching n'est plus une optimisation urgente ; il reste pertinent pour trois raisons : la marge sur des machines plus faibles et sur Mac (à mesurer), les effets et particules d'un ARPG qui peuvent dépasser largement 20 000 quads, et les fonctions par sprite (teinte, tri) qui alourdiraient le chemin naïf. Deux options pour la partie 3 :

1. **Relever l'objectif** (par exemple 50 000 sprites à la cadence de l'écran) et faire du batching l'outil pour l'atteindre.
2. **Mesurer d'abord sur Mac** avant de décider de l'urgence.

Dans les deux cas, le critère de réussite du batching devient chiffré : un coût CPU par sprite **nettement inférieur aux 45 ns mesurés**, et un nombre de draw calls égal au nombre de lots.

### Validation

- [ ] Le jalon 1 est validé sur Mac.
- [ ] Une commande lance les tests unitaires, et ils passent sur les deux OS. *Windows vérifié (Debug et Release), Mac à faire.*
- [x] Le test de charge naïf donne des chiffres de référence notés dans ce document (nombre de sprites, FPS, temps CPU).
- [x] Le bac à sable ne contient plus de code de création de pipeline.

---

## 3. Sprite batching

### But

Dessiner un grand nombre de sprites avec très peu de draw calls, en regroupant les sprites qui partagent la même texture et le même état.

### Le principe

| Approche | Draw calls | Envois d'uniformes | Coût CPU |
|---|---|---|---|
| Actuelle (1 sprite = 1 draw) | N | N | Très élevé |
| **Batching** | 1 par lot | 1 par frame | Faible |

Le jeu accumule les sprites dans un tableau en mémoire. Au moment de dessiner, on remplit un grand buffer de sommets, on l'envoie au GPU d'un coup, et on dessine tout en une ou quelques commandes.

### Esquisse d'API (illustrative)

```cpp
SpriteBatch batch(renderer);
batch.begin(camera);
batch.draw(region, position, tint);   // appelé pour chaque sprite
batch.draw(region, position, tint);
batch.end();                          // trie, envoie, dessine
```

### Tâches

- [x] Définir la structure d'un sprite (position, taille, rectangle de texture, teinte, pivot, profondeur). *Sans pivot : il arrive avec les atlas (partie 5).*
- [x] Créer un buffer de sommets **dynamique**, réécrit à chaque frame.
- [x] Créer un buffer d'indices **statique** (le motif `0,1,2,0,2,3` répété).
- [x] Remplacer la matrice `mvp` par sprite par une matrice **vue-projection unique** par frame (le shader reçoit des positions en coordonnées monde).
- [x] Trier les sprites (profondeur, puis texture) et découper en lots. *Tri sur la profondeur seule, stable ; la texture n'entre pas dans la clé (voir les décisions).*
- [x] Gérer le dépassement de capacité d'un lot.
- [x] Ajouter la **teinte** par sprite (couleur multipliée à la texture).
- [x] Ajouter le retournement horizontal et vertical (échange des coordonnées de texture).
- [x] Exposer des statistiques : sprites, lots, draw calls, octets envoyés. *Un « lot » est un draw call.*
- [x] Écrire le test de charge batché et comparer aux chiffres de référence de la partie 2.

### Comment remplir le buffer de sommets

| Approche | Principe | Avantages | Inconvénients |
|---|---|---|---|
| **A. Sommets construits côté CPU** | 4 sommets complets par sprite dans un buffer dynamique | La plus simple, marche partout | 4 fois plus de données à envoyer |
| **B. Instanciation** | 1 « instance » par sprite ; le vertex shader construit le quad à partir de `SV_VertexID` | Données réduites (1 entrée par sprite) | Un peu plus de subtilité dans le pipeline |
| **C. Buffer de stockage** | Les sprites sont lus depuis un buffer de stockage par le vertex shader | Très flexible | Liaisons de ressources plus complexes, à valider sur Metal |

**Recommandation** : commencer par **A**, qui a le moins d'éléments à valider sur Mac, puis mesurer. Passer à B seulement si les mesures montrent que l'envoi des sommets est un goulot.

### Questions à se poser

**Structure d'une frame**
- **Quelle forme donner à la file de dessin ?** Voir la partie 2 : elle doit être finalisée avant d'écrire ce code.
- **Faut-il un ou plusieurs passes de rendu par frame ?** Une seule passe (monde, puis interface) reste simple ; plusieurs passes deviennent nécessaires pour des effets plein écran ou une résolution virtuelle.

**Buffer dynamique**
- **Comment éviter de bloquer le GPU ?** Réécrire un buffer que le GPU est encore en train de lire cause une attente. SDL_GPU propose l'option de « cycle » (`cycle = true`) à l'envoi : buffers, textures et zones de transfert y fonctionnent comme des tampons circulaires, et si la ressource est déjà liée, SDL passe à la suivante disponible (voir l'introduction de `SDL_gpu.h`). C'est le mécanisme prévu pour ce cas, à maîtriser avant d'écrire le batch.
- **Quelle capacité prévoir ?** Fixe (par exemple 16 384 sprites par lot) ou extensible ? Que faire quand elle est dépassée : lot supplémentaire, ou agrandissement ?
- **Un buffer par frame en vol ou un seul avec le cycle ?** Cela dépend de la réponse à la question précédente.
- **Quelle taille pour les indices ?** Avec des indices sur 16 bits (valeurs de 0 à 65 535), un lot ne peut pas dépasser 65 536 sommets, soit 16 384 sprites. Des indices sur 32 bits lèvent cette limite au prix de plus de mémoire.
- **Quel format pour la teinte ?** 4 octets normalisés (RGBA8) sont plus compacts que 4 flottants.

**Tri et état**
- **Comment concilier tri en profondeur et regroupement par texture ?** Pour un rendu correct avec de la transparence, il faut dessiner de l'arrière vers l'avant. Mais trier par profondeur mélange les textures et **multiplie les lots**. C'est le compromis central du rendu 2D : les atlas (partie 5) le réduisent en mettant beaucoup d'images dans une même texture.
- **Quelle clé de tri ?** Couche, puis profondeur, puis texture ? Est-elle assez stable pour éviter le scintillement entre deux sprites à profondeur égale ?
- **Comment gérer plusieurs modes de mélange ?** Le mélange alpha classique, l'additif (effets de lumière, feu), le multiplicatif. Chaque mode est un pipeline différent et **interrompt** un lot. À prévoir dès la clé de tri.
- **Faut-il une zone de découpe (scissor)** pour l'interface (listes défilantes, inventaire) ? Elle interrompt aussi un lot.

**Pré-multiplication de l'alpha**
- **Passe-t-on à l'alpha pré-multiplié maintenant ?** Le jalon 1 a retenu l'alpha non pré-multiplié en signalant qu'il faudrait le revoir avec les atlas. Le pré-multiplié donne un filtrage correct et évite les halos sur les bords, mais impose de convertir les images (au moment de l'empaquetage, partie 5) et de changer l'état de mélange. À trancher **avant** de produire des atlas.

**API et architecture**
- **Le batch est-il utilisable en dehors de `Game::render` ?** Par exemple pour dessiner l'interface avec une autre caméra.
- **Comment le jeu désigne-t-il une texture ?** Un pointeur, un identifiant, ou un objet `SpriteRegion` qui contient la texture et le rectangle ? Cela sera repris par les atlas.
- **Rotation et échelle par sprite ?** Utiles pour les projectiles et effets ; elles coûtent des calculs de sinus par sprite ou plus de données.

### Pièges connus

- Envoyer des données pendant une passe de rendu : invalide, il faut un copy pass avant.
- Réutiliser un transfer buffer ou un buffer GPU avant que le GPU ait fini de le lire.
- Dépasser 65 536 sommets par draw call avec des indices 16 bits : sprites qui disparaissent ou apparaissent au mauvais endroit.
- Un lot vide qui provoque un draw call à zéro élément, ou une taille de buffer à zéro.
- Trier à chaque frame **toutes** les données avec un tri instable : scintillement d'ordre entre deux sprites à profondeur égale.
- Compter uniquement les FPS pour comparer : un test de charge qui dépasse le VSync ne montre rien. Mesurer le **temps CPU** par frame, avec `--no-vsync`.

### Implémentation réalisée (partie 3)

Testé sur Windows uniquement. Détail des fichiers dans la [documentation des fichiers](FICHIERS_DU_PROJET.md).

**Ce qui a changé**

- **`SpriteBatcher`** (nouveau, logique pure sans GPU) : reçoit les sprites de la frame, les trie si besoin, construit les sommets et les découpe en lots. C'est la partie la plus testée du moteur.
- **`SpriteRenderer`** exécute le résultat : un buffer de sommets dynamique envoyé en un seul transfert, un buffer d'indices statique, **une matrice vue-projection par frame** (et non plus une par sprite), un draw call par lot.
- **`SpriteRenderer::draw(texture, position, taille, SpriteOptions)`** : les options sont le rectangle de texture, la teinte, la profondeur et les retournements. L'appel de base ne change pas.
- **Shaders** : le vertex shader reçoit désormais des positions en pixels, des coordonnées de texture et une teinte par sommet ; le fragment shader multiplie la texture par la teinte. Le MSL pour Mac a été régénéré.
- **Statistiques** : `RenderStats` compte aussi les octets envoyés au GPU.
- **Bac à sable** : `--no-batching` (un draw call par sprite, image identique), `--depth` (ordre d'enregistrement mélangé, tri par hauteur) et `--freeze-after N` (fige la scène, pour comparer deux captures).
- **Tests** : 32 cas et 2 104 vérifications, dont 16 cas pour le batcher (un test compare le tri à une référence sur 3 000 profondeurs aléatoires ; un test de mutation a confirmé qu'ils détectent un tri cassé).

**Décisions prises**

- **Sommets construits côté CPU** (approche A) : 4 sommets de 20 octets par sprite (position, coordonnées de texture, teinte sur 4 octets normalisés), soit 80 octets par sprite. C'est l'approche qui a le moins d'éléments à valider sur Mac.
- **Indices sur 16 bits, statiques**, pour 16 384 quads. Un draw call ne dépasse jamais 16 384 sprites ; les lots suivants atteignent leurs sommets grâce au décalage de sommet de base (`vertex_offset`) du draw call.
- **Un seul buffer de sommets pour toute la frame**, de 16 384 sprites au départ, **doublé** quand une frame en demande plus (l'agrandissement est écrit dans les logs). L'ancien buffer est libéré par SDL dès que le GPU a fini de s'en servir. La zone de transfert a la même taille.
- **Envoi avec `cycle = true`** sur la zone de transfert et sur le buffer : SDL fournit un nouveau buffer interne si le précédent est encore lu, sans attendre. Seule la partie utilisée est envoyée.
- **Tri sur la profondeur seule, croissante, stable.** Le tri n'a lieu que si les sprites n'ont pas déjà été enregistrés dans l'ordre : un simple suivi de la profondeur maximale, à coût nul, le détecte. Les sprites de même profondeur restent dans leur ordre d'enregistrement.
- **La texture n'entre pas dans la clé de tri.** Regrouper par texture avant de trier en profondeur donnerait un rendu faux avec de la transparence. La conséquence est celle annoncée plus haut : ce sont les **atlas** (partie 5) qui réduiront le nombre de lots.
- **Tri par base (radix)** sur 4 passes de 8 bits, sur des clés entières dérivées de la profondeur flottante. Il ne déplace que 8 octets par sprite au lieu de 56 pour la structure entière, et il est stable.
- **Un nouveau lot** commence quand la texture change ou quand la limite de 16 384 sprites est atteinte.
- **Un seul mode de mélange** (alpha classique) : aucun changement d'état ne casse encore un lot. Les modes additif et multiplicatif restent à faire.
- **Alpha non pré-multiplié conservé** : la décision reste à prendre avant les atlas (partie 5).
- **Pas de rotation ni de pivot** pour l'instant.
- **Une texture est identifiée par son pointeur GPU** dans le batcher, ce qui le garde indépendant du GPU et testable.

**Vérifications visuelles** (captures de la fenêtre, 3 000 sprites teintés, scène figée)

- **Batché contre un draw call par sprite : 0 pixel de différence**, sans et avec tri.
- **Deux lancements identiques : 0 pixel de différence** (la scène est déterministe).
- **Tri en profondeur sur le GPU** : le sprite principal est enregistré **en premier** mais a la profondeur maximale. Ses 540 texels de test sont tous intacts, alors que 162 texels transparents de son coin sont recouverts par de petits sprites qui passent bien derrière. Sans tri, l'image est différente de 398 402 pixels : l'ordre compte, et le test le détecte.
- **Non-régression** : les pixels de référence du jalon 1 (quadrants, fond, contour, halo, frontière nette) sont identiques ; la teinte blanche par défaut ne change rien.
- **Agrandissement du buffer** : avec 40 001 sprites, le buffer passe de 16 384 à 65 536 sprites (5 Mio), et le rendu fait 3 draw calls, sans erreur, en Debug avec la validation du GPU active et la vérification du tas.

**Mesures** (Release, mêmes conditions que la partie 2 : Direct3D 12, RTX 4070 Ti SUPER, écran à 164 Hz, une seule texture, temps en millisecondes)

*Batching, sans VSync :*

| Sprites | FPS | CPU moyen | CPU p99 | CPU max | dont `record` | dont `end_frame` | Draw calls | Envoyé |
|---|---|---|---|---|---|---|---|---|
| 1 001 | 4 108 | 0,164 | 0,422 | 2,215 | 0,017 | 0,145 | 1 | 78 Kio |
| 10 001 | 2 096 | 0,428 | 0,793 | 4,983 | 0,180 | 0,243 | 1 | 781 Kio |
| 40 001 | 780 | 1,259 | 1,777 | 2,533 | 0,658 | 0,594 | 3 | 3 125 Kio |
| 160 001 | 165 | 5,977 | 7,284 | 8,274 | 2,665 | 3,089 | 10 | 12 500 Kio |

*Comparaison avec le rendu naïf de la partie 2 (un draw call et un uniforme par sprite) :*

| Sprites | Naïf | Batching | Gain |
|---|---|---|---|
| 10 001 | 0,560 ms | 0,428 ms | −24 % |
| 40 001 | 1,887 ms | 1,259 ms | −33 % |

Le coût par sprite passe d'environ 44 ns à environ 28 ns.

*Isoler chaque coût, sans VSync :*

| Mesure | 10 001 sprites | 40 001 sprites |
|---|---|---|
| Batching (référence) | 0,428 ms | 1,259 ms |
| Un draw call par sprite (`--no-batching`) | 0,577 ms | 1,930 ms |
| Tri par base, ordre mélangé (`--depth`) | 0,509 ms | 1,893 ms |

- **Un draw call coûte environ 15 à 17 ns** de CPU (l'écart entre les deux premières lignes, divisé par le nombre de sprites).
- **Le tri en ordre mélangé coûte environ 16 ns par sprite** à 40 001 sprites (+0,63 ms). Avec un `std::stable_sort` classique, mesuré avant de passer au tri par base, le surcoût était de 3,2 ms : le tri par base est environ 5 fois moins cher.

*Avec VSync (cadence de l'écran) :*

| Sprites | FPS | CPU moyen | CPU p99 | CPU max |
|---|---|---|---|---|
| 40 001 | 164 | 1,629 | 3,056 | 5,166 |
| 80 001 | 165 | 2,718 | 3,804 | 7,395 |

**Ce que ces chiffres disent**

- **L'objectif est dépassé** : 80 001 sprites tiennent la cadence de l'écran, avec 2,7 ms de CPU sur un budget d'environ 6 ms.
- **Le gain du batching est réel mais modeste** (−33 % à 40 001 sprites), parce qu'un draw call est très bon marché sur cette machine. C'est le tri et la construction des sommets qui coûtent maintenant.
- **Où va le CPU à 40 001 sprites** : environ la moitié dans `record` (le code du bac à sable qui appelle `draw`, et l'ajout au batcher), l'autre moitié dans `end_frame` (construction des sommets, copie, envoi, dessin). Deux pistes pour plus tard : réduire la taille de `SpriteDesc`, ou construire les sommets dès l'enregistrement.
- **Au-delà de 80 000 sprites, le coût monte plus vite que linéairement** : 160 001 sprites coûtent 6 ms, et envoient 12,5 Mio par frame.

**Précautions de lecture**

- Mêmes limites qu'en partie 2 : **une seule machine puissante, aucun chiffre pour Metal**, le temps GPU n'est pas mesuré.
- **Une seule texture** : la scène de test forme un ou quelques lots. Une scène réelle avec beaucoup de textures triées en profondeur ferait alterner les textures et **rapprocherait le nombre de lots du nombre de sprites**. Le coût réel se situe entre la ligne « Batching » et la ligne « Un draw call par sprite ».
- La scène de test a des teintes par sprite, l'ancienne référence n'en avait pas : la comparaison est légèrement à l'avantage de l'ancienne.

**Incident : un plantage venait d'un build incrémental périmé, pas du code**

Pendant cette partie, le bac à sable a planté en Debug avec « heap corruption detected — wrote memory after end of heap buffer ». Un build propre a fait disparaître le problème. La cause : **Ninja ne suivait plus les dépendances d'en-têtes**, donc modifier `sprite_batcher.hpp` (qui a changé la taille de la classe) n'avait pas recompilé les fichiers qui l'incluent. Ces fichiers créaient encore l'objet avec l'ancienne taille, d'où l'écriture hors limites.

- **Pourquoi** : MSVC affiche « Remarque : inclusion du fichier : » avec des **espaces insécables**, dont l'octet dépend de la page de codes de la console (`ff` en 850, `a0` en 1252, `c2 a0` en UTF-8). CMake écrit ce texte dans `build.ninja` au moment de la configuration ; Ninja le compare aux lignes de `cl` au moment du build. **Si les deux ne se font pas sous la même page de codes, plus aucune dépendance d'en-tête n'est enregistrée**, sans aucun message d'erreur. Ici, le `build.ninja` généré par CLion contenait l'octet `ff` (page 850, d'où je déduis que CLion configure sous cette page), alors que mon shell construisait sous la page 65001 (`c2 a0`).
- **Symptôme** : la sortie de build contient des centaines de lignes « Remarque : inclusion du fichier : … ».
- **Ton environnement CLion n'était pas en cause** : configuration et build y utilisent la même page de codes. C'est la manière dont j'ai lancé les builds qui a produit le binaire fautif.
- **Correctif** : je construis désormais sous `chcp 850`, comme CLion, et j'ai vérifié que Ninja enregistre bien 247 dépendances pour `sprite_renderer.cpp` et recompile tous ses dépendants quand `sprite_batcher.hpp` change. Voir aussi la section « Problèmes connus » du [README](../moteur/README.md).
- **Les mesures précédentes restent valides** : elles ont été refaites après un build propre, et donnent les mêmes valeurs.

**Ce qui n'est pas vérifié**

- **Tout le côté Mac** : Metal, le format de sommet d'octets normalisés pour la teinte, la liaison des buffers, le MSL régénéré.
- **Le temps GPU**, et les scènes riches en textures.
- **Plusieurs modes de mélange**, la rotation, le pivot, un scissor pour l'interface.
- **La couche de validation D3D12** : aucun message vu, mais je n'ai pas vérifié qu'elle est installée sur ta machine.

### Validation

- [x] Le rendu batché est **identique au pixel près** au rendu naïf sur une scène de test (comparer deux captures). *0 pixel de différence, sans et avec tri.*
- [x] L'objectif de sprites fixé tourne à la cadence de l'écran, en Release. *80 001 sprites à 165 FPS ; coût par sprite d'environ 28 ns contre 44 ns mesurés avant.*
- [x] Le nombre de draw calls est égal au nombre de changements de texture ou de mode de mélange, pas au nombre de sprites. *Vérifié par les tests unitaires (changement de texture, plafond de 16 384) et par les mesures (3 draw calls pour 40 001 sprites d'une seule texture). Un seul mode de mélange est implémenté.*
- [x] Les statistiques affichées sont cohérentes avec la scène.
- [x] Aucun message de la couche de validation du GPU. *Aucun message vu sur Windows. La partie 10 a confirmé, en provoquant volontairement une erreur d'usage de l'API, que la validation est bien active : son absence de message ici est donc un vrai résultat, pas une couche inactive.*
- [ ] Même comportement sur Windows et sur Mac. *Windows vérifié, Mac à faire.*

---

## 4. Caméra

### But

Une caméra qui définit ce qu'on voit du monde : position, zoom, conversion entre coordonnées écran et monde, et projection isométrique pour l'ARPG.

### Vocabulaire

| Espace | Description |
|---|---|
| **Monde** | Là où vivent les objets du jeu (unités du monde ou tuiles) |
| **Vue** | Le monde vu depuis la caméra (translation, zoom) |
| **Clip** | Coordonnées normalisées du GPU, produites par la matrice de projection |
| **Écran** | Pixels de la fenêtre, origine en haut à gauche |

### L'isométrie en deux mots

Pour une projection isométrique 2:1 (la plus courante dans les jeux 2D), une position `(x, y)` dans la grille de tuiles devient, à l'écran :

```
écran_x = (x - y) * largeur_tuile / 2
écran_y = (x + y) * hauteur_tuile / 2
```

Avec des tuiles de 64×32 pixels par exemple. L'inverse (de l'écran vers la grille) sert à savoir **sur quelle tuile est la souris**, ce qui est indispensable pour un ARPG (clic pour se déplacer, survol d'objets au sol).

### Tâches

- [x] Créer une classe `Camera2D` : position, zoom, taille de la vue.
- [x] Calculer la matrice vue-projection (remplace le `mvp` du jalon 1).
- [x] Convertir écran vers monde et monde vers écran.
- [x] Calculer le **rectangle visible** en coordonnées monde, pour ne dessiner que ce qui est vu.
- [x] Écrire les conversions **grille isométrique vers écran** et l'inverse.
- [x] Trouver la **tuile sous la souris** (avec la conversion des coordonnées de la souris en pixels physiques).
- [x] Piloter la caméra dans le bac à sable : déplacement (clavier ou souris), zoom (molette). *Clavier (flèches, ZQSD ou WASD) et molette.*
- [x] Interpoler la position de la caméra entre deux pas fixes, comme pour les sprites.
- [x] Tests unitaires : aller-retour écran, monde, écran ; cas limites ; tuile sous un point connu.

### Questions à se poser

**Résolution et zoom**
- **Rendu à la résolution native ou à une résolution virtuelle ?** Deux familles de choix :
  - *Native + zoom* : le monde est dessiné à la résolution réelle de la fenêtre, avec un facteur de zoom. Simple, mais les sprites changent de taille apparente selon l'écran.
  - *Résolution virtuelle* : tout est dessiné dans une texture de taille fixe (par exemple 640×360), puis agrandie à la fenêtre. Rendu identique partout et pixel art parfait, mais demande une **cible de rendu hors écran** et une passe d'agrandissement, et complique l'interface (texte net).
- **Le zoom est-il continu ou par paliers entiers ?** Un zoom fractionnaire avec un filtrage `NEAREST` fait **scintiller** les pixels (certains texels sont plus larges que d'autres). Des paliers entiers (×1, ×2, ×3) évitent ce défaut.
- **La caméra s'aligne-t-elle sur la grille de pixels ?** Une position fractionnaire produit des artefacts sur le pixel art. Arrondir la position de la caméra au pixel supprime le problème, mais rend le mouvement moins doux.

**Unités et conventions**
- **Quelle unité pour le monde ?** Le jalon 1 a retenu le pixel physique avec Y vers le bas. Pour un monde isométrique, il est plus naturel d'exprimer le monde **en tuiles** (flottants), puis de projeter. Quel est l'espace « de vérité » pour la logique de jeu : la grille (avant projection) ou l'écran ?
- **Comment gère-t-on la hauteur (axe Z) ?** Un objet en l'air se décale vers le haut de l'écran. Faut-il prévoir un Z dès maintenant ?
- **Quelle taille de tuile ?** Un rapport 2:1 (64×32, 128×64) est le plus courant. Le choix conditionne la taille des assets.
- **La caméra peut-elle tourner ?** Dans un ARPG isométrique classique, non : cela simplifie beaucoup l'art et le tri.

**Densité de pixels (Retina)**
- **En quelles unités sont la souris et la fenêtre ?** SDL fournit des positions en *points*, le rendu est en *pixels*. Sur Mac Retina, le rapport est de 2. Il faut convertir explicitement pour la souris, sinon la tuile sous le curseur est décalée.
- **Comment garder une taille d'interface lisible ?** Un facteur d'échelle de l'interface, indépendant de la densité de pixels.

**Comportement**
- **La caméra suit-elle le joueur ?** Directement, ou avec un lissage ? Le lissage doit utiliser le temps du pas fixe, sinon il varie avec la cadence d'affichage.
- **La caméra a-t-elle des limites** (bords de la carte) ? Des secousses (tremblement d'écran) ?
- **Où vit l'interface ?** Elle a besoin d'une seconde caméra fixe, en pixels d'écran.

### Pièges connus

- Mélanger points et pixels : souris décalée sur Mac, fonctionnelle sur Windows.
- Inverser un signe sur Y ou sur la projection : monde à l'envers ou tuile sous la souris décalée d'une demi-tuile.
- Une caméra à position fractionnaire avec des tuiles : **coutures** visibles entre elles (une ligne de pixels de fond apparaît).
- Précision des flottants avec de très grandes coordonnées (au-delà de quelques dizaines de milliers de pixels).
- Tester la tuile sous la souris seulement au centre de l'écran : les erreurs se voient aux bords.
- Oublier que la projection dépend de la taille de la fenêtre : recalculer au redimensionnement (déjà fait au jalon 1 pour le sprite).

### Implémentation réalisée (partie 4)

Testé sur Windows uniquement. Détail des fichiers dans la [documentation des fichiers](FICHIERS_DU_PROJET.md).

**Ce qui a été fait**

- **`Camera2D`** (logique pure, testée) : position, zoom, taille de la vue, matrice vue-projection, conversions écran / monde dans les deux sens, rectangle visible, alignement sur les pixels et interpolation.
- **`IsoProjection`** (logique pure, testée) : conversions grille / monde en projection isométrique 2:1, tuile sous un point, et plage de tuiles à dessiner pour un rectangle visible.
- **`window_to_pixels()` et `Application::to_pixels()`** : conversion de la souris des points vers les pixels.
- **`SpriteRenderer::set_view_projection()`** : la caméra remplace la projection en pixels d'écran. Sans caméra, les sprites restent placés directement en pixels d'écran, comme avant.
- **Bac à sable** : une scène `--iso` avec une carte de tuiles, la caméra pilotable, un repère à l'origine du monde et la tuile sous la souris surlignée. Options `--map`, `--zoom`, `--camera`, `--mouse`, `--interleave` et `--no-input` pour des essais reproductibles.
- **Assets** : trois losanges 64×32 (deux sols et un surlignage).
- **Tests** : 28 nouveaux cas, soit 60 cas et 3 222 vérifications au total.

**Décisions prises**

- **Résolution native + zoom.** La résolution virtuelle demande une cible de rendu hors écran et une passe d'agrandissement ; elle sera reprise avec l'interface.
- **Le zoom est un facteur quelconque dans la caméra**, mais le bac à sable le pilote **par paliers entiers** (1 à 8) : pas de scintillement.
- **Alignement sur les pixels activé par défaut.** La translation écran est arrondie à un pixel entier. Le dessin et le picking utilisent la même translation arrondie, donc ils ne se contredisent jamais.
- **Sens des mots** : `position` est le point du monde affiché au centre de la fenêtre ; `zoom` est le nombre de pixels d'écran par pixel du monde ; `écran = monde × zoom + translation`.
- **Le monde est en pixels** (à zoom 1, Y vers le bas), et **la grille isométrique est une projection à part** : `IsoProjection`. La caméra ne connaît pas les tuiles. L'espace « de vérité » de la logique de jeu (grille ou monde) reste à confirmer avec le gameplay de l'ARPG : le moteur fournit les deux conversions.
- **Hauteur** : un paramètre `height` en pixels de `to_world()`, qui remonte le point à l'écran. Pas d'axe Z complet pour l'instant.
- **Tuiles de 64×32** (rapport 2:1) dans le bac à sable, réglables dans `IsoProjection`.
- **Pas de rotation de caméra.**
- **Interpolation de la caméra** : `begin_update()` mémorise la position, `interpolated(alpha)` donne une copie placée entre les deux pas. **La même caméra interpolée sert à dessiner et à pointer**, donc la tuile sous la souris reste la bonne pendant un mouvement.
- **Une seule caméra par frame** : `set_view_projection()` s'applique à toute la frame. Une seconde caméra fixe pour l'interface demandera de découper les lots par vue : c'est repoussé à la partie 8.
- **Vitesse de déplacement constante à l'écran**, quel que soit le zoom.
- **Entrées : événements SDL bruts**, comme prévu (l'abstraction reste au jalon 3).

**Vérifications**

*Tuile sous la souris.* Pour chaque cas, la tuile attendue est calculée avec **ma propre formule écrite dans le script de test, indépendante du code du moteur**. Je compare la tuile annoncée par le programme, la couleur du losange à l'endroit attendu et les quatre tuiles voisines, qui ne doivent pas être surlignées.

| Zoom | Souris | Caméra | Tuile attendue | Résultat |
|---|---|---|---|---|
| 1 | (10, 10) | milieu de la carte | (9, 28) | identique |
| 1 | (1270, 10) | milieu de la carte | (28, 9) | identique |
| 1 | (10, 710) | milieu de la carte | (31, 50) | identique |
| 1 | (1270, 710) | milieu de la carte | (50, 31) | identique |
| 1 | (640, 360) | milieu de la carte | (30, 30) | identique |
| 2 | (300, 200) | milieu de la carte | (24, 30) | identique |
| 2 | (900, 500) | milieu de la carte | (34, 30) | identique |
| 3 | (777, 333) | (100,3 ; 200,7) | (8, 3) | identique |
| 4 | (123, 456) | milieu de la carte | (28, 32) | identique |
| 2 | (640, 361) | (50,5 ; 900,3) | (28, 27) | identique |

**10 cas sur 10**, aux quatre coins de l'écran, à quatre niveaux de zoom et avec deux positions de caméra fractionnaires.

- **Coutures** : 12 combinaisons (zooms 1 à 4, trois positions de caméra fractionnaires), sur une zone de 285 600 pixels entièrement couverte par la carte. **0 pixel de fond** dans chacune.
- **Échelle** : le repère à l'origine du monde a la bonne taille aux zooms 1, 2 et 3.
- **Redimensionnement** : la fenêtre passe à 984×661 ; la tuile attendue (28, 30) est bien celle mesurée.
- **Clavier** : maintenir D pendant 0,6 s déplace le repère de 360, 370 et 370 pixels aux zooms 1, 2 et 3 (attendu : 600 px/s × 0,6 s = 360, quel que soit le zoom).
- **Molette** : la largeur de la zone jaune du repère passe de 12 à 24, 36 et 48 pixels à chaque cran vers le haut, et revient à 36 après un cran vers le bas.
- **Tests** : 60 cas. Une projection isométrique volontairement fausse fait échouer 5 tests ; ignorer l'alignement sur les pixels en fait échouer 2.
- **Debug avec validation du GPU** : aucun message.

Mon premier essai du clavier et de la molette a donné des résultats nuls : la fenêtre n'avait pas le focus dans mon script de test. Un clic simulé pour donner le focus a suffi, ce n'était pas un défaut du moteur.

**Mesures** (Release, Windows, RTX 4070 Ti SUPER, sans VSync, entrées réelles ignorées avec `--no-input`, temps en millisecondes)

| Scène | FPS | CPU moyen | CPU p99 | CPU max | Sprites | Draw calls | Envoyé |
|---|---|---|---|---|---|---|---|
| Carte 60×60, zoom 1 | 3 512 | 0,212 | 0,434 | 1,096 | 2 305 | 3 | 180 Kio |
| Carte 200×200, zoom 1 | 3 505 | 0,213 | 0,444 | 3,770 | 2 305 | 3 | 180 Kio |
| Carte 1000×1000, zoom 1 | 3 336 | 0,226 | 0,464 | 0,878 | 2 305 | 3 | 180 Kio |
| Carte 1000×1000, zoom 2 | 4 155 | 0,168 | 0,386 | 0,730 | 677 | 3 | 53 Kio |
| Carte 1000×1000, zoom 4 | 4 392 | 0,154 | 0,355 | 0,850 | 257 | 3 | 20 Kio |
| Carte 1000×1000, textures alternées | 2 438 | 0,353 | 0,661 | 1,373 | 2 305 | 2 258 | 180 Kio |

**Ce que ces chiffres disent**

- **Le culling fonctionne** : le nombre de tuiles envoyées ne dépend pas de la taille de la carte (2 305 sprites pour 3 600 comme pour un million de cases), et il diminue avec le zoom. *Limite : la carte de test n'est pas stockée (la texture se déduit de la parité de `i + j`), donc la mémoire d'une vraie carte n'est pas testée : c'est la partie 7.*
- **Alterner deux textures tuile par tuile casse le batching** : 2 258 draw calls au lieu de 3, et le CPU passe de 0,226 à 0,353 ms (+56 %). Cela donne environ **56 ns par draw call qui change de texture**, contre 15 à 17 ns pour un draw call simple (partie 3) : **un changement de texture coûte environ trois fois plus**. C'est la démonstration chiffrée de l'intérêt des atlas (partie 5). Le bac à sable regroupe donc les tuiles par texture, ce qui est légitime pour un sol qui ne se recouvre pas ; `--interleave` garde le cas défavorable.
- **Une scène de 2 305 tuiles coûte environ 0,21 ms de CPU**, soit environ 3,5 % du budget de 6 ms à 165 Hz.

Un incident de mesure à retenir : mes premières mesures donnaient des nombres de sprites qui changeaient d'un lancement à l'autre (par exemple 1 201 au lieu de 2 305). Des **entrées réelles** (molette, touches) atteignaient la fenêtre pendant la mesure et modifiaient le zoom. L'option `--no-input` les ignore, et les chiffres ci-dessus sont ceux obtenus avec elle.

**Ce qui n'est pas vérifié**

- **Écran Retina et Mac** : la conversion points / pixels n'est testée qu'en test unitaire (avec un facteur 2 simulé), pas sur un vrai écran à haute densité.
- **Le scintillement pendant un déplacement fractionnaire** : les captures d'écran ne le mesurent pas. Les paliers entiers de zoom et l'alignement sur les pixels le suppriment par construction, mais c'est à juger à l'œil.
- **La fluidité du mouvement de la caméra** et le **temps GPU**.
- **Deux écrans de densités différentes**, et le comportement des touches et de la molette sur Mac.

### Validation

- [x] Aller-retour écran, monde, écran : erreur inférieure à 0,001 sur toute la fenêtre (test unitaire).
- [x] La tuile sous la souris est correcte aux quatre coins de l'écran, à plusieurs niveaux de zoom. *10 cas sur 10, contre une formule indépendante.*
- [ ] Le déplacement et le zoom ne produisent ni scintillement ni couture. *Aucune couture (0 pixel de fond sur 12 combinaisons) ; le scintillement est à juger à l'œil.*
- [ ] Les positions de souris sont correctes sur écran Retina. *Conversion testée en test unitaire seulement.*
- [ ] Le résultat est identique sur Windows et sur Mac. *Windows vérifié, Mac à faire.*

---

## 5. Atlas de textures

### But

Regrouper de nombreuses petites images dans quelques grandes textures, pour que des sprites différents partagent la même texture et donc le même lot.

### Pourquoi c'est vital pour un ARPG

Chaque changement de texture interrompt un lot. Or un ARPG affiche des centaines d'objets différents. Sans atlas, chaque type d'objet est un lot séparé. Avec des atlas, tout un ensemble de monstres, d'objets au sol et d'effets tient dans quelques pages.

**Mesuré en partie 4** : un sol de 2 305 tuiles avec deux textures alternées tuile par tuile donne 2 258 draw calls au lieu de 3, et 0,353 ms de CPU au lieu de 0,226 (+56 %). Un draw call qui change de texture coûte environ 56 ns, soit trois fois un draw call simple. C'est l'ordre de grandeur de ce que les atlas font gagner.

### Deux approches

| Approche | Principe | Usage |
|---|---|---|
| **Hors ligne** | Un outil assemble les images au *build* et produit une image d'atlas + un fichier de description | Personnages, décors, objets : tout ce qui est connu à l'avance |
| **À l'exécution** | Le moteur place des images dans un atlas au fil de l'eau | Glyphes de police, contenu généré ou chargé dynamiquement |

**Recommandation** : l'approche hors ligne d'abord (c'est elle qui compte pour le contenu), l'approche à l'exécution pour le texte (partie 8).

### Un atout par rapport aux shaders

Contrairement à shadercross (impossible à installer sur Mac), l'outil d'empaquetage est **notre propre code C++**. Il se compile sur les deux OS et se lance au build partout : plus besoin de fichiers pré-générés versionnés.

### Tâches

- [x] Définir le **format de description** de l'atlas (JSON) : pages, noms, rectangles, décalages, pivot.
- [x] Écrire l'outil `atlas_packer` (cible CMake séparée) : lit un dossier d'images, produit une ou plusieurs pages PNG et le JSON.
- [x] Utiliser `stb_rect_pack.h` (déjà installé avec le paquet `stb`) pour l'empaquetage.
- [x] Ajouter la marge et l'extrusion des bords autour de chaque image.
- [x] Rogner les zones transparentes et enregistrer le décalage correspondant.
- [x] Ajouter une dépendance **JSON** (par exemple `nlohmann-json`, disponible dans vcpkg) pour lire et écrire la description.
- [x] Écrire la classe runtime `TextureAtlas` : charge le JSON et les pages, retrouve une région par nom.
- [x] Définir `SpriteRegion` : texture, rectangle de texture, taille, pivot.
- [x] Intégrer l'empaquetage au build (commande CMake personnalisée, comme pour les shaders).
- [x] Tests unitaires : aucune superposition, tout est dans les limites de la page, résultat **déterministe**.

### Format de description (exemple)

```json
{
  "pages": [{ "file": "monsters.png", "width": 2048, "height": 2048 }],
  "frames": {
    "skeleton_walk_e_00": {
      "page": 0,
      "x": 130, "y": 4, "w": 61, "h": 92,
      "source_w": 128, "source_h": 128,
      "offset_x": 34, "offset_y": 20,
      "pivot_x": 30, "pivot_y": 88
    }
  }
}
```

### Ordre de grandeur de la mémoire

Pour un jeu à sprites pré-rendus en 8 directions :

| Élément | Calcul | Résultat |
|---|---|---|
| 1 image de 128×128 en RGBA8 | 128 × 128 × 4 | 64 Kio |
| 1 animation (8 directions × 12 images) | 96 × 64 Kio | 6 Mio |
| 1 monstre (10 animations) | 10 × 6 Mio | 60 Mio |

Avec des dizaines de types de monstres, on atteint vite le gigaoctet. Le rognage, le miroir des directions (5 stockées au lieu de 8), des tailles de sprite plus modestes et le chargement par zone deviennent des sujets sérieux. C'est la raison pour laquelle le choix du style visuel doit être fait tôt.

### Questions à se poser

**Format et outil**
- **Hors ligne, à l'exécution, ou les deux ?** Voir plus haut.
- **Quelle taille de page ?** 2048×2048 ou 4096×4096 ? Les limites de texture dépendent du GPU (souvent 16 384 sur les machines récentes *(à vérifier)*) ; une page plus grande contient plus de sprites mais consomme plus de mémoire même si elle est à moitié vide.
- **Quel format de fichier pour les pages ?** Le PNG est le seul format lu par notre chargeur (stb, limité au PNG). Les formats compressés pour le GPU (BC7 sur Windows, ASTC sur Apple Silicon) divisent la mémoire par 4 à 8, mais sont différents selon l'OS. À garder pour plus tard.
- **Comment organiser les fichiers source ?** Un dossier d'art source (`art/`) distinct des ressources produites (`assets/`). Faut-il versionner les images sources avec Git LFS ?
- **Quel identifiant pour un sprite ?** Une chaîne de caractères (pratique, données JSON lisibles), un identifiant numérique haché (rapide), ou une énumération générée. Le contenu de l'ARPG (objets, monstres) référencera les sprites par nom dans ses fichiers de données.

**Qualité visuelle**
- **Marge et extrusion : combien de pixels ?** Sans marge, lorsqu'un sprite est échantillonné près de son bord (zoom fractionnaire, rotation), la couleur d'une image voisine de l'atlas « bave » dessus. Une extrusion de 1 à 2 pixels des bords de chaque image règle le problème.
- **Alpha pré-multiplié ?** À décider ici, au moment de la conversion (voir la partie 3). Le faire dans l'outil d'empaquetage est le plus propre : les images sources restent normales.
- **Faut-il rogner les zones transparentes ?** Cela économise beaucoup d'espace mais impose de conserver la taille d'origine et le décalage pour repositionner correctement. Cela demande d'être rigoureux sur le pivot.

**Organisation**
- **Comment répartir les sprites entre pages ?** Toutes les images d'une même animation doivent tenir sur la même page, sinon un lot est interrompu au milieu d'une animation. Les personnages fréquemment affichés ensemble gagnent à partager une page.
- **Le pivot** : pour un personnage, le point de référence est généralement les **pieds**, en bas au centre. C'est lui qui donne la position dans le monde et la profondeur pour le tri. Il peut varier légèrement d'une image à l'autre : il est donc enregistré par image.
- **Comment mettre à jour un atlas sans tout reconstruire ?** L'empaquetage au build doit être incrémental (ne pas repasser sur des milliers d'images si rien n'a changé) ; c'est aussi lié à la vitesse du build.

### Pièges connus

- **Bavure de couleur** entre images voisines de l'atlas (marge insuffisante).
- Coordonnées de texture décalées d'une demi-texel : rectangles trop larges ou trop étroits d'un pixel.
- Un empaquetage **non déterministe** (ordre de parcours des fichiers variable selon l'OS) : les builds Windows et Mac produisent des atlas différents et des JSON qui changent à chaque commit. Trier les fichiers par nom avant l'empaquetage.
- Un atlas plus grand que la limite du GPU : échec à la création de la texture, parfois seulement sur une machine.
- Perdre le décalage de rognage : le sprite apparaît à côté de sa position.
- Mélanger alpha pré-multiplié et non pré-multiplié entre l'atlas et l'état de mélange : bords sombres ou clairs autour des sprites.

### Implémentation réalisée (partie 5)

Testé sur Windows uniquement. Détail des fichiers dans la [documentation des fichiers](FICHIERS_DU_PROJET.md).

**Ce qui a été fait**

- **`build_atlas()`** (logique pure, testée) : rogne, ajoute la marge, empaquette en une ou plusieurs pages et remplit les pages. Elle ne dépend d'aucun fichier ni du GPU.
- **`atlas_packer`** (outil en ligne de commande, cible CMake) : parcourt un dossier de PNG, appelle `build_atlas()` et écrit `<nom>.json` et `<nom>_0.png`, `<nom>_1.png`…
- **`moteur_add_atlas()`** (module CMake) : lance l'outil au build, seulement quand une image change. Contrairement aux shaders, l'outil est **notre propre code**, donc la même commande tourne sur Windows et sur Mac, sans fichiers pré-générés.
- **`TextureAtlas`** : lit le JSON et les pages, et retrouve une région par son nom.
- **`SpriteRegion` et `place_region()`** : la région d'un sprite, et le calcul de son placement selon le pivot, l'échelle et le retournement.
- **`SpriteRenderer::draw(region, ancre, échelle, options)`** : dessine un sprite d'atlas de façon que **son pivot tombe exactement sur l'ancre**.
- **Alpha pré-multiplié** : décision de la partie 3 tranchée (voir plus bas).
- **Sources d'art** dans `art/` (nouveau dossier), séparées des ressources produites. Deux atlas : `world` (le sol isométrique, avec un fichier `pivots.json`) et `test` (19 images variées dont 8 images de marche).
- **Bac à sable** : la scène isométrique lit désormais ses tuiles dans l'atlas `world`, et une scène `--atlas` affiche tout l'atlas de test.
- **Tests** : 89 cas et 4 797 vérifications, plus un benchmark désactivé par défaut.

**Format de description** (version 1). Les clés sont triées, donc le fichier est identique d'une exécution à l'autre.

```json
{
  "frames": {
    "walk_03": {
      "h": 53, "w": 28, "x": 119, "y": 1, "page": 0,
      "source_w": 48, "source_h": 64,
      "offset_x": 10, "offset_y": 8,
      "pivot_x": 24, "pivot_y": 64
    }
  },
  "pages": [{ "file": "test_0.png", "width": 256, "height": 256 }],
  "version": 1
}
```

`x`, `y`, `w`, `h` situent l'image **rognée** dans la page (sans la marge). `offset_x` et `offset_y` disent où elle se trouvait dans l'image d'origine, et le pivot est exprimé dans l'image **d'origine**. Ici, `walk_03` passe de 48×64 à 28×53.

**Décisions prises**

- **Alpha pré-multiplié : oui, appliqué par le moteur au chargement** (`create_texture`), et non par l'outil d'empaquetage. Les PNG et les atlas restent en alpha normal, donc visibles et modifiables dans n'importe quel éditeur d'image. Le mélange devient `source + fond × (1 − alpha de la source)`, et le shader pré-multiplie aussi la teinte. Le filtrage linéaire donne alors des bords corrects, sans halo. Vérifié : les pixels de référence des jalons 1 et 4 sont **identiques** après le changement.
- **Hors ligne d'abord.** L'empaquetage à l'exécution (glyphes, contenu dynamique) est repoussé à la partie 8.
- **Taille des pages : 2 048 au maximum, mais chaque page est la plus petite qui convienne** (puissances de deux, la plus carrée à surface égale). Un petit ensemble donne une petite page ; seuls ceux qui dépassent 2 048 sont répartis sur plusieurs pages pleines.
- **Marge de 1 pixel, remplie par extrusion** : chaque pixel de la marge est une copie du pixel de bord le plus proche. Réglable (`--padding`).
- **Rognage activé** (`--no-trim` pour le désactiver). Une image entièrement transparente devient un pixel transparent, pour que le sprite existe quand même.
- **Pivot par défaut : en bas au milieu de l'image d'origine** (les pieds d'un personnage). Un fichier optionnel `pivots.json` dans le dossier source le fixe par image, en pixels de l'image d'origine. Le sol isométrique l'utilise (32, 0), le sommet du losange.
- **Identifiant d'un sprite : une chaîne**, le chemin relatif au dossier source sans extension et avec des `/` (`monsters/skeleton/walk_00`). Les sous-dossiers servent donc à ranger. Le contenu de l'ARPG les référencera par leur nom dans ses fichiers de données.
- **JSON : `nlohmann-json`** (paquet vcpkg), pour lire comme pour écrire.
- **Le PNG reste le format des pages** ; les formats compressés pour le GPU (BC7, ASTC) sont repoussés.
- **Sources d'art dans `art/`**, ressources produites dans le dossier `assets/` à côté de l'exécutable. **Git LFS pas encore décidé.**
- **Un retournement met aussi le pivot et le décalage en miroir** : un personnage retourné garde ses pieds où ils étaient.

**Choix de conception qui évitent des bugs difficiles**

- **Le tri de `stb_rect_pack` est remplacé.** Il utilise `qsort`, qui range les éléments **égaux** dans un ordre différent selon la bibliothèque C (Windows, Linux, macOS). Sans précaution, deux machines auraient produit des atlas différents. Un tri stable maison le remplace, et l'ordre d'empaquetage est entièrement déterminé (hauteur, largeur, puis nom). Un test mélange les entrées de trois façons et exige un résultat identique.
- **Les noms sont triés octet par octet**, sans dépendre de la langue du système.
- **L'outil et le runtime ne lisent les fichiers qu'avec `SDL_LoadFile`** (chemins UTF-8), pas avec `fopen`.

**Vérifications**

*Le rendu contre les PNG sources* : la vérité de terrain est le fichier source, lu à part. Pour chaque pixel opaque, la couleur affichée à l'écran doit être exactement celle du fichier, à la position calculée d'après le pivot ; pour un pixel semi-transparent, la couleur mélangée au fond (tolérance de 2).

| Vérification | Pixels testés | Écarts |
|---|---|---|
| Grille des 19 sprites de l'atlas, chacun à son pivot (les pixels recouverts par un autre sprite ou par un repère sont exclus) | 34 540 | **0** |
| `walk_03` normal, échelle 1 | 3 054 | **0** |
| `walk_03` retourné, échelle 1 | 3 054 | **0** |
| `walk_03` normal, échelle 2 | 12 270 | **0** |
| `walk_03` retourné, échelle 3 | 27 630 | **0** |

Cela vérifie ensemble le rognage, le décalage restitué, le pivot, l'échelle, le retournement autour du pivot, les pixels semi-transparents (l'ombre et l'anneau à bords doux) et l'alpha pré-multiplié.

*Non-régression de la scène isométrique* : ses tuiles viennent maintenant de l'atlas `world`, où elles sont rognées (le losange passe de 64 à 62 pixels de large) et placées par leur pivot. Le picking et les coutures de la partie 4 sont revérifiés : la tuile sous la souris est la bonne dans 5 cas sur 5 (deux coins de l'écran, zooms 1 à 4, une caméra fractionnaire), et il y a **0 pixel de fond** sur 285 600 à chacun des quatre zooms.

*Le build*

- **Déterminisme** : trois productions (deux appels de l'outil et le build) donnent exactement les mêmes octets (empreintes SHA-256 identiques).
- **Reconstruction ciblée** : modifier une image ne repaquette que son atlas ; sans changement, rien n'est refait.
- **Image ajoutée** : prise en compte au build suivant.
- **Image retirée** : ce cas **échouait** au premier essai, l'atlas gardait l'image supprimée. Quand un fichier disparaît, il ne reste aucune dépendance plus récente que la sortie, donc rien ne se relançait. Corrigé par un fichier qui liste les entrées et ne change que si cette liste change. Vérifié : après ajout puis retrait, on retrouve **exactement** le JSON d'origine.

*Les erreurs de chargement* : sur une copie dont j'abîme les fichiers un par un, chaque cas donne un message qui nomme le fichier et le problème.

| Cas | Message |
|---|---|
| JSON tronqué | « … is not valid: parse error at line 12 … » |
| Version de format inconnue | « format version 2 is not supported (expected 1) » |
| Sprite absent | « has no sprite named 'walk_03' (it holds 18 sprites) » |
| Taille de page fausse | « page 'test_0.png' is 256x256 but the atlas declares 512x256 » |
| Sprite hors de sa page | « sprite 'walk_03' lies outside its page » |
| Champ obligatoire manquant | « key 'pivot_y' not found » |
| Page PNG absente | « Cannot read image … » |
| Fichier JSON absent | « cannot read the file … » |

*Les tests* : 89 cas. Deux mutations volontaires de l'empaquetage (une extrusion fausse, une marge oubliée) sont détectées, par des assertions de bornes du vecteur en Debug. *Limite : en Release, ces bornes ne sont pas vérifiées, la détection y reposerait sur les comparaisons de contenu.*

**Mesures**

*Effet sur le batching* (Release, sans VSync, scène isométrique de 2 305 tuiles). La partie 4 avait montré que deux textures alternées cassent le batching. Avec les deux tuiles dans un seul atlas :

| Tuiles alternées tuile par tuile | Draw calls | CPU moyen |
|---|---|---|
| Deux textures séparées (partie 4) | 2 258 | 0,353 ms |
| **Une page d'atlas** | **2** | **0,273 ms** |

L'écart entre « alternées » et « regroupées » tombe de 0,127 ms à 0,03 ms : **l'ordre d'enregistrement n'a plus d'importance**, tant que tout est dans la même page.

*Vitesse de l'outil* (Release, images de 8 à 128 pixels de côté) :

| Images | Temps | Pages | Pixels d'images / pixels des pages |
|---|---|---|---|
| 200 | 7 ms | 1 | 46 % |
| 1 000 | 30 ms | 2 | 86 % |
| 3 000 | 91 ms | 4 | 81 % |
| 8 000 | 242 ms | 10 | 92 % |

L'empaquetage n'est pas un souci de temps de build. Les petits ensembles gaspillent davantage (46 % pour 200 images) à cause de la contrainte de puissance de deux : c'est le prix de cette contrainte.

*Compacité* : l'atlas de test est passé de 1024×256 (**7,9 %** d'occupation) à 256×256 (**31,6 %**), et l'atlas du monde de 256×64 à 64×128.

**Défauts trouvés en route**

- **Page allongée et gaspilleuse** : ma première version remplissait une seule rangée sur toute la largeur autorisée (2 048) avant de passer à la suivante, d'où 7,9 % d'occupation. Vu en regardant la sortie de l'outil, corrigé par la recherche de la plus petite page qui convient.
- **Image retirée non prise en compte** : voir plus haut.
- **Un build en échec m'a donné des résultats trompeurs** : un binaire mélangé (nouveau shader, ancien code de mélange) a affiché un surlignage de tuile faux. Rien n'était cassé dans le moteur : j'avais oublié de contrôler que la compilation avait réussi avant de vérifier les pixels. Je contrôle désormais le code de sortie du build avant toute vérification visuelle.

**Ce qui n'est pas vérifié**

- **Le déterminisme entre Windows et Mac** : trois exécutions sous Windows sont identiques, et les deux causes connues de divergence (l'ordre d'égalité de `qsort`, le tri des noms) sont neutralisées, mais **aucune exécution n'a eu lieu sur Mac**. C'est le test décisif.
- **Les bavures entre images voisines** : le filtrage du moteur est `NEAREST`, qui ne peut pas faire baver un sprite dans le rectangle voisin. La marge et l'extrusion sont testées en test unitaire, mais leur effet visible n'apparaîtra qu'avec un **filtrage linéaire**, que le moteur n'utilise pas encore.
- **La taille des fichiers PNG** : l'atlas de test pèse 11,5 Kio, contre 5,5 Kio pour les 19 sources. L'encodeur PNG de stb compresse moins bien que les outils habituels. Sans importance ici, à évaluer sur de vrais volumes.
- **Les limites de texture du GPU** (souvent 16 384 sur les machines récentes) : une page reste à 2 048 au maximum.
- **Les chemins avec des accents** dans l'outil : la conversion UTF-8 est prévue mais n'a été testée qu'avec des chemins ASCII.
- **La mémoire réelle d'un jeu** : les images de test sont petites.

### Validation

- [ ] L'outil produit les mêmes fichiers deux fois de suite, et les mêmes sur Windows et sur Mac. *Identique sur Windows (trois exécutions, mêmes octets) ; Mac à faire.*
- [x] Aucune superposition et aucun dépassement de page (test unitaire). *300 images aléatoires sur plusieurs pages.*
- [ ] Aucune bavure visible autour des sprites, à plusieurs niveaux de zoom. *Non vérifiable avec le filtrage `NEAREST` ; marge et extrusion testées en test unitaire seulement.*
- [x] Le rognage et le pivot placent les sprites au bon endroit (comparaison avec l'image d'origine). *80 548 pixels comparés aux PNG sources, 0 écart.*
- [x] La recherche d'une région par nom fonctionne et signale clairement un nom inconnu.
- [x] Le build reconstruit l'atlas seulement quand une image source change. *Y compris après l'ajout et le retrait d'une image.*

---

## 6. Animations de sprites

### But

Faire jouer des séquences d'images, avec le bon rythme, dans la bonne direction, et déclencher des événements à des images précises.

### Pourquoi c'est plus qu'un simple « changer d'image »

Dans un ARPG, l'animation est **liée au gameplay** : le coup d'une attaque touche à une image précise, et la vitesse d'attaque modifie la vitesse de lecture de l'animation (c'est une mécanique centrale des ARPG comme Path of Exile). L'animation doit donc être **déterministe** et **pilotée par la simulation**, pas seulement décorative.

### Tâches

- [x] Définir un `AnimationClip` : liste d'images, durée de chaque image, mode de lecture (une fois, boucle, aller-retour).
- [x] Écrire un lecteur `AnimationPlayer` : temps écoulé, image courante, terminé ou non.
- [x] Ajouter un **multiplicateur de vitesse** de lecture.
- [x] Ajouter des **événements** attachés à des images (par exemple « touche l'ennemi », « pas de marche »).
- [ ] Gérer les **directions** : choisir l'ensemble d'images selon l'angle (8 ou 16 directions). *Hors du périmètre réduit : le jeu étant en 3D, les personnages ne seront pas des séquences d'images par direction.*
- [x] Gérer la symétrie : réutiliser des images retournées pour économiser de la mémoire. *Rien à ajouter au moteur : `flip_x` retourne déjà l'image autour du pivot (partie 5). La démo s'en sert pour les créatures qui vont vers la gauche.*
- [x] Charger les définitions depuis des fichiers de données (JSON).
- [x] Relier les animations aux noms de régions de l'atlas.
- [x] Tests unitaires du temps, des boucles, des événements et des vitesses.

### Questions à se poser

**Temps et déterminisme**
- **Sur quelle horloge tourne l'animation ?** Sur le **pas fixe** de la simulation (déterministe, indispensable si des événements d'animation déclenchent des dégâts) ou sur le temps réel de la frame (plus fluide mais dépendant de la cadence) ? Pour un ARPG, la première est la bonne base ; l'interpolation de rendu peut lisser le reste.
- **Comment représenter la durée d'une image ?** En secondes flottantes, ou en **ticks entiers** du pas fixe ? Les entiers évitent toute dérive et rendent les résultats identiques sur les deux OS.
- **Que se passe-t-il quand le temps saute d'un coup ?** Si la simulation avance de plusieurs pas, les événements des images sautées doivent quand même être déclenchés, dans l'ordre. Sinon, un coup peut « passer à travers » l'ennemi.
- **Comment gérer la vitesse d'attaque ?** Le multiplicateur change la durée de chaque image. Il faut décider où il s'applique : sur le temps du lecteur, ou sur la durée des images ?

**Directions et contenu**
- **8 ou 16 directions ?** Le nombre multiplie le volume d'images (voir la partie 5). 8 est la norme en isométrique.
- **Symétrie par retournement ?** On stocke 5 directions au lieu de 8 et on retourne les autres. Cela économise environ 37 % de mémoire, mais impose d'avoir un retournement horizontal dans le batch **et** de retourner le pivot.
- **Quel style d'animation ?** Séquences d'images (le plus simple, ce que décrit cette partie), ou animation squelettique 2D (plus souple, moins de mémoire, mais un nouveau système complet). C'est un choix de direction artistique qui change fortement le travail moteur : à trancher avant de produire du contenu.
- **Comment équiper un personnage ?** Certains ARPG 2D superposent plusieurs sprites (corps, armure, arme) qui jouent les mêmes animations. Le moteur doit-il gérer plusieurs couches par personnage, synchronisées ?

**Architecture**
- **Où vit l'état d'animation ?** Comme simple structure de données, sans dépendance au rendu, pour pouvoir devenir un composant de l'ECS au jalon 3. Éviter une classe qui « se dessine elle-même ».
- **Comment les événements sont-ils communiqués ?** Par callback, par liste d'événements consultée par le jeu, ou par file ? La liste consultée est la plus simple à déboguer et à rendre déterministe.
- **Comment décrire les animations dans les données ?** Un fichier JSON par personnage, ou par type d'animation ? Les noms d'images suivent-ils une convention (`monstre_action_direction_index`) qui permet de générer les clips automatiquement ?

### Pièges connus

- Des durées en flottants qui s'accumulent : décalage progressif entre deux machines.
- Une erreur d'un cran à la frontière de boucle (première ou dernière image jouée deux fois).
- Des événements manqués quand le pas de temps est grand.
- Un retournement horizontal qui oublie de retourner le pivot : le personnage « glisse » d'un côté à l'autre.
- Une vitesse de lecture à zéro ou négative (division par zéro, boucle infinie).

### Implémentation réalisée (partie 6)

**Périmètre réduit.** Le jeu étant en 3D, on garde ce qui servira aussi aux animations 3D et aux effets 2D (icônes animées, effets en séquence d'images) : un lecteur **déterministe** piloté par le pas fixe, la vitesse variable et des événements fiables. Les 8 directions et les couches d'équipement sont laissées de côté. Fichiers : `animation.hpp` / `animation.cpp`, tests dans `test_animation.cpp`.

- **`AnimationClip`** : images (nom de sprite + durée en **ticks entiers**), mode (`Once`, `Loop`, `PingPong`) et événements (un nom sur une image). Le constructeur refuse un clip sans image, une image de moins d'un tick ou un événement sur une image qui n'existe pas. Il précalcule les « pas » d'un cycle : en aller-retour, les images du retour s'ajoutent sans répéter celles des extrémités (a b c d c b).
- **`AnimationPlayer`** : de simples données (un pointeur vers le clip, un temps, une vitesse), sans lien avec le rendu, prêtes à devenir un composant d'ECS. `advance(ticks, &événements)` est appelé une fois par tick de simulation ; `region()` donne le sprite à dessiner.
- **Temps et vitesse en entiers** : le temps est compté en **millièmes de tick** (`int64`) et la vitesse en millièmes (`kSpeedOne = 1000` pour ×1, `set_speed(1.5)` arrondi au millième). Aucune dérive de flottants : deux machines donnent exactement la même image au même tick.
- **Événements** : à chaque `advance`, le lecteur liste les débuts d'image situés dans l'intervalle **(avant, après]**. Ces intervalles se touchent sans se chevaucher, donc chaque début d'image est vu une fois et une seule, même quand un grand pas saute plusieurs images ou plusieurs boucles. La première image compte comme atteinte au premier `advance` après `play()`. Le jeu fournit la liste à remplir (pas d'allocation par lecteur), et un événement se déclenche au tick où son image devient visible.
- **Vitesse nulle ou négative** : 0 met en pause, une vitesse négative lève une exception.
- **`set_time()`** place un lecteur à un point du cycle sans déclencher l'image courante : la démo s'en sert pour que la foule ne marche pas au pas.
- **`AnimationLibrary`** : lit un fichier JSON versionné (`assets/animations.json`, format décrit dans `animation.hpp`), les clips étant triés par nom. Une image peut être un simple nom (durée `frame_ticks`) ou `{ "region", "ticks" }`. `check_regions(atlas)` vérifie au chargement que chaque image existe dans l'atlas, plutôt que d'échouer en pleine partie. Toute erreur nomme le fichier.
- **Démo** : les 3 000 créatures jouent le clip `walk` (les 8 images `walk_00` à `walk_07`, 6 ticks chacune, un événement `step` sur les images 1 et 5), à une vitesse liée à leur vitesse de déplacement (×0,75 à ×1,25), retournées quand elles vont vers la gauche de l'écran. La superposition compte les événements `step` reçus.

**Vérifications faites**

- 15 cas de tests unitaires (image à chaque tick dans les trois modes, ×2, ×0,5, ×0,7, pause, événements avec pas de 1 et de 20 ticks, clip `Once` après la fin, `set_time`, lecture et erreurs du JSON).
- **Les tests ont été vérifiés en cassant le code** : compter deux fois un début d'image fait échouer 4 tests, ne jamais déclencher la première image 3, ne parcourir qu'une boucle par `advance` 3, répéter l'image d'extrémité en aller-retour 2, ne pas bloquer le temps à la fin d'un clip `Once` 1.
- `--demo` : pas de nouveau draw call (169, comme avant), 0,75 ms de CPU par frame en Release pour 5 700 sprites. Deux lancements `--seed 42 --freeze-after 30 --capture` donnent la même image (`bd91e8b2c616`).

### Validation

- [x] Pour des durées données, l'image affichée au temps *t* est celle attendue (tests unitaires).
- [x] Chaque événement se déclenche **exactement une fois**, même avec de gros pas de temps. *Même liste avec un pas de 20 ticks qu'avec 20 pas d'un tick.*
- [x] À vitesse ×2, la séquence dure moitié moins longtemps.
- [ ] Un personnage tourne visuellement à travers les 8 directions sans saut de position. *Hors périmètre réduit (jeu en 3D). Le retournement autour du pivot, qui empêche le saut de position, est vérifié en partie 5.*
- [ ] Le résultat est identique sur Windows et sur Mac. *Identique par construction (arithmétique entière), à confirmer en lançant les tests et la démo sur Mac.*

---

## 7. Tilemaps

### But

Afficher un monde composé de tuiles : des dizaines de milliers de cases, dont seules quelques milliers sont visibles, avec un tri en profondeur correct.

### Tâches

- [x] Définir la structure `TileMap` : largeur, hauteur, calques, identifiant de tuile par case.
- [x] Définir `Tileset` : identifiant vers région d'atlas, plus des propriétés de tuile.
- [x] Dessiner les tuiles visibles (rectangle visible de la caméra converti en plage de tuiles) via le batch.
- [x] Gérer les tuiles plus hautes que leur emprise au sol (murs, arbres). *Hauteur passée à `to_world()` et marge de 2 tuiles dans `tiles_in()`.*
- [x] Trier les tuiles et les entités ensemble pour la profondeur. *Clé `x + y` (partie 9).*
- [x] Générer une carte de test (motif ou bruit) pour les mesures.
- [x] Surligner la tuile sous la souris.
- [x] Mesurer, et si besoin passer à des **blocs statiques** (voir plus bas). *Pas besoin : voir les mesures ci-dessous.*
- [x] Tests unitaires de la structure de carte et de la conversion en plage visible.

### Rendu via le batch, puis par blocs si nécessaire

| Stratégie | Principe | Quand |
|---|---|---|
| **Via le batch** | Chaque tuile visible est un sprite comme les autres | Point de départ : la plus simple, réutilise tout |
| **Blocs statiques** | La carte est découpée en blocs (par exemple 16×16 tuiles) dont les sommets sont construits **une fois** et réutilisés à chaque frame | Si la construction par frame devient un coût mesurable |

Ne passer aux blocs que sur mesure. Une carte de 200×200 tuiles fait 40 000 cases, mais seules 2 000 à 3 000 sont visibles : le batch les gère normalement.

### Questions à se poser

**Dimensions et géométrie**
- **Quelle taille de tuile ?** (Voir la partie 4.) Un rapport 2:1 est courant : 64×32, 128×64.
- **Comment traiter les éléments plus hauts que la tuile ?** Un mur dépasse au-dessus de sa case : l'image est plus grande que la tuile. Cela agrandit la marge à prévoir pour éviter qu'ils disparaissent trop tôt au bord de l'écran (*pop-in*).
- **Quelle plage de tuiles est visible ?** Avec une projection isométrique, le rectangle de l'écran devient un **losange** dans la grille : le calcul n'est pas une simple plage de lignes et colonnes.

**Ordre de dessin (le point le plus délicat en isométrique)**
- **Comment trier sols, murs et entités ?** Le sol n'a jamais besoin d'être trié : il est dessiné en premier. Les murs, arbres et entités doivent être triés entre eux. Un tri sur `x + y` (la profondeur isométrique) fonctionne pour des objets de même taille.
- **Que faire des objets qui occupent plusieurs cases ?** Un grand objet trié avec un seul point de profondeur peut passer devant ou derrière un voisin par erreur. Les solutions : le découper en morceaux d'une case, lui donner un point de tri choisi à la main, ou utiliser des cartes de profondeur pré-calculées.
- **Comment intégrer les personnages ?** La profondeur d'un personnage est celle de son **pivot** (les pieds). Les entités et les tuiles hautes passent par le même tri (voir la partie 3).

**Données**
- **Combien de calques ?** Sol, décor, objets au minimum. Chaque calque est un tableau de la taille de la carte.
- **Quelle taille pour un identifiant de tuile ?** 16 bits (65 536 tuiles différentes) suffit largement, avec quelques bits pour des indicateurs (retournement, rotation).
- **Quelle place pour la collision et la navigation ?** Elles sont au jalon 4, mais la structure de données doit déjà pouvoir accueillir des propriétés par tuile (marchable, opaque).
- **Faut-il des tuiles animées** (eau, lave) ? Cela se relie au système d'animation de la partie 6.
- **Tuiles à variantes** : plusieurs images pour la même tuile logique, choisies de façon déterministe à partir de la position, pour éviter l'effet de répétition.

**Format et outils**
- **Format maison ou Tiled ?** [Tiled](https://www.mapeditor.org) est un éditeur de cartes gratuit qui gère les cartes **isométriques**. Il existe des bibliothèques de lecture (`tmxlite` dans vcpkg *(à vérifier)*). Utiliser Tiled pourrait rendre inutile l'« éditeur de cartes minimal » du jalon 5. Mais les zones d'un ARPG sont généralement **générées de façon procédurale**, donc la carte à la main sert surtout aux tests, aux zones spéciales et aux salles pré-dessinées.
- **Comment les zones générées arriveront-elles ?** Le générateur produira ces mêmes structures en mémoire. Le rendu ne doit pas dépendre de la source de la carte.

### Pièges connus

- **Coutures** : une ligne de pixels du fond apparaît entre deux tuiles à certaines positions de caméra ou niveaux de zoom (arrondis, voir la partie 4).
- **Pop-in** : les tuiles hautes apparaissent brusquement au bord de l'écran, faute d'une marge de visibilité suffisante.
- Un tri de profondeur qui semble juste sur un cas simple puis se contredit sur des objets multi-cases.
- Trop de niveaux de zoom et un rectangle visible calculé pour un seul.
- Reconstruire les données d'une carte entière à chaque frame.
- Perdre l'alignement entre les tuiles et les entités à cause d'origines de coordonnées différentes.

### Implémentation réalisée (partie 7)

**Périmètre réduit.** En 3D, le sol ne sera plus dessiné en tuiles 2D, mais le jeu aura toujours besoin d'une **grille de cases avec des propriétés** : collisions et pathfinding (jalon 4), visibilité. On garde donc la structure de données, indépendante du rendu, et on la branche sur le rendu isométrique existant. Pas de blocs statiques (inutiles, voir la mesure) ni de lecture de fichiers Tiled (les zones seront générées). Fichiers : `tilemap.hpp` / `tilemap.cpp`, tests dans `test_tilemap.cpp`.

- **`TileId`** : 16 bits, `kNoTile` (0) pour une case vide.
- **`Tileset`** : les types de tuiles, numérotés à partir de 1. Un `TileType` porte un nom de sprite (vide si le jeu dessine la tuile autrement), `walkable` et `opaque`. Les propriétés sont dans le type, pas dans la carte : la carte reste une grille de petits nombres.
- **`TileMap`** : largeur, hauteur, et un nombre de calques qui couvrent tous toute la carte (un seul tableau, calque après calque). `at` et `set` lèvent une exception hors de la carte. `fill` remplit un calque.
- **`clip(plage)`** : la partie d'une plage de tuiles qui tombe dans la carte. Avec `IsoProjection::tiles_in()`, elle donne directement les tuiles à dessiner, sans les `max`/`min` recopiés à la main dans chaque scène.
- **`walkable(tileset, case)`** : faux hors de la carte, ou si une tuile d'un des calques n'est pas praticable. Premier usage « gameplay » de la carte.
- **Démo** : la carte est maintenant une vraie `TileMap` 100×100 à deux calques (sol en damier `tile_a`/`tile_b`, murs). Le rendu lit la carte au lieu de recalculer le motif, et **les créatures font demi-tour devant un mur** au lieu de le traverser. Elles naissent toujours sur une case praticable.

**Mesures** (Release, `--demo --no-vsync --no-input`, 100×100 tuiles, 3 000 créatures) : 5 726 sprites envoyés par frame (et non 10 000 tuiles + murs + créatures), 169 draw calls, **0,41 ms** pour construire la frame (`record`) et 0,75 ms de CPU en tout. Reconstruire les tuiles visibles à chaque frame ne coûte presque rien : les blocs statiques ne se justifient pas.

**Vérifications faites** : 6 cas de tests unitaires (numérotation du tileset, calques indépendants, `fill`, refus hors de la carte, `clip` à l'intérieur, à cheval et hors de la carte, plage visible d'une caméra dans un coin, `walkable` sur plusieurs calques). Ne regarder que le premier calque dans `walkable` fait échouer un test.

### Validation

- [x] Une carte de 100×100 tuiles se déplace à la cadence de l'écran, avec caméra et zoom. *`--demo`, partie 9.*
- [x] Seules les tuiles visibles (plus la marge) sont envoyées (compteur de statistiques). *5 726 sprites par frame au lieu de plus de 13 000.*
- [x] Aucune couture aux niveaux de zoom prévus. *Parties 4 et [TEST_MAC.md](TEST_MAC.md) (zoom 1 et 3, sur les deux OS).*
- [x] Un personnage marche derrière et devant un mur : l'ordre est correct. *Tri `x + y` de la partie 9, vérifié sur capture.*
- [x] La tuile sous la souris est surlignée correctement, y compris aux bords de la carte. *Partie 4 (quatre coins de l'écran, plusieurs zooms) ; hors de la carte, rien n'est surligné.*
- [ ] Le rendu est identique sur les deux OS. *Atlas identiques ; capture de la démo à comparer à résolution égale (voir la partie 9).*

---

## 8. Texte et polices

### But

Afficher du texte lisible et net : compteur de FPS, statistiques de débogage, et, à terme, l'interface de l'ARPG (objets, infobulles, dialogues).

### Un point propre au français

Le texte contient des accents (`é è à ç ù ô œ`) et des guillemets (`« »`). Un code qui parcourt le texte octet par octet **casse ces caractères** : ils sont codés sur plusieurs octets en UTF-8. Le décodage UTF-8 fait partie du travail dès le départ, pas d'un raffinement futur.

### Les options

| Option | Principe | Avantages | Inconvénients |
|---|---|---|---|
| **A. `stb_truetype`** | On rasterise les glyphes d'un fichier de police TrueType dans un atlas | Déjà installé (paquet `stb`), aucune dépendance de plus, rendu via notre batch | Pas de crénage avancé ni de mise en forme complexe |
| **B. SDL3_ttf** | Bibliothèque officielle SDL, basée sur FreeType | Plus riche (styles, qualité de rasterisation), moteur de texte pour le GPU *(à vérifier)* | Dépendance de plus (`sdl3-ttf`, FreeType) |
| **C. Polices bitmap** | Une image pré-dessinée contenant tous les caractères | Parfait pour du pixel art, aucune rasterisation à l'exécution | Une image par taille ; il faut la créer |
| **D. Champs de distance (SDF)** | Une image en distance permet un agrandissement net à toute taille | Une seule image pour tous les zooms | Un shader spécifique, préparation des données |

**Recommandation** : commencer par **A**. Elle réutilise le batch et les atlas déjà construits, sans dépendance nouvelle, et couvre le français. Garder une interface (`Font`, `draw_text`) qui permette de passer à B ou C plus tard sans toucher au reste du code.

### Tâches

- [x] Choisir une police libre de droits et **vérifier sa licence** (une police au format OFL, par exemple).
- [x] Rasteriser les glyphes voulus dans un atlas *(au chargement de la police, pas à l'exécution image par image : voir les décisions)*.
- [x] Décoder l'**UTF-8** en points de code.
- [x] Écrire la classe `Font` : métriques des glyphes, hauteur de ligne, crénage.
- [x] Écrire `draw_text(...)` *(`Font::draw`)* : émet un sprite par glyphe dans le batch.
- [x] Mesurer un texte (largeur, hauteur) sans le dessiner.
- [x] Ajouter l'alignement (gauche, centre, droite) et le retour à la ligne automatique.
- [x] Ajouter une couleur par texte, puis par portion. *Par texte seulement ; par portion repoussé, voir les décisions.*
- [x] Afficher les FPS et les statistiques de rendu avec ce texte.
- [x] Tests unitaires : décodage UTF-8 (accents), mesure, retour à la ligne.

### Questions à se poser

**Contenu et police**
- **Quels caractères couvrir ?** Latin de base plus Latin-1 (accents français) tient sur une petite page. Cyrillique, grec ou caractères asiatiques exigent un atlas dynamique qui rasterise à la demande. Le jeu sera-t-il traduit ?
- **Quelle police ?** Une police lisible en petite taille pour l'interface, éventuellement une police « de caractère » pour les titres. La licence doit permettre l'inclusion dans un jeu.
- **Une taille unique ou plusieurs ?** Chaque taille demande son propre atlas de glyphes (avec A et C).

**Qualité**
- **Comment rester net sur écran Retina et avec un facteur d'échelle d'interface ?** Il faut rasteriser à la taille **physique** finale (taille en points × échelle × densité de pixels), pas agrandir une petite version.
- **Filtrage `NEAREST` ou `LINEAR` ?** Le pixel art de l'interface demande `NEAREST` ; une police lissée demande `LINEAR`. Cela concerne aussi l'alignement des glyphes sur la grille de pixels.
- **Quel positionnement des glyphes ?** Aligner chaque glyphe sur un pixel entier donne un texte net ; garder des positions fractionnaires donne un espacement plus régulier mais plus flou.
- **Le texte doit-il rester lisible sur un décor chargé ?** Contour ou ombre : les dessiner deux fois, ou passer aux champs de distance.

**Mise en forme**
- **Quel niveau de mise en forme ?** Crénage (les paires comme « AV » se rapprochent) et retour à la ligne suffisent pour du latin. Les écritures qui se lient ou se réordonnent (arabe, langues indiennes) demandent une bibliothèque de mise en forme comme HarfBuzz : hors périmètre.
- **Texte enrichi** : les infobulles d'objets d'un ARPG mêlent couleurs (rare, magique, propriété), tailles et parfois icônes intégrées. Faut-il prévoir dès maintenant un petit langage de balises, par exemple `[c=orange]Feu[/c]` ? Il vaut mieux concevoir l'API pour qu'elle **puisse** l'accueillir, sans l'implémenter complètement.
- **Règles typographiques françaises** : espace insécable avant `:`, `;`, `!`, `?` et autour des guillemets. Le retour à la ligne doit-il en tenir compte ?

**Architecture**
- **Format de la texture de glyphes** : `stb_truetype` produit une image en niveaux de gris (une valeur d'opacité par pixel). La convertir en blanc avec cet alpha, dans la même texture RGBA que les sprites, permet de passer par le même pipeline. Une texture à un seul canal économise de la mémoire mais demande un shader ou un mode de mélange dédié.
- **Quand l'atlas de glyphes est-il rempli ?** Au démarrage pour une liste fixe de caractères, ou à la demande pour de nouveaux ?
- **Où le texte est-il dessiné ?** Dans la même passe que les sprites du monde, avec la caméra d'interface (fixe en pixels d'écran).

### Pièges connus

- **Parcours octet par octet** du texte : les accents s'affichent comme deux symboles étranges.
- Confondre le **haut** et la **ligne de base** d'un glyphe : le texte est décalé verticalement, et les lettres avec jambage (`g`, `p`) sont mal placées.
- Une mesure qui ne correspond pas au dessin (arrondis différents) : le texte déborde de son cadre.
- Une rasterisation à la mauvaise taille puis un agrandissement : texte flou.
- Une texture de glyphes traitée comme une image normale : bords noirs ou blancs autour des lettres (alpha mal géré).
- Une police avec une licence qui n'autorise pas la redistribution.
- Un cadre de texte qui dépend de l'OS : le rendu d'une même police doit être identique sur Windows et Mac (ce qui est le cas avec `stb_truetype`, pas avec les moteurs de rendu de texte du système).

### Implémentation réalisée (partie 8)

Testé sur Windows uniquement. Détail des fichiers dans la [documentation des fichiers](FICHIERS_DU_PROJET.md).

**Ce qui a été fait**

- **`decode_utf8`** (logique pure, testée) : décode l'UTF-8 en points de code, remplace toute séquence invalide (octet de continuation isolé, séquence tronquée, encodage surlong, substitut UTF-16) par des `U+FFFD`, un octet à la fois, pour resynchroniser sans planter.
- **`layout_text`** (logique pure, testée) : retour à la ligne automatique, alignement, retours à la ligne explicites, une règle simplifiée d'espace insécable française. Elle ne connaît ni police ni GPU : elle prend en paramètres une fonction d'avancement et une fonction de crénage, ce qui la rend testable avec de fausses métriques.
- **`Font`** : charge une police TrueType avec `stb_truetype`, rasterise une fois un jeu de caractères fixe (latin de base, Latin-1, plus œ/Œ, tirets, guillemets courbes) et empaquette les glyphes **avec l'outil d'atlas de la partie 5**, réutilisé tel quel. `Font::measure()` et `Font::draw()` partagent le même calcul de mise en page, donc ils ne peuvent pas diverger.
- **Une police** : Inter (SIL Open Font License 1.1), récupérée depuis le dépôt Google Fonts et placée dans `assets/fonts/`, avec le texte de la licence à côté.
- **Bac à sable** : une scène `--text` avec une phrase française, un paragraphe avec retour à la ligne, une démonstration des trois alignements et un compteur de FPS.
- **Tests** : 26 nouveaux cas (utf8 et mise en page), soit 114 cas et 4 852 vérifications au total.

**Un défaut trouvé en cours de route, d'une portée plus large que le texte**

En écrivant les tests d'UTF-8 avec de vrais caractères accentués dans le code source, la moitié des tests échouaient de façon incohérente (par exemple `"é"` décodé en 2 points de code au lieu d'1). La cause : **MSVC lit les fichiers source dans la page de codes du système, pas en UTF-8**, sauf si on le lui demande explicitement. Cela corrompt silencieusement tout caractère accentué écrit directement dans le code, y **compris dans un littéral `u8"..."`**, que le standard C++ garantit pourtant encodé en UTF-8 : le compilateur doit d'abord décoder les octets du fichier avant de ré-encoder, et s'il se trompe de page de codes au premier décodage, le résultat est faux dès le départ.

**Correctif** : le drapeau **`/utf-8`** ajouté pour MSVC dans `cmake/Warnings.cmake`, qui s'applique à toute la compilation Windows (source et exécution). Clang et GCC ne sont pas concernés, ils supposent déjà une source UTF-8. Cette correction dépasse le texte : **tout fichier futur qui écrirait un caractère français directement dans une chaîne de caractères** (un message d'erreur, un nom d'objet) était concerné, silencieusement, avant ce correctif.

**Décisions prises**

- **Option A retenue** : `stb_truetype`, comme prévu. Aucune dépendance de plus, réutilise le batch et l'outil d'atlas déjà écrits.
- **Rasterisation au chargement de la police, pas glyphe par glyphe à l'exécution.** Le jeu de caractères est fixe (latin de base + Latin-1 + quelques caractères), connu à l'avance : un atlas dynamique rempli à la demande n'est pas nécessaire pour du français. Cette option reste ouverte pour des écritures plus larges (cyrillique, CJK) plus tard.
- **Une seule taille de police pour l'instant** (20 pixels), fixée au chargement. Changer de taille demande de recharger la police : c'est le prix à payer pour rester net (agrandir un rendu existant le rendrait flou), conforme à la mise en garde de la doc sur l'écran Retina.
- **Filtrage hérité du reste du moteur (`NEAREST`)**, comme pour les sprites : cohérent avec le pixel art, mais un texte lissé (filtrage `LINEAR`) donnerait un rendu plus doux. À revoir si l'interface finale le demande.
- **Alpha pré-multiplié**, comme tout le reste du moteur depuis la partie 5 : le glyphe est stocké en blanc avec la couverture stb_truetype comme alpha, et la teinte est elle-même pré-multipliée dans le shader avant d'être multipliée à la texture.
- **Une seule couleur par appel à `draw()`.** Le texte enrichi (couleurs multiples, balises) est **explicitement repoussé**, mais `TextOptions` est conçu pour l'accueillir plus tard sans changer la signature de `draw()`.
- **Mesure et dessin appellent la même fonction interne** (`Font::resolve`), qui elle-même appelle `layout_text`. C'est la garantie que « la largeur mesurée correspond à la largeur dessinée », un des pièges explicitement listés plus haut.
- **Crénage** : lu à la demande via `stbtt_GetCodepointKernAdvance` (table `kern` ou `GPOS` basique), pas précalculé. Simple, mais refait le calcul à chaque mise en page ; sans effet mesurable à l'échelle d'un texte d'interface.
- **Espace insécable française simplifiée** : un signe seul parmi `: ; ! ?` est rattaché au mot précédent (espace gardée, mais plus jamais en début de ligne). Les autres règles typographiques françaises (espaces autour des guillemets, etc.) ne sont pas implémentées.
- **Aucun mot n'est coupé** : un mot plus large que la largeur maximale déborde de sa ligne plutôt que d'être tronqué au milieu.

**Vérifications**

*Tests unitaires* : 26 cas. Deux mutations volontaires (désactiver le rejet des séquences UTF-8 surlongues, désactiver la règle de ponctuation insécable) font chacune échouer exactement le test correspondant, sans toucher aux autres.

*Mesure contre le rendu réel*, en comparant `Font::measure()` à la boîte englobante des pixels effectivement allumés à l'écran (tolérance de quelques pixels, normale pour les marges typographiques) :

| Élément | Mesuré par `measure()` | Boîte de pixels observée |
|---|---|---|
| Phrase française (accents, œ, guillemets) | 266,36 × 20 | largeur 265, hauteur d'encre 14 (dans une ligne de 20) |
| Paragraphe avec retour à la ligne (largeur max 420) | 420 × 80 (4 lignes) | largeur d'encre 388 (≤ 420, cohérent avec un alignement à gauche), hauteur d'encre 77 (dans 80) |

Les écarts de hauteur (20 contre 14) reflètent la différence normale entre la hauteur de ligne (avec l'interligne) et la hauteur d'encre du texte réellement dessiné, pas un défaut.

*Alignement*, mesuré dans une boîte de 300 pixels commençant à x = 40 :

| Alignement | Boîte d'encre observée | Attendu |
|---|---|---|
| Gauche | commence à x = 41 | contre le bord gauche (x = 40) |
| Centre | centrée en x = 190 | centre de la boîte = 40 + 300/2 = 190, **exact** |
| Droite | finit à x = 338 | contre le bord droit (x = 340), à 2 px près (fin d'encre du dernier glyphe, normal) |

*Non-régression* : les pixels de référence du sprite (jalon 1) et la tuile sous la souris de la scène isométrique (partie 4) restent identiques après le grand nombre de fichiers recompilés par l'ajout du drapeau `/utf-8`.

*Performance* (Release, sans VSync) : la scène de texte (199 glyphes, 1 draw call) coûte en moyenne **0,203 ms** de CPU par frame, avec p99 à 0,43 ms. Le compteur de FPS peut donc être affiché en continu sans coût visible.

**Ce qui n'est pas vérifié**

- **Le rendu sur Mac** : rien n'a tourné, aucun changement de shader n'était nécessaire pour cette partie (le pipeline de sprites existant est réutilisé tel quel), donc le risque principal est faible, mais non nul.
- **Une police plus grande ou du texte très long** (mémoire, temps de chargement) : testé seulement avec 236 glyphes et des textes courts.
- **Le texte enrichi et le contour/l'ombre** : explicitement repoussés.
- **Les écritures non latines.**

### Validation

- [x] Une phrase française avec accents et guillemets s'affiche correctement (`Où étaient les œufs d'été ? « Ici. »`). *Vérifié à l'écran et par comparaison de la boîte d'encre à la mesure.*
- [x] La largeur mesurée d'un texte correspond à sa largeur dessinée (au pixel près). *Écart de quelques pixels près des bords, expliqué par les marges typographiques normales (voir le tableau ci-dessus) ; `measure()` et `draw()` partagent le même calcul, donc ils ne peuvent pas diverger par construction.*
- [x] Le retour à la ligne automatique et l'alignement fonctionnent. *Alignement centré exact au pixel ; retour à la ligne vérifié par test unitaire et par la scène `--text`.*
- [x] Un compteur de FPS s'affiche en continu sans ralentir le rendu (ses sprites sont dans le batch). *0,2 ms de CPU en moyenne, 1 draw call.*
- [x] Le rendu est identique sur Windows et sur Mac. *Vérifié sur les deux OS (voir [TEST_MAC.md](TEST_MAC.md), étape 8).*

---

## 9. Scène de démonstration

### But

Réunir toutes les briques dans une scène qui sert de **test de validation** et de démonstration.

### Contenu de la scène

- Une carte isométrique de 100×100 tuiles, avec au moins deux types de sol et des murs.
- Plusieurs milliers de sprites animés (8 directions) qui se déplacent, triés en profondeur avec le décor.
- Une caméra qui se déplace et zoome.
- La tuile sous la souris surlignée.
- Un affichage en texte : FPS, temps de frame, nombre de sprites, de lots et de draw calls.
- Des options de ligne de commande : nombre de sprites, graine aléatoire, durée, absence de VSync.

### Tâches

- [x] Assembler la scène dans le bac à sable (ou un nouveau programme `demo_rendu`). *`--demo`, dans le bac à sable existant ; voir « Implémentation réalisée » ci-dessous pour le périmètre réduit.*
- [x] Générer des sprites de test (formes colorées en 8 directions) si l'art réel n'existe pas encore. *Personnages : sprite `walk_03` de l'atlas de test, teinté par créature, déplacement (pas animation) en 8 directions. Murs : boîte teintée générique. Voir les décisions.*
- [x] Ajouter les options de ligne de commande de test. *`--demo`, `--seed`, `--capture` (les autres existaient déjà : `--sprites`, `--run-seconds`, `--no-vsync`, `--map`, `--freeze-after`).*
- [x] Ajouter la **capture d'image** (voir ci-dessous). *`Renderer::request_capture()` + `write_capture_png()`.*
- [ ] Écrire un script de mesure qui lance la scène et enregistre les résultats. *Pas fait : `--report` donne déjà les chiffres bruts (voir partie 3 et 10) ; un script dédié n'apporterait rien de plus tant qu'il n'y a pas plusieurs scènes à comparer automatiquement.*

### La capture d'image, pour des tests automatiques

Jusqu'ici, les vérifications visuelles passaient par la lecture de pixels à l'écran depuis un script externe. C'est fragile (fenêtres qui se superposent, bordures, résolution). Le moteur pourrait produire lui-même une image de la frame : lire la texture du swapchain vers un buffer CPU (**readback**), puis l'écrire en PNG.

Avantages :
- Des tests **reproductibles** : scène déterministe (graine fixe, nombre de pas fixe), capture, comparaison avec une image de référence.
- La comparaison **Windows / Mac** devient automatisable, ce qui répond directement au critère « rendu identique sur les deux OS ».

### Questions à se poser

- **Implémente-t-on la capture d'image ?** Elle demande un envoi du GPU vers le CPU (`SDL_DownloadFromGPUTexture`, qui existe dans l'API, avec une zone de transfert en mode téléchargement) et l'écriture d'un PNG (`stb_image_write`, déjà installé). Elle vaut l'effort dès que les vérifications deviennent nombreuses.
- **Quelle tolérance pour comparer deux images ?** Le pixel art doit être identique au pixel près ; les dégradés ou le texte lissé peuvent différer légèrement selon le pilote graphique.
- **Comment rendre la scène déterministe ?** Graine aléatoire fixe, pas de dépendance au temps réel, nombre de pas fixé.
- **Où stocker les images de référence ?** Dans le dépôt, par OS si elles diffèrent.
- **La démo est-elle un programme séparé ?** Cela évite d'alourdir le bac à sable, qui reste un terrain d'expérimentation.

### Implémentation réalisée (partie 9)

**Périmètre réduit.** Le jeu final sera en 3D (voir les décisions de style visuel) ; ce moteur 2D ne servira plus qu'à l'UI, aux icônes et aux effets. Les parties 6 (animations) et 7 (tilemaps dédiées) n'avaient donc pas été construites en amont comme le prévoyait l'ordre initial du jalon (elles l'ont été ensuite, en version réduite : voir leurs sections « Implémentation réalisée »). La scène de démonstration (`--demo`) réutilise ce qui existe déjà (grille isométrique procédurale de `--iso`, atlas, texte, caméra) plutôt que d'attendre ces deux parties :

- **Carte** : 100×100 par défaut (`--map` pour changer), deux types de sol (`tile_a`/`tile_b`, comme `--iso`) et des **murs procéduraux** : un quadrillage de lignes de grille avec des trous tous les 4 tuiles (effet « pièces avec portes »), calculé à la volée (`is_wall(i, j)`), sans donnée de niveau ni art dédiés. Les murs utilisent la texture générique du sprite principal, teintée, faute d'art de mur réel.
- **Créatures** : 3 000 par défaut (`--sprites`), positionnées et mises en mouvement par un générateur pseudo-aléatoire à graine fixe (`--seed`, comme le `Random` déjà utilisé pour le test de charge). Chacune se déplace dans l'une des 8 directions de la grille (`{-1,0,1}²` sans `(0,0)`), rebondit sur les bords de la carte. **Simplification assumée** : le sprite `walk_03` de l'atlas de test est utilisé tel quel (teinté par créature), sans les animations de la partie 6 — cohérent avec le nouveau statut de ce moteur (UI/effets, pas le rendu de gameplay final).
- **Tri en profondeur** : la clé de tri est `tuile.x + tuile.y` (murs et créatures), qui suit exactement l'ordre de la projection isométrique — `to_world().y = (tx + ty) * tile_height / 2` (voir `iso.hpp`) est croissant avec cette somme. Une créature devant un mur (somme plus grande) se dessine donc après lui, et inversement. Le sol reste à la profondeur par défaut (0), toujours le plus en arrière.
- **Caméra** et **tuile survolée** : réutilisées telles quelles depuis `--iso` (déplacement clavier/ZQSD, zoom molette, surbrillance).
- **Superposition de statistiques** (FPS, temps de frame, sprites, lots/draw calls) : voir la découverte ci-dessous, un vrai problème d'architecture rencontré en la construisant.
- **Capture d'image** : voir sa propre section ci-dessous.

**Découverte : pas de superposition d'UI en espace écran par-dessus une scène en espace monde.** `SpriteRenderer::set_view_projection()` s'applique à **toute la frame** (« the last call wins », un seul uniforme de projection poussé dans `render()`) : impossible de dessiner certains sprites avec la caméra et d'autres directement en pixels d'écran dans la même frame. Une première version du texte de statistiques, dessiné après avoir positionné la caméra, se retrouvait donc transformé par elle — invisible ou mal placé selon le zoom. **Correctif appliqué** : convertir l'ancre d'écran voulue via `camera.screen_to_world()` avant de dessiner le texte, avec une profondeur énorme (`1e6`) pour rester devant tout le reste. Ça fonctionne mais reste une **rustine** : le texte change de taille avec le zoom, ce qu'un vrai texte d'interface ne devrait pas faire. **Ce que ça implique pour plus tard** : une vraie couche d'UI (texte, barres de vie, menus) aura besoin d'un deuxième jeu de sprites en espace écran, donc soit un second appel à `prepare()`/`render()` par frame avec sa propre projection, soit un indicateur par sprite (« ignorer la caméra »). Pas fait ici : hors du périmètre réduit, et le nouveau statut du moteur 2D (UI seulement, à terme) veut dire que cette UI existera de toute façon un jour — mais dans le futur moteur 3D, pas celui-ci.

**Découverte : `RenderStats` n'est pas lisible pendant `render()`.** `Renderer::begin_frame()` remet `stats_` à zéro avant que `Game::render()` soit appelé (voir `renderer.hpp` : « valid from end_frame() until the next begin_frame() »). Une première version lisait `renderer.stats()` directement dans `render_demo()` et affichait systématiquement 0 sprite et 0 draw call. **Correctif** : la lecture se fait dans `update()`, qui tourne juste *avant* le `begin_frame()` de cette itération — dernier moment où `stats_` contient encore les valeurs de la frame précédente — et le résultat est mis en cache (`last_stats_`) pour que `render_demo()` s'en serve. C'est exactement le même schéma que l'affichage FPS de la fenêtre dans `Application::run()`, découvert après coup en le relisant.

**Capture d'image.** Nouveau : `Renderer::request_capture(path)` (un indicateur, consommé une fois) et `write_capture_png()` (`src/moteur/screenshot.cpp`, `stb_image_write` — déjà tiré par `atlas_packer`, rien à ajouter au projet). `end_frame()` fait, si une capture est demandée : une passe de copie du swapchain vers un tampon de transfert **avant** la soumission (tant que la texture du swapchain est valide), puis, après soumission, une attente GPU et une lecture du tampon pour écrire le PNG (conversion BGRA→RGB si le format du swapchain l'exige, alpha forcé à 255 — celui du swapchain n'a pas de sens une fois présenté). Toutes les scènes du bac à sable en profitent : `--capture chemin.png` avec `--freeze-after N` capture la première frame gelée, reproductible avec la même graine et les mêmes options. Coût : une attente GPU complète par capture (`SDL_WaitForGPUIdle`), acceptable pour un outil de test, jamais à faire à chaque frame.

**Vérifications faites**

- Build Debug propre, 114/114 tests unitaires toujours au vert (le changement ne touche aucune logique testée, seulement le rendu et le bac à sable).
- `--iso`, `--text`, `--atlas` et le test de charge (`--sprites`) repassés en fumée après le refactor (extraction de `update_fps_counter()`, ajout des membres partagés) : aucune régression.
- `--demo --seed 42 --freeze-after 120 --run-seconds 3 --no-input --capture demo.png` : capture non vide, texte de statistiques lisible et bien positionné (`168 FPS (5 ms)`, `5695 sprites, 169 lots/draw calls`), murs et créatures correctement mêlés au sol par la profondeur.
- **Déterminisme** : deux lancements avec `--seed 42 --freeze-after 30` produisent des PNG strictement identiques (même hachage SHA-256), avant même de comparer visuellement.
- Capture testée aussi sur `--text` (scène sans caméra ni créatures) : fonctionne sans changement, confirmant que la fonctionnalité est bien générique et pas spécifique à `--demo`.

**Ce qui n'est pas fait**

- **Le script de mesure dédié** : `--report` donne déjà les chiffres qu'il aurait affichés ; un script séparé n'a de sens que lorsqu'il y aura plusieurs scènes de référence à comparer automatiquement dans la durée.
- **La comparaison Windows/Mac** : nécessite le Mac, comme pour tout le reste du jalon.
- **Une vraie couche d'UI en espace écran** : voir la découverte ci-dessus ; nécessaire si l'UI 2D (qui restera après le passage en 3D) doit un jour se superposer à une scène de gameplay avec caméra.

### Validation

- [x] La scène tourne à la cadence de l'écran avec les statistiques de l'objectif de performance atteintes (voir la partie 1). *`--demo` avec 3 000 créatures + carte 100×100 : 168 FPS avec VSync activé sur la machine de développement (voir le compteur intégré), cohérent avec les mesures de charge de la partie 3 pour un nombre de sprites comparable.*
- [x] Deux lancements avec la même graine donnent la même capture. *Vérifié par hachage SHA-256 : deux exécutions de `--demo --seed 42 --freeze-after 30` produisent un PNG identique au bit près.*
- [ ] Les captures de Windows et de Mac sont comparées (identiques ou différences expliquées). *Référence Windows : `--demo --seed 42 --freeze-after 30 --no-input --capture demo.png` → `bd91e8b2c616` (1280×720, stable sur deux lancements ; avec les animations et la carte des parties 6 et 7). Les atlas sont identiques sur les deux OS, mais le Mac capture en 2560×1440 (Retina) : une comparaison au pixel près demande la même résolution en pixels des deux côtés.*

---

## 10. Débogage et performance

### Outils

| Outil | OS | Usage |
|---|---|---|
| Statistiques intégrées | Les deux | Sprites, lots, draw calls, octets envoyés, temps de frame |
| Mesures CPU (`SDL_GetPerformanceCounter`) | Les deux | Temps par phase : mise à jour, tri, construction, envoi, dessin |
| RenderDoc | Windows | Capture d'une frame : vérifier les lots, les textures liées, les buffers |
| Débogueur GPU Metal (Xcode) | Mac | Capture d'une frame Metal |
| Tracy (`tracy` dans vcpkg) | Les deux | Profilage CPU détaillé avec chronologie *(à évaluer)* |
| Couche de validation du GPU | Les deux | Erreurs d'utilisation de l'API |

### Questions à se poser

- **CPU ou GPU ?** Avant d'optimiser, savoir lequel limite. Si le temps CPU par frame est bien inférieur au budget mais que les FPS n'atteignent pas la cadence, le GPU est en cause (surdessin, textures trop grandes).
- **Que compter dans les statistiques ?** Un compteur inutile est du bruit ; ceux qui sont retenus doivent servir à trancher (lots, sprites, octets envoyés).
- **Comment nommer les ressources GPU** pour les retrouver dans les captures ? SDL_GPU offre `SDL_SetGPUBufferName`, `SDL_SetGPUTextureName` et des groupes ou étiquettes de débogage. Sous Direct3D 12, ces étiquettes demandent la bibliothèque `WinPixEventRuntime.dll`.
- **Que mesurer en Release ?** Le mode debug du GPU et le build Debug faussent tout : les mesures se font en Release, avec la couche de validation désactivée.
- **Quelle variabilité accepter ?** Une seule mesure ne suffit pas : moyenne et **pires cas** (les saccades viennent des pics, pas de la moyenne).

### Implémentation réalisée (partie 10)

Testé sur Windows uniquement. Détail des fichiers dans la [documentation des fichiers](FICHIERS_DU_PROJET.md).

**Ce qui a été fait**

- **Nommage des ressources GPU** : `NameProperty`, une petite classe RAII qui crée les propriétés SDL nécessaires pour nommer une ressource à sa création (buffer, texture, sampler, shader, pipeline), puis les détruit. `Renderer::create_buffer()`, `create_texture()`, `create_sampler()` et `load_shader()` acceptent désormais un nom optionnel. Le moteur nomme lui-même ce qu'il crée : `sprite.vertices`, `sprite.indices`, `sprite.sampler`, `sprite pipeline`, les shaders par leur nom de fichier, les pages d'atlas par leur nom de fichier PNG, les pages de police par `font glyphs #N`.
- **Vérification que la couche de validation est réellement active** (question restée ouverte depuis les parties précédentes). Voir plus bas.
- **Analyse CPU ou GPU ?**, à partir des mesures déjà faites en partie 3 (aucune nouvelle mesure nécessaire).
- **Revue des statistiques existantes** : décision de ne rien ajouter, voir plus bas.
- **Tracy** : évalué, non retenu pour l'instant. Voir la décision.

**La couche de validation est bien active : vérification directe**

Les parties précédentes notaient à chaque fois « aucun message de validation vu, mais pas vérifié qu'elle soit installée ». Pour trancher, j'ai provoqué **volontairement** deux erreurs d'usage de l'API, chacune corrigée immédiatement après :

1. **Un draw call avec beaucoup plus d'indices que n'en contient le buffer d'indices** (500 000 de plus) : aucun message n'est apparu. Cela ne veut pas dire que la validation est absente, mais que la couche de validation « standard » de Direct3D 12 vérifie surtout la **cohérence des appels d'API** (états, liaisons, tailles déclarées), pas les accès mémoire à l'intérieur d'un draw call : cela demande une validation **basée sur le GPU** (*GPU-Based Validation*), un mode séparé, plus lourd, que SDL_GPU n'active pas avec le simple drapeau `debug`.
2. **Un sampler de fragment non lié avant un draw call** : SDL a immédiatement arrêté le programme avec un message explicite :
   ```
   Assertion failure at SDL_GPU_CheckGraphicsBindings (...SDL_gpu.c:550), triggered 1 time:
     '!"Missing fragment sampler binding!"'
   ```
   C'est **la preuve directe que la validation est active** et qu'elle détecte une erreur réaliste. C'est SDL_GPU lui-même qui valide les liaisons (pas seulement la couche native de Direct3D 12), avec un message qui nomme précisément le problème.

**Piège rencontré pendant ce test** : une assertion SDL_GPU qui échoue ouvre une invite qui **bloque le programme**, exactement comme les assertions du CRT rencontrées dans les jalons précédents. Le même traitement (lancer avec un délai, tuer le processus s'il ne se termine pas) s'applique.

**Décisions prises**

- **CPU ou GPU ?** Analyse à partir des mesures « sans VSync » déjà faites en partie 3 : le temps de frame réel (1 / FPS) est comparé au temps CPU moyen mesuré par nos propres compteurs.

  | Sprites | FPS | Temps de frame réel | CPU moyen mesuré | Écart non expliqué |
  |---|---|---|---|---|
  | 1 001 | 4 108 | 0,244 ms | 0,164 ms | 0,080 ms (33 %) |
  | 10 001 | 2 096 | 0,477 ms | 0,428 ms | 0,049 ms (10 %) |
  | 40 001 | 780 | 1,282 ms | 1,259 ms | 0,023 ms (2 %) |
  | 160 001 | 165 | 6,061 ms | 5,977 ms | 0,084 ms (1 %) |

  **L'écart non expliqué reste à peu près constant (0,02 à 0,08 ms) quel que soit le nombre de sprites**, au lieu de croître avec la charge. Un goulot GPU qui s'aggraverait avec le nombre de sprites donnerait un écart croissant ; ce n'est pas le cas. **Conclusion : le moteur est limité par le CPU aux volumes testés, et le GPU n'est jamais le facteur limitant.** L'écart lui-même est probablement le coût fixe de la synchronisation avec l'affichage et de la soumission, non capturé par nos horloges CPU. Cela confirme, avec des chiffres, ce que les parties 3 et 4 avaient déjà observé sans le nommer explicitement.
- **Que compter dans les statistiques ?** Revue de l'existant : sprites, draw calls, octets envoyés (partie 3), temps CPU moyen/p99/maximum et détaillé par phase — mise à jour, enregistrement, soumission (parties 2 et 3). **Aucun compteur retiré ni ajouté** : chaque valeur a déjà servi à trancher une décision dans les parties précédentes (le nombre de draw calls a démontré l'intérêt des atlas, les octets envoyés ont expliqué le coût à 160 000 sprites). Un compteur qui n'aurait jamais servi à rien serait du bruit ; ce n'est le cas d'aucun des compteurs actuels.
- **Tracy : non intégré pour l'instant.** Nos statistiques (moyenne, p99, maximum, détail par phase) suffisent à toutes les décisions prises jusqu'ici. Tracy apporterait une chronologie détaillée **à l'intérieur** d'une phase (par exemple, quelle partie du tri ou de la construction des sommets coûte le plus), utile seulement si une phase devient un problème identifié sans qu'on sache pourquoi. Ce n'est pas encore le cas : la partie 3 a déjà expliqué où va le temps de `record` et `end_frame` sans profileur. À réévaluer si une optimisation fine devient nécessaire.
- **RenderDoc** : installé après coup (voir plus bas) ; une capture a confirmé le nombre de lots attendu.

**Capture RenderDoc de la scène de test**

Une fois RenderDoc 1.46 installé, capture de la scène `--sprites 3000 --still` (touche F12 pendant l'exécution) : le *Event Browser* de la capture ne montre **qu'un seul appel de dessin** pour toute la frame, `DrawIndexedInstanced(18006, 1)` — 18 006 indices, soit 3 001 sprites à 6 indices chacun (2 triangles par sprite). Comparé au compteur interne du moteur pour la même scène :

```
bac_a_sable --sprites 3000 --still --run-seconds 2 --report --no-input
perf: per frame  3001 sprites, 1 draw calls, 234.5 KiB uploaded
```

**1 draw call rapporté par le moteur, 1 draw call vu dans la capture, même nombre d'indices** : le lot unique attendu (un seul atlas, une seule texture, pas de rupture de lot) est confirmé indépendamment par un outil externe. RenderDoc a aussi signalé « No problems detected » au chargement de la capture, cohérent avec la couche de validation déjà confirmée active en partie 10.

**Ce qui n'est pas vérifié**

- **Le débogueur Metal (Xcode)** : nécessite le Mac.
- **Le temps GPU réel** : toujours pas mesuré directement (seule son absence de goulot est déduite par comparaison de temps, voir plus haut).
- **`WinPixEventRuntime.dll`** : les groupes de débogage nommés par frame (`SDL_PushGPUDebugGroup`) n'ont pas été ajoutés, car ils demandent cette bibliothèque sous Direct3D 12 (non fournie). Le nommage des ressources elles-mêmes n'a pas cette contrainte et fonctionne sans elle.

### Validation

- [x] Une capture RenderDoc montre le nombre de lots attendu. *Capture de la scène `--sprites 3000` : un seul `DrawIndexedInstanced`, 18 006 indices = 3 001 sprites, contre 1 draw call rapporté par le moteur pour la même scène — confirmation croisée.*
- [x] Le budget de temps est ventilé par phase, avec les chiffres notés dans la doc. *Fait dès la partie 3 ; complété ici par l'analyse CPU/GPU.*

---

## 11. Critères de fin de jalon

Le jalon est terminé quand **tout** ce qui suit est vrai :

- [ ] Le [jalon 1](JALON_1_FONDATIONS.md) est validé sur Mac, et les tests unitaires passent sur les deux OS.
- [ ] Le **batching** dessine l'objectif de sprites (par exemple 10 000) à la cadence de l'écran, avec un nombre de draw calls égal au nombre de lots.
- [ ] La **caméra** gère déplacement, zoom, conversions écran/monde et tuile sous la souris, sans scintillement ni couture.
- [ ] L'**outil d'empaquetage** produit des atlas déterministes, sans bavure, avec les métadonnées de rognage et de pivot.
- [x] Les **animations** sont pilotées par le pas fixe, avec vitesse variable et événements fiables.
- [x] La **carte de tuiles** de 100×100 s'affiche avec un tri en profondeur correct entre sols, murs et entités.
- [ ] Le **texte** UTF-8 s'affiche correctement (accents français), mesure et dessin concordent.
- [ ] La **scène de démonstration** tourne identiquement sur Windows et sur Mac.
- [ ] Aucun avertissement de compilation, aucun message de la couche de validation du GPU.
- [ ] Les décisions de la section [Décisions à consigner](#décisions-à-consigner) sont remplies.
- [ ] La documentation des nouveaux fichiers est ajoutée à [FICHIERS_DU_PROJET.md](FICHIERS_DU_PROJET.md).

---

## 12. Risques principaux

| Risque | Impact | Parade |
|---|---|---|
| Structure de frame mal adaptée au batching (copies pendant une passe de rendu) | Réécriture du renderer | La régler en partie 2, avant d'écrire le batch |
| Tri en profondeur incompatible avec le regroupement par texture | Trop de lots, performances insuffisantes | Atlas dès le début ; mesurer le nombre de lots sur la scène réelle |
| Tri isométrique incorrect pour les objets multi-cases | Bugs visuels difficiles à corriger tard | Décider tôt du point de tri et de la découpe des objets |
| Volume de textures d'un jeu à sprites 8 directions | Mémoire saturée, chargements longs | Rognage, symétrie, atlas par zone ; décider du style visuel avant de produire du contenu |
| Décisions de couleur (sRGB, alpha pré-multiplié) tardives | Assets à reconvertir | Trancher avant d'empaqueter les premiers atlas |
| Divergences de rendu entre Direct3D 12 et Metal | Bugs sur un seul OS | Capture d'image et comparaison automatisée entre OS |
| Coordonnées de souris incorrectes sur écran Retina | Clics décalés, surtout sur Mac | Convertir explicitement points et pixels ; tester sur Mac dès la caméra |
| Empaquetage non déterministe | Diffs parasites, builds différents | Trier les entrées, résultat testé par un test unitaire |
| Dérive du périmètre (particules, éclairage, effets) | Jalon sans fin | Ces sujets appartiennent au jalon 4 : les noter, ne pas les faire |
| Choix de police avec licence incompatible | Retrait forcé plus tard | Vérifier la licence à l'ajout |
| Jalon 1 non validé sur Mac | Dette accumulée, tout à retester | Le faire en premier (partie 2) |
| Build incrémental incohérent (page de codes différente entre la configuration et le build) | Objets périmés, plantages qu'un build propre fait disparaître | Configurer et construire dans le même environnement ; en cas de doute, build propre (voir l'incident de la partie 3 et le README) |

---

## Décisions à consigner

À remplir au fil du jalon. Chaque ligne correspond à une question posée plus haut.

| Sujet | Décision | Raison |
|---|---|---|
| Objectif de performance (sprites visibles, budget par frame) | **Provisoire** : le rendu naïf faisait déjà 20 000 sprites à 164 Hz ; le batching en fait 80 000 sur Windows. Objectif à fixer après mesure sur Mac | Voir les chiffres de la partie 2 et de la partie 3 |
| Style visuel (dessiné, pré-rendu 3D, squelettique) | **Jeu en 3D** (modèles 3D), à traiter dans un jalon 3D ultérieur | Décidé par l'utilisateur. Le rendu 2D sert alors surtout à l'interface, aux icônes, aux effets et aux tests. Les volumes de sprites pré-rendus de la partie 5 ne s'appliquent plus aux personnages |
| Framework de tests (doctest ou Catch2) | doctest | Léger, un seul en-tête ; le choix est peu engageant |
| Forme de la file de dessin et de la nouvelle structure de frame | Le jeu enregistre avec `renderer.sprites().draw(...)`, le moteur exécute en `end_frame()` (copies, puis render pass, puis soumission) | Les copies sont interdites dans un render pass ; le moteur est libre de trier et regrouper |
| Ressources GPU et durée de vie | Classes RAII non copiables ; le `Renderer` doit survivre aux ressources | Supprime la libération manuelle ; règle documentée, non vérifiée à l'exécution |
| Méthode de mesure | Temps CPU hors attente de l'écran, en Release, 60 premières frames ignorées, moyenne + p99 + maximum | Les pics font saccader, pas la moyenne ; le mode debug fausse les chiffres |
| Approche du buffer de sommets (CPU, instanciation, buffer de stockage) | Sommets construits côté CPU, 20 octets par sommet | Le plus simple à valider sur Mac ; l'envoi (80 octets par sprite) n'est pas le goulot mesuré |
| Taille des indices et capacité d'un lot | Indices 16 bits statiques, 16 384 sprites par draw call ; buffer de sommets de 16 384 sprites, doublé à la demande | Décalage de sommet de base pour les lots suivants ; agrandissement rare |
| Clé de tri et compromis profondeur / texture | Profondeur seule, stable, tri par base (radix), déclenché seulement si l'ordre d'enregistrement n'est pas déjà croissant | Un rendu correct avec la transparence passe avant le regroupement par texture ; les atlas réduiront les lots |
| Modes de mélange pris en charge | Alpha classique uniquement | Pas d'effet additif encore ; à ajouter avec les effets |
| Alpha pré-multiplié : oui ou non | **Oui**, appliqué par le moteur à la création de la texture ; les PNG et les atlas restent en alpha normal | Filtrage correct sans halo, et changement bien moins coûteux avant d'avoir du contenu (tranché en partie 5) |
| Résolution : native + zoom ou virtuelle | Native + zoom | La résolution virtuelle demande une cible hors écran ; à reprendre avec l'interface |
| Zoom continu ou par paliers entiers | La caméra accepte tout facteur ; le bac à sable pilote des paliers entiers de 1 à 8 | Pas de scintillement du pixel art |
| Alignement de la caméra sur les pixels | Activé par défaut, translation écran arrondie ; dessin et picking utilisent la même | Pas de couture ni de flou ; désactivable |
| Unité du monde (tuiles ou pixels) et gestion de la hauteur | Monde en pixels ; grille isométrique = projection à part ; hauteur = paramètre en pixels | L'espace de vérité de la logique (grille ou monde) reste à confirmer avec le gameplay |
| Taille et rapport des tuiles | 64×32 (2:1) dans le bac à sable, réglable | Rapport le plus courant ; à fixer avec l'art |
| Caméra fixe ou avec rotation | Sans rotation | Simplifie l'art et le tri |
| Interface : seconde caméra | **Repoussé à la partie 8** | Demandera des lots par vue dans le batcher |
| Atlas : hors ligne, à l'exécution, ou les deux | Hors ligne d'abord ; l'exécution est repoussée à la partie 8 (polices) | Le contenu est connu à l'avance ; l'outil est notre code, donc identique sur les deux OS |
| Taille des pages d'atlas | 2 048 au maximum ; chaque page est la plus petite (puissances de deux) qui convienne | Petits ensembles compacts ; le gaspillage des puissances de deux reste, jusqu'à ~50 % sur peu d'images |
| Marge et extrusion autour des sprites | 1 pixel, rempli par copie des pixels de bord ; réglable | Évite les bavures avec un filtrage linéaire ; sans effet visible avec `NEAREST` |
| Rognage des zones transparentes | Activé ; décalage et pivot conservés dans l'image d'origine | Gagne de la place ; le pivot reste exprimé dans l'original |
| Format de description des atlas et bibliothèque JSON | JSON versionné (`version` 1), écrit et lu avec `nlohmann-json` ; clés triées | Lisible, déterministe, extensible |
| Organisation art source / ressources produites, Git LFS | Sources dans `art/`, produits dans `assets/` à côté de l'exécutable ; **Git LFS pas encore décidé** | Les atlas se régénèrent au build ; le poids des sources se jugera avec le vrai contenu |
| Identifiant d'un sprite (chaîne, haché, énumération) | Chaîne : chemin relatif sans extension, séparateur `/` | Lisible dans les fichiers de données ; les sous-dossiers rangent |
| Pivot | Bas au milieu de l'image d'origine par défaut ; `pivots.json` par image | Les pieds d'un personnage ; surcharge simple |
| Format des pages | PNG ; formats compressés GPU repoussés | Un seul chargeur (stb) ; BC7 / ASTC diffèrent selon l'OS |
| Horloge des animations (pas fixe ou temps réel), unité de durée | Pas fixe ; durées en ticks entiers, temps en millièmes de tick, vitesse en millièmes | Aucune dérive de flottants : identique sur les deux OS ; indispensable si un événement inflige des dégâts |
| Nombre de directions et symétrie par retournement | Pas de directions dans le moteur 2D ; symétrie par `flip_x` autour du pivot | Personnages en 3D ; le 2D sert aux effets et à l'interface |
| Animation par images ou squelettique | Par images pour le 2D ; le squelettique viendra avec la 3D | Jeu en 3D (décision de style visuel) |
| Gestion des événements d'animation | Liste remplie par `advance()` et lue par le jeu ; intervalle (avant, après] | Simple à déboguer, déterministe, chaque événement exactement une fois |
| Format des cartes (maison ou Tiled) | Structures en mémoire (`TileMap`), pas de format de fichier pour l'instant | Zones générées ; Tiled reste possible plus tard pour les salles dessinées à la main |
| Stratégie de tri en profondeur des objets multi-cases | Pas d'objets multi-cases pour l'instant ; clé `x + y` pour les objets d'une case | À reprendre si le 2D en a besoin ; en 3D, le tampon de profondeur s'en charge |
| Nombre de calques et taille des identifiants de tuiles | Nombre de calques libre par carte ; identifiants 16 bits, 0 = vide | 65 535 types suffisent ; propriétés dans le `Tileset`, pas dans la carte |
| Blocs statiques de tuiles : oui ou non, taille | Non | Mesuré : 0,41 ms pour construire une frame de 5 700 sprites |
| Option de texte (`stb_truetype`, SDL3_ttf, bitmap, SDF) | `stb_truetype`, rasterisé au chargement de la police | Aucune dépendance de plus, réutilise le batch et l'outil d'atlas |
| Police retenue et sa licence | Inter, SIL Open Font License 1.1 | Bonne lisibilité, licence libre, couverture Latin-1 complète |
| Jeu de caractères couvert | Latin de base + Latin-1 + œ/Œ, tirets, guillemets courbes (~236 glyphes) | Couvre le français ; écritures non latines hors périmètre |
| Niveau de texte enrichi prévu | Une couleur par appel à `draw()` ; `TextOptions` conçu pour accueillir plus, non implémenté | Recommandation de la doc : concevoir l'API sans tout implémenter |
| Capture d'image pour les tests automatiques | Fait en partie 9 : `Renderer::request_capture()` + `write_capture_png()` (readback GPU→CPU, `stb_image_write`), disponible sur toute scène du bac à sable via `--capture` | Déterminisme vérifié par hachage SHA-256 entre deux lancements à graine identique |
