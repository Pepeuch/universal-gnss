# UG ESP32 — RTCM portable et architecture des profils

Branche de travail : `feat/esp32` (renommée sans recréer la branche).
Base UG : `069d868a0bc1ccb55baa48f4c19c4e8e14fa381d`.
Référence historique publiée : `7a1ec69f5e9d07522c0284ec2dbae380f72c7e85`
([branche](https://github.com/Pepeuch/esp32-rtk-gateway/tree/feat/espnow-rtcm-usb)).
Le firmware historique reste la référence matérielle (740/740 trames nominales).
Aucune endurance, réception RF ou opération de flash n'a été rejouée dans ce lot.

Le premier commit portable est `134e48b12acec960bba842cb06715847ec960a99`.
Le [lot d'intégration applicative](integration/README.md) construit maintenant les
applications historiques complètes S3 Gateway/C3 Rover avec cette brique UG,
dans un overlay externe transitoire ; les résultats, budgets et limites y sont
séparés des preuves du premier commit décrites ci-dessous.

## Périmètre implémenté

`components/ug_rtcm` compile **le fichier UG existant**
`protocols/gnss_protocols/src/rtcm_crc24q.cpp`, sans le copier ni le modifier.
Une façade C expose le CRC et la validation d'un conteneur RTCM3 complet.
Elle n'alloue pas, ne conserve aucun pointeur/état et ne consulte aucune horloge.
Le propriétaire du buffer reste l'appelant et garantit les octets lisibles et
le pointeur de sortie valides. La validation de trame est bornée à 1029 octets ;
le calcul CRC brut traite la longueur fournie par l'appelant.
Les arguments invalides sont refusés ; le résultat CRC reste inchangé sur erreur.
La sémantique spécifique des messages RTCM n'est pas validée par cette façade.
Attention à l'intégration future : le framer UG peut rendre `RecordReady` avec
un checksum invalide ; le consommateur doit vérifier explicitement ce statut,
pas assimiler un enregistrement complet à une correction admissible.

Le CRC historique est remplacé **seulement dans une copie de test temporaire**
par un appel à UG, puis les tests historiques inchangés sont exécutés.
Le parseur historique et la fragmentation ne sont pas importés dans UG.
`probe/` est une application de compilation, pas un firmware gateway ou rover.
Les binaires de probe utilisent les partitions génériques IDF : ne pas les
flasher sur les cartes du banc. Le lot suivant
[intègre le composant à la chaîne ESP-NOW réelle](integration/README.md) dans une
copie de build transitoire, sans migration des autres services.
Les preuves physiques historiques nominales (740/740) sont
acceptées pour poursuivre le développement ; les compléments d'endurance et de
portée se feront sur le terrain. Ce lot ne revendique aucune nouvelle preuve RF.

## Revue ciblée du premier commit proposé

| Contrat revu | Conclusion et preuve |
|---|---|
| API C/C++17 | Deux fonctions `extern "C"`, types C standards ; consommateur de test en C lié à l'implémentation C++17, probes IDF S3/C3 déjà compilés |
| Arguments CRC | Refus des pointeurs invalides détectables, NULL accepté seulement pour entrée vide, sortie inchangée sur erreur ; vecteur CRC24Q `123456789` → `CDE703` |
| Bornes de trame | 6..1029 octets, préambule et bits réservés vérifiés avant lecture du corps, longueur déclarée égale à la taille reçue ; tests 0..5, 1030 et SIZE_MAX |
| CRC et en-têtes | 1024 tailles de payload, CRC altéré ; préambule, bits réservés et longueur incohérente testés avec CRC recalculé valide pour isoler chaque contrôle |
| Mémoire | Revue du chemin complet façade + CRC UG : variables locales seulement, pas de new/malloc/vecteur/état global ; buffer emprunté, aucune copie complète ni horloge |
| Dépendances | Le composant IDF compile deux fichiers : façade et CRC UG inchangé ; headers UG privés, C++17 public, aucun backend POSIX/OpenSSL/ROS2/NTRIP |
| Distributions | Exclusion ciblée `embedded/esp32/`, COLCON_IGNORE ; huit plans comparés, voisin non classé toujours rejeté avec DistributionError ; aucune CI ni règle de publication modifiée |

Les tests hôtes de cette revue sont exécutés sous ASan/UBSan avec warnings en
erreurs. Les builds S3/C3 et les résultats natifs/générateur/Python précédents
restent valides : leurs sources de production sont inchangées par ces corrections
d'architecture et les ajouts au portail sont uniquement documentaires.
Aucune implémentation de moteur de passerelle, profil fonctionnel, client NTRIP,
runtime ou portail n'est ajoutée au contenu proposé pour ce premier commit.

## Commandes sans flash

Depuis la racine UG, utilisateur normal :

```bash
cmake -S embedded/esp32/tests -B /tmp/ug-embedded-host
cmake --build /tmp/ug-embedded-host
ctest --test-dir /tmp/ug-embedded-host --output-on-failure
python3 embedded/esp32/tests/test_distribution_boundary.py
python3 embedded/esp32/tests/check_historical_bridge.py --historical-repo /chemin/esp32-rtk-gateway
```

Le dernier script lit uniquement le commit épinglé via `git archive`, sans fetch,
checkout ou modification du dépôt historique. Il nécessite Git, C/C++, Python3.12
(pour l'extraction sécurisée de l'archive) et ASan/UBSan.
Avec ESP-IDF **5.5.3** activé :

```bash
idf.py -C embedded/esp32/probe -B /tmp/ug-s3 -D IDF_TARGET=esp32s3 -D SDKCONFIG=/tmp/ug-s3.config build
idf.py -C embedded/esp32/probe -B /tmp/ug-c3 -D IDF_TARGET=esp32c3 -D SDKCONFIG=/tmp/ug-c3.config build
```

Ces commandes compilent uniquement. Aucune interface radio, Ethernet ou USB de
production n'est liée par le composant UG lui-même.

## Audit de portabilité et décisions

| UG actuel | Décision | Limites sous IDF |
|---|---|---|
| `gnss_core` | ADAPT | Headers C++17 : modèle générique réutilisable, allocation et budgets à qualifier ; aucune réduction de précision autorisée |
| CRC24Q protocoles | ALREADY PRESENT | Réutilisé ici sans modification, compilé S3/C3 |
| Framer/parser RTCM UG | ADAPT | C++17 compilable en principe, mais vecteurs payload/raw/timestamps et récupération de trame embarquée diffèrent du parseur historique ; pas encore porté/compilé dans ce lot |
| `gnss_transport` | ADAPT | Interfaces ByteSource/Sink et writer génériques ; backends série/TCP/UDP Linux avec OpenSSL exclus ; créer un backend IDF indépendant |
| `gnss_ntrip` | ADAPT | Reconnect/request/GGA à réutiliser après audit ciblé ; NtripClient est gardé par `__linux__` et macro TCP, caster local dépend des sockets POSIX. Ne pas importer le client complet aveuglément |
| `gnss_driver` | ADAPT | Dépend protocoles/transport, C++17 et allocations ; qualification de sessions, reset, source/incarnation nécessaire, hors premier lot |
| `gnss_runtime` | ADAPT | Threads requis ; factory POSIX et supervisor Linux à exclure. FreeRTOS doit porter l'orchestration sans réinterpréter les contrats UG |

Cela n'est pas une certification des six bibliothèques complètes sur ESP32 :
seul le CRC UG est qualifié sous IDF, dans les probes du premier lot puis dans
les applications de référence du [lot suivant](integration/README.md).

Inventaire historique et traitement prévu :

| Fonction historique | Décision | Destination proposée |
|---|---|---|
| CRC24Q dupliqué | SUPERSEDED | Implémentation UG unique via façade C |
| Validation/filtre RTCM | ADAPT | Validation UG ; profils de filtrage à comparer explicitement avant substitution |
| Fragmentation/réassemblage ESP-NOW | PORT | Composant IDF dédié, protocole filaire conservé et tests rejoués |
| W5500 Ethernet | ADAPT | Adaptateur IDF BASE ; aucune dépendance de core/ vers une carte |
| NTRIP GET, GGA/retries | ADAPT | Réutiliser les politiques UG et un transport IDF ; traiter EOF, timeouts et limites HTTP sans modifier Linux |
| NVS/configuration persistante | ADAPT | Schéma versionné, migration et restauration, secrets jamais exportés dans les preuves publiques |
| Statistiques/diagnostics | ADAPT | Compteurs distincts file/âge/CRC/TX/RX et états de source ; canal de diagnostic séparé de l'USB binaire |
| WebUI | ADAPT | Surface légère BASE, contrats API à inventorier ; pas de copie massive du serveur historique |
| USB natif C3 | PORT | Sortie RTCM applicative binaire, phase ROM au reset explicitement séparée |
| LoRa, C6, appairage, `repeater-espnow`, multirobot | REJECT | Rejet pour ce lot uniquement, pas abandon du support futur ; `relay-ntrip` est un profil prévu du moteur commun, pas un nouveau moteur |

## Référence externe et licences

Pas de submodule ajouté : `Snapshot.__init__` dans `tools/branches/distribution.py`
rejette les gitlinks avant de lire les exclusions du manifeste. Un submodule,
même exclu, casserait donc le générateur actuel. Aucune protection n'est affaiblie.
`historical-reference.json` épingle URL/SHA et le sous-module SX126x imbriqué.
Le checkout historique existant reste utilisable en deuxième racine d'un workspace
éditeur (ajouter son dossier existant, sans nouveau clone UG).

UG contient une licence LGPLv3 ; le dépôt historique une GPLv3, avec notices GPLv3
ou ultérieure dans ses fichiers. Le sous-module Semtech/Lora-net conserve ses
notices Clear BSD, inventoriées dans le
[lot applicatif](integration/README.md#licences-et-limites-de-qualification).
Aucune source historique GPL ni source fournisseur
n'est copiée dans l'arbre UG. Le harnais extrait une copie locale temporaire pour
les tests, conserve les notices et ne distribue aucun nouveau firmware combiné.
L'import futur de code GPL et sa distribution devront faire l'objet d'une revue
de licence explicite ; cette séparation ne prétend pas dispenser des obligations
applicables à un futur binaire lié.

## Une distribution future, plusieurs profils

Une seule distribution future porte le nom **`esp32`**. Ses profils matériels et
fonctionnels sont des configurations de compilation de cette distribution, pas
des distributions distinctes, des forks du moteur ou des branches spécialisées.
Aucun profil ESP32 n'est encore ajouté au générateur ROS2 : `embedded/esp32/`
reste explicitement exclu de ses payloads. La brique RTCM et les applications de
référence construites par l'overlay transitoire ne constituent pas encore les firmwares définitifs de ces profils.

| Profil fonctionnel prévu | Fonction | Cible matérielle envisagée | État |
|---|---|---|---|
| `gateway` | Passerelle complète : GNSS local facultatif, NTRIP, Ethernet, ESP-NOW, configuration, diagnostics et WebUI | S3 + W5500 en premier | Overlay historique compilé ; application UG à porter |
| `relay-ntrip` | Passerelle légère sans récepteur GNSS : NTRIP sur Ethernet redistribué par ESP-NOW, services réduits | C3 + W5500 possible après étude broches/mémoire | Configuration du même moteur, non implémentée |
| `rover` | Réception ESP-NOW, réassemblage/validation, sortie RTCM USB ou UART ; services inutiles exclus | C3 en premier, C6 futur | Overlay historique compilé ; application UG à porter |
| `repeater-espnow` | Extension de couverture radio ESP-NOW | À étudier | Futur uniquement, aucune implémentation dans ce lot |

`relay-ntrip` désigne une source réseau Ethernet/NTRIP vers la radio. Ce n'est
pas le futur répéteur radio `repeater-espnow`.

## Moteur de passerelle unique et ownership

- `core/` et `protocols/` restent génériques et autoritatifs.
- `embedded/esp32/components/` accueille les façades C et adaptateurs ESP-IDF.
- Le futur composant commun `ug_gateway` possède le pipeline de corrections.
  `gateway` et `relay-ntrip` sélectionnent le **même moteur**, la même logique
  NTRIP et le même routage RTCM. Les différences portent sur les services compilés
  et la configuration, pas sur une copie du client ou du routeur.
- La BASE (`gateway`) peut fonctionner en mode relais NTRIP avec sa source GNSS
  locale désactivée. L'absence de GNSS n'est jamais une erreur de démarrage pour
  ce mode ; aucune observation ou phrase GGA de position n'est inventée.
- `relay-ntrip` ne doit ni initialiser ni imposer un driver, UART ou récepteur
  GNSS local. Ses dépendances de compilation excluront ces services. Un caster
  exigeant une position GGA nécessite une politique explicite ultérieure ; ce
  profil ne promet pas une compatibilité inconditionnelle avec tous les casters.
- Moteur commun : source Ethernet/NTRIP (et source locale facultative pour
  `gateway`) → validation RTCM UG → routage commun → file bornée → ESP-NOW TX.
- Le futur adaptateur NTRIP IDF réutilisera les politiques portables UG ; le client
  Linux actuel n'est pas copié dans deux applications. L'extraction nécessaire
  préservera les API/comportements Linux dans un lot distinct.
- `rover` partage validation/transport RTCM, mais n'embarque pas le moteur de
  passerelle NTRIP, Ethernet ou WebUI. Callback RX copie bornée → worker et
  réassemblage → validation UG → USB/UART binaire.
- Les services NVS, diagnostics et WebUI sont modulaires : la WebUI complète
  appartient à `gateway`, elle n'est pas une dépendance obligatoire des profils
  légers. Aucun de ces services n'est implémenté dans ce premier commit proposé.
- Le worker possède les buffers ; aucun parseur, USB bloquant ou allocation dans
  le callback Wi-Fi. Ne pas transformer une retransmission en observation nouvelle.
- Au remplacement/reconnect d'une source : invalider parser, fragments incomplets
  et files de l'ancienne incarnation. Le propriétaire du transport autorise le reset.
  La reprise physique sans anciens octets reste à qualifier, pas à supposer.
- Fraîcheur : horloge monotone IDF, distincte de l'heure GNSS/UTC. Un message retardé
  garde sa date de réception initiale ; aucun mélange d'horloges implicite.
- Canal radio fixe explicitement configuré, aucune association STA nécessaire au
  transport ESP-NOW ; aucun scan/appairage automatique dans ce lot.

## Portail officiel UG Build & Flash — lot futur

Le portail sera intégré à Universal GNSS. La page historique `flasher.html` et
ses fonctionnalités pertinentes seront auditées, migrées et modernisées après
revue des licences et notices. Le présent lot ne copie ni ne développe ce portail,
ne configure pas GitHub Pages et ne crée aucun workflow de publication.

La distribution future reste unique : `esp32`. Le portail propose des profils
validés, pas des distributions supplémentaires. Les rôles affichés correspondent
aux profils : BASE RTK → `gateway`, RELAIS NTRIP sans GNSS → `relay-ntrip`,
ROVER → `rover`. BASE peut aussi utiliser le mode relais du moteur commun.
`repeater-espnow` demeure hors sélection tant qu'il n'est pas implémenté/qualifié.

Fonctions à porter et concevoir :

- Sélection de famille ESP32, carte, rôle, version du firmware et capacités
  Ethernet/GNSS/LoRa/flash/PSRAM. Une option future n'est pas présentée comme
  supportée : le portail n'autorise que les combinaisons réellement validées.
  `relay-ntrip` exclut explicitement un récepteur GNSS local.
- Récupération des binaires issus de compilations CI identifiées (commit, profil,
  SDK et configuration), téléchargement des images et flash USB navigateur via
  ESP Web Tools. L'absence de support Web Serial doit laisser le téléchargement
  disponible sans prétendre à un flash réussi.
- Génération, validation puis publication des manifestes ESP Web Tools depuis
  les mêmes artefacts vérifiés ; conserver leurs offsets, tailles, puce cible,
  empreintes et provenance. Les métadonnées supplémentaires nécessaires au
  portail ne doivent pas inventer des champs incompatibles avec ESP Web Tools.
- Le bouton **Build** lance un véritable processus de compilation CI ou une
  demande de build autorisée et affiche son état/résultat. Générer un fichier
  de profil ne constitue pas un build. Aucun jeton de publication ne doit être
  embarqué dans une page statique publique ; authentification et autorisation
  du déclenchement seront définies dans ce lot.
- Séparer installation complète et mise à jour compatible conservant NVS et
  configuration. Une mise à jour ne promet la conservation que si puce, carte,
  flash, table/offsets/slots et schéma de configuration sont compatibles.
  Ne pas traiter une réécriture complète ou un changement de partitions comme
  une simple mise à jour. Prévoir sauvegarde, restauration et décision explicite
  avant une opération qui efface ou migre des données.
- Refuser les profils, images et partitions incompatibles. Les contrôles doivent
  porter sur les artefacts et l'identité réelle disponible du matériel ; si une
  information nécessaire sur l'installation existante n'est pas vérifiable,
  ne pas annoncer une mise à jour préservant les données comme sûre. Les limites
  d'identification de chaque carte et du navigateur feront partie de la revue.

La CI couvrira une **liste de profils supportés** et leurs configurations de
référence, pas le produit cartésien de toutes les options matérielles. Les profils
à retenir seront déterminés quand leurs applications seront prêtes : S3/W5500
`gateway` et C3 `rover` en premier ; C3/W5500 `relay-ntrip` uniquement après étude
et validation. Les configurations proches réutilisent le moteur et les étapes
communes, tout en contrôlant leurs limites de flash/RAM et leurs exclusions de
services. Les branches de contribution ne publient pas automatiquement d'images
ou de manifestes officiels ; le circuit de validation et de publication restera
conforme à la politique `dev → main → distributions générées`.

Ce lot futur devra qualifier la matrice, le déclenchement réel de Build, la
traçabilité des versions, les manifestes, les cas négatifs de compatibilité et
les deux procédures d'installation/mise à jour. Aucune compatibilité du flasheur
historique avec les nouveaux profils UG n'est supposée acquise.

## Mémoire et limites

La façade CRC/validation n'alloue pas, ne duplique pas les trames et ne possède
aucun stockage persistant. Les buffers appartiennent au transport appelant.
Le framer UG actuel conserve des vecteurs raw/payload et timestamps par octet :
son budget ne doit pas être assimilé au parseur C historique à buffer fixe.
Le code IDF doit plafonner toutes les files et traiter les écritures partielles.

Probes IDF5.5.3 par défaut (pas le gateway complet) : S3 image189913octets,
DIRAM55955octets ; C3 image166630octets, DRAM57140octets, selon `idf.py size`.
Le tableau S3 indique aussi une région IRAM réservée de16384octets entièrement
occupée ; ce n'est pas la preuve d'un épuisement de toute la RAM S3.
Ce sont des tailles statiques d'un probe incluant le runtime IDF, pas le heap
libre réel sous Wi-Fi/W5500, ni le coût isolé du CRC.

Cible matérielle : S3 flash16Mo/PSRAM8Mo, C3 flash4Mo sans PSRAM.
Les queues historiques seules représentent environ8,3Ko (8slots complets), auxquels
s'ajoutent deux codecs, message worker, stack4096 et buffers USB. Les régions DMA,
interruptions et Wi-Fi doivent rester en mémoire interne appropriée. Mesurer le
heap minimum et les high-water marks sur les futures applications avant déploiement.

## Validation et lots suivants

Rejoués : historique12groupes C +11Python ; façade C1CTest couvrant1024tailles,
CRC golden, corruption, arguments et headers invalides ; même chaîne historique
avec CRC UG :12groupes C +11Python sous sanitizers. Probes S3/C3 compilés, non flashés.
UG natif68/68CTest, politique/générateur200/200, Python existant55/55.
Frontière distributions :2tests, huit plans et inventaires/source payloads identiques,
chemin expérimental exclu et chemin voisin non classé toujours rejeté.
Le manifeste change uniquement pour classifier `embedded/esp32/` : les métadonnées
manifest/SHA générées changent légitimement, pas les fichiers sources copiés.
`COLCON_IGNORE` empêche aussi la découverte involontaire du probe ESP-IDF
par colcon dans le checkout complet. Sans commit UG autorisé, cette comparaison utilise le snapshot candidat en mémoire,
pas une publication ni une génération prétendument issue d'un SHA UG final.
Aucun build ROS2/MAVROS supplémentaire, CI distante, flash ou endurance n'a été rejoué.

1. Premier commit proposé : composant RTCM portable, façade C minimale, probes,
   tests et documentation indispensables. Exclusion ESP-IDF du générateur ROS2.
2. Intégrer ce composant au transport ESP-NOW historique conservé ; préparer
   `rover` et les interfaces du moteur `ug_gateway`, sans second moteur RTCM.
3. Porter un seul adaptateur NTRIP IDF et le routage commun ; permettre le mode
   relais NTRIP de `gateway` et la configuration légère `relay-ntrip`, sans GNSS
   imposé. Préserver le client Linux, traiter EOF, erreurs, reconnexion et GGA.
4. Ajouter NVS versionnée, diagnostics séparés puis WebUI légère de `gateway`.
   Qualifier C3 + W5500 avant d'annoncer cette cible pour `relay-ntrip`.
5. Étendre la distribution unique `esp32` aux profils de compilation effectivement
   prêts, dans un lot spécifique du générateur ; aucune branche de distribution
   ni publication n'est créée ici.
6. Auditer les licences et `flasher.html`, puis migrer le portail officiel Build &
   Flash dans UG avec sa matrice CI ciblée, ses vrais builds autorisés, ses
   artefacts/manifeste ESP Web Tools et ses protections installation/mise à jour.
   Dépend des profils, tables et schémas NVS validés dans les lots précédents.
7. Fiabilité complémentaire et portée sur le terrain ; C6, LoRa, appairage,
   `repeater-espnow` et multirobot restent des chantiers ultérieurs.

GO pour revue du premier commit UG, uniquement sur ce périmètre. Aucun commit UG,
push UG, PR, publication ou flash autorisé/exécuté par ce lot. Le moteur portable,
ROSIDL, Lyrical, MAVROS et les workflows Release n'ont pas été modifiés.
