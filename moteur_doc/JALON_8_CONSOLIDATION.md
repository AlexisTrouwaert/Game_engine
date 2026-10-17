# Jalon 8 - Consolidation

Retour à la [roadmap générale](ROADMAP.md). Jalon précédent : [Jalon 7 - Outils et données](JALON_7_OUTILS_ET_DONNEES.md). Description des fichiers existants : [Fichiers du projet](FICHIERS_DU_PROJET.md).

## Objectif

Faire du moteur un **produit** : qu'il se **construise et se teste tout seul** sur Windows et macOS à chaque envoi, qu'il soit une **bibliothèque** que le projet ARPG consomme sans rien copier, qu'il **tienne ses budgets** sur la machine minimale (et pas seulement sur la machine de développement), qu'il produise une **version livrable** (assets préparés et regroupés, outils de développement retirés, plantages enregistrés) **installable et signée** sur les deux OS, et qu'il soit **documenté** assez pour qu'on écrive un jeu avec lui sans relire ses sources.

À la fin du jalon, la **tranche jouable** doit exister comme un **programme livré** : un exécutable `tranche` (sans ImGui, sans rechargement à chaud, sans scènes de test), ses assets **préparés** (textures BC7 / BC5, tables et cartes compilées) dans une **archive**, un **installeur** sous Windows et un **`.app`** dans une image disque sous macOS (signature et notarisation prêtes, activées quand les comptes existeront) ; installé sur une machine **qui n'a jamais vu le projet**, il se lance, se joue du titre au retour au titre, sauvegarde et recharge, tient **60 images par seconde** sur le Mac M3 dans la scène la plus chargée, et un plantage provoqué laisse un **rapport** exploitable (pile d'appels avec noms de fonctions). Le script de vérification montre le même résultat sur les deux OS.

**Niveau d'exigence** : comme au jalon 7, ce moteur sert un **jeu livrable**, pas un prototype. Chaque recommandation vise la solution de **production** (celle des studios quand on la connaît), même si elle coûte plus de travail.

**Pourquoi maintenant ?** Les jalons 1 à 7 ont construit les systèmes ; le moteur n'a encore **jamais été livré**, ni **construit ailleurs** que sur les deux machines de l'utilisateur, ni **mesuré** ailleurs que sur une RTX 4070 Ti SUPER. La dette Mac s'accumule (les vérifications des jalons 2 à 7 attendent dans [TEST_MAC.md](TEST_MAC.md)). Le projet ARPG va commencer : la frontière moteur / jeu doit être nette **avant** que du code de jeu ne s'y appuie, et chaque choix de packaging (archive, chemins, CRT, signature) coûte plus cher à changer une fois le jeu écrit.

## Prérequis

- Le [jalon 7](JALON_7_OUTILS_ET_DONNEES.md) est terminé et commité (`7cba197`).
- **Comptes et certificats** : **repoussés** (questionnaire du 2026-10-05) : pas d'Apple Developer Program, pas de signature Windows, pas de CI hébergée dans ce jalon. Tout est préparé pour s'activer le jour où ils existeront.
- Le **Mac M3** disponible pour les mesures de la partie 4 et les vérifications de la partie 12.
- Une **machine Windows « propre »** (ou une machine virtuelle Windows 11 vierge, ou Windows Sandbox) pour essayer l'installeur.
- Outils (pas des bibliothèques liées au programme) : **Doxygen** pour la documentation, **Inno Setup** ou WiX selon la décision de la partie 10, les outils Xcode (`codesign`, `notarytool`, `stapler`, `hdiutil`) sur le Mac.

## Comment lire ce document

Même structure que les jalons précédents :

- **But** : ce qu'on cherche à obtenir.
- **Tâches** : ce qu'il faut faire, dans l'ordre.
- **Questions à se poser** : les décisions à trancher avant ou pendant le travail. À noter dans la section [Décisions à consigner](#décisions-à-consigner) une fois tranchées.
- **Pièges connus** : erreurs fréquentes.
- **Validation** : comment savoir que la partie est terminée.

Les points marqués *(à vérifier)* sont des informations dont je ne suis pas certain ou qui évoluent vite (prix, noms de services, politiques d'Apple et de Microsoft). Il faut les confirmer dans la documentation actuelle avant de s'appuyer dessus.

## Sommaire

1. [Vue d'ensemble et ordre de travail](#1-vue-densemble-et-ordre-de-travail)
2. [Intégration continue Windows et macOS](#2-intégration-continue-windows-et-macos)
3. [Le moteur comme bibliothèque](#3-le-moteur-comme-bibliothèque)
4. [Profilage : machine minimale, scènes de mesure et budgets](#4-profilage--machine-minimale-scènes-de-mesure-et-budgets)
5. [Optimisation : chargement en arrière-plan, pics et mémoire](#5-optimisation--chargement-en-arrière-plan-pics-et-mémoire)
6. [Version livrée : configuration et préparation des assets](#6-version-livrée--configuration-et-préparation-des-assets)
7. [Archive des assets](#7-archive-des-assets)
8. [Robustesse : plantages, affichage, analyses](#8-robustesse--plantages-affichage-analyses)
9. [Documentation de l'API](#9-documentation-de-lapi)
10. [Packaging Windows](#10-packaging-windows)
11. [Packaging macOS : signature et notarisation](#11-packaging-macos--signature-et-notarisation)
12. [La tranche livrée et le rattrapage Mac](#12-la-tranche-livrée-et-le-rattrapage-mac)
13. [Critères de fin de jalon](#13-critères-de-fin-de-jalon)
14. [Risques principaux](#14-risques-principaux)
15. [Décisions à consigner](#décisions-à-consigner)

---

## 1. Vue d'ensemble et ordre de travail

### Où on part

Après le jalon 7 :

- **Build** : CMake + vcpkg (manifeste, port maison d'ozz-animation, triplet Mac maison), presets `windows-debug/release` et `macos-debug/release`. Construit **à la main** sur deux machines ; sous Windows, le preset `windows-debug` échoue hors de CLion (toolchain vcpkg de Visual Studio), on passe par un script local. **Pas d'intégration continue.** Les shaders sont compilés en DXIL sous Windows ; sous macOS, le MSL **exporté depuis Windows** et versionné est copié (le port `sdl3-shadercross` ne s'installe pas sur Mac).
- **Tests** : 389 tests unitaires (doctest) ; captures de référence vérifiées par `tools/tests/check_references.py` (2D, démo 3D, rejeu, tranche, exactitude de la sauvegarde) ; `--check-data` ; auto-test de l'éditeur. Tout se lance à la main, sous Windows.
- **Frontière moteur / jeu** : le moteur est une bibliothèque statique `moteur` (76 sources, 72 en-têtes dans `include/moteur/`), consommée par le **bac à sable** et l'**éditeur** du même dépôt. Le projet ARPG n'a encore aucun code. Rien n'est « installable » (pas de cible exportée, pas de version du moteur).
- **Mesures** : profiler maison et Tracy, `--report`, `--gpu-timing`, `--skinned N`, `--meshes N`. Toutes les mesures ont été faites sur **une** machine (RTX 4070 Ti SUPER) : tranche **1,10 ms** de CPU par image, démo 3D 1,20 ms ; plafond d'environ 1 600 personnages animés ; chargement de la tranche **770 ms**, 40,8 Mo d'assets. **Rien n'est mesuré sur le Mac M3**, qui est pourtant la machine minimale décidée au jalon 3.
- **Chargement** : tout est chargé **sur le fil principal** (le chargement en arrière-plan a été laissé de côté au jalon 4 : aucun chargement ne dépassait une seconde). Le transcodage UASTC → BC7 / BC5 se fait **à chaque chargement** (10 à 40 ms par modèle) ; les tables ont une forme binaire (`--compile-data`) que rien n'utilise encore.
- **Livraison** : les assets sont **copiés en vrac** à côté de l'exécutable ; les chemins partent de `SDL_GetBasePath()` ; les licences des bibliothèques sont copiées dans `licenses/` et les crédits affichés dans « À propos ». ImGui, le rechargement à chaud (efsw), la console et le profiler sont **dans chaque exécutable**. Pas d'icône, pas de ressource de version, pas de signature, pas de `.app`. Un plantage ne laisse **rien** à part le journal (`journal.txt`, aujourd'hui `log.txt`).
- **Affichage** : une fenêtre redimensionnable ; **pas** de plein écran, pas de choix de résolution ni de moniteur ; synchronisation verticale en option de ligne de commande ; anticrénelage et résolution de rendu réglables dans les panneaux de debug.
- **Documentation** : un document par jalon et [FICHIERS_DU_PROJET.md](FICHIERS_DU_PROJET.md) (1 500 lignes, fichier par fichier) ; les en-têtes ont des commentaires en anglais, sans format commun. **Pas de référence de l'API** ni de guide « écrire un jeu avec le moteur ».
- **Dette** : vérifications Mac des jalons 2 à 7 ; tranche à la manette (jalon 4) ; particules douces et plusieurs scènes jamais lancées sur Metal.

Les principes restent ceux de la roadmap : le moteur ne connaît rien de l'ARPG. Ce jalon n'ajoute **pas de fonctionnalité de jeu** ; il rend livrable, mesurable et documenté ce qui existe.

### Dépendances entre les parties

```
2. Intégration continue ------------------------------------------+
            |                                                     |
3. Le moteur comme bibliothèque                                   |
            |                                                     |
            +----------------------+                              |
            |                      |                              |
4. Profilage (mesures)    6. Version livrée : configuration       |
            |                et préparation des assets            |
5. Optimisation                    |                              |
            |             7. Archive des assets                   |
            |                      |                              |
            |             8. Robustesse (plantages, affichage)    |
            |                      |                              |
            |             10. Packaging Windows                   |
            |             11. Packaging macOS ----------------------+  (la CI produit les paquets)
            |                      |
            +----------+-----------+
                       |
            9. Documentation de l'API   (au fil de l'eau, finie après 3)
                       |
            12. La tranche livrée et le rattrapage Mac
```

- La **CI** vient en premier : elle construit le Mac à chaque envoi et arrête la dette de grossir. Elle produira ensuite les paquets.
- La **bibliothèque** (partie 3) fixe ce qui est public ; la **documentation** (partie 9) s'écrit sur cette surface-là, pas avant.
- Les **mesures** (partie 4) précèdent toute **optimisation** (partie 5) : on n'optimise que ce que la machine minimale montre.
- La **configuration livrée** (partie 6) précède l'**archive** (partie 7), qui précède les **paquets** (parties 10 et 11).

### Ce qui peut se faire en pause du moteur

- **Comptes et certificats** : l'inscription à l'Apple Developer Program et la validation d'identité pour la signature Windows prennent des jours, voire des semaines *(à vérifier)* : à lancer **dès le début** du jalon.
- **Côté ARPG** : le squelette du dépôt du jeu (CMake qui consomme le moteur, partie 3), et la bibliothèque `gameplay` sans moteur (calcul des stats de [CONCEPTION_STATS.md](../../ARPG/ARPG_doc/CONCEPTION_STATS.md)) qui peut avancer en parallèle.
- **Documentation** : le guide « écrire un jeu avec le moteur » et la référence des formats de fichiers.

### Estimation indicative

Pour un dev solo à temps partiel. À prendre comme un ordre de grandeur, pas comme un engagement.

| Partie | Estimation |
|---|---|
| 2. Intégration continue | 1 à 2 semaines |
| 3. Le moteur comme bibliothèque | 1 à 2 semaines |
| 4. Profilage : machine minimale, scènes, budgets | 1 à 2 semaines |
| 5. Optimisation (chargement en arrière-plan compris) | 3 à 4 semaines |
| 6. Configuration livrée et préparation des assets | 2 semaines |
| 7. Archive des assets | 1 à 2 semaines |
| 8. Robustesse (plantages, affichage, analyses) | 2 à 3 semaines |
| 9. Documentation de l'API | 2 semaines |
| 10. Packaging Windows | 1 semaine |
| 11. Packaging macOS | 1 à 2 semaines |
| 12. Tranche livrée et rattrapage Mac | 1 à 2 semaines |
| **Total** | **environ 4 à 5 mois** |

### Liens avec les autres jalons

- **Rendu 3D** (jalon 3) : la compression des textures avait été décidée avec un **transcodage d'avance** au packaging ; c'est la partie 6. La **résolution de rendu** et l'**anticrénelage** deviennent des réglages du joueur (partie 8).
- **Systèmes de base** (jalon 4) : le **chargement en arrière-plan** laissé de côté est fait en partie 5 ; `read_file()`, prévue « pour qu'une archive puisse la remplacer », est remplacée en partie 7 ; **retirer ImGui de l'exécutable**, renvoyé au jalon 8, est la partie 6.
- **Animation** (jalon 5) : les clips peuvent partir **préparés** (archives ozz) plutôt que reconstruits depuis le glTF à chaque chargement (partie 6, si la mesure le justifie).
- **Outils et données** (jalon 7) : la **forme binaire** des tables part dans la version livrée ; le **profiler** sert aux mesures de la partie 4 et est **retiré** de la version distribuée ; la console reste, **fermée**, limitée aux commandes d'outil.
- **Côté ARPG** : à la fin du jalon, le dépôt du jeu consomme le moteur comme une bibliothèque versionnée, et son packaging (jalon 7 de l'ARPG, « Options, menus, packaging ») réutilise tel quel celui de la tranche.

### Questions générales

- **Faut-il ajouter des fonctionnalités ?** Non, sauf ce qu'exige une version livrable : plein écran et options d'affichage, rapport de plantage, chargement en arrière-plan. Le post-traitement (bloom, occlusion ambiante), le TAA et le culling sur GPU restent dans « Plus tard » de la roadmap, sauf si les mesures de la partie 4 montrent qu'un budget ne tient pas sans eux.
- **Le bac à sable est-il livré ?** Non. Il reste l'outil de développement (scènes de test, captures, mesures). La version livrée est un **exécutable à part**, `tranche`, qui préfigure celui de l'ARPG (partie 12).
- **Où vit le travail de packaging : moteur ou jeu ?** Le **moteur** fournit les outils (préparation des assets, archive, fonctions CMake de paquet, rapport de plantage, options d'affichage) ; le **jeu** fournit son nom, son icône, ses identifiants et ses certificats. La tranche est le premier client.

---

## 2. Intégration continue Windows et macOS

> **Décision du 2026-10-05 : la CI hébergée est repoussée.** Dans ce jalon, la partie se réduit à un **script de vérification local** (`tools/ci/verify.py`, Python, lancé à la main sur Windows et sur le Mac) qui enchaîne exactement les étapes ci-dessous ; le workflow GitHub Actions viendra plus tard et n'aura qu'à l'appeler. Les tâches propres à GitHub (workflow, cache binaire, badge) restent écrites pour ce moment-là.

### But

À chaque envoi sur GitHub, **construire** le moteur, le bac à sable, l'éditeur et les outils sur **Windows et macOS**, en Debug et en Release, sans avertissement ; **lancer** les tests unitaires, `--check-data`, `--compile-data` et l'auto-test de l'éditeur ; plus tard (parties 10 et 11) **produire les paquets**. Le Mac ne peut plus prendre de retard sans qu'on le voie.

### Tâches

- [ ] Un **workflow GitHub Actions** avec une matrice Windows (`windows-2022` ou plus récent) et macOS **arm64** (`macos-14` ou plus récent : processeurs Apple Silicon) *(à vérifier : noms des images en vigueur)*.
- [ ] **vcpkg en cache binaire** : le premier build (SDL3, ktx, DXC de shadercross, ozz) prend longtemps ; sans cache, chaque envoi le refait. Le cache des paquets binaires de vcpkg se branche sur le cache d'Actions ou sur GitHub Packages (NuGet) *(à vérifier : le fournisseur `x-gha` a été retiré de vcpkg ; voir la documentation actuelle)*.
- [ ] Faire fonctionner le **preset `windows-debug` hors de CLion** (toolchain de vcpkg via `VCPKG_ROOT`) : la CI doit utiliser exactement les presets du dépôt, comme un développeur.
- [ ] **Avertissements = erreurs en CI** (option CMake, éteinte par défaut en local pour ne pas gêner le travail).
- [ ] Étapes : configurer, construire, `ctest`, `bac_a_sable --check-data`, `--compile-data` dans un dossier temporaire, `editeur ... --self-test`.
- [ ] **Vérifier le MSL versionné** : une étape Windows régénère le MSL (`export_msl_shaders`) et **échoue** si le résultat diffère des fichiers du dépôt (shader modifié sans réexport : aujourd'hui, le Mac le découvre au prochain passage).
- [ ] Les **assets de test non versionnés** (modèles Poly Haven, personnages KayKit, sons, HDRI) : les scripts `fetch_*` les téléchargent ; les mettre en cache d'Actions par empreinte du script, pour ne pas solliciter les sites à chaque envoi.
- [ ] Badge d'état dans le `README.md`.
- [ ] **Dans ce jalon** : `tools/ci/verify.py [--quick]` fait les étapes ci-dessus (configurer avec les presets, construire Debug / Release / livré, tests, données, éditeur, MSL à jour sous Windows, références sous Windows) et imprime un résumé ; code de sortie non nul au premier échec.

### Questions à se poser

- **GitHub Actions, autre service, ou machine à soi ?** Recommandation : **GitHub Actions**, le dépôt y est déjà ; les machines macOS hébergées sont en Apple Silicon. Coût : gratuit pour un dépôt **public** ; pour un dépôt **privé**, les minutes macOS comptent **10 fois** dans le quota gratuit *(à vérifier)*. Si le quota ne suffit pas, le Mac de l'utilisateur peut servir de **runner auto-hébergé** pour l'étape macOS seulement.
- **Les captures de référence en CI ?** Les machines hébergées n'ont **pas de GPU** sous Windows ; sous macOS, Metal est disponible dans la machine virtuelle mais avec un GPU paravirtualisé *(à vérifier)*. Recommandation : **pas de captures en CI** dans ce jalon (elles dépendent du GPU et du pilote) ; elles restent un contrôle local avant chaque commit de jalon, par `check_references.py`. À réexaminer si le rendu logiciel WARP de Direct3D 12 est utilisable par SDL_GPU *(à vérifier)*.
- **Quand la CI tourne-t-elle ?** À chaque envoi sur toutes les branches et sur les pull requests ; les **paquets** seulement sur les étiquettes de version (`v0.8.0`) et à la demande, pour économiser les minutes.

### Pièges connus

- Les **fins de ligne** : `.gitattributes` existe, mais les fichiers de référence (cartes ASCII, JSON attendus des tests) doivent rester en `LF` sur les deux OS, sinon les comparaisons octet à octet échouent sous Windows.
- Le **code page** de MSVC en français a déjà cassé les dépendances d'en-têtes de Ninja (jalon 4) : la CI est en anglais, ce qui masque le problème au lieu de le résoudre. Garder la vérification `ninja -t deps` dans les notes.
- **Chemins avec espaces** : le dossier local s'appelle `Moteur + ARPG` ; la CI n'en a pas. Garder un test local avec espaces et accents.
- Le cache vcpkg doit être **indexé par le triplet, le baseline et les ports maison** (`ports/`, `triplets/`), sinon un port modifié reprend un vieux binaire.

### Validation

- **Dans ce jalon** : `verify.py` passe sur Windows et sur le Mac ; un avertissement introduit exprès le fait échouer ; un shader modifié sans réexport du MSL aussi.
- **Plus tard (CI)** : un envoi construit et teste les deux OS en moins de 30 minutes avec le cache ; mêmes échecs provoqués.

### Implémentation réalisée (partie 2)

- **`tools/ci/verify.py`** (Python 3, sans dépendance) : pour chaque configuration (`debug`, `release` ; `--quick` : `release` seule ; `--configs` pour choisir), configure avec **le preset du dépôt** et `-DMOTEUR_WARNINGS_AS_ERRORS=ON`, construit, vérifie les dépendances d'en-têtes (Windows), lance `ctest`, `--check-data` et `--compile-data` ; puis l'**exemple extérieur** (partie 3), le **MSL** (Windows) et `check_references.py` (captures, exactitude, auto-test de l'éditeur). Résumé avec les durées, code de sortie non nul au premier échec (`--keep-going` pour tout voir). Les étapes à venir (configuration livrée, paquets) s'y ajoutent au fil du jalon.
- **Environnement préparé par le script** : sous Windows, l'environnement de Visual Studio (trouvé par `vswhere`, `vcvars64.bat`), CMake et Ninja du `PATH` ou de CLion ; vcpkg pris dans `MOTEUR_VCPKG_ROOT`, puis celui de CLion, puis `VCPKG_ROOT`. C'était la cause de l'échec du preset `windows-debug` hors de CLion : l'invite de Visual Studio met dans `VCPKG_ROOT` **sa propre copie** de vcpkg. Les presets du dépôt fonctionnent maintenant en ligne de commande (`build/windows-debug` créé par le script).
- **`MOTEUR_WARNINGS_AS_ERRORS`** (option CMake, éteinte par défaut, `/WX` ou `-Werror` sur les cibles du projet).
- **MSL** : le script exporte le MSL et **échoue** si un fichier change (le fichier régénéré reste, à commiter).
- **Piège trouvé et fermé** : MSVC en français écrit « Remarque : inclusion du fichier : » avec un espace insécable dont l'**octet dépend de la page de code de la console** ; CMake l'a détecté en page 850, et un build lancé depuis une autre console (Python, Git Bash) faisait enregistrer à Ninja **zéro dépendance d'en-tête**, sans erreur (objets périmés après une modification d'en-tête : le « stack around mask corrupted » du jalon 4). Le script passe la console en page 850 et **vérifie après chaque build** qu'aucun objet n'a zéro dépendance (`ninja -t deps`). *(Revu le 2026-10-16, partie 8 : console propre à chaque commande au lieu de `chcp`, et outils de CLion en premier.)* VSLANG=1033 ne sert à rien sans le pack de langue anglais de Visual Studio.
- Première exécution complète (Windows, 2026-10-05) : tout passe, captures inchangées (2D `bd91e8b2c616`, démo 3D `fdc076d9c3ae`, rejeu `952477744ffb`, tranche `f54c9067ab40`, exactitude `063b80d5f127`).

---

## 3. Le moteur comme bibliothèque

### But

Que le **dépôt de l'ARPG** consomme le moteur **sans copier de fichiers** et sans connaître sa cuisine interne (shaders, assets du moteur, dépendances vcpkg), avec une **surface publique** décidée et une **version** du moteur.

### Tâches

- [ ] Décider le **mode de consommation** (voir les questions) et l'outiller : fonctions CMake réutilisables (`moteur_add_game(<cible> ...)` qui branche shaders, assets, licences et paquet), aujourd'hui écrites pour `bac_a_sable` seulement.
- [ ] **Séparer les shaders du moteur** de ceux du bac à sable : aujourd'hui `moteur_add_shaders(bac_a_sable ...)` liste les shaders du moteur dans la cible du bac à sable. Le moteur doit fournir ses shaders lui-même ; un jeu n'ajoute que les siens.
- [ ] **Assets du moteur** (police de l'interface de debug, textures de repli, `credits.json` des bibliothèques) séparés des **assets du jeu** : deux racines, l'archive les fusionne.
- [ ] **Surface publique** : relire `include/moteur/` ; ce qui est détail d'implémentation va dans `src/moteur/` (privé) ou un espace `moteur::detail`. Exemple déjà fait : ozz est lié en `PRIVATE`. Vérifier qu'aucun en-tête public n'expose une bibliothèque qui devrait rester privée.
- [ ] **Version du moteur** : `moteur::version()` (majeur.mineur.correctif, plus l'empreinte du commit et la date du build, injectées par CMake), écrite dans le journal au démarrage et dans les rapports de plantage. Jalon 8 terminé = `0.8.0`.
- [ ] Un **projet minimal** (`examples/jeu_minimal/` : une fenêtre, un cube, une entrée, un son) construit **depuis l'extérieur** du dépôt du moteur, comme le fera l'ARPG ; la CI le construit.
- [ ] Le **squelette du dépôt ARPG** (CMake qui consomme le moteur, exécutable vide) : fait avec l'utilisateur, à la fin de la partie.

### Questions à se poser

- **Comment l'ARPG consomme-t-il le moteur ?** Trois façons :
  - **sous-module git** du moteur dans le dépôt du jeu, puis `add_subdirectory` : une seule construction, on débogue dans les deux, on **épingle** une version précise du moteur ; c'est ce que font la plupart des studios avec un moteur maison (moteur et jeu dans le même arbre de sources) ;
  - **port vcpkg** du moteur (registre ou port en overlay) : propre, mais chaque changement du moteur demande de republier, pénible quand on développe les deux à la fois ;
  - **dossiers voisins** (`../moteur`) : simple, mais rien n'épingle la version, et la CI du jeu doit savoir où est le moteur.

  Recommandation : **sous-module git + `add_subdirectory`**, avec des **étiquettes de version** sur le moteur. On travaille sur les deux à la fois, le jeu fixe exactement la version qu'il utilise, et la CI du jeu récupère tout d'un coup. Les dépendances vcpkg du moteur passent par le manifeste du jeu (qui reprend celui du moteur) *(à vérifier : la meilleure façon de composer deux manifestes vcpkg)*.
- **Bibliothèque statique ou dynamique ?** Recommandation : **statique**, comme aujourd'hui (un seul exécutable, pas de problème d'ABI, l'optimisation à l'édition de liens fonctionne) ; le rechargement de code à chaud n'est pas un objectif.
- **Stabilité de l'API ?** Il n'y a qu'un client : pas de promesse de compatibilité binaire. Mais une **note de version** par étiquette (ce qui a changé, comment migrer), pour que l'ARPG avance de version en connaissance de cause.

### Pièges connus

- Les définitions de compilation **publiques** (`MOTEUR_ASSETS_SOURCE_DIR`, `MOTEUR_TRACY`) fuient dans le jeu : chacune doit être décidée publique ou privée, et nommée par cible.
- `CMAKE_SOURCE_DIR` dans les fonctions CMake du moteur pointe vers le **jeu** quand le moteur est un sous-dossier : utiliser `CMAKE_CURRENT_FUNCTION_LIST_DIR` / `PROJECT_SOURCE_DIR`.
- Les **options** du moteur (tests, Tracy) ne doivent pas s'imposer au jeu : préfixées `MOTEUR_` et éteintes quand le moteur n'est pas le projet principal (`PROJECT_IS_TOP_LEVEL`).

### Validation

- `examples/jeu_minimal` se construit hors de l'arbre du moteur, sur les deux OS en CI, en ne nommant que la cible `moteur` et la fonction `moteur_add_game` ; le dépôt ARPG vide fait de même.

### Implémentation réalisée (partie 3)

- **Le moteur, projet ou sous-dossier** : `CMakeLists.txt` ne prend la main sur vcpkg et le langage que s'il est le projet principal ; `project(moteur VERSION 0.8.0)` ; `MOTEUR_ROOT` (variable interne) remplace `CMAKE_SOURCE_DIR` dans toutes les fonctions CMake ; options `MOTEUR_BUILD_SANDBOX`, `MOTEUR_BUILD_EXAMPLES`, `MOTEUR_BUILD_TESTS` allumées seulement quand le moteur est le projet. La cible `moteur` porte elle-même C++20 (`cxx_std_20`) et `/utf-8` (publics : les sources du jeu en ont besoin).
- **`moteur_add_game(<cible> [NAME ...] [VERSION ...] [PUBLISHER ...] [ICON ...] [ASSETS ...] [SHADER_DIR ... SHADERS ...] [LICENSES ...] [SHARE_OUTPUT_OF <autre>])`** (`cmake/Game.cmake`) : shaders du moteur (`MOTEUR_ENGINE_SHADERS`, listés dans `cmake/Shaders.cmake`) et du jeu, assets du moteur puis du jeu fusionnés dans `assets/`, licences des bibliothèques du moteur (`MOTEUR_LICENSE_PACKAGES`) et du jeu, DLL de PIX hors version livrée. Le bac à sable et l'éditeur l'utilisent (`SHARE_OUTPUT_OF` : l'éditeur partage le dossier du bac à sable). `moteur_add_shaders` prend un `SOURCE_DIR` (MSL dans `<dossier>/generated/msl`), `moteur_add_assets` plusieurs dossiers. *Aujourd'hui (parties 10 et 11) : `NAME` (le nom vu par les joueurs, défaut : la cible), `VERSION` (x.y.z, défaut : celle du projet), `PUBLISHER` et `ICON` (chemin sans extension : `.ico` sous Windows, `.icns` sous macOS) donnent sous Windows les ressources de l'exécutable (icône, informations de version, manifeste, modèles dans `cmake/windows/`) et servent à `moteur_package` (`cmake/Package.cmake`).*
- **Assets du moteur** : `assets_moteur/` (la police Inter de l'interface de debug, `credits_moteur.json` : bibliothèques et police) ; `assets/credits.json` ne garde que les assets du bac à sable. La lecture des crédits passe dans le moteur (`moteur/credits.hpp` : `Credits::load({fichiers})`, filtres de plateforme et d'option), pour la tranche et le jeu.
- **Surface publique** : `ktx_texture.hpp`, `screenshot.hpp`, `debug_ui.hpp` (inclus par aucun en-tête public ni par le bac à sable) passent dans `src/moteur/private/` ; les tests y ont accès. Les autres en-têtes restent publics : ceux qui pourraient être internes (`audio_rules`, lots de dessin, `asset_cache`) sont inclus par des en-têtes publics ; leur rôle est signalé dans la référence (partie 9).
- **Version** : `moteur::version()` (« 0.8.0 »), `build_commit()` (« 7cba197+ » : `+` si l'arbre a des modifications non commitées), `build_commit_date()`, `build_description()` (« moteur 0.8.0 (7cba197+, 2026-10-04, Release) ») ; écrite en tête du journal. `build_info.cpp` est produit à chaque build par `cmake/BuildInfo.cmake` et **réécrit seulement s'il change** (rien n'est recompilé sinon).
- **`examples/jeu_minimal`** : un cube éclairé tourné aux flèches ou au stick, un bip (fichier généré, sans licence) sur Espace ou A, Échap pour quitter, ses liaisons dans ses assets. Construit dans le projet du moteur **et de l'extérieur** avec ses propres presets (`MOTEUR_DIR`, manifeste vcpkg du moteur par `VCPKG_MANIFEST_DIR`, triplets du moteur) : 9 s de configuration (paquets vcpkg servis par le cache binaire local), 35 s de build.
- **Squelette du dépôt ARPG** (`ARPG/` : `CMakeLists.txt`, presets, `src/main.cpp`, `assets/input/arpg.json`, `README.md`, `.gitignore`) : une fenêtre vide sur le moteur, construite et lancée. Le moteur est pris dans `extern/moteur` (le futur sous-module) ou, à défaut, dans le dossier voisin `../moteur`. Le dépôt git du jeu et le sous-module restent à créer par l'utilisateur (commande dans le README).

---

## 4. Profilage : machine minimale, scènes de mesure et budgets

### But

Savoir, **chiffres à l'appui**, si le moteur tient ses budgets sur la **machine minimale**, dans les **pires cas** d'un ARPG (combats chargés), et pas seulement en moyenne : les **pics** (images longues), la **mémoire** et les **temps de chargement** comptent autant.

### Tâches

- [ ] **Fixer la configuration minimale et recommandée** (voir les questions) et l'écrire dans la roadmap.
- [ ] **Scènes de mesure** reproductibles (rejouées par fichier d'entrées, sans interaction), lancées par un seul script sur les deux OS :
  - **combat chargé** : la tranche avec 300 monstres à l'écran, 30 effets de particules actifs (proche du budget de 20 000), 32 lumières ponctuelles dont 4 ombrées, le héros au milieu ;
  - **foule** : `--skinned 800` au zoom de jeu ;
  - **traversée** : la caméra parcourt toute la carte de la tranche (culling, ombres, brouillard qui se découvre) ;
  - **chargement** : titre → partie → titre → chargement d'une sauvegarde, dix fois (temps et mémoire).
- [ ] Chaque scène produit un **rapport** (`--report` étendu) : CPU par image (moyenne, 99ᵉ centile, maximum), temps GPU par passe, nombre d'images au-delà de 16,7 ms et de 33 ms, mémoire du processus (pic), mémoire GPU allouée par le moteur, temps de chargement ; en **JSON** pour comparer deux versions par script.
- [ ] **Mesures sur le Mac M3** (Release, plein écran Retina, avec et sans résolution de rendu réduite) et sur la machine Windows, rangées dans ce document.
- [ ] **Mémoire GPU** : compter ce que le moteur alloue (textures, tampons, cibles de rendu) par catégorie, visible dans DEBUG ; SDL_GPU ne donne pas la mémoire du pilote *(à vérifier)*.
- [ ] Capturer une image du combat chargé dans **RenderDoc** (Windows) et dans le **débogueur Metal d'Xcode** (Mac) : coût par passe, surdessin des particules.
- [ ] Une étape de CI (ou un script local) qui **compare** le rapport à celui de la version précédente et signale une régression de plus de 10 %.

### Questions à se poser

- **Quelle machine minimale ?** Le jalon 3 a fixé **Mac M3, 60 images par seconde**. Il manque l'équivalent Windows. Recommandation : **minimale** = carte graphique de la classe GTX 1060 6 Go / RX 580, processeur 4 cœurs / 8 fils (classe Core i5 de 8ᵉ génération), 8 Go de mémoire, Windows 10 64 bits, SSD, en 1080p à 60 images par seconde avec les réglages bas ; **recommandée** = la classe RTX 3060, 16 Go, 1440p, réglages hauts. C'est l'ordre de grandeur des ARPG récents à leur sortie *(à vérifier sur les fiches de Path of Exile 2 et Last Epoch)*. Problème : l'utilisateur n'a pas de telle carte. Options : emprunter ou acheter une carte d'occasion ; à défaut, **le Mac M3 fait office de machine minimale** (son GPU est proche de cette classe) et le PC sert de machine recommandée.
- **Quels budgets ?** Recommandation, sur la machine minimale, dans le combat chargé :
  - **16,7 ms par image** au 99ᵉ centile (60 images par seconde tenues, pas seulement en moyenne) ;
  - **CPU du fil principal ≤ 8 ms** (logique ≤ 3 ms par tick, préparation du rendu ≤ 5 ms) pour garder de la marge au jeu, qui n'a encore rien ajouté ;
  - **aucune image au-delà de 33 ms** pendant le jeu (chargements de zone exceptés, derrière un écran de chargement) ;
  - **mémoire** ≤ 2 Go pour le processus, ≤ 1,5 Go de GPU ;
  - **chargement** d'une zone ≤ 5 s depuis un SSD, retour au titre ≤ 1 s.
- **Mesurer la machine minimale sans la posséder ?** Brider la machine de développement (fréquence du GPU, résolution de rendu) ne reproduit ni la bande passante mémoire ni les pilotes : c'est un indice, pas une mesure. Le Mac est une **vraie** machine plus faible : il est la référence tant qu'il n'y en a pas d'autre.

### Pièges connus

- Mesurer en **Debug** ou avec la validation du GPU : chiffres faux (rappel du README).
- La **synchronisation verticale** masque le coût réel : mesurer sans, juger le ressenti avec.
- Les **moyennes** cachent les pics : un ARPG se juge sur le 99ᵉ centile et les images longues.
- Le **Mac en Retina** dessine quatre fois plus de pixels que le 1280×720 des captures : mesurer à la résolution réelle de l'écran.
- Le profiler maison **allumé** pendant une mesure de référence : l'éteindre (`profile.enabled 0`), même si son coût mesuré est dans le bruit.

### Validation

- Les quatre scènes de mesure tournent par un seul script sur les deux OS et produisent leur rapport JSON ; les chiffres du Mac M3 et du PC sont dans ce document, avec les budgets tenus ou non.

### Implémentation réalisée (partie 4)

- **Configuration minimale** (décision du 2026-10-05) : le **Mac M3** est la machine minimale **mesurée** ; la cible Windows écrite est la classe GTX 1060 / RX 580, 4 cœurs, 8 Go, 1080p à 60 images par seconde en réglages bas ; le PC de développement (RTX 4070 Ti SUPER) est la machine recommandée.
- **Rapport de mesure du moteur** : `ApplicationConfig::report_json_path` (`--report-json FICHIER` dans le bac à sable) écrit à la fin de l'exécution : CPU par image et **intervalle entre images** (moyenne, médiane, 99ᵉ centile, maximum), images au-delà de 16,7 et 33,3 ms, phases (logique, enregistrement, envoi), toutes les **zones du profiler**, compteurs par image (appels de dessin, dont ceux des ombres ponctuelles, triangles, Ko envoyés), temps GPU par passe (avec `--gpu-timing`), **mémoire du processus** (actuelle et **pic**, `process_peak_memory_bytes()`), **mémoire GPU du moteur** par catégorie (textures, cibles de rendu, tampons ; actuelle et pic), plus les **valeurs du jeu** (`Application::report_value(nom, valeur)` : temps de chargement, objets après chaque cycle). Le GPU et la version du moteur sont en tête.
- **Mémoire GPU** : chaque `GpuResource` créé avec une taille la compte (`gpu_memory()`, calculée depuis les formats avec `SDL_CalculateGPUTextureFormatSize`, niveaux, couches et échantillons compris ; le pilote ajoute ses alignements, que SDL_GPU ne donne pas). Affichée dans DEBUG > Assets avec l'état des pools de géométrie (partie 5).
- **Scènes de mesure** (`--mesure combat|traversee` dans la tranche, `Demo3D::Options::measure`) : **combat** = les 300 créatures sur une spirale autour du héros invulnérable qui frappe sans arrêt, 30 feux et 32 lumières (4 ombrées) autour ; **traversée** = le héros parcourt toute la carte en serpentin (20 points). Plus **foule** (`--skinned 800`) et **chargement** (`--states --states-cycles 10`, qui donne les temps de chargement et les objets GPU de chaque cycle).
- **`tools/measures/measure.py`** : lance les quatre scènes (`fight`, `crowd`, `crossing`, `loading` depuis le 2026-10-17 ; `combat`, `foule`, `traversee`, `chargement` avant) en Release, sans synchronisation, en 1920×1080 par défaut (`--size`), une seconde fois avec `--gpu-timing` pour le temps GPU par passe ; écrit un rapport par scène et `summary.json` ; affiche chaque **budget** tenu ou dépassé ; `--compare DOSSIER` signale toute mesure **plus de 10 % moins bonne** qu'une mesure précédente (code de sortie 1).
- **Premiers chiffres (PC, Release, 1920×1080, avant la partie 5)** : combat **9,05 ms** de CPU par image en moyenne (p99 11,0), dont **7,4 ms** dans « maillages : préparation » ; traversée 1,71 ms ; chargement de la tranche 890 ms avec une étape de **347 ms** (aucune image ne doit dépasser 33 ms : écran de chargement figé). Le reste dans les budgets. Mesures du Mac M3 : dans [TEST_MAC.md](TEST_MAC.md) (à faire sur le Mac).

---

## 5. Optimisation : chargement en arrière-plan, pics et mémoire

### But

Tenir les budgets de la partie 4 sur la machine minimale. **On n'optimise que ce que les mesures montrent** ; trois chantiers sont toutefois certains, parce qu'un jeu livré en a besoin quels que soient les chiffres : le **chargement en arrière-plan**, la suppression des **pics** (images longues) et la **maîtrise de la mémoire**.

### Tâches

- [ ] **Chargement en arrière-plan** :
  - lecture des fichiers, décodage (glTF, KTX2, images, sons, tables) sur un **fil de travail** ; création des ressources GPU et envoi des données sur le **fil principal**, par petites tranches à chaque image (budget en ms) ;
  - `Assets` donne une poignée **en cours de chargement** (le repli existant s'affiche en attendant) et un état « prêt » ; `LoadingState` attend que tout soit prêt et affiche une progression réelle ;
  - le rechargement à chaud et le ramasse-miettes restent sur le fil principal ;
  - la **logique** ne lit jamais un asset en cours de chargement (déterminisme : un rejeu ne doit pas dépendre de la vitesse du disque).
- [ ] **Pics** : chercher toute création de ressource GPU, de pipeline ou d'allocation importante **pendant le jeu** (première apparition d'un effet, d'un monstre, d'un son) ; tout précharger au chargement de la zone. Mesurer la **première** image de chaque nouveauté (le pilote compile parfois à la première utilisation d'un pipeline *(à vérifier selon le pilote et Metal)*).
- [ ] **Mémoire** : budget par catégorie (textures, maillages, sons, animations, particules), affiché dans DEBUG ; libération à la sortie d'une zone vérifiée (le cycle de 100 états du jalon 4, refait avec la tranche et le chargement en arrière-plan).
- [ ] Puis, **seulement si la partie 4 le demande** : candidats connus, par ordre de probabilité :
  - **résolution de rendu** par défaut plus basse sur Retina (déjà réglable), et les ombres des lumières ponctuelles en qualité réglable ;
  - **lumières** : au-delà de 32, découpage de l'écran (*forward+*, décision du jalon 3) ;
  - **animation** : échantillonnage des poses sur plusieurs fils (décision « pas de multithreading » du jalon 5, à revoir sur mesure) ;
  - **particules** : tri et envoi vers le GPU plus économes, ou simulation sur GPU.
- [ ] Refaire les mesures de la partie 4 après chaque optimisation ; noter avant / après ; captures de référence inchangées ou changements expliqués.

### Questions à se poser

- **Un système de tâches (*job system*) ?** Le chargement en arrière-plan demande au moins **un** fil de travail ; l'animation et les particules pourraient en profiter. Recommandation : **un petit système de tâches maison** (file de tâches, N fils, attente d'un groupe), assez simple pour l'écrire nous-mêmes ; le chargement l'utilise tout de suite, les systèmes de jeu **seulement si une mesure le demande**, et sans jamais rendre l'ordre des calculs de la logique dépendant des fils. Alternative : la bibliothèque **enkiTS** (zlib, dans vcpkg *(à vérifier)*), si le nôtre devient compliqué.
- **Que faire si le Mac M3 ne tient pas 60 images par seconde à pleine résolution Retina ?** Recommandation : **résolution de rendu par défaut à 75 %** sur Retina (la 3D, pas l'interface, comme prévu au jalon 3) plutôt que de baisser la qualité de tout ; MetalFX / FSR restent « Plus tard ».

### Pièges connus

- Un **asset partagé** demandé deux fois pendant son chargement : une seule lecture, deux poignées vers le même résultat.
- **SDL_GPU depuis un fil de travail** : les copies et créations de ressources se font sur le fil qui possède le périphérique *(à vérifier : ce que SDL_GPU autorise réellement entre fils)* ; garder le GPU sur le fil principal.
- Le **rechargement à chaud** pendant un chargement en arrière-plan : le désactiver le temps du chargement, ou le mettre en file.
- Une optimisation qui change l'**ordre des calculs** de la logique casse les rejeux : les tests d'exactitude et les captures de référence le diront.

### Validation

- La tranche se charge derrière un écran qui **reste fluide** (progression animée à 60 images par seconde) ; aucune image au-delà de 33 ms pendant le jeu dans les scènes de mesure ; les budgets de la partie 4 sont tenus sur le Mac M3 (ou les écarts restants sont expliqués et acceptés par l'utilisateur) ; rejeux et captures inchangés.

### Implémentation réalisée (partie 5)

Ce que les mesures de la partie 4 ont montré, corrigé dans l'ordre :

- **Signatures des ombres ponctuelles** (combat : 5,9 ms sur 9) : la signature qui décide si l'ombre d'une lumière doit être redessinée hachait **octet par octet** les palettes d'os de chaque personnage à portée, et la même palette autant de fois que le personnage a de pièces. Hachage par mots de 64 bits, et une palette une seule fois par lumière (les pièces d'un personnage se suivent) : **9,05 → 2,70 ms** de CPU par image dans le combat chargé (p99 11,0 → 3,8). Zones du profiler ajoutées dans la préparation des maillages (lots de la scène, de l'ombre, ombres ponctuelles, signatures).
- **Envois au GPU** : chaque maillage ou texture créé envoyait sa copie dans son propre tampon de commandes puis **attendait que le GPU soit inactif** (`SDL_WaitForGPUIdle`), environ 1 ms chacun. Les envois sont maintenant **groupés** (`Renderer::stage_upload`) : pages de transfert de 16 Mo partagées, un seul tampon de commandes, envoyé avant celui de l'image suivante (ou à la fin de l'image en cours), sans attente. Les pages forment un **anneau** : réutilisées une fois leur barrière GPU franchie (au plus 4 gardées), sinon le pilote gardait les pages libérées et la mémoire du processus montait de 30 Mo par rechargement.
- **Pool de géométrie** (`moteur/geometry_pool.hpp`) : sous Direct3D 12, chaque tampon GPU est une ressource à part (environ 0,33 ms de création, mémoire arrondie à 64 Ko) ; les 400 petits tampons du sol de la tranche coûtaient 130 ms. Les maillages prennent maintenant une **tranche** (`GpuSlice`) de pages de 8 Mo (premier emplacement libre, alignement 256 octets, emplacements fusionnés à la libération), un maillage de plus de 2 Mo garde un tampon à lui ; une tranche libérée n'est **réutilisée qu'après 3 images** (le GPU peut encore la lire). `draw_batch` lie les tampons à leur décalage. Objets GPU de la tranche : **583 → 75**.
- **Système de tâches** (`moteur/jobs.hpp` : `JobSystem`, `submit`, `wait` qui aide à vider la file, `parallel_for`, `jobs()` du processus) ; règles : jamais la logique du jeu, jamais le GPU, les caches d'assets ni les registres. Premier usage : le **préfiltrage des environnements** ligne par ligne (même résultat au bit près) : ciel procédural de la tranche **86 → 17 ms**.
- **Chargement en arrière-plan** : `Assets::prefetch_model / texture / skeleton / clips / sound` lisent et décodent les fichiers sur les fils de travail (glTF, tangentes, images et KTX2 des matériaux décodés comme le cache des textures les demandera, squelettes et clips ozz, sons) ; l'appel habituel (`model()`…) sur le fil principal ne fait plus que créer les objets GPU, ou attend le décodage s'il n'est pas fini. `Assets::prefetching()` compte ce qui reste ; les préchargements jamais pris partent au ramasse-miettes.
- **Étapes qui attendent** : `LoadingState::Step::until` ; l'écran reste animé pendant l'attente. Les ticks d'attente sont des **ticks qui ne comptent pas** (`Game::waiting()`, relayé par la pile d'états, la démo des états et le menu du bac à sable) : sans entrée, sans commande de console, ni enregistrés ni rejoués, puisque leur nombre dépend de la machine. Le tick qui voit l'attente finie enchaîne l'étape suivante : un chargement a **autant de ticks comptés** qu'avant (la capture de la tranche, qui passe par les menus, est inchangée : `f54c9067ab40`). Test unitaire.
- **Chargement de la tranche** : **890 → 245 ms** au premier chargement, 222 ms ensuite ; plus longue étape **50 ms** (la construction du monde, du code de la tranche, 310 ms avant) ; personnages 332 → 36 ms sur le fil principal.
- **Mémoire** : 10 cycles titre → jeu → titre, objets GPU stables (70), mémoire du processus stable à 2 Mo près.
- **Mesures après la partie 5** (PC, Release, 1920×1080, `tools/measures/measure.py`) :

| Scène | CPU moyenne / p99 / max (ms) | GPU (ms) | Processus pic | GPU pic |
|---|---|---|---|---|
| Combat chargé | 2,70 / 3,76 / 6,07 | 2,53 | 322 Mo | 116 Mo |
| Foule (800 animés) | 4,80 / 6,20 / 7,38 | — | 289 Mo | 84 Mo |
| Traversée | 1,35 / 2,09 / 4,43 | 1,52 | 303 Mo | 113 Mo |
| Chargement (10 cycles) | tranche 245 ms au plus | — | stable | 113 Mo |

- **Pas fait, et pourquoi** : le multithreading des systèmes de jeu (aucune mesure ne le demande : le fil principal est à 2,7 ms dans le pire cas sur le PC) ; le *forward+* (32 lumières suffisent à la tranche) ; les particules sur GPU (20 000 tiennent). À revoir avec les mesures du Mac M3.

---

## 6. Version livrée : configuration et préparation des assets

### But

Une **configuration de compilation livrée** sans outils de développement, et une étape de **préparation des assets** (*cooking*) qui transforme les sources de développement en données prêtes à charger : rien n'est converti au lancement du jeu livré.

### Tâches

- [ ] **Configuration livrée** : option CMake `MOTEUR_SHIPPING` (ou preset `*-shipping`), qui retire :
  - **Dear ImGui** et toutes les fenêtres de `DebugTools` (le code n'est pas compilé, pas seulement désactivé) ;
  - le **profiler** (macros vides) et Tracy ;
  - le **rechargement à chaud** (efsw non lié) et les écritures vers les sources d'assets ;
  - les **commandes de console de jeu** (triches) ; la console d'outil reste selon la décision du jalon 7 ;
  - les vérifications coûteuses de debug, mais **pas** les assertions qui protègent les données du joueur (sauvegarde).
  Avec optimisation à l'édition de liens (LTCG / LTO) et symboles de debug **séparés** (PDB, dSYM) gardés pour les rapports de plantage.
- [ ] **Préparation des assets** (`moteur_cook`, outil en ligne de commande construit avec le moteur, appelé par CMake et la CI) :
  - **textures** : KTX2 UASTC → **BC7 / BC5 prêts** (décision du jalon 3), PNG des modèles écartés quand un KTX2 existe ;
  - **tables** : forme binaire `.mdat` (jalon 7) ; **cartes** et **descriptions d'animation** : CBOR, comme les tables, si la mesure du chargement le justifie ;
  - **clips d'animation** : archives ozz prêtes à lire, si la mesure le justifie (aujourd'hui reconstruits depuis le glTF à chaque chargement) ;
  - **shaders** : les compilés (DXIL ou MSL) rangés avec les assets ;
  - **vérification** : chaque fichier source préparé est relu par le chargeur du jeu ; références manquantes, fichiers orphelins (jamais référencés) et licences manquantes dans `credits.json` signalés ;
  - **incrémental** : un fichier n'est repréparé que si sa source ou la version de l'outil a changé (empreintes).
- [ ] **Le jeu livré ne lit que des assets préparés** : pas de repli vers les sources ; un asset manquant est une erreur de la préparation, pas du lancement.
- [ ] **Liste des assets livrés** : ce qui part est ce qui est **référencé** depuis les points d'entrée du jeu (cartes, tables, interface), pas le dossier entier (les modèles de test et les scènes du bac à sable ne partent pas).

### Questions à se poser

- **Préparer pendant le build ou à part ?** Recommandation : une **cible CMake** `<jeu>_cooked` qui appelle l'outil, incrémentale, utilisée par la CI et par le packaging ; le développement quotidien garde les sources (rechargement à chaud), avec une option pour **jouer sur les assets préparés** en local (le bug « marche en dev, pas en livré » se voit ainsi avant la livraison).
- **Retirer ImGui de la version livrée, ou garder une interface de debug cachée ?** Recommandation : **retirer** (code et bibliothèque) ; les rapports de plantage et le journal suffisent pour le support. Les studios livrent sans leurs outils internes.
- **Console dans la version livrée ?** Le jalon 7 a décidé « présente mais fermée, outils seulement ». Sans ImGui, elle n'a plus de fenêtre : soit une console minimale dessinée avec le texte du moteur, soit **pas de console** et seulement des **arguments de lancement** (`+set`). Recommandation : **arguments de lancement seulement** dans ce jalon (le jeu décidera d'une console de triches au jalon des finitions).

### Pièges connus

- Du code qui **n'existe qu'en livré** (ou qu'en dev) et qui casse la compilation de l'autre configuration : la CI construit les deux.
- Une donnée **lue seulement en dev** (une table chargée par un panneau ImGui) et absente du livré : la liste des assets livrés la manquera ; l'essai de la tranche livrée de bout en bout la trouvera.
- Le transcodage BC7 d'avance doit donner **exactement** les texels que donne le transcodage au chargement (même bibliothèque, mêmes réglages) : capture de la démo 3D identique sur assets préparés.

### Validation

- La tranche en configuration livrée n'a ni ImGui, ni efsw, ni Tracy dans l'exécutable (vérifié sur les symboles) ; elle tourne sur les assets préparés seuls ; sa capture de référence est identique à celle du développement ; son chargement est plus court (transcodage supprimé), mesuré.

### Implémentation réalisée (partie 6)

- **`MOTEUR_SHIPPING`** (option CMake ; presets `windows-shipping` et `macos-shipping`) : `debug_tools.cpp` et `debug_ui.cpp` ne sont pas compilés, ImGui et efsw ne sont pas liés (ImGui n'est plus public qu'en développement) ; `Application::debug_tools()` et `Renderer::debug_ui()` rendent toujours null ; les zones du profiler sont des macros vides ; le rechargement à chaud refuse avec un message ; au démarrage, seules les lignes `set` passent (`+set nom valeur`), jamais une commande de jeu. Les outils (bac à sable, éditeur, tests) ne sont pas construits dans cette configuration ; les exemples et la tranche le sont. Tracy et `MOTEUR_SHIPPING` s'excluent.
- **Réglages de compilation** (`cmake/Shipping.cmake`) : optimisation à l'édition de liens (LTCG / LTO), **CRT statique** (`/MT`, triplet `x64-windows-static`), symboles dans un **PDB** à part (`/DEBUG:FULL /OPT:REF /OPT:ICF`, portés par la cible `moteur` pour atteindre aussi les programmes du jeu) ; `-g` sur macOS (le dSYM est extrait au packaging). `winpixevent` exclu du triplet statique (`windows & !staticcrt` : un outil de développement).
- Vérifié sur `jeu_minimal` et `tranche` livrés : **aucune DLL** de Visual C++ (seulement celles du système, contre MSVCP140, VCRUNTIME140 et `api-ms-win-crt-*` en Release), aucune chaîne « ImGui », « efsw », « Tracy » dans l'exécutable (5 en Release).
- **Préparation des assets** (`moteur/cook.hpp` : `cook()`, `cook_main()` pour l'outil d'un jeu) : **manifeste de livraison** (globs `inclure` / `exclure`, dossier des tables `donnees`, groupes d'`archives`) ; **dépendances** suivies (fichiers des glTF, avec la préférence KTX2 : les PNG de repli restent derrière ; textures des effets) ; **KTX2 UASTC → BC7 / BC5** une fois pour toutes (`transcode_ktx2_for_shipping`, supercompressé Zstandard ; le choix sRGB reste au chargement) ; **tables compilées** par le rappel du jeu ; **shaders** copiés ; **relecture** de chaque fichier avec le chargeur du moteur (glTF, KTX2, images, sons, cartes, effets, descriptions d'animation) ; **licences** : avertissement pour tout modèle, son, environnement ou police qu'aucune entrée de crédits ne couvre ; **sources non livrées** listées (`--orphelins`) ; **incrémental** (empreinte de chaque source et version de l'outil, `preparation.json`) ; fichiers périmés retirés de la sortie. Outils : `moteur_cook` (jeu sans tables) et `tranche_cook`.
- **Tranche** (`assets/livraison_tranche.json`) : 50 fichiers, 3 textures transcodées, tables compilées, aucun avertissement ; 1,3 s (5,3 s avec les archives). Tests des globs.
- **Crédits** : la lecture des crédits du moteur filtre `"build": "DEVELOPPEMENT"` (ImGui, efsw, PIX) dans la version livrée, dont les licences ne partent pas non plus.
- **Pas fait, et pourquoi** : cartes et descriptions d'animation en CBOR, clips ozz préparés (« si la mesure le justifie » : toutes les tables se lisent en 0,3 ms, la tranche se charge en 245 ms, les personnages en 36 ms).

---

## 7. Archive des assets

### But

Livrer les assets dans **quelques fichiers d'archive** plutôt que des milliers de fichiers en vrac : chargement plus rapide (moins d'ouvertures de fichiers, lectures contiguës), installation et mises à jour plus propres, données moins faciles à modifier par erreur.

### Tâches

- [ ] **Format d'archive** (voir les questions) : en-tête (magie, version, empreinte), **table des entrées** (chemin normalisé → position, taille, taille compressée, compression, CRC32), données **alignées** (4 Kio) ; écrit par `moteur_cook`.
- [ ] **Lecture** : `read_file()` (jalon 4) passe par un **système de fichiers virtuel** : d'abord les archives montées (dans un ordre de priorité, pour qu'une archive de correctif remplace une entrée), puis, en développement seulement, le dossier. Les shaders y passent aussi (aujourd'hui lus à côté de l'exécutable).
- [ ] **Compression par entrée**, choisie par type : rien pour ce qui est déjà compressé (KTX2 avec Zstandard, OGG), **Zstandard** pour le reste (JSON, CBOR, glTF binaires, WAV).
- [ ] **Découpage** en plusieurs archives par usage (commun, une par zone plus tard, audio), pour que le chargement d'une zone lise une archive et qu'une mise à jour ne change que les archives touchées.
- [ ] Vérification de l'intégrité au lancement (empreinte de la table des entrées) et à la lecture (CRC32 de l'entrée) : une archive abîmée donne un message clair, pas un plantage.
- [ ] Mesure : chargement de la tranche depuis l'archive contre depuis le dossier, à froid (cache disque du système vidé) et à chaud.

### Questions à se poser

- **Format maison, ZIP, ou bibliothèque (PhysFS) ?** Le ZIP est lisible par tout le monde, mais sa table est à la fin, ses entrées ne sont pas alignées et il ne connaît pas Zstandard partout ; PhysFS ajoute une couche générale dont on n'utiliserait qu'un tiers. Recommandation : **format maison** (le lecteur fait quelques centaines de lignes, nous le maîtrisons, et il se teste comme les autres lecteurs), avec **Zstandard** en bibliothèque (vcpkg `zstd`, BSD ; on ne l'écrira pas nous-mêmes). C'est ce que font les gros jeux (archives `.pak` d'Unreal, bundles de Path of Exile, CASC de Blizzard), chacun avec son format.
- **Chiffrer les archives ?** Recommandation : **non**. Le chiffrement n'arrête pas un joueur déterminé (la clé est dans l'exécutable) et le jeu est solo ; les **modifications** par les joueurs ne sont pas un problème à combattre. Une empreinte suffit à détecter une archive abîmée.
- **Mises à jour** : les plateformes comme Steam calculent des différences par blocs ; elles fonctionnent mieux si une modification d'un asset ne **décale pas** tout le reste de l'archive *(à vérifier : recommandations de Steamworks sur les fichiers d'archive)*. Recommandation : ordre des entrées **stable** (trié par chemin), alignement, une archive par usage.

### Pièges connus

- La **casse des chemins** : Windows ne la distingue pas, macOS par défaut non plus, l'archive si. Normaliser (minuscules, `/`) à l'écriture et à la lecture, et **refuser** en préparation deux chemins qui ne diffèrent que par la casse. *Retenu à l'implémentation : pas de passage en minuscules ; l'archive garde la casse, compare les chemins exactement et refuse à l'écriture deux chemins qui ne diffèrent que par la casse.*
- Lire une entrée compressée **par petits morceaux** (flux audio) : la musique en flux de miniaudio lit au fil de l'eau ; l'archive doit offrir une lecture par flux (entrée non compressée pour l'OGG, donc lecture directe dans l'archive).
- `cgltf` lit les tampons externes par chemin : ils doivent passer par le système de fichiers virtuel (c'est déjà le cas via `read_file()`, à vérifier pour chaque type).

### Validation

- La tranche livrée se charge **uniquement** depuis ses archives (le dossier des assets absent) ; même capture de référence ; une archive tronquée ou modifiée donne un message clair au lancement ; les temps de chargement à froid et à chaud sont notés.

### Implémentation réalisée (partie 7)

- **Format `.pak`** (`moteur/archive.hpp`) : « MPAK », version 1, table des entrées **triée** (chemin, position, taille stockée et d'origine, compression, CRC32) vérifiée par une empreinte FNV-1a à l'ouverture, données **alignées sur 4 Ko** ; **Zstandard** (niveau 19, vcpkg `zstd` 1.5.7, BSD, crédité) par entrée, sauf ce qui est déjà compressé (KTX2, OGG, MP3, PNG, JPEG, FLAC) ou ne rétrécit pas ; chemins comparés exactement, deux chemins qui ne diffèrent que par la casse **refusés** à l'écriture ; lecture d'une entrée sur son propre descripteur (sûre entre fils : le préchargement lit en parallèle) avec contrôle du CRC32.
- **Système de fichiers virtuel** (`paths.hpp`) : `read_file()` cherche un fichier du dossier du programme dans les archives montées (la dernière d'abord : un correctif masque une entrée), puis sur le disque ; `file_exists()`, `mount_archive()`, `mount_archives(dossier)`, `set_archives_only()`. Les **shaders** passent maintenant par `read_file()`, ainsi que les **tables compilées** et `Assets::exists()` ; la vérification de casse des chemins est faite par l'archive elle-même. `ApplicationConfig::archives_directory` (`archives`, autrefois `paquets` (renommé le 2026-10-17)) : monté avant tout chargement ; **en version livrée, une fois des archives présentes, rien d'autre n'est lu** (un fichier non préparé n'existe pas) ; une archive abîmée arrête le programme avec un message qui conseille de réinstaller.
- **Préparation → archives** : le préparateur range sa sortie dans `archives/<nom>.pak` selon les groupes du manifeste, le reste et les shaders dans `common.pak` (autrefois `paquets/` et `commun.pak` (renommé le 2026-10-17)). Tranche : `audio.pak` 9,7 Mo, `personnages.pak` 3,8 Mo, `common.pak` 3,8 Mo.
- **Essai de bout en bout** : la tranche **livrée** (exe de 7 Mo, `licenses/`, `archives/`, aucun dossier d'assets) lancée sur le rejeu de référence donne **exactement** la capture de référence `f54c9067ab40` ; chargement 242 ms (231 ms depuis le dossier). Tests : aller-retour, tri et alignement, collision de casse, index et entrée abîmés, fichier qui n'est pas une archive, montage et lecture par `read_file()`, mode « archives seulement ».

---

## 8. Robustesse : plantages, affichage, analyses

### But

Qu'un jeu livré **survive** au monde réel : quand il plante, il laisse un **rapport** qu'on sait lire ; il s'affiche correctement sur les écrans des joueurs (**plein écran**, résolutions, plusieurs écrans, densités) ; il a passé les **outils d'analyse** qui trouvent les bugs que les tests ne voient pas.

### Tâches

- [ ] **Rapport de plantage** :
  - Windows : filtre d'exceptions non gérées qui écrit un **minidump** (`MiniDumpWriteDump`, API du système) et le journal dans le dossier des préférences (`crash_reports/`, autrefois `plantages/` (renommé le 2026-10-17)), puis un message sobre au joueur (où se trouve le rapport) ;
  - macOS : le système écrit déjà un rapport de plantage (`.ips`) ; on ajoute un gestionnaire de signaux qui écrit la **pile** et la fin du journal dans `crash_reports/` *(à vérifier : ce qu'on peut faire sans risque dans un gestionnaire de signal)* ;
  - **symboles** (PDB, dSYM) de chaque version livrée **archivés** par la CI avec la version ; un script retrouve la pile d'un rapport avec les noms de fonctions ;
  - commande de debug `crash` (et `crash.gpu`, `crash.assert`) pour provoquer chaque cas et vérifier la chaîne ;
  - le rapport contient la version du moteur et du jeu, l'OS, le GPU et son pilote, la mémoire, les 200 dernières lignes du journal.
- [ ] **Options d'affichage** (réglages du joueur, fichier `display.json` dans les préférences, comme `audio.json`) :
  - **fenêtré**, **plein écran fenêtré** (sans bordure) et **plein écran exclusif** si SDL_GPU et l'OS le permettent ; choix de l'**écran** ; résolution et fréquence ;
  - **synchronisation verticale** et limite d'images par seconde ; **résolution de rendu** ; **anticrénelage** ; qualité des **ombres** ;
  - changement **sans redémarrer** ; retour au réglage précédent si l'écran ne confirme pas dans les 15 secondes (comme les jeux PC) ;
  - fenêtre déplacée d'un écran Retina vers un écran standard : l'interface garde sa taille physique.
- [ ] **Perte du GPU / mise en veille / changement de carte graphique** : ce que fait SDL_GPU quand le périphérique est perdu (D3D12 *device removed*) *(à vérifier)* ; au minimum, un rapport propre plutôt qu'un plantage muet.
- [ ] **Analyses** :
  - **AddressSanitizer** sur les tests unitaires et une partie de la tranche rejouée, sous Windows (MSVC le prend en charge) et macOS (Clang), en CI pour les tests ;
  - **UndefinedBehaviorSanitizer** sur macOS ;
  - **analyse statique** : `clang-tidy` avec une liste de règles choisie (pas tout), en CI, sans nouvel avertissement ;
  - **fuzzing** des lecteurs de fichiers (cartes, tables, sauvegardes, KTX2, glTF, archives) avec libFuzzer (Clang, macOS) : quelques heures par lecteur, les fichiers trouvés deviennent des tests de `test_robustness.cpp`.
- [ ] **Test d'endurance** : la tranche rejouée en boucle **8 heures** (rejeu + retour au titre + chargement d'une sauvegarde), mémoire et temps par image relevés toutes les minutes : ni fuite, ni dérive.
- [ ] **Chemins de l'utilisateur** : nom d'utilisateur avec accents et espaces (dossier des préférences), disque plein pendant une sauvegarde (message, sauvegarde précédente intacte), dossier des préférences en lecture seule.

### Questions à se poser

- **Rapport de plantage : maison ou bibliothèque (Crashpad, Sentry Native) ?** Crashpad est l'outil de Chrome : solide, mais lourd à construire et à intégrer *(à vérifier : disponibilité dans vcpkg)*. Recommandation : **écrire nous-mêmes** la partie locale (minidump sous Windows avec l'API du système, rapport du système plus pile sous macOS), assez simple ; l'**envoi** automatique des rapports à un service (Sentry, Backtrace, qui lisent les minidumps) est une décision du jeu, avec consentement du joueur, à prendre avant la première version publique.
- **Plein écran exclusif ?** Sous Windows, le plein écran **fenêtré** est devenu la norme (même latence avec les modèles de présentation récents, pas de clignotement au changement de fenêtre) ; sous macOS, le plein écran est un espace à part. Recommandation : **fenêtré et plein écran fenêtré** sur les deux OS, **exclusif** seulement si SDL_GPU le gère proprement *(à vérifier)*.
- **Que garder des sanitizers en CI ?** ASan double le temps des tests : recommandation, en CI sur chaque envoi pour les tests unitaires ; la tranche sous ASan et le fuzzing avant chaque étiquette de version.

### Pièges connus

- Un **minidump complet** pèse la mémoire du processus (des centaines de Mo) : écrire un minidump **réduit** (piles, modules, données de quelques variables) ; le complet seulement en option.
- Les **symboles** qui ne correspondent pas exactement à l'exécutable (reconstruit entre-temps) sont inutilisables : la CI archive les symboles **du même build** que le paquet.
- ASan sous MSVC demande que **toutes** les bibliothèques statiques soient compatibles (annotations des conteneurs) : un triplet vcpkg dédié peut être nécessaire *(à vérifier)*.
- Plein écran sous macOS : la barre de menus et l'encoche des portables (`safe area`) ; l'interface du jeu ne doit pas être sous l'encoche.

### Validation

- Les trois plantages provoqués (`crash`, `crash.gpu`, `crash.assert`) laissent chacun un rapport dont le script tire une pile avec noms de fonctions, sur les deux OS ; les options d'affichage changent sans redémarrer et se retrouvent au lancement suivant ; ASan propre sur les tests et la tranche ; 8 heures d'endurance sans dérive ; chaque lecteur a tourné sous fuzzing, les fichiers trouvés sont des tests.

### Implémentation réalisée (partie 8) — terminée sous Windows (macOS repoussé)

Code écrit le 2026-10-05 ; bilan fait le 2026-10-16 (Windows) ; terminée sous Windows le 2026-10-17. Seules les vérifications macOS restent (voir « Reste pour la partie 8 »).

- **Rapports de plantage** (`moteur/crash.hpp`) : `install_crash_reporter` (appelé par `Application`, `ApplicationConfig::crash_reports`, dossier `crash_reports/` des préférences, autrefois `plantages/` (renommé le 2026-10-17)). Sous Windows, filtre d'exceptions non gérées + `SIGABRT` + `std::terminate` : un **minidump réduit** (fils, piles, modules : environ 170 Ko) et un **rapport texte** (programme, version du moteur, système, GPU, événement, code et adresse, pile en module + décalage avec fonction et ligne quand les symboles sont là, 200 dernières lignes du journal), puis un message au joueur (`MOTEUR_CRASH_SILENT` le supprime pour les vérifications). Sous macOS : gestionnaire de signaux sur une pile à part, `backtrace` + `dladdr`, rapport texte (le système écrit aussi le sien). Tout ce que le gestionnaire utilise est préparé à l'installation (`set_crash_context`, anneau des lignes du journal rempli par `LogFile`).
- **Provoquer un plantage** : commande de console `crash [acces|abort|exception]` ; `tranche --test-plantage acces|abort|exception`. `crash.gpu` et `crash.assert` du plan ne sont pas faits (une assertion de la version livrée passe par `abort`, déjà couvert).
- **Lecture d'un rapport** : `tools/crash/read_report.py RAPPORT SYMBOLES` (dbghelp sous Windows, `atos` sous macOS) ajoute fonction, fichier et ligne à chaque ligne de la pile.
- **Options d'affichage** (`moteur/display.hpp`) : `DisplaySettings` (fenêtré / plein écran fenêtré, écran, synchronisation, limite d'images, résolution de rendu 50 à 100 %, anticrénelage, qualité des ombres en plafonds `shadow_caps`), `display.json` dans les préférences (autrefois `affichage.json`, à clés françaises, encore lu quand `display.json` manque (renommé le 2026-10-17) ; `ApplicationConfig::display_settings`, valeurs inconnues ou fausses → défauts avec avertissement ; format dans `moteur_doc/FORMATS.md`), `Application::apply_display()` sans redémarrer. Plein écran **exclusif** non proposé (décision). Menu **Options** de la tranche (titre et pause, `SliceUi`) : retour aux réglages précédents si un changement de mode ou d'écran n'est pas confirmé dans les **15 s**.
- **Analyses** : presets `windows-asan` / `macos-asan` (`MOTEUR_ASAN`) ; presets `*-fuzz` (`MOTEUR_FUZZ`, libFuzzer + ASan) avec `tests/fuzz/fuzz.cpp`, un programme par lecteur (cibles `map`, `effect`, `animation`, `save`, `ktx2`, `gltf`, `table`, `bindings`, `image`, `archive`), lancés par `tools/fuzz/fuzzer.py` (corpus amorcé par les fichiers du projet, dans `build/fuzz/`) ; `clang-tidy` avec `.clang-tidy` (règles `bugprone`, `clang-analyzer`, `performance`) par `tools/ci/static_analysis.py`.

**Bilan du 2026-10-16 (Windows, RTX 4070 Ti SUPER)** :

| Vérification | Résultat |
|---|---|
| `verify.py --quick` (Release) | Build, 401 tests, données, exemple extérieur, MSL : **ok**. Références **toutes identiques** (2D `bd91e8b2c616`, démo 3D `fdc076d9c3ae`, rejeu `952477744ffb`, tranche `f54c9067ab40` deux fois, exactitude `063b80d5f127`). Étape « dépendances des en-têtes » en **échec** au bilan, corrigée le même jour : voir ci-dessous |
| Configurations livrée et ASan | Se construisent |
| Plantages provoqués (tranche Release et livrée) | Accès invalide, `abort`, exception non attrapée : **un rapport chacun**, `.txt` + `.dmp`, contenu complet ; pile avec noms de fonctions par `read_report.py` |
| Options d'affichage | `display.json` (alors `affichage.json`) modifié relu et appliqué au lancement (synchronisation coupée, limite à 30 images/s tenue, résolution 0,7, MSAA 4×, ombres basses) |
| ASan, tests unitaires | **Propre** (401 tests) |
| ASan, tranche rejouée (340 ticks) | **Aucune erreur**. Capture différente de la référence par la seule ligne de statistiques (« recalculées » 3 au lieu de 0) : l'image figée suit d'une image interpolée sous ASan (images lentes), pas une différence d'état |
| Fuzzing (passes du 2026-10-05, 2 min par lecteur par défaut) | **3 défauts trouvés**, corrigés le 2026-10-16 (voir ci-dessous) |
| `clang-tidy` | **70 avertissements** : 12 dans les bibliothèques (ozz, cgltf, stb : à filtrer), 58 dans le moteur (`misplaced-widening-cast` 19, concaténations 13, copies en boucle 12, `vector` sans `reserve` 4, autres 10) |

**Défauts trouvés** :

- **Fuzzing, archive** *(corrigé le 2026-10-16)* : `Archive::open` allouait la taille d'index lue dans l'en-tête sans la comparer à la taille du fichier : une archive abîmée demandait 4 Go. Maintenant `read_range` compare toute plage demandée à la taille du fichier **avant** d'allouer (message « truncated »), une entrée dont position + taille déborde est refusée, et `Archive::read` refuse une taille décompressée plus grande que ce que les trames Zstandard stockées peuvent contenir (`ZSTD_decompressBound`). `SDL_PathInfo` initialisé (le message d'erreur lisait une valeur indéterminée).
- **Fuzzing, glTF** *(corrigé le 2026-10-16)* : un skin **sans os** (clé `joints` abîmée) était accepté ; un sommet sans poids prend l'os 0 de la palette, qui était vide : **plantage** (lecture à l'adresse 0). Un skin sans os est refusé (« has no joints »), comme l'exige la spécification glTF ; les indices d'os des sommets étaient déjà contrôlés.
- **Fuzzing, KTX2** *(corrigé le 2026-10-16)* : libktx alloue les tailles décompressées déclarées par l'en-tête avant tout contrôle : 4 Go sur un fichier abîmé. L'en-tête est maintenant **validé avant libktx** (`validate_header` dans `ktx_texture.cpp`, pour `decode_ktx2` et `transcode_ktx2_for_shipping`) : texture 2D simple de 1 à 16 384 pixels de côté, nombre de niveaux possible pour cette taille, schéma de supercompression connu, descripteur, clés et données globales dans le fichier, chaque niveau dans le fichier et de **taille exacte** pour son format (16 octets par bloc 4 × 4 en UASTC, BC7, BC5 ; 4 par pixel en RGBA8 ; 0 déclaré en BasisLZ, comme le veut la spécification).
- **Cas de test** (`test_robustness.cpp`, 3 cas, 42 vérifications) : les deux petits fichiers trouvés, gardés dans `tests/data/fuzz/` (`archive_index_demesure.pak`, 271 octets ; `gltf_skin_sans_os.glb`, 8 Ko ; le KTX2 de 950 Ko n'est pas versionné), et chaque défaut refait sur un fichier sain (taille d'index, taille décompressée avec la somme de contrôle de l'index refaite, clé `joints` abîmée du modèle de référence, tailles de niveau, position, dimensions et nombre de niveaux d'un KTX2 RGBA8 et d'un KTX2 UASTC + Zstandard). `MOTEUR_TEST_DATA` donne le dossier `tests/data/` aux tests ; `.gitattributes` déclare `.glb`, `.pak` et `.ktx2` binaires. La minimisation automatique de libFuzzer (`-minimize_crash`) ne fonctionne pas sous Windows. Les trois fichiers trouvés, rejoués par les programmes de fuzzing reconstruits, sont refusés sans erreur. Nouvelle passe de 5 minutes par lecteur corrigé (2026-10-16) : archive 30 492 essais, glTF 1 072 314, KTX2 152 298, **rien trouvé**. Verifier (Release) : tout passe, références identiques, sauf l'étape connue des dépendances d'en-têtes (lancement sans console).
- **`verify.py`, dépendances des en-têtes** *(corrigé le 2026-10-16)* : lancé sans console (depuis un outil, un IDE, un agent), l'étape échouait (149 objets sans dépendances) et **chaque passage reconstruisait tout** (233 cibles, 77 s). Deux causes :
  - **deux jeux d'outils sur les mêmes dossiers** : le script prenait le CMake 3.31 et le Ninja 1.12 que l'environnement de Visual Studio met dans le `PATH`, alors que les dossiers `build/<preset>` sont configurés et construits par ceux de **CLion** (CMake 4.3, Ninja 1.13, partagés grâce aux presets). Le simple `ninja -t deps` du Ninja 1.12 **supprime le `.ninja_log`** du Ninja 1.13 : tout est reconstruit au build suivant. Le script prend maintenant les outils de CLion en premier (le `PATH` seulement à défaut), et le contrôle utilise le Ninja enregistré dans le cache du dossier (`CMAKE_MAKE_PROGRAM`) ;
  - **la page de code** : `chcp 850` n'agit que dans une console, et changeait au passage celle du terminal de l'utilisateur. Chaque commande du script tourne maintenant dans **sa propre console cachée** (`own_console`), à la page de code du système, comme dans un terminal ou dans CLion, quel que soit le lanceur ; plus de `chcp`.

  Vérifié sans console : trois passages de suite, étape réussie, plus aucune ligne « Remarque » non reconnue, build à jour en 1 à 3 s ; un objet privé exprès de ses dépendances (préfixe faussé) est détecté et recompilé par l'étape. MSVC laisse encore, rarement, une compilation sur une centaine sans préfixe reconnu : la réparation automatique (jusqu'à trois recompilations) s'en charge.
- **Rapport de mesure** : `"vsync"` écrit la valeur de la configuration, pas celle des réglages d'affichage appliqués.
- **`affichage.json`** (aujourd'hui `display.json`) : la résolution de rendu est réécrite en `0.699999988` (flottant converti tel quel).

**Suite de la partie 8 (2026-10-17)** :

- **`clang-tidy` propre** : 61 avertissements du moteur traités, 57 corrigés dans le code (concaténations, conversions élargies avant la multiplication, copies en boucle, `reserve`, `std::forward` dans `make_asset`, déplacement `noexcept` de `TextureAtlas`, adresses hachées par `std::uintptr_t`, `catch` vides traités) et 4 justifiés sur la ligne (`NOLINT` avec la raison : l'accès nul voulu de `crash_now`, l'adresse passée à `GetModuleHandleExA`, l'échec de préchargement d'une texture rapporté par le cache des textures). Arithmétique et ordre des calculs inchangés : captures identiques. `static_analysis.py` ne juge que le code du projet (`src/`, `apps/`) ; ce qu'il dit des en-têtes des bibliothèques (le code SSE d'ozz écrit pour MSVC, que clang ne compile pas ; cgltf, stb) est compté à part.
- **`verify.py`** gagne deux étapes : l'**analyse statique** (tout avertissement échoue ; `--no-analysis`) et les **tests unitaires sous ASan** (preset `<os>-asan`, avertissements en erreurs ; `--no-asan`).
- **Chemins de l'utilisateur** (`tests/test_user_paths.cpp`) : sauvegardes, fichiers et journal dans un dossier au nom accentué, avec espaces et symboles ; une sauvegarde qui ne peut pas s'écrire (le fichier temporaire impossible à créer, comme un disque plein) laisse la précédente intacte et dit pourquoi, et la suivante passe dès que l'écriture refonctionne ; un dossier des préférences où rien ne s'écrit : refus avec une raison, le journal continue au terminal, jamais de plantage. Le dossier du projet (`Moteur + ARPG`) a déjà espaces et symbole.
- **Perte du périphérique GPU** : SDL_GPU n'a pas de notion de périphérique perdu ni de récupération ; sous Direct3D 12, un périphérique retiré (pilote réinitialisé, carte retirée) fait échouer l'acquisition et l'envoi des commandes et l'acquisition de l'image, avec le motif de Windows dans l'erreur. Comme les moteurs sur PC (Unreal), le moteur **détecte, rapporte et ferme proprement** : `Renderer::device_lost()` à la première de ces défaillances (le renderer ne touche plus au GPU), `Application` écrit un rapport comme pour un plantage (`report_fatal_error`, minidump compris, événement « GPU device lost » et motif), montre au joueur le message du jeu (`ApplicationConfig::gpu_lost_message`, défaut en anglais ; celui de la tranche en français, qui conseille de mettre à jour le pilote) et termine (`ended_on_error()` : code de sortie 1). Vérifié par `crash gpu` (console) et `tranche --test-plantage gpu` : rapport complet, journal, fin propre.
- **Endurance de 8 heures** : retirée de la liste (décision de l'utilisateur, 2026-10-17).
- **Fuzzing long** (2 heures par lecteur, les 10 en parallèle, 2026-10-17) : `map` 292 623 essais, `effect` 5,5 millions, `animation` 4,9 millions, `save` 337 millions, `bindings` 4,5 millions, `image` 18,7 millions, `archive` 358 185 : **rien**. Trois défauts, corrigés le même jour :
  - **KTX2, niveaux dans le désordre** : chaque niveau était dans le fichier et de la bonne taille, mais un niveau placé sur le précédent faisait déborder le calcul de la taille des données de libktx (allocation géante). La spécification range les niveaux du plus petit au plus grand, chacun **avant** le précédent : `validate_header` l'exige maintenant (« mip level N is not stored before mip level N-1 »).
  - **glTF, temps des clés démesurés** : un échantillonneur aux temps immenses faisait tourner sans fin l'échantillonnage à 60 Hz d'une spline cubique, et la conversion du temps en entier avait un comportement indéfini. Les temps doivent être finis et d'au plus une heure (`read_keys`, « time beyond ») ; le fichier trouvé est gardé (`tests/data/fuzz/gltf_temps_demesures.glb`, refusé en quelques millisecondes).
  - **Tables compilées (et sauvegardes), CBOR très imbriqué** : `nlohmann::json::from_cbor` lit les conteneurs imbriqués par récursion ; quelques centaines de kilo-octets de listes imbriquées débordent la pile et tuent le programme avant toute erreur. Le lecteur des tables est mort ainsi pendant le fuzzing long sans laisser de fichier, et `fuzzer.py` l'affichait « rien » : il signale maintenant un lecteur arrêté anormalement. Correction : `check_cbor_structure` (`private/moteur/cbor_check.hpp`), un parcours **itératif** de la structure CBOR (RFC 8949 : longueurs définies et indéfinies, étiquettes, chaînes en morceaux) qui vérifie que tout est dans les octets et refuse plus de **128** niveaux, appelé avant nlohmann par `DataTableBase::decode` et par la lecture des sauvegardes CBOR. Le JSON texte n'est pas concerné (le lecteur de nlohmann est itératif : 200 000 niveaux lus sans plantage).
  - **Cas de test** (`test_robustness.cpp`, 3 cas) : un KTX2 sain à 4 niveaux puis le niveau 1 déplacé sur le niveau 0 ; le glTF trouvé, refusé en moins d'une seconde ; la structure CBOR (document écrit par le moteur, longueurs indéfinies et étiquettes acceptés, 128 niveaux acceptés et 129 refusés, CBOR tronqué, conteneur plus grand que le fichier, `break` isolé) et une table de 200 000 niveaux qui donne une erreur au lieu du plantage. 419 tests. Les trois entrées rejouées par les programmes de fuzzing reconstruits passent en 2 à 9 ms.
  - **Contrôle** : nouvelle passe de 15 minutes des lecteurs corrigés en parallèle : tables 1,1 million d'essais (le lecteur va maintenant au bout), KTX2 321 937, glTF 1,3 million, sauvegarde 46 millions : **rien**. `verify.py` complet (Debug, Release, exemple extérieur, MSL, références, clang-tidy, tests sous ASan) : **tout passe**, références identiques.
- `vsync` du rapport et écriture de la résolution de rendu : corrigés (2026-10-16 et 2026-10-17).

**Reste pour la partie 8** :

- Essais **à la main** (utilisateur, 2026-10-17, Windows) : options d'affichage dans la tranche (plein écran fenêtré, changement d'écran, retour après 15 s sans confirmation) : **tout fonctionne**. `Win+Ctrl+Maj+B` pendant la partie : **la tranche continue normalement** ; ce raccourci réinitialise l'affichage sans retirer le périphérique Direct3D 12 de l'application, donc rien à rapporter. Le chemin de la perte du périphérique reste vérifié par la perte simulée (`crash gpu`, `tranche --test-plantage gpu`) ; une vraie perte (pilote planté ou mis à jour pendant la partie, délai du GPU dépassé) passe par les mêmes échecs d'acquisition et d'envoi.
- **macOS** *(repoussé par l'utilisateur, 2026-10-17 ; seul point ouvert de la partie 8)* : rapport de plantage et symboles avec `atos`, ASan/UBSan, plein écran (le code POSIX du rapport n'a encore jamais été compilé).

---

## 9. Documentation de l'API

### But

Qu'on puisse **écrire un jeu avec le moteur** (l'ARPG, d'abord) en lisant sa documentation, sans relire ses sources : une **référence** de l'API publique, un **guide** qui explique comment les morceaux s'assemblent, et la **référence des formats de fichiers**.

### Tâches

- [ ] **Référence** générée par **Doxygen** depuis les en-têtes publics (`include/moteur/`) : un format de commentaire commun (`///` en tête de chaque classe et fonction publique : rôle, unités, fil, propriété des pointeurs, coût), complété en relisant chaque en-tête. Le HTML est produit par une cible CMake `moteur_docs` et par la CI (artefact ; publication sur GitHub Pages si le dépôt est public).
- [ ] **Avertissements de Doxygen = erreurs** en CI pour les en-têtes publics (fonction publique non documentée, paramètre inconnu).
- [ ] **Guide** (`moteur_doc/GUIDE.md`, en français comme les autres documents) :
  - démarrer un jeu (`moteur_add_game`, `Application`, `Game`, la pile d'états) ;
  - la boucle (pas fixe, ticks entiers, interpolation, règles du déterminisme : ce que la logique peut lire et ne pas lire) ;
  - le monde (ECS, composants du moteur, collecte vers le rendu) ;
  - assets et données (gestionnaire, tables, références, rechargement, préparation et archives) ;
  - rendu, animation, audio, entrées, monde et déplacement, particules, brouillard ;
  - sauvegarde (composants sérialisables, identifiants persistants, migrations) ;
  - outils (console, variables, profiler, inspecteur, éditeur) ;
  - livrer (configuration livrée, paquets, rapports de plantage).
  Chaque chapitre renvoie aux exemples (`examples/`, bac à sable) et aux décisions des jalons.
- [ ] **Référence des formats** (`moteur_doc/FORMATS.md`) : cartes (v1, v2), tables, descriptions d'animation (graphe compris), effets de particules, liaisons de touches, sauvegardes (en-tête, sections, versions), archives, réglages ; chaque format avec sa version et un exemple minimal.
- [ ] Mettre à jour [FICHIERS_DU_PROJET.md](FICHIERS_DU_PROJET.md) et décider de son avenir (voir les questions).

### Questions à se poser

- **Doxygen ou autre (Sphinx + Breathe, MkDocs, documentation à la main) ?** Recommandation : **Doxygen seul**, HTML : il lit le C++ directement, sans chaîne Python, et la référence reste à côté du code (un commentaire changé en même temps que la fonction). Doxygen est un outil de build, pas une bibliothèque du programme : sa licence (GPL) ne touche pas le moteur.
- **Langue de la référence ?** Les commentaires du code sont en **anglais**, les documents en **français**. Recommandation : garder cette séparation (référence en anglais, depuis le code ; guide et formats en français).
- **Et FICHIERS_DU_PROJET.md ?** Il décrit chaque fichier en détail et a servi pendant la construction ; une partie fera doublon avec la référence. Recommandation : le **garder** pour l'architecture et le build (il explique *pourquoi*), et y renvoyer à la référence pour le *quoi* des classes, plutôt que de maintenir deux descriptions.

### Pièges connus

- Une référence générée sur des en-têtes **non relus** donne des pages vides : la valeur est dans les commentaires écrits, pas dans l'outil.
- Documenter les détails internes : seule la surface publique de la partie 3 est documentée en référence.

### Validation

- `moteur_docs` produit la référence sans avertissement ; le guide et les formats sont écrits ; l'exemple minimal se construit en suivant **uniquement** le guide (essai : le relire comme si on démarrait l'ARPG).

### Implémentation réalisée (partie 9)

Faite le 2026-10-17 (Windows), en autonomie à la demande de l'utilisateur.

- **Outil** : **Doxygen 1.18.0**, installé par `winget` (paquet officiel `DimitriVanHeesch.Doxygen`). Configuration `tools/docs/Doxyfile.in`, page d'accueil `tools/docs/mainpage.md` ; cible CMake **`moteur_docs`** (`cmake/Docs.cmake` : la référence HTML dans `<build>/docs/html`, `WARN_AS_ERROR = FAIL_ON_WARNINGS` ; pas de cible si Doxygen manque) ; `tools/docs/check_docs.py [EN-TÊTES] [--output DOSSIER]` lance Doxygen et liste les avertissements (tous, ou ceux de quelques en-têtes). `verify.py` a une étape « API reference (Doxygen) » (`--no-docs`).
- **Les 74 en-têtes publics** (`include/moteur/`) : leurs commentaires `//` ont été convertis en commentaires Doxygen (`///` au-dessus d'une déclaration, `///<` en bout de ligne) par un script, puis **relus et complétés à la main, en anglais**, d'après les sources : chaque déclaration publique a son commentaire (rôle, unités, bornes, ce qui arrive en cas d'erreur, fil d'appel, propriété et durée de vie des pointeurs, coût quand il compte), les commentaires qui couvraient plusieurs déclarations sont devenus des groupes, les exemples sont des blocs de code, l'espace de noms `moteur` est décrit. **Environ 1 080 avertissements au départ, 0 à la fin.** Environ 70 commentaires faux ou périmés corrigés au passage (fonctions disparues, valeurs par défaut fausses, « attend le GPU » depuis les envois groupés de la partie 5, etc.).
- **La relecture a trouvé une soixantaine de défauts dans le code**, tous corrigés le même jour avec un test chacun (`tests/test_review_*.cpp` : core, render, world, readers, platform, world2, apps ; 473 tests en tout). Les principaux :
  - **déterminisme** : `reset` et `toggle` d'une variable de logique passaient hors de la file des commandes de jeu (hors rejeu) ; `std::atan2` dans un tick de la démo, et `cos`/`sin` dans la commande `spawn`, remplacés par des calculs sans trigonométrie ;
  - **comportement indéfini** : `DataIssues::clear()` (compte d'erreurs), migration de sauvegarde sans étape, `std::clamp` avec min > max, `MapData` par défaut, squelettes et clips vides, image d'environnement vide, `ExploredMap` de taille négative, divisions par zéro de la caméra et des ombres ;
  - **atomicité** : `Animator::load_state()`, `Credits::add_file()`, `Audio::apply_settings_json()` sont maintenant tout ou rien ;
  - **monde et déplacement** : `NoPath` écrasé par `Arrived` dans le même tick, champ de flux « valide » sans cible atteinte, direction vers une cible dans un mur, cache des boîtes du monde gardé après un changement ou un **rechargement à chaud** de modèle (compteur de rechargements par type d'asset) ;
  - **lecteurs** : archive à l'index non trié ou à la compression inconnue refusée à l'ouverture, collisions de casse vérifiées sur tout l'index, `**` des globs du préparateur, courbes des effets à valeur unique, clés de version 2 dans une carte de version 1, `display.json` (version lue, types faux signalés, valeurs par défaut du jeu), extensions glTF requises que le lecteur sait lire (`KHR_texture_basisu`), `variables.cfg` (seules les variables archivées, textes échappés), enregistrements de sauvegarde sans `id` ou `c` signalés au lieu de lever une exception ;
  - **plateforme** : rapports de plantage aux noms uniques (deux événements dans la même seconde s'écrasaient), `uninstall_crash_reporter()`, constructeur d'`Application` qui défait tout en cas d'échec (SDL, archives, profiler, journal) ;
  - **interface publique** : l'envoi d'une `CompressedImage` (type privé) sort de `renderer.hpp` (`create_compressed_texture()` dans `ktx_texture.hpp`, privé) ; `Input::stop_replay()` / `replaying()` ; `ParticleSystem::set_ground()` (le sol des particules plates, au lieu de l'origine de l'effet : une fumée de mort partait de la poitrine).
- **Guide** : [GUIDE.md](GUIDE.md) (en français, environ 940 lignes) : démarrer un jeu, la boucle et les règles du déterminisme, le monde, assets et données, chaque système, sauvegardes, outils, livrer, vérifier, ce qui n'est pas encore là. Chaque fonction citée a été vérifiée dans le code.
- **Référence des formats** : [FORMATS.md](FORMATS.md) (en français, environ 1 130 lignes) : cartes, tables (JSON et `.mdat`), descriptions d'animation et graphe, effets, liaisons de touches, sauvegardes, archives `.pak`, manifeste de livraison et `cook_state.json`, réglages du joueur, rapport de mesure, crédits, rapports de plantage, KTX2 et glTF acceptés, enregistrements d'entrées ; chaque format avec sa version, ses clés, les anciennes clés encore lues et un exemple minimal tiré d'un vrai fichier.
- [FICHIERS_DU_PROJET.md](FICHIERS_DU_PROJET.md) **gardé** (décision) pour l'architecture et le build, mis à jour avec l'arbre actuel ; les sections qui recopiaient l'API d'une classe renvoient à la référence. README remis à jour (structure, menus, vérification, paquets).
- **Scripts Python en anglais** (messages, options, noms) : `tools/ci/verify.py` (était `verifier.py` ; `--quick`, `--no-*`, `--keep-going`), `static_analysis.py`, `tools/crash/read_report.py`, `tools/measures/measure.py` (scènes `fight`, `crowd`, `crossing`, `loading` ; `summary.json` ; il lisait encore les clés françaises du rapport version 1 et aurait échoué), `tools/fuzz/fuzzer.py` (programmes `fuzz_map`, `fuzz_effect`, `fuzz_save`, `fuzz_bindings`...). Les valeurs du rapport de mesure écrites par la tranche sont en anglais (`slice_load_ms_max`...).
- **Pas fait** : la publication de la référence (GitHub Pages) attend la CI hébergée, repoussée ; l'essai « écrire l'exemple minimal en suivant uniquement le guide » est à faire par l'utilisateur en relisant le guide.

---

## 10. Packaging Windows

### But

Un **installeur** (et une archive portable) de la tranche, **signé**, avec icône et informations de version, qui s'installe et se désinstalle proprement sur une machine qui n'a jamais vu le projet.

### Tâches

- [ ] **Exécutable** : icône (`.ico` multi-tailles), **ressource de version** (nom du produit, éditeur, version, copyright), **manifeste** (prise en compte de la densité par écran *PerMonitorV2*, chemins longs, UTF-8 comme page de code du processus).
- [ ] **Bibliothèque C++ d'exécution** (CRT) : voir les questions ; vérifier l'exécutable avec `dumpbin /dependents` sur la machine propre.
- [ ] **Installeur** : dossier `Program Files`, raccourcis (menu Démarrer, bureau en option), désinstallation qui retire tout **sauf** les sauvegardes et les réglages (dans le dossier des préférences) ; mise à jour par-dessus une version précédente.
- [ ] **Archive portable** (`.zip`) : le même contenu sans installeur.
- [ ] **Signature** de l'exécutable **et** de l'installeur (SHA-256, horodatage) : étape prévue, **sautée dans ce jalon** (décision du 2026-10-05).
- [ ] Fonction CMake `moteur_package(<cible> ...)` qui produit les deux (utilisée par la tranche, puis par l'ARPG).
- [ ] Essai sur une **machine propre** (Windows 11 vierge, ou Windows Sandbox) et sur Windows 10 : installation, lancement, jeu, sauvegarde, désinstallation.

### Questions à se poser

- **Quel installeur ?** **Inno Setup** (simple, un script, très répandu pour les jeux indépendants), **WiX** (MSI, plus lourd, utile en entreprise), **MSIX** (Microsoft Store). Recommandation : **Inno Setup**. Si le jeu sort sur **Steam**, Steam installe lui-même les fichiers : l'installeur ne sert qu'à la distribution hors Steam (itch.io, site).
- **CRT : statique ou DLL ?** Le triplet actuel `x64-windows-static-md` lie les bibliothèques en statique mais la CRT en **DLL** : il faut alors livrer les DLL de Visual C++ (redistribuable de Microsoft, ou DLL copiées à côté de l'exécutable). Recommandation : **CRT statique** (`/MT`, triplet `x64-windows-static`) pour la version livrée : un exécutable sans dépendance, rien à installer. À vérifier : toutes les bibliothèques de vcpkg l'acceptent, et la taille de l'exécutable.
- **Comment signer ?** Sans signature, SmartScreen affiche « Windows a protégé votre ordinateur » et beaucoup de joueurs abandonnent. Options *(prix et conditions à vérifier)* :
  - **Azure Trusted Signing** (service de signature de Microsoft, abonnement mensuel d'une dizaine de dollars ; ouvert aux particuliers dans certains pays seulement) : pas de clé physique, se branche à la CI ;
  - **certificat OV classique** (quelques centaines d'euros par an, clé sur jeton matériel depuis 2023, donc difficile à brancher sur une CI hébergée) ;
  - **pas de signature** tant que le jeu passe par Steam (Steam ne l'exige pas, et l'avertissement SmartScreen ne s'affiche pas pour un jeu lancé par Steam *(à vérifier)*).

  Les certificats EV ne donnent plus de réputation SmartScreen immédiate *(à vérifier)* : la réputation s'acquiert au fil des téléchargements dans tous les cas. Recommandation : **Azure Trusted Signing** si l'utilisateur y est éligible, sinon certificat OV ; c'est une **décision de l'utilisateur** (coût, identité). **Décidé le 2026-10-05 : pas de signature pour l'instant** ; `moteur_package` prévoit l'étape (`signtool`), sautée tant qu'aucune identité n'est fournie.

### Pièges connus

- Signer **avant** de construire l'installeur (l'exécutable à l'intérieur doit être signé) puis signer l'installeur.
- Les antivirus signalent parfois un exécutable non signé, récent, qui écrit des minidumps : signer, et soumettre les faux positifs aux éditeurs si besoin.
- Les **chemins** : l'exécutable est dans `Program Files` (lecture seule) ; tout ce qui s'écrit va dans le dossier des préférences (déjà le cas : journal, sauvegardes, réglages, `imgui.ini`).

### Validation

- Sur une machine Windows propre : l'installeur s'installe (non signé : SmartScreen avertit, « Exécuter quand même »), la tranche se joue, la désinstallation laisse seulement les sauvegardes ; l'archive portable fonctionne aussi. Le jour où une identité est fournie, la même commande produit l'installeur signé.

### Implémentation réalisée (partie 10)

Faite le 2026-10-17 (Windows).

- **Exécutable** : `moteur_add_game` prend maintenant `NAME`, `VERSION`, `PUBLISHER`, `ICON` ; sous Windows il ajoute les **ressources** (`cmake/windows/game.rc.in` : icône, informations de version visibles dans les propriétés du fichier) et le **manifeste** (`cmake/windows/game.manifest` : Windows 10 / 11, densité par écran *PerMonitorV2*, chemins longs, page de code **UTF-8**, aucune élévation). Icône provisoire de la tranche dessinée par `tools/package/make_icon.py` (`art/icons/tranche.png`, `.ico` de 16 à 256 pixels, `.icns`), à remplacer par l'illustration du jeu.
- **CRT statique** : vérifié par `dumpbin /dependents` sur la tranche livrée : seulement des DLL du système.
- **`moteur_package(<jeu> MANIFEST ... [COOK_TOOL] [IDENTIFIER] [WINDOWS_APP_ID])`** (`cmake/Package.cmake`, configuration livrée seulement) : cible `<jeu>_package` qui prépare les assets (incrémental), dispose ce qui s'installe (l'exécutable, `archives/`, `licenses/` ; aucun asset en vrac, rien d'autre), garde l'exécutable et son **PDB par version** (`packages/symbols/<jeu>-<version>-windows-x64/`, pour `read_report.py`), écrit l'**archive portable** (`.zip`) et l'**installeur**. La tranche et l'exemple minimal l'utilisent.
- **Installeur : Inno Setup 7.1.0**, installé par `winget` (paquet officiel `JRSoftware.InnoSetup.7` ; la version 7 est l'actuelle). Script `cmake/windows/installer.iss.in` (enregistré en UTF-8 avec BOM) : pour tous les utilisateurs (`Program Files`) par défaut, ou **pour l'utilisateur seul** sans droits d'administrateur (choix sur la première page, `/CURRENTUSER`) ; mise à jour par-dessus la version précédente (identifiant d'application stable, dérivé de l'éditeur et du nom) avec les anciennes archives retirées d'abord ; raccourci du menu Démarrer, du bureau en option ; français et anglais ; Windows 10 1903 au minimum (page de code UTF-8) ; la désinstallation retire ce qui a été installé et **rien d'autre** (sauvegardes, réglages, journal et rapports restent dans le dossier du joueur).
- **Signature** : l'étape existe (`MOTEUR_SIGN_COMMAND`, l'exécutable signé avant l'archive et l'installeur, puis l'installeur et son désinstalleur) ; vide : non signé, comme décidé.
- **Vérification automatique** : `tools/package/check_package.py [BUILD] [--game] [--replay --expect]` installe en silence pour l'utilisateur seul dans un dossier au nom accentué, deux fois (une mise à jour), vérifie fichiers, informations de version, raccourci et entrée « Applications », lance le jeu **installé** sur le rejeu de la tranche (capture **identique à la référence du développement**, `f54c9067ab40`), compare l'archive portable, désinstalle et vérifie qu'il ne reste rien sauf le dossier du joueur. Étape de `verify.py` (configuration livrée, paquet, installation ; `--no-package`, sautée par `--quick`). Tranche : installeur 21,8 Mo, archive portable 20,6 Mo.
- **Reste (utilisateur)** : l'essai sur une **machine Windows propre** et sur **Windows 10** (Windows Sandbox n'existe pas sous Windows Famille) : installer, jouer, sauvegarder, désinstaller.

---

## 11. Packaging macOS : signature et notarisation

### But

Un **`.app`** de la tranche, **signé** (Developer ID) avec le *hardened runtime*, **notarisé** par Apple et **agrafé** (*stapled*), livré dans une **image disque** (`.dmg`), qui s'ouvre par un double clic sur un Mac qui n'a jamais vu le projet, sans passer par les réglages de sécurité.

### Tâches

- [ ] **Bundle** : `Contents/MacOS/tranche`, `Contents/Resources/` (archives d'assets, shaders MSL, licences, icône `.icns`), `Info.plist` (identifiant `com.<éditeur>.<jeu>`, version, version minimale de macOS **13**, catégorie « jeux », prise en charge de la haute densité) ; produit par CMake (`MACOSX_BUNDLE`).
- [ ] **Chemins** : `base_path()` doit trouver les ressources dans `Contents/Resources` (SDL_GetBasePath renvoie ce dossier dans un bundle *(à vérifier)*) ; rien ne s'écrit dans le bundle (il est signé : le modifier casse la signature).
- [ ] **Architecture** : arm64 seulement (Apple Silicon, machine minimale M3), ou binaire universel (voir les questions).
- [ ] **Signature** : `codesign` de l'intérieur vers l'extérieur, avec **Developer ID Application**, *hardened runtime* (`--options runtime`), horodatage ; droits (*entitlements*) au strict minimum (aucun a priori : pas de JIT, pas de micro).
- [ ] **Notarisation** : `xcrun notarytool submit --wait` (avec une clé d'API App Store Connect en CI), puis `xcrun stapler staple` sur le `.app` et la `.dmg`.
- [ ] **Image disque** : `.dmg` avec le `.app` et un lien vers `Applications`, signée et notarisée elle aussi.
- [ ] Le tout dans la CI macOS (secrets : certificat `.p12` et clé d'API), sur les étiquettes de version ; `moteur_package` fait les deux OS.
- [ ] Essai sur un **compte utilisateur neuf** du Mac (ou un autre Mac) : téléchargé par un navigateur (attribut de quarantaine), double clic, lancement sans avertissement de Gatekeeper.

### Questions à se poser

- **Apple Developer Program ?** Indispensable pour un certificat Developer ID et la notarisation : **99 $ par an** *(à vérifier)*, inscription en tant que particulier possible. Sans lui, le joueur doit passer par les réglages de sécurité pour lancer le jeu (et macOS durcit cela à chaque version) : inacceptable pour un jeu livré. **Décision de l'utilisateur.** Si le jeu sort sur Steam, la notarisation reste nécessaire pour l'application elle-même *(à vérifier)*. **Décidé le 2026-10-05 : plus tard.** Dans ce jalon, le `.app` est **signé ad hoc** (`codesign -s -`, obligatoire de toute façon sur Apple Silicon) ; les étapes Developer ID, notarisation et agrafe existent dans `moteur_package`, sautées tant que le compte n'existe pas. Un `.app` téléchargé non notarisé s'ouvre par **Réglages système > Confidentialité et sécurité > Ouvrir quand même** *(à vérifier selon la version de macOS)*.
- **arm64 ou universel ?** Les Mac Intel ne sont plus vendus et le M3 est la machine minimale décidée. Recommandation : **arm64 seulement** ; un binaire universel doublerait les dépendances vcpkg à construire (triplet universel à faire) pour des machines sous la cible de performance.
- **Mac App Store ?** Il impose le bac à sable (*sandbox*) d'Apple, qui change les chemins des préférences et des sauvegardes. Recommandation : **hors périmètre** ; distribution directe et Steam d'abord.

### Pièges connus

- `codesign --deep` est déconseillé : signer chaque binaire imbriqué explicitement, du plus profond au bundle *(à vérifier : recommandations actuelles d'Apple)*.
- Un fichier **modifié après la signature** (même un `.DS_Store`) l'invalide : la signature est la dernière étape avant l'image disque.
- `altool` ne sert plus à la notarisation (remplacé par `notarytool`).
- L'attribut de **quarantaine** n'est posé que sur un fichier téléchargé : un essai par copie locale ne montre pas ce que verra le joueur.
- La version minimale de macOS du bundle doit correspondre à `CMAKE_OSX_DEPLOYMENT_TARGET` (13.0) et à celle des bibliothèques vcpkg (triplet maison).

### Validation

- **Dans ce jalon** : la `.dmg` téléchargée sur un compte neuf s'ouvre ; après « Ouvrir quand même », la tranche se lance et se joue ; `codesign --verify --deep --strict` accepte le `.app` signé ad hoc.
- **Avec le compte Apple** (plus tard) : `spctl --assess --type execute` accepte le `.app` ; `stapler validate` confirme l'agrafe ; la tranche se lance sans passer par les réglages de sécurité.

### Implémentation réalisée (partie 11)

Écrite le 2026-10-17 sous Windows, **pas encore essayée** (il faut le Mac : voir [TEST_MAC.md](TEST_MAC.md), jalon 8).

- `cmake/PackageMacos.cmake` (même cible `<jeu>_package`) : `<nom>.app` (`Contents/MacOS/<exe>`, `Contents/Resources/archives/`, `licenses/`, l'icône `.icns`), `Info.plist` (`cmake/macos/Info.plist.in` : identifiant, version, macOS 13 minimum, catégorie jeux, haute densité, manettes ; contrôlé par `plutil -lint`), symboles extraits en **dSYM** et gardés par version, exécutable allégé (`strip -S`), **signature de l'intérieur vers l'extérieur** sans `--deep`, *hardened runtime*, aucun droit particulier ; **ad hoc** par défaut (`MOTEUR_MACOS_SIGN_IDENTITY = -`), `codesign --verify --strict` ; image disque `.dmg` (l'app et un lien vers Applications). **Notarisation et agrafe** prêtes (`MOTEUR_MACOS_NOTARY_PROFILE`, profil de `notarytool`), sautées sans compte Apple, comme décidé.
- `SDL_GetBasePath()` donne `Contents/Resources/` dans un bundle : les archives y sont cherchées ; rien ne s'écrit dans le bundle.

---

## 12. La tranche livrée et le rattrapage Mac

### But

Prouver le jalon : la **tranche jouable** existe comme un **produit** installé sur les deux OS, et la **dette Mac** des jalons 2 à 7 est soldée.

### Contenu

- **Exécutable `tranche`** (`apps/tranche/`) : la tranche des jalons 4 à 7 extraite du bac à sable en programme à part (titre, chargement en arrière-plan, partie, pause, options d'affichage et d'audio, liaisons de touches, sauvegarder / charger, quitter), **sans** scène de test, **sans** ImGui en configuration livrée ; le bac à sable garde sa propre entrée vers la même tranche (pour les captures et les outils), comme l'éditeur partage `bac_a_sable_commun`.
- Ses **assets préparés** dans ses archives ; ses **paquets** Windows et macOS signés, produits par la CI sur une étiquette `v0.8.0`.

### Tâches

- [ ] Extraire la tranche en `apps/tranche/` ; menu d'**options** (affichage de la partie 8, audio, liaisons de touches existantes).
- [ ] Captures de référence : la tranche **livrée** (assets préparés, archive) donne la **même image** que celle du bac à sable au même tick (`check_references.py` étendu).
- [ ] Jouer la version **installée** sur la machine Windows propre et sur le Mac, du titre au retour au titre, **au clavier-souris et à la manette** (dette du jalon 4), sauvegarde et chargement compris.
- [ ] **Rattrapage Mac** : passer [TEST_MAC.md](TEST_MAC.md) jalon par jalon (2 à 7) ; ce que la CI vérifie déjà (compilation, tests unitaires) est coché d'office ; le reste (rendu, Metal, Retina, audio, manette) à la main ; les mesures de la partie 4 sur le M3. Tout écart trouvé est corrigé dans ce jalon.
- [ ] Mettre à jour la roadmap (jalon 8 coché, configuration minimale écrite) et les décisions.

### Validation

- Voir les critères de fin de jalon.

### Implémentation réalisée (partie 12)

Faite le 2026-10-17 (Windows).

- **Code de la tranche dans `apps/tranche/`** : `demo3d`, `states_demo`, `slice_ui`, `map_objects`, `sandbox_data`, `sandbox_scene` y sont déplacés (bibliothèque `tranche_commun`, définie là) ; le programme `tranche`, son outil de préparation et son paquet ; le bac à sable garde son entrée vers la même tranche (captures, mesures).
- **Menu Options** : une page par sujet, **Affichage** (partie 8), **Audio** (volume général, musique, effets, ambiance, interface, par pas de 10 %, entendus aussitôt et écrits en quittant la page : `Application::save_audio_settings()` est public) et **Commandes** (la disposition, puis chaque action, les directions des axes comprises, réattribuées pour le périphérique avec lequel le joueur confirme : clavier et souris, ou manette ; une touche déjà prise est retirée de l'autre action, avec un message ; « Rétablir les touches par défaut » ; Échap ou Start annulent ; écrit à chaque changement). `SliceMenu` fait défiler les longues listes. Vérifié à l'image par des rejeux qui ouvrent les pages.
- **Moteur** : `Input` gagne la réattribution (`set_sources`, `set_axis_sources`, `axis_sources`, `reset_sources`, `reset_all_sources`, `changed_by_player`, `actions_using`) et la **capture de la prochaine touche** (`begin_capture`, `cancel_capture`, `capturing`, `take_captured` : touche, bouton de souris, molette, bouton de manette, stick ou gâchette au-delà du seuil ; aucune action ne voit rien pendant la capture). Tests.
- **Captures** : la tranche **livrée et installée** (assets préparés, archives) donne la même image que celle du bac à sable (voir partie 10).
- **Premiers assets de l'ARPG dans la tranche** (2026-10-17, à la demande de l'utilisateur, après ses imports) : le **lampadaire gothique** et les deux **sols de pavés humides** du jeu (`ARPG/assets/`), copiés par `tools/models/fetch_arpg_models.py` (le `.glb` déballé en `.gltf`, ses textures converties en KTX2, hors de Git comme les autres assets de test, crédités dans `assets/credits.json`). La tranche pose ses pavés en dalles de 8 m (la variante B par plaques) à la place du damier, et un lampadaire par salle : une tuile `lampadaire` non praticable de la carte, placée par `tools/maps/place_slice_lamps.py` dans le premier coin libre de chaque salle (98 salles sur 100), le modèle et sa lumière lue dans son `.prop.json`. Sans ces fichiers, la tranche garde son damier et des poteaux simples. Archive livrée `decor.pak` (30 Mo). Captures de référence enregistrées à nouveau : tranche `da6c481570e6`, exactitude `dc24e9427ee5` (les autres inchangées) ; la tranche installée donne la même.
- **Défaut du moteur trouvé avec le lampadaire** : les pipelines d'ombre des lumières ponctuelles étaient en *depth clamp* (comme ceux du soleil, où c'est voulu) : la géométrie entourant la lumière n'était jamais découpée par le plan proche, et le bol de fer sous la flamme couvrait le sol d'un disque noir de plusieurs mètres (et cachait les poteaux). Corrigé : découpage en profondeur pour les ombres ponctuelles, et **`PointLight::shadow_near` / `LightSource::shadow_near`** (plan proche réglable par lumière, comme le *shadow near plane* d'Unity ; 0 : automatique) : ce qui est plus près de la lumière ne fait pas d'ombre. Le lampadaire utilise 0,55 m (son bol descend à 0,5 m sous la flamme), écrit par son pipeline Blender (`pipeline_export.py`) dans le `.prop.json`, lu par la scène « rue » de l'ARPG (`ARPG/src/prop.*`). Test. Les captures existantes sont inchangées (la lumière des braseros n'avait rien autour d'elle).
- **Reste (utilisateur)** : jouer la version installée du titre au retour au titre **au clavier-souris et à la manette** (avec les nouvelles pages d'options), sur la machine propre ; le **rattrapage Mac** (TEST_MAC.md, jalons 2 à 8) : repoussé par l'utilisateur.

---

## 13. Critères de fin de jalon

Le jalon est terminé quand **tout** ce qui suit est vrai. État au 2026-10-17 (Windows fait, le Mac repoussé par l'utilisateur) :

- [ ] Le **script de vérification** (`tools/ci/verify.py`) construit et teste le moteur, le bac à sable, l'éditeur, l'exemple minimal et la tranche sur **Windows et macOS** (Debug, Release, livré), sans avertissement, avec les sanitizers sur les tests ; le MSL versionné est vérifié ; il produit les paquets. *(CI hébergée repoussée, décision du 2026-10-05.)* — **Windows : fait** (toutes les étapes, paquet et installation compris) ; macOS : à passer.
- [x] Le moteur est une **bibliothèque** consommée par un projet extérieur (exemple minimal, squelette du dépôt ARPG) par une seule cible et une fonction CMake ; il a une **version**.
- [ ] La **configuration minimale** est écrite ; les **scènes de mesure** tournent par un script sur les deux OS ; les **budgets** (16,7 ms au 99ᵉ centile, aucune image > 33 ms en jeu, mémoire, chargement) sont tenus sur le Mac M3, ou les écarts restants sont expliqués et acceptés. — Écrite, script fait, mesures du PC faites ; **mesures du M3 à faire**.
- [x] Le **chargement en arrière-plan** fonctionne : écran de chargement fluide, rejeux et captures inchangés.
- [x] La **configuration livrée** n'a ni ImGui, ni efsw, ni profiler ; elle lit uniquement des **assets préparés** dans des **archives** vérifiées ; même capture de référence que le développement (vérifiée aussi sur la tranche **installée**).
- [ ] Un **plantage** provoqué laisse un rapport dont on tire une pile avec noms de fonctions, sur les deux OS ; les symboles de chaque version sont archivés. — Windows fait (symboles par version dans `packages/symbols/`) ; macOS à essayer.
- [x] Les **options d'affichage** (fenêtré, plein écran fenêtré, écran, résolution de rendu, synchronisation, anticrénelage, ombres) s'appliquent sans redémarrer et sont retenues (essai à la main du 2026-10-17).
- [x] **ASan** propre sur les tests et la tranche ; chaque lecteur de fichier est passé au **fuzzing**. *L'endurance de 8 heures a été retirée par l'utilisateur (2026-10-17).*
- [x] La **référence de l'API** (Doxygen, sans avertissement), le **guide** et la **référence des formats** sont écrits.
- [ ] L'**installeur Windows** et la **`.dmg`** (non signé / signée ad hoc, décision du 2026-10-05 ; la signature est prête à s'activer) s'installent sur des machines propres ; la tranche installée se joue du titre au retour au titre, au clavier-souris et à la manette, avec sauvegarde et chargement. — Installeur fait et vérifié automatiquement (installation, capture, désinstallation) sur la machine de développement ; **machine propre, Windows 10, manette et `.dmg` : à l'utilisateur**.
- [ ] Les vérifications de [TEST_MAC.md](TEST_MAC.md) des jalons 2 à 7 sont faites ou expliquées. — Repoussé par l'utilisateur (section jalon 8 ajoutée à la liste).
- [x] Les nouvelles bibliothèques et les nouveaux outils sont dans les crédits (`assets_moteur/credits_moteur.json` pour le moteur, `assets/credits.json` pour les assets ; fenêtre « About ») et leurs licences partent dans les paquets. *Aucune nouvelle bibliothèque dans ce jalon depuis Zstandard ; Doxygen et Inno Setup sont des outils de build, absents du programme (le programme d'installation d'Inno Setup est libre d'usage, sans mention exigée).*
- [x] Les décisions de la section [Décisions à consigner](#décisions-à-consigner) sont remplies ; [FICHIERS_DU_PROJET.md](FICHIERS_DU_PROJET.md) est à jour.

---

## 14. Risques principaux

| Risque | Impact | Parade |
|---|---|---|
| Le Mac M3 ne tient pas 60 images par seconde dans le combat chargé | Machine minimale à revoir, ou qualité à baisser | Mesurer **tôt** (partie 4 avant toute autre optimisation) ; résolution de rendu par défaut sur Retina ; réglages de qualité ; candidats d'optimisation connus |
| Dette Mac plus grosse que prévu (Metal, MSL, Retina, audio) | Jalon qui s'allonge | CI macOS dès la partie 2 (compilation et tests au moins) ; rattrapage par jalon dans TEST_MAC.md, du plus ancien au plus récent |
| Délais d'Apple et de Microsoft (inscription, validation d'identité, notarisation) | Paquets bloqués en fin de jalon | Comptes demandés **au début** du jalon ; paquets non signés produits d'abord, la signature ajoutée ensuite |
| Coût de la CI (minutes macOS, dépôt privé) | Facture ou CI coupée | Cache vcpkg ; paquets sur étiquettes seulement ; runner auto-hébergé sur le Mac en repli |
| Chargement en arrière-plan qui casse le déterminisme ou crée des courses | Rejeux divergents, plantages rares | La logique ne lit que des assets prêts ; GPU sur le fil principal ; ASan / TSan ; rejeux et captures à chaque étape |
| « Marche en dev, pas en livré » (asset non préparé, code seulement dans une configuration) | Version livrée cassée découverte tard | CI construit la configuration livrée ; option pour jouer en local sur assets préparés ; capture de référence de la version livrée |
| Optimisation prématurée ou sans fin | Jalon interminable, code compliqué | Budgets écrits ; on n'optimise que ce que la mesure montre, et on s'arrête quand le budget tient |
| Documentation écrite une fois, jamais maintenue | Référence fausse | Avertissements de Doxygen en erreur dans la CI ; référence générée depuis les en-têtes |
| Surface publique du moteur trop large | L'ARPG dépend de détails internes | Relecture de `include/moteur/` en partie 3 ; exemple minimal qui n'utilise que la surface publique |

---

## Décisions à consigner

Questionnaire du 2026-10-05 pour les décisions propres à l'utilisateur (comptes, coûts, matériel) ; toutes les autres recommandations du document validées le même jour (« je valide tout »). À compléter au fil du jalon.

| Sujet | Décision | Raison |
|---|---|---|
| Service de CI ; visibilité du dépôt ; captures en CI | **CI repoussée** (« plus tard »). Dans ce jalon : un **script de vérification local** portable (`tools/ci/verify.py`) qui fait exactement les étapes prévues pour la CI ; la CI le reprendra tel quel. Pas de captures en CI | Choix de l'utilisateur (2026-10-05) |
| Consommation du moteur par l'ARPG (sous-module, port vcpkg, dossiers voisins) ; statique ou dynamique ; version | **Sous-module git + `add_subdirectory`**, étiquettes de version ; bibliothèque **statique** ; `moteur::version()`, jalon 8 = `0.8.0` | Recommandation validée par l'utilisateur (2026-10-05) |
| Configuration minimale et recommandée (Windows, Mac) ; machine de mesure | **Mac M3** = machine minimale **mesurée** ; cible Windows écrite : classe GTX 1060 / RX 580, 4 cœurs, 8 Go, 1080p 60 images par seconde en réglages bas ; le PC (RTX 4070 Ti SUPER) = machine recommandée | Recommandation validée par l'utilisateur (2026-10-05) : pas de PC faible à disposition |
| Budgets (image, CPU, pics, mémoire, chargement) | Sur la machine minimale, combat chargé : **16,7 ms au 99ᵉ centile**, CPU du fil principal **≤ 8 ms**, **aucune image > 33 ms** en jeu, processus **≤ 2 Go**, GPU **≤ 1,5 Go**, zone **≤ 5 s**, retour au titre **≤ 1 s** | Recommandation validée par l'utilisateur (2026-10-05) |
| Système de tâches (maison, enkiTS, aucun) ; multithreading des systèmes | **Maison**, petit ; le chargement l'utilise ; les systèmes de jeu **seulement si une mesure le demande** | Recommandation validée par l'utilisateur (2026-10-05) |
| Configuration livrée : ImGui, console, profiler, assertions | ImGui, profiler, Tracy, efsw **retirés** (non compilés) ; **pas de console**, arguments de lancement (`+set`) seulement ; assertions qui protègent les données du joueur gardées | Recommandation validée par l'utilisateur (2026-10-05) |
| Préparation des assets : quand, quoi (textures, tables, cartes, clips, shaders) | Cible CMake incrémentale (`moteur_cook`) ; textures BC7 / BC5 prêtes, tables `.mdat`, shaders ; cartes et clips préparés **si la mesure le justifie** ; option pour jouer en local sur les assets préparés | Recommandation validée par l'utilisateur (2026-10-05) |
| Archive : format, compression, découpage, chiffrement | **Format maison**, entrées alignées et triées, **Zstandard** par entrée selon le type, plusieurs archives par usage, **pas de chiffrement** | Recommandation validée par l'utilisateur (2026-10-05) |
| Rapport de plantage : maison ou bibliothèque ; envoi automatique | **Maison** (minidump réduit sous Windows, pile + journal sous macOS) ; envoi automatique décidé par le jeu plus tard | Recommandation validée par l'utilisateur (2026-10-05) |
| Options d'affichage : modes de plein écran | **Fenêtré et plein écran fenêtré** ; exclusif seulement si SDL_GPU le gère proprement | Recommandation validée par l'utilisateur (2026-10-05) |
| Documentation : outil, langue de la référence, avenir de FICHIERS_DU_PROJET.md | **Doxygen** seul ; référence en **anglais** (depuis le code), guide et formats en **français** ; FICHIERS_DU_PROJET.md **gardé** pour l'architecture, renvoyant à la référence | Recommandation validée par l'utilisateur (2026-10-05) |
| Windows : installeur, CRT, signature | Signature : **pas pour l'instant** ; l'étape existe dans `moteur_package`, activée quand une identité est fournie. **Inno Setup** + archive portable ; **CRT statique** (`/MT`) pour la version livrée | Choix de l'utilisateur (2026-10-05) (signature) ; Recommandation validée par l'utilisateur (2026-10-05) (installeur, CRT) |
| macOS : Apple Developer Program, architecture, App Store | Apple Developer Program **plus tard** : `.app` et `.dmg` **signés ad hoc** (non notarisés) dans ce jalon ; signature Developer ID et notarisation prêtes dans `moteur_package`, activées quand le compte existera. **arm64 seulement** ; **pas** de Mac App Store | Choix de l'utilisateur (2026-10-05) (compte) ; Recommandation validée par l'utilisateur (2026-10-05) (architecture, App Store) |
