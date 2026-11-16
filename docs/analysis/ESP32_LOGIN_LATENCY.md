# ESP32-S3 — latence de connexion

Mesures préalables au gel Git sur `fix/esp32-single-login`, base
`d3c9e0688bc6756bc0f493397d03c7bd3c2f1b40`. La publication de firmware
ne fait pas partie de ce lot.

## Cause établie et compatibilité

Le firmware HTTP installé `706a302a…` fonctionne : le propriétaire confirme sa
connexion, les six onglets et l’affichage du mot de passe NTRIP. La vérification
reste excessivement lente (~88 s). Les mesures antérieures du même vecteur public
isolent le coût de PBKDF2, indépendamment du rendu navigateur : voir le
[rapport précédent](ESP32_WEBUI_PAIRING_AUTH.md#coût-cpu-encore-ouvert).

Le nouvel adaptateur garde **PBKDF2-HMAC-SHA256, 600000 itérations, sel 16 octets,
résultat 32 octets et enregistrement NVS v1 de 56 octets**. Il ne réinitialise ni
mot de passe, ni certificat, ni configuration. Les règles de session et les
comparaisons de résultats restent inchangées. Aucun nouveau protocole de preuve,
cache persistant de mot de passe ou changement de configuration TLS.

## Adaptation du calcul

`ug_platform/include/ug_password_hmac.hpp` prépare une seule fois les états SHA
après les blocs HMAC inner/outer, puis les clone pour chaque MAC. Les préfixes
ne vivent que pendant une dérivation ; buffers temporaires et états sont effacés
à la sortie, y compris après erreur. Les mots de passe de plus de 64 octets sont
pré-hachés conformément à HMAC. Aucune nouvelle implémentation SHA n’est ajoutée.

L’adaptateur S3 utilise les fonctions `esp_sha256_driver_*` du **SDK 6.1 épinglé**.
Le driver assure le padding et l’arbitrage matériel. Ses contextes sont fournis
par l’appelant, sur la pile interne existante. Le dispatcher PSA général est
écarté pour cette boucle : son clonage alloue/libère un contexte interne à chaque
opération dans ce SDK. Aucun verrou SHA/AES n’est conservé pendant un yield ;
les piles, priorités et seuils mémoire ne changent pas. Le header de driver est
une dépendance privée à requalifier lors d’un changement de SDK.

Les 585 attentes d’un tick sont encore conservées. À 100 Hz, elles constituent
à elles seules environ 5,85 secondes : aucune promesse de connexion instantanée.
L’accélération effective doit être mesurée, pas déduite du succès du build.

## Validation effectuée

- Équivalence HMAC avec OpenSSL : clés 0/1/8/63/64/65/128/257 octets et messages
  0/8/32/65 octets, dont octets non ASCII ; réinitialisation avec une autre clé.
- PBKDF2 comparé à OpenSSL pour 1/2/1024/600000 itérations et le même sel.
- Treize points d’échec injectés : initialisation, pré-hachage, clonage, update,
  finish ; rejet et effacement des résultats et états partiels.
- **68/68 CTest ASan/UBSan** réussis (8,57 s sur l’hôte).
- S3 HTTP : deux builds indépendants, vérification native et reproductibilité
  des quatre segments réussies, arbre `f7614f0064bc0054824c090371492a802f249ca0`.
  Application **1485216 octets**, SHA-256
  `9df28836b0afd95f990160d051d7e46179423af880f9091fac2005ac6a3e4328`.
  Cet arbre contient aussi la lecture USB d’identité prévue pour l’appairage ;
  l’écart Flash avec `706a302a…` (+1568 octets) ne mesure donc pas le seul KDF.

## Qualification restante

Le témoin HTTPS passe aussi ses deux builds, vérifications et reproductibilité
(1490000 octets, SHA-256
`0b9171b6cbe5c5c271c33a2d57b2ddc85d5a2b12383202985f3b9bb4c37e8052`).
Les segments bootloader/table/OTA sont identiques aux témoins précédents pour
les deux transports. Le binaire HTTP lie les fonctions de clonage/update/finish
du pilote SHA vérifiées par la table des symboles ELF. `.dram0.data` reste
23757 octets, `.dram0.bss` passe de 70512 à 70576 octets, dont le nouvel état
de lecture USB ; ces chiffres ne constituent pas une mesure de heap d’authentification.

## Mesures sur le S3 physique

Après accord explicite sur `9df28836…`, application seule installée à `0x20000`
sur Waveshare S3 W5500, MAC STA vérifiée `30:ed:a0:29:d1:c0`, Flash 16 Mio,
PSRAM 8 Mio. SDK et image ci-dessus. Hôte Linux, USB Serial/JTAG pour la maintenance,
Ethernet pour HTTP, radio arrêtée et NTRIP conservé dans son état initial.

Sauvegarde privée fraîche de la zone `0x8000..0x11000` (table, NVS, OTA), vérification
du digest de l’application précédente complète puis copie de l’artefact de retour
`706a302a…`. L’espace inutilisé de son slot n’est pas inclus dans cette sauvegarde
applicative. Aucune écriture de bootloader, partition, OTA ou NVS. La zone protégée
est identique octet pour octet avant/après flash **et après les essais** ; les
réglages, leur génération, les namespaces d’authentification et d’identité TLS
restent identiques. Aucun reset d’identifiant ou changement de mot de passe.

| Mesure console, même vecteur public | Résultat |
| --- | ---: |
| Dérivation 1 | 36,804141 s |
| Dérivation 2 | 36,864662 s |
| Dérivation 3 | 36,844149 s |
| Médiane | **36,844149 s** |

Chaque résultat est comparé au vecteur attendu et les 600000 itérations confirmées.
La référence logicielle historique est 86,221 s : rapport de durées **2,34**,
réduction d’environ **57 %**. Ce rapprochement avec une mesure historique n’est
pas une nouvelle campagne A/B simultanée de tous les firmwares.

Une unique tentative HTTP avec un mot de passe synthétique volontairement invalide
est rejetée par 401 en **38,579958 s** (référence historique HTTP : 88,439 s).
Le vrai mot de passe de l’opérateur n’est pas utilisé par l’agent. L’HTML de connexion
répond 200 en 34 ms ; le statut HTTP reste accessible avant/après le test.

Après chacune des trois dérivations : **zéro allocation échouée**, plus grand bloc
interne **34816 octets**, interne libre **114795 / 114795 / 114811 octets**,
PSRAM libre **8344068 octets**, minimum interne historique **48560 octets**.
Ces observations courtes ne qualifient pas l’endurance ni trois dérivations
simultanées. La légère différence avant/après ne suffit pas à attribuer un coût
permanent au seul KDF, les services réseau continuant à fonctionner.

Les preuves détaillées et sauvegardes restent privées, hors dépôt, sous
`~/.local/share/ug-private/auth-latency-20261116/`. Les scripts de mesure n’écrivent
aucune configuration. C3, clés ESP-NOW, réglages GNSS/NTRIP et profils restent intacts.

## Acceptation restante

**Amélioration mesurée, mais latence encore excessive** : ce lot ne résout pas
complètement le problème ergonomique de connexion. La compatibilité du résultat
cryptographique est vérifiée ; le propriétaire confirme aussi une connexion réussie avec son mot de passe
habituel après le redémarrage, sans partage du secret. La compatibilité avec
l’enregistrement existant est ainsi vérifiée sur cette carte.
Le redémarrage final passe : HTTP en état `running`, page de connexion et
API statut en 200, HTTPS désactivé/TCP 443 refusé, ESP-NOW configuré et réellement
arrêté. Interne libre 115095 octets, bloc 34816 octets, PSRAM 8344068 octets,
zéro allocation échouée. La console USB est libérée.

L’endurance, les connexions simultanées, l’impact sous trafic GNSS/RTCM réel et
la qualification radio restent distincts. Aucun succès GNSS/RTK revendiqué sans
récepteur raccordé. Aucun commit, push ou publication de cette expérimentation.

## Gel Git du travail validé — 16 novembre 2026

Le gel conserve ensemble les corrections de connexion unique, déconnexion et
révocation/reconnexion, les sessions administrateur glissantes et restreintes,
la WebUI, la consultation NTRIP authentifiée, les cinq filtres RTCM indépendants,
la variante HTTP explicite, le backend KDF compatible, la lecture USB d’identité
et les payloads des files réseau en PSRAM. Tests et rapports associés sont inclus.
Le code partagé UG, ROS2/Linux, C3 Relay, partitions, seuils mémoire, versions,
workflows et politiques de publication ne changent pas.

Contrôles rejoués avant commit : 68/68 CTest ASan/UBSan (8,41 s), 23/23 tests
JavaScript, 11/11 tests de politique CI ESP32, formatage clang-format-21, syntaxe
Python, liens locaux et `git diff --check` réussis. Aucune nouvelle compilation
ni campagne matérielle n’est lancée pour le seul gel Git.

Les fichiers de code du gel sont identiques aux entrées de l’arbre S3 validé
ci-dessus ; seuls les rapports et guides ont évolué après ces builds. Les deux
builds indépendants HTTP et HTTPS sont réutilisés, sans présenter leur résultat
comme une nouvelle compilation d’un commit publié. Les cinq parcours Chromium
API/USB simulés et la connexion réelle de l’opérateur sont des preuves distinctes.
La lecture USB d’identité est compilée et couverte par ses tests de fenêtre,
nonce, expiration et refus de commandes ; l’appairage complet depuis le Web Flasher
n’est pas encore implémenté/qualifié. Aucun changement fonctionnel supplémentaire
n’est réalisé pendant le gel.

Limites maintenues explicitement :

- **Authentification HTTP encore lente : environ 38,6 secondes** ; l’amélioration
  mesurée ne clôt pas ce problème d’ergonomie.
- **Marge SRAM avec ESP-NOW actif non qualifiée** : les essais précédents se sont
  arrêtés sur un bloc de 31744 octets, inférieur aux 32768 requis. L’absence
  d’allocation échouée ne transforme pas ce résultat en PASS.
- **Flux GNSS/RTCM réel non validé dans cette campagne** : aucun récepteur GNSS
  raccordé, aucune preuve de RTK FIX ou de latence sous corrections réelles.
- HTTP transmet les identifiants et l’administration en clair sur le LAN ; HTTPS
  reste disponible et demeure le défaut de compilation/publication. Il n’existe
  aucun repli HTTP automatique après une erreur TLS.
- Retour GGA rover vers S3, plusieurs MAC et broadcast, endurance et appairage
  complet restent hors de ce gel, sans être présentés comme fonctionnels.

Les sauvegardes Flash/NVS, certificats, clés, jetons, traces privées, artefacts,
checkpoints locaux, illustrations et audit mémoire préexistants, ainsi que
l’archive WebUI locale, restent exclus du commit. Les préréglages Centipède publics
et les identifiants synthétiques des tests sont distingués des données privées.
La conservation Git de ce travail n’autorise ni nouveau firmware publié ni fusion.
