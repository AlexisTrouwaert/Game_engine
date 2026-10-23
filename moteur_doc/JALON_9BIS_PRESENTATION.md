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

- [x] **Passe transparente** : `alphaMode: BLEND` de glTF (aujourd'hui un avertissement), objets triés d'arrière en avant, après l'opaque, éclairés ; double face, ombres (sans, ou en ombres colorées plus tard).
- [x] **Variantes de matériau** *(sans permutations : voir l'implémentation)* : des **fonctionnalités** activées par donnée (dissolution, éclair, tramage, émissif animé, uv animées), compilées en **permutations** de shaders à la préparation (pas de compilation au moment du jeu) ; paramètres **par instance** (le temps de dissolution d'un monstre précis).
- [x] **Format de matériau** en données (référencé par identifiant), au-delà de celui du glTF : un matériau du jeu peut surcharger celui d'un modèle (un sol commun à plusieurs pièces de kit).
- [ ] **Shaders compilés sur macOS** *(port écrit, à essayer sur le Mac)* : `SDL_shadercross` construit sur le Mac par un port vcpkg maison (comme ozz), avec SPIRV-Cross pour produire le MSL sur place ; le MSL versionné et son étape du verifier disparaissent.

### Questions à se poser

- **Éditeur de matériaux à nœuds ?** Au jalon 11 (éditeur de production). Ici : variantes et paramètres en données.
- **Combien de permutations ?** Elles se multiplient (fonctionnalités × peau × alpha × ombres). Recommandation : **seulement celles utilisées** par les matériaux du jeu, listées à la préparation.

### Pièges connus

- Le **tri** des objets transparents qui se croisent : pas de solution parfaite sans OIT (transparence indépendante de l'ordre, jalon 10 si besoin) ; trier par objet et éviter les croisements dans les assets.
- Le MSL compilé sur le Mac qui **diffère** de celui de Windows : les captures de référence du Mac le diront.

### Implémentation réalisée (partie 2)

Faite le 2026-10-18 (en autonomie), sous Windows ; la partie Mac est écrite sans essai (décision du 2026-10-18).

- **Transparence** : `Material::blend` (glTF `BLEND` lu à l'import ; format des modèles préparés v4), laissée hors des passes opaques et des ombres ; `MeshBatcher::build_transparent()` trie les objets visibles du plus loin au plus proche (centre des boîtes, ordre d'enregistrement entre égaux) et regroupe les voisins identiques ; 4 pipelines (peau × double face) en alpha prémultiplié, profondeur testée non écrite, dans la passe mélangée qui suit la scène, avant les particules ; la cible de distance garde l'opaque (les particules douces s'y fient) ; le passage d'identifiants les voit (un fantôme se sélectionne). L'émission est pondérée par l'alpha comme le reste (choix de Filament et des moteurs courants ; une lueur indépendante est le travail d'une particule).
- **Effets par objet** (`MaterialEffects`) : **dissolution** (bruit de valeur à deux octaves calculé dans le shader, fixé à l'objet, bord incandescent ; l'ombre au soleil et aux torches se dissout avec : l'objet passe dans les pipelines d'ombre découpée), **éclair** (la couleur éclairée tirée vers une couleur, un coup) ; composant `material_effects` du monde, sauvegardé ; `MeshRenderer::set_effects()` hors du monde. Côté matériau : `uv_scroll` et `emissive_pulse`, sur l'horloge du jeu (`set_time()`).
- **Pas de permutations** (décision) : ces effets coûtent quelques instructions dans des branches uniformes par instance, un seul shader de scène (et sa variante transparente) ; une permutation compilée à la préparation ne vaudra que pour un effet coûteux, mesuré. Les effets voyagent dans **un seul attribut d'instance** (4 entiers : éclair RGBA8, bord RGBA8 × 16, dissolution et pulsation en demi-flottants, défilement en demi-flottants) : SDL_GPU limite un pipeline à 16 attributs, et un maillage animé les prend tous (une première version en deux attributs a buté sur cette limite en Debug).
- **Matériaux en données** : `.material.json` (FORMATS 24, `parse_material()` strict), `Assets::material()` rechargé à chaud ; `ModelComponent::materials` remplace un matériau d'un modèle par son nom (réflexion : préfabriqués, niveaux) ; `Model::material_names`.
- **La tranche** : un coup fait flasher sa cible (10 ticks), un mort reste au sol 1,5 s puis se dissout en 1,5 s (composant `restes`, sauvegardé : l'exactitude des sauvegardes tient) ; sa barre de vie disparaît ; un **fantôme** (modèle fait par l'assistant avec Blender, `tools/models/make_ghost.py`, crédité et livré) tourne autour du pilier ouest de l'arche (point `fantome` du niveau).
- **Scène de test** `bac_a_sable --materials` (capture de référence `materials`, trois exécutions identiques) : le fantôme passe derrière le pilier, son voile s'efface vers le bas ; deux vitres croisées devant une sphère ; dissolutions à 0, 35 et 70 % et une qui tourne, ombres comprises ; éclair ; lave (fichier de matériau : défilement et battement) ; le même fantôme au voile remplacé en rouge par son nom.
- **Mac** : port maison `ports/sdl3-shadercross-macos` (même source que le port officiel ; DXC pris dans le SDK Vulkan de LunarG, `VULKAN_SDK` ou `~/VulkanSDK/<version>/macOS`, copié à côté de l'outil ; `libdxil` inutile pour le MSL), fonctionnalité vcpkg `mac-shaders`, option CMake `MOTEUR_MAC_SHADERCROSS` (désactivée par défaut) qui compile le HLSL en MSL au build. Le MSL versionné et son étape du vérificateur restent jusqu'à l'essai (TEST_MAC.md, jalon 9 bis).
- **Tests** : tri et lots transparents, exclusion des passes opaques et d'ombre, effets empaquetés, ombre découpée d'un objet qui se dissout ; fichier de matériau (valeurs, rôles des textures, erreurs nommées) ; remplacement par nom et effets transmis par le monde, composant enregistré. 556 tests.
- **Références** : tranche **a048249d0645** (le fantôme, les éclairs, les morts qui se dissolvent), exactitude **8e210da133e7** (A, B et C identiques après 120 et 250 ticks), nouvelle **materials** **76967a84d651** ; les autres inchangées (le chemin des effets ne change rien sans effet).
- **Restes** : essayer le port Mac ; le coût GPU des effets sera relevé avec les mesures de la partie 8 ; ombres colorées des vitres et transparence indépendante de l'ordre : jalon 10.

### Validation

- Un fantôme semi-transparent passe devant et derrière un pilier sans défaut ; un monstre se dissout à sa mort ; un coup fait clignoter sa cible.
- Le Mac construit ses shaders lui-même ; le dépôt ne contient plus de MSL généré.

---

## 3. Animation : root motion, couches additives

### But

Les animations du gameplay d'un ARPG : des compétences dont le **déplacement suit l'animation** (bond, ruée, charge), et des **réactions superposées** (coup reçu, respiration, visée) en mélange additif.

### Tâches

- [x] **Root motion optionnel** (5.1) : extrait à la préparation en **courbe de déplacement en entiers** (déterministe), lue par la logique au tick, jamais par la pose ; combinée avec la navigation et les collisions (une ruée s'arrête contre un mur).
- [x] **Couches additives** (5.2) : pose de référence, différence ajoutée par-dessus ; masques par couche (déjà là).
- [x] **Nombre de couches et de mouvements** donné par l'`AnimationSet` (aujourd'hui 2 couches, 4 mouvements par couche).
- [x] **Événements d'animation** enrichis pour un combat piloté par l'animation (question ouverte du jeu) : fenêtres (début et fin d'une période d'invulnérabilité, de dégâts), pas seulement des instants.
- [x] **Graphe** : les nouveaux nœuds (additif, root motion) dans le graphe d'animation du jalon 7.

### Questions à se poser

- **Combat piloté par l'animation ?** C'est une question du jeu ([DECISIONS_DE_CONCEPTION.md](../../ARPG/ARPG_doc/DECISIONS_DE_CONCEPTION.md)). Le moteur fournit les fenêtres d'événements et le root motion dans tous les cas ; le jeu décide s'il s'en sert. Recommandation : **fournir les deux**, sans attendre la décision du jeu.

### Pièges connus

- Lire le root motion depuis la **pose** (flottants interpolés, dépendants de l'image) : rompt le déterminisme. Il vient de la courbe préparée, au tick.
- L'additif appliqué sur une pose de référence **différente** de celle de l'export : un personnage qui se tord. Pose de référence explicite par clip.

### Implémentation réalisée (partie 3)

Faite le 2026-10-18 (en autonomie), sous Windows.

- **Root motion** : `ClipLibrary::root_track()` échantillonne l'articulation racine (clé `root_joint`, `root` par défaut) en espace modèle à **chaque tick du clip**, une fois, et la garde en **millimètres entiers** (plan horizontal seulement : la hauteur d'un saut reste dans la pose). À chaque tick, l'`Animator` ajoute le chemin parcouru sur la courbe (cycles entiers des boucles compris, interpolation en entiers entre deux ticks), pondéré par le poids du mouvement et de sa couche et par le facteur du clip (`"root_motion": 6.0`), en entiers ; le jeu le prend par `take_root_motion()` ; la pose garde la racine à sa place de départ sur le plan (`PoseInput::root_joint`). Sauvegardé avec l'animateur.
- **Avec la navigation et les collisions** : `Mover::root_motion` (mètres sur le plan), parcouru par `move_movers()` avec le contrôleur de personnage (3D) ou les murs de la grille ; une ruée **s'arrête contre un mur**, identique d'une exécution à l'autre (test).
- **Couches additives** : chaque clip garde sa **première pose** (pose de référence calculée à sa construction) ; une entrée additive ajoute sa différence (translation, rotation `conj(réf) × r`, échelle, comme l'`AdditiveAnimationBuilder` d'ozz) par les couches additives du `BlendingJob` d'ozz, avec son poids et son masque.
- **Couches et mouvements** : `"layers"` (1 à 8 : nom, masque, additive) et `"max_motions"` dans l'`AnimationSet` ; sans eux, les deux couches d'avant. Le mélange généralisé : chaque couche qui remplace prend ses articulations par son poids, ce que laissent les couches au-dessus (de haut en bas). Une sauvegarde à moins de couches se relit (les autres au repos).
- **Fenêtres d'événements** : `"end"` sur un événement : départ et fin (`AnimatorEvent::Phase`), `Animator::in_window()`.
- **Graphe** : ses couches suivent celles de l'animateur (jusqu'à 8) ; une couche additive du graphe joue des réactions, un état peut jouer un clip à root motion (la ruée).
- **La tranche** : compétence 2 = **ruée** (`Dodge_Forward` de KayKit, `root_motion` ×6 : 2 m en 0,4 s, état `ruee` du graphe, ininterruptible), fenêtre `esquive` (aucun coup reçu pendant) ; le coup reçu (`Hit_A`) passe sur une couche **additive** `reactions` : il secoue sans couper la marche ni l'attaque.
- **Écart à la décision** « ruée et réaction sur les clips Quaternius » : les clips KayKit du même squelette ont déjà une esquive avec déplacement de la racine et des réactions ; ceux de Quaternius demanderaient un reciblage sur un autre squelette (un chantier en soi). Leur facteur ×6 compense une esquive courte (0,33 m dans le fichier) ; un vrai clip de ruée viendra avec les assets de l'ARPG.
- **Tests** : courbe et prise du root motion, pose sur place, boucles, sauvegarde, fondu pondéré ; couches remplaçantes et additive ; `max_motions` ; sauvegarde à moins de couches ; fenêtres (début, fin, dedans) ; refus du fichier ; ruée contre un mur identique en rejeu. 562 tests.
- **Références** : tranche **cfe660dd4d6a** (les coups reçus en additif), exactitude **9b7330b11d0f** (A, B et C identiques) ; les autres inchangées.
- **À essayer à la main** : la ruée (touche de la compétence 2) dans la tranche, contre un mur et à travers les squelettes.

### Validation

- Une compétence de ruée avance le personnage selon son animation, s'arrête contre un mur, identique en rejeu ; un coup reçu se superpose à la course sans l'interrompre.

---

## 4. Texte : champ de distance, mise en forme, polices de repli

### But

Un texte **net à toute taille** (4K, Retina, échelle de l'interface choisie par le joueur), avec **contour et ombre** (infobulles, dégâts flottants), et prêt pour **toute langue** (décision 2.2 de la revue : mise en forme toujours).

### Tâches

- [x] **Glyphes en champ de distance signé multicanal** (MSDF) : générés depuis les contours de la police, rangés dans un **atlas dynamique** (les glyphes ajoutés à la demande, pas 236 au chargement) ; contour, ombre et épaisseur dans le shader.
- [x] **Mise en forme** (*shaping*) : crénage, ligatures, positions des marques, écritures complexes (arabe, devanagari) selon les tables OpenType de la police.
- [x] **Texte bidirectionnel** (arabe, hébreu mêlés au latin) et **coupure des lignes** selon les règles Unicode.
- [x] **Polices de repli** : un caractère absent d'une police est cherché dans les suivantes (CJK, symboles) ; le glyphe `.notdef` seulement en dernier recours.
- [x] **Texte enrichi** léger (couleurs, gras, icônes en ligne : les mots-clés d'une infobulle d'objet).
- [x] `TextLayout` et `Font` actuels remplacés ; la tranche et les outils (console, interface de débogage hors ImGui) passent dessus.

### Questions à se poser

- **Lecture des polices et rasterisation : stb_truetype (déjà là), FreeType, ou maison ?** stb_truetype lit les contours TrueType mais pas toutes les tables (CFF partiel *(à vérifier)*). FreeType (licence FTL ou GPL) est la référence. Règle : maison par défaut ; **recommandation : stb_truetype pour les contours, génération MSDF maison** (l'algorithme de Chlumský est publié), FreeType à **proposer** si des polices CFF ou variables sont nécessaires.
- **Mise en forme : maison ou HarfBuzz ?** HarfBuzz (MIT) est la référence, utilisée par les navigateurs et la plupart des moteurs. Écrire un moteur de mise en forme OpenType complet (GSUB, GPOS, écritures complexes) est un très gros travail. Règle : **maison par défaut** ; c'est l'endroit du jalon où la **proposition HarfBuzz** est la plus probable. **Décision de l'utilisateur** au moment de la tâche, avec un essai de la version maison sur le latin, le cyrillique et l'arabe.
  **Décidé (2026-10-17)** : **stb_truetype (contours) + MSDF maison + HarfBuzz** pour la mise en forme (bibliothèque, choix de l'utilisateur : MIT, crédits et licence ajoutés ; paquet vcpkg `harfbuzz` *(à vérifier : options sans dépendances inutiles)*). FreeType reste à proposer si des polices CFF ou variables sont nécessaires.

### Pièges connus

- Les MSDF qui **bavent** aux petites tailles ou aux angles aigus : taille de champ et correction des artefacts (publiée avec l'algorithme).
- L'atlas dynamique qui **déborde** en jeu (beaucoup de glyphes CJK) : plusieurs pages, éviction des glyphes inutilisés.

### Implémentation réalisée (partie 4)

Faite le 2026-10-18 (en autonomie, après le « go » de l'utilisateur sur les recommandations), sous Windows ; le Mac reste à faire avec le reste du jalon.

- **Téléchargements** (accordés par l'utilisateur) : `tools/text/fetch_text_data.py` (SHA-256 vérifiés, hors de Git) : la **base de données Unicode 18.0.0** (la dernière, publiée en 2026 ; 5,7 Mo) et les polices de repli **Noto Sans Arabic** (0,8 Mo) et **Noto Sans SC** (17,8 Mo, plus que les ~10 Mo annoncés), SIL OFL, avec leurs licences. CLDR (partie 5) et le paquet Kenney complet (partie 7) seront pris à leur partie.
- **Unicode maison** : `tools/text/make_unicode_tables.py` génère `unicode_tables.inc` (gardé dans Git : construire ne télécharge rien) ; `moteur/unicode.hpp` (propriétés, **graphèmes** UAX #29, **coupure des lignes** UAX #14) et `moteur/bidi.hpp` (**algorithme bidirectionnel** UAX #9 complet : embeddings, isolats, crochets, L1, L2). Ils passent **tous les tests de conformité officiels** (`tests/test_unicode.cpp` : BidiTest et BidiCharacterTest, plus de 800 000 cas (un sur huit en Debug, tous en Release), LineBreakTest, GraphemeBreakTest), gardés compressés dans `tests/data/unicode/`. Aucune bibliothèque (ni ICU, ni FriBidi, ni libunibreak).
- **HarfBuzz 14.4.0** (vcpkg, sans FreeType, glib ni ICU) : mise en forme de chaque suite (police, écriture, niveau, style) avec **tout le paragraphe en contexte** (l'arabe se lie à travers les suites), langue (`locl`), grappes monotones. **Changement par rapport à la décision** : les contours aussi viennent de HarfBuzz (`hb_font_draw_glyph_or_fail`) au lieu de stb_truetype : il lit déjà la police, et donne les contours **CFF** et ceux des **polices variables à toute graisse** (stb_truetype ignore les variations : Inter et les Noto sont variables). FreeType n'est donc plus à proposer pour le CFF.
- **Champs de distance maison** (`msdf.hpp/.cpp`, d'après la méthode publiée de Chlumský) : coloration des arêtes, pseudo-distances, **vraie distance dans l'alpha** (MTSDF : contours larges et ombres sans les angles du multicanal), signe vérifié par balayage, correction des texels en conflit. **Recouvrements** : la parade de msdfgen (combinaison par contour) laissait des points parasites dans le « e » d'Inter (un seul contour qui se recoupe, courant dans les polices variables) ; à la place, le **bord réel de la forme** est calculé avant (courbes coupées à leurs croisements par subdivision, seuls gardés les morceaux qui ont la forme d'un seul côté, retournés et rechaînés en contours fermés). Les traits superposés des caractères chinois en profitent.
- **Atlas dynamique** (`glyph_atlas.hpp/.cpp`) : pages de 1024² RGBA8 (8 au plus, 32 Mio), 48 texels par em, plage de 12 texels ; glyphes faits **à la demande** sur les fils du `JobSystem`, **48 par image au plus** (un compte et non un temps : une exécution montre toujours les mêmes glyphes à la même image, les captures restent identiques) ; quand tout est plein, la page utilisée il y a le plus longtemps est vidée, jamais une page de l'image en cours. `Renderer::update_texture()` (une partie d'une texture, avec les envois en attente) et `Renderer::frame_index()` sont nouveaux.
- **`Font` remplacée** (`font.hpp`) : une **pile de polices** (`.font.json`, FORMATS 21) ou un fichier seul ; **taille au dessin** (`TextStyle::size`, la taille d'em), plus une police par taille ; `TextStyle` (couleur, **graisse** 100 à 900 vraie sur une police variable, dilatation sinon, **italique** penché, **contour**, **ombre** floue, alignement `Start`/`End` selon le sens, retour à la ligne, interligne, sens, langue, balises, profondeur) ; `layout()` (en cache, 2048 textes, de n'importe quel fil), `measure()`, `draw()` ; **repli par graphème** (la ponctuation reste dans la police du texte qui l'entoure) ; **coupure gloutonne** aux opportunités d'UAX #14 (un mot trop long déborde) ; lignes réordonnées par L1 et L2 ; **balises** légères `<b>`, `<i>`, `<color=…>`, `<size=…>`, `<weight=…>`, `<icon=…>` (syntaxe proche des balises de l'interface à venir). `Assets::font(path)` (plus de taille) partage une `FontLibrary` (fichiers lus une fois, un atlas). Les glyphes passent par un **second genre de quad** du `SpriteRenderer` (pipeline `text.vert/frag`, filtrage linéaire), triés par profondeur avec les sprites : un texte entre deux panneaux reste entre eux.
- **Applications** : bac à sable, tranche (`SliceUi`, la démo 3D) et l'éditeur passent dessus ; piles `assets/fonts/interface.font.json` (Inter, Noto arabe et chinois : le bac à sable) et `assets/fonts/tranche.font.json` (Inter et Noto arabe : la tranche, qui ne livre pas les 17,8 Mo du chinois), `assets_moteur/fonts/default.font.json` (Inter) ; le manifeste de la tranche livre la pile, la police arabe et les licences. La scène **Texte** du bac à sable (`--text`) montre les critères de la partie : titre avec contour et ombre, une phrase de 10 à 200 pixels, les graisses d'Inter, un mot arabe dans une phrase française, un paragraphe arabe (à droite, chiffres de gauche à droite), du chinois trouvé dans la police de repli, une infobulle en balises.
- **Bogue du jalon 9 corrigé en passant** : la texture d'identifiants du contour de sélection (partie 7) était un entier R32 avec l'usage « échantillonneur », refusé par Direct3D 12 (assertion de SDL dans toute exécution Debug). Elle est maintenant lue comme **texture de stockage** (`GRAPHICS_STORAGE_READ`, `tonemap.frag`), ce qui est le bon usage pour un `Load`.
- **Crédits et licences** : HarfBuzz (MIT) et la base Unicode (licence Unicode v3, `assets_moteur/licenses/Unicode-License.txt`, livrée) dans `credits_moteur.json` ; les deux Noto dans `assets/credits.json` (section `fonts`). MSL des nouveaux shaders et du `tonemap.frag` réexporté.
- **Tests** : `test_unicode.cpp` (conformité), `test_text.cpp` (champs d'un carré, d'un cercle, de carrés superposés ; Inter lue par HarfBuzz, axe de graisse ; crénage, retour à la ligne, « ? » et « » » jamais en début de ligne, alignement, cache ; repli chinois, arabe lié et de droite à gauche, ordre d'un mot arabe dans du français ; balises ; icônes ; atlas en mémoire). 532 tests en tout.
- **Références** : toutes les captures avec du texte changent (le même texte, plus net) : 2D **bc4ad4d426b2**, démo 3D **babb3f58f81d**, son rejeu **a09952b867c5**, tranche **88e940f9f739**, exactitude **2bebdc73eb7b** (A, B et C toujours identiques) ; nouvelle capture **texte 2bd273b56328** (la scène `--text`).
- **Mesures** (Release, Windows) : la préparation du latin d'une pile, **199 glyphes en 53 ms** (sur les fils) ; la scène texte entière, fil principal **1,2 ms** par image (p99 1,9 ms ; 3,0 ms au pire, les premières images qui font l'arabe et le chinois).
- **Vérificateur** (Windows, complet) : compilations, tests (Debug, Release, ASan), MSL, analyse statique (0), Doxygen (0), paquet installé et joué : bons. Une fois, juste après la passe Debug, la **première** des deux captures de la tranche a différé (64d4adb38c19 au lieu de 88e940f9f739) ; pas reproduit en 4 vérifications de plus ni en 11 exécutions (niveau repréparé compris) : à surveiller.
- **Restes** : les vérifications sur le **Mac** (le texte, les shaders MSL réexportés) ; le **curseur et la sélection** dans un texte (`TextLayout` garde les grappes et les lignes pour cela : partie 6, les champs de saisie) ; les écritures sans espaces entre les mots (thaï, lao, khmer, birman) ne se coupent qu'aux espaces (il leur faudrait un dictionnaire) ; pas de police de repli pour la dévanagari ni l'hébreu dans le bac à sable (une police Noto de plus le jour où il le faudra).

### Validation

- Le même texte net de 10 à 200 pixels ; un titre avec contour et ombre ; une phrase arabe mêlée de français affichée dans le bon ordre ; un caractère chinois trouvé dans la police de repli.

---

## 5. Localisation

### But

Que le jeu se **traduise** sans toucher au code : tables de chaînes à clés stables, paramètres, pluriels, langue choisie par le joueur (T1).

### Tâches

- [x] **Tables de chaînes** en données, une par langue, par **clé stable** (`ui.options.title`) ; référencées par l'interface (partie 6), les données (noms d'objets, de compétences) et le code du jeu.
- [x] **Paramètres nommés** et **formats** (nombres, pourcentages, durées) selon la langue ; **pluriels** selon les règles CLDR de chaque langue (le français, l'anglais, le russe, l'arabe n'en ont pas le même nombre).
- [x] **Genre et accords** de base (un mot qui s'accorde avec l'objet) *(à cadrer avec le jeu)*.
- [x] **Langue du joueur** : réglage, changement à chaud (l'interface se refait), repli vers la langue de base pour une clé absente (avec avertissement en développement).
- [x] **Vérification à la préparation** : clés absentes, paramètres manquants ou en trop, chaînes non traduites listées par langue.
- [x] **Pseudo-localisation** (texte allongé et accentué) pour trouver les textes en dur et les interfaces trop étroites.
- [x] Les textes du joueur encore dans le moteur passent par là : noms des touches (`input_source_label`, « Clic gauche », « Manette A »), libellés que le moteur fournit au jeu.

### Questions à se poser

- **Format des chaînes** : syntaxe maison, ou inspirée d'**ICU MessageFormat** ou de **Fluent** (Mozilla) pour les pluriels et les sélecteurs ? Recommandation : **une syntaxe inspirée de Fluent** (lisible par un traducteur, pluriels et sélecteurs intégrés), lue par du code maison ; les règles de pluriel tirées des données CLDR (publiques) et générées en table.
  **Décidé (2026-10-17)** : **inspirée de Fluent**, lue par du code maison.
- **Fichiers de traduction** pour un traducteur extérieur (formats PO ou XLIFF) ? Recommandation : un **export / import** vers un format d'échange standard, plus tard ; les tables maison restent la source.

### Pièges connus

- Construire une phrase par **concaténation** (« Vous avez trouvé » + n + « objets ») : intraduisible. Toujours une chaîne entière avec paramètres.
- Les textes qui débordent en allemand ou en russe (30 % plus longs *(ordre de grandeur courant)*) : la pseudo-localisation les montre tôt.

### Implémentation réalisée (partie 5)

Faite le 2026-10-18 (en autonomie), sous Windows.

- **CLDR 48.2.3** (la dernière version stable ; la 49 est en bêta), téléchargé par `fetch_text_data.py` (accord de l'utilisateur ; SHA-256 vérifiés, hors de Git) ; `tools/text/make_locale_tables.py` **compile les règles de pluriel** des 224 langues du CLDR (cardinales et ordinales, syntaxe des opérandes `n i v w f t e`) en fonctions C++ (`private/moteur/locale_tables.inc`, gardé dans Git) et en extrait les **13 786 exemples** officiels (`tests/data/locale/plural_samples.txt`), tous vérifiés par les tests ; les **formats des nombres** (séparateurs, groupement indien, groupement à partir de 5 chiffres de l'espagnol et du polonais, signe et motif du pourcentage) de 29 langues courantes. `moteur/locale.hpp` : `plural_category()`, `PluralOperands`, `format_number()`.
- **Fichiers de chaînes maison, syntaxe de Fluent** (`moteur/localization.hpp`, FORMATS 22) : messages, attributs, termes et leurs attributs (genre), termes à arguments, variables, références, sélecteurs sur la valeur exacte puis la catégorie CLDR, `NUMBER()` (chiffres, groupement, pourcentage, ordinal), `DURATION()`, littéraux et échappements, lignes continuées ; une entrée fautive est sautée et signalée avec son fichier et sa ligne, le reste du fichier gardé. Repli vers la langue de base (avertissement une fois par message en développement), puis `{identifiant}`. Valeurs **isolées** (FSI…PDI) dans une langue de droite à gauche. Différence avec Fluent : l'indentation d'une ligne continuée est ôtée entière (pas seulement l'indentation commune).
- **`Localization` dans l'`Application`** : `assets/locales/languages.json` (langue de base, langues offertes, ressources) et les `.ftl`, rechargés à chaud ; la langue du joueur (variable archivée `language`, sinon la préférence du système parmi les langues offertes), `ApplicationConfig::language` / `--language` pour les vérifications ; `Application::set_language()` ; `generation()` pour qu'une interface refasse ses textes ; console `language [code]`. Le **message de plantage** et celui de la **carte graphique perdue** suivent la langue (`moteur-crash-message`, `moteur-gpu-lost-message`, nouveau `set_crash_message()`).
- **Textes du moteur** sortis du code : les noms des commandes (`input_source_label(source, &localization)`, `Input::describe()`, « (aucune) », sticks) par les messages `input-*` de `assets_moteur/locales/{fr,en}/moteur.ftl` (anglais sans fichier) ; les touches sans caractère (Espace, Entrée, Échap, Maj...) par `input-key-<nom SDL>` ; les durées.
- **La tranche en français et en anglais** : tous ses écrans (titre, chargement, pause, options et leurs pages, sauvegardes, emplacements, commandes, liste défilante) et l'affichage en jeu (dégâts, statistiques, aide) passent par `assets/locales/{fr,en}/tranche.ftl` (176 messages avec ceux du moteur) ; les menus gardent des **identifiants** et les mettent en mots au dessin, leurs choix se comparent aux identifiants (plus au texte affiché) ; nouvelle page **Options > Langue** (les langues par leur propre nom, le changement immédiat) ; la date des sauvegardes dans l'ordre de la langue, la vie en pourcentage, les créatures au pluriel. La traduction anglaise est faite par l'assistant (décision : langue source française).
- **Vérification** : `Localization::check()` (syntaxe, références inconnues : **erreurs** ; message absent, en trop, **variables différentes**, texte identique à la langue de base : avertissements) dans `bac_a_sable --check-data` (lancé par le vérificateur) et dans la **préparation** du paquet (`cook` : une erreur arrête la livraison). Le manifeste de la tranche livre `locales/**`.
- **Pseudo-localisation** (`qps`) : la langue de base accentuée, 40 % plus longue, entre crochets, avec les nombres et pluriels de la base. Essayée sur la tranche : **aucun texte resté en dur** (tout est entre crochets).
- **Tests** : `test_localization.cpp` (tous les exemples CLDR ; pluriels du français, de l'anglais, du russe, de l'arabe ; nombres de 6 langues ; messages, variables, pluriels, termes et genres, attributs, lignes continuées, littéraux, repli ; génération ; erreurs de syntaxe et reprise ; vérification ; pseudo-localisation ; isolation de droite à gauche ; libellés des commandes). Les tests d'entrées attendent maintenant les libellés anglais du moteur sans fichier. 542 tests.
- **Références** : changent (les nombres de l'affichage en jeu groupés à la française, les noms de touches du moteur) : démo 3D **bdac506a1d47**, son rejeu **a11ef1f25496**, tranche **676453a10dd3**, exactitude **8cef206034c0** (A, B et C identiques) ; 2D et texte inchangés. Les captures sont faites en français (`--language fr`), ainsi que la partie jouée du paquet installé.
- **Restes** : l'export et l'import vers un format d'échange (PO, XLIFF) pour un traducteur, plus tard (recommandation du document) ; la tranche ne se dessine pas encore de droite à gauche (aucune langue de droite à gauche offerte) ; le titre de la fenêtre reste celui de la configuration ; les commandes de la console de développement restent en français ou en anglais (outil).

### Validation

- La tranche en **français et en anglais**, la langue changée dans les options sans redémarrer ; la préparation signale une clé absente ajoutée exprès ; la pseudo-localisation ne montre aucun texte en dur.

---

## 6. Système d'interface

### But

Le **système d'interface maison** (décision 2.3) au niveau d'un jeu livré : mode retenu, mise en page automatique, écrans et styles **en données** rechargés à chaud, animations, navigation à la manette, échelle, accessibilité. Il remplace `SliceUi`.

### Tâches

- [x] **Arbre d'éléments en mode retenu** : conteneurs, texte (partie 4), images (9 tranches), boutons, cases, curseurs, listes déroulantes, champs de saisie, listes défilantes, barres (vie, ressource), infobulles, fenêtres.
- [x] **Mise en page automatique** de type *flexbox* (direction, alignement, marges, tailles minimales et maximales, retour à la ligne) ; unités indépendantes de la densité ; **échelle de l'interface** réglée par le joueur.
- [x] **Écrans et styles en données** (format maison, rechargé à chaud) ; styles par classes et états (survolé, focus, désactivé), thèmes.
- [x] **Liaison aux données** du jeu (la vie du héros, la liste des objets) sans réécrire l'écran à chaque image : propriétés observées.
- [x] **Animations et transitions** (ouverture d'un menu, survol, apparition des dégâts flottants), courbes d'interpolation.
- [x] **Focus et navigation** au clavier et à la manette (la direction choisit le voisin le plus proche, ou un ordre déclaré) ; la souris en même temps ; actions du jeu (partie Input du jalon 4).
- [x] **Texte localisé** (partie 5) ; changement de langue à chaud.
- [ ] **Accessibilité** : taille du texte, contrastes, daltonisme (palettes), sous-titres, aucun texte en dur. *Fait : l'échelle 75 à 200 %, aucun texte en dur (vérifié), les thèmes par variables ; reste : palettes pour daltoniens et sous-titres, avec l'audio (partie 7) et l'ARPG.*
- [x] **Rendu** : lots par texture et par couche, découpe (*clipping*), peu d'appels de dessin ; en espace écran et **en espace monde** (barres de vie au-dessus des monstres, noms des objets au sol).
- [x] Les écrans de la tranche (titre, chargement, pause, options, emplacements de sauvegarde, barre de vie) refaits dessus.

### Questions à se poser

- **Format des écrans** : une syntaxe maison de type balises (comme le XAML ou le RML), du JSON, ou du code C++ ? Recommandation : **balises légères + styles de type CSS (sous-ensemble)**, en données, parce que l'éditeur d'écrans du jalon 11 les écrira et qu'un intégrateur les lit facilement. **Décision**.
  **Décidé (2026-10-17)** : **balises + styles de type CSS**, rechargés à chaud.
- **Les outils de développement** (inspecteur, profiler, console) restent-ils en Dear ImGui ? Recommandation : **oui** : ce sont des outils, retirés de la version livrée ; l'interface maison est celle du jeu.

### Pièges connus

- Une mise en page **recalculée à chaque image** pour tout l'arbre : invalidation par élément.
- L'interface qui lit la logique **pendant** un tick : elle lit l'état publié à la fin du tick.
- Des dizaines de barres de vie en espace monde, une par appel de dessin : lots.

### Implémentation réalisée (partie 6)

Faite le 2026-10-18 (en autonomie), sous Windows. Tout maison (aucune bibliothèque ajoutée).

- **Fichiers** (FORMATS 23) : écrans `.ui` (sous-ensemble de XML, lecteur maison qui donne la ligne d'une erreur) et styles `.uss` (sous-ensemble de CSS : sélecteurs d'élément, classe, id, états `:hover :focus :active :disabled :checked :first-child :last-child`, descendant et enfant, spécificité, héritage, variables `var(--x)`, transitions et courbes). Une propriété inconnue est signalée et ignorée. **Rechargés à chaud** (l'élément focalisé garde son focus par son id).
- **`moteur::Ui`** (`moteur/ui.hpp`) : documents par calques (le plus haut **modal** prend les entrées ; un HUD est `modal="false"`), **modèle** (`UiModel` : chemins, versions ; seuls les éléments qui lisent une valeur changée sont refaits ; la mise en page n'est refaite que pour un document qui a changé), **événements** (`Activate`, `Change`, `Back`, `Focus`) lus après le tick. Contrôles : bouton, case, curseur, liste de choix (textes ou liste du modèle), champ de saisie (texte du système, curseur, clic), barre, image (asset, ou texture du jeu par `set_texture()`), zone qui défile, infobulle, listes répétées (`<repeat>`), modèles et **instances** dans l'espace du monde (`instance()`, gardées tant que le jeu les demande à chaque image, dessinées sous le reste du document).
- **Mise en page** *flexbox* : direction, retour à la ligne, `justify-content`, `align-items` et `align-self`, `flex` / `flex-grow` / `flex-shrink` / `flex-basis` (sens de CSS : `flex: 1` partage toute la place), marges dont `auto`, tailles min et max, pourcentages, positions absolues ; un texte qui passe à la ligne dans une rangée grandit sa boîte (hauteurs mesurées à la largeur finale). Points × échelle (densité de l'écran × choix du joueur).
- **Navigation** dans le **tick**, par les actions du jeu (`UiInput::from_actions()` : `menu_*`, `pause`, le pointeur ; un rejeu rejoue les menus) : voisin le plus proche dans la direction, voisins imposés (`nav-*`), `autofocus`, tour en haut et en bas, flèches gauche et droite sur les curseurs et les listes, clic, glisser, molette ; le focus qui sort d'une zone qui défile la fait défiler. Le focus d'ouverture n'envoie pas d'événement (pas de son à chaque menu).
- **Dessin** : boîtes à coins arrondis et bordures en **champ de distance** (une forme de l'atlas des glyphes : nettes à toute taille, dans le même lot que le texte), neuf tranches, découpe des zones qui défilent, opacité, décalage et échelle hérités, barres de défilement. Environ **150 barres de vie et le HUD en 2 appels de dessin** (démo 3D à 3 000 créatures).
- **Transitions** sur le temps des ticks (les captures et les rejeux ne dépendent pas de la vitesse de la machine).
- **La tranche** : `SliceUi` **retiré** ; chaque état ouvre son document (`assets/ui/` : titre, chargement, pause, options, affichage, question, audio, commandes, attente d'une touche, langue, emplacements, HUD ; thème `tranche.uss`), remplit le modèle, réagit aux actions ; les vignettes des sauvegardes sont des textures données à l'interface ; le HUD de `Demo3D` (chiffres, aide, **barres de vie**, **mots des coups** qui montent et s'effacent) est `hud.ui` (la démo seule du bac à sable a sa propre interface). Nouvelle option **Taille de l'interface** (75 à 200 %, variable archivée `ui.scale`). Les écrans utilisent la police livrée de la tranche (sans chinois) ; le manifeste livre `ui/**`.
- **Vérifications** : `check_ui_files()` dans `--check-data` et dans la **préparation** (une erreur arrête la livraison) ; `bac_a_sable --ui-tour DOSSIER` (chaque menu capturé, problèmes comptés : documents, styles, messages absents) lancé en **français, anglais et pseudo-localisation** par `check_references.py` (les captures montrent les sauvegardes et réglages du joueur : gardées pour être regardées, pas comparées). La pseudo-localisation a montré deux défauts corrigés : textes à la ligne qui débordaient de leurs boutons, boutons de largeurs inégales.
- **Tests** : `test_ui.cpp` (balises, erreurs, sélecteurs, cascade, héritage, variables, transitions, `flex`, couleurs et longueurs, modèle, vérification des fichiers). 551 tests.
- **Références** : changent (le HUD dessiné par l'interface, ses barres et ses mots) : démo 3D **a8985fe0709a**, son rejeu **1cf5a9fce754**, tranche **d93fe94fc292** (aussi celle du jeu installé), exactitude **9e2e2cca401d** (A, B et C identiques après 120 et 250 ticks : l’ordre de dessin des barres suit l’ordre des demandes de l’image, pas leur apparition) ; 2D et texte inchangés.
- **Restes** : palettes pour daltoniens et sous-titres (avec l'audio et l'ARPG) ; fenêtres déplaçables, glisser-déposer, inventaire et arbre de passifs : à l'ARPG, sur ce socle ; l'éditeur d'écrans au jalon 11 ; une interface de droite à gauche (miroir des rangées) quand une telle langue sera offerte.

### Validation

- Les écrans de la tranche refaits en données ; un style changé dans un fichier se voit sans redémarrer ; tout se navigue à la manette ; l'échelle de 75 à 200 % reste lisible ; 200 barres de vie à l'écran en un petit nombre d'appels de dessin.

---

## 7. Audio par événements

### But

Les **événements audio** maison décidés le 2026-10-16 (4.1), avec une API calquée sur les concepts de **FMOD Studio**, pour que le jeu ne joue jamais un fichier directement et que la migration vers FMOD reste un changement de moteur audio. L'**outil de création sonore** est au jalon 11.

### Tâches

- [x] **Événements** en données, par nom ou identifiant (`combat/epee/impact`) : pistes, sons choisis au hasard ou en séquence, variations de volume et de hauteur, boucles, limites d'instances et vol de voix par priorité.
- [x] **Paramètres** d'événement (locaux et globaux : `matiere = os`, `intensite_combat`) qui choisissent les sons et règlent volume, hauteur, filtres.
- [x] **Bus, VCA et instantanés** (*snapshots*) de mixage : la pause baisse la musique, un dialogue baisse le reste (*ducking*), les réglages du joueur sont des VCA.
- [x] **Spatialisation** : auditeur, atténuation par courbe, occlusion par la géométrie (rayons, partie 5 du [jalon 9](JALON_9_NIVEAU_PRODUCTION.md)), **zones de réverbération** (crypte, extérieur) ; effets (filtre passe-bas, réverbération) écrits maison.
- [x] **Musique interactive** : transitions sur la mesure, couches selon l'intensité.
- [x] **Banques** préparées (partie 4 du [jalon 9](JALON_9_NIVEAU_PRODUCTION.md)) dans les archives ; flux pour la musique.
- [x] La tranche et `Audio::play` direct remplacés par des événements (l'API par fichiers reste interne au moteur).

### Questions à se poser

- **Format des événements** : calqué sur la structure de FMOD Studio (événements, pistes, instruments, paramètres) *(à vérifier : la structure exacte des projets FMOD Studio)* pour faciliter une migration. Recommandation : **oui**, mêmes concepts et mêmes noms.

### Pièges connus

- Les événements joués **par la logique** au tick et rejoués : l'audio est de la présentation ; le rejeu peut les rejouer, il ne doit jamais en dépendre.
- Des centaines d'instances d'un même impact en combat : limites par événement et par bus.

### Implémentation réalisée (partie 7)

Faite le 2026-10-18 (en autonomie), sous Windows. Tout maison sur miniaudio (aucune bibliothèque ajoutée).

- **Banques** (FORMATS 25) : `SoundBank` (`moteur/sound_bank.hpp`), lecteur strict (clé inconnue, bus, paramètre, label ou plage faux : erreur qui nomme la banque et l'endroit), `Assets::sound_bank()` rechargé à chaud ; la **préparation suit ses fichiers** (le manifeste de la tranche ne liste plus les sons, seulement `audio/banks/*.bank.json` et les licences) ; elle part en CBOR dans l'archive `audio`.
- **`moteur::AudioEvents`** (`moteur/audio_events.hpp`), API calquée sur FMOD Studio : `play(nom, {position, paramètres, labels})`, `stop`, `stop_all(préfixe)`, `set_parameter` (nombre ou label), `set_global`, `start_snapshot` / `stop_snapshot`, `set_vca`, `set_reverb_zones`, `set_occlusion_test`, `update(dt)`, `instances()` et `stats()` pour le débogage. Les sons viennent d'`Assets`, ou de chargeurs donnés (`AudioEvents::Loaders` : les tests).
- **Événements** : sons au hasard, mêlés (*shuffle*) ou en séquence, **aiguillage** par un paramètre à labels (les pas selon `sol`), plages de volume et de hauteur, boucle, flux, fondus, **limites d'instances** avec vol (plus ancienne, plus lointaine, ou refus) et priorité.
- **Paramètres** locaux et globaux, nombres ou labels ; **automations** (volume, hauteur, passe-bas) par courbes linéaires.
- **Mixage** (`Audio`) : arbre de **bus** (`add_bus`, sous les quatre groupes du joueur) en `ma_sound_group` ; **VCA** et **instantanés** en fondu (la pause : musique à 35 %, effets et ambiance en pause) ; un **passe-bas** par voix (`ma_lpf_node`, pour l'occlusion et les automations) ; une **réverbération maison** (Freeverb de Jezar : 8 filtres en peigne amortis, 4 passe-tout, par canal) en nœud sur les bus de base choisis, réglée par la **zone** de l'auditeur avec fondu aux bords.
- **Occlusion** : le test du jeu (la tranche : segment de l'oreille à la source contre la collision du niveau, `segment_clear`), toutes les 4 images par instance, lissé ; un mur laisse 45 % du volume et coupe au-dessus de 900 Hz.
- **Musique interactive** : sections choisies par un paramètre, **départ et arrêt programmés à l'échantillon** (`PlaySound::start_at`, `Audio::stop_at`, horloge `time_frames()`), changement sur la mesure (ou le temps) suivante, fondu enchaîné, **couches** en phase dont le volume suit un paramètre.
- **La tranche** : `tranche.bank.json` (pas selon le sol : pavés dans la rue, dalles sur le parvis ; impacts métalliques et coups reçus du paquet **Kenney Impact complet** ; ambiance des tombes ; musique `titre` et `jeu` sur la mesure ; clics de l'interface), zones **rue** et **parvis**, occlusion par le niveau ; aucun fichier joué directement par son code. L'**auditeur suit la caméra** (il restait à l'origine : bogue corrigé). La scène d'animation du bac à sable passe aussi par la banque.
- **Tests** : `test_audio_events.cpp` (lecture et refus des banques, courbes, lecture par nom, limites et vol, aiguillage, bus, instantané, VCA, zone et queue de réverbération, occlusion, changement de section sur le temps), mixés sans périphérique. 572 tests.
- **Restes** : couches de musique avec des pistes séparées (Ableton, plus tard ; le code est là, la tranche n'en a pas) ; atténuation par courbe dessinée (aujourd'hui : min / max de miniaudio) ; *ducking* d'un dialogue (un instantané le fait ; pas de dialogue dans la tranche) ; panneau de débogage des instances ; l'outil de création sonore au jalon 11.

### Validation

- Les sons de la tranche passent par des événements : les pas changent selon le sol (paramètre), la crypte réverbère, la pause baisse la musique par un instantané ; aucun fichier joué directement par le code de la tranche.

---

## 8. La tranche refaite : interface, texte, sons

### But

Prouver le jalon : la tranche du jalon 9 avec la présentation de production, livrée et vérifiée.

### Tâches

- [x] Les écrans de la tranche (titre, chargement, pause, options, emplacements de sauvegarde, barre de vie, dégâts flottants) refaits avec le système d'interface (partie 6), en **français et en anglais** (partie 5) ; `SliceUi` retiré.
- [x] Les sons de la tranche par **événements** (partie 7) ; aucun fichier joué directement par son code.
- [x] Un matériau transparent (fantôme ou eau), la dissolution des monstres à leur mort, l'éclair à l'impact (partie 2).
- [x] Une compétence à root motion (ruée) et une réaction additive aux coups (partie 3).
- [ ] Rejeu, captures de référence (nouvelles, documentées), mesures sur le PC et le Mac M3, paquet installé et joué sur les deux OS ; documentation à jour. *Fait sous Windows ; reste le Mac.*

### Implémentation réalisée (partie 8)

Faite le 2026-10-18 (en autonomie), sous Windows. La tranche a été refaite au fil des parties 6, 2, 3 et 7 ; cette partie la vérifie de bout en bout.

- **Ce que la tranche montre** : écrans et HUD du système d'interface, en **français et en anglais** (Options > Langue), à l'échelle choisie ; texte en champ de distance ; **fantôme** transparent sur le parvis ; **éclair** à chaque coup, **dissolution** des morts après un temps au sol ; **ruée** à root motion (compétence 2, arrêtée par les murs) et **réaction additive** aux coups ; **sons par événements** (pas selon le sol, impacts, ambiance, musique sur la mesure, réverbération de la rue et du parvis, pause par instantané) ; aucun fichier joué directement par son code.
- **Livraison** : le vérificateur complet passe (Debug, Release, ASan, clang-tidy, Doxygen, exemple externe, données, références, politique d'erreur, paquet installé, joué par son rejeu et désinstallé). Le paquet emporte la banque et **exactement les 23 sons qu'elle nomme** (la préparation les suit), HarfBuzz et sa licence (`licenses/harfbuzz.txt`, crédits du moteur). `--check-data` vérifie aussi les banques.
- **Références** (Windows) : 2D **bc4ad4d426b2**, démo 3D **a8985fe0709a**, son rejeu **1cf5a9fce754**, tranche **cfe660dd4d6a** (aussi celle du jeu installé), texte **2bd273b56328**, matériaux **76967a84d651**, exactitude **9b7330b11d0f** ; inchangées par la partie 7 (le son n'est pas dans les images).
- **Mesures** (Release, Windows, RTX 4070 Ti SUPER, 1920 × 1080, `build/measures/j9bis_p8`, comparées au jalon 9) : combat : fil principal **1,66 ms** (jalon 9 : 2,1), p99 2,38 ms, GPU **2,57 ms** (4,4) ; traversée : 1,24 ms, p99 1,92 ms, GPU 2,28 ms ; foule de 800 : 4,06 ms ; **aucune image au-delà de 33 ms** ; chargement de la tranche **975 ms** au pire de 10 cycles (1,5 s), objets GPU stables d'un cycle à l'autre ; processus 1,05 Go, GPU 454 Mo (budgets 2 Go et 1,5 Go). Le GPU du combat baisse sur toutes ses passes, ombres comprises, que le jalon n'a pas touchées : c'est l'état de la carte pendant la mesure, pas un gain ; ce qui compte est l'absence de coût. **Coût des effets de matériaux** : la passe de scène du combat (les branches d'effets dans chaque instance, la passe transparente) reste à 1,75 ms (2,05 au jalon 9) ; la scène `--materials`, qui les concentre (fantôme, vitres, dissolutions, lave), coûte 0,14 ms de GPU pour sa passe de scène. Pas de permutation à ajouter.
- **Restes** : tout ce qui demande le **Mac** (compilation des shaders par le port, vérificateur, mesures sur le M3, `.app` et `.dmg` : [TEST_MAC.md](TEST_MAC.md)) ; les essais à la main de l'utilisateur (ci-dessous, critères).

### Validation

- Voir les critères de fin de jalon.

---

## 9. Critères de fin de jalon

Le jalon est terminé quand **tout** ce qui suit est vrai :

- [ ] Les **matériaux** ont la transparence triée et des variantes ; le **Mac compile ses shaders** (plus de MSL versionné). *Fait sous Windows ; le port du Mac est écrit, pas essayé.*
- [x] L'**animation** a le root motion (déterministe, au tick) et les couches additives.
- [x] Le **texte** est net à toute taille (champ de distance), mis en forme par HarfBuzz, avec polices de repli et texte bidirectionnel.
- [x] La **localisation** fonctionne : français et anglais, changement à chaud, vérification à la préparation, pseudo-localisation ; plus aucun texte du joueur dans le moteur.
- [x] L'**interface** de la tranche est faite avec le système maison, en balises et styles rechargés à chaud, navigable à la manette, à l'échelle choisie par le joueur. *La manette est passée par les actions du jeu ; l'essai à la main reste à faire.*
- [x] Les sons passent par des **événements** (paramètres, bus, instantanés, réverbération).
- [ ] La **tranche** refaite se livre, s'installe et se joue sur Windows et macOS ; le verifier passe sur les deux ; les budgets tiennent sur le Mac M3. *Windows : fait (partie 8) ; le Mac reste.*
- [x] HarfBuzz est dans les crédits et sa licence dans les paquets ; la documentation est à jour ; les décisions sont consignées.

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
| Contours des glyphes | **HarfBuzz** (`hb_font_draw_glyph_or_fail`) au lieu de stb_truetype ; FreeType n'est plus utile | Décidé en autonomie à la partie 4 (2026-10-18) : la bibliothèque choisie lit déjà la police ; elle donne le CFF et les graisses des polices variables (Inter, Noto), que stb_truetype ignore |
| Bidirectionnel, coupure des lignes, graphèmes | **Maison**, tables générées depuis l'UCD, vérifiés par les tests de conformité officiels | Recommandation (règle des bibliothèques), validée par l'utilisateur le 2026-10-18 avec les téléchargements |
| Version d'Unicode | **18.0.0** (la dernière) | Décidé à la partie 4 : publiée en 2026, ses règles sont celles des annexes en ligne |
| Polices de repli | **Noto** (SIL OFL) : Sans Arabic et Sans SC, hors de Git (`fetch_text_data.py`) ; la tranche ne livre que l'arabe | Validé par l'utilisateur le 2026-10-18 |
| Balises du texte enrichi | `<b>`, `<i>`, `<color=…>`, `<size=…>`, `<weight=…>`, `<icon=…>`, entités `&lt;` `&gt;` `&amp;` ; une balise inconnue reste visible | Décidé en autonomie (2026-10-18) : proche des balises de l'interface (partie 6) |
| Langue source | **Français** ; l'anglais traduit par l'assistant (partie 5) | Validé par l'utilisateur le 2026-10-18 |
| Données CLDR | **48.2.3** (dernière stable) ; règles de pluriel compilées en C++ pour toutes les langues, formats des nombres pour 29 langues, chiffres latins | Décidé en autonomie à la partie 5 (2026-10-18) |
| Identifiants des messages | Style de Fluent, **tirets** (`options-title`), pas de points (le point désigne un attribut) | Décidé à la partie 5 : la syntaxe de Fluent l'impose |
| Préférence de langue | Variable archivée `language` (`variables.cfg`) ; vide : la langue du système parmi celles offertes | Décidé à la partie 5 |
| Mac (parties 2 et 8) | Tout fait sous Windows, port vcpkg de `SDL_shadercross` écrit **sans essai** ; le MSL versionné reste jusqu'à la session Mac | Validé par l'utilisateur le 2026-10-18 |
| Assets des validations | Fantôme transparent fait par l'assistant (pipeline Blender), ruée et réaction sur les clips Quaternius ; pas selon le sol avec le paquet Kenney complet ; musique : transitions d'abord, couches avec des pistes séparées d'Ableton plus tard | Validé par l'utilisateur le 2026-10-18 |
| Localisation : syntaxe des chaînes | **Inspirée de Fluent**, lue par du code maison ; règles de pluriel tirées de CLDR | Recommandation validée par l'utilisateur (2026-10-17) |
| Format des écrans d'interface | **Balises + styles de type CSS**, rechargés à chaud | Recommandation validée par l'utilisateur (2026-10-17) |
| Animation : ruée et réaction | Clips **KayKit** (`Dodge_Forward` à root motion ×6, `Hit_A` additif), pas Quaternius | Décidé en autonomie à la partie 3 (2026-10-18) : même squelette, pas de reciblage ; écart à la décision du 2026-10-18 |
| Animation : root motion | Courbe en **millimètres entiers par tick**, calculée au chargement de la bibliothèque (pas à l'import) ; plan horizontal seulement | Décidé à la partie 3 : déterministe sans changer le format des modèles préparés ; la hauteur reste dans la pose |
| Matériaux : variantes | **Pas de permutations** : effets en branches uniformes par instance dans un shader ; permutation seulement pour un effet coûteux, mesuré | Décidé en autonomie à la partie 2 (2026-10-18) : peu d'instructions, aucun temps de compilation ni explosion des variantes |
| Matériaux : émission transparente | Pondérée par l'alpha (prémultipliée) | Décidé à la partie 2 : comportement attendu des moteurs (Filament) ; les lueurs indépendantes sont des particules |
| Shaders du Mac | `shadercross` construit sur le Mac (port maison), DXC du **SDK Vulkan de LunarG** | Décidé à la partie 2 : `directx-dxc` de vcpkg n'existe pas sur macOS ; le SDK Vulkan livre DXC pour le Mac |
| Interface : sens de `flex` | Celui de CSS (`flex: 1` = base 0) ; longueurs en points × échelle ; transitions au temps des ticks | Décidé en autonomie à la partie 6 (2026-10-18) : un intégrateur qui connaît CSS n'est pas surpris ; les captures restent exactes |
| Interface : où vit l'état des écrans | Le jeu écrit un **modèle** (chemins) et lit des **événements** ; un écran ne lit jamais le jeu ; un document par état de la pile | Décidé à la partie 6 : l'interface lit l'état publié, jamais la logique pendant un tick (piège du document) |
| Audio : format des banques | **Concepts et noms de FMOD Studio** (événements, paramètres à labels, bus, VCA, instantanés, quantification) dans un JSON maison ; un événement n'a qu'un instrument (sons, aiguillage, ou musique) | Décidé en autonomie à la partie 7 (2026-10-18), selon la recommandation du document ; les pistes multiples d'un événement viendront avec l'outil de création (jalon 11) |
| Audio : réverbération et filtres | **Freeverb** (domaine public) réécrit, en nœud miniaudio ; passe-bas de miniaudio par voix | Décidé à la partie 7 : la réverbération de miniaudio n'existe pas, Freeverb est la référence simple et connue |
| Audio : occlusion | Un **test donné par le jeu** (0 à 1), appelé toutes les 4 images par instance et lissé | Décidé à la partie 7 : le moteur audio ne connaît pas la collision ; le coût reste borné |
| Audio : musique | Sections **programmées à l'échantillon** sur l'horloge du mixeur (50 ms d'avance) | Décidé à la partie 7 : une transition à l'image près s'entend |
| Éditeur de matériaux à nœuds, éditeur d'écrans, outil de création sonore | Au **jalon 11** | Plan validé (2026-10-16) |

