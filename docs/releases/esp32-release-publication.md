# Publication ROS2 / ESP32 par Release RC

La **compilation de développement** reste indépendante de la publication : le
workflow `ESP32 native validation` compile S3 Gateway, C3 Rover USB et C3 Rover
UART sur **chaque push de toute branche ou tag**, même sans PR, ainsi que sur les
PR. Aucun registre d'approbation ni appartenance à `main` n'est requis pour ces
builds. Leurs échecs restent signalés ; ils n'autorisent pas une publication.

## Parcours courant

Publier une Release `vX.Y.Z-RCn` depuis un compte mainteneur `admin` ou `maintain`.
Le tag doit déjà exister, cibler exactement un commit présent dans `main` et suivre
le format canonique : nombres sans zéro initial, `RC` majuscule et numéro ≥ 1.

- `docker-release.yml` reste **strictement inchangé** : push du tag `v*`, images
  Kilted/Lyrical, plateformes AMD64/ARM64. Les deux chaînes utilisent le même SHA
  résolu depuis le tag ; elles ne partagent pas un numéro de build mutable.
- `esp32-alpha.yml` reçoit `release: published`, conserve la maintenance manuelle
  et réexamine les RC lorsque les CI `main` se terminent. Aucun nouveau workflow.
- La publication de la Release par un mainteneur constitue son autorisation.
  Conformément à la décision du mainteneur pendant ce lot, les rulesets de tags
  sont **reportés**, sans seconde approbation ni édition de registre par version.
  Les tags ne sont donc pas présentés comme protégés contre toute modification.
- Le contrôleur fige SHA, arbre, objet du tag et ID de Release. Il les revérifie
  avant l'archivage et avant le marqueur final. Un déplacement de tag ou un conflit
  arrête la chaîne ; le code ne crée, déplace ou remplace aucun tag RC.
- Une entrée explicitement révoquée ou un arbre différent dans le registre existant
  ne sont pas réapprouvés automatiquement. Le parcours alpha historique conserve
  son autorisation explicite et son mode manuel.

Les sept dernières exécutions `push/main` doivent réussir sur le **SHA source exact** :
`backlog-status.yml`, `cmake.yml`, `distributions.yml`, `docker.yml`, `format.yml`,
`ros2-lyrical.yml`, `esp32.yml`. Une réussite de PR ne suffit pas. Échec, annulation,
job ignoré ou validation absente/en cours empêchent la publication. Le contrôleur
issu de `main` doit lui aussi être vert ; son SHA est conservé entre les jobs.

Les builds existants `s3-gateway` et `c3-rover-usb` sont réutilisés : SDK épinglé,
qualification reproductible, sources exactes archivées, utilisateur non-root,
réseau de build coupé après provisionnement, aucun périphérique ni montage des
sources hôtes. Le C3 UART configurable conserve son workflow existant.
Les images Docker ne sont ni reconstruites ni republiées par la chaîne ESP32.

## Archivage et relance

La Release versionnée doit **déjà être publiée**. Le code ajoute des assets
`esp32-*` sans modifier le titre, la description, les autres assets ou le tag :

- `esp32-site.zip`, `esp32-firmware-source.tar.gz`, `esp32-catalog.json`,
  `esp32-approved-sources.json` ;
- pour chacun des deux profils : bundle ZIP, manifeste, provenance,
  qualification et quatre segments binaires nommés avec leurs offsets ;
- `esp32-SHA256SUMS`, puis **en dernier** `esp32-publication.json`.

Les 22 fichiers sont immuables du point de vue du publisher : tous les conflits
sont contrôlés avant écriture, seuls les fichiers absents sont ajoutés et tous
les octets sont retéléchargés/vérifiés avant le marqueur. Aucun `--clobber`, aucune
suppression, aucun retag, aucune recréation de Release ou édition de ses notes.

Après interruption, la chaîne reprend l'archive initiale, ancrée par le SHA256
fourni par GitHub, et revalide ses bundles, son origine et sa qualification. Elle
n'utilise pas des artefacts Actions expirables et ne reconstruit pas les firmwares.
Un upload GitHub resté incomplet/invérifiable échoue explicitement ; il n'est pas
supprimé automatiquement pour contourner le conflit.

GitHub ne permet pas une transaction atomique de plusieurs assets sur une Release
déjà publique : des fichiers peuvent être visibles pendant l'archivage. Sans
marqueur complet vérifié, ils ne sont jamais considérés comme une publication
ESP32 achevée et n'actualisent pas Pages. Le workflow Web seul ignore ces RC
incomplètes ; une RC marquée complète mais corrompue provoque un échec explicite.

Après archivage, le catalogue conserve les anciens firmwares, manifestes et
empreintes. Les ressources HTML/JS peuvent être actualisées indépendamment sans
créer ni autoriser de firmware. Le verrou global existant
`esp32-alpha-publication` sérialise les workflows firmware et Web. Une publication
complète peut être redéployée sans rebuild. La date RC reste celle de la Release,
avec SHA complet, canal `RC` et statut `validated_authorized_rc`.
Les binaires et leurs manifestes qualifiés ne sont pas réécrits pour changer leur
étiquette ALPHA historique : le canal RC est une métadonnée de publication,
aucune nouvelle qualification matérielle n'est revendiquée.

## Réglage Pages et état distant constaté

Audit du 16 novembre 2026 :

- `main` : `e632686c9c2e8dd5d4dc5b8d15f641dc1a633824` ;
- `dev` : `e496116672a06e5687e79e866b5861259f1efc2e`, même arbre que `main` ;
- Release existante `v0.7.5-RC1`, ID `447677079`, publiée, non draft,
  non verrouillée par l'option GitHub d'immutabilité des Releases, initialement
  sans assets ; tag au SHA `e632686c9c2e8dd5d4dc5b8d15f641dc1a633824` ;
- sept validations `push/main` réussies pour ce SHA ; publication Docker
  [43966826035](https://github.com/Pepeuch/universal-gnss/actions/runs/43966826035)
  également réussie ;
- aucun ruleset de tags observé. Aucun ruleset n'a été ajouté par ce lot ;
- l'environnement `github-pages` autorisait seulement la branche `main`.
  L'autorisation de déploiement des tags `v*-RC*` a été ajoutée et relue via API,
  en conservant `main`, sans reviewer supplémentaire. Ce réglage unique évite
  qu'une Release RC soit bloquée avant son archivage/déploiement.

Ces observations sont une photographie, pas une garantie de l'état futur GitHub.
Les protections renforcées pourront être étudiées séparément lorsque le mainteneur
le décidera. Il ne faut pas activer l'immutabilité GitHub de la Release avant
l'ajout des assets : la chaîne actuelle ajoute les firmwares après `published`.

## Rattrapage de v0.7.5-RC1 après intégration

L'événement passé n'est pas rejoué. Après intégration et promotion du correctif
sur `main`, attendre ses sept validations vertes puis utiliser l'interface GitHub :

**Actions → ESP32 alpha publication → Run workflow**, branche **main** :

```text
source_sha: e632686c9c2e8dd5d4dc5b8d15f641dc1a633824
release_tag: v0.7.5-RC1
```

Le tag reste au même SHA et les assets sont ajoutés à la Release existante.
Aucun script local, nouveau tag ou nouvelle Release n'est nécessaire. Les
prochaines Releases RC publiées depuis le workflow déployé se déclenchent seules.
Ce rattrapage n'a pas été lancé pendant la préparation de la PR.

## Validation de cette modification

Les tests de `tools/branches/tests/test_esp32_release.py` et
`test_esp32_alpha.py` couvrent autorité mainteneur, absence de registre obligatoire
RC, révocation explicite, format de tag, SHA/objet/ID modifiés, sept validations
source et contrôleur, déclenchement Release, rattrapage manuel, attente de CI,
permission read-only des builds et reprise fail-closed.

Le test de bout en bout utilise de **petits binaires IDF synthétiques**, mais les
vrais empaqueteurs, inspecteurs de bundles, staging, archive, sommes et vérificateur
public. Il contrôle les deux profils, les manifestes/offsets, les assets relus,
le catalogue durable RC et une relance sans écriture. Les accès GitHub et Pages
sont simulés : ce test n'est ni une compilation réelle ni une publication distante.
Les tests de conflit, interruption, corruption et refus d'autorité prouvent que
le marqueur/catalogue n'est pas validé après un échec.

Résultats locaux : **293/293 tests générateur/politique CI**, dont 24 nouveaux
cas Release, deux nouveaux cas tag/catalogue RC et deux cas déclenchement ESP32
sans filtre, réussis en 23,584 secondes.
Les **44 tests de maintenance** (34 + 10), les **94 liens locaux**, la syntaxe
Python/YAML/Bash, l'audit des checkpoints (19 entrées, zéro problème), le dashboard
et `git diff --check` réussissent. Aucun firmware n'est flashé,
aucune NVS/partition/clé/configuration n'est modifiée. Aucun build ROS2/ESP32 n'est
rejoué localement pour ce changement Python/YAML : les sources et commandes de
build sont inchangées. La qualification hébergée du parcours RC complet reste à
réaliser après déploiement sur `main`, sans confondre CI de PR et publication réelle.
