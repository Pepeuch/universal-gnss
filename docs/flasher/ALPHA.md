# ESP32 ALPHA : publication et premiers essais

La cible du Web Flasher est
[pepeuch.github.io/universal-gnss/flasher.html](https://pepeuch.github.io/universal-gnss/flasher.html).
Le succès du déploiement et de sa vérification HTTP est la preuve de disponibilité,
pas la présence de ce lien. Au démarrage de cette préparation, cette URL retournait
HTTP 404 ; aucun déploiement public n’est effectué depuis la branche de préparation.

La **compilation de développement** reste indépendante de la publication : le
workflow `ESP32 native validation` compile S3 Gateway, C3 Rover USB et C3 Rover
UART sur **chaque push de toute branche ou tag**, même sans PR, ainsi que sur les
PR. Aucun registre d'approbation ni appartenance à `main` n'est requis pour ces
builds. Leurs échecs restent signalés ; ils n'autorisent pas une publication.

## Publication normale : publier la Release

Le parcours courant utilise le workflow existant
[`esp32-alpha.yml`](../../.github/workflows/esp32-alpha.yml), sans infrastructure
supplémentaire : un mainteneur publie une Release `vX.Y.Z-RCn`, la chaîne attend
les sept validations `push/main` sur son SHA exact, construit S3 Gateway et C3
Rover USB, archive leurs fichiers dans **cette Release**, puis actualise Pages.
Une Release publiée avant la fin des CI est réexaminée à leur achèvement.

La publication de la Release par un compte `admin` ou `maintain` constitue
l'autorisation : aucune entrée d'approbation par version n'est requise pour ce
parcours RC. Le mainteneur a explicitement reporté les rulesets de tags ; leur
protection n'est donc pas revendiquée. Les contrôles de SHA/arbre, d'appartenance
à `main`, de CI et d'intégrité restent obligatoires. Une ancienne révocation
explicite n'est pas annulée automatiquement.

Les deux firmwares réutilisent le runner natif et le SDK épinglé : utilisateur
non-root, sources archivées, aucun périphérique ou montage des sources hôtes,
réseau déconnecté après provisionnement et double reconstruction reproductible.
Le profil C3 UART conserve son workflow de demande existant. Les images Docker
Kilted/Lyrical AMD64/ARM64 continuent d'être construites par `docker-release.yml`
sur le tag `v*`, sans modification de ce workflow ni second build Docker.

## Rattrapage et maintenance

Le lancement manuel **Actions → ESP32 alpha publication → Run workflow** reste
accessible depuis `main`. Pour une RC, indiquer son **tag existant** et son **SHA
complet** ; les mêmes contrôles s'appliquent. Il permet notamment le rattrapage
de `v0.7.5-RC1` après déploiement du workflow sur `main`, car un ancien événement
`published` n'est pas rejoué par GitHub.

Sans tag RC, le parcours alpha historique `esp32-alpha-<SHA court>` reste soumis
au registre explicite `approved-sources.json`. `0.7.5-a01` n'est accepté que si
son tag existant correspond au SHA sélectionné. Aucun tag RC n'est créé ou déplacé.

## Archive, reprise et Pages

La chaîne RC ajoute uniquement des fichiers `esp32-*`, sans remplacer les assets,
le titre, la description ou le tag de la Release. Elle publie d'abord l'archive du
site, puis les bundles, manifestes, provenances, qualifications, segments binaires,
source et sommes SHA256. Le marqueur `esp32-publication.json` arrive **en dernier**,
après relecture et vérification de tous les fichiers. Pages n'est déployé qu'ensuite.

Une relance vérifie les fichiers existants et ajoute seulement les manquants.
Après interruption, l'archive initiale est réutilisée et revalidée, sans reconstruire
les firmwares ni changer leurs métadonnées. Un conflit d'octets ou un upload incomplet
non vérifiable arrête l'opération sans écrasement. L'API GitHub ne propose pas de
transaction multi-assets : des fichiers en attente peuvent être visibles dans la
Release pendant l'upload, mais ne constituent pas une publication complète tant
que le marqueur manque. Le workflow Web seul les ignore.

Le catalogue conserve les versions précédentes et tous leurs octets. Les deux
workflows Pages partagent le verrou `esp32-alpha-publication`, sans annulation
d'un déploiement en cours. La date RC est celle de publication de la Release,
avec SHA complet, canal `RC` et statut logiciel. Une modification HTML/CSS/JS
n'autorise et ne compile aucun firmware.

L'environnement `github-pages` doit autoriser les branches `main` et les tags
`v*-RC*`. C'est un réglage unique de compatibilité du déploiement, pas une
approbation à répéter pour chaque version. Les permissions d'écriture restent
limitées aux jobs de publication ; les builds restent `contents: read`.

Le [rapport et contrat Release](../releases/esp32-release-publication.md) précise
les tests, la politique actuelle, les limites et le rattrapage de RC1. Aucun essai
logiciel ne qualifie le RTK, le câblage, la portée radio ou une mise à jour NVS.

## Installation et configuration locale

Choisir la carte exacte, attendre la vérification de tous les segments, puis
confirmer l’installation complète. Les images expérimentales ne sont pas des
preuves de RTK FIX, de portée radio ou de réception GNSS. Sauvegarder la Flash/NVS
avant tout remplacement d’un firmware existant ; la conservation d’une ancienne
configuration n’est pas annoncée comme une mise à jour qualifiée.

Le C3 générique choisit USB/UART et GPIO/débit depuis NVS. Aucun pair, clé, canal
ou fichier utilisateur n’entre dans la ROM publique. Le S3 garde le câblage W5500
soudé et exige une configuration GNSS réelle avant activation.

Après installation, la console USB du S3 permet le provisionnement local de
l’identité d’administration et TLS ; voir la
[procédure native](../../embedded/esp32/migration/README.md) et le
[contrat portable](../../embedded/esp32/PORTABLE_CONFIG.md).
Le bouton de restauration locale utilise ce protocole ou la maintenance C3,
avec aperçu/confirmation, sans reset ni RUN automatique et sans envoi du fichier
vers GitHub. Les données de contrôle ne partagent pas un flux RTCM actif.

## Activations dans la WebUI du S3

Les formulaires GNSS/UART et NTRIP restent configurables sans récepteur ou
serveur raccordé. La section « Activer / désactiver les fonctions » expose aussi
ESP-NOW, le caster local et les modes Wi-Fi. Un secret laissé vide est conservé ;
désactiver ESP-NOW conserve l’association. Les contraintes de GPIO, de clé/pair
et d’incompatibilité STA/ESP-NOW restent vérifiées avant sauvegarde.

L’activation demandée et l’état observé sont distincts : un périphérique absent
peut laisser un service en attente ou en erreur. Aucun paquet reçu ni état RTK
n’est déduit d’un interrupteur. MQTT n’a pas encore de service natif et LoRa
reste différé ; l’interface les signale indisponibles. La sortie RTCM UART
autonome appartient actuellement au profil Rover ; l’UART S3 est le transport
GNSS configurable. Les changements Wi-Fi et la réactivation radio demandent un
redémarrage explicite.

Les observations et limites du premier essai physique sont consignées dans le
[bilan S3](../../embedded/esp32/HARDWARE_ALPHA.md). Elles ne qualifient pas les
cartes C3/C6 ni un déploiement public du portail.
