# Jalon 9 bis - Niveau production : présentation

Retour à la [roadmap générale](ROADMAP.md). Jalon précédent : [Jalon 9 - Niveau production : monde et données](JALON_9_NIVEAU_PRODUCTION.md). Origine du contenu : [Revue des jalons 1 à 7](REVUE_JALONS_1_A_7.md) (les points **P2** et les décisions du 2026-10-16). Utilisation du moteur : [Guide](GUIDE.md) ; formats : [Formats](FORMATS.md).

## Objectif

La seconde moitié du jalon 9 « Niveau production » (découpage décidé le 2026-10-17) : ce que le joueur voit et entend. Des **matériaux** complets (transparence triée, variantes, shaders compilés sur le Mac), l'**animation** du gameplay (root motion, couches additives), un **texte** net à toute taille et prêt pour toute langue (champ de distance maison, mise en forme par HarfBuzz), la **localisation** (syntaxe inspirée de Fluent), le **système d'interface** maison (balises et styles de type CSS), et l'**audio par événements** à la manière de FMOD Studio.

À la fin du jalon, la **tranche jouable** du [jalon 9](JALON_9_NIVEAU_PRODUCTION.md) (dans son niveau 3D) a une **interface** maison (titre, options, sauvegardes, barre de vie, dégâts flottants) en **français et en anglais**, des **sons par événements** (pas selon le sol, réverbération dans les salles), des matériaux transparents et des effets de matériau (dissolution, éclair), et des compétences dont le déplacement suit l'animation ; elle se livre et se joue sur Windows et macOS. Le jalon 1 de l'ARPG peut commencer.

**Niveau d'exigence** et **règle des bibliothèques** : les mêmes qu'au jalon 9. Dans ce jalon, l'utilisateur a choisi **HarfBuzz** pour la mise en forme du texte.

## Prérequis

- Le [jalon 9](JALON_9_NIVEAU_PRODUCTION.md) est terminé et commité (réflexion, identifiants d'assets, formats préparés, monde 3D, éditeur de niveaux).
- Le **Mac M3** pour les shaders compilés sur le Mac, le texte, l'audio et les mesures.

## Comment lire ce document

Même structure que les jalons précédents (But, Tâches, Questions à se poser, Pièges connus, Validation). Les points marqués *(à vérifier)* sont à confirmer avant de s'appuyer dessus.

## Sommaire

1. [Vue d'ensemble et ordre de travail](#1-vue-densemble-et-ordre-de-travail)
2. [Matériaux : transparence, variantes ; shaders compilés sur le Mac](#2-matériaux--transparence-variantes--shaders-compilés-sur-le-mac)
3. [Animation : root motion, couches additives](#3-animation--root-motion-couches-additives)
4. [Texte : champ de distance, mise en forme, polices de repli](#4-texte--champ-de-distance-mise-en-forme-polices-de-repli)
5. [Localisation](#5-localisation)
6. [Système d'interface](#6-système-dinterface)
7. [Audio par événements](#7-audio-par-événements)
8. [La tranche refaite : interface, texte, sons](#8-la-tranche-refaite--interface-texte-sons)
9. [Critères de fin de jalon](#9-critères-de-fin-de-jalon)
10. [Risques principaux](#10-risques-principaux)
11. [Décisions à consigner](#décisions-à-consigner)

---

## 1. Vue d'ensemble et ordre de travail

### Dépendances entre les parties

```
2 Matériaux          3 Animation          7 Audio par événements
4 Texte ──> 5 Localisation ──> 6 Interface
8 La tranche refaite : à la fin, sur tout le reste
```

**Ordre recommandé** : la chaîne **texte → localisation → interface** (4, 5, 6) d'abord, parce qu'elle est la plus longue et que le jalon 1 de l'ARPG en a besoin (barre de vie, menus) ; les parties 2, 3 et 7 s'intercalent où cela arrange ; 8 à la fin.

### Ce qui n'est pas dans ce jalon

- **Jalon 10 « Rendu et effets »** : rendu *clustered forward*, profondeur inversée, post-traitement, particules sur le GPU, physique 3D, éditeur d'effets.
- **Jalon 11 « Éditeur de production »** : éditeurs de matériaux, de graphes d'animation, d'**écrans d'interface**, l'**outil de création sonore**, l'édition des tables par la réflexion.

### Estimation indicative

**Plusieurs mois** : très gros (6), gros (4, 5), moyens (2, 3, 7), petit (8 hors essais).

---

## 2. Matériaux : transparence, variantes ; shaders compilés sur le Mac

### But

Des matériaux de production : la **transparence triée** (verre, fantômes, eau), les **variantes** (dissolution à la mort, éclair à l'impact, émissif animé) choisies par donnée, et des shaders **compilés sur le Mac** au lieu d'un MSL généré sous Windows (1.1).

### Tâches

- [ ] **Passe transparente** : `alphaMode: BLEND` de glTF (aujourd'hui un avertissement), objets triés d'arrière en avant, après l'opaque, éclairés ; double face, ombres (sans, ou en ombres colorées plus tard).
- [ ] **Variantes de matériau** : des **fonctionnalités** activées par donnée (dissolution, éclair, tramage, émissif animé, uv animées), compilées en **permutations** de shaders à la préparation (pas de compilation au moment du jeu) ; paramètres **par instance** (le temps de dissolution d'un monstre précis).
- [ ] **Format de matériau** en données (référencé par identifiant), au-delà de celui du glTF : un matériau du jeu peut surcharger celui d'un modèle (un sol commun à plusieurs pièces de kit).
- [ ] **Shaders compilés sur macOS** : `SDL_shadercross` construit sur le Mac par un port vcpkg maison (comme ozz), avec SPIRV-Cross pour produire le MSL sur place ; le MSL versionné et son étape du verifier disparaissent.

### Questions à se poser

- **Éditeur de matériaux à nœuds ?** Au jalon 11 (éditeur de production). Ici : variantes et paramètres en données.
- **Combien de permutations ?** Elles se multiplient (fonctionnalités × peau × alpha × ombres). Recommandation : **seulement celles utilisées** par les matériaux du jeu, listées à la préparation.

### Pièges connus

- Le **tri** des objets transparents qui se croisent : pas de solution parfaite sans OIT (transparence indépendante de l'ordre, jalon 10 si besoin) ; trier par objet et éviter les croisements dans les assets.
- Le MSL compilé sur le Mac qui **diffère** de celui de Windows : les captures de référence du Mac le diront.

### Validation

- Un fantôme semi-transparent passe devant et derrière un pilier sans défaut ; un monstre se dissout à sa mort ; un coup fait clignoter sa cible.
- Le Mac construit ses shaders lui-même ; le dépôt ne contient plus de MSL généré.

---

## 3. Animation : root motion, couches additives

### But

Les animations du gameplay d'un ARPG : des compétences dont le **déplacement suit l'animation** (bond, ruée, charge), et des **réactions superposées** (coup reçu, respiration, visée) en mélange additif.

### Tâches

- [ ] **Root motion optionnel** (5.1) : extrait à la préparation en **courbe de déplacement en entiers** (déterministe), lue par la logique au tick, jamais par la pose ; combinée avec la navigation et les collisions (une ruée s'arrête contre un mur).
- [ ] **Couches additives** (5.2) : pose de référence, différence ajoutée par-dessus ; masques par couche (déjà là).
- [ ] **Nombre de couches et de mouvements** donné par l'`AnimationSet` (aujourd'hui 2 couches, 4 mouvements par couche).
- [ ] **Événements d'animation** enrichis pour un combat piloté par l'animation (question ouverte du jeu) : fenêtres (début et fin d'une période d'invulnérabilité, de dégâts), pas seulement des instants.
- [ ] **Graphe** : les nouveaux nœuds (additif, root motion) dans le graphe d'animation du jalon 7.

### Questions à se poser

- **Combat piloté par l'animation ?** C'est une question du jeu ([DECISIONS_DE_CONCEPTION.md](../../ARPG/ARPG_doc/DECISIONS_DE_CONCEPTION.md)). Le moteur fournit les fenêtres d'événements et le root motion dans tous les cas ; le jeu décide s'il s'en sert. Recommandation : **fournir les deux**, sans attendre la décision du jeu.

### Pièges connus

- Lire le root motion depuis la **pose** (flottants interpolés, dépendants de l'image) : rompt le déterminisme. Il vient de la courbe préparée, au tick.
- L'additif appliqué sur une pose de référence **différente** de celle de l'export : un personnage qui se tord. Pose de référence explicite par clip.

### Validation

- Une compétence de ruée avance le personnage selon son animation, s'arrête contre un mur, identique en rejeu ; un coup reçu se superpose à la course sans l'interrompre.

---

## 4. Texte : champ de distance, mise en forme, polices de repli

### But

Un texte **net à toute taille** (4K, Retina, échelle de l'interface choisie par le joueur), avec **contour et ombre** (infobulles, dégâts flottants), et prêt pour **toute langue** (décision 2.2 de la revue : mise en forme toujours).

### Tâches

- [ ] **Glyphes en champ de distance signé multicanal** (MSDF) : générés depuis les contours de la police, rangés dans un **atlas dynamique** (les glyphes ajoutés à la demande, pas 236 au chargement) ; contour, ombre et épaisseur dans le shader.
- [ ] **Mise en forme** (*shaping*) : crénage, ligatures, positions des marques, écritures complexes (arabe, devanagari) selon les tables OpenType de la police.
- [ ] **Texte bidirectionnel** (arabe, hébreu mêlés au latin) et **coupure des lignes** selon les règles Unicode.
- [ ] **Polices de repli** : un caractère absent d'une police est cherché dans les suivantes (CJK, symboles) ; le glyphe `.notdef` seulement en dernier recours.
- [ ] **Texte enrichi** léger (couleurs, gras, icônes en ligne : les mots-clés d'une infobulle d'objet).
- [ ] `TextLayout` et `Font` actuels remplacés ; la tranche et les outils (console, interface de débogage hors ImGui) passent dessus.

### Questions à se poser

- **Lecture des polices et rasterisation : stb_truetype (déjà là), FreeType, ou maison ?** stb_truetype lit les contours TrueType mais pas toutes les tables (CFF partiel *(à vérifier)*). FreeType (licence FTL ou GPL) est la référence. Règle : maison par défaut ; **recommandation : stb_truetype pour les contours, génération MSDF maison** (l'algorithme de Chlumský est publié), FreeType à **proposer** si des polices CFF ou variables sont nécessaires.
- **Mise en forme : maison ou HarfBuzz ?** HarfBuzz (MIT) est la référence, utilisée par les navigateurs et la plupart des moteurs. Écrire un moteur de mise en forme OpenType complet (GSUB, GPOS, écritures complexes) est un très gros travail. Règle : **maison par défaut** ; c'est l'endroit du jalon où la **proposition HarfBuzz** est la plus probable. **Décision de l'utilisateur** au moment de la tâche, avec un essai de la version maison sur le latin, le cyrillique et l'arabe.
  **Décidé (2026-10-17)** : **stb_truetype (contours) + MSDF maison + HarfBuzz** pour la mise en forme (bibliothèque, choix de l'utilisateur : MIT, crédits et licence ajoutés ; paquet vcpkg `harfbuzz` *(à vérifier : options sans dépendances inutiles)*). FreeType reste à proposer si des polices CFF ou variables sont nécessaires.

### Pièges connus

- Les MSDF qui **bavent** aux petites tailles ou aux angles aigus : taille de champ et correction des artefacts (publiée avec l'algorithme).
- L'atlas dynamique qui **déborde** en jeu (beaucoup de glyphes CJK) : plusieurs pages, éviction des glyphes inutilisés.

### Validation

- Le même texte net de 10 à 200 pixels ; un titre avec contour et ombre ; une phrase arabe mêlée de français affichée dans le bon ordre ; un caractère chinois trouvé dans la police de repli.

---

## 5. Localisation

### But

Que le jeu se **traduise** sans toucher au code : tables de chaînes à clés stables, paramètres, pluriels, langue choisie par le joueur (T1).

### Tâches

- [ ] **Tables de chaînes** en données, une par langue, par **clé stable** (`ui.options.title`) ; référencées par l'interface (partie 6), les données (noms d'objets, de compétences) et le code du jeu.
- [ ] **Paramètres nommés** et **formats** (nombres, pourcentages, durées) selon la langue ; **pluriels** selon les règles CLDR de chaque langue (le français, l'anglais, le russe, l'arabe n'en ont pas le même nombre).
- [ ] **Genre et accords** de base (un mot qui s'accorde avec l'objet) *(à cadrer avec le jeu)*.
- [ ] **Langue du joueur** : réglage, changement à chaud (l'interface se refait), repli vers la langue de base pour une clé absente (avec avertissement en développement).
- [ ] **Vérification à la préparation** : clés absentes, paramètres manquants ou en trop, chaînes non traduites listées par langue.
- [ ] **Pseudo-localisation** (texte allongé et accentué) pour trouver les textes en dur et les interfaces trop étroites.
- [ ] Les textes du joueur encore dans le moteur passent par là : noms des touches (`input_source_label`, « Clic gauche », « Manette A »), libellés que le moteur fournit au jeu.

### Questions à se poser

- **Format des chaînes** : syntaxe maison, ou inspirée d'**ICU MessageFormat** ou de **Fluent** (Mozilla) pour les pluriels et les sélecteurs ? Recommandation : **une syntaxe inspirée de Fluent** (lisible par un traducteur, pluriels et sélecteurs intégrés), lue par du code maison ; les règles de pluriel tirées des données CLDR (publiques) et générées en table.
  **Décidé (2026-10-17)** : **inspirée de Fluent**, lue par du code maison.
- **Fichiers de traduction** pour un traducteur extérieur (formats PO ou XLIFF) ? Recommandation : un **export / import** vers un format d'échange standard, plus tard ; les tables maison restent la source.

### Pièges connus

- Construire une phrase par **concaténation** (« Vous avez trouvé » + n + « objets ») : intraduisible. Toujours une chaîne entière avec paramètres.
- Les textes qui débordent en allemand ou en russe (30 % plus longs *(ordre de grandeur courant)*) : la pseudo-localisation les montre tôt.

### Validation

- La tranche en **français et en anglais**, la langue changée dans les options sans redémarrer ; la préparation signale une clé absente ajoutée exprès ; la pseudo-localisation ne montre aucun texte en dur.

---

## 6. Système d'interface

### But

Le **système d'interface maison** (décision 2.3) au niveau d'un jeu livré : mode retenu, mise en page automatique, écrans et styles **en données** rechargés à chaud, animations, navigation à la manette, échelle, accessibilité. Il remplace `SliceUi`.

### Tâches

- [ ] **Arbre d'éléments en mode retenu** : conteneurs, texte (partie 4), images (9 tranches), boutons, cases, curseurs, listes déroulantes, champs de saisie, listes défilantes, barres (vie, ressource), infobulles, fenêtres.
- [ ] **Mise en page automatique** de type *flexbox* (direction, alignement, marges, tailles minimales et maximales, retour à la ligne) ; unités indépendantes de la densité ; **échelle de l'interface** réglée par le joueur.
- [ ] **Écrans et styles en données** (format maison, rechargé à chaud) ; styles par classes et états (survolé, focus, désactivé), thèmes.
- [ ] **Liaison aux données** du jeu (la vie du héros, la liste des objets) sans réécrire l'écran à chaque image : propriétés observées.
- [ ] **Animations et transitions** (ouverture d'un menu, survol, apparition des dégâts flottants), courbes d'interpolation.
- [ ] **Focus et navigation** au clavier et à la manette (la direction choisit le voisin le plus proche, ou un ordre déclaré) ; la souris en même temps ; actions du jeu (partie Input du jalon 4).
- [ ] **Texte localisé** (partie 5) ; changement de langue à chaud.
- [ ] **Accessibilité** : taille du texte, contrastes, daltonisme (palettes), sous-titres, aucun texte en dur.
- [ ] **Rendu** : lots par texture et par couche, découpe (*clipping*), peu d'appels de dessin ; en espace écran et **en espace monde** (barres de vie au-dessus des monstres, noms des objets au sol).
- [ ] Les écrans de la tranche (titre, chargement, pause, options, emplacements de sauvegarde, barre de vie) refaits dessus.

### Questions à se poser

- **Format des écrans** : une syntaxe maison de type balises (comme le XAML ou le RML), du JSON, ou du code C++ ? Recommandation : **balises légères + styles de type CSS (sous-ensemble)**, en données, parce que l'éditeur d'écrans du jalon 11 les écrira et qu'un intégrateur les lit facilement. **Décision**.
  **Décidé (2026-10-17)** : **balises + styles de type CSS**, rechargés à chaud.
- **Les outils de développement** (inspecteur, profiler, console) restent-ils en Dear ImGui ? Recommandation : **oui** : ce sont des outils, retirés de la version livrée ; l'interface maison est celle du jeu.

### Pièges connus

- Une mise en page **recalculée à chaque image** pour tout l'arbre : invalidation par élément.
- L'interface qui lit la logique **pendant** un tick : elle lit l'état publié à la fin du tick.
- Des dizaines de barres de vie en espace monde, une par appel de dessin : lots.

### Validation

- Les écrans de la tranche refaits en données ; un style changé dans un fichier se voit sans redémarrer ; tout se navigue à la manette ; l'échelle de 75 à 200 % reste lisible ; 200 barres de vie à l'écran en un petit nombre d'appels de dessin.

---

## 7. Audio par événements

### But

Les **événements audio** maison décidés le 2026-10-16 (4.1), avec une API calquée sur les concepts de **FMOD Studio**, pour que le jeu ne joue jamais un fichier directement et que la migration vers FMOD reste un changement de moteur audio. L'**outil de création sonore** est au jalon 11.

### Tâches

- [ ] **Événements** en données, par nom ou identifiant (`combat/epee/impact`) : pistes, sons choisis au hasard ou en séquence, variations de volume et de hauteur, boucles, limites d'instances et vol de voix par priorité.
- [ ] **Paramètres** d'événement (locaux et globaux : `matiere = os`, `intensite_combat`) qui choisissent les sons et règlent volume, hauteur, filtres.
- [ ] **Bus, VCA et instantanés** (*snapshots*) de mixage : la pause baisse la musique, un dialogue baisse le reste (*ducking*), les réglages du joueur sont des VCA.
- [ ] **Spatialisation** : auditeur, atténuation par courbe, occlusion par la géométrie (rayons, partie 5 du [jalon 9](JALON_9_NIVEAU_PRODUCTION.md)), **zones de réverbération** (crypte, extérieur) ; effets (filtre passe-bas, réverbération) écrits maison.
- [ ] **Musique interactive** : transitions sur la mesure, couches selon l'intensité.
- [ ] **Banques** préparées (partie 4 du [jalon 9](JALON_9_NIVEAU_PRODUCTION.md)) dans les archives ; flux pour la musique.
- [ ] La tranche et `Audio::play` direct remplacés par des événements (l'API par fichiers reste interne au moteur).

### Questions à se poser

- **Format des événements** : calqué sur la structure de FMOD Studio (événements, pistes, instruments, paramètres) *(à vérifier : la structure exacte des projets FMOD Studio)* pour faciliter une migration. Recommandation : **oui**, mêmes concepts et mêmes noms.

### Pièges connus

- Les événements joués **par la logique** au tick et rejoués : l'audio est de la présentation ; le rejeu peut les rejouer, il ne doit jamais en dépendre.
- Des centaines d'instances d'un même impact en combat : limites par événement et par bus.

### Validation

- Les sons de la tranche passent par des événements : les pas changent selon le sol (paramètre), la crypte réverbère, la pause baisse la musique par un instantané ; aucun fichier joué directement par le code de la tranche.

---

## 8. La tranche refaite : interface, texte, sons

### But

Prouver le jalon : la tranche du jalon 9 avec la présentation de production, livrée et vérifiée.

### Tâches

- [ ] Les écrans de la tranche (titre, chargement, pause, options, emplacements de sauvegarde, barre de vie, dégâts flottants) refaits avec le système d'interface (partie 6), en **français et en anglais** (partie 5) ; `SliceUi` retiré.
- [ ] Les sons de la tranche par **événements** (partie 7) ; aucun fichier joué directement par son code.
- [ ] Un matériau transparent (fantôme ou eau), la dissolution des monstres à leur mort, l'éclair à l'impact (partie 2).
- [ ] Une compétence à root motion (ruée) et une réaction additive aux coups (partie 3).
- [ ] Rejeu, captures de référence (nouvelles, documentées), mesures sur le PC et le Mac M3, paquet installé et joué sur les deux OS ; documentation à jour.

### Validation

- Voir les critères de fin de jalon.

---

## 9. Critères de fin de jalon

Le jalon est terminé quand **tout** ce qui suit est vrai :

- [ ] Les **matériaux** ont la transparence triée et des variantes ; le **Mac compile ses shaders** (plus de MSL versionné).
- [ ] L'**animation** a le root motion (déterministe, au tick) et les couches additives.
- [ ] Le **texte** est net à toute taille (champ de distance), mis en forme par HarfBuzz, avec polices de repli et texte bidirectionnel.
- [ ] La **localisation** fonctionne : français et anglais, changement à chaud, vérification à la préparation, pseudo-localisation ; plus aucun texte du joueur dans le moteur.
- [ ] L'**interface** de la tranche est faite avec le système maison, en balises et styles rechargés à chaud, navigable à la manette, à l'échelle choisie par le joueur.
- [ ] Les sons passent par des **événements** (paramètres, bus, instantanés, réverbération).
- [ ] La **tranche** refaite se livre, s'installe et se joue sur Windows et macOS ; le verifier passe sur les deux ; les budgets tiennent sur le Mac M3.
- [ ] HarfBuzz est dans les crédits et sa licence dans les paquets ; la documentation est à jour ; les décisions sont consignées.

---

## 10. Risques principaux

| Risque | Impact | Parade |
|---|---|---|
| Le système d'interface (partie 6) absorbe des mois | Le jalon 1 de l'ARPG attend | Commencer par ce que la tranche utilise ; l'éditeur d'écrans est au jalon 11 |
| L'intégration de HarfBuzz (construction vcpkg sur les deux OS, taille) | Texte bloqué | L'intégrer tôt, derrière l'API de texte du moteur ; le latin fonctionne sans elle pendant ce temps |
| Les permutations de shaders se multiplient | Préparation longue, mémoire | Seulement les permutations utilisées par les matériaux du jeu |
| Le MSL compilé sur le Mac diffère de celui de Windows | Rendu différent sur Mac | Captures de référence du Mac |

---

## Décisions à consigner

Questionnaire du 2026-10-17 (avec celui du [jalon 9](JALON_9_NIVEAU_PRODUCTION.md#décisions-à-consigner)).

| Sujet | Décision | Raison |
|---|---|---|
| Texte : rasterisation et mise en forme | **stb_truetype** (contours) + **champ de distance multicanal maison** + **HarfBuzz** pour la mise en forme ; FreeType à proposer si des polices CFF ou variables sont nécessaires | Choix de l'utilisateur (2026-10-17), contre la recommandation « tout maison » |
| Localisation : syntaxe des chaînes | **Inspirée de Fluent**, lue par du code maison ; règles de pluriel tirées de CLDR | Recommandation validée par l'utilisateur (2026-10-17) |
| Format des écrans d'interface | **Balises + styles de type CSS**, rechargés à chaud | Recommandation validée par l'utilisateur (2026-10-17) |
| Éditeur de matériaux à nœuds, éditeur d'écrans, outil de création sonore | Au **jalon 11** | Plan validé (2026-10-16) |

