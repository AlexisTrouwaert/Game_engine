# Revue des jalons 1 à 7 : exigence d'un moteur professionnel

Retour à la [roadmap](ROADMAP.md). Écrit pendant le [jalon 8](JALON_8_CONSOLIDATION.md), avant sa clôture (2026-10-16).

## Pourquoi cette revue

Les jalons 1 à 7 ont été décidés quand le projet visait d'abord à **apprendre et à faire tourner** un moteur. Depuis le jalon 7, l'objectif est une **version finale, proche d'un moteur de jeu professionnel**, pour un ARPG livrable. Certaines décisions prises « au plus simple » ne tiennent plus dans ce cadre, et chacune coûte plus cher à changer une fois que le code de l'ARPG s'appuie dessus.

Ce document reprend les tableaux « Décisions à consigner » des jalons 1 à 7 et le code tel qu'il est (vérifié le 2026-10-16). Il ne retient que ce qui **pose un problème** : ce qui reste juste n'y figure pas. Pour chaque point : la décision d'origine, ce qui change avec l'exigence professionnelle, ma recommandation et sa priorité.

**Priorités** :

- **P1** : avant de clore le jalon 8. Petit, ou bien la partie 9 (documentation de l'API) le figerait.
- **P2** : avant que l'ARPG n'en dépende, dans un jalon de mise à niveau (voir la fin du document).
- **P3** : plus tard, quand une mesure ou le contenu le demandera ; la décision d'origine tient d'ici là.
- **Décision à prendre** : un choix qui engage l'architecture ou un coût, et qui est le tien.

Les points marqués *(à vérifier)* demandent une recherche avant de s'y appuyer.

---

## Synthèse

| # | Sujet | Priorité |
|---|---|---|
| T1 | Langue des messages et textes du joueur écrits en dur dans le moteur ; pas de localisation | P1 (séparer) puis P2 (localisation) |
| T2 | Pas d'assertions du moteur (contrats, niveaux dev / livré) | P1 |
| T3 | Politique d'erreur : exceptions possibles au milieu d'une image | P2 |
| T4 | MSVC en français : classe de bugs des dépendances d'en-têtes | P1 (action de ta part) |
| T5 | Tout le jalon 8 non commité | P1 |
| T6 | Stockage des vrais assets (Git LFS ou autre) jamais décidé | Décision à prendre |
| T7 | Identité des assets par leur chemin | P2 |
| 1.1 | MSL généré sous Windows et versionné | P2 |
| 2.1 | Code 2D hérité (isométrique, démo 2D) dans l'API publique | P1 |
| 2.2 | Texte : bitmap à une taille, latin seulement, pas de mise en forme | P2 |
| 2.3 | Pas de système d'interface pour le jeu | Décision à prendre |
| 3.1 | Matériaux : pas de transparence ni d'alpha testé, pas de variantes | P1 (alpha testé) puis P2 |
| 3.2 | `Material` tient des pointeurs bruts vers les textures | P2 |
| 3.3 | 32 lumières ponctuelles en boucle simple | P2 |
| 3.4 | Aucun post-traitement (bloom, occlusion ambiante, étalonnage, TAA) | P2 |
| 3.5 | Sélection des objets à la souris provisoire | P2 (avant l'ARPG) |
| 3.6 | Maillages lus depuis le glTF ; pas de niveaux de détail | P3 |
| 4.1 | Audio maison sans événements sonores en données | Décision à prendre |
| 5.1 | Pas de root motion | P2 |
| 5.2 | Pas de couche additive ; 2 couches, 4 mouvements | P2 |
| 6.1 | Monde plat (un seul plan) | Décision à prendre |
| 6.2 | Particules sur le CPU, sans traînées ni maillages | P2 |
| 6.3 | Fréquence de 60 Hz supposée en dur à plusieurs endroits | P1 |
| 7.1 | Pas de réflexion des types : inspecteur, sauvegarde et tables écrits à la main | Décision à prendre |
| 7.2 | Énumérations sauvegardées comme entiers | P1 |
| 7.3 | Profiler encore actif dans la version livrée | P1 |

---

## Points transverses

### T1. Langue des messages, textes du joueur, localisation

- **D'origine** (jalon 1) : code en anglais, documentation en français. Rien n'a été décidé pour les textes affichés.
- **Constat** : le moteur mélange les deux langues. Une centaine de chaînes en français sont écrites en dur dans `src/moteur` : `debug_tools.cpp` (98), `application.cpp`, `cook.cpp`, `data_table.cpp`, `data_reader.cpp`, `save_file.cpp`, `console.cpp`, `display.cpp`. Certaines sont des textes **du joueur**, comme « Fenêtré », « Plein écran » et « Basses » dans `display.cpp`, ou le message de plantage. Il n'y a aucun système de localisation.
- **Exigence pro** : le moteur ne contient **aucun texte du joueur**. Ses messages pour le développeur (journal, erreurs de données, outils) sont dans **une seule langue**, l'anglais, celle du code. Les textes du joueur passent par des **tables de chaînes** à clés stables, une par langue, avec paramètres et pluriels ; le jeu fournit les traductions. Un ARPG se traduit presque toujours (PoE : une dizaine de langues).
- **Recommandation** :
  - **P1** : sortir du moteur les libellés du joueur (`display.cpp`, message de plantage) ; le jeu les donne.
  - **P2** : un module de localisation dans le moteur (tables en données, clés, paramètres nommés, pluriels, langue choisie par le joueur, vérification des clés manquantes à la préparation des assets).
  - Décider la langue des messages du développeur : anglais recommandé, puisque le moteur est en anglais.

### T2. Assertions et contrats

- **Constat** : aucune macro d'assertion dans le moteur (pas de `MOTEUR_ASSERT`). Les erreurs de programmation passent par des exceptions, des `SDL_Log` ou rien.
- **Exigence pro** : des assertions à niveaux. Les vérifications coûteuses tombent dans la version livrée, celles qui protègent les données du joueur restent. Chaque échec donne un message avec fichier et ligne, et passe par le rapport de plantage de la partie 8.
- **Recommandation** : **P1**, `MOTEUR_ASSERT` (développement), `MOTEUR_VERIFY` (toujours) et `MOTEUR_UNREACHABLE`, branchés sur `crash.hpp` ; puis les placer aux contrats documentés des en-têtes publics pendant la partie 9.

### T3. Politique d'erreur

- **D'origine** (jalon 1) : exceptions pour l'initialisation.
- **Constat** : des exceptions peuvent partir au milieu d'une image. Par exemple, la préparation des maillages lève si une copie GPU échoue ; le tampon de commandes de l'image n'est alors pas envoyé.
- **Recommandation** : **P2**. Écrire la règle : exceptions **au chargement** seulement ; aucune ne traverse la boucle d'une image (erreur notée, image sautée, ou plantage contrôlé avec rapport). La faire respecter à la relecture de la partie 9.

### T4. MSVC en français

- **Constat** : deux jalons ont perdu du temps sur l'espace insécable de « Remarque : inclusion du fichier » (jalon 4 et jalon 8). La parade du verifier tient, mais CLion, une console ou un futur outil peuvent retomber dans le piège.
- **Recommandation** : **P1, à faire de ton côté**. Installer le **pack de langue anglais** de Visual Studio (Visual Studio Installer > Modifier > Modules linguistiques) et mettre `VSLANG=1033`. Le préfixe devient de l'ASCII pur : toute la classe de bugs disparaît. C'est la configuration habituelle des machines de build des studios.

### T5. Discipline git

- **Constat** : tout le jalon 8 est non commité (une centaine de fichiers). Un commit unique par jalon rend les régressions difficiles à retrouver (`git bisect`) et fait courir le risque de perdre beaucoup de travail.
- **Recommandation** : **P1**. Un commit par partie terminée (au moins), une branche par jalon fusionnée à la fin, une étiquette par version (`v0.8.0`). Pour le jalon 8 : le découper maintenant en quelques commits par partie. Je peux préparer les messages ; c'est toi qui commites.

### T6. Stockage des vrais assets

- **D'origine** (jalons 2 et 3) : modèles de test hors de Git, retéléchargés par script ; « Git LFS à décider avec les premiers vrais assets ».
- **Constat** : les captures de référence dépendent d'assets non versionnés. L'ARPG va produire des Go de sources (Blender, textures, sons).
- **Options** : **Git LFS** (simple, GitHub le propose, quotas payants au-delà d'un seuil *(à vérifier)*) ; un **dépôt d'assets séparé** ; **Perforce** (la norme des studios pour les gros binaires, gratuit pour quelques utilisateurs *(à vérifier)*).
- **Recommandation** : **décision à prendre** avant le premier asset de l'ARPG. Pour un développeur seul sur GitHub : **Git LFS** pour les sources d'art, et les assets de test versionnés eux aussi (fin des scripts de téléchargement pour les références).

### T7. Identité des assets par leur chemin

- **Constat** : cartes, tables, sauvegardes et manifestes désignent les assets par leur **chemin**. Renommer ou déplacer un fichier casse silencieusement ce qui le référence (la préparation ne voit que ce qu'elle suit).
- **Exigence pro** : Unity et Godot (depuis la version 4) donnent un **identifiant stable** à chaque asset, Unreal garde des **redirections** après un renommage. Path of Exile désigne ses fichiers par chemin, mais avec des outils qui vérifient toutes les références *(à vérifier)*.
- **Recommandation** : **P2**. Garder les chemins (lisibles, adaptés aux données en texte) mais :
  - la préparation des assets vérifie **toutes** les références (cartes, tables, effets, descriptions d'animation), avec erreur si une cible manque ;
  - l'éditeur offre « renommer / déplacer » qui met à jour les références.

  Les identifiants stables (GUID) ne valent leur coût que si les données ne sont plus écrites à la main.

---

## Jalon 1 - Fondations

| Décision d'origine | Ce qui change | Recommandation | Priorité |
|---|---|---|---|
| **1.1** MSL pré-généré sous Windows et versionné (shadercross absent de vcpkg sur Mac) | Une étape manuelle : un shader modifié sans réexport casse le Mac. Le verifier la détecte, mais le Mac ne compile toujours pas ses propres shaders | Construire `SDL_shadercross` sur macOS par un port vcpkg maison (comme ozz), avec SPIRV-Cross pour produire le MSL sur place ; supprimer le MSL versionné | P2 |
| Filtrage `NEAREST`, monde en pixels, Y vers le bas (pixel art) | Obsolète : le jeu est en 3D, et l'interface doit s'adapter à la densité des écrans | À reprendre avec le système d'interface (2.3) : filtrage linéaire et unités indépendantes de la densité | P2 (avec 2.3) |
| Baseline vcpkg épinglée `5f96cd1` | Juste, mais sans règle de mise à jour : les correctifs de sécurité des dépendances (zstd, libktx, SDL) n'arrivent jamais | Écrire la règle : mise à jour de la baseline au début de chaque jalon, verifier complet, note de version | P3 |

## Jalon 2 - Rendu 2D

| Décision d'origine | Ce qui change | Recommandation | Priorité |
|---|---|---|---|
| **2.1** Rendu 2D complet : projection isométrique, animations de sprites, démo 2D, atlas de test | Le jeu est en 3D. `IsoProjection`, `AnimationPlayer` à images et la démo 2D font partie de l'**API publique** que la partie 9 va documenter et figer. Le lot de sprites et le texte servent toujours à l'interface ; `TileMap` sert à la grille | Avant la partie 9 : retirer du moteur ce qui ne sert plus au jeu (isométrique, scènes 2D du bac à sable) ou le déplacer dans le bac à sable ; garder le lot de sprites, le texte et `TileMap` | P1 |
| **2.2** Texte : `stb_truetype`, une taille par police rasterisée au chargement, ~236 glyphes latins, pas de mise en forme | Une interface d'ARPG change d'échelle (4K, Retina, réglage du joueur) et a besoin de contours et d'ombres (infobulles, dégâts flottants). Toute traduction hors alphabet latin est impossible | Glyphes en **champ de distance signée** (SDF ou MSDF : une police nette à toute taille, contour et ombre dans le shader), atlas de glyphes **dynamique**. Mise en forme par **HarfBuzz** seulement si des langues non latines sont visées (lié à T1) | P2 |
| **2.3** Pas d'interface du jeu : `SliceUi` écrit à la main pour la tranche, ImGui pour les outils | Inventaire, arbre de passifs, infobulles, menus : un ARPG a l'une des interfaces les plus lourdes du genre. Sans système d'interface, chaque écran se code à la main | Choisir maintenant : **RmlUi** (bibliothèque libre, MIT, interfaces décrites en HTML / CSS, rendu à brancher sur SDL_GPU, utilisée dans des jeux livrés *(à vérifier)*), **NoesisGUI** (XAML, payante), ou un **système maison** en mode retenu. Recommandation : **RmlUi**, faute de pouvoir raisonnablement écrire nous-mêmes un moteur de mise en page | Décision à prendre |

## Jalon 3 - Rendu 3D

| Décision d'origine | Ce qui change | Recommandation | Priorité |
|---|---|---|---|
| **3.1** Matériau PBR fixe ; `alphaMode` de glTF ignoré, alpha de la couleur de base ignoré par le shader | Pas de feuillage, de chaînes, de grillages ni de tissus découpés (alpha **testé**), pas d'objets transparents (verre, fantômes, eau). Pas de variantes de matériau (dissolution à la mort, éclair blanc à l'impact, émissif animé) | **P1** : alpha testé (`MASK` + `alphaCutoff`, ombres comprises). **P2** : passe transparente triée (`BLEND`), et un système de matériaux à **variantes** (paramètres et fonctions choisis par donnée, permutations de shaders compilées à la préparation) | P1 / P2 |
| **3.2** `Material` tient des `const Texture*` | Durée de vie fragile : un rechargement à chaud ou le ramasse-miettes peut laisser un pointeur pendant | Des poignées `Asset<Texture>` dans le matériau | P2 |
| **3.3** 32 lumières ponctuelles par image, boucle simple ; *forward+* « si les sorts l'exigent » | Un ARPG en combat chargé allume des dizaines de sources (sorts, projectiles, impacts) : au-delà de 32, elles sont ignorées sans bruit | **Rendu clustered forward** (lumières rangées par case de l'écran et tranche de profondeur), le standard actuel du forward ; garder le budget d'ombres | P2 |
| **3.4** Pas de post-traitement (« Plus tard » de la roadmap) | Le style visé est réaliste et sombre (Bloodborne, Beksiński) : sans **bloom**, **occlusion ambiante**, **étalonnage** (table de couleurs) et anticrénelage temporel, le rendu restera « technique ». Path of Exile 2 et Diablo IV ont tout cela | Un jalon de rendu : bloom, GTAO (ou SSAO), étalonnage par table 3D, exposition, TAA ou au moins SMAA ; puis distorsion pour les effets | P2 |
| **3.5** Sélection des objets **provisoire** : la créature la plus proche du point visé au sol | Cliquer sur un monstre, un objet au sol ou une porte est le geste de base d'un ARPG, et la sélection doit suivre la silhouette | Rayon contre les formes de collision (cercles, boîtes) avec priorités (monstre > objet > sol), et tampon d'identifiants à l'image pour la précision au pixel du survol | P2 (avant le jalon 1 de l'ARPG) |
| **3.6** glTF lu à l'exécution, pas de format de maillage préparé, pas de niveaux de détail | Mesuré : ça tient pour la tranche. Le contenu d'un ARPG complet (centaines de modèles, foules de 300) demandera des maillages préparés et des niveaux de détail | Format binaire préparé (sommets prêts, tangentes calculées) et niveaux de détail, quand une mesure le montre | P3 |

## Jalon 4 - Systèmes de base

| Décision d'origine | Ce qui change | Recommandation | Priorité |
|---|---|---|---|
| **4.1** Audio : miniaudio et mixage maison ; un son = un fichier, joué par le code | Dans un studio, le **concepteur sonore** règle ses sons sans programmeur : événements sonores (variations au hasard, hauteur, volume, priorité), bus, effets (réverbération par zone), atténuation. C'est le rôle de **FMOD** ou de **Wwise** | **Décision à prendre** : (a) garder miniaudio et ajouter des **événements sonores en données** (variations, bus, réverbération), le cœur de ce que donne FMOD, à écrire nous-mêmes ; ou (b) **FMOD Studio** (gratuit sous un seuil de revenus pour les indépendants *(à vérifier)*, outil de création complet, non libre). Recommandation : **(a)**, en accord avec « bibliothèques seulement pour ce qu'on ne peut pas écrire » ; (b) si un concepteur sonore rejoint le projet | Décision à prendre |
| Rechargement à chaud : pas les atlas ni les animations 2D | Mineur (2D hérité, voir 2.1) | Rien, ou retiré avec 2.1 | — |

### Audio : maison maintenant, FMOD plus tard ?

Question de l'utilisateur (2026-10-16) : garder des événements maison, et passer à FMOD plus tard si besoin, est-ce que ça risque de coûter beaucoup de temps ?

**FMOD aujourd'hui** *(à vérifier : la page officielle n'a pas pu être lue, les chiffres viennent de sources secondaires)* :

- **Licence gratuite « Indie »** : budget de développement sous 500 000 $ (600 000 $ selon une source plus récente), revenus de l'éditeur sous 200 000 $ par an, projet enregistré sur le site de FMOD, mention « FMOD » et « Firelight Technologies » dans les crédits. Au-delà : licence payante par jeu (environ 2 000 $ en « Indie » d'après une source ancienne, plus pour les budgets plus gros).
- **Technique** : bibliothèques fournies en DLL (Windows) et dylib (macOS, arm64), donc à livrer à côté de l'exécutable et à signer dans le `.app`. Les banques de sons peuvent se lire depuis la mémoire ou par des fonctions de lecture fournies par le jeu : elles passent par nos archives. L'outil FMOD Studio, où le concepteur sonore construit ses événements, est le vrai apport.
- **Non libre** : contraire à la préférence du projet pour les bibliothèques libres, mais rien n'oblige à l'éviter si la licence convient.

**Le coût d'un passage plus tard dépend d'une seule chose : la forme de l'API audio que le jeu utilise.**

- Si le jeu ne parle qu'en **événements** (« jouer `combat/epee/impact` ici, avec le paramètre `matiere = os` »), avec des **paramètres**, des **bus** et des **instantanés** de mixage (ce sont exactement les concepts de FMOD Studio), passer à FMOD revient à **remplacer l'implémentation** derrière cette API. Comptons **1 à 2 semaines** côté moteur : branchement, banques dans les archives, paquets, crédits. Il faut y ajouter **la reconstruction des sons dans FMOD Studio**, proportionnelle au contenu. C'est le travail du concepteur sonore, et nos fichiers d'événements ne s'importent pas.
- Si le jeu manipule des fichiers et des voix (« jouer `impact_03.ogg` à tel volume »), comme le fait aujourd'hui `Audio::play`, le passage oblige à reprendre **chaque appel** du jeu : un coût qui grandit avec l'ARPG.

**Recommandation** : événements **maison** maintenant, avec une API **calquée sur les concepts de FMOD Studio** (événements nommés, paramètres, bus et VCA, instantanés, auditeur). Le jeu n'appelle jamais de fichier directement. Le passage à FMOD reste alors un changement de moteur audio, pas une réécriture du jeu ; la décision peut attendre l'arrivée d'un concepteur sonore ou les limites de la version maison.

## Jalon 5 - Animation 3D

| Décision d'origine | Ce qui change | Recommandation | Priorité |
|---|---|---|---|
| **5.1** Animations sur place, pas de root motion | Bonds, ruées, charges, coups lourds qui avancent : dans un ARPG, le déplacement de ces compétences suit l'animation. Sans root motion, il faut l'imiter à la main pour chaque compétence | Root motion **optionnel** : extrait au chargement en courbe de déplacement entière (déterministe), lu par la logique au tick, jamais par la pose | P2 |
| **5.2** Deux couches, quatre mouvements par couche, pas de mélange additif | Réactions aux coups, respiration et visée se superposent en **additif** dans tous les moteurs actuels. Un monstre « livrable » a souvent plus de quatre mouvements actifs | Couches additives et nombre de couches donné par l'`AnimationSet` | P2 |
| Clips reconstruits depuis le glTF à chaque chargement | Mesuré : 36 ms pour la tranche | Archives ozz préparées, quand la mesure le demandera | P3 |

## Jalon 6 - Monde et déplacement

| Décision d'origine | Ce qui change | Recommandation | Priorité |
|---|---|---|---|
| **6.1** **Monde plat** : un plan unique pour la logique ; Recast & Detour « avec une zone qui en a besoin » | La décision la plus lourde de la revue. Collisions, chemins, vision, brouillard, sélection, sauvegardes et éditeur supposent `y = 0`. Path of Exile a des escaliers, des ponts et des dénivelés ; l'univers décrit (remparts, cryptes, ponts de tendons) en appelle | **Décision à prendre, avec la conception de l'ARPG** : (a) rester plat (le décor monte, le jeu non : c'est le choix de Diablo II) ; (b) **2,5D** : une hauteur par case de la grille et des rampes, un seul niveau jouable à un endroit donné, ce qui couvre escaliers et dénivelés à un coût modéré ; (c) **plusieurs niveaux** superposés, avec maillage de navigation (Recast & Detour). Recommandation : **(b)** si l'univers le demande, à décider **avant** l'écriture du gameplay | Décision à prendre |
| **6.2** Particules sur le CPU, à l'image, budget de 20 000, billboards seulement | Les effets d'un ARPG (sorts, explosions, traînées d'armes) demandent des **traînées** (rubans), des **particules maillées**, de la distorsion et, en combat chargé, bien plus de 20 000 particules | Particules **sur le GPU** (calcul), rubans et maillages ; garder le système CPU pour les petits effets. À cadrer après les mesures du Mac M3 | P2 |
| **6.3** 60 Hz supposé : `kBlockedTicks = 30` (« une demi-seconde à 60 Hz »), `kClipTicksPerSecond = 60` | `ApplicationConfig::fixed_hz` est réglable, mais une autre fréquence change silencieusement des durées de jeu | Une seule source de vérité : les durées en secondes dans les données, converties en ticks par la fréquence de l'application | P1 |

## Jalon 7 - Outils et données

| Décision d'origine | Ce qui change | Recommandation | Priorité |
|---|---|---|---|
| **7.1** Lecture typée en C++ sans schéma ; inspecteur par composant écrit à la main ; sérialiseurs de sauvegarde écrits à la main | Chaque nouveau type demande trois écritures parallèles : lecture des données, panneau d'inspecteur, sauvegarde. Elles divergent avec le temps. Les moteurs professionnels déclarent les champs **une fois** (réflexion : `UPROPERTY` d'Unreal) et en tirent lecture, édition, sauvegarde, schéma pour les outils et différences de versions | **Décision à prendre** : un système de **réflexion** maison léger (macros ou descriptions `constexpr` des champs), qui produit les lecteurs de tables, l'inspecteur, la sauvegarde et un schéma JSON pour les outils. Coût réel (plusieurs semaines), mais il diminue à chaque type ajouté ; l'ARPG va en ajouter des centaines (stats, objets, compétences, monstres). Recommandation : **oui, avant les systèmes de l'ARPG** | Décision à prendre |
| **7.2** Sauvegarde du monde : les énumérations (`MoveMode`, `MoveState`, `PathStatus`) écrites en **entiers** | Réordonner une énumération casse les sauvegardes des joueurs sans aucun message | Écrire les énumérations **par nom** ; migration de la version de section | P1 |
| **7.3** Profiler retiré de la version livrée (décision du jalon 7) | `Application::run` appelle `profiler_.begin_zone` directement : les zones de la boucle restent actives dans la version livrée | Passer ces appels par les macros, vides dans la version livrée | P1 |
| Cartes en JSON + caractères, au plus 255 symboles de légende | Une grande carte faite à la main (beaucoup de points nommés) épuise les symboles | Un format v3 en tableaux de calques le jour où une carte de production l'atteint | P3 |

---

## Révision : la complexité et la durée ne sont pas des critères

Précision de l'utilisateur (2026-10-16), après la première version de cette revue : *« Ne tiens pas compte de la complexité ni de la durée de mise en place d'une solution. Ce qui prime avant tout, c'est le moteur final dans une version professionnelle et utilisable. »*

Plusieurs recommandations ci-dessus pesaient le coût (« coût modéré », « si la mesure le justifie », « quand le contenu le demandera »). Elles sont revues ici ; **cette section remplace les recommandations correspondantes plus haut**. La mesure garde un rôle : elle décide des **micro-optimisations**, pas de l'**architecture**.

| # | Avant | Recommandation revue | Priorité |
|---|---|---|---|
| 6.1 | 2,5D (une hauteur par case), « coût modéré » | **Monde en vraie 3D pour la logique** : maillage de navigation généré depuis la géométrie des niveaux (Recast & Detour, la référence du domaine), plusieurs niveaux superposés (ponts, étages, cryptes sous la ville), collisions contre le maillage de navigation, sélection à la souris contre la géométrie, vision et brouillard avec la hauteur. C'est ce que font Diablo IV et Path of Exile 2 *(à vérifier pour chacun)*. La grille reste un outil (requêtes, flow fields par niveau), pas le monde | **Décision à revoir** |
| 4.1 | Événements maison, au nom de « bibliothèques seulement pour ce qu'on ne peut pas raisonnablement écrire » | Sans critère de coût, la solution des studios est un **intergiciel audio avec son outil de création** (**FMOD Studio** ou **Wwise**) : concepteur sonore autonome, mise à jour en direct dans le jeu, profileur audio, DSP mûrs. Un système maison du même niveau suppose aussi d'écrire **l'outil de création**. Le choix reste ouvert entre deux valeurs : posséder tout le code (maison, outil compris) ou l'outillage standard (FMOD : non libre, licence) | **Décision à revoir** |
| 2.3 | Système d'interface maison (décidé) | Confirmé. Exigences pour qu'il soit de niveau professionnel : mode retenu, mise en page automatique (de type *flexbox*), styles et écrans **en données** rechargés à chaud, animations et transitions, navigation à la manette et au clavier (focus), texte SDF avec mise en forme, localisation, échelle de l'interface, accessibilité (taille du texte, contrastes), et un **éditeur d'écrans** | P2 |
| T7 | Garder les chemins, avec vérification et renommage dans l'éditeur | **Identifiants stables** : chaque asset reçoit un identifiant permanent (fichier `.meta` voisin, comme Unity ; Godot 4 fait de même), les données référencent l'identifiant, le chemin reste lisible à côté ; renommer ou déplacer ne casse rien, la préparation vérifie tout | P2 |
| 2.2 | HarfBuzz « seulement si des langues non latines sont visées » | **Toujours** : rasterisation FreeType en MSDF, mise en forme HarfBuzz, polices de repli, texte bidirectionnel ; le moteur est prêt pour toute langue | P2 |
| 3.6 | Maillages préparés et niveaux de détail « quand une mesure le montre » | **Format de maillage préparé** (sommets quantifiés, ordre optimisé pour le cache : *meshoptimizer*), **niveaux de détail** générés à la préparation, et imposteurs pour les foules lointaines | P2 |
| 5.3 | Archives ozz préparées « quand la mesure le demandera » | **Animations préparées** à la préparation des assets (archives ozz compressées), plus aucune reconstruction depuis le glTF dans le jeu livré | P2 |
| Jalon 8, partie 6 | Cartes et descriptions d'animation en CBOR « si la mesure le justifie » | **Tout ce que le jeu livré lit est préparé** en binaire (cartes, descriptions d'animation, effets, interface) | P2 |
| 7.x | Format de carte v3 « le jour où une carte l'atteint » | Un format de niveau **v3 binaire et découpé** (morceaux chargés à la demande), avec la géométrie 3D et la navigation de 6.1 | P2 |
| Jalons 5, 6 et 8 | « Pas de multithreading » tant que les mesures ne le demandent pas | **Image parallèle par architecture** : graphe de tâches (le système de tâches existe), préparation du rendu et échantillonnage des poses en parallèle, systèmes de logique parallèles là où l'ordre des résultats reste fixe (déterminisme). Rajouter le parallélisme après coup coûte plus que de le prévoir | P2 |
| Jalon 3 | Profondeur non inversée « pour l'instant » | **Profondeur inversée** (*reversed-Z*, flottant 32 bits) : la précision standard des moteurs actuels | P2 (jalon 10) |
| 6.2 | Particules GPU « à cadrer après les mesures du Mac » | **Particules GPU**, rubans, particules maillées, distorsion, sans attendre ; plus un **éditeur d'effets** | P2 (jalon 10) |
| Roadmap « Plus tard » | Physique 3D (Jolt) « si le gameplay en a besoin » | **Jolt** (libre, utilisé dans des jeux AAA *(à vérifier)*) pour les ragdolls, objets et débris dynamiques, destruction ; en mode déterministe là où la logique en dépend | P2 (jalon 10) |
| Roadmap « Plus tard » | Éditeur visuel complet | Un **éditeur de production** : scène et niveaux en 3D (avec 6.1), matériaux, effets, graphe d'animation, tables de données (par la réflexion de 7.1), écrans d'interface. L'éditeur de cartes du jalon 7 en devient une partie | P2 (jalon dédié) |
| Jalon 8 | CI hébergée repoussée | Inchangé : c'est un coût en argent et un choix de l'utilisateur, pas une question de complexité. La version professionnelle a une CI ; à rouvrir quand l'utilisateur le décidera | Choix de l'utilisateur |

**Règle des bibliothèques revue** (décision de l'utilisateur, 2026-10-16) : **maison dès que possible**, quel que soit l'effort. Une bibliothèque n'entre dans le moteur que sur **proposition** argumentée, au moment où le besoin se présente, et **c'est l'utilisateur qui décide**, selon ses critères (niveau professionnel, logiciel libre, maîtrise du code). Les bibliothèques citées dans ce tableau (Recast & Detour, FreeType, HarfBuzz, meshoptimizer, Jolt) sont donc des **options à proposer**, pas des décisions : par défaut, la version maison.

**Conséquence sur le plan** : le jalon 9 « Niveau production » grossit (monde 3D et navigation, identifiants d'assets, formats préparés, image parallèle, éditeur), et un jalon d'éditeur s'ajoute. Les P1 du jalon 8 ne changent pas.

## Réalisation des P1 (2026-10-16)

- **T1, langue et textes du joueur** :
  - le moteur ne contient plus de libellé du joueur : les libellés des options d'affichage sont passés dans le jeu (`states_demo.cpp`), `display.hpp` ne donne que des noms techniques (`window_mode_name`, `shadow_quality_name`) ;
  - le message de plantage montré au joueur vient du jeu (`ApplicationConfig::crash_message`, `{report}` pour le chemin ; défaut en anglais). Le rapport lui-même est pour les développeurs, donc en anglais, et `tools/crash/read_report.py` le lit ;
  - tous les messages du moteur pour le développeur sont en anglais : journal, exceptions, erreurs de données, sauvegardes, préparation des assets, console et ses aides, variables, fenêtres de debug, zones du profiler. `tools/measures/measure.py` suit le nouveau nom de zone. Les outils du moteur (bac à sable, éditeur) aussi ; la tranche, qui est le jeu, reste en français.
  - **Reste** :
    - les noms des touches et boutons affichés par `Input::describe` (« Clic gauche »…) sont des textes du joueur : ils passeront par la localisation (jalon 9) ;
    - les **clés des formats de fichiers** sont en français (`affichage.json`, rapport de mesure, manifeste de livraison, options `--manifeste` / `--sortie` de l'outil de préparation) : à décider (voir plus bas) ;
    - les messages des scripts Python (`tools/`) sont en français.
- **T2, assertions** : `moteur/assert.hpp` : `MOTEUR_ASSERT` (développement, retirée de la version livrée sans évaluer la condition), `MOTEUR_VERIFY` (toujours), `MOTEUR_UNREACHABLE`. Un échec écrit le message, l'expression, le fichier et la ligne au journal et dans le contexte du rapport de plantage, puis arrête (sous débogueur : point d'arrêt). Un quatrième plantage provoqué vérifie la chaîne : `crash assertion`, `tranche --test-plantage assertion`. Le placement des assertions aux contrats des en-têtes publics se fait avec la partie 9.
- **2.1, 2D hérité** : `Camera2D`, `IsoProjection` et les animations de sprites quittent le moteur pour le bac à sable (bibliothèque `bac_a_sable_2d`, espace de noms `bac_a_sable`), avec leurs tests ; la démo 2D et sa capture de référence ne changent pas. Le moteur garde ce dont la 3D a besoin : `TileRange` (dans `tilemap.hpp`), `window_to_pixels` (dans `display.hpp`, testé dans le nouveau `test_display.cpp` avec les réglages d'affichage). Le cache d'animations 2D quitte `Assets`, et les types d'assets ont maintenant des noms (`AssetType`) au lieu d'indices magiques (« 4 et 5 ne se rechargent pas »).
- **3.1, alpha testé** :
  - `Material::alpha_cutoff`, lu depuis `alphaMode` `MASK` et `alphaCutoff` (0,5 par défaut) ; un matériau `BLEND` reste opaque, avec un avertissement au journal ;
  - le seuil voyage dans la composante libre de la matrice des normales de l'instance ;
  - la passe principale et **toutes les ombres** (soleil et lumières ponctuelles, avec ou sans skinning) découpent la forme (`alpha_test.hlsli`) ; 8 pipelines d'ombre « découpées » (`shadow_masked*`, `point_shadow_masked*`), les objets opaques gardent les leurs, sans coût en plus ;
  - le regroupement sépare les ombres découpées par texture ;
  - la signature du cache des ombres ponctuelles tient compte de la forme découpée ;
  - MSL exporté.
  - **Vérifié** : modèle `alpha_mask_reference.glb` (damier ajouré) dans la scène « Rendu 3D » ; le sol se voit à travers ; l'ombre de la torche est un damier net ; celle du soleil montre le cadre et un intérieur éclairé, la finesse des cases étant sous la résolution de la carte d'ombre du soleil.
  - Tests : regroupement des ombres découpées, lecture des modes glTF.
- **6.3, fréquence des ticks** : `moteur::tick_rate()` et `ticks_from_seconds()` (`fixed_timestep.hpp`), fixés par `ApplicationConfig::tick_rate` (remplace `fixed_hz`). Ce qui avait 60 en dur suit la fréquence : la durée des clips et de leurs événements (une autre fréquence ne change plus leur durée réelle), le délai de blocage des déplacements (0,5 s), et le code du jeu (confirmation de 15 s des options, rafale de la scène audio).
- **7.2, énumérations** : `MoveMode`, `MoveState` et `PathStatus` sauvegardés par nom (`to_point`, `blocked`…) ; les numéros des sauvegardes du jalon 7 sont encore lus ; un nom inconnu est signalé. Test.
- **7.3, profiler** : les zones de la boucle de `Application` passent par les macros, et le profiler ne commence ni ne finit d'image dans la version livrée.
- **T4** (pack de langue anglais de Visual Studio) et **T5** (commits par partie) : de ton côté ; le découpage en commits est proposé à la fin du jalon.

### Décision à prendre : la langue des formats de fichiers

Les clés et valeurs des fichiers que le moteur lit et écrit mêlent les deux langues. En français : `affichage.json` (`mode`, `ecran`, `resolution_rendu`…), le rapport de mesure (`images_mesurees`, `phases_ms.logique`…), le manifeste de livraison (`inclure`, `exclure`, `donnees`), les options de l'outil de préparation (`--manifeste`, `--sortie`), les directions des connecteurs de carte (`nord`, `est`…). En anglais : les cartes (`tiles`, `rows`, `objects`), les descriptions d'animation, les effets, les liaisons de touches, les tables (`rows`, `base`), les sauvegardes. Un format est une interface du moteur, comme son API : un moteur professionnel les écrit dans une seule langue, celle de l'API (l'anglais). Recommandation : tout passer en anglais, avec lecture des anciennes clés le temps de migrer les fichiers existants.

**Décidé et fait le 2026-10-17** (recommandation validée par l'utilisateur) :

| Format | En anglais | Ancien encore lu |
|---|---|---|
| Réglages d'affichage du joueur | `display.json` : `display`, `vsync`, `frame_limit`, `render_scale` (arrondie au centième), `anti_aliasing`, `shadows` ; valeurs `windowed` / `fullscreen`, `low` / `medium` / `high` | `affichage.json` et ses clés et valeurs (lu si `display.json` manque, puis réécrit en `display.json`) |
| Rapport de mesure | version **2** : `engine`, `platform`, `frames_measured`, `interval_ms`, `phases_ms` (`logic`, `record`, `submit`), `frames_over`, `zones` (`name`, `mean`, `per_frame`), `per_frame`, `gpu_ms`, `memory_mib`, `values` ; `tools/measures/measure.py` suit | non (rapport produit à chaque mesure ; la version 1 avait les clés françaises) |
| Manifeste de livraison | `include`, `exclude`, `data`, `check` (`maps`, `effects`, `animations`), `archives` (`name`, `include`) ; manifestes du projet migrés | `inclure`, `exclure`, `donnees`, `verifier`, `cartes`, `effets`, `nom`, avec un avertissement « deprecated » |
| Outil de préparation | `--manifest`, `--output`, `--orphans` ; sortie `archives/<name>.pak`, `common.pak`, état `cook_state.json` (`files`) | non (outil interne ; appelants mis à jour) |
| Cartes | directions `north` / `east` / `south` / `west` ; point d'entrée `start` (`moteur::kStartPoint`, exigé par l'éditeur) ; cartes du projet migrées | `nord`, `est`, `sud`, `ouest` et le point `depart` (lu comme `start`, avec un avertissement ; l'éditeur réécrit en anglais) |
| Crédits | option `"build": "DEVELOPMENT"` | `DEVELOPPEMENT` |
| Graphe d'animation | noms de couches par défaut `body`, `upper` | — (les noms écrits dans les fichiers sont des données du jeu) |
| Fichiers et dossiers des préférences | `log.txt` (`log.1.txt`), `crash_reports/`, `profiles/profile.json` ; dossier des archives du programme `archives/` | — (les anciens fichiers restent sur le disque, plus écrits) |

Les **données du jeu** gardent la langue du jeu : champs des tables de la tranche (`vie`, `vitesse`), noms de ses états d'animation, valeurs de son rapport (`report_value`), dossier de ses sauvegardes. Tests : réglages d'affichage (anglais écrit, français lu), cartes (`depart` et directions françaises lues, réécrites en anglais), manifeste (clés anglaises, et françaises avec avertissement). La préparation de la tranche avec le manifeste migré : 50 fichiers, 0 avertissement. **Reste** : les options et messages des scripts Python (`verify.py --quick`, `mesurer.py --sortie`…), en français, et le format de leurs propres résumés (`mesurer.py`). *Aujourd'hui (2026-10-17) : fait ; les scripts sont en anglais (`tools/ci/verify.py`, `tools/measures/measure.py --output`, `summary.json`...).*

## Ce qui reste juste

Pour mémoire, les décisions qui tiennent face à l'exigence professionnelle :

- **Fondations** : pas fixe à 60 Hz, interpolation, SDL_GPU (Direct3D 12 et Metal), HLSL unique, CMake + vcpkg en manifeste.
- **Déterminisme** : ticks et poids entiers, logique séparée du rendu, rejeux et test d'exactitude.
- **ECS** pour le monde seulement.
- **Animation** : ozz-animation, skinning sur le GPU.
- **Rendu** : PBR, couleurs linéaires, KTX2 BC7 / BC5.
- **Monde et données** : A\* et flow fields maison, tables avec héritage et forme binaire, sauvegardes atomiques versionnées, pas de langage de script.
- **Jalon 8** : préparation des assets et archives maison, rapports de plantage maison.

---

## Proposition de plan

1. **Avant de clore le jalon 8** (les P1, quelques jours) :
   - séparer les textes du joueur (T1) ;
   - assertions (T2) ;
   - commits par partie (T5) ;
   - retrait du 2D hérité de l'API (2.1) ;
   - alpha testé (3.1) ;
   - fréquence centralisée (6.3) ;
   - énumérations par nom (7.2) ;
   - profiler de la version livrée (7.3) ;
   - et de ton côté le pack de langue anglais (T4).

   La partie 9 (documentation) se fait **après**, sur la surface nettoyée.
2. **Trancher les « Décisions à prendre »** : T6 stockage des assets, 2.3 interface, 4.1 audio, 6.1 monde plat, 7.1 réflexion. Ce sont elles qui décident de l'ordre de la suite.
3. **Un jalon 9 « Niveau production »** avant le jalon 1 de l'ARPG, avec ce qui est P2 et nécessaire au prototype jouable :
   - réflexion (si oui) ;
   - interface et texte SDF ;
   - localisation ;
   - sélection des objets ;
   - matériaux et transparence ;
   - root motion et couches additives ;
   - hauteur du monde (si 6.1 b ou c) ;
   - MSL compilé sur Mac.

   Le rendu (post-traitement, clustered forward) et les particules GPU forment un **jalon 10 « Rendu et effets »**, qui peut avancer en parallèle de l'ARPG, puisqu'il change l'image et pas l'API du jeu.

## Décisions à consigner

| Sujet | Décision | Raison |
|---|---|---|
| T1 Langue des messages du développeur ; localisation | Messages du moteur pour le développeur en **anglais** ; textes du joueur hors du moteur, puis par la localisation (jalon 9) | Recommandation validée par l'utilisateur (2026-10-16) |
| T6 Stockage des assets (Git LFS, dépôt séparé, Perforce) | **Repoussé** jusqu'au premier asset de l'ARPG (développeur seul ; le dossier peut rester sur le PC, partagé au Mac par le réseau local). Condition posée dès maintenant : quand les vrais assets arrivent, un **historique des versions** et une **sauvegarde hors de la machine** | Choix de l'utilisateur (2026-10-16) |
| 2.3 Système d'interface (RmlUi, Noesis, maison) | **Système maison** (mode retenu, écrit par nous) | Choix de l'utilisateur (2026-10-16), contre la recommandation RmlUi : contrôle total du rendu, de la mise en page et des animations de l'interface ; coût de plusieurs mois à prévoir dans le jalon 9 |
| 4.1 Audio (événements maison, FMOD) | **Maison, outil de création compris** : moteur audio, événements en données (nommés, paramètres, bus et VCA, instantanés, auditeur), et un outil de création sonore dans l'éditeur du moteur. API calquée sur FMOD Studio, le jeu ne joue jamais un fichier directement : la **migration vers FMOD reste ouverte** comme simple changement de moteur audio | Choix de l'utilisateur (2026-10-16), revu après la précision « la complexité n'est pas un critère » |
| 6.1 Hauteur du monde (plat, 2,5D, plusieurs niveaux) | **Vraie 3D** pour la logique : plusieurs niveaux superposés, maillage de navigation généré depuis la géométrie (maison par défaut ; Recast & Detour à proposer selon la règle des bibliothèques). *Remplace « 2,5D », d'abord choisi le même jour sur une recommandation qui pesait le coût* | Recommandation revue, validée par l'utilisateur (2026-10-16) |
| Règle des bibliothèques | **Maison dès que possible** ; une bibliothèque seulement sur proposition, décidée par l'utilisateur | Choix de l'utilisateur (2026-10-16) |
| 7.1 Réflexion des types | **Oui**, un système de réflexion maison, avant les systèmes de l'ARPG | Recommandation validée par l'utilisateur (2026-10-16) |
| Plan (P1 dans le jalon 8, jalons 9 et 10) | **Validé** : les P1 avant la clôture du jalon 8, la partie 9 (documentation) ensuite sur la surface nettoyée ; jalon 9 « Niveau production » avant le jalon 1 de l'ARPG ; jalon 10 « Rendu et effets » en parallèle de l'ARPG | Recommandation validée par l'utilisateur (2026-10-16) |
