# Jalon 5 - Animation 3D

Retour à la [roadmap générale](ROADMAP.md). Jalon précédent : [Jalon 4 - Systèmes de base](JALON_4_SYSTEMES_DE_BASE.md). Description des fichiers existants : [Fichiers du projet](FICHIERS_DU_PROJET.md).

## Objectif

Faire **bouger les personnages** : charger des **squelettes** et des **clips d'animation** depuis des fichiers glTF, déformer les maillages sur le **GPU** (skinning), **mélanger** et **enchaîner** les animations (marcher, courir, frapper), déclencher des **événements** au bon moment de chaque clip (le pied touche le sol, le coup porte), et **attacher** des objets à un os (une épée dans la main).

À la fin du jalon, la **tranche jouable** du jalon 4 doit avoir un **héros animé** : il est immobile au repos, marche ou court selon sa vitesse sans que ses pieds glissent, frappe avec une animation d'attaque dont le **coup porte à l'événement « impact »** (et non au clic), fait entendre ses **pas à l'événement « pas »** (et non tous les 0,85 case), et tient une **épée attachée à sa main**. Les créatures sont animées elles aussi (repos, marche, touchée, mort). Une scène de test « Animation » montre un personnage, ses clips, son squelette et les mélanges, et un test de charge mesure **combien de personnages animés** tiennent dans le budget.

**Pourquoi maintenant ?** Le jalon 4 a posé tout ce dont l'animation a besoin : le gestionnaire d'assets (squelettes et clips sont des assets), l'ECS (l'animation est un composant, sa mise à jour un système), les entrées (les actions déclenchent les attaques), l'audio (les événements jouent les pas) et les outils ImGui (inspecteur). Le jalon 6 (monde et déplacement) fera marcher les personnages le long de chemins : autant qu'ils marchent vraiment.

## Prérequis

- Le [jalon 4](JALON_4_SYSTEMES_DE_BASE.md) est terminé (sous Windows ; la manette et les vérifications Mac restent listées dans sa validation et dans [TEST_MAC.md](TEST_MAC.md)).
- Lire la partie **skins et animations** de la spécification glTF 2.0 (sections *Skins*, *Animations*, et l'annexe sur l'interpolation `CUBICSPLINE`), et le tutoriel glTF de Khronos sur le skinning (*glTF Tutorial : Simple Skin*).
- Selon la décision de la partie 2 : la documentation d'**ozz-animation** (exemples *playback*, *blend*, *partial blend*, *attach*, *skinning*).
- Blender (déjà installé, 5.2.2) pour regarder les modèles de test et comparer une pose.

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
2. [Bibliothèque et modèles de test](#2-bibliothèque-et-modèles-de-test)
3. [Squelettes et clips glTF](#3-squelettes-et-clips-gltf)
4. [Lecture des clips et temps de la simulation](#4-lecture-des-clips-et-temps-de-la-simulation)
5. [Skinning sur le GPU](#5-skinning-sur-le-gpu)
6. [Mélanges et transitions](#6-mélanges-et-transitions)
7. [Événements d'animation](#7-événements-danimation)
8. [Attaches aux os](#8-attaches-aux-os)
9. [Outils de debug et scène de test](#9-outils-de-debug-et-scène-de-test)
10. [Performances](#10-performances)
11. [Personnages animés dans la tranche jouable](#11-personnages-animés-dans-la-tranche-jouable)
12. [Critères de fin de jalon](#12-critères-de-fin-de-jalon)
13. [Risques principaux](#13-risques-principaux)
14. [Décisions à consigner](#décisions-à-consigner)

---

## 1. Vue d'ensemble et ordre de travail

### Où on part

Après le jalon 4 :

- **Modèles** : `load_gltf` / `parse_gltf` (cgltf) lisent les maillages, les matériaux et la hiérarchie des nœuds, mais **aplatissent** cette hiérarchie : chaque partie reçoit la matrice monde de son nœud (`ModelPart::transform`). Les attributs `JOINTS_0` / `WEIGHTS_0`, les skins et les animations sont **ignorés**.
- **Sommets** : `Vertex3D` fait 48 octets (position, normale, uv, tangente) ; le commentaire annonce déjà les attributs de skinning pour ce jalon. `mesh.vert.hlsl` lit les locations 0 à 3 par sommet et 4 à 12 par instance (`MeshInstance`, 144 octets) ; `shadow.vert.hlsl` et `point_shadow.vert.hlsl` ne lisent que la position et les lignes de la matrice monde.
- **Rendu** : `MeshBatcher` regroupe les dessins par maillage, textures et faces (un appel instancié par lot), avec culling par boîte (`Aabb`) ; les ombres du soleil et des lumières ponctuelles repassent les mêmes lots.
- **Animation** : l'`AnimationPlayer` du jalon 2 joue des **clips de sprites** : temps en **millièmes de tick** (entiers), vitesse en millièmes, modes `Once` / `Loop` / `PingPong`, **événements exactement une fois** même quand un grand pas saute des images ou des boucles. `AnimationLibrary` lit un JSON de clips. Tout cela est testé, et c'est le modèle à suivre pour la 3D.
- **Monde** : `moteur::World` (un registre EnTT par scène), `Transform` en quaternion interpolé par slerp, `PreviousTransform`, `Parent` (« l'attache à un os (jalon 5) s'y ajoutera »), `collect()` qui remplit un `WorldSink` pour le renderer.
- **Tranche jouable** : le héros et les créatures sont des assemblages de primitives (corps, tête, épée) ; les pas sont joués toutes les 0,85 case marchée, le coup porte au clic.

Les principes restent ceux de la roadmap : le moteur ne connaît rien de l'ARPG (il n'y a pas de « marcher » ni d'« attaquer » dans le moteur, seulement des clips, des poids et des événements nommés), la logique tourne à **pas fixe** et doit rester **déterministe**.

### Dépendances entre les parties

```
2. Bibliothèque et modèles de test
               |
3. Squelettes et clips glTF
               |
4. Lecture et temps de la simulation
               |
      +--------+---------+-------------+
      |                  |             |
5. Skinning GPU   6. Mélanges   7. Événements
      |                  |             |
      +--------+---------+-------------+
               |
      8. Attaches aux os
               |
      9. Outils de debug et scène de test   (commence dès la partie 3 : voir le squelette)
               |
     10. Performances
               |
     11. Tranche jouable
```

- La **bibliothèque** (ou non) se choisit d'abord : elle décide du format des squelettes et des clips en mémoire.
- Le **chargement** vient ensuite : sans squelette ni clip, rien à jouer.
- La **lecture** (le temps d'un clip, sa vitesse, ses boucles) est la base commune : le skinning dessine la pose qu'elle donne, les mélanges combinent plusieurs lectures, les événements sont lus sur son temps.
- Le **skinning**, les **mélanges** et les **événements** sont indépendants et peuvent s'intercaler. Avant le skinning, on peut déjà voir un squelette animé en lignes de debug (partie 9).
- Les **attaches** ont besoin de poses correctes (donc du skinning, pour vérifier que l'épée est bien dans la main dessinée).

### Ce qui peut se faire en pause du moteur

Logique pure, testable en ligne de commande :

- **Lecture d'un squelette et de clips** depuis un glTF, sans GPU : nombre d'os, parents, matrices de liaison inverses, durées.
- **Échantillonnage** d'un clip à un temps donné, comparé à des valeurs connues (un modèle de test minimal écrit à la main, comme `make_reference_model.py` au jalon 3).
- **Temps de lecture** : boucles, vitesse, événements exactement une fois, conversion des secondes du fichier en ticks.
- **Mélanges** : poids qui somment à 1, fondus, masques par os, chemin le plus court des quaternions.
- **Skinning sur le CPU** pour les tests : même calcul que le shader, pour vérifier une pose sans GPU.
- **Recherche des modèles animés de test** et vérification de leurs licences.

### Estimation indicative

Pour un dev solo à temps partiel. À prendre comme un ordre de grandeur, pas comme un engagement.

| Partie | Estimation |
|---|---|
| 2. Bibliothèque et modèles de test | 1 semaine |
| 3. Squelettes et clips glTF | 1 à 2 semaines |
| 4. Lecture et temps | 1 semaine |
| 5. Skinning sur le GPU | 2 à 3 semaines |
| 6. Mélanges et transitions | 2 à 3 semaines |
| 7. Événements | 1 semaine |
| 8. Attaches aux os | 1 semaine |
| 9. Outils de debug et scène de test | 1 à 2 semaines |
| 10. Performances | 1 à 2 semaines |
| 11. Tranche jouable | 1 à 2 semaines |
| **Total** | **environ 3 à 4 mois** |

### Liens avec les autres jalons

- **Rendu 3D** (jalon 3) : le **TAA** a été repoussé « après l'animation », faute de vecteurs de mouvement. Ce jalon ne fait pas le TAA, mais garder la **palette d'os de la frame précédente** (question de la partie 5) le rendrait possible.
- **Monde et déplacement** (jalon 6) : le pathfinding donnera une vitesse et une direction ; l'animation doit **suivre** ce mouvement (vitesse de lecture accordée), pas le décider. Les **particules** pourront partir d'un os (étincelles sur l'épée) grâce aux attaches de la partie 8.
- **Outils et données** (jalon 7) : les **événements** et les **transitions** décrits en JSON profiteront du rechargement à chaud des données. La **sauvegarde** écrira l'état d'animation (clip, temps en ticks), pas la pose : c'est une raison de garder un état **entier et simple**.
- **Consolidation** (jalon 8) : le packaging pourra convertir les clips en un format prêt à lire (compressé), comme les KTX2.

### Questions générales

- **Animation sur le CPU ou le GPU ?** Deux choses différentes : l'**échantillonnage** des clips et le **mélange** donnent une pose (quelques dizaines d'os par personnage) : c'est peu de calcul, sur le CPU. Le **skinning** déforme des milliers de sommets par cette pose : sur le **GPU**. C'est ce que dit la roadmap, et ce que font presque tous les moteurs. Le calcul des poses sur le GPU (compute) est pour plus tard, si le test de charge le demande.
- **Qui décide quelle animation joue ?** Le **jeu**. Le moteur fournit un lecteur, des mélanges et des transitions ; la tranche (le jeu) dit « le héros court à 3,2 m/s » ou « lance l'attaque ». Un graphe d'animation décrit en données (machine à états en JSON) est une question de la partie 6.
- **Le jeu lit-il la pose ?** Le moins possible : la logique déterministe ne dépend que du **temps** des clips (entier) et de leurs **événements**, jamais de la position calculée d'un os (voir la partie 4).

---

## 2. Bibliothèque et modèles de test

### But

Décider si l'on écrit l'échantillonnage et les mélanges soi-même ou si l'on s'appuie sur une bibliothèque, et trouver des **personnages animés libres de droits** pour tout le jalon.

### Tâches

- [ ] Comparer les options (voir les questions), sur un personnage de test : compilation sous Windows et Mac, taille, API.
- [ ] Ajouter la bibliothèque choisie à `vcpkg.json` et à `credits.json`.
- [ ] Trouver deux ou trois **personnages animés** en glTF sous licence **CC0** (ou compatible), avec au moins : repos, marche, course, attaque, touché, mort. Un avec une **arme** séparée ou un os de main nommé.
- [ ] Les télécharger par un script (`tools/models/fetch_test_models.py`, ou un script à part pour les personnages), avec SHA-256 vérifiés, hors de Git, crédités ; convertir leurs textures en KTX2 avec `convert_gltf_textures.py`.
- [ ] Un **modèle de référence minimal** écrit par un script (deux ou trois os, un clip de quelques clés aux valeurs connues), pour les tests unitaires : comme `make_reference_model.py` au jalon 3.

### Questions à se poser

**Bibliothèque**

- **Écrire nous-mêmes ou prendre une bibliothèque ?** Trois options :
  - **Tout à la main** sur cgltf : lire les clés, interpoler (linéaire, slerp, `STEP`, `CUBICSPLINE`), mélanger. C'est quelques centaines de lignes pour la lecture simple, mais la compression des clips, les mélanges partiels, l'IK et les performances (SIMD, clés rangées pour un accès en avant) sont un vrai travail.
  - **ozz-animation** (MIT, dans vcpkg *(à vérifier : nom du port et version)*) : squelettes, échantillonnage avec cache, mélanges avec masques par os, mélange additif, IK à deux os et « regarder vers », passage local → modèle, attaches, compression des clips, en SIMD. Les données viennent d'une partie « offline » (`RawSkeleton`, `RawAnimation`, puis `SkeletonBuilder` / `AnimationBuilder`) que l'on peut remplir **au chargement**, à partir de ce que cgltf a lu. ozz fournit aussi un convertisseur `gltf2ozz` *(à vérifier : fourni par le port vcpkg ?)*.
  - **Un moteur d'animation plus complet** (graphes, machine à états) : il n'y en a guère d'indépendant, libre et léger en C++ ; et le graphe est justement ce que le jeu doit garder en main.
  
  Recommandation : **ozz-animation**, conformément à la préférence pour l'open source : on garde **cgltf** pour lire les fichiers (un seul lecteur de glTF dans le moteur, le glTF reste le seul format source, le rechargement à chaud marche comme pour les modèles), et on remplit les structures « raw » d'ozz au chargement. Le **temps**, les **événements** et le **choix des clips** restent à nous (partie 4) : ozz ne connaît que des secondes en `float`, pas nos ticks.
- **ozz sur Mac (ARM)** : ozz a un chemin SSE pour x86 ; sur ARM, il retombe sur une implémentation de référence scalaire *(à vérifier : support NEON)*. À mesurer sur le Mac M3 (partie 10) avant de s'engager pour de bon.

**Modèles de test**

- **Où trouver des personnages animés CC0 en glTF ?** Pistes *(toutes à vérifier : licence exacte, présence des clips voulus, format)* :
  - **Quaternius** : *Ultimate Animated Character Pack*, *Universal Animation Library* (CC0, glTF / FBX), beaucoup de clips de locomotion et de combat.
  - **KayKit** (Kay Lousberg) : *Adventurers*, *Skeletons* (CC0, `.glb`, armes séparées, animations d'un ARPG : repos, course, attaque, mort). Style proche d'un ARPG vu de haut.
  - **Kenney** : *Animated Characters* (CC0, surtout FBX).
  - **Khronos glTF-Sample-Assets** : `SimpleSkin`, `RiggedSimple`, `RiggedFigure`, `CesiumMan`, `Fox`, `BrainStem` : faits pour **tester un lecteur** (cas limites : plusieurs skins, interpolations), avec des licences variées (CC-BY pour certains : à créditer).
  - **Mixamo** : exclu (conditions d'Adobe, pas une licence libre ; pas redistribuable).
  
  Recommandation : **KayKit** ou **Quaternius** pour le héros et les créatures, et **quelques modèles Khronos** pour la conformité du lecteur.

### Pièges connus

- Des packs « gratuits » qui ne sont pas CC0 (utilisation permise mais pas la redistribution) : le dépôt ne les contient pas, mais la fenêtre « À propos » doit dire vrai. Lire la licence du pack, pas la page qui le présente.
- Des animations livrées dans des **fichiers séparés** du personnage (un `.glb` d'animations pour plusieurs personnages) : il faut que le squelette soit le même (mêmes noms d'os) ; c'est une question de la partie 3.
- Des modèles exportés depuis FBX avec une **échelle 0,01** ou un axe Z en haut dans un nœud d'armature : vérifier dans Blender que le personnage fait bien sa taille (environ 1,8 m, soit 1,8 case).

### Validation

- [ ] La bibliothèque choisie compile et lit un personnage de test sous Windows (Mac dans TEST_MAC.md).
- [ ] Les personnages de test sont téléchargés par script, crédités, et leurs licences notées dans les décisions.

---

## 3. Squelettes et clips glTF

### But

Lire depuis un glTF le **squelette** (os, parents, pose de liaison), les **poids** de chaque sommet et les **clips**, et en faire des assets partagés par le cache.

### Tâches

- [ ] `parse_gltf` lit `JOINTS_0` et `WEIGHTS_0` (formats `UNSIGNED_BYTE`, `UNSIGNED_SHORT`, et `FLOAT` / normalisés pour les poids) ; les poids sont **renormalisés** (somme 1) ; un sommet sans poids est rattaché à l'os de son nœud.
- [ ] `ModelData` garde un **squelette** : pour chaque os, son nom, son parent, sa transformation locale de repos (translation, rotation, échelle) et sa **matrice de liaison inverse** (`inverseBindMatrices`). Les os sont rangés **parents avant enfants** (ordre qu'attendent ozz et le calcul local → modèle).
- [ ] Les parties **skinnées** ne sont plus aplaties comme les autres : leur matrice de nœud est ignorée (règle de glTF : la pose vient des os), et elles gardent l'indice de leur skin.
- [ ] Lire les **animations** : chaque canal (translation, rotation, échelle d'un nœud), ses temps et ses valeurs, ses interpolations (`LINEAR`, `STEP`, `CUBICSPLINE`). Ignorer avec un message les canaux de **morph targets** (`weights`) et ceux qui visent un nœud hors du squelette.
- [ ] En faire des assets : `Skeleton` et `AnimationClip3D` (noms provisoires) dans le cache, à côté des `Model`. Clé d'un clip : `models/hero.glb#Walk` (le fichier et le nom du clip).
- [ ] Permettre des **clips dans un autre fichier** que le personnage, à condition que le squelette corresponde (voir les questions).
- [ ] Rechargement à chaud : un clip modifié est rechargé en place ; un squelette dont la structure change est refusé avec un message (comme `Model::replace_in_place`).
- [ ] Afficher dans DEBUG > Assets les squelettes et les clips (nombre, mémoire, durée).

### Questions à se poser

- **Squelette partagé ou propre à chaque modèle ?** Plusieurs personnages d'un pack partagent souvent le même squelette et les mêmes clips. Recommandation : le squelette est **un asset à part**, identifié par le fichier qui le définit ; deux fichiers ont le « même » squelette s'ils ont les mêmes os, dans le même ordre, avec les mêmes parents (vérifié au chargement, avec un message qui nomme le premier os différent).
- **Retargeting** (jouer les clips d'un squelette sur un autre, aux proportions différentes) : hors de ce jalon. On se limite aux squelettes identiques.
- **Combien d'os au plus ?** Un personnage d'ARPG en a 20 à 70 (doigts compris). Recommandation : **255** au plus par squelette (indices sur 8 bits dans les sommets, voir la partie 5), refus au chargement au-delà.
- **Plus de quatre influences par sommet** (`JOINTS_1` / `WEIGHTS_1`) : recommandation : garder les **quatre plus fortes**, renormaliser, et le noter dans le log (une fois par modèle).
- **Rééchantillonner les clips au chargement ?** Les clés peuvent avoir des temps irréguliers ; ozz les convertit dans son propre format de toute façon. `CUBICSPLINE` : ozz ne le lit pas directement *(à vérifier)* ; recommandation : échantillonner les clips cubiques à 60 Hz en clés linéaires au chargement.

### Pièges connus

- L'ordre des os du **skin** (`skin.joints`) n'est pas l'ordre des nœuds : `JOINTS_0` indexe `skin.joints`, pas les nœuds. Garder une table de l'un à l'autre.
- Le **nœud racine** de l'armature (souvent au-dessus du premier os, avec une rotation de −90° ou une échelle 0,01 venues de l'export) : s'il n'est pas un os, sa transformation doit quand même s'appliquer au personnage entier.
- Le nœud qui porte le maillage skinné a sa propre transformation, que glTF dit **d'ignorer** : l'appliquer décale le personnage.
- Des quaternions de clés **non normalisés** ou qui changent de signe d'une clé à l'autre (q et −q sont la même rotation) : l'interpolation passe par le « long chemin ».
- Des clips qui ne commencent pas à 0 (Blender exporte le début de la plage) : la durée est `fin - début`, et le temps 0 du clip est le début.
- Des os qui ne sont animés par aucun canal : ils gardent leur pose de repos, pas l'identité.

### Validation

- [ ] Le modèle de référence minimal donne, au chargement, le bon nombre d'os, les bons parents, les bonnes matrices et les bonnes durées (tests unitaires).
- [ ] Les personnages de test et les modèles Khronos choisis se chargent sans erreur ; un fichier au squelette incompatible est refusé avec un message qui nomme l'os.
- [ ] Les modèles statiques du jalon 3 se chargent comme avant (captures de référence inchangées).

---

## 4. Lecture des clips et temps de la simulation

### But

Jouer un clip 3D avec les **mêmes garanties** que l'`AnimationPlayer` du jalon 2 : temps entier, vitesse exacte, événements exactement une fois, même résultat sur toutes les machines ; et en tirer, à chaque image, une **pose interpolée** fluide.

### Tâches

- [ ] Séparer dans l'`AnimationPlayer` actuel ce qui est **commun** (temps en millièmes de tick, vitesse en millièmes, modes de lecture, événements franchis) de ce qui est propre aux sprites (images et régions), pour que le lecteur 3D réutilise le premier. Les tests du jalon 2 doivent passer sans changement.
- [ ] Un clip 3D a une **durée en ticks** (la durée du fichier en secondes × 60, arrondie) et des événements à des **ticks** (partie 7).
- [ ] Un composant d'animation (ECS) : le clip (ou les clips, voir la partie 6), le temps, la vitesse ; un **système** qui avance tous les lecteurs d'un tick dans `update()`.
- [ ] L'**échantillonnage** se fait au dessin : au temps du tick précédent et du tick présent, interpolé par l'`alpha` du pas fixe (comme `PreviousTransform`), ou directement au temps interpolé (voir les questions).
- [ ] Calcul **local → modèle** (matrices des os dans l'espace du personnage), puis **palette** de skinning (matrice modèle de l'os × matrice de liaison inverse).
- [ ] Personnages hors de la vue : ne pas échantillonner (le temps avance quand même, au tick).

### Questions à se poser

- **Échantillonner au temps interpolé, ou interpoler deux poses ?** Échantillonner une fois au temps `précédent + alpha × (présent - précédent)` coûte un échantillonnage par image ; interpoler deux poses en coûte deux. Recommandation : **au temps interpolé**. Attention aux boucles (le temps revient à 0) et aux changements de clip entre deux ticks (dans ce cas, prendre la pose du tick présent).
- **Qu'est-ce qui est déterministe ?** Le **temps** (entier), les **événements**, le clip joué, les poids des mélanges (calculés en entiers ou à partir d'entiers au tick) : tout ce que la logique lit. La **pose** (flottants, SIMD) ne sert qu'au dessin. Recommandation : **la logique ne lit jamais une pose**. Si un jour le jeu a besoin de la position d'un os (lancer un projectile depuis la main), il l'échantillonne au tick, au temps entier, et on accepte qu'elle puisse différer d'un ulp entre Windows et Mac (les captures de référence sont déjà propres à chaque OS pour les images ; les rejeux, eux, ne doivent pas en dépendre).
- **La même horloge que les sprites ?** Oui, c'est le sens de la ligne de la roadmap : un seul mécanisme de temps et d'événements pour les sprites 2D (effets, interface) et les squelettes 3D.

### Pièges connus

- Convertir les secondes du fichier en ticks **à chaque échantillonnage** avec des `float` : dérive au bout de longues boucles. Garder le temps en entiers et ne convertir en secondes qu'au dernier moment, pour ozz.
- Le **dernier instant d'une boucle** : échantillonner exactement à la durée donne la dernière clé, puis la première au tick suivant ; si les deux ne sont pas identiques dans le fichier, la boucle « saute ». Le noter, ne pas le corriger dans le moteur (c'est un défaut du clip).
- Un clip `Once` terminé qui reprend à 0 à cause d'une interpolation entre « fin » et « début » : le temps d'un clip terminé ne boucle pas.

### Validation

- [ ] Un clip avancé tick par tick ou par grands pas donne le même temps et les mêmes événements (tests unitaires, comme au jalon 2).
- [ ] À 60, 144 et 165 Hz d'écran, l'animation est fluide (pas de saccade au rythme des ticks) ; `--fixed-dt` ou une fréquence de logique basse (option de debug) le rend visible si ce n'est pas le cas.

---

## 5. Skinning sur le GPU

### But

Déformer les maillages skinnés dans le **vertex shader**, pour la passe de la scène **et** pour les ombres, en gardant l'instanciation et le culling du jalon 3.

### Tâches

- [ ] Un format de sommet pour les maillages skinnés : les 48 octets de `Vertex3D`, plus **quatre indices d'os** et **quatre poids** (voir les questions). Les maillages statiques ne changent pas.
- [ ] Un **tampon de palettes** par image : toutes les matrices d'os de tous les personnages visibles, les unes après les autres, dans un **storage buffer** lu par le vertex shader ; chaque instance porte le **début de sa palette**.
- [ ] Variantes skinnées de `mesh.vert.hlsl`, `shadow.vert.hlsl` et `point_shadow.vert.hlsl` (le même code de skinning, dans un fichier inclus), et les pipelines qui vont avec (toutes les variantes d'échantillonnage MSAA comprises).
- [ ] `MeshBatcher` : les maillages skinnés forment leurs propres lots (même maillage, mêmes textures : un appel instancié, chaque instance avec sa palette).
- [ ] **Boîtes** pour le culling : une boîte qui contient toutes les poses possibles (voir les questions), mise à la place du personnage.
- [ ] Ombres du soleil et des lumières ponctuelles correctes pour un personnage animé (le cache des ombres ponctuelles, `PointShadowSlots`, doit savoir qu'un personnage animé **change** à chaque image).
- [ ] Un skinning **sur le CPU** dans les tests (même formule), pour vérifier une pose sans GPU.
- [ ] Vues de debug (`MeshView`) : les poids d'un os choisi en couleur.

### Questions à se poser

- **Format des indices et des poids ?** Options : indices `UBYTE4` + poids `UNORM8×4` (8 octets de plus), ou indices `USHORT4` + poids `UNORM16×4` (16 octets), ou poids en `float4` (24 octets). Recommandation : **indices sur 8 bits, poids en `UNORM16`** (12 octets de plus, 60 par sommet), dans un **second tampon de sommets** à côté du premier, pour que les passes qui ne skinnent pas continuent de lire le même tampon. *(À vérifier : formats de sommets de SDL_GPU, `UBYTE4` lu comme `uint4` dans HLSL et en MSL.)*
- **Nombre d'attributs** : `mesh.vert.hlsl` en lit déjà 13 (0 à 12). Deux pour le skinning et un pour le début de la palette font **16**, la limite garantie par Vulkan et sans doute par SDL_GPU *(à vérifier : limite de SDL_GPU et de Metal)*. Si ça ne tient pas : passer les attributs d'instance dans un storage buffer (lu par `SV_InstanceID`), ce que le jalon 3 n'avait pas besoin de faire.
- **Storage buffer ou tampon uniforme pour les palettes ?** Un tampon uniforme est limité (64 Ko, soit environ 1 000 matrices 4×4) ; un storage buffer n'a pas cette limite et tout tient en un seul envoi par image. Recommandation : **storage buffer**, matrices 3×4 (48 octets par os). *(À vérifier : liaison des storage buffers du vertex shader dans SDL_GPU, `register(t…, space0)`, et leur traduction par shadercross vers MSL.)*
- **Skinning linéaire ou par quaternions doubles ?** Le skinning linéaire (LBS) écrase les articulations tordues (effet « papier de bonbon ») ; les quaternions doubles (DQS) le corrigent mais gèrent mal l'échelle. Vu de haut et de loin, les défauts du LBS se voient peu. Recommandation : **linéaire**, DQS seulement si un modèle le montre.
- **Boîtes des personnages animés** : calculer la boîte de chaque pose (coûteux), une boîte de repos agrandie (fausse pour une attaque bras tendu), ou une boîte **par clip**, calculée au chargement en échantillonnant le clip. Recommandation : **boîte par clip** (union des poses échantillonnées), et l'union des clips d'un mélange.
- **Garder la palette de l'image précédente ?** Pour des vecteurs de mouvement (TAA) plus tard. Recommandation : **pas dans ce jalon**, mais le tampon de palettes est rangé de façon qu'un second tampon puisse s'ajouter.

### Pièges connus

- La **matrice des normales** : avec le skinning, la normale doit passer par la matrice de l'os (et la tangente aussi), sinon l'éclairage reste figé pendant que le personnage bouge. Avec une échelle non uniforme dans les os, il faudrait l'inverse transposée ; sinon la matrice suffit, puis normaliser.
- Les **ombres** oubliées : le personnage s'anime mais son ombre reste en pose de repos. Les trois shaders de sommets doivent skinner.
- Les **locations des attributs** : shadercross renumérote les entrées dans l'ordre (piège déjà rencontré : les locations doivent être contiguës à partir de 0). Les variantes skinnées ont donc leur propre numérotation, et les pipelines doivent la suivre.
- Un **cache d'ombres** qui garde l'ombre d'un personnage animé d'une image à l'autre : la signature de `PointShadowSlots` doit inclure ce qui bouge.
- Des poids qui ne somment pas à 1 : le personnage grossit ou rétrécit par endroits. Normaliser au chargement.
- Le culling avec la boîte de repos : un bras levé disparaît au bord de l'écran.

### Validation

- [ ] Un personnage de test s'anime dans la scène, avec son ombre du soleil et d'une torche ; même pose que Blender à un temps donné (comparaison comme au jalon 3, écart noté).
- [ ] Les captures de la démo 3D et de la tranche **sans personnage animé** sont inchangées (les maillages statiques n'ont pas changé de chemin).
- [ ] Aucun message de la couche de validation D3D12 en Debug, avec toutes les options d'anticrénelage.

---

## 6. Mélanges et transitions

### But

Passer d'une animation à l'autre **sans à-coup**, accorder la marche à la vitesse réelle, et jouer une attaque avec le haut du corps pendant que les jambes marchent.

### Tâches

- [ ] **Fondu** (crossfade) : passer d'un clip à un autre en une durée donnée **en ticks** ; le poids du nouveau monte, celui de l'ancien descend.
- [ ] **Mélange selon un paramètre** (un « blend space » en une dimension) : repos, marche, course placés sur un axe de vitesse ; le paramètre choisit les deux voisins et leur poids, et **synchronise leurs phases** (les pieds des deux clips se posent en même temps).
- [ ] **Vitesse de lecture accordée** au déplacement : un clip de marche déclare sa vitesse au sol (mètres par seconde, mesurée ou écrite dans le fichier de description), le jeu donne la vitesse réelle, le lecteur en déduit sa vitesse de lecture. Plus de pieds qui glissent.
- [ ] **Mélange partiel** (masque par os) : l'attaque sur le haut du corps (à partir d'un os choisi, la colonne), la locomotion en dessous.
- [ ] Un **fichier de description** des animations d'un personnage (JSON, dans l'esprit de `animations.json` du jalon 2) : clips, vitesses au sol, masques, durées des fondus par défaut, événements (partie 7).
- [ ] Une API pour le jeu, simple : « jouer ce clip avec un fondu de N ticks », « régler ce paramètre », « jouer ce clip sur ce masque » ; et un état lisible : clip dominant, fini ou non.

### Questions à se poser

- **Graphe d'animation en données, ou en code ?** Une machine à états décrite en JSON (états, transitions, conditions) est ce qu'ont les grands moteurs ; elle demande un petit langage de conditions et un éditeur pour être agréable. Le jeu de la tranche a quatre ou cinq états (repos/marche/course, attaque, touché, mort). Recommandation : **en code dans le jeu**, avec les briques du moteur (fondu, blend space, masque) ; les **paramètres** (durées, vitesses, masques, événements) en JSON. Un graphe en données sera reconsidéré au jalon 7 (outils et données), quand il y aura un vrai besoin.
- **Combien de couches ?** Recommandation : **deux** (corps entier, haut du corps masqué), plus les fondus à l'intérieur de chaque couche. Pas de mélange additif dans ce jalon (utile pour « respirer » ou « touché » par-dessus tout : à ajouter si la tranche le demande ; ozz le permet).
- **Poids déterministes ?** Les poids des fondus se calculent à partir du tick (entiers) ; ceux du blend space à partir d'un paramètre donné par le jeu. Seule la pose qui en sort est flottante (voir la partie 4).
- **Mouvement de la racine (root motion) ?** Soit le jeu déplace le personnage et l'animation suit (animations « sur place ») ; soit l'animation déplace le personnage (le déplacement de l'os racine devient celui de l'entité). Dans un ARPG, le déplacement vient du clic et du pathfinding (jalon 6) : recommandation : **sur place**, avec la vitesse de lecture accordée ; le root motion éventuellement plus tard pour des attaques qui avancent (charge, bond), et alors **extrait au chargement** en données par tick, pour rester déterministe.

### Pièges connus

- Mélanger des quaternions sans vérifier leur signe : la rotation fait le tour par le mauvais côté. Choisir le signe par le produit scalaire (ozz le fait *(à vérifier)*).
- Mélanger une marche et une course **sans synchroniser** leurs phases : jambes qui se croisent. Normaliser le temps (phase de 0 à 1) entre les clips d'un blend space.
- Le fondu qui **redémarre** l'attaque si le joueur clique à chaque tick : le jeu doit savoir si le clip joue déjà.
- La somme des poids qui n'est pas 1 dans une couche (fondu interrompu par un autre fondu) : garder une liste de clips avec leurs poids, et les renormaliser.
- Un masque qui coupe net à la colonne : la torsion se voit ; un poids dégressif sur deux ou trois os adoucit.

### Validation

- [ ] Du repos à la course en passant par la marche, en faisant varier la vitesse à la main (curseur dans la scène de test) : pas d'à-coup, pieds qui ne glissent pas (à l'œil, et la vitesse au sol mesurée affichée).
- [ ] Une attaque en marchant : haut du corps attaque, jambes marchent.
- [ ] Tests unitaires : poids d'un fondu tick par tick, fondu interrompu, voisins et poids du blend space, phases synchronisées.

---

## 7. Événements d'animation

### But

Déclencher du gameplay et du son **au bon moment d'un clip** : le pied touche le sol, le coup porte, la créature tombe. C'est la ligne de la roadmap : réutiliser les événements de l'`AnimationPlayer` (exactement une fois).

### Tâches

- [ ] Des événements par clip, à un **temps** (en secondes dans le fichier de description, convertis en ticks au chargement) avec un nom : `step_left`, `step_right`, `impact`, `death_ground`.
- [ ] Le système d'animation les **collecte** à chaque tick, avec l'entité et le clip, dans une liste que le jeu lit dans son `update()` (comme `advance(…, &fired)` au jalon 2).
- [ ] Règles avec les **mélanges** : les événements d'un clip dont le poids est faible ne partent pas (voir les questions).
- [ ] Dans la tranche : les **pas** à l'événement de pas (plus de compteur de distance), les **dégâts** à l'événement d'impact.
- [ ] Lire des événements dans les glTF ? Voir les questions.

### Questions à se poser

- **Où écrire les événements ?** glTF n'a pas d'événements. Options : dans le fichier de description JSON (à la main, avec le temps lu dans Blender), dans les `extras` du glTF (propriétés personnalisées de Blender, à exporter), ou par les **marqueurs** de la timeline de Blender (non exportés en glTF *(à vérifier)*). Recommandation : **dans le fichier de description JSON**, rechargé à chaud au jalon 7 ; les `extras` si un jour un script Blender les écrit.
- **Événements pendant un fondu** : si la marche s'estompe pendant que la course monte, qui joue les pas ? Recommandation : un événement part si son clip a un poids **≥ 0,5** au moment où il est franchi (dans un blend space synchronisé, le clip dominant) ; chaque clip peut le surcharger (un « impact » part quel que soit le poids, si le clip vient d'être lancé).
- **Événements sautés** : une vitesse ×3 ou un grand pas ne doivent pas en perdre (garantie déjà tenue au jalon 2). Un fondu qui démarre un clip au milieu ne rejoue pas les événements d'avant.

### Pièges connus

- Un événement exactement au temps 0 d'un clip qui boucle : partir **une fois par boucle**, ni zéro ni deux (le cas est testé au jalon 2 pour les sprites, à reprendre).
- Un clip interrompu juste avant son « impact » : pas de dégâts, ce qui est voulu (l'attaque a été annulée) ; le jeu ne doit pas les appliquer au clic « par sécurité ».
- Les événements lus **pendant le dessin** : jamais. Ils partent au tick, sinon ils dépendent de la cadence d'affichage.

### Validation

- [ ] Les pas du héros tombent sur ses pieds, à toutes les vitesses (à l'oreille et à l'œil) ; un rejeu donne les mêmes pas aux mêmes ticks.
- [ ] Tests unitaires : événements exactement une fois en boucle, avec vitesse, dans un fondu (règle du poids), au temps 0.

---

## 8. Attaches aux os

### But

Mettre un objet dans la main d'un personnage (épée, bouclier, torche) ou sur un point de son corps (effets, barre de vie au-dessus de la tête), et qu'il suive l'animation sans retard.

### Tâches

- [ ] Un composant « attaché à un os » : l'entité propriétaire, l'**os** (par nom, résolu en indice au chargement), un décalage (`Transform`). Il s'ajoute au `Parent` du jalon 4, qui prévoyait cette extension.
- [ ] Dans `World::collect()`, la matrice monde d'une entité attachée est : matrice monde du propriétaire × matrice modèle de l'os (**pose interpolée** de l'image) × décalage.
- [ ] Des **points d'attache** nommés dans le fichier de description (« main droite » = os `hand.R` + décalage), pour que le jeu ne connaisse pas les noms d'os d'un modèle.
- [ ] L'épée des modèles KayKit (ou autre) comme objet attaché ; la torche d'un personnage qui éclaire (une `LightSource` attachée).
- [ ] Les barres de vie et les noms au-dessus de la tête suivent l'os de la tête (ou restent au-dessus de la boîte : à décider en voyant le résultat).

### Questions à se poser

- **L'attache est-elle calculée au tick ou au dessin ?** Au **dessin**, avec la pose interpolée : sinon l'épée a un tick de retard sur la main. La logique ne lit pas la position de l'épée (voir la partie 4).
- **Ordre de calcul** : les poses de tous les personnages doivent être calculées **avant** la collecte des entités attachées. Recommandation : une étape « poses » au début de la collecte, puis la collecte.
- **Un objet attaché qui projette une ombre ou émet de la lumière** : il passe par les mêmes chemins qu'une entité ordinaire (c'est tout l'intérêt de le laisser être une entité).

### Pièges connus

- L'objet attaché à l'**os** au lieu du **point de prise** : l'épée traverse le poignet. Le décalage (et une rotation) est presque toujours nécessaire.
- L'échelle de l'os (0,01 d'un export FBX) appliquée à l'épée : l'arme devient minuscule. Décider si l'attache hérite de l'échelle de l'os (recommandation : **non**, seulement position et rotation).
- Un objet attaché à un personnage hors de la vue, lui-même visible (une longue lance) : le personnage n'a pas calculé sa pose. Calculer la pose si un de ses enfants est visible, ou agrandir la boîte du personnage.

### Validation

- [ ] L'épée reste dans la main pendant la marche, la course et l'attaque, à toutes les cadences d'affichage.
- [ ] Une torche attachée éclaire et suit la main.

---

## 9. Outils de debug et scène de test

### But

Voir ce que fait l'animation : le squelette, le clip joué, les poids, les événements. Et un endroit pour essayer un personnage sans passer par la tranche.

### Tâches

- [ ] Dessin du **squelette** en lignes de debug (`DebugLineRenderer` du jalon 3) : un segment de chaque os à son parent, les axes des os choisis, par-dessus le maillage ou seul.
- [ ] Inspecteur (jalon 4) : le composant d'animation (clips, temps en ticks et en secondes, vitesse, poids de chaque couche), et les attaches.
- [ ] Une fenêtre **Animation** dans DEBUG : le personnage choisi, une ligne de temps par clip actif avec ses événements, le dernier événement parti et son tick.
- [ ] Une scène de test « **Animation** » dans DEBUG > Tests moteur (et `--animation` en ligne de commande) : les personnages de test sur une plateforme, la liste de leurs clips, lecture / pause / image par image, vitesse, curseur de la vitesse de déplacement (blend space), bouton « attaque » (masque), fondu réglable, squelette et poids en vue de debug, pose de liaison.
- [ ] `--anim-compare` (dans l'esprit de `--blender-compare`) : un personnage à un temps donné, capturé, et le script Blender qui rend la même pose.
- [ ] Mettre à jour `TEST_MAC.md` avec une section « Jalon 5 ».

### Pièges connus

- Les lignes du squelette dessinées avec la pose **du tick** et le maillage avec la pose **interpolée** : le squelette « traîne ». Une seule pose par image, pour tout.

### Validation

- [ ] Dans la scène de test, chaque clip de chaque personnage se joue, s'arrête, avance image par image ; le squelette est dessiné à sa place exacte dans le maillage.
- [ ] La pose comparée à Blender au même temps : écart noté (comme la comparaison des matériaux du jalon 3).

---

## 10. Performances

### But

Savoir **combien de personnages animés** tiennent dans le budget, sur la machine de développement et sur la machine visée (Mac M3, 60 images par seconde), et où part le temps.

### Tâches

- [ ] Un test de charge `--skinned N` (dans la scène de test ou la démo 3D) : N personnages animés, chacun à une phase différente, sur plusieurs clips et fondus.
- [ ] Mesurer par image : temps CPU d'échantillonnage et de mélange, calcul des palettes, envoi du tampon de palettes ; temps GPU des passes (`--gpu-timing`) avec et sans ombres.
- [ ] Culling avant échantillonnage (un personnage hors de la vue ne calcule pas sa pose) et vérifier que ça compte.
- [ ] Si le CPU ne suffit pas : répartir l'échantillonnage sur plusieurs fils (chaque personnage est indépendant), ou réduire la fréquence d'échantillonnage des personnages lointains. Seulement si les mesures le demandent.
- [ ] Noter les chiffres ici, et les comparer à ceux de la partie 9 du jalon 4 (1,36 ms de CPU par image pour la démo 3D).

### Questions à se poser

- **Combien de personnages faut-il tenir ?** Un ARPG a des « vagues » de monstres : 50 à 150 à l'écran dans les moments chargés, parfois plus. Recommandation : viser **200 personnages animés** de 30 à 60 os à 60 images par seconde sur le Mac M3, avec de la marge pour la logique (IA, chemins) ; noter le plafond mesuré.
- **Plusieurs fils pour l'animation ?** Le jalon 4 disait : la logique sur un seul fil, le multithreading là où c'est isolé. L'échantillonnage des poses est **isolé** (chaque personnage lit ses clips et écrit sa palette) et ne touche pas à la logique. C'est le premier bon candidat. Recommandation : **mesurer d'abord** ; s'il le faut, une simple répartition par tranches sur quelques fils (sans système de tâches complet), et le résultat doit être identique à un seul fil.
- **Compression des clips** : ozz compresse ses clips (tolérance d'erreur réglable). À régler selon la mémoire mesurée : quelques Mo pour une centaine de clips, sans doute pas un sujet.

### Pièges connus

- Mesurer en Debug : ozz et GLM sans optimisation sont des dizaines de fois plus lents. Mesurer en Release, comme au jalon 3.
- Des personnages tous à la même phase : la mémoire cache aide trop, le chiffre est optimiste. Varier clips et phases.
- Un tampon de palettes recréé à chaque image : le réutiliser et le remplir (comme les tampons d'instances du jalon 3).

### Validation

- [ ] Le plafond de personnages animés à 60 images par seconde est mesuré et noté, sous Windows (et sur Mac dans TEST_MAC.md).
- [ ] La démo 3D sans personnage animé garde ses performances du jalon 4.

---

## 11. Personnages animés dans la tranche jouable

### But

Réunir le jalon dans la tranche jouable : la preuve que l'animation tient dans un vrai jeu.

### Contenu

- **Héros** : un personnage animé de test à la place du corps en primitives ; repos, marche et course selon sa vitesse (blend space, vitesse accordée), attaque au clic sur une créature ou à la compétence 1, avec l'épée attachée à la main ; les **pas** et l'**impact** aux événements.
- **Créatures** : un second personnage (squelette, monstre) : repos, marche, **touché** (mélange partiel ou additif, selon ce qui est fait), **mort** (clip `Once`, la créature reste au sol).
- Les **barres de vie** et les « Touché ! » suivent les têtes.
- La **pause** fige les animations (le temps est au tick : rien à faire de plus, à vérifier).

### Tâches

- [ ] Assembler, en gardant la démo 3D du jalon 3 (sans héros) et ses captures inchangées.
- [ ] Une **capture de référence** avec le rejeu de la tranche (`tests/data/slice_replay.json`, mis à jour si les coups ne portent plus au même tick), stable sur deux lancements.
- [ ] Mesurer : chargement (les personnages et leurs clips), mémoire (squelettes, clips, palettes), CPU et GPU par image, et comparer au jalon 4.
- [ ] Mettre à jour `FICHIERS_DU_PROJET.md`, `credits.json`, `TEST_MAC.md` (section « Jalon 5 »).

### Validation

- [ ] La tranche se joue du titre au retour au titre avec un héros et des créatures animés, sans message de validation en Debug.
- [ ] Sa capture de référence est stable ; les performances restent dans le budget, et l'écart avec le jalon 4 est noté.

---

## 12. Critères de fin de jalon

Le jalon est terminé quand **tout** ce qui suit est vrai :

- [ ] Squelettes, poids et clips se lisent depuis des glTF, passent par le **gestionnaire d'assets**, et se rechargent à chaud.
- [ ] Le **skinning** se fait sur le GPU, pour la scène et les ombres, avec instanciation et culling.
- [ ] Les clips se jouent au **tick** (temps entier, vitesse exacte), l'image étant **interpolée** ; la logique ne dépend d'aucune pose.
- [ ] **Fondus**, **blend space** de locomotion (pieds qui ne glissent pas) et **mélange partiel** fonctionnent.
- [ ] Les **événements** d'animation partent exactement une fois et pilotent les pas et les coups de la tranche.
- [ ] Une arme **attachée à un os** suit la main sans retard.
- [ ] La scène de test « Animation », le squelette en lignes de debug et la fenêtre Animation sont dans le menu DEBUG.
- [ ] Le plafond de personnages animés est mesuré et noté.
- [ ] Aucun avertissement de compilation, aucun message de la couche de validation du GPU. *Sous Windows (D3D12) ; Metal dans TEST_MAC.md.*
- [ ] La logique pure (chargement du squelette, temps, poids, événements, skinning CPU) a ses tests unitaires ; les captures de référence des jalons précédents sont inchangées.
- [ ] Les nouvelles bibliothèques et les nouveaux assets sont dans `credits.json` (fenêtre « À propos »).
- [ ] Les décisions de la section [Décisions à consigner](#décisions-à-consigner) sont remplies.
- [ ] La documentation des nouveaux fichiers est ajoutée à [FICHIERS_DU_PROJET.md](FICHIERS_DU_PROJET.md), et les vérifications Mac sont regroupées dans [TEST_MAC.md](TEST_MAC.md).

---

## 13. Risques principaux

| Risque | Impact | Parade |
|---|---|---|
| Modèles de test aux licences floues ou aux exports bancals (échelle, axes) | Temps perdu, crédits faux | Packs CC0 connus (KayKit, Quaternius) ; licence lue dans le pack ; vérification dans Blender |
| Lecteur glTF incomplet (ordre des os, nœud racine, cubique) | Personnages tordus sur certains modèles seulement | Modèle de référence minimal + modèles de conformité Khronos ; tests unitaires |
| Logique qui dépend de la pose | Rejeux et captures qui divergent, surtout entre OS | Règle : la logique ne lit que temps et événements ; poses au dessin seulement |
| Limite d'attributs de sommets | Pipelines refusés sur un OS | Compter avant d'écrire (16 attributs) ; repli : instances dans un storage buffer |
| Ombres ou cache d'ombres oubliés pour les personnages animés | Ombres figées | Trois shaders de sommets skinnés ; signature des ombres ponctuelles qui voit le mouvement |
| ozz lent sur ARM sans SIMD | Plafond de personnages trop bas sur Mac | Mesurer tôt sur le Mac (partie 10) ; fils de travail si besoin |
| Graphe d'animation en données trop tôt | Jalon sans fin | Transitions en code dans le jeu ; paramètres en JSON ; graphe revu au jalon 7 |
| Dérive du périmètre (IK, retargeting, root motion, morph targets, TAA) | Jalon sans fin | Hors périmètre sauf besoin de la tranche ; notés pour plus tard |

---

## Décisions à consigner

À remplir au fil du jalon. Chaque ligne correspond à une question posée plus haut.

| Sujet | Décision | Raison |
|---|---|---|
| Bibliothèque d'animation (maison, ozz-animation) | | |
| Modèles animés de test et licences | | |
| Squelette partagé entre modèles, clips dans d'autres fichiers | | |
| Nombre d'os et d'influences par sommet | | |
| Interpolation `CUBICSPLINE` | | |
| Temps d'un clip 3D (ticks), partage avec l'`AnimationPlayer` du jalon 2 | | |
| Échantillonnage au dessin (temps interpolé ou deux poses) | | |
| Ce qui est déterministe (temps, événements) et ce qui ne l'est pas (poses) | | |
| Format des indices et poids de sommets, second tampon | | |
| Palettes (storage buffer, matrices 3×4) | | |
| Skinning linéaire ou quaternions doubles | | |
| Boîtes des personnages animés pour le culling | | |
| Graphe d'animation (code ou données) et nombre de couches | | |
| Root motion ou animations sur place | | |
| Où sont écrits les événements ; règle des événements pendant un fondu | | |
| Attaches : calcul au dessin, héritage de l'échelle, points d'attache nommés | | |
| Budget de personnages animés, multithreading de l'échantillonnage | | |
