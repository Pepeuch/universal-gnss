# Universal GNSS — TODO refactor architecture / repository policy

## Statut, périmètre et priorités

Base du lot 0 accepté et de la synchronisation `dev`/`main` :
`ba7f220da30ee0b216f199ab24947db0678b3d7b`.
La PR 1 formalise la politique et le plan ; elle ne déplace aucun composant et
n'implémente ni générateur, ni workflow, ni protection GitHub.
Les impératifs des sections suivantes décrivent des travaux à réaliser, sauf
les règles de contribution applicables dès maintenant.

Trois axes sont suivis séparément :

| Axe | Objet | Condition d'avancement |
| --- | --- | --- |
| Refactor structurel | Politique Git, génération et rangement progressif | Lots revus et contrôles structurels applicables réussis |
| Évolutions fonctionnelles UG | Capacités GNSS et adaptations plateforme | Contrats et tests fonctionnels propres à chaque évolution |
| Publication | Tags, images, distributions et GitHub Releases | Décision explicite et qualification des artefacts concernés |

Le mainteneur considère v0.7.4 fonctionnellement stable et validée. L'existence
d'un tag ou d'une Release est un fait de publication distinct, documenté dans la
[politique de versionnement](docs/releases/versioning.md). Le refactor ne
déclenche automatiquement aucune nouvelle version UG et ne change ni les
preuves historiques ni les compteurs de qualification.

L'intégration future ESP32 **BASE / RELAY / ROVER**, avec **ESP-NOW LR**, est une
priorité fonctionnelle à étudier et qualifier sans lui assigner ici une version.
Elle réutilisera les sémantiques UG communes ; aucun rôle ni transport embarqué
n'est implémenté par ce plan. Les travaux Linux standalone, BlueOS et ROS2 Humble
restent prévus. Leurs prérequis techniques restent applicables, indépendamment
de cette priorité et de l'ordre des lots structurels.

[CONTRIBUTING.md](CONTRIBUTING.md) définit les règles de contribution ;
[TODO.md](TODO.md) et [ROADMAP.md](ROADMAP.md) conservent le suivi fonctionnel et
les critères de validation. Ce document porte le chantier structurel séparé.

## Avancement structurel — lot 2

La PR 1 est fusionnée dans `dev` au commit
`5a6f285f9864d924fdfc032e814fdbd1d7bf5eb1`. Le lot 2 fournit le
[générateur local](tools/branches/README.md), son manifeste de composants,
la fermeture transitive et le vérificateur pour ROS2 Kilted/Lyrical, avec
MAVROS et packaging Docker en options. Les chemins et contenus source restent
inchangés ; les sorties sont identifiées par leur commit et leur manifeste.
Le générateur n'effectue ni création de branche ni publication. Ce lot ne
termine pas la PR 3, ne change aucune protection/CI et ne qualifie pas les
cibles ESP32, Humble, Linux standalone ou BlueOS.
Les validations du lot et leurs limites sont décrites avec l'outil ; aucun
compteur fonctionnel UGA/projet/release n'est incrémenté par ce jalon structurel.

## Avancement structurel — lot 3

La PR 2 est fusionnée dans `dev` au commit
`eff7263629a5ec91e87879c7b2d8a058e80191cf`. Le lot 3 implémente localement la
CI de validation et ses tests, avec les deux pilotes et un contrôle final
obligatoire ; son exécution sur GitHub reste à confirmer après publication de
la PR. Voir les [consignes et le bilan local](tools/branches/CI.md).
La PR 3 reste partielle pour la publication : le rapport de préparation est
sans droit d'écriture et refuse toute demande de publication. Les protections,
l'identité autorisée et la transaction cohérente entre distributions nécessitent
une opération ultérieure explicitement approuvée. Aucune branche spécialisée,
aucun tag et aucune Release ne sont publiés. Ce jalon structurel ne change aucun
compteur fonctionnel ou historique de qualification.

## Avancement structurel — lot 4

Le seul composant déplacé est `gnss_protocols/`, désormais situé dans
`protocols/gnss_protocols/`. Les 58 fichiers du composant gardent leurs contenus
et leurs noms publics ; les références de build, du manifeste, du formateur et
de la documentation courante sont adaptées. Voir le
[bilan du déplacement](tools/branches/PROTOCOLS_MOVE.md).
Les anciens chemins présents dans les audits et preuves figés restent liés à
leurs bases historiques. Aucun autre composant, comportement, version ou
workflow de publication ne change ; les compteurs fonctionnels restent inchangés.

## Avancement structurel — lot 5, premier déplacement

L'audit des cinq composants et le [bilan du déplacement du modèle commun](tools/branches/CORE_MOVE.md)
sélectionnent uniquement `gnss_core/` vers `core/gnss_core/`. Les dix fichiers
sont conservés octet pour octet ; les références CMake, deux sources de liens
CI, le formateur, le manifeste et ses tests sont adaptés sans changement public.
`gnss_driver`, `gnss_transport`, `gnss_ntrip` et `gnss_runtime` restent en place.
L'ordre proposé des PR suivantes est driver, NTRIP, transport puis runtime,
avec réaudit avant chaque étape. La dette de formatage runtime préexistante
est seulement documentée ; elle n'est pas corrigée dans ce lot. Aucun compteur
fonctionnel, version, comportement GNSS ou qualification matérielle ne change.

## Avancement structurel — lot 5B, driver

Le driver est déplacé seul de `gnss_driver/` vers `core/gnss_driver/`, en
conservant ses 73 fichiers, contenus, modes et contrats publics. Le
[bilan du déplacement du driver](tools/branches/DRIVER_MOVE.md) décrit l'audit
depuis le merge de la PR #16, les références actives adaptées et les validations.
Les dépendances vers protocoles et transport sont conservées. Transport, NTRIP
et runtime restent en place ; la dette de formatage runtime et les preuves
historiques ne sont pas modifiées. Les prochaines étapes recommandées restent
NTRIP, transport puis runtime, avec réaudit avant chaque déplacement. Aucun
compteur fonctionnel, version ou jalon de qualification matérielle ne change.

## Avancement structurel — lot 5C, NTRIP

Le seul déplacement du lot 5C est `gnss_ntrip/` vers `core/gnss_ntrip/`.
Les 32 fichiers, leurs contenus et modes sont conservés, ainsi que les contrats
NTRIP/GGA/RTCM/TLS. Les helpers et fixtures TLS restent dans `gnss_transport`.
Le [bilan NTRIP](tools/branches/NTRIP_MOVE.md) détaille les références adaptées,
la conservation du graphe et les contrôles supplémentaires sur les fixtures
TLS calculées. Le générateur et les workflows ne sont pas modifiés.
Transport puis runtime restent les prochains déplacements à réauditer ; aucun
autre composant, compteur fonctionnel, version ou historique de validation
ne change. La dette de formatage runtime reste hors périmètre.

## Avancement structurel — lot 5D, transport

Le lot 5D déplace uniquement `gnss_transport/` vers `core/gnss_transport/`.
Les 30 fichiers, dont les deux helpers TLS et les huit fixtures, gardent leurs
contenus et modes. Les CMake consommateurs NTRIP/runtime et les destinations
TLS du manifeste sont adaptés au nouveau chemin, sans changement réseau/TLS.
Le [bilan transport](tools/branches/TRANSPORT_MOVE.md) recense les références,
les contrats conservés et les validations. Aucun algorithme du générateur ni
workflow n'est modifié. Runtime reste à la racine et fera l'objet d'un audit
séparé ; sa dette de formatage reste intacte. Versions, compteurs fonctionnels
et preuves historiques ne changent pas.

## Avancement structurel — lot 5E, runtime

Le seul déplacement du lot 5E est `gnss_runtime/` vers `core/gnss_runtime/`.
Ses huit fichiers restent identiques, CMake compris. Le runtime natif reste
exclu des distributions ROS2 : les 13 composants et les huit sélections ne
changent pas. Les interdictions et le contrôle d'isolation couvrent les deux
emplacements, ancien et nouveau, sans sélection globale de `core/`.
Le [bilan runtime](tools/branches/RUNTIME_MOVE.md) détaille les dépendances,
les exclusions et les validations. La dette de formatage préexistante demeure :
ni les fichiers runtime ni le périmètre du formateur ne sont modifiés.
Les cinq composants étudiés au lot 5 sont désormais regroupés sous `core/` ;
ce jalon structurel ne change aucune version, qualification ou mesure de
progression fonctionnelle. Les preuves historiques restent intactes.

## Objectif

Faire évoluer Universal GNSS d’un driver principalement ROS2 vers une plateforme GNSS commune pouvant alimenter plusieurs environnements sans dupliquer la logique métier :

- ROS2
- ROS2 Humble
- Linux standalone
- MAVROS / ArduPilot
- BlueOS
- ESP32
- GUI / WebUI
- futurs backends éventuels

Principe fondamental :

> Une fonctionnalité ou un correctif générique doit être implémenté une seule fois dans le cœur UG puis bénéficier à toutes les distributions concernées.

---

# 1. Politique Git

## Branches principales

### `dev`

Branche de développement complète et cible de toutes les PR de contribution.
Créer les branches de travail depuis un `dev` à jour.

Elle conserve tous les composants existants et accueillera les futurs composants
une fois implémentés ; cette liste de périmètre ne signifie pas qu’ils existent déjà :

- core UG
- drivers/protocoles
- tests
- outils
- ROS2
- MAVROS
- Linux
- ESP32
- BlueOS
- GUI
- documentation
- packaging
- scripts de génération des branches spécialisées

Tous les développements et corrections passent d’abord par `dev`, puis par les
tests et la revue applicables avant intégration dans `main`. Aucune PR de
contribution ne cible `main` ni une branche spécialisée.

### `main`

Branche stable complète.

- même architecture générale que `dev`
- uniquement du code validé
- source officielle de toutes les distributions publiées
- aucune branche spécialisée ne doit être générée directement depuis `dev`

Flux :

```text
développement
    ↓
   dev
    ↓
tests / validation / renforcement
    ↓
   main
```

---

# 2. Branches spécialisées générées

Créer des branches contenant uniquement ce qui est nécessaire à leur cible.

Cibles envisagées :

```text
ros2
humble
linux
esp32
blueos
```

MAVROS devra être réévalué selon son architecture finale :

- soit composant du backend ROS2
- soit distribution dédiée si cela devient réellement nécessaire

Le GUI pourra également être :

- intégré aux distributions qui l’utilisent ;
- ou avoir sa propre distribution si son évolution le justifie.

---

# 3. Règle fondamentale des branches spécialisées

Les branches spécialisées ne doivent **jamais être développées manuellement**.

Flux autorisé :

```text
dev
 ↓
main
 ↓
workflow
 ↓
branches spécialisées générées
```

Flux interdit :

```text
esp32 → modification manuelle
blueos → modification manuelle
ros2 → modification manuelle
```

Si un bug apparaît sur ESP32 :

```text
bug ESP32
   ↓
analyse
   ↓
correction dans dev
   ↓
tests
   ↓
main
   ↓
nouvelle génération esp32
```

Même principe pour toutes les plateformes.

---

# 4. Protection GitHub

À mettre en œuvre dans un lot ultérieur : configurer les branches générées
comme branches protégées avant leur publication. Cette PR ne change aucune
protection et ne prétend pas que ces règles sont déjà imposées par GitHub.

Objectifs :

- interdire les push manuels ;
- interdire les modifications directes ;
- autoriser uniquement le workflow de publication ;
- éviter les divergences entre backends ;
- empêcher qu’une correction reste uniquement dans une branche spécialisée.

---

# 5. Refactor de l’arborescence

Faire progressivement évoluer le dépôt vers une séparation claire entre logique commune et adaptations plateforme.

## Structure actuelle et composants existants

La table décrit l’arbre courant après le lot 5E ; les protocoles et les cinq
composants communs du lot 5 sont regroupés, les autres composants restent à la racine.
La base du lot 0 reste celle indiquée en tête :

| Chemin actuel | État / rôle | Destination indicative, à justifier par PR |
| --- | --- | --- |
| `core/gnss_core/` | Modèle, agrégation et diagnostics communs ; headers | En place depuis le premier déplacement du lot 5 ; noms publics inchangés |
| `core/gnss_driver/` | Sessions, détection, profils et configuration | En place depuis le lot 5B ; noms publics inchangés |
| `protocols/gnss_protocols/` | NMEA, UBX, Unicore et RTCM | En place depuis le lot 4 ; noms publics inchangés |
| `core/gnss_transport/` | Abstractions et implémentations mémoire/POSIX/réseau | En place depuis le lot 5D ; noms publics inchangés |
| `core/gnss_ntrip/` | Client/caster NTRIP et GGA | En place depuis le lot 5C ; noms publics inchangés |
| `core/gnss_runtime/` | Superviseur commun et intégration série Linux | En place depuis le lot 5E ; exclu des pilotes ROS2, noms publics inchangés |
| `gnss_tools/` | Bibliothèque réutilisée par ROS2 et CLI | `tools/gnss_tools/` |
| `gnss_ros2/` | Package runtime, launch et configuration ROS2 | `adapters/ros2/gnss_ros2/` |
| `universal_gnss_msgs/` | Package public ROSIDL | Racine initialement ; déplacement séparé éventuel |
| `gnss_mavros/` | Adaptateur et plugin MAVROS optionnel | `adapters/mavros/gnss_mavros/` |
| `Dockerfile`, `docker/` | Packaging ROS2 de production | `packaging/docker/` avec points d'entrée compatibles |
| `blueos/` | Étude et modèles Bazaar/permissions ; pas de runtime BlueOS | Documentation et `packaging/blueos/` à terme |
| Tests des composants, `testdata/` | Tests colocalisés et fixtures partagées | Conserver initialement ; centralisation sélective justifiée |
| `scripts/` | Maintenance, support et validation | Migration sélective vers `tools/`, anciens points d'entrée préservés |
| `docs/`, `examples/`, `assets/` | Références, exemples et ressources | Conserver tant qu'aucun déplacement n'est justifié |
| `universal-gnss-validation/` | Résultats et copies historiques de scripts | Conserver intacts |

Le core est déjà indépendant de ROS2. Transport et runtime comportent des
parties conditionnelles Linux : les déplacer ne justifie pas de les redécouper
fonctionnellement dans la même PR. `gnss_tools` n'est pas un simple ensemble
facultatif de CLI : le runtime ROS2 consomme aussi sa bibliothèque.

GUI/WebUI, API HTTP commune, intégration UG ESP32 et packaging Debian sont
futurs. Le superviseur natif Linux existe, mais le service système, le package
Debian et l'API restent à compléter. ROS2 Kilted/Lyrical et MAVROS existent ;
la distribution Humble et les branches spécialisées restent à construire.
Les tâches BlueOS existantes ne constituent pas un backend opérationnel.

## Structure cible indicative

L'arbre ci-dessous illustre des responsabilités à terme, pas des dossiers déjà
présents ni une obligation de déplacer chaque dossier. Garder d'abord les
composants entiers évite de mêler rangement et modification du moteur. La
répartition détaillée des parsers entre `core/` et `protocols/` sera précisée
avant toute extraction ; aucune seconde implémentation ne doit être créée.

Architecture cible indicative :

```text
universal-gnss/
│
├── core/
│   ├── receiver/
│   ├── parsers/
│   ├── profiles/
│   ├── diagnostics/
│   ├── rtcm/
│   ├── validation/
│   └── ...
│
├── protocols/
│   ├── unicore/
│   ├── ubx/
│   ├── nmea/
│   └── ...
│
├── adapters/
│   ├── ros2/
│   ├── mavros/
│   ├── linux/
│   ├── blueos/
│   └── esp32/
│
├── gui/
│   ├── frontend/
│   └── backend/
│
├── packaging/
│   ├── ros/
│   ├── debian/
│   ├── docker/
│   ├── blueos/
│   └── esp32/
│
├── tests/
│   ├── core/
│   ├── protocol/
│   ├── integration/
│   └── backend/
│
├── docs/
│
└── tools/
```

Cette migration doit être progressive afin d’éviter un refactor « big bang ».
Les fichiers racine d'entrée, `.github/`, `.devcontainer/`, `.agent/`, la licence
et les règles agent restent en place tant qu'aucune nécessité n'est démontrée.

## Contrats publics et références à préserver

Conserver les noms des packages `universal_gnss_ros2`, `universal_gnss_msgs` et
`universal_gnss_mavros`, les cibles/bibliothèques `universal_gnss_*`, les noms
d'exécutables et les préfixes d'include `universal_gnss/...`,
`universal_gnss_driver/...`, `universal_gnss_protocols/...` et leurs équivalents.
Conserver les interfaces ROSIDL `GnssStatus`, `RtcmFrame`, `GetReceiverSnapshot`,
les alias C++ historiques, les exports ament et l'identité du plugin MAVROS.

Les launch, paramètres, topics/services, chemins installés sous `bin`, `lib`,
`lib/universal_gnss_ros2` et `share/universal_gnss_ros2`, ainsi que les chemins
runtime Docker et variables opérateur, sont des contrats de consommation.
Ne pas les renommer avec les dossiers source. Les numéros de version restent
inchangés pendant un simple déplacement. Tout changement public intentionnel
requiert une justification explicite, documentation et autorisation distinctes.

Les chemins source internes peuvent changer dans une PR atomique qui adapte
toutes leurs références : CMake ROS2 reconstruit des bibliothèques depuis les
dossiers frères ; Docker et la CI utilisent ces chemins ; NTRIP/runtime
partagent les fixtures TLS du transport et les outils utilisent `testdata/`.
Préserver les dépendances transitives dans chaque manifest de distribution.
Vérifier aussi les imports Python et la sélection de fichiers de formatage,
pour qu'un déplacement ne réduise pas silencieusement les contrôles.

Aucun changement de protocole, API, précision, horloge, fraîcheur, séquence,
provenance ou invalidation ne fait partie du rangement. Conserver les preuves
historiques, leurs SHA, chemins documentés, hashes et scripts associés : ne pas
réécrire les campagnes pour refléter la nouvelle arborescence. Le contrôleur rc6
vérifie notamment le hash du script de rendu historique.

---

# 6. Séparer le core UG des plateformes

Objectif à terme :

```text
octets GNSS
    ↓
UG Core
    ↓
données structurées
    ↓
adaptateur plateforme
```

Le core ne doit idéalement pas dépendre directement de :

- ROS2 / rclcpp ;
- MAVROS ;
- BlueOS ;
- ESP-IDF ;
- GUI ;
- API Web.

Le core doit concentrer :

- détection des récepteurs ;
- parsers ;
- profils ;
- configuration ;
- décodage NMEA ;
- Unicore ;
- UBX ;
- RTCM ;
- diagnostics ;
- validation ;
- logique GNSS commune.

---

# 7. Politique de développement UG

Conserver et formaliser la méthode actuelle :

```text
implémenter
   ↓
stabiliser
   ↓
tester
   ↓
valider
   ↓
renforcer
   ↓
publier
```

Une fonctionnalité générique ne doit pas être considérée comme terminée uniquement parce qu’un backend fonctionne.

Elle doit être validée au niveau du core et des backends concernés.

---

# 8. Générateur de branches

Créer :

```text
tools/
└── branches/
    ├── manifest.yaml
    ├── make_backend.sh
    ├── verify_backend.sh
    └── publish_backend.sh
```

`manifest.yaml`, `make_backend.sh` et `verify_backend.sh` sont désormais
implémentés pour une génération locale sans réseau ; voir la
[documentation du lot 2](tools/branches/README.md). Le `publish_backend.sh`
ci-dessus reste une proposition pour un lot ultérieur, pas un outil existant.
Le schéma des manifests suivants reste conceptuel : le manifeste réel sépare
composants, dépendances et profils plutôt que de copier une simple whitelist.
Le nom exact des futurs outils de publication pourra évoluer.

---

# 9. Manifest des distributions

Le contenu de chaque branche doit être défini explicitement.

Préférer une **whitelist** plutôt qu’une blacklist.

Exemple :

```yaml
esp32:
  include:
    - core/
    - protocols/
    - adapters/esp32/
    - tests/core/
    - tests/esp32/
    - docs/common/
    - docs/esp32/
    - packaging/esp32/

ros2:
  include:
    - core/
    - protocols/
    - adapters/ros2/
    - tests/core/
    - tests/ros2/
    - docs/common/
    - docs/ros2/
    - packaging/ros/
```

Avantage :

un nouveau dossier important oublié dans le manifest peut être détecté au lieu d’être silencieusement publié ou supprimé.

---

# 10. Vérification automatique du manifest

`verify_backend.sh` doit notamment pouvoir vérifier :

- fichiers indispensables présents ;
- fichiers interdits absents ;
- manifest complet ;
- dépendances du backend satisfaites ;
- absence de fichiers non prévus ;
- cohérence du packaging ;
- cohérence de la documentation.

Exemple d’erreur recherchée :

```text
ERROR:
core/receivers/new_receiver/
is not covered by any distribution manifest.
```

---

# 11. Métadonnées des branches générées

Ajouter dans chaque branche générée un fichier du type :

```text
.generated-from-main
```

Contenu possible :

```text
backend=esp32
source_commit=0123456789abcdef
ug_version=1.4.0
generator_version=1
generated=true
```

Objectifs :

- connaître exactement la source d’une distribution ;
- faciliter le debug ;
- rendre les builds reproductibles ;
- identifier immédiatement une branche générée.

---

# 12. `.gitignore` spécifique par distribution

Chaque branche générée pourra avoir son `.gitignore`.

Exemple ESP32 :

```text
.pio/
build/
sdkconfig
sdkconfig.old
```

Exemple ROS2 :

```text
build/
install/
log/
.colcon/
```

Ces fichiers devront eux aussi être générés depuis les sources présentes dans `main`.

---

# 13. CI des branches générées

Avant publication d’une branche :

```text
main
 ↓
génération backend
 ↓
validation manifest
 ↓
build
 ↓
tests spécifiques
 ↓
publication
```

Une branche ne doit pas être mise à jour si son build ou ses tests échouent.

---

# 14. ROS2

Le backend ROS2 moderne reste la référence principale dans un premier temps.

Objectifs :

- préserver les validations Kilted/Lyrical existantes ;
- préserver le minimum CMake 3.22 existant ;
- conserver une architecture compatible avec le core indépendant ;
- supprimer progressivement les dépendances ROS2 du code générique.

---

# 15. ROS2 Humble

Créer une distribution dédiée Humble.

But :

- supporter l’ancien LTS utile ;
- éviter de polluer le code moderne avec une multitude de conditions de compatibilité.

Le code générique doit rester aussi proche que possible du backend moderne.

Les adaptations Humble doivent être limitées principalement à :

- CMake ;
- `package.xml` ;
- launch ;
- API ROS2 différentes ;
- QoS/API spécifiques ;
- dépendances disponibles.

---

# 16. MAVROS

L’intégration MAVROS récemment ajoutée doit être prise en compte dans le refactor.

À déterminer :

- ce qui appartient réellement au core UG ;
- ce qui appartient à ROS2 ;
- ce qui est spécifique à MAVROS ;
- si MAVROS doit rester un adaptateur ROS2 ou devenir une distribution dédiée.

Éviter de recréer dans MAVROS une seconde implémentation de la logique GNSS.

---

# 17. BlueOS

Préparer l’arrivée de BlueOS dès la conception.

Prévoir :

- adaptateur BlueOS ;
- packaging/container ;
- API adaptée ;
- éventuelle intégration GUI ;
- mécanisme d’installation/mise à jour ;
- tests spécifiques.

BlueOS devra consommer le même core que ROS2/Linux autant que possible.

---

# 18. Linux standalone

Le superviseur natif existe déjà sans ROS2 ; compléter sa distribution et ses
surfaces de déploiement sans dupliquer le moteur.

Objectif :

```text
GNSS
 ↓
Universal GNSS
 ↓
Linux service / daemon
 ↓
API / sockets / Web / CLI
```

Prévoir à terme :

- daemon ;
- service systemd ;
- configuration ;
- logs ;
- diagnostics ;
- package `.deb` ;
- API locale ;
- possibilité d’utiliser le GUI sans ROS2.

---

# 19. Packaging Linux

Préparer :

```text
apt install universal-gnss
```

ou équivalent.

Créer progressivement :

```text
packaging/debian/
```

Avec :

- service systemd ;
- fichiers de configuration ;
- règles udev ;
- permissions série ;
- installation GUI éventuelle ;
- mise à jour propre.

---

# 20. Packaging ROS

Préparer à terme une installation standard :

```text
apt install ros-<distro>-universal-gnss
```

ou publication via les mécanismes ROS appropriés.

Le packaging ROS ne doit pas conditionner l’architecture interne du core.

---

# 21. ESP32

Priorité fonctionnelle future : rôles **BASE / RELAY / ROVER** et transport
**ESP-NOW LR**, sans association automatique à une version UG ni à un lot de
rangement. Définir les contrats RTCM, capacités par rôle, dépendances plateforme
et tests avant implémentation. Le relais ne doit pas créer une seconde logique
GNSS. Cette priorité préserve les travaux Linux, BlueOS et Humble.

Ne pas considérer l’actuel `esp32-rtk-gateway` comme architecture de référence du futur UG ESP32.

Le considérer comme :

```text
prototype / donor repository
```

Faire un audit avant migration.

À récupérer potentiellement :

- NTRIP ;
- Wi-Fi ;
- Ethernet / W5500 ;
- LoRa ;
- RTCM transport ;
- WebUI ;
- gestion mémoire ;
- reconnexion ;
- fake RTCM ;
- self-tests ;
- abstractions board utiles.

---

# 22. ESP32 — ne pas dupliquer la couche GNSS

Ne pas reprendre automatiquement les parsers/profils GNSS du gateway actuel.

Le futur backend ESP32 doit être aligné sur :

- comportement UG ;
- parsers UG ;
- profils UG ;
- diagnostics UG ;
- politiques RTCM UG ;
- jeux de tests UG.

Objectif :

```text
même GNSS
même comportement
plateforme différente
```

---

# 23. Audit de `esp32-rtk-gateway`

Avant de commencer la nouvelle intégration ESP32 :

classer chaque composant :

```text
KEEP
ADAPT
REWRITE
DROP
```

Catégories à examiner :

- NTRIP ;
- Ethernet ;
- Wi-Fi ;
- LoRa ;
- RTCM ;
- GNSS ;
- profils ;
- diagnostics ;
- WebUI ;
- storage ;
- configuration ;
- watchdog ;
- board support ;
- OTA ;
- tests.

---

# 24. GUI

Le GUI doit progressivement devenir indépendant de ROS2.

Objectif :

```text
UG Core
 ↓
API commune
 ↓
GUI
```

Le GUI doit pouvoir servir plusieurs environnements :

- ROS2 ;
- Linux standalone ;
- éventuellement BlueOS ;
- éventuellement ESP32/WebUI selon les capacités.

Éviter de dupliquer toute l’interface pour chaque backend.

---

# 25. API commune

Étudier une API interne stable entre le core et :

- GUI ;
- ROS2 ;
- Linux ;
- BlueOS.

Cette API pourra transporter :

- état GNSS ;
- fix ;
- satellites ;
- RF ;
- jamming ;
- RTCM ;
- configuration ;
- profils ;
- diagnostics ;
- commandes.

---

# 26. Portail Web Universal GNSS

Créer une petite page Web servant de point d’entrée officiel pour les utilisateurs.

Le portail doit permettre de choisir facilement :

```text
ROS2
Linux
ESP32
BlueOS
MAVROS
```

Puis afficher uniquement les instructions adaptées.

---

# 27. Documentation automatique par cible

Pour chaque cible, afficher :

- version stable ;
- prérequis ;
- méthode d’installation ;
- commandes ;
- branche associée ;
- documentation ;
- changelog ;
- dépannage ;
- téléchargement éventuel.

L’utilisateur ne doit pas avoir besoin de comprendre toute l’organisation interne Git.

---

# 28. Web flasher ESP32

Intégrer à terme un flash ESP32 directement depuis le navigateur.

Fonctions envisagées :

- sélection de la carte ;
- sélection du rôle ;
- choix de la version ;
- flash firmware ;
- affichage du journal ;
- récupération après échec ;
- éventuellement configuration initiale.

Rôles fonctionnels à préparer :

```text
BASE
RELAY
ROVER
```

`DUAL_DEBUG` reste un scénario de diagnostic à étudier, distinct des rôles
fonctionnels. ESP-NOW LR et le web flasher nécessitent leur propre qualification.

---

# 29. Manifest firmware ESP32

La CI doit générer les binaires et un manifest exploitable par le Web flasher.

Exemple conceptuel :

```json
{
  "version": "1.4.0",
  "target": "esp32-s3",
  "role": "base",
  "firmware": "..."
}
```

Le site ne doit pas avoir à coder les numéros de version en dur.

---

# 30. GitHub Pages / site UG

Étudier GitHub Pages comme première solution d’hébergement.

Le site pourra être mis à jour automatiquement lors d’une release.

Flux :

```text
release UG
   ↓
build distributions
   ↓
build firmwares
   ↓
build packages
   ↓
génération métadonnées
   ↓
mise à jour portail Web
```

---

# 31. Versioning global

Appliquer la [politique de versionnement](docs/releases/versioning.md) : une
version UG décrit le comportement et la compatibilité du moteur. Le rangement,
la politique Git et la génération n'imposent pas de nouvelle version fonctionnelle.
Qualifier séparément le moteur, chaque distribution et les artefacts publiés.

Les numéros ci-dessous sont des exemples conceptuels historiques, comme ceux
des manifests précédents, pas des versions décidées pour ce chantier. Exemple
de numéro UG commun :

```text
UG v1.4.0
```

pouvant correspondre aux distributions :

```text
UG ROS2 v1.4.0
UG Linux v1.4.0
UG ESP32 v1.4.0
UG BlueOS v1.4.0
```

Même si certaines distributions n’exposent pas exactement toutes les capacités.

---

# 32. Matrice des capacités

Créer une description machine-readable des fonctionnalités disponibles par backend.

Exemple :

```yaml
features:
  um982: true
  ublox: true
  ntrip_client: true
  ntrip_server: false
  lora: true
  gui: true
```

Elle pourra servir :

- aux builds ;
- au GUI ;
- au site ;
- à la documentation ;
- aux tests.

---

# 33. Tests communs

Les parsers doivent autant que possible utiliser les mêmes jeux de données de test sur toutes les plateformes.

Créer/conserver :

- captures réelles ;
- messages valides ;
- CRC invalides ;
- trames tronquées ;
- bruit série ;
- resynchronisation ;
- valeurs limites ;
- messages inconnus.

---

# 34. Validation cross-backend

Pour une entrée GNSS identique, les différents backends doivent autant que possible produire les mêmes données fonctionnelles.

Exemple :

```text
BESTNAVB donné
 ↓
ROS2 parser
ESP32 parser
Linux parser
 ↓
résultat fonctionnel équivalent
```

---

# 35. Documentation architecture

Créer un document expliquant clairement :

```text
dev  = full repository development
main = full repository stable

branches spécialisées =
distributions générées
```

Ainsi que :

```text
core
adapter
distribution
package
```

---

# 36. Politique des contributions

Documenter que les PR doivent viser :

```text
dev
```

et jamais directement :

```text
main
esp32
ros2
humble
blueos
linux
```

Les corrections backend doivent également repartir par `dev`.

---

# 37. Étapes et dépendances des lots

Le lot 0 d'audit est accepté. `dev` a été synchronisé avec `main` en
fast-forward à la base commune indiquée en tête ; ne pas recréer la branche.
Les étapes suivantes sont des PR distinctes, chacune avec périmètre, risques,
tests, critères d'acceptation et retour arrière explicites :

| Étape | Périmètre | Dépendance / acceptation | Retour arrière |
| --- | --- | --- | --- |
| PR 1 | Politique Git, versions, contributions et architecture | Lot 0 et synchronisation ; cohérence des cinq documents, liens et diff documentaire contrôlés | Revert documentaire |
| PR 2 (lot 2 implémenté localement) | `tools/branches/`, whitelist, génération locale et métadonnées | PR 1 ; sorties déterministes, dépendances et fichiers non couverts contrôlés, sans publication | Revert ; aucune branche publiée |
| PR 3 | Vérification CI des distributions et publication contrôlée | PR 2 ; essais sans publication, build/tests bloquants ; protections et identité autorisée configurées séparément avant activation | Désactiver publication, revert du workflow ; conserver les sorties vérifiées |
| PR 4 | Déplacement de `gnss_protocols` entier | Socle précédent validé ; CMake/includes/tests/format/manifests et consommateurs adaptés ensemble | Revert atomique |
| PR 5 | Composants communs sous `core/` sans extraction fonctionnelle | PR 4 ; tests natifs et consommateurs ROS2, fixtures et installations préservés | Revert atomique |
| PR 6a / 6b | Déplacements ROS2 et MAVROS dans deux PR | PR 5 ; exports stables, builds Kilted/Lyrical, interfaces/launch et tests MAVROS ; messages à la racine initialement | Revert indépendant de chaque PR |
| PR 7 | Packaging Docker et modèles BlueOS | Chemins source stabilisés ; builds/smoke multi-architecture, contrats opérateur et points d'entrée compatibles | Revert ; images/digests antérieurs conservés |
| PR 8 | Outils et tests sélectionnés | PR 5 et validation des consommateurs ; imports, CLI, fixtures et périmètre de tests conservés | Revert par sous-ensemble |

Le numéro de PR indique un lot structurel, pas une version UG. La publication
réelle et les changements de protections restent des opérations explicitement
autorisées à part. Une PR ne se déclare pas terminée tant que ses contrôles
applicables échouent ou restent inconnus.

Ne pas déplacer massivement les tests : leur colocalisation est conservée au
début. Une extraction POSIX, un déplacement des messages, une API GUI commune
ou l'intégration ESP32 nécessitent chacun une étude et une PR ultérieures.
Les évolutions BlueOS, Linux et Humble suivent leurs propres dépendances et
critères fonctionnels ; la seule génération d'un arbre ne vaut pas support.

Avant chaque déplacement : figer la base et les contrats, adapter les références
dans la même PR, comparer les contenus et les installations, exécuter les tests
ciblés puis les consommateurs affectés, vérifier format/liens et `git diff --check`.
Maintenir les manifests à jour sans réduire le périmètre de CI. Aucun lot
structurel ne donne de crédit nouveau aux compteurs UGA, projet ou release en
l'absence de leurs preuves d'acceptation propres.

---

# 38. Principe à conserver

Ne pas sacrifier la facilité de développement interne pour rendre le dépôt visuellement minimal.

Le dépôt complet doit rester pratique pour développer UG.

La simplification doit être réalisée au niveau des **distributions générées**, pas en éclatant la source de vérité en plusieurs projets divergents.

Architecture générale visée :

```text
                         dev
                          │
                 développement complet
                          │
                          ↓
                        main
                   full repo stable
                          │
                  workflow de release
                          │
       ┌──────────────┬───┴────┬──────────────┐
       ↓              ↓        ↓              ↓
      ROS2          Linux     ESP32         BlueOS
       │
     Humble
```

Une correction générique :

```text
1 correction dans dev
        ↓
validation
        ↓
main
        ↓
toutes les distributions concernées en bénéficient
```

C’est la règle centrale du futur refactor Universal GNSS.
