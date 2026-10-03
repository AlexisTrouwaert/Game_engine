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

- [x] Comparer les options (voir les questions), sur un personnage de test : compilation sous Windows et Mac, taille, API. *ozz-animation retenu (recommandation validée par l'utilisateur le 2026-09-25) ; Windows fait, Mac dans TEST_MAC.md.*
- [x] Ajouter la bibliothèque choisie à `vcpkg.json` et à `credits.json`. *Par un port à nous : ozz n'est pas dans le registre vcpkg (voir l'implémentation).*
- [x] Trouver deux ou trois **personnages animés** en glTF sous licence **CC0** (ou compatible), avec au moins : repos, marche, course, attaque, touché, mort. Un avec une **arme** séparée ou un os de main nommé. *KayKit : chevalier, guerrier et sbire squelettes, armes à part et os `handslot`.*
- [x] Les télécharger par un script (`tools/models/fetch_test_models.py`, ou un script à part pour les personnages), avec SHA-256 vérifiés, hors de Git, crédités ; convertir leurs textures en KTX2 avec `convert_gltf_textures.py`. *Script à part, `fetch_test_characters.py` ; textures laissées en PNG (voir l'implémentation).*
- [x] Un **modèle de référence minimal** écrit par un script (deux ou trois os, un clip de quelques clés aux valeurs connues), pour les tests unitaires : comme `make_reference_model.py` au jalon 3. *`make_skinned_reference_model.py`, valeurs vérifiées dans Blender.*

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

### Implémentation réalisée (partie 2)

Fichiers : `vcpkg.json`, `vcpkg-configuration.json`, `ports/ozz-animation/` (port), `src/moteur/CMakeLists.txt`, `tests/CMakeLists.txt` et `tests/test_ozz.cpp`, `apps/bac_a_sable/CMakeLists.txt` (licence copiée à côté de l'exécutable), `assets/credits.json`, `tools/models/fetch_test_characters.py`, `tools/models/make_skinned_reference_model.py` et `assets/models/skinned_reference.glb`, `.gitignore`. Aucune ligne de code du moteur : il est seulement lié à ozz, qu'il utilisera à la partie 3.

- **ozz-animation 0.17.0 n'est pas dans vcpkg** (ni dans la baseline du projet, ni dans le registre à jour) : le point *(à vérifier)* de la question s'est révélé faux. Plutôt que de le récupérer par CMake (`FetchContent`) à côté de vcpkg, le projet a son **port** : `vcpkg-configuration.json` déclare le dossier `ports/` (*overlay ports*), où `ports/ozz-animation/` décrit la construction. vcpkg le traite comme les autres (même triplet, même cache, Windows et Mac, CLion et presets), et le jour où ozz entre au registre, il suffira de supprimer le dossier.
- Le port construit les bibliothèques en statique, **sans** outils (`gltf2ozz`, `fbx2ozz`), exemples ni tests : les squelettes et les clips seront construits **au chargement** depuis ce que cgltf lit (`RawSkeleton`, `RawAnimation`, puis `SkeletonBuilder` / `AnimationBuilder`). ozz installe ses bibliothèques mais pas de paquet CMake : le port ajoute `ozz-animationConfig.cmake`, qui déclare `ozz::base`, `ozz::animation`, `ozz::animation_offline` et `ozz::geometry`. ozz traite ses avertissements en erreurs ; le port le désactive, pour qu'un compilateur plus récent ne casse pas le build d'une dépendance.
- **SIMD** : confirmé dans les sources, ozz n'a que des chemins SSE / AVX ; sur ARM (Mac M3), il prend son implémentation de référence, scalaire. Rien à faire ici, mais la mesure sur Mac de la partie 10 est d'autant plus importante.
- **Précision** : ozz stocke les rotations **quantifiées sur 16 bits** ; une pose est juste à 10⁻⁴ près (4·10⁻⁵ m mesuré dans le test). Les tests comparent donc les positions avec une tolérance absolue, et la comparaison avec Blender (partie 9) devra en tenir compte.
- `tests/test_ozz.cpp` : un squelette de deux os et deux clips construits en mémoire, échantillonnés (`SamplingJob`), mélangés (`BlendingJob`) et passés en espace modèle (`LocalToModelJob`). Il vérifie qu'ozz compile, se lie avec le bon runtime MSVC en Debug comme en Release, et calcule ce qu'on attend.

**Personnages de test** (`python tools/models/fetch_test_characters.py`, 14 Mo dans `assets/models/characters/`, hors de Git) :

| Fichier | Source et licence | Contenu |
|---|---|---|
| `kaykit_adventurers/Knight.glb` | KayKit *Adventurers* (Kay Lousberg), CC0 | Le héros : 41 os, 76 clips (`Idle`, `Walking_A/B/C`, `Running_A/B`, `1H_Melee_Attack_*`, `Hit_A/B`, `Death_A/B`, `Block`, `Dodge_*`...) ; 6 maillages skinnés et 9 **maillages rigides accrochés à des os** : deux épées et une épée courte dans `handslot.r` / `handslot.l`, quatre boucliers dans `handslot.l`, casque sous `head`, cape sous `chest` |
| `kaykit_adventurers/sword_1handed.gltf` (+ `.bin`, `knight_texture.png`) | idem | Une épée seule, à attacher (partie 8) |
| `kaykit_skeletons/Skeleton_Warrior.glb`, `Skeleton_Minion.glb` | KayKit *Skeletons* (Kay Lousberg), CC0 | Les créatures : **le même squelette de 41 os** que le chevalier (mêmes noms, même ordre), 95 clips (ceux du chevalier, plus `Death_C_Skeletons`, `Death_C_Skeletons_Resurrect`...) |
| `kaykit_skeletons/Skeleton_Blade.gltf` (+ `.bin`, `skeleton_texture.png`) | idem | Une lame seule |
| `khronos/SimpleSkin.gltf` | Khronos, CC0 | 2 os, le plus petit cas |
| `khronos/InterpolationTest.glb` | Khronos, CC0 | Pas de squelette : neuf cubes animés en `STEP`, `LINEAR` et `CUBICSPLINE` |
| `khronos/RiggedFigure.glb` | Cesium, **CC-BY 4.0** | 19 os, nœud racine `Z_UP`, clip sans nom |
| `khronos/Fox.glb` | PixelMannen (CC0), tomkranis, @AsoboStudio, @scurest (**CC-BY 4.0**) | 24 os, `JOINTS_0` en 16 bits, **pas de normales** (le moteur les calcule), 3 clips (`Survey`, `Walk`, `Run`) |

Écartés : `CesiumMan` (logo de Cesium, marque déposée), `BrainStem` (licence de Poser, pas libre), Mixamo, et Quaternius (le pack KayKit a déjà tout ce qu'il faut, avec un seul squelette pour le héros et les monstres).

Ce qu'on a appris des modèles KayKit, à reprendre dans les parties suivantes :

- Toutes les durées sont des multiples de 1/30 s (30 images par seconde dans Blender) : **deux ticks par image**, donc des durées en ticks entières sans arrondi (1,067 s = 64 ticks).
- Chaque clip anime les 41 os (123 canaux), y compris les os de contrôle de l'IK de Blender (`kneeIK.l`, `IK-foot.l`...) qui ne déforment aucun sommet : ils coûtent à l'échantillonnage. À mesurer (partie 10) ; les retirer du squelette au chargement est possible s'ils ne portent ni poids ni objet.
- Les armes et les boucliers sont **tous** présents dans le fichier, accrochés aux os : il faudra pouvoir **choisir les parties affichées** (partie 3 ou 8), sinon le chevalier tient deux épées et quatre boucliers.
- Les pieds sont à y = 0 et le chevalier mesure **2,45 m** casque compris (proportions de figurine, grosse tête) : il faudra une **échelle** à l'entité pour la carte, où une case fait 1 m (partie 11).
- Les textures sont de petites palettes PNG 1024 × 1024 (15 Ko), **dans** les `.glb` : non converties en KTX2, car `convert_gltf_textures.py` ne traite que les `.gltf` aux images séparées. En RGBA8 avec mipmaps, 5,3 Mo par personnage ; si ça compte, étendre l'outil aux `.glb` (partie 11).

**Modèle de référence animé** (`python tools/models/make_skinned_reference_model.py` → `assets/models/skinned_reference.glb`, 8 Ko, versionné, déterministe) : une colonne skinnée sur trois os et un cube rigide accroché à un os, avec les pièges de la partie 3 faits exprès (nœud d'armature qui n'est pas un os, nœuds listés enfants d'abord, `skin.joints` dans un autre ordre, translation du nœud du maillage à ignorer, `JOINTS_0` en 16 bits), et cinq clips aux valeurs connues : `Bend` (commence à 0,5 s), `Step` (`STEP`), `Cubic` (`CUBICSPLINE`), `Flip` (quaternions de signes opposés), `Scale`. La docstring du script donne les positions attendues.

**Vérifications faites (Windows)**

- vcpkg construit ozz 0.17.0 par le port, en Release et en Debug (runtime `/MD` et `/MDd` du triplet `x64-windows-static-md`) ; le moteur, le bac à sable et les tests se lient sans avertissement ; la licence est copiée dans `licenses/ozz-animation.txt` à côté de l'exécutable.
- **274** tests unitaires au vert (272 avant) en Release et en Debug, dont les deux d'ozz.
- `fetch_test_characters.py` : 15 fichiers, 13,6 Mo, SHA-256 vérifiés ; un second lancement ne télécharge rien.
- `make_skinned_reference_model.py` lancé deux fois : mêmes octets. Importé dans **Blender 5.2** (en ligne de commande), les positions de l'os `lower` sont celles de la docstring à 10⁻⁴ près dans les sept cas (`Bend` à 0,5 s du clip, `Step` à 0,25 et 0,75 s, `Flip` à 0,5 s, `Scale` à 0,5 s, `Cubic` à 0,25 et 0,5 s), et Blender trouve les trois os dans le bon ordre de parenté et les cinq clips.
- Chevalier, guerrier et sbire ouverts dans Blender : squelettes, tailles et parties rigides relevés ci-dessus.

### Validation

- [x] La bibliothèque choisie compile et lit un personnage de test sous Windows (Mac dans TEST_MAC.md). *Compile, se lie et calcule des poses justes (tests) ; la lecture d'un vrai personnage par le moteur est le travail de la partie 3.*
- [x] Les personnages de test sont téléchargés par script, crédités, et leurs licences notées dans les décisions.

---

## 3. Squelettes et clips glTF

### But

Lire depuis un glTF le **squelette** (os, parents, pose de liaison), les **poids** de chaque sommet et les **clips**, et en faire des assets partagés par le cache.

### Tâches

- [x] `parse_gltf` lit `JOINTS_0` et `WEIGHTS_0` (formats `UNSIGNED_BYTE`, `UNSIGNED_SHORT`, et `FLOAT` / normalisés pour les poids) ; les poids sont **renormalisés** (somme 1) ; un sommet sans poids est rattaché à l'os de son nœud. *Un sommet sans poids suit le premier os de la palette (le nœud d'un maillage skinné n'a pas de sens pour glTF) ; `JOINTS_1` / `WEIGHTS_1` lus aussi.*
- [x] `ModelData` garde un **squelette** : pour chaque os, son nom, son parent, sa transformation locale de repos (translation, rotation, échelle) et sa **matrice de liaison inverse** (`inverseBindMatrices`). Les os sont rangés **parents avant enfants** (ordre qu'attendent ozz et le calcul local → modèle). *Les matrices de liaison inverses sont par skin (`SkinData`), pas par os : deux skins peuvent lier le même os différemment.*
- [x] Les parties **skinnées** ne sont plus aplaties comme les autres : leur matrice de nœud est ignorée (règle de glTF : la pose vient des os), et elles gardent l'indice de leur skin.
- [x] Les parties **rigides accrochées à un os** (un maillage sans poids dont le nœud descend d'un os : armes, casque, cape des KayKit, cube de `skinned_reference.glb`) suivent cet os : on garde l'os et la transformation relative, au lieu de la matrice aplatie.
- [x] Pouvoir **choisir les parties affichées** d'un modèle par leur nom (le chevalier KayKit contient trois épées et quatre boucliers) : une liste de parties cachées au niveau de l'entité, ou un filtre au chargement. *Au niveau de l'entité : `ModelComponent::hide("Round_Shield")`.*
- [x] Lire les **animations** : chaque canal (translation, rotation, échelle d'un nœud), ses temps et ses valeurs, ses interpolations (`LINEAR`, `STEP`, `CUBICSPLINE`). Ignorer avec un message les canaux de **morph targets** (`weights`) et ceux qui visent un nœud hors du squelette. *Les nœuds animés font partie du squelette : aucun canal de translation, rotation ou échelle n'est perdu.*
- [x] En faire des assets : `Skeleton` et `AnimationClip3D` (noms provisoires) dans le cache, à côté des `Model`. Clé d'un clip : `models/hero.glb#Walk` (le fichier et le nom du clip). *`Skeleton` et `ClipLibrary` (tous les clips d'un fichier, `SkeletalClip` chacun), clé = le fichier : voir l'implémentation.*
- [x] Permettre des **clips dans un autre fichier** que le personnage, à condition que le squelette corresponde (voir les questions). *`ClipLibrary::mismatch(skeleton)`.*
- [x] Rechargement à chaud : un clip modifié est rechargé en place ; un squelette dont la structure change est refusé avec un message (comme `Model::replace_in_place`).
- [x] Afficher dans DEBUG > Assets les squelettes et les clips (nombre, mémoire, durée). *Nombre et mémoire (les deux nouveaux types) ; les durées sont dans la scène « Animation ».*

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

### Implémentation réalisée (partie 3)

Fichiers : `animation_data.hpp` / `.cpp` (nouveaux : données sans GPU ni ozz), `skeleton.hpp` / `.cpp` (nouveaux : ozz), `mesh.hpp` (`VertexSkin`), `model.hpp` / `model.cpp`, `assets.hpp` / `.cpp`, `world.hpp` / `.cpp` (parties cachées) ; `tests/test_skeleton.cpp` et un cas de `test_world.cpp` ; dans le bac à sable, `animation_test.hpp` / `.cpp` et le menu.

**Lecture (`parse_gltf`, données simples)**

- **Le squelette** (`SkeletonData`) : les os des skins **et** les nœuds que les clips animent, avec **tous leurs ancêtres**, dans l'ordre de la scène (profondeur d'abord, enfants dans l'ordre du fichier : l'ordre dans lequel ozz construit ses squelettes, vérifié à la construction). Ainsi le nœud d'armature (`Rig` des KayKit, `armature` du modèle de référence, `Z_UP` de RiggedFigure) déplace le personnage, et un fichier sans skin dont des nœuds sont animés (InterpolationTest) a quand même un squelette. Chaque os : nom (`node <n>` s'il n'en a pas), parent, pose de repos (TRS, une matrice du fichier étant décomposée).
- **Les skins** (`SkinData`) : la palette (les os du skin, dans l'ordre de `skin.joints`) et une matrice de liaison inverse par entrée. **256 os au plus par skin** (indices sur 8 bits dans les sommets) ; le squelette, lui, peut en avoir 1 024 (limite d'ozz).
- **Les poids** (`VertexSkin`, dans `MeshData::skin`, un par sommet) : `JOINTS_0` / `WEIGHTS_0` et `JOINTS_1` / `WEIGHTS_1`, les **quatre plus forts** gardés (à égalité, l'os de plus petit indice, pour ne pas dépendre de l'ordre du fichier), normalisés en 65535es dont la somme fait **exactement** 65535 (l'arrondi est rendu au plus fort). Un sommet sans poids suit le premier os de la palette. Un message par modèle compte ces corrections.
- **Trois sortes de parties** : skinnées (`skin`), **rigides sur un os** (`joint` et `joint_offset`, pour un maillage sans poids sous le nœud d'un os ou sous un os lui-même), fixes. `transform` reste la place de la partie **dans la pose de repos**, pour dessiner un modèle sans animation : pour une partie skinnée, la matrice de repos du premier os de son skin × sa matrice de liaison inverse (l'identité quand le fichier a été lié dans sa pose de repos, comme les KayKit ; **pas** pour RiggedFigure, dont le maillage est en Z vers le haut : il apparaissait couché et démesuré au premier essai avec l'identité).
- **Les clips** (`ClipData`) : une piste par os, les clés converties en clés **linéaires** partant de 0 (le premier temps du clip, 0,5 s pour `Bend`). `STEP` : une clé 0,1 ms avant chaque changement garde la valeur précédente. `CUBICSPLINE` : échantillonné à 60 Hz et à chaque clé (splines d'Hermite, tangentes multipliées par la durée du segment, comme le dit l'annexe de glTF). Un clip d'une seule pose (les `*_Pose` des KayKit, toutes les clés à 0) dure 1/60 s. Les canaux de morph targets sont ignorés (un message) ; les noms en double reçoivent « (2) ».
- `GltfOptions::meshes = false` ne lit que le squelette et les clips (ni parties, ni matériaux, ni images) : c'est ce que font les assets `Skeleton` et `ClipLibrary`, sans décoder les textures d'un personnage pour ses clips.

**Lecture des clips (ozz, derrière `skeleton.hpp`)**

- `Skeleton::create(SkeletonData)` : le squelette d'ozz, construit au chargement.
- `ClipLibrary::create(squelette, clips, fichier)` : **tous les clips d'un fichier** (`SkeletalClip` chacun, qui ne change jamais d'adresse, rechargement compris : un lecteur pourra garder un pointeur). Une propriété sans clé garde sa **valeur de repos** (ozz prendrait l'identité). `mismatch(skeleton)` dit pourquoi les clips ne peuvent pas jouer sur un autre squelette (nombre d'os, premier nom ou parent différent), vide sinon : les 95 clips du guerrier squelette jouent sur le chevalier.
- `sample_model_pose(squelette, clip, secondes, matrices)` : la pose en espace modèle à un instant. Il alloue à chaque appel : c'est l'outil des tests et de la scène de test, pas du jeu. Le lecteur au tick est la partie 4.
- ozz reste **privé** : aucun en-tête public ne l'inclut, le bac à sable et les tests (sauf `test_ozz.cpp`) ne le voient pas.

**Assets**

- `assets.skeleton(chemin)` et `assets.clips(chemin)`, **sans** asset de remplacement (une erreur lève une exception : pas de squelette sensé à inventer). Clé = le fichier : **une** lecture pour les 80 clips d'un KayKit, plutôt que 80 avec des clés `fichier#clip` comme le proposait la doc. Mémoire : 1,3 Mo pour les 95 clips du guerrier.
- Rechargement à chaud **en place** : le squelette refuse un changement de structure, la bibliothèque un changement de squelette ou de noms de clips (« reload the scene »). Deux nouveaux types dans DEBUG > Assets (`skeleton`, `clips`), rechargeables.

**Parties cachées** : `ModelComponent::hidden_parts` (indices triés) et `hide("Round_Shield")`, qui cache toutes les parties faites de ce nœud ; `Model::parts_of(nœud)`. Le monde ne les dessine pas, cache des entités fixes compris.

**Scène de test « Animation »** (DEBUG > Tests moteur, dernier de la liste ; `--menu-test 10`), en avance sur la partie 9 : le modèle de référence, le chevalier (une épée et un bouclier, les autres parties cachées), le guerrier et le sbire squelettes, le renard et RiggedFigure, côte à côte à la même hauteur, chacun jouant un clip de son fichier ; lecture, vitesse, choix du personnage et du clip, curseur du temps, pose de repos. Les maillages skinnés restent en **pose de repos** (le skinning est la partie 5) ; le **squelette** est dessiné par-dessus en lignes de debug, et les **parties rigides suivent déjà leur os** : l'épée et le bouclier bougent avec les mains du chevalier, le cube du modèle de référence avec l'os `lower`.

**Mesures (Release)** : `Knight.glb` lu en 30 ms (maillages, 76 clips, texture PNG) ; les 95 clips du guerrier construits pour ozz en 18 ms, 1,3 Mo.

**Vérifications faites (Windows)**

- **285** tests unitaires (274 avant) en Release et en Debug : `test_skeleton` (squelette du modèle de référence : ordre, parents, ancêtre qui n'est pas un os, palette dans l'ordre de `skin.joints`, matrices de liaison ; parties skinnée et rigide ; clés `STEP` et `CUBICSPLINE` ; **les dix positions de la docstring, retrouvées par ozz comme par Blender** ; lecture sans maillages ; quatre influences sur six et normalisation exacte ; messages de `mismatch` ; rechargement en place accepté et refusé ; KayKit : 42 os, clips partagés entre le guerrier et le chevalier, armes et casque sur leurs os ; Khronos : SimpleSkin, Fox, RiggedFigure, InterpolationTest) et un cas de `test_world` (parties cachées).
- Captures de référence **inchangées** : tranche `dbf96994a52d`, démo 3D `fdc076d9c3ae` et `952477744ffb`.
- Scène « Animation » en Release et en Debug (couche de validation D3D12 active) : aucun message ; squelettes à leur place dans les six modèles (capture d'écran), épée et bouclier qui suivent les mains.
- Rechargement à chaud : `skinned_reference.glb` réécrit dans les sources pendant que la scène tourne : `model`, `skeleton` et `clips` rechargés, sans plantage.
- Build sans avertissement, Release et Debug.

### Validation

- [x] Le modèle de référence minimal donne, au chargement, le bon nombre d'os, les bons parents, les bonnes matrices et les bonnes durées (tests unitaires).
- [x] Les personnages de test et les modèles Khronos choisis se chargent sans erreur ; un fichier au squelette incompatible est refusé avec un message qui nomme l'os. *Le refus est un message de `ClipLibrary::mismatch` (testé) ; c'est le lecteur de la partie 4 qui l'appliquera.*
- [x] Les modèles statiques du jalon 3 se chargent comme avant (captures de référence inchangées).

---

## 4. Lecture des clips et temps de la simulation

### But

Jouer un clip 3D avec les **mêmes garanties** que l'`AnimationPlayer` du jalon 2 : temps entier, vitesse exacte, événements exactement une fois, même résultat sur toutes les machines ; et en tirer, à chaque image, une **pose interpolée** fluide.

### Tâches

- [x] Séparer dans l'`AnimationPlayer` actuel ce qui est **commun** (temps en millièmes de tick, vitesse en millièmes, modes de lecture, événements franchis) de ce qui est propre aux sprites (images et régions), pour que le lecteur 3D réutilise le premier. Les tests du jalon 2 doivent passer sans changement. *`ClipClock` ; tests et capture de la démo 2D inchangés.*
- [x] Un clip 3D a une **durée en ticks** (la durée du fichier en secondes × 60, arrondie) et des événements à des **ticks** (partie 7). *Durée : `SkeletalClip::duration_ticks()` ; les événements sont la partie 7.*
- [x] Un composant d'animation (ECS) : le clip (ou les clips, voir la partie 6), le temps, la vitesse ; un **système** qui avance tous les lecteurs d'un tick dans `update()`. *`Animator` et `advance_animators()`.*
- [x] L'**échantillonnage** se fait au dessin : au temps du tick précédent et du tick présent, interpolé par l'`alpha` du pas fixe (comme `PreviousTransform`), ou directement au temps interpolé (voir les questions). *Au temps interpolé.*
- [x] Calcul **local → modèle** (matrices des os dans l'espace du personnage), puis **palette** de skinning (matrice modèle de l'os × matrice de liaison inverse). *`PoseSampler::model()` et `palette(skin)`, que la partie 5 enverra au GPU.*
- [x] Personnages hors de la vue : ne pas échantillonner (le temps avance quand même, au tick). *`CollectOptions::view`.*

### Questions à se poser

- **Échantillonner au temps interpolé, ou interpoler deux poses ?** Échantillonner une fois au temps `précédent + alpha × (présent - précédent)` coûte un échantillonnage par image ; interpoler deux poses en coûte deux. Recommandation : **au temps interpolé**. Attention aux boucles (le temps revient à 0) et aux changements de clip entre deux ticks (dans ce cas, prendre la pose du tick présent).
- **Qu'est-ce qui est déterministe ?** Le **temps** (entier), les **événements**, le clip joué, les poids des mélanges (calculés en entiers ou à partir d'entiers au tick) : tout ce que la logique lit. La **pose** (flottants, SIMD) ne sert qu'au dessin. Recommandation : **la logique ne lit jamais une pose**. Si un jour le jeu a besoin de la position d'un os (lancer un projectile depuis la main), il l'échantillonne au tick, au temps entier, et on accepte qu'elle puisse différer d'un ulp entre Windows et Mac (les captures de référence sont déjà propres à chaque OS pour les images ; les rejeux, eux, ne doivent pas en dépendre).
- **La même horloge que les sprites ?** Oui, c'est le sens de la ligne de la roadmap : un seul mécanisme de temps et d'événements pour les sprites 2D (effets, interface) et les squelettes 3D.

### Pièges connus

- Convertir les secondes du fichier en ticks **à chaque échantillonnage** avec des `float` : dérive au bout de longues boucles. Garder le temps en entiers et ne convertir en secondes qu'au dernier moment, pour ozz.
- Le **dernier instant d'une boucle** : échantillonner exactement à la durée donne la dernière clé, puis la première au tick suivant ; si les deux ne sont pas identiques dans le fichier, la boucle « saute ». Le noter, ne pas le corriger dans le moteur (c'est un défaut du clip).
- Un clip `Once` terminé qui reprend à 0 à cause d'une interpolation entre « fin » et « début » : le temps d'un clip terminé ne boucle pas.

### Validation

### Implémentation réalisée (partie 4)

Fichiers : `animation_clock.hpp` / `.cpp` et `animator.hpp` / `.cpp` (nouveaux), `animation.hpp` / `.cpp` (l'`AnimationPlayer` passe sur l'horloge commune), `skeleton.hpp` / `.cpp` (`duration_ticks`, `PoseSampler`), `world.hpp` / `.cpp` (modèles animés, `CollectOptions::view`), `debug_tools.cpp` (inspecteur) ; `tests/test_animator.cpp` ; dans le bac à sable, `animation_test.*` (passée au monde) et `main.cpp` (`--fixed-hz`).

- **`ClipClock`** : le temps d'un clip, commun aux sprites et aux squelettes. Durée d'un cycle en ticks entiers, temps et vitesse en **millièmes** (entiers), lecture unique ou en boucle, et des **marques** (des points du cycle en ticks) que `advance()` signale **exactement une fois**, même quand un grand pas en saute plusieurs ou fait plusieurs tours. C'est l'algorithme du jalon 2, sorti de l'`AnimationPlayer` : celui-ci garde son API et ses tests ; ses marques sont les débuts de ses étapes (le retour d'un `PingPong` fait partie du cycle). La partie 7 y mettra les événements des clips 3D.
- **Durée en ticks** : `SkeletalClip::duration_ticks()` = la durée × 60, arrondie, au moins 1 (`kClipTicksPerSecond`). Les KayKit tombent juste (30 images par seconde : 64 ticks pour `Idle`). À une autre fréquence de logique, un clip garde son nombre de ticks, donc joue plus vite ou plus lentement : voulu, c'est ce qui le garde déterministe.
- **`Animator`** (composant) : poignées du squelette et des clips, le clip joué (un pointeur, qui ne bouge jamais), son horloge, le temps du tick précédent. `Animator::create` refuse des clips faits pour un autre squelette (message de `mismatch`) ; `play(nom, boucle)` repart du début en gardant la vitesse, et refuse un nom inconnu en nommant le fichier. **`advance_animators(registry)`** : le système, une fois par `update()`.
- **Interpolation** : `ratio(alpha)` donne la position dans le clip entre le tick précédent et le présent. Le temps n'est jamais ramené dans le cycle (il continue de compter), donc une boucle qui repart entre deux ticks avance (0,95 → 1 → 0,05) au lieu de revenir en arrière par le milieu. Juste après `play()`, rien à interpoler : la première pose du clip.
- **Dessin** (`World::collect`) : une entité qui a un `ModelComponent` et un `Animator` est **échantillonnée** au temps interpolé, puis ses parties rigides suivent leur os (les parties skinnées restent en pose de repos jusqu'à la partie 5). Culling **avant** l'échantillonnage : la boîte du modèle, doublée autour de son centre (un bras qui se tend sort de la pose de repos ; les boîtes par clip viendront avec le skinning), contre `CollectOptions::view` (le frustum de la caméra ; par défaut, tout passe). La pose est gardée dans un composant **`AnimationPose`** (un `PoseSampler` : le cache d'ozz, qui rend la lecture d'un clip vers l'avant bon marché, et les matrices), que les outils lisent : le squelette en lignes de la scène de test vient de là. Ce n'est pas un état du jeu : la logique ne le lit pas.
- **Inspecteur** : l'`Animator` est inscrit (clip, en boucle ou une fois, temps en ticks, fini, vitesse modifiable).
- **Scène « Animation »** passée au monde : chaque personnage est une entité (`Transform` pour la mise à l'échelle, `ModelComponent` avec ses parties cachées, `Animator`), avancée au tick, dessinée par `World::submit` avec le frustum. Nouveaux réglages : **Interpolation** (sinon, dessin au dernier tick), temps en ticks, fréquence de la logique affichée. Le monde est dans l'inspecteur (DEBUG > Inspecteur d'entités).
- **`--fixed-hz N`** (bac à sable) : une autre fréquence de logique. À 10, l'interpolation se voit : avec, le mouvement reste fluide à la fréquence de l'écran ; sans, il saute dix fois par seconde.

**Vérifications faites (Windows)**

- **291** tests unitaires (285 avant), en Release et en Debug : `test_animator` (marques vues une fois pas à pas ou d'un seul pas de 25 ticks, lecture unique, vitesse ×1,5, `restart`, `set_time` ; durées en ticks du modèle de référence et du chevalier, pose seule = 1 tick ; `Animator` : ratio aux ticks et entre deux, passage de la boucle vers l'avant, lecture unique finie, vitesse gardée, nom inconnu et squelette incompatible refusés ; `World` : le cube du modèle de référence à sa place à mi-`Bend`, colonne en pose de repos, `AnimationPose` lisible, rien d'échantillonné ni de dessiné hors du frustum). Les tests de l'`AnimationPlayer` du jalon 2 passent **sans changement**.
- Captures de référence **inchangées** : démo 2D `bd91e8b2c616` (qui joue des `AnimationPlayer`), tranche `dbf96994a52d`, démo 3D `fdc076d9c3ae` et `952477744ffb`.
- Scène « Animation » en Release (à 60 et à 10 ticks par seconde, 164 images par seconde) et en Debug (couche de validation D3D12 active) : aucun message. 1,6 ms de CPU par image pour six personnages.
- **Vérifié à la main** (2026-09-25, par l'utilisateur, Windows) : l'animation est fluide à l'œil ; à `--fixed-hz 10`, le mouvement reste fluide avec l'interpolation et saute dix fois par seconde sans.

### Validation

- [x] Un clip avancé tick par tick ou par grands pas donne le même temps et les mêmes événements (tests unitaires, comme au jalon 2).
- [x] À 60, 144 et 165 Hz d'écran, l'animation est fluide (pas de saccade au rythme des ticks) ; `--fixed-dt` ou une fréquence de logique basse (option de debug) le rend visible si ce n'est pas le cas. *Vérifié à l'œil sous Windows avec `--fixed-hz 10` et la case « Interpolation » ; reste l'écran du Mac (TEST_MAC.md).*

---

## 5. Skinning sur le GPU

### But

Déformer les maillages skinnés dans le **vertex shader**, pour la passe de la scène **et** pour les ombres, en gardant l'instanciation et le culling du jalon 3.

### Tâches

- [x] Un format de sommet pour les maillages skinnés : les 48 octets de `Vertex3D`, plus **quatre indices d'os** et **quatre poids** (voir les questions). Les maillages statiques ne changent pas.
- [x] Un **tampon de palettes** par image : toutes les matrices d'os de tous les personnages visibles, les unes après les autres, dans un **storage buffer** lu par le vertex shader ; chaque instance porte le **début de sa palette**.
- [x] Variantes skinnées de `mesh.vert.hlsl`, `shadow.vert.hlsl` et `point_shadow.vert.hlsl` (le même code de skinning, dans un fichier inclus), et les pipelines qui vont avec (toutes les variantes d'échantillonnage MSAA comprises).
- [x] `MeshBatcher` : les maillages skinnés forment leurs propres lots (même maillage, mêmes textures : un appel instancié, chaque instance avec sa palette).
- [x] **Boîtes** pour le culling : une boîte qui contient toutes les poses possibles (voir les questions), mise à la place du personnage. *Une boîte **par image**, celle de la pose : voir l'implémentation.*
- [x] Ombres du soleil et des lumières ponctuelles correctes pour un personnage animé (le cache des ombres ponctuelles, `PointShadowSlots`, doit savoir qu'un personnage animé **change** à chaque image).
- [x] Un skinning **sur le CPU** dans les tests (même formule), pour vérifier une pose sans GPU. *`skin_position()`.*
- [x] Vues de debug (`MeshView`) : les poids d'un os choisi en couleur. *Fait à la partie 9 (`MeshView::Weights`).*

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

### Implémentation réalisée (partie 5)

Fichiers : `shaders/skinning.hlsli` (nouveau), `mesh.vert.hlsl`, `shadow.vert.hlsl`, `point_shadow.vert.hlsl` (une partie `#ifdef SKINNED`) et leurs variantes `*_skinned.vert.hlsl` (nouvelles, trois lignes chacune), `cmake/Shaders.cmake` ; `mesh.hpp` / `.cpp`, `mesh_batcher.hpp` / `.cpp`, `mesh_renderer.hpp` / `.cpp`, `model.hpp` / `.cpp`, `skeleton.hpp` / `.cpp`, `world.hpp` / `.cpp` ; `tests/test_skinning.cpp` ; la scène « Animation » (torche).

- **Sommets** : `Mesh::skin`, un second tampon de sommets (un `VertexSkin` de **12 octets** par sommet : indices sur 8 bits, poids en `UNORM16`), créé pour un maillage qui a des poids. Les maillages fixes et leur tampon de 48 octets ne changent pas. Attributs 13 (`UBYTE4`, lus en `uint4`) et 14 (`USHORT4_NORM`), sur un troisième emplacement de tampon.
- **Palettes** : `MeshRenderer::add_palette(matrices)` range les matrices d'une pose (3 lignes de `float4` chacune) dans le tableau de l'image et rend un `Palette` (premier indice, taille) ; `draw(..., palette)` enregistre un maillage skinné. Toutes les palettes partent **en un seul envoi** par image, dans un **storage buffer** (`SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ`) lié aux vertex shaders skinnés (`register(t0, space0)` ; en Metal, `buffer(1)`, après le bloc uniforme). Une palette est donnée **une fois par skin et par personnage**, quel que soit le nombre de ses maillages (les six du chevalier partagent la sienne).
- **Le début de la palette** voyage dans `MeshInstance::emissive.w`, qui était libre : **aucun attribut de plus**. Le shader de la scène lit 15 attributs (13 + 2), sous la limite de 16 ; les shaders d'ombre lisent la ligne émissive de l'instance en location 4, puis les indices et les poids en 5 et 6 (locations contiguës, comme l'exige la renumérotation de shadercross).
- **Shaders** : `skinning.hlsli` (palette et somme pondérée des quatre matrices, skinning linéaire) ; chaque vertex shader a sa partie `#ifdef SKINNED`, et `mesh_skinned.vert.hlsl` (de même pour les ombres) se résume à `#define SKINNED 1` puis `#include "mesh.vert.hlsl"`. `shadercross` reçoit `-I shaders/` ; tout shader est recompilé quand un fichier de `shaders/` change (dépendances simples et sûres). La normale et la tangente passent par la partie 3 × 3 de la matrice de skinning (les os ne sont pas étirés inégalement), puis par l'instance comme avant. MSL régénéré (`export_msl_shaders`) : trois fichiers de plus, et `mesh.vert.msl` ne change que par le nom de ses variables.
- **Pipelines** : huit de plus, créés comme les autres (scène : simple et double face, fil de fer ; refaits avec le MSAA ; ombre du soleil ; ombres ponctuelles). `draw_batch()` lie ce que lit le pipeline du lot : sommets (et poids), tranche des instances, palettes pour un lot skinné, indices.
- **Lots** : la clé d'un lot a un drapeau « skinné » : un maillage skinné dessiné sans palette (le chevalier comme modèle fixe, dans la scène « Rendu 3D ») reste un maillage fixe, en pose de repos, dans son propre lot. Des personnages qui partagent un maillage restent **un seul appel instancié**, chaque instance avec sa palette.
- **Monde** : `WorldSink` a deux méthodes de plus, `palette()` et `skinned_mesh()` (par défaut : un maillage ordinaire, pour les tests qui enregistrent). Une partie skinnée d'un modèle animé est donnée avec la place de l'entité (la palette contient tout le reste).
- **Boîtes** : pas une boîte par clip, mais **la boîte de la pose de l'image**, simple et toujours juste : la boîte des os du skin dans la pose, agrandie de la **portée** de la partie (`skin_radius`, calculée au chargement : la plus grande distance d'un sommet à son os principal dans la pose de repos). Elle sert au culling de chaque passe et au tri des ombres ponctuelles. (Le rejet grossier avant l'échantillonnage, la boîte de repos doublée, reste celui de la partie 4.)
- **Ombres ponctuelles** : la signature d'une lumière comprend les matrices de la palette de chaque personnage animé dans sa sphère : l'ombre est redessinée quand la pose change, et reste en cache pour un personnage immobile (vitesse 0, pose identique).
- **CPU** : `skin_position(palette, poids, position)`, la même formule que le shader, pour les tests.
- **Scène « Animation »** : les personnages sont déformés, et une **torche** (lumière ponctuelle avec ombres, case à cocher) éclaire la rangée de côté.

**Comparaison avec Blender** : le chevalier dans trois poses (`Idle` à 0,5 s, `1H_Melee_Attack_Chop` à 0,4 s, `Running_A` à 0,3 s), ses six maillages skinnés déformés sur le CPU par la formule du shader, comparés aux maillages évalués par Blender 5.2 après import du `.glb` : les boîtes coïncident à **3·10⁻⁴ m** près (tolérance du test : 2 mm), l'arrondi des rotations d'ozz compris. Le skinning du GPU suit la même formule ; à l'écran, les maillages tombent exactement sur les squelettes dessinés en lignes.

**Mesures** (Release, scène « Animation », six personnages, torche) : 1,52 ms de CPU par image ; 34 maillages, 155 appels de dessin ; ombres ponctuelles redessinées à chaque image (80 maillages, 42 000 triangles dans les six faces) ; 962 Kio envoyés par image, surtout les instances des sept passes. Le coût à grande échelle est la partie 10.

**Vérifications faites (Windows)**

- **295** tests unitaires (291 avant), en Release et en Debug : `test_skinning` (colonne de référence : immobile au repos, pliée à mi-`Bend` sommet par sommet, y compris les sommets mélangés 0,75 / 0,25 ; portée des parties ; lots skinnés séparés et début de palette dans l'instance, pour la scène et les ombres ; chevalier contre Blender).
- Captures de référence **inchangées** : tranche `dbf96994a52d`, démo 3D `fdc076d9c3ae` et `952477744ffb`.
- Scène « Animation » en Debug (couche de validation D3D12) avec **aucun, FXAA, MSAA 2× et 4×**, et les vues **fil de fer** et **normales** : aucun message. Capture d'écran : maillages sur leurs squelettes, bras baissés du chevalier au repos, marche et course des squelettes, renard qui marche, colonne pliée avec son ombre ; en fil de fer aussi.
- **Non vérifié** : le cache des ombres ponctuelles avec un personnage immobile (vitesse 0 : aucune ombre redessinée), vu seulement par construction de la signature.

### Validation

- [x] Un personnage de test s'anime dans la scène, avec son ombre du soleil et d'une torche ; même pose que Blender à un temps donné (comparaison comme au jalon 3, écart noté). *3·10⁻⁴ m.*
- [x] Les captures de la démo 3D et de la tranche **sans personnage animé** sont inchangées (les maillages statiques n'ont pas changé de chemin).
- [x] Aucun message de la couche de validation D3D12 en Debug, avec toutes les options d'anticrénelage.

---

## 6. Mélanges et transitions

### But

Passer d'une animation à l'autre **sans à-coup**, accorder la marche à la vitesse réelle, et jouer une attaque avec le haut du corps pendant que les jambes marchent.

### Tâches

- [x] **Fondu** (crossfade) : passer d'un clip à un autre en une durée donnée **en ticks** ; le poids du nouveau monte, celui de l'ancien descend.
- [x] **Mélange selon un paramètre** (un « blend space » en une dimension) : repos, marche, course placés sur un axe de vitesse ; le paramètre choisit les deux voisins et leur poids, et **synchronise leurs phases** (les pieds des deux clips se posent en même temps).
- [x] **Vitesse de lecture accordée** au déplacement : un clip de marche déclare sa vitesse au sol (mètres par seconde, mesurée ou écrite dans le fichier de description), le jeu donne la vitesse réelle, le lecteur en déduit sa vitesse de lecture. Plus de pieds qui glissent. *Mesurée par `measure_stride()`, écrite dans le fichier.*
- [x] **Mélange partiel** (masque par os) : l'attaque sur le haut du corps (à partir d'un os choisi, la colonne), la locomotion en dessous.
- [x] Un **fichier de description** des animations d'un personnage (JSON, dans l'esprit de `animations.json` du jalon 2) : clips, vitesses au sol, masques, durées des fondus par défaut, événements (partie 7). *`assets/animations/kaykit.json` ; les événements viendront avec la partie 7.*
- [x] Une API pour le jeu, simple : « jouer ce clip avec un fondu de N ticks », « régler ce paramètre », « jouer ce clip sur ce masque » ; et un état lisible : clip dominant, fini ou non.

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

### Implémentation réalisée (partie 6)

Fichiers : `animation_set.hpp` / `.cpp` (nouveaux), `animator.hpp` / `.cpp` (réécrits), `skeleton.hpp` / `.cpp` (`PoseInput`, `PoseSampler::blend`, `measure_stride`), `world.hpp` / `.cpp` (la pose demandée à l'`Animator`), `assets.hpp` / `.cpp` (`assets.animation_set(chemin)`), `debug_tools.cpp` (inspecteur) ; `assets/animations/kaykit.json` (nouveau) ; `tests/test_blending.cpp` (nouveau) ; la scène « Animation » (le chevalier des mélanges).

- **Fichier de description** (`AnimationSet`, asset rechargé à chaud) : fondu par défaut en ticks ; par clip, vitesse au sol (mètres du fichier par seconde), fondu propre, décalage de phase ; les **blend spaces** (une liste de clips, placés d'eux-mêmes sur l'axe de la vitesse à leur vitesse au sol : 0 pour un repos) ; les **masques** (un os, et une rampe sur les premiers os en dessous) et le masque de la couche du haut. Format en commentaire dans l'en-tête. Données seules : l'`Animator` résout clips et os quand il joue (un rechargement vaut pour les mouvements lancés ensuite).
- **`Animator`** (réécrit) : **deux couches**, 0 pour le corps entier, 1 pour le haut du corps (le masque du fichier), par-dessus. Une couche joue des **mouvements** : un clip, ou un blend space. `play(nom, {couche, fondu, boucle, relancer, accorder})` fait un fondu enchaîné depuis ce que jouait la couche ; un fondu interrompu repart des poids où il en était ; quatre mouvements au plus par couche (le plus faible part). **Poids en millièmes, entiers**, avancés au tick comme les temps : mêmes poids sur toutes les machines ; `advance(5)` donne ce que donnent cinq `advance()`. Le dessin interpole poids, temps et vitesse entre les deux derniers ticks, et renormalise les poids.
- **Ne pas relancer** : `play` d'un mouvement que la couche joue déjà (le dernier, pas fini) ne fait rien et rend `false` ; `restart` force. Un clip joué une fois et fini peut être redemandé.
- **Blend space** : une horloge de **phase** (le cycle compté en `kPhaseTicks`), dont le pas par tick est l'inverse de la durée des deux voisins pondérée : les deux clips restent en phase (les pieds se posent ensemble, avec le décalage de phase du fichier). Au-delà du clip le plus rapide, celui-ci joue plus vite (`rate`).
- **Vitesse de lecture accordée** : `set_move_speed(m/s du fichier)` place les blend spaces sur leur axe, et un clip joué avec `match_speed` et une vitesse au sol avance à `vitesse / vitesse au sol`. L'`Animator` ignore l'échelle de l'entité : le jeu divise par elle.
- **Couche du haut** : elle monte en fondu quand elle démarre ; un clip joué une fois s'y **efface de lui-même** pour finir avec le clip (le fondu commence quand il reste autant de ticks que la durée de son fondu). Au dessin, le corps entier cède la place os par os : poids `1 - u × masque` pour la couche 0, `u × masque` pour la couche 1.
- **Pose** : `Animator::pose(alpha, requête)` donne les clips, leurs temps et leurs poids ; `PoseSampler::blend` les échantillonne (un cache d'ozz par entrée) et les mélange avec le **`BlendingJob` d'ozz** (poids par os en SoA). Un seul clip sans masque coûte un échantillonnage, comme avant. ozz prend bien le **chemin court** entre deux quaternions de signes opposés (test).
- **Vitesses au sol mesurées** : `measure_stride(squelette, clip, pieds)` échantillonne le clip, garde les instants où chaque pied est près de son point le plus bas, et moyenne sa vitesse vers l'arrière ; il donne aussi l'instant où chaque pied se pose. Chevalier : `Walking_A` 0,820 m/s (64 ticks), `Running_A` 3,467 m/s (48 ticks) ; le pied gauche se pose à 0,99 du cycle de la marche et à 0,16 de celui de la course : décalage de phase **0,164** pour la course dans `kaykit.json`.
- **Masque du haut du corps** : depuis `spine`, rampe de 2 (`spine` 1/3, `chest` 2/3, bras et tête 1) : pas de cassure nette à la taille.
- **Scène « Animation »** : un **chevalier** fait le tour d'une ellipse devant la rangée ; curseur de vitesse (il accélère vers elle à 3 m/s²), « Avancer » (décoché : il marche sur place, pour comparer), fondu en ticks, fondu vers n'importe quel clip ou vers `locomotion`, attaque au choix sur le haut du corps (« Frapper »), poids et phases affichés, vitesses au sol du fichier et mesurées, et la **dérive du pied posé** le long du chemin (moyenne glissante, mesurée à l'image) : **+0,05 m/s** en marchant à 0,66 m/s.
- **Inspecteur** : l'`Animator` montre ses couches, ses mouvements et leurs poids, et la vitesse de déplacement.

**Mesures du glissement** (test `KayKit: the planted foot does not drift`, en ligne droite, une pose par tick) : dérive moyenne du pied posé −0,04 m/s à 0,82 m/s (marche seule), −0,11 à 1,5 (mélange marche/course 74/26), 0,00 à 2,5, −0,12 à 3,47 (course seule) et −0,15 à 4,2 (course accélérée) : 3 à 7,5 % de la vitesse. Le glissement total (en norme, le pied qui roule du talon à la pointe et oscille de côté compris) est de 0,05 à 0,3 m/s : il vient des clips eux-mêmes, pas du réglage de la cadence.

**Vérifications faites (Windows)**

- **307** tests unitaires (295 avant), en Release et en Debug : `test_blending` (fichier de description et ses erreurs ; masque en rampe, os inconnu refusé ; fondu tick par tick et dessin entre deux ticks ; fondu interrompu, poids qui somment à 1, quatre mouvements au plus ; pas de relance ; blend space : voisins, poids, phase décalée, cycle de durée pondérée, vitesse interpolée entre deux ticks, course accélérée ; clip accordé à la vitesse ; attaque du haut du corps, jambes qui marchent, effacement tout seul ; un grand pas égal à des petits ; quaternions opposés ; vitesses au sol et phases de `kaykit.json` contre les clips ; dérive du pied de la marche à la course). Les tests de l'`Animator` de la partie 4 passent (seul l'accès à la vitesse a changé).
- Captures de référence **inchangées** : démo 2D `bd91e8b2c616`, tranche `dbf96994a52d`, démo 3D `fdc076d9c3ae` et `952477744ffb`.
- Scène « Animation » en Debug (couche de validation D3D12) : aucun message. En Release : 1,2 ms de CPU par image avec le chevalier des mélanges, 164 images par seconde.
- Rechargement à chaud de `kaykit.json` pendant la scène : « animation set 'animations/kaykit.json' reloaded ».
- **Vérifié à la main** (2026-09-25, par l'utilisateur, Windows) : du repos à la course au curseur, fluide ; l'attaque en marchant ; les fondus.

### Validation

- [x] Du repos à la course en passant par la marche, en faisant varier la vitesse à la main (curseur dans la scène de test) : pas d'à-coup, pieds qui ne glissent pas (à l'œil, et la vitesse au sol mesurée affichée). *Dérive mesurée (test, et affichée dans la scène) ; vérifié à l'œil sous Windows.*
- [x] Une attaque en marchant : haut du corps attaque, jambes marchent. *Tests, et vérifié à l'œil sous Windows.*
- [x] Tests unitaires : poids d'un fondu tick par tick, fondu interrompu, voisins et poids du blend space, phases synchronisées.

---

## 7. Événements d'animation

### But

Déclencher du gameplay et du son **au bon moment d'un clip** : le pied touche le sol, le coup porte, la créature tombe. C'est la ligne de la roadmap : réutiliser les événements de l'`AnimationPlayer` (exactement une fois).

### Tâches

- [x] Des événements par clip, à un **temps** (en secondes dans le fichier de description, convertis en ticks au chargement) avec un nom : `step_left`, `step_right`, `impact`, `death_ground`. *Convertis quand l'`Animator` lance le clip.*
- [x] Le système d'animation les **collecte** à chaque tick, avec l'entité et le clip, dans une liste que le jeu lit dans son `update()` (comme `advance(…, &fired)` au jalon 2).
- [x] Règles avec les **mélanges** : les événements d'un clip dont le poids est faible ne partent pas (voir les questions).
- [x] Dans la tranche : les **pas** à l'événement de pas (plus de compteur de distance), les **dégâts** à l'événement d'impact. *Fait à la partie 11.*
- [x] Lire des événements dans les glTF ? Voir les questions. *Non : dans le fichier de description.*

### Questions à se poser

- **Où écrire les événements ?** glTF n'a pas d'événements. Options : dans le fichier de description JSON (à la main, avec le temps lu dans Blender), dans les `extras` du glTF (propriétés personnalisées de Blender, à exporter), ou par les **marqueurs** de la timeline de Blender (non exportés en glTF *(à vérifier)*). Recommandation : **dans le fichier de description JSON**, rechargé à chaud au jalon 7 ; les `extras` si un jour un script Blender les écrit.
- **Événements pendant un fondu** : si la marche s'estompe pendant que la course monte, qui joue les pas ? Recommandation : un événement part si son clip a un poids **≥ 0,5** au moment où il est franchi (dans un blend space synchronisé, le clip dominant) ; chaque clip peut le surcharger (un « impact » part quel que soit le poids, si le clip vient d'être lancé).
- **Événements sautés** : une vitesse ×3 ou un grand pas ne doivent pas en perdre (garantie déjà tenue au jalon 2). Un fondu qui démarre un clip au milieu ne rejoue pas les événements d'avant.

### Pièges connus

- Un événement exactement au temps 0 d'un clip qui boucle : partir **une fois par boucle**, ni zéro ni deux (le cas est testé au jalon 2 pour les sprites, à reprendre).
- Un clip interrompu juste avant son « impact » : pas de dégâts, ce qui est voulu (l'attaque a été annulée) ; le jeu ne doit pas les appliquer au clic « par sécurité ».
- Les événements lus **pendant le dessin** : jamais. Ils partent au tick, sinon ils dépendent de la cadence d'affichage.

### Validation

### Implémentation réalisée (partie 7)

Fichiers : `animation_set.hpp` / `.cpp` (`ClipEvent`, lecture), `animator.hpp` / `.cpp` (marques, `AnimatorEvent`, `advance(ticks, &événements, entité)`, `advance_animators(registry, ticks, &événements)`) ; `assets/animations/kaykit.json` (les événements) ; `tests/test_blending.cpp` ; la scène « Animation » (sons et liste).

- **Fichier de description** : par clip, `"events": [{"time": secondes, "name": nom, "always": booléen}]`, triés par temps. Pas d'événements dans les glTF (ils n'en ont pas) ni dans Blender pour l'instant.
- **Marques** : quand l'`Animator` lance un clip, chaque événement devient une marque de son `ClipClock` (ticks arrondis ; en boucle, la fin est le début ; joué une fois, un événement peut tomber sur la toute fin). Dans un blend space, les événements de chaque clip sont placés dans le cycle commun (phase du clip retranchée). Un événement au-delà de la fin du clip est refusé, avec le nom du fichier, du clip et de l'événement. C'est l'algorithme du jalon 2 : **exactement une fois**, quels que soient la vitesse et la taille des pas ; `seek` (outils) ne déclenche pas ce qu'il saute.
- **Au tick, jamais au dessin** : `advance_animators(registry, 1, &événements)` ajoute des `AnimatorEvent` (entité, nom, clip, mouvement, couche, poids), entité par entité dans l'ordre du registre, que le jeu lit dans son `update()`.
- **Règle des mélanges** : un événement part si le poids de son clip, à la fin du tick, vaut **au moins la moitié** (pour la couche du haut : poids du clip × poids de la couche) ; dans un blend space, seul le clip **qui pèse le plus** a ses événements (à égalité, le plus lent). Un événement `"always"` part quel que soit le poids, tant que le clip joue (l'impact d'une attaque encore en fondu). Un clip qui commence en fondu ne joue pas son événement du temps 0 s'il pèse moins de la moitié à ce tick : voulu (un pas de plus au milieu d'une transition sonnerait faux).
- **Temps des KayKit** (`kaykit.json`) : les **pas** là où chaque pied se pose (`measure_stride`), pour `Walking_A` / `_B` et `Running_A` / `_B` ; l'**impact** des quatre attaques à une main juste après le moment où la main va le plus vite (0,60 s pour `Chop`, 0,40 pour la diagonale et l'estoc, 0,27 pour l'horizontale), en `always` ; `death_ground` quand les hanches touchent le sol (`Death_A` 0,53 s, `Death_B` 1,67 s).
- **Scène « Animation »** : le chevalier des mélanges joue ses **pas** et ses **coups** sur les événements (sons de la tranche, placés à sa position, écoutés depuis la caméra), et le panneau liste les huit derniers (tick, nom, clip, couche, poids).

**Vérifications faites (Windows)**

- **313** tests unitaires (307 avant), en Release et en Debug : événements lus et triés, erreurs ; exactement une fois en boucle, au temps 0 et à la fin d'une boucle, d'un grand pas ou tick par tick, à ×3 ; `seek` qui ne déclenche rien ; fin d'un clip joué une fois ; fondu (clip sous la moitié muet, début d'un clip en fondu muet, `always` à 375) ; blend space (seulement la marche à 50/50, seulement la course à 75 %, au bon tick) ; couche du haut, entités et poids dans `advance_animators` ; événement au-delà de la fin refusé ; chaque clip KayKit lançable avec ses événements, et les pas de `Walking_A` et `Running_A` à 2 % près de l'instant où le pied se pose.
- Captures de référence **inchangées** : démo 2D `bd91e8b2c616`, tranche `dbf96994a52d`, démo 3D `fdc076d9c3ae` et `952477744ffb`.
- Scène « Animation » en Debug (couche de validation D3D12) : aucun message ; en marchant, un pas tous les 32 ticks, gauche et droite en alternance.
- **Vérifié à la main** (2026-09-25, par l'utilisateur, Windows) : à l'oreille, les pas sur les pieds et les coups au bon moment, dans la scène « Animation ». Le rejeu de la tranche avec les pas viendra avec la partie 11 (le déterminisme est tenu par construction : entiers, et testé).

### Validation

- [x] Les pas du héros tombent sur ses pieds, à toutes les vitesses (à l'oreille et à l'œil) ; un rejeu donne les mêmes pas aux mêmes ticks. *Écouté dans la scène « Animation », puis dans la tranche avec le héros (partie 11) ; rejeu : capture de la tranche stable, et les mêmes événements aux mêmes ticks sont testés (grand pas, vitesse).*
- [x] Tests unitaires : événements exactement une fois en boucle, avec vitesse, dans un fondu (règle du poids), au temps 0.

---

## 8. Attaches aux os

### But

Mettre un objet dans la main d'un personnage (épée, bouclier, torche) ou sur un point de son corps (effets, barre de vie au-dessus de la tête), et qu'il suive l'animation sans retard.

### Tâches

- [x] Un composant « attaché à un os » : l'entité propriétaire, l'**os** (par nom, résolu en indice au chargement), un décalage (`Transform`). Il s'ajoute au `Parent` du jalon 4, qui prévoyait cette extension. *`BoneAttachment` (le point ou l'os, par nom, résolu au dessin).*
- [x] Dans `World::collect()`, la matrice monde d'une entité attachée est : matrice monde du propriétaire × matrice modèle de l'os (**pose interpolée** de l'image) × décalage.
- [x] Des **points d'attache** nommés dans le fichier de description (« main droite » = os `hand.R` + décalage), pour que le jeu ne connaisse pas les noms d'os d'un modèle.
- [x] L'épée des modèles KayKit (ou autre) comme objet attaché ; la torche d'un personnage qui éclaire (une `LightSource` attachée).
- [x] Les barres de vie et les noms au-dessus de la tête suivent l'os de la tête (ou restent au-dessus de la boîte : à décider en voyant le résultat). *Décidé en voyant les deux repères (2026-09-25) : **au-dessus de la boîte**, fixes ; l'os de la tête reste pour les effets.*

### Questions à se poser

- **L'attache est-elle calculée au tick ou au dessin ?** Au **dessin**, avec la pose interpolée : sinon l'épée a un tick de retard sur la main. La logique ne lit pas la position de l'épée (voir la partie 4).
- **Ordre de calcul** : les poses de tous les personnages doivent être calculées **avant** la collecte des entités attachées. Recommandation : une étape « poses » au début de la collecte, puis la collecte.
- **Un objet attaché qui projette une ombre ou émet de la lumière** : il passe par les mêmes chemins qu'une entité ordinaire (c'est tout l'intérêt de le laisser être une entité).

### Pièges connus

- L'objet attaché à l'**os** au lieu du **point de prise** : l'épée traverse le poignet. Le décalage (et une rotation) est presque toujours nécessaire.
- L'échelle de l'os (0,01 d'un export FBX) appliquée à l'épée : l'arme devient minuscule. Décider si l'attache hérite de l'échelle de l'os (recommandation : **non**, seulement position et rotation).
- Un objet attaché à un personnage hors de la vue, lui-même visible (une longue lance) : le personnage n'a pas calculé sa pose. Calculer la pose si un de ses enfants est visible, ou agrandir la boîte du personnage.

### Validation

### Implémentation réalisée (partie 8)

Fichiers : `world.hpp` / `.cpp` (`BoneAttachment`, `attach_point_matrix`, poses à la demande), `animator.hpp` (`AnimationPose::collection`), `animation_set.hpp` / `.cpp` (`AttachPoint`), `debug_tools.cpp` (inspecteur) ; `assets/animations/kaykit.json` (points) ; `tests/test_attachment.cpp` (nouveau) ; la scène « Animation » (épée, torche, repères).

- **`BoneAttachment { point }`**, avec un `Parent` animé : le `Transform` de l'entité est relatif au point, qui est un **point d'attache** du fichier de description du parent (`"attach_points": {"right_hand": {"joint": "handslot.r", "position": [...], "rotation": [degrés X, Y, Z]}}`) ou, à défaut, un nom d'os. Le jeu dit « right_hand », jamais un nom d'os. Point ou parent introuvables : l'objet reste à l'origine du parent (visible, pas perdu). Résolu à chaque image par nom (quelques recherches par objet attaché ; la partie 10 mesurera).
- **Au dessin, sur la pose de l'image** : monde du parent × os (sans son échelle) × point × `Transform` de l'entité. La pose d'un personnage est calculée **une fois par collecte, à la première demande** (par son modèle ou par un objet sur un de ses os), donc dans n'importe quel ordre, et **même hors de la vue** quand un objet attaché la demande (la longue lance). L'objet suit donc la main sans retard ; la logique ne le lit pas (`world_matrix()` donne la dernière pose dessinée, pour les outils).
- **Échelle** : celle de l'**os** n'est pas transmise (position et rotation seulement) ; celle de l'**entité parent** l'est, comme pour tout `Parent` (l'épée KayKit, dans les unités du chevalier, suit sa mise à l'échelle).
- **Tout passe par les chemins ordinaires** : un modèle, un maillage, une `LightSource` ou un billboard attachés sont collectés comme les autres (ombres, lumières, `Hidden`, destruction avec le parent).
- **Points KayKit** (`kaykit.json`) : `right_hand` (os `handslot.r`, 3,3 cm plus haut, tourné de 180° autour de Y : exactement le placement de l'épée dans le fichier du chevalier), `left_hand`, `shield` (le placement du bouclier rond), `head`.
- **Scène « Animation »** : le chevalier des mélanges tient **`sword_1handed.gltf`** (un fichier à part) sur `right_hand`, à la place de l'épée de son fichier (case « Épée attachée » : décochée, celle du fichier, qui doit se confondre), et une **torche** sur `left_hand` à la place du bouclier (un bâton, une flamme émissive et une lumière ponctuelle avec ombres, enfants l'un de l'autre). « Repères de tête » : un repère **jaune sur l'os de la tête** (point `head`, 1,5 m au-dessus : le casque est grand, l'os est au cou) et un **blanc au-dessus de la boîte**, fixe, pour choisir où mettre les barres de vie.
- **Inspecteur** : le point d'un `BoneAttachment` se lit et se change.

**Vérifications faites (Windows)**

- **319** tests unitaires (313 avant), en Release et en Debug : points lus et erreurs ; objet sur l'os de la pose **entre deux ticks**, décalage de l'entité en plus ; point nommé (décalage et rotation) ; point inconnu à l'origine du parent ; échelle de l'os **non transmise**, celle du parent oui ; pose calculée pour un parent **hors de la vue**, lumière attachée qui suit ; destruction du parent qui emporte ce qu'il tient ; `world_matrix()` sur la dernière pose ; l'épée sur `right_hand` tombe sur **l'épée du fichier du chevalier à 1 mm près** (ozz), en `Idle`, `Chop` et `Running_A`.
- Captures de référence **inchangées** : démo 2D `bd91e8b2c616`, tranche `dbf96994a52d`, démo 3D `fdc076d9c3ae` et `952477744ffb`.
- Scène « Animation » en Debug (couche de validation D3D12) : aucun message ; en Release, 1,5 ms de CPU par image (264 appels de dessin avec la torche et ses ombres). Capture : l'épée dans la main, la lumière de la torche sur le visage.
- **Vérifié à la main** (2026-09-25, par l'utilisateur, Windows) : l'épée dans la main pendant la marche, la course et l'attaque ; la torche qui suit la main et éclaire ; barres de vie au-dessus de la boîte.

### Validation

- [x] L'épée reste dans la main pendant la marche, la course et l'attaque, à toutes les cadences d'affichage. *Testé contre l'épée du fichier, et vérifié à l'œil sous Windows.*
- [x] Une torche attachée éclaire et suit la main. *Vérifié à l'œil sous Windows.*

---

## 9. Outils de debug et scène de test

### But

Voir ce que fait l'animation : le squelette, le clip joué, les poids, les événements. Et un endroit pour essayer un personnage sans passer par la tranche.

### Tâches

- [x] Dessin du **squelette** en lignes de debug (`DebugLineRenderer` du jalon 3) : un segment de chaque os à son parent, les axes des os choisis, par-dessus le maillage ou seul. *`draw_skeleton()` (`animation_debug.hpp`).*
- [x] Inspecteur (jalon 4) : le composant d'animation (clips, temps en ticks et en secondes, vitesse, poids de chaque couche), et les attaches.
- [x] Une fenêtre **Animation** dans DEBUG : le personnage choisi, une ligne de temps par clip actif avec ses événements, le dernier événement parti et son tick.
- [x] Une scène de test « **Animation** » dans DEBUG > Tests moteur (et `--animation` en ligne de commande) : les personnages de test sur une plateforme, la liste de leurs clips, lecture / pause / image par image, vitesse, curseur de la vitesse de déplacement (blend space), bouton « attaque » (masque), fondu réglable, squelette et poids en vue de debug, pose de liaison.
- [x] `--anim-compare` (dans l'esprit de `--blender-compare`) : un personnage à un temps donné, capturé, et le script Blender qui rend la même pose.
- [x] Mettre à jour `TEST_MAC.md` avec une section « Jalon 5 ». *Tenue à jour partie par partie.*
- [x] *(Reporté de la partie 5.)* Vue de debug des **poids** d'un os choisi (`MeshView::Weights`).

### Pièges connus

- Les lignes du squelette dessinées avec la pose **du tick** et le maillage avec la pose **interpolée** : le squelette « traîne ». Une seule pose par image, pour tout.

### Validation

### Implémentation réalisée (partie 9)

Fichiers : `animation_debug.hpp` / `.cpp` (nouveaux), `skeleton.hpp` / `.cpp` (`PoseSampler::bind`), `world.hpp` / `.cpp` (`CollectOptions::bind_pose`), `animator.hpp` / `.cpp` (`ticks`, `last_event`), `mesh_renderer.hpp` / `.cpp` (`MeshView::Weights`, `set_weight_joint`), `shaders/mesh.vert.hlsl` et `mesh.frag.hlsl` (et leurs MSL), `debug_tools.hpp` / `.cpp` (fenêtre Animation, inspecteur) ; dans le bac à sable, `anim_compare.hpp` / `.cpp` (nouveaux), `animation_test.*` et `main.cpp` (`--animation`, `--anim-compare`, `--view weights`) ; `tools/blender/compare_pose.py` et `compare_pose_images.py` (nouveaux) ; `tests/test_animation_debug.cpp` (nouveau).

- **`draw_skeleton(lignes, monde, squelette, pose, options)`** : un segment de chaque os à son parent, une sphère par os, les **axes** des os (tous, ou l'os mis en valeur), par-dessus les maillages ou non. Il prend la pose que le monde a dessinée à l'image (`AnimationPose`) : le squelette ne traîne jamais derrière le maillage (le piège de la section).
- **Pose de liaison** : `PoseSampler::bind` place les os des skins à l'inverse de leurs matrices de liaison (toutes les palettes deviennent l'identité : les maillages tels que modelés), `CollectOptions::bind_pose` la demande pour toute une collecte.
- **Vue des poids** (`MeshView::Weights`, reportée de la partie 5) : le vertex shader skinné calcule le poids de l'entrée de palette choisie (`set_weight_joint`, une constante de plus dans le bloc de la passe) et le passe au fragment shader, qui le montre du bleu (0) au rouge (1) par le vert ; les maillages sans skin sont gris foncé. Une seule entrée pour toute l'image : faite pour un personnage, ou pour des personnages qui partagent la disposition de leur skin (les KayKit). Menu Débogage > Vue, et `--view weights`.
- **`Animator`** : ses **ticks** et son **dernier événement** (nom, clip, tick), gardés même quand le jeu ne ramasse pas les événements, pour la fenêtre de debug.
- **Fenêtre DEBUG > Animation** : l'entité animée choisie (celle de l'inspecteur si elle est animée, sinon une liste), son tick, sa vitesse et sa vitesse de déplacement, son dernier événement et depuis combien de ticks ; par couche, une **ligne de temps** par mouvement (nom, poids, temps en ticks et en secondes, ou phase et cadence d'un mélange), avec ses **événements** marqués en jaune (leur liste au survol). Ouverte ou non d'une exécution à l'autre, comme les autres (`imgui.ini`).
- **Inspecteur** : le temps des clips aussi en secondes ; l'attache (`BoneAttachment`) se lit et se change depuis la partie 8.
- **Scène « Animation »** (`--animation`, ou `--menu-test 10`) : en plus des parties 3 à 8, **Tick suivant** (en pause, un tick à la vitesse choisie, événements compris ; la pause vaut aussi pour le chevalier des mélanges), **Axes des os**, **Pose de liaison**, un **os** à choisir sur le personnage sélectionné (mis en valeur, avec ses axes) et la **vue des poids** de cet os.
- **`--anim-compare [clip secondes]`** : le chevalier seul, dans ce clip à ce moment (`seek`, vitesse 0), en **couleur de base** (sans lumière : seules comptent la pose et les textures), fond vert, capturé à la 10e image avec un JSON à côté (modèle, parties cachées, clip, moment, caméra). `tools/blender/compare_pose.py` pose **le même fichier** dans Blender avec son propre import et sa propre animation, rend avec Workbench (couleurs des textures, plates, vue « Standard », fond transparent) ; `compare_pose_images.py` compare les **silhouettes** (intersection sur union, pixels d'un seul côté, image de superposition) et les couleurs moyennes.

**Comparaison avec Blender 5.2** (1280 × 720, MSAA 4×) :

| Clip, moment | Recouvrement des silhouettes | Pixels du moteur seul | Pixels de Blender seul | Couleur moyenne (moteur / Blender) |
|---|---|---|---|---|
| `1H_Melee_Attack_Chop`, 0,4 s | 0,9920 | 844 | 0 | (136, 133, 132) / (136, 132, 132) |
| `Running_A`, 0,3 s | 0,9934 | 692 | 0 | (138, 140, 140) / (138, 139, 140) |
| `Idle`, 0,5 s | 0,9928 | 760 | 0 | (137, 135, 136) / (137, 135, 136) |
| `Death_A`, 0,5 s | 0,9911 | 569 | 0 | (123, 109, 112) / (123, 109, 112) |

Les pixels du moteur seul forment une ligne d'un pixel sur le bord de la silhouette (l'anticrénelage du moteur mélange le bord au vert ; Blender compte ce pixel comme transparent) : **la pose est la même au pixel près**. Premier essai sur fond gris : faux écart, l'armure grise se confondait avec le fond ; d'où le vert.

**Vérifications faites (Windows)**

- **322** tests unitaires (319 avant), en Release et en Debug : squelette en lignes (segments, sphères, axes de tous les os, os mis en valeur) ; pose de liaison : palettes identité (`PoseSampler::bind`, et le monde avec `bind_pose` pendant que `Bend` joue, puis de nouveau pliée sans) ; ticks et dernier événement de l'`Animator` sans liste d'événements.
- Captures de référence **inchangées** : démo 2D `bd91e8b2c616`, tranche `dbf96994a52d`, démo 3D `fdc076d9c3ae` et `952477744ffb`.
- En Debug (couche de validation D3D12) : scène « Animation » éclairée et en vue des poids, `--anim-compare` : aucun message. MSL régénérés (`mesh.vert`, `mesh_skinned.vert`, `mesh.frag`).
- Capture de la vue des poids : maillages skinnés en bleu sans os choisi, parties rigides grises ; avec un os, le dégradé vers le vert là où il partage les sommets.
- **Vérifié à la main** (2026-09-25, par l'utilisateur, Windows) : la fenêtre DEBUG > Animation (lignes de temps, événements), Tick suivant, la pose de liaison, les axes, la vue des poids en choisissant des os.

### Validation

- [x] Dans la scène de test, chaque clip de chaque personnage se joue, s'arrête, avance image par image ; le squelette est dessiné à sa place exacte dans le maillage. *Squelette tiré de la pose même du dessin ; vérifié à l'œil sous Windows.*
- [x] La pose comparée à Blender au même temps : écart noté (comme la comparaison des matériaux du jalon 3). *Silhouettes confondues à 0,991 – 0,993 (un pixel de bord), couleurs à 1/255 près, sur quatre poses.*

---

## 10. Performances

### But

Savoir **combien de personnages animés** tiennent dans le budget, sur la machine de développement et sur la machine visée (Mac M3, 60 images par seconde), et où part le temps.

### Tâches

- [x] Un test de charge `--skinned N` (dans la scène de test ou la démo 3D) : N personnages animés, chacun à une phase différente, sur plusieurs clips et fondus.
- [x] Mesurer par image : temps CPU d'échantillonnage et de mélange, calcul des palettes, envoi du tampon de palettes ; temps GPU des passes (`--gpu-timing`) avec et sans ombres.
- [x] Culling avant échantillonnage (un personnage hors de la vue ne calcule pas sa pose) et vérifier que ça compte.
- [x] Si le CPU ne suffit pas : répartir l'échantillonnage sur plusieurs fils (chaque personnage est indépendant), ou réduire la fréquence d'échantillonnage des personnages lointains. Seulement si les mesures le demandent. *Les mesures ne le demandent pas : voir plus bas.*
- [x] Noter les chiffres ici, et les comparer à ceux de la partie 9 du jalon 4 (1,36 ms de CPU par image pour la démo 3D).

### Questions à se poser

- **Combien de personnages faut-il tenir ?** Un ARPG a des « vagues » de monstres : 50 à 150 à l'écran dans les moments chargés, parfois plus. Recommandation : viser **200 personnages animés** de 30 à 60 os à 60 images par seconde sur le Mac M3, avec de la marge pour la logique (IA, chemins) ; noter le plafond mesuré.
- **Plusieurs fils pour l'animation ?** Le jalon 4 disait : la logique sur un seul fil, le multithreading là où c'est isolé. L'échantillonnage des poses est **isolé** (chaque personnage lit ses clips et écrit sa palette) et ne touche pas à la logique. C'est le premier bon candidat. Recommandation : **mesurer d'abord** ; s'il le faut, une simple répartition par tranches sur quelques fils (sans système de tâches complet), et le résultat doit être identique à un seul fil.
- **Compression des clips** : ozz compresse ses clips (tolérance d'erreur réglable). À régler selon la mémoire mesurée : quelques Mo pour une centaine de clips, sans doute pas un sujet.

### Pièges connus

- Mesurer en Debug : ozz et GLM sans optimisation sont des dizaines de fois plus lents. Mesurer en Release, comme au jalon 3.
- Des personnages tous à la même phase : la mémoire cache aide trop, le chiffre est optimiste. Varier clips et phases.
- Un tampon de palettes recréé à chaque image : le réutiliser et le remplir (comme les tampons d'instances du jalon 3).

### Validation

### Implémentation réalisée (partie 10)

Fichiers : `world.hpp` / `.cpp` (`AnimationStats`, `World::animation_stats()`), dans le bac à sable `animation_test.*` (la foule) et `main.cpp` (`--skinned N`, `--skinned-zoom F`, `--no-shadows`) ; `tests/test_animation_debug.cpp`.

- **Mesures dans le moteur** : `World::collect()` compte, à chaque collecte, les modèles animés rencontrés, les poses échantillonnées (celles hors de la vue ne le sont pas), les palettes et leurs matrices, et le temps de l'échantillonnage (clips, mélange, espace modèle) et des palettes (produit par les matrices de liaison, remise au renderer). Quelques appels à l'horloge par personnage.
- **Test de charge** `--skinned N` (la scène « Animation » en mode foule, seule) : N personnages KayKit (chevalier, guerrier, sbire, à tour de rôle) sur une grille, vus comme dans le jeu (50° de plongée), chacun avec son mouvement (blend space `locomotion` à une vitesse tirée au sort, `Idle`, `Running_A` accordé, `Walking_B`, attaque, touché, mort), sa phase et sa vitesse ; à **chaque tick**, un soixantième d'entre eux change de mouvement en fondu (12 ticks) ou attaque du haut du corps. Tirages d'un générateur à graine fixe : le même essai à chaque lancement. `--report` ajoute les chiffres de l'animation au rapport ; `--no-shadows` retire les ombres du soleil ; `--skinned-zoom F` rapproche la caméra (une partie de la foule hors de la vue). Le panneau de la scène montre les mêmes chiffres en direct. Les sons des événements sont coupés en mode foule.
- **Tampon de palettes** : il était déjà réutilisé et ne grandit que par doublement (partie 5) : le piège de la section est évité.

**Mesures** (Windows, Release, 1920 × 1080, `--skinned N --report --gpu-timing`, 8 s). « Enregistrement » est le temps CPU de préparation de l'image, là où l'animation se fait ; le temps CPU total de l'image comprend l'attente de l'écran (vsync à 165 Hz), d'où environ 6 ms jusqu'à 400 personnages.

| Personnages | Enregistrement | dont échantillonnage | dont palettes | Image (CPU, moyenne / p99) | GPU (scène + ombre) |
|---|---|---|---|---|---|
| 50 | 0,49 ms | 0,24 | 0,12 | vsync | 1,0 ms |
| 100 | 0,59 ms | 0,28 | 0,15 | vsync | 1,0 ms |
| 200 | 1,06 ms | 0,53 | 0,26 | vsync | 1,3 ms |
| 400 | 1,70 ms | 0,89 | 0,39 | vsync | 1,7 ms |
| 800 | 3,35 ms | 1,88 | 0,73 | 7,6 / 8,8 ms | 2,4 ms |
| 1 600 | 7,13 ms | 4,25 | 1,41 | 13,7 / 15,5 ms | 3,5 ms |
| 3 200 | 15,9 ms | 10,4 | 2,78 | 28,5 / 32,3 ms | 7,0 ms |

- **Plafond à 60 images par seconde sur cette machine : environ 1 600 personnages animés** (13,7 ms par image en moyenne, 15,5 au 99e centile) ; à 3 200, 35 images par seconde. C'est le **CPU** qui limite (échantillonnage surtout), pas le GPU.
- **Coût par personnage** : environ 2,6 µs d'échantillonnage et de mélange (41 os, un ou deux clips en moyenne) et 0,9 µs de palette. La **cible de 200 personnages coûte environ 1 ms** d'enregistrement.
- **Ombres du soleil** : à 800, 2,4 ms de GPU avec, 2,0 ms sans (la passe d'ombre coûte 0,85 ms) ; le CPU ne change presque pas (l'ombre réutilise les palettes de la scène).
- **Culling avant échantillonnage** : 800 personnages, caméra 3 fois plus près (`--skinned-zoom 3`) : **400 poses** échantillonnées au lieu de 800, enregistrement **1,59 ms au lieu de 3,35**. Le rejet grossier (boîte de repos doublée, partie 4) garde large autour de la vue ; à resserrer si un jour il le faut.
- **Plusieurs fils** : **non**. À la cible, l'animation prend 1 ms sur les 16,7 ; le fil principal garde toute sa marge pour la logique. La répartition par tranches reste la piste si une scène la demande un jour (chaque pose est indépendante).
- **Mémoire des clips** : 95 clips KayKit construits en 1,3 Mo (mesure de la partie 3) : la compression d'ozz n'est pas un sujet.
- **Démo 3D sans personnage animé** : jalon 4 (commit `953988f`, recompilé à côté) **1,187 ms** de CPU par image, aujourd'hui **1,196 ms** (sans vsync, graine 42, 8 s, trois lancements alternés ; enregistrement 0,413 → 0,423 ms) : inchangée. (Le 1,36 ms du jalon 4 venait d'un autre jour : comparer côte à côte, comme le disait le jalon 4.)

**Vérifications faites (Windows)**

- **323** tests unitaires (322 avant), en Release et en Debug : statistiques d'une collecte (deux personnages, un hors de la vue : deux rencontrés, une pose).
- Captures de référence **inchangées** : démo 2D `bd91e8b2c616`, tranche `dbf96994a52d`, démo 3D `fdc076d9c3ae` et `952477744ffb`.
- En Debug (couche de validation D3D12) : `--skinned 100`, aucun message.

### Validation

- [x] Le plafond de personnages animés à 60 images par seconde est mesuré et noté, sous Windows (et sur Mac dans TEST_MAC.md). *Environ 1 600 sous Windows ; le Mac est à mesurer (TEST_MAC.md).*
- [x] La démo 3D sans personnage animé garde ses performances du jalon 4. *1,196 contre 1,187 ms, mesurés côte à côte.*

---

## 11. Personnages animés dans la tranche jouable

### But

Réunir le jalon dans la tranche jouable : la preuve que l'animation tient dans un vrai jeu.

### Contenu

- **Héros** : un personnage animé de test à la place du corps en primitives ; repos, marche et course selon sa vitesse (blend space, vitesse accordée), attaque au clic sur une créature ou à la compétence 1, avec l'épée attachée à la main ; les **pas** et l'**impact** aux événements.
- **Créatures** : un second personnage (squelette, monstre) : repos, marche, **touché** (mélange partiel ou additif, selon ce qui est fait), **mort** (clip `Once`, la créature reste au sol).
- Les **barres de vie** et les « Touché ! » suivent les têtes. *Révisé à la partie 8 (choix de l'utilisateur) : **au-dessus de la boîte**, fixes.*
- La **pause** fige les animations (le temps est au tick : rien à faire de plus, à vérifier).

### Tâches

- [x] Assembler, en gardant la démo 3D du jalon 3 (sans héros) et ses captures inchangées.
- [x] Une **capture de référence** avec le rejeu de la tranche (`tests/data/slice_replay.json`, mis à jour si les coups ne portent plus au même tick), stable sur deux lancements. *Rejeu inchangé ; nouvelle capture `fe8d57ceb2cc`.*
- [x] Mesurer : chargement (les personnages et leurs clips), mémoire (squelettes, clips, palettes), CPU et GPU par image, et comparer au jalon 4.
- [x] Mettre à jour `FICHIERS_DU_PROJET.md`, `credits.json`, `TEST_MAC.md` (section « Jalon 5 »). *`credits.json` : rien de nouveau (les packs KayKit y sont depuis la partie 2, l'épée fait partie de celui des aventuriers).*

### Validation

### Implémentation réalisée (partie 11)

Fichiers : dans le bac à sable, `demo3d.hpp` / `.cpp` et `states_demo.cpp` (chargement).

- **Seulement dans la tranche** (`Options::hero`), et seulement si les personnages sont là (`fetch_test_characters.py` ; sinon, les corps en primitives du jalon 4 et un message) : la démo 3D du jalon 3 ne change pas (captures `fdc076d9c3ae` et `952477744ffb` identiques).
- **Héros** : le chevalier KayKit à 1,80 m, son **épée** (`sword_1handed.gltf`, un fichier à part) sur le point `right_hand`, l'épée de son fichier et les armes en trop cachées, le bouclier gardé. Il joue `locomotion` à sa **vitesse réelle** (le déplacement du tick, divisé par son échelle : mètres du fichier), et se tourne vers où il va. Ses **pas** sonnent sur les événements `step_left` / `step_right` (plus de compteur de distance).
- **Coup** : le clic sur une créature à portée ou la compétence 1 lance l'attaque `1H_Melee_Attack_Chop` sur la **couche du haut** (les jambes continuent de marcher) ; un coup en cours n'est pas relancé. Les **dégâts** tombent à l'événement **`impact`** (0,6 s plus tard), sur la créature visée si elle est encore à portée, sinon sur la plus proche à portée : une créature qui entre dans le geste est touchée, une qui en sort ne l'est pas. Le héros se tourne vers sa cible.
- **Créatures** : 300 squelettes KayKit (guerriers et sbires en alternance) à 1,50 m, `locomotion` à leur vitesse, tournés vers où ils vont ; **touché** : `Hit_A` sur la couche du haut (mélange partiel) ; **mort** : `Death_A` joué une fois, la créature reste au sol. Leurs propres pas ne sonnent pas (il y en a des centaines).
- **Barres de vie et « Touché ! »** : au-dessus de la **boîte** de chaque personnage (sa taille, composant `Stature` de la démo), fixes, comme décidé à la partie 8 ; les créatures du jalon 3 gardent leur hauteur de 1,6 m (captures inchangées).
- **Culling** : la démo donne maintenant le frustum de la caméra à la collecte : les personnages hors de la vue ne calculent pas leur pose.
- **Figé** (`--freeze-after`, les captures) : les animations avancent de **0 tick** à chaque tick figé ; le tick précédent devient le présent, et chaque image figée montre la même pose, quelle que soit sa place entre deux ticks (sans cela, deux lancements donnaient deux images).
- **Chargement** : l'écran de chargement de la tranche charge d'avance, en une étape « Personnages animés », les trois modèles, leurs squelettes et leurs clips, l'épée et la description `kaykit.json`.

**Capture de référence** : `--states --replay-input tests/data/slice_replay.json --freeze-after 340 --run-seconds 9 --pixel-size 1280 720 --capture` → **`fe8d57ceb2cc`**, deux lancements sur deux (jalon 4 : `dbf96994a52d`). Le rejeu n'a pas changé ; il donne maintenant **2 coups** au lieu de 11 : un coup dure toute l'attaque (environ une seconde) et les appuis pendant ce temps ne relancent rien (voulu, partie 7), et les dégâts attendent l'impact. L'image montre le héros bouclier au bras, un sbire dont la barre a baissé, les barres au-dessus des boîtes.

**Mesures** (Windows, Release, rejeu de la tranche, 1280 × 720, sans vsync, 9 s, deux lancements ; jalon 4 : la tranche avant cette partie, mesurée de la même façon le même jour) :

| | Jalon 4 | Jalon 5 | Écart |
|---|---|---|---|
| CPU par image | 1,01 ms | 1,10 ms | +0,09 ms |
| dont logique (update) | 0,047 | 0,088 | +0,04 (300 animateurs, orientations) |
| dont enregistrement | 0,37 | 0,33 | −0,04 (personnages hors de la vue sautés ; plus de corps en deux entités) |
| dont envoi | 0,60 | 0,69 | +0,09 (skinning, palettes) |
| GPU par image (`--gpu-timing`) | | 0,90 ms | |
| Chargement | 430 ms | 770 ms | +340 ms (trois personnages, leurs clips, l'épée) |
| Assets | 14,9 Mo | 40,8 Mo | modèles 17,8 Mo sur le GPU, clips 3,4 Mo, squelettes 0,02 Mo |
| Processus | 160 Mo | 189 Mo | |

Palettes : une vingtaine de personnages visibles à la fois, 41 matrices de 48 octets chacun, soit environ 40 Kio par image. Tout reste loin du budget de 16,7 ms.

**Vérifications faites (Windows)**

- **323** tests unitaires en Release et en Debug (inchangés : la partie est du code de jeu).
- Captures : démo 3D `fdc076d9c3ae` et `952477744ffb`, démo 2D `bd91e8b2c616` **inchangées** ; tranche `fe8d57ceb2cc` stable sur deux lancements.
- En Debug (couche de validation D3D12) : le rejeu (titre, jeu, pause, reprise), puis trois cycles titre → jeu → titre (`--states-cycles 3`) : aucun message ; « 0 pauses where the world moved » (la pause fige aussi les animations) ; mémoire stable d'un cycle à l'autre (288,1 Mo, +0,0).
- **Vérifié à la main** (par l'utilisateur, Windows) : la tranche jouée au clavier et à la souris, du titre au retour au titre (pas au bon moment, coups et touchés, morts, épée, barres, pause).

### Validation

- [x] La tranche se joue du titre au retour au titre avec un héros et des créatures animés, sans message de validation en Debug. *Sans message en Debug (rejeu et cycles) ; jouée à la main sous Windows.*
- [x] Sa capture de référence est stable ; les performances restent dans le budget, et l'écart avec le jalon 4 est noté. *`fe8d57ceb2cc` ; +0,09 ms de CPU par image, +340 ms de chargement.*

---

## 12. Critères de fin de jalon

Le jalon est terminé quand **tout** ce qui suit est vrai :

- [x] Squelettes, poids et clips se lisent depuis des glTF, passent par le **gestionnaire d'assets**, et se rechargent à chaud. *Parties 3 et 6 (et la description `kaykit.json`).*
- [x] Le **skinning** se fait sur le GPU, pour la scène et les ombres, avec instanciation et culling. *Partie 5.*
- [x] Les clips se jouent au **tick** (temps entier, vitesse exacte), l'image étant **interpolée** ; la logique ne dépend d'aucune pose. *Partie 4 ; la tranche ne lit que les événements et les positions des entités.*
- [x] **Fondus**, **blend space** de locomotion (pieds qui ne glissent pas) et **mélange partiel** fonctionnent. *Partie 6, vérifié à l'œil.*
- [x] Les **événements** d'animation partent exactement une fois et pilotent les pas et les coups de la tranche. *Parties 7 et 11.*
- [x] Une arme **attachée à un os** suit la main sans retard. *Partie 8, vérifié à l'œil.*
- [x] La scène de test « Animation », le squelette en lignes de debug et la fenêtre Animation sont dans le menu DEBUG. *Partie 9.*
- [x] Le plafond de personnages animés est mesuré et noté. *Partie 10 : environ 1 600 sous Windows.*
- [x] Aucun avertissement de compilation, aucun message de la couche de validation du GPU. *Sous Windows (D3D12) ; Metal dans TEST_MAC.md.*
- [x] La logique pure (chargement du squelette, temps, poids, événements, skinning CPU) a ses tests unitaires ; les captures de référence des jalons précédents sont inchangées. *323 tests ; démos 2D et 3D inchangées ; la tranche a sa nouvelle capture (ses personnages ont changé).*
- [x] Les nouvelles bibliothèques et les nouveaux assets sont dans `credits.json` (fenêtre « À propos »). *ozz-animation, packs KayKit, modèles Khronos (partie 2).*
- [x] Les décisions de la section [Décisions à consigner](#décisions-à-consigner) sont remplies.
- [x] La documentation des nouveaux fichiers est ajoutée à [FICHIERS_DU_PROJET.md](FICHIERS_DU_PROJET.md), et les vérifications Mac sont regroupées dans [TEST_MAC.md](TEST_MAC.md).

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
| Bibliothèque d'animation (maison, ozz-animation) | **ozz-animation 0.17.0** (MIT), par un **port vcpkg du projet** (`ports/`, *overlay*), sans ses outils ; squelettes et clips construits **au chargement** depuis cgltf ; temps, événements et choix des clips à nous | Recommandation validée par l'utilisateur (2026-09-25), préférence pour l'open source. Absent du registre vcpkg : un port garde une seule façon d'obtenir les dépendances. Un seul lecteur de glTF dans le moteur |
| Modèles animés de test et licences | **KayKit** *Adventurers* (chevalier) et *Skeletons* (guerrier, sbire), **CC0**, un seul squelette de 41 os ; modèles Khronos `SimpleSkin`, `InterpolationTest` (CC0), `RiggedFigure`, `Fox` (**CC-BY 4.0**, crédités) ; `fetch_test_characters.py`, hors de Git ; modèle de référence maison `skinned_reference.glb`, versionné | Recommandation validée ; tous les clips d'un ARPG (repos, marche, course, attaques, touché, mort), armes à part et os de main, héros et monstres qui partagent squelette et clips. Mixamo, BrainStem et CesiumMan écartés (licences) |
| Squelette partagé entre modèles, clips dans d'autres fichiers | Un **asset `Skeleton` par fichier** ; les clips en **`ClipLibrary` par fichier** (clé = le fichier, pas `fichier#clip`), jouables sur tout squelette de même structure (`mismatch` : même nombre d'os, mêmes noms, mêmes parents, dans le même ordre). Le squelette comprend les os des skins, les nœuds animés et leurs ancêtres | Une lecture par fichier au lieu d'une par clip (80 clips par KayKit) ; les KayKit partagent squelette et clips ; le nœud d'armature compte |
| Nombre d'os et d'influences par sommet | **256 os par skin** (indices sur 8 bits), 1 024 par squelette (ozz) ; **4 influences** (les plus fortes de `JOINTS_0` et `_1`), poids en 65535es de somme exacte ; sommet sans poids : premier os de la palette | Voir la partie 5 pour le format des sommets ; la limite porte sur la palette d'un maillage, pas sur le squelette |
| Interpolation `CUBICSPLINE` | Échantillonnée à **60 Hz** (et à chaque clé) en clés linéaires au chargement ; `STEP` en paires de clés à 0,1 ms | ozz n'interpole que linéairement ; valeurs vérifiées contre Blender |
| Temps d'un clip 3D (ticks), partage avec l'`AnimationPlayer` du jalon 2 | **`ClipClock`** commun : cycle en ticks entiers, temps et vitesse en millièmes, lecture unique ou en boucle, marques vues exactement une fois ; durée d'un clip 3D = secondes × **60**, arrondie, au moins 1 tick, quelle que soit la fréquence de la logique | Un seul mécanisme, déjà testé au jalon 2 (tests et capture 2D inchangés) ; entiers : même temps sur toutes les machines |
| Échantillonnage au dessin (temps interpolé ou deux poses) | **Au temps interpolé** (`Animator::ratio(alpha)`), une seule fois par image et par personnage visible ; temps jamais ramené dans le cycle, pour qu'une boucle avance ; première pose d'un clip juste lancé | Une passe d'ozz au lieu de deux ; pas de retour en arrière au bout d'une boucle |
| Ce qui est déterministe (temps, événements) et ce qui ne l'est pas (poses) | **Déterministe** : clip, temps, vitesse, fin d'un clip (état de l'`Animator`, avancé au tick). **Hors du jeu** : la pose (`AnimationPose`, calculée au dessin, seulement pour ce qui est visible), que la logique ne lit jamais | Rejeux identiques sur toutes les machines ; les flottants et le SIMD d'ozz ne touchent que l'image |
| Format des indices et poids de sommets, second tampon | **Indices `UBYTE4`, poids `USHORT4_NORM`** (12 octets par sommet), dans un **second tampon** (`Mesh::skin`) ; attributs 13 et 14 | Recommandation du document ; les maillages fixes et leurs passes ne changent pas ; 15 attributs sur 16 |
| Palettes (storage buffer, matrices 3×4) | **Storage buffer** unique par image, **3 lignes `float4`** par matrice ; début de la palette dans `MeshInstance::emissive.w` ; une palette par skin et par personnage | Un envoi par image ; aucun attribut de plus ; les maillages d'un personnage partagent sa palette |
| Skinning linéaire ou quaternions doubles | **Linéaire** (`skinning.hlsli`), la même formule sur le CPU (`skin_position`) | Recommandation ; aucun défaut visible sur les modèles de test ; conforme à Blender à 3·10⁻⁴ m |
| Boîtes des personnages animés pour le culling | **Boîte de la pose de l'image** : os du skin dans la pose, agrandis de la portée de la partie (`skin_radius`, calculée au chargement). *Révisé : d'abord « une boîte par clip »* | Toujours juste (mélanges et attaques compris, même un clip qui n'existait pas au chargement), serrée, et quelques dizaines d'os à parcourir par personnage |
| Graphe d'animation (code ou données) et nombre de couches | **En code dans le jeu**, avec les briques du moteur (fondu, blend space 1D, masque) ; les **paramètres en JSON** (`AnimationSet` : vitesses au sol, phases, fondus, blend spaces, masques). **Deux couches** (corps entier ; haut du corps masqué par-dessus), quatre mouvements au plus par couche, pas de mélange additif | Recommandation validée (2026-09-25) ; la tranche n'a que quatre ou cinq états ; un graphe en données sera revu au jalon 7 |
| Root motion ou animations sur place | **Sur place**, la cadence accordée à la vitesse de déplacement (`set_move_speed`, `match_speed`, blend spaces) ; vitesses au sol **mesurées** sur les clips (`measure_stride`) et écrites dans le fichier | Recommandation validée ; le déplacement vient du clic et du pathfinding ; root motion éventuel plus tard, extrait au chargement |
| Où sont écrits les événements ; règle des événements pendant un fondu | **Dans le fichier de description JSON** (temps en secondes, nom, `always`), convertis en ticks quand le clip est lancé ; un événement part si son clip pèse **au moins 0,5** à la fin du tick (couche du haut : × le poids de la couche), dans un blend space seulement le clip qui pèse le plus ; `always` : quel que soit le poids ; au tick, jamais au dessin | Recommandation validée (2026-09-25) ; glTF n'a pas d'événements ; entiers : mêmes événements aux mêmes ticks partout |
| Barres de vie et noms : sur l'os de la tête ou au-dessus de la boîte | **Au-dessus de la boîte**, fixes ; l'os de la tête (point `head`) pour les effets | Choix de l'utilisateur en voyant les deux repères (2026-09-25) : une barre qui oscille à chaque pas se lit mal |
| Attaches : calcul au dessin, héritage de l'échelle, points d'attache nommés | **Au dessin**, sur la pose de l'image, poses calculées une fois par collecte à la première demande (même hors de la vue si un objet attaché la demande) ; échelle de l'**os non transmise**, celle de l'entité parent oui ; **points nommés** dans le fichier de description (os + position + rotation), un nom d'os accepté à défaut ; `BoneAttachment` en plus du `Parent` | Recommandation validée (2026-09-25) ; aucun retard sur la main ; le jeu ignore les noms d'os des modèles |
| Budget de personnages animés, multithreading de l'échantillonnage | Cible **200 personnages** animés (environ 1 ms de CPU sous Windows) ; plafond mesuré à 60 images par seconde : environ **1 600** sous Windows, le Mac à mesurer. **Pas de multithreading** : les mesures ne le demandent pas ; piste gardée (tranches de personnages, résultat identique à un fil) | Mesures de la partie 10 (2026-09-25) ; le fil principal garde sa marge pour la logique |
