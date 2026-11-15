# Universal GNSS ESP32 Web Flasher

Le [portail source](../flasher.html) et son badge README sont intégrés à UG.
Le catalogue et le registre d’approbation sont volontairement vides : aucune
image installable, URL Pages active ou publication n’est annoncée. Le portail
nécessite HTTPS (ou localhost), WebCrypto et, pour le flash, un navigateur avec
Web Serial. Les téléchargements restent indépendants du support Web Serial.

## Configuration portable actuelle

Le [contrat portable v1](../../embedded/esp32/PORTABLE_CONFIG.md) définit le fichier
anonyme, les GPIO, l’import/export et la restauration locale USB. Le portail
propose désormais deux choix de ROM : S3 Gateway et C3 générique à défaut USB.
Ce même C3 sélectionne USB/UART, TX et débit depuis NVS ; ces champs ne sont pas
ajoutés aux demandes GitHub ni à l’identité de build. L’identifiant historique
`c3-rover-usb` est conservé. L’ancienne variante `c3-rover-uart` ci-dessous reste
un profil technique de validation à valeur initiale UART, pas une nécessité pour
une installation utilisateur. Les builds qualifiés antérieurs au support portable
v1 ne sont pas acceptés comme images compatibles par le portail actuel.

Le bouton de restauration ouvre uniquement une liaison USB locale autorisée et
présente un aperçu avant confirmation. Son transport logiciel est testé ; il ne
prouve pas la restauration physique d’une carte et ne lance ni flash ni reset.

## Profils et configuration

| Variante de ROM | Matériel fixé | Configuration à l’exécution |
| --- | --- | --- |
| `s3-gateway` | Waveshare ESP32-S3 ETH, W5500, Flash 16 MB, PSRAM 8 MB | BASE GNSS ou relais NTRIP sans GNSS actif, moteur commun ; GNSS et NTRIP dans la WebUI |
| `c3-rover-usb` | SuperMini ESP32-C3, Flash 4 MB, sans PSRAM/Ethernet | Pair, canal et clé privée par maintenance USB locale |
| `c3-rover-uart` | Même C3, sortie UART1 TX | Même maintenance USB ; broche TX et débit sont sélectionnables en NVS (ancienne valeur initiale CI conservée) |

UART runtime : GPIO4/5/6/7/10, 115200/230400/460800/921600 baud. Une sélection ne
qualifie pas le câblage. La CI ordinaire conserve ses quatre variantes de référence,
y compris le C3 relais expérimental ; le catalogue expose seulement les trois
variantes ci-dessus, sans produit cartésien de GNSS, services, clés ou canaux.
Aucun profil C6, LoRa ou répéteur radio n’est proposé. La distribution future
reste unique : `esp32`.

Le schéma de profil **2**, défini dans
[profiles.py](../../embedded/esp32/native/profiles.py), refuse champs inconnus,
MAC, canal et clés. La clé de build est le SHA256 du commit Git complet, suivi
d’un saut de ligne et du JSON canonique du profil, tronqué à 20 caractères.
Le SDK épinglé et les outils font partie de ce commit. Le choix GNSS/NTRIP affiché
par le portail est une préparation à la configuration après installation, pas
un paramètre appliqué par le flash. Voir le [contrat d’appairage](../../embedded/esp32/PAIRING.md).

## Demande de compilation réelle et contrôlée

Une page publique ne contient aucun jeton GitHub. Son lien de demande crée une
**issue de revue**, pas une compilation. Aucun événement `issues` n’exécute de
texte utilisateur. Le registre [approved-sources.json](approved-sources.json)
contient uniquement des SHA complets, arbres Git, références de revue et droits
de publication explicitement approuvés par le mainteneur.

Pour publier les deux firmwares alpha, utiliser désormais la
[procédure alpha](ALPHA.md) : le lancement manuel **ESP32 alpha publication** sur
`main` autorise les builds, génère leur registre, archive les images durablement
et déploie Pages. Aucune seconde édition manuelle du registre n’est requise.

Le workflow de revue seul reste disponible pour une demande particulière sans
publication. Sa procédure est distincte :

1. Le mainteneur examine la demande, le commit et son arbre, puis intègre une
   approbation dans le registre de la branche par défaut. `not_authorized`
   permet le build de revue sans autoriser sa publication ; `main_approved`
   reste une décision explicite distincte et exige une source contenue dans `main`.
2. Après intégration du [workflow manuel](../../.github/workflows/esp32-request.yml)
   à la branche par défaut, lancer **ESP32 requested firmware validation**, avec
   le SHA approuvé et l’une des trois variantes. Le contrôleur valide les entrées
   avant récupération du commit. Exécuter depuis une autre branche échoue.
3. Le run Actions constitue l’état vérifiable de la vraie compilation : SDK
   officiel épinglé, source archivée sans overlay hôte, builds non-root sans
   réseau après provisionnement, tests et deux reconstructions indépendantes.
   Le workflow est sérialisé, limité à 40 minutes et `contents: read`.
4. Le bundle de revue Actions expire après **7 jours**. Il n’est ni une archive
   durable ni une publication officielle. Une erreur de build, test ou
   empaquetage empêche sa production ; l’issue ne peut déclarer le build réussi.

Ce workflow de revue exige son registre et ne publie pas son résultat. Il ne
conditionne pas le lancement manuel de la chaîne alpha.
Pour une validation locale de développement autorisée, depuis la racine du dépôt :

```bash
SOURCE_SHA=$(git rev-parse HEAD)
python3 embedded/esp32/native/ci.py --source-ref "$SOURCE_SHA" \
  --variant c3-rover-usb --bundle-output /tmp/ug-review-c3
```

La destination doit être nouvelle et extérieure au dépôt. Pour UART, utiliser
`--variant c3-rover-uart --uart-tx 4 --uart-baud 115200`. Le script ne publie
ni ne flashe ; la qualification est liée au commit exact, pas au working tree.
Le contrôleur `tools/branches/firmware_request.py` sans `--execute` ne fait que
valider une demande et indique explicitement `build_started: false`.

## Binaires, partitions et contrôle navigateur

[package_firmware.py](../../tools/package_firmware.py) exige le rapport CI du
commit, le profil canonique et `native-verification.json`. Il contrôle les
véritables métadonnées IDF, le sdkconfig, la cible/Flash/PSRAM, les sorties UART,
les empreintes des quatre segments, leurs offsets et la table de partitions
avec son checksum. Application, bootloader, table et otadata sont comparés entre
les deux builds. Les segments ne peuvent chevaucher NVS ni une autre partition
protégée. Aucun offset historique n’est réutilisé.

Exemple d’empaquetage d’un build **déjà qualifié**, depuis la racine :

```bash
python3 tools/package_firmware.py \
  --build /tmp/ug-ci-a/build --verified /tmp/ug-ci-a/native-verification.json \
  --profile-json /tmp/ug-profile.json --source-sha "$SOURCE_SHA" \
  --qualification /tmp/ug-qualification.json --output /tmp/ug-packaged
```

Le résultat est `published/<build_key>/` : quatre bins, manifeste ESP Web Tools,
provenance, rapport, notices, référence source et ZIP déterministe. Le runner
écrit aussi `catalog-entry.json`. Cet outil ne compile ni ne signe les images.
Le rapport lie les empreintes et offsets mais n’est pas une attestation signée
indépendante ; la chaîne de confiance reste le dépôt et sa procédure de revue.

Avant d’activer le bouton de flash, le navigateur contrôle la source approuvée,
la cohérence catalogue/provenance/rapport, le profil, les partitions et tous les
SHA256 des segments. Il borne les téléchargements et refuse les chemins hors
du portail. Les octets vérifiés sont figés en Blob URLs, y compris le manifeste
transmis à ESP Web Tools : pas de seconde lecture distante mutable pendant le
flash. Un changement de sélection annule les lectures et révoque les anciennes
URLs. Une entrée ambiguë, corrompue ou non approuvée reste refusée.

**Installation complète uniquement** : confirmation explicite de l’utilisateur,
avertissement d’effacement et sauvegarde préalable. Le navigateur distingue la
famille de puce via ESP Web Tools ; il ne prouve pas le modèle de carte, son
câblage, sa PSRAM ni le contenu NVS existant. Aucune mise à jour conservant la
configuration n’est proposée tant que partitions et schémas ne sont pas qualifiés.
La compatibilité effective Web Serial/Blob manifeste reste à accepter sur matériel.

## Conservation durable et préparation Pages

La politique reste `dev → main → distributions générées`. La chaîne
[`esp32-alpha.yml`](../../.github/workflows/esp32-alpha.yml) reçoit cette
autorisation par son déclenchement volontaire sur `main`, pour un SHA exact
contenu dans `main`. Elle conserve les deux bundles, le code source correspondant,
les notices et empreintes dans une prépublication immuable, vérifie les assets
retéléchargés puis déploie Pages. Une archive Actions temporaire ne la remplace
pas. La qualification matérielle manquante est affichée comme limite alpha,
sans désactiver une installation dont les images et la cible ont été vérifiées.

[prepare_esp32_site.py](../../tools/branches/prepare_esp32_site.py) prépare un
répertoire statique local, sans déployer. Sans bundle, il produit un site source
au catalogue vide. Avec des bundles, il exige l’approbation `main_approved`,
l’ascendance Git réelle dans `origin/main` et l’identité de l’arbre. Il vérifie
les fichiers et le ZIP, refuse doublons et écrasements ; le miroir Pages est
borné à 512 images/512 MiB, sans se substituer au stockage durable.

```bash
python3 tools/branches/prepare_esp32_site.py --output /tmp/ug-pages-review
# Après autorisation de publication et archivage durable seulement :
# ajouter --bundle /chemin/du/bundle-revu (répétable).
```

Le workflow alpha déploie **ce répertoire préparé**, jamais tout le dépôt. La
commande locale ci-dessus reste une préparation sans publication.
Les ressources sont relatives et `.nojekyll` est inclus. Le chemin envisagé est
`https://pepeuch.github.io/universal-gnss/flasher.html` ; il n’est pas déclaré actif.
Aucune intégration avec le workflow Release Docker n’est ajoutée.

## Licences et limites

- Adaptation `ADAPT` de l’expérience de sélection historique au commit
  `7a1ec69f5e9d07522c0284ec2dbae380f72c7e85` de `esp32-rtk-gateway`, GPLv3.
  Anciennes variantes radio compilées et anciens offsets : `SUPERSEDED`.
  Le portail UG et son JS portent conservativement `GPL-3.0-only`.
- ESP Web Tools **10.4.0**, chargé séparément depuis un CDN :
  [Apache-2.0](https://github.com/esphome/esp-web-tools/blob/10.4.0/LICENSE).
  Le navigateur nécessite cet accès ; ce n’est pas un portail entièrement hors ligne.
- Le bundle combiné conserve GPLv3 pour les composants historiques et LGPLv3
  pour les bibliothèques UG, avec les notices SDK/dépendances et référence source.
  L’acceptation juridique de publication, y compris la disponibilité du code source
  correspondant et des notices tierces, reste distincte de la validation logicielle.

Les tests logiciels ne qualifient ni RF/CCMP, ni cycle USB réel, ni W5500/UART,
ni RTK FIX. Ils ne lèvent aucune quarantaine GNSS et n’effacent pas les dettes
historiques. Les rapports natifs et le [contrat d’appairage](../../embedded/esp32/PAIRING.md)
distinguent les budgets statiques, les tests host et l’acceptation matérielle.

## Qualification logicielle locale de ce lot

Source exacte du portail et des exports :
`aa130f0d252940035cffbaacf7307cbc621889e2`.
Les commandes `ci.py --bundle-output` ont été réellement exécutées pour S3 et C3
USB dans deux conteneurs jetables indépendants, sans montage hôte ni périphérique.
Chaque run a compilé deux fois et comparé les quatre segments, puis exporté le
bundle avant nettoyage. Les deux runs et leur nettoyage ont réussi.

| Variante | App `.bin` / slot (octets) | Mémoire liée (octets, pas le heap libre) | Preuve |
| --- | --- | --- | --- |
| S3 Gateway | 1 451 296 / 3 145 728 | DIRAM 215 004 + IRAM 16 384 | Build/reconstruction/export rejoués sur le SHA ci-dessus |
| C3 Rover USB | 687 648 / 1 310 720 | DRAM 111 114 | Build/reconstruction/export rejoués sur le SHA ci-dessus |
| C3 Rover UART | 699 136 / 1 310 720 | DRAM 111 776 | Deux builds validés au commit d’appairage `7182f5b4d408fdd9219678130484c50cbc308a09`, code firmware inchangé depuis |
| C3 relais interne | 1 305 760 / 1 310 720 | DRAM 160 534 | Même preuve conservée ; marge de slot **4 960 octets**, absent du catalogue |

Offsets réels vérifiés dans ces exports : bootloader `0`, table `0x8000`,
otadata/application `0xf000`/`0x20000` sur S3 et `0xe000`/`0x10000` sur C3.
Ces chiffres ne sont pas des offsets à substituer à la lecture des futurs builds.

Contrôles effectivement rejoués :

- 235 tests du générateur et de politique CI, 55 tests Python existants.
- 40 CTests natifs ASan/UBSan dans le run S3, 8 tests des outils de capture,
  1 test WebUI et 8 tests JavaScript du portail public.
- Acceptation des véritables octets des deux bundles par le vérificateur JS,
  dans une VM avec une approbation **fictive en mémoire pour le test uniquement**.
  Le registre public reste vide ; cette vérification n’autorise aucune publication.
- Intégrité SHA256 et identité des 11 entrées de chaque ZIP avec ses fichiers.
- Huit combinaisons ROS2 générées deux fois, intègres et reproductibles ;
  inventaires, octets copiés et modes identiques à la référence avant ce lot.
- 50 cibles de liens Markdown locaux, parsing Python/YAML, staging Pages vide,
  dashboard, checkpoint audit et `git diff --check`.

La VM de test n’exerce pas Web Serial ni les dialogues réels d’ESP Web Tools.
Le SDK, le CRC UG unique et les standards C17/C++17 sont vérifiés dans les ELF.
Aucun build Lyrical/Kilted, scénario matériel, flash ou contrôle GitHub distant
n’a été ajouté pour ce lot du portail. Les résultats CI distants de l’ancien
commit ne deviennent pas verts par l’effet d’un commit local. Autorisation de
push, intégration à la branche par défaut, approbations, stockage durable et
acceptation matérielle restent nécessaires avant mise à disposition publique.
