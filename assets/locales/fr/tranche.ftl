# Les textes de la tranche jouable (jalon 9 bis, partie 5). Le français est la langue source :
# les autres langues traduisent ces identifiants (locales/<langue>/tranche.ftl).

## Mots communs

common-yes = oui
choice-yes = Oui
choice-no = Non
common-no = non
common-back = Retour
common-keep = Garder
common-cancel = Annuler
common-apply = Appliquer
common-percent = { NUMBER($value, style: "percent") }
date-time = { $day }/{ $month }/{ $year } { $hour }:{ $minute }

## Écran titre

game-title = Tranche jouable
title-subtitle = Titre, chargement, la carte avec un héros, pause.
title-help = Flèches et Entrée, croix et A de la manette, ou la souris.
title-error = Échec : { $error }
title-play = Jouer
title-continue = Continuer
title-load = Charger
title-options = Options
title-quit = Quitter

## Chargement

loading-title = Chargement
loading-world = Construction du monde
loading-files = Lecture des fichiers
loading-barrel = Modèle du tonneau
loading-decor = Pavés et lampadaires
loading-characters = Personnages animés
loading-sounds = Sons

## Pause

pause-title = Pause
pause-frozen = Le monde est figé dessous.
pause-help = Échap ou Start : reprendre
pause-resume = Reprendre
pause-save = Sauvegarder
pause-options = Options
pause-quit = Retour au titre

## Sauvegardes

slots-save-title = Sauvegarder la partie
slots-load-title = Charger une partie
slots-help = Flèches et Entrée ; Échap : retour
slots-saved = Sauvegardé dans { $slot }.
slots-failed = Échec : { $error }
slot-manual = Emplacement { $number }
slot-auto = Sauvegarde automatique { $number }
slot-empty = (vide)
slot-unreadable = Illisible : { $error }
slot-when = { $date }, { DURATION($seconds) } de jeu
slot-when-backup = { $date }, { DURATION($seconds) } de jeu (copie de secours)
slot-summary = Vie { NUMBER($health, style: "percent") }, { $creatures ->
        [one] { $creatures } créature debout
       *[other] { $creatures } créatures debout
    }
save-unreadable = Sauvegarde « { $slot } » illisible : { $error }

## Options

options-title = Options
options-display = Affichage
options-audio = Audio
options-controls = Commandes
options-language = Langue

options-display-title = Affichage
options-display-help = Flèches gauche et droite : changer ; Entrée : appliquer
options-display-confirm-title = Garder ces réglages ?
options-display-confirm-countdown = { $seconds ->
        [one] Retour aux réglages précédents dans { $seconds } seconde.
       *[other] Retour aux réglages précédents dans { $seconds } secondes.
    }
options-display-mode-label = Mode
options-display-screen-label = Écran
options-display-screen-number = Écran { $number }
options-display-vsync-label = Synchronisation verticale
options-display-frame-limit-label = Limite d'images
options-display-frame-limit = { $limit ->
        [0] Aucune
       *[other] { $limit } par seconde
    }
options-display-render-scale-label = Résolution de la 3D
options-display-anti-aliasing-label = Anticrénelage
options-display-shadows-label = Ombres
options-display-ui-scale-label = Taille de l'interface
display-mode-fullscreen = Plein écran
display-mode-windowed = Fenêtré
shadows-low = Basses
shadows-medium = Moyennes
shadows-high = Hautes
anti-aliasing-none = Aucun

options-audio-title = Audio
options-audio-help = Flèches gauche et droite : changer
audio-volume = { NUMBER($volume, style: "percent") }
audio-master = Volume général
audio-music = Musique
audio-effects = Effets
audio-ambience = Ambiance
audio-interface = Interface

options-controls-title = Commandes
controls-device-pad = Manette (le clavier : confirmer au clavier)
controls-device-keyboard = Clavier et souris (la manette : confirmer à la manette)
controls-help = Entrée : changer la touche
controls-press-pad = Appuyez sur un bouton de la manette.
controls-press-keyboard = Appuyez sur une touche ou un bouton de la souris.
controls-cancel-pad = Start : annuler
controls-cancel-keyboard = Échap : annuler
controls-layout-label = Disposition
controls-reset = Rétablir les touches par défaut
controls-reset-done = Touches par défaut rétablies.
controls-taken = { $key } retiré de : { $actions }
controls-save-failed = Impossible d'enregistrer les touches.
input-profile-clic = Déplacement au clic
input-profile-zqsd = Déplacement au clavier (ZQSD)

options-language-title = Langue
options-language-help = Le changement s'applique aussitôt.

## Actions (les commandes du jeu)

action-move-to = Se déplacer (au clic)
action-forward = Avancer
action-backward = Reculer
action-left = Aller à gauche
action-right = Aller à droite
action-skill = Compétence { $number }
action-zoom-in = Zoom avant
action-zoom-out = Zoom arrière
action-select = Choisir une créature
action-next = Créature suivante
action-follow = Suivre la créature
action-camera-up = Caméra : haut
action-camera-down = Caméra : bas
action-camera-left = Caméra : gauche
action-camera-right = Caméra : droite
action-pause = Pause

## En jeu

flash-hit = Touché !
flash-skill = Compétence { $number }
hud-world = Démo 3D : carte { $width }×{ $height }, { $creatures } créatures, { $objects } objets fixes, { $torches } torches
hud-scene = scène : { $meshes } / { $meshes_submitted } maillages, { $triangles } triangles, { $draws } draw calls ; ombre : { $casters } / { $casters_submitted }, { $shadow_draws } draw calls
hud-lights = torches : { $shadowed } ombrées, { $updated } recalculées, { $light_draws } draw calls ; billboards : { $billboards } ; draw calls en tout : { $draws }
hud-move = { $layout } : se déplacer { $keys }, compétences { $skills }
hud-hero = { $move } sur une créature proche, ou { $strike } : la frapper (coups : { $hits }). { $follow } : caméra suivie ({ $following }), { $pause } : pause
hud-cell = Case ({ $x }, { $y }).
hud-observer = { $cell }{ $select } : choisir une créature, { $next } : la suivante, { $follow } : la suivre ({ $following })

## Messages du moteur que le jeu remplace (rapport de plantage, carte graphique perdue)

moteur-crash-message =
    Le jeu a rencontré une erreur et doit se fermer.

    Un rapport a été écrit ici :
    { $report }

    Joindre ce fichier (et le .dmp voisin) à un signalement aide à corriger le problème.
moteur-gpu-lost-message =
    La carte graphique ne répond plus (son pilote a été réinitialisé, ou la carte retirée) et le jeu doit se fermer.

    Mettre à jour le pilote graphique corrige le plus souvent le problème.

    Un rapport a été écrit ici :
    { $report }
