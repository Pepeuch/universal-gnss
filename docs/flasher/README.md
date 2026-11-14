# Universal GNSS ESP32 Web Flasher

Le [portail source](../flasher.html) et son badge README sont intégrés à UG.
Le catalogue et le registre d’approbation sont volontairement vides : aucune
image installable, URL Pages active ou publication n’est annoncée. Le portail
nécessite HTTPS (ou localhost), WebCrypto et, pour le flash, un navigateur avec
Web Serial. Les téléchargements restent indépendants du support Web Serial.

## Profils et configuration

| Variante de ROM | Matériel fixé | Configuration à l’exécution |
| --- | --- | --- |
| `s3-gateway` | Waveshare ESP32-S3 ETH, W5500, Flash 16 MB, PSRAM 8 MB | BASE GNSS ou relais NTRIP sans GNSS actif, moteur commun ; GNSS et NTRIP dans la WebUI |
| `c3-rover-usb` | SuperMini ESP32-C3, Flash 4 MB, sans PSRAM/Ethernet | Pair, canal et clé privée par maintenance USB locale |
| `c3-rover-uart` | Même C3, sortie UART1 TX | Même maintenance USB ; broche TX et débit sont des options de compilation |

UART : GPIO4/5/6/7/10, 115200/230400/460800/921600 baud. Une sélection ne
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

Procédure intermédiaire :

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

Tant que le workflow n’est pas intégré à la branche par défaut et que le registre
reste vide, aucun lancement public n’est disponible. Ne pas simuler cet état.
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

La politique reste `dev → main → distributions générées`. Avant toute publication,
le mainteneur doit accepter les limites matérielles/licences, approuver la source
contenue dans `main`, puis autoriser une **opération séparée** de stockage durable
(assets d’une GitHub Release ou stockage versionné approprié). Aucun workflow de
ce lot ne crée de Release, tag, branche spécialisée ou déploiement Pages.
L’archive durable doit conserver le bundle, sa référence source exacte, les
notices et les empreintes. Une archive Actions temporaire ne la remplace pas.

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

Déployer **ce répertoire préparé** après autorisation distincte, pas tout le dépôt.
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
