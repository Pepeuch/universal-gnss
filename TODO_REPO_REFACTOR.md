# Universal GNSS — TODO refactor architecture / repository policy

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

Branche de développement complète.

Elle contient absolument tout :

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

Tous les développements passent d’abord par `dev`.

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

Configurer les branches générées comme branches protégées.

Objectifs :

- interdire les push manuels ;
- interdire les modifications directes ;
- autoriser uniquement le workflow de publication ;
- éviter les divergences entre backends ;
- empêcher qu’une correction reste uniquement dans une branche spécialisée.

---

# 5. Refactor de l’arborescence

Faire progressivement évoluer le dépôt vers une séparation claire entre logique commune et adaptations plateforme.

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

Le nom exact pourra évoluer.

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

- migration Lyrical ;
- CMake minimum 3.22 ;
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

Préparer UG à fonctionner sans ROS2.

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

Rôles possibles :

```text
BASE
ROVER
DUAL_DEBUG
```

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

Étudier un numéro de version UG commun :

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

# 37. Première étape concrète

Avant de déplacer beaucoup de code :

1. documenter cette nouvelle politique ;
2. créer `dev` si nécessaire ;
3. maintenir `main` full repo ;
4. créer `tools/branches/` ;
5. définir un premier `manifest.yaml` ;
6. tester la génération sur une branche non critique ;
7. ajouter les protections GitHub ;
8. seulement ensuite commencer le gros rangement `core/adapters/packaging`.

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