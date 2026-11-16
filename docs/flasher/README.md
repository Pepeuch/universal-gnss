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


## Actualisation automatique de l’interface Pages

Après fusion sur `main`, `esp32-web-pages.yml` actualise l’interface lorsque ses
ressources ou scripts de staging changent. Les PR ne déploient pas. Le lancement
manuel est également limité à `main`. Ce workflow ne compile ni n’autorise aucun
firmware ; ses permissions de contenu sont en lecture seule.

La source durable est la plus récente Release publiée non brouillon contenant
`esp32-site.zip` (ou le nom historique `esp32-alpha-site.zip`) et `SHA256SUMS` (par date de publication, parmi les 100
Releases retournées). Une source absente, incomplète ou invalide bloque tout
upload Pages : aucun catalogue vide de secours. L’archive est vérifiée par ses
sommes et, lorsqu’elle est fournie, l’empreinte de l’asset GitHub. Les approbations,
ascendance dans main, qualifications, manifestes, ZIP, tailles, offsets et SHA-256
sont revalidés par les vérificateurs existants. Le registre versionné vide n’est
pas substitué au registre publié. Catalogue, approbations et tous les octets du
payload publié sont conservés à l’identique.

Les URL des ressources locales HTML comportent leur SHA-256 ; modifier JavaScript
ou le logo change leur URL. La publication alpha manuelle utilise le même staging
et conserve ses contrôles. Les deux workflows partagent le verrou global
`esp32-alpha-publication`, sans annulation d’un déploiement en cours. Cela sérialise
également les builds alpha ; les files GitHub ne garantissent pas l’ordre FIFO et
peuvent remplacer une exécution en attente. Le cache du document HTML reste celui
de GitHub Pages : un rechargement peut être nécessaire pendant sa propagation.

La validation locale de ce lot a restauré la véritable Release
`esp32-alpha-a9962cab58ab`, sans publication ni build firmware. Le premier
push/merge du nouveau workflow doit encore qualifier le déploiement Actions et
l’environnement github-pages. Aucun essai matériel n’est requis ou effectué ici.

Tests locaux : 250 tests générateur/politique et 9 tests JavaScript du portail
réussis ; contrôle HTML des ressources, montage depuis la Release réelle et
`git diff --check` réussis. Aucun SDK ou firmware reconstruit.


## Publication firmware autorisée après validation de main (P0)

`esp32-alpha.yml` conserve son lancement manuel et écoute désormais la fin des
sept workflows obligatoires de `main`. Il reconsulte l’API Actions, ne confond pas
un succès de PR avec un succès `push/main`, et exige le succès de la plus récente
exécution de chaque workflow, sur la source autorisée et sur le main portant
le code de publication. Un échec, une annulation, un skip ou une exécution
encore en cours ne permet pas de publier. L’entrée de publication doit également
figurer explicitement dans `approved-sources.json` avec SHA complet, arbre exact
et `main_approved`. Un merge vert seul n’autorise donc pas un nouveau firmware.
Le candidat est la révision autorisée la plus récente dans l’historique main ;
aucun fallback sur une ancienne révision si ses validations ne sont pas terminées.
L’autorité est vérifiée avant build puis de nouveau avant création des assets.

L’autorisation ajoutée par ce lot concerne exclusivement
`52aea50a098a1839b6028a247229d273a92ee302` (demande P0 du mainteneur). Les commits
ultérieurs exigent leur propre entrée revue ; l’automatisation n’ajoute pas
elle-même d’approbation. Les profils construits restent S3 Gateway et C3 Rover USB.
Une Release complète préexistante évite les builds/publications firmware répétés :
son catalogue durable est revérifié puis redéployé sans compilation ;
une publication partielle requiert une récupération explicite. Aucune réécriture
d’asset ou de tag n’est autorisée. Les Releases précédentes restent disponibles.

Avant archivage, le staging réimporte et revalide le catalogue durable précédent,
unionne les sources approuvées et conserve tous ses fichiers firmware. Tout conflit
d’identité ou d’octets et toute limite de capacité bloque la publication, sans
suppression de l’historique. `publications.json` enregistre les sources, leur date
UTC de préparation pour publication et leur statut `validated_authorized_alpha`.
Pour l’ancienne Release, sa date GitHub est récupérée. Le portail affiche le SHA
complet, la date et le statut logiciel ; la révision publiée la plus récente connue est sélectionnée par défaut. Chaque flash revérifie les vrais
manifestes, binaires, offsets et empreintes. Cette validation ne prouve pas le RTK.

Le workflow Web uniquement conserve aussi cet historique et n’approuve ni ne
recompile aucun firmware. Les deux workflows gardent leur verrou partagé. Le
format reste une **installation complète** : une mise à jour préservant NVS reste
`not_qualified`. Certificats/configurations doivent toujours être sauvegardés.

Le critère P0 reste ouvert jusqu’à fusion du correctif, réussite des builds alpha,
archivage durable et contrôle HTTP du nouveau catalogue et de chaque segment
publié par `verify-online`. Les tests synthétiques de transport HTTP ne remplacent
pas cette vérification publique réelle. Une indisponibilité des dépendances SDK,
APT ou des services GitHub bloque la livraison sans fallback sur un firmware
nouvellement non qualifié.

État consigné le 16 novembre 2026 : ce critère a été atteint pour la source
`52aea50a098a1839b6028a247229d273a92ee302`, après correction de la liste/glob et
récupération explicitement autorisée du tag. La Release durable, le déploiement
Pages, les deux manifestes et les huit nouveaux segments ont été vérifiés ; les
anciennes images sont conservées. Voir le
[bilan de reprise](../../.agent/shared/checkpoints/retained/UG-ESP32-PUBLICATION-RECOVERY.md).
Cette observation antérieure n'est pas une nouvelle vérification du site public.
Le refus 403 initial n'est pas résolu de manière générale pour les futurs tags
historiques : [suivi de l'autorité](../../.agent/shared/checkpoints/active/UG-ESP32-PUBLICATION-AUTHORITY.md).
Aucun firmware plus récent de la branche de travail n'est publié par ce constat.

Validation P0 locale : 262 tests générateur/politique et 10 tests JavaScript du
portail ; autorité réelle des sept validations main vérifiée pour le SHA autorisé.
Les essais HTTP bout en bout utilisent des réponses synthétiques comparées octet
par octet, pas un nouveau firmware déjà publié.


## Disponibilité de la liste et canaux publiés

La liste des versions provient de `catalog.json`, qui décrit les firmwares déjà
publiés. Alpha, bêta et stable sont tous affichés : le canal ne constitue pas un
filtre de visibilité. Une nouvelle autorisation de compilation n’est pas exigée
pour afficher un firmware déjà publié. Les contrôles de provenance, d’empreintes,
de partitions et de carte restent exécutés avant le flash.

`publications.json` enrichit les libellés avec le canal, le tag et la date. Son
absence, une erreur HTTP (dont 503) ou un JSON invalide ne masque jamais le
catalogue valide. Le registre indisponible ne masque pas non plus la liste ; la
vérification nécessaire au flash peut alors demander de réessayer. Sans date,
le libellé indique « date indisponible » plutôt que d’inventer une date. La
restauration durable accepte aussi les Releases stables, vérifie le tag source,
et préserve les octets des firmwares. Les anciennes métadonnées restent lisibles.

Le diagnostic du 16 novembre a confirmé deux défauts : une réponse 503 sur les
métadonnées optionnelles masquait la liste ; le nom `C/C++ Format`, utilisé comme
filtre glob sans échappement, empêchait le démarrage du publisher. Le filtre est
maintenant `C/C\+\+ Format`, sans changement du nom du workflow/check.

Les trois profils supportés (`s3-gateway`, `c3-rover-usb`, `c3-rover-uart`) sont
compilés et reproduits automatiquement par la CI native sur PR et push dev/main,
quelle que soit la future désignation alpha/bêta/stable. C3 Relay reste différé.
Compilation de validation et autorisation de publication restent distinctes.

Validation de la correction : 265 tests de politique/générateur, 12 tests JS du
portail, 6 tests JS de configuration portable. Chromium desktop/mobile sur les
véritables binaires publiés a vérifié six scénarios : métadonnées 503, 404 et JSON
invalide, liste visible et quatre segments effectivement vérifiés à chaque fois.
Ces essais utilisent un site local restauré depuis la Release durable ; ils ne
prétendent pas que le correctif est déjà déployé sur Pages avant sa fusion.
