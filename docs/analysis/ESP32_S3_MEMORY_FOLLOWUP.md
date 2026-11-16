# ESP32-S3 : investigation et qualification du stockage satellites

Les sections préliminaires ci-dessous conservent le raisonnement avant la phase 2.
Les mesures de la variante seront distinguées de ces tailles candidates dans la
section de qualification ajoutée en fin de rapport.

## Référence et périmètre

Le travail [qualifié](ESP32_S3_MEMORY_QUALIFICATION.md) a été commité et poussé
sur `perf/esp32-s3-memory` :
`8a5ca854b5e0eca042cff6243b88174efe863b80`.
Les 26 fichiers du rapport constituent exactement ce commit. Les indications de
non-publication à la fin du rapport décrivent l'état avant cette publication.

Cette investigation distincte part du même commit sur
`perf/esp32-s3-memory-followup`. Elle ne change aucun firmware. Pas de nouvelle
compilation, requête matérielle, reset ou écriture Flash. Le C3 relais, le moteur
partagé, les protocoles, TLS et les seuils restent hors périmètre des modifications.

Les mesures déjà qualifiées sont conservées. Le but est de sélectionner un
prochain lot sans confondre tailles de structures, mémoire effectivement
récupérable et gain de bloc contigu.

## Mesures statiques actualisées

Source : ELF exact de l'application qualifiée, SHA-256 binaire
`5f2216881bf80467e90a1357b100216563d20e99e85268ad1d48952bf16a83c0`.
Sections/symboles lus par `readelf -SW` et `readelf -sW`. Les tailles de types
ci-dessous sont lues dans le DWARF Xtensa avec pyelftools du SDK épinglé, en
UID 1000, image en lecture seule, sans réseau, périphérique ni montage.
Il ne s'agit pas de `sizeof` hôte x86.

Les données statiques internes sont : `.dram0.data` **23725**, `.dram0.bss`
**107920**, `.noinit` **3**, soit **131648 octets**. Les sections `dummy`,
les assets Flash, les piles et les allocations dynamiques ne s'additionnent pas
à ce total comme s'ils étaient des objets BSS.

| Objet actuel | Octets liés | Propriétaire / frontière |
| --- | ---: | --- |
| `receiver` | 19576 | Acteur GNSS, sessions et parseurs natifs |
| dont `SatelliteReports` | 11856 | Sous-objet inclus dans `receiver`, pas à additionner une seconde fois |
| `satellite_snapshot` | 1080 | Copie publiée sous `status_guard` |
| `command_snapshot` | 928 | Historique de commandes sous le même mutex |
| `gateway` | 17608 | Acteur de routage RTCM |
| `publishers[5]` | 11060 | Files, métadonnées FreeRTOS et contrôle ; 2212 par élément |
| `caster_box` | 2212 | Même type `Outbox` |
| `ingress_memory` | 8512 | Stockage de file FreeRTOS |
| `s_queue` | 8328 | File ESP-NOW |
| `s_stream` et `s_radio` | 4432 au total | Deux codecs radio, chacun 2216 |
| historique NTRIP | 808 | Accès sous section critique `metrics_guard` |
| événements services | 800 | Accès sous mutex du superviseur |

Le chiffre ancien de 11020 octets pour les publishers et de 2204 pour le caster
ne décrit plus cet ELF : ils valent maintenant 11060 et 2212. Ces différences
sont constatées sans réécrire l'étude initiale préexistante.

## Candidat suivant : stockage satellites natif

**12936 octets de contenu candidat**, constitués du cache de 11856 octets et
du snapshot de 1080. Ce nombre est une taille liée, pas un gain PSRAM mesuré ni
une garantie d'un bloc libre de cette taille. L'indirection, l'alignement et les
métadonnées d'allocation doivent être déduits d'une future mesure.

Contrats à conserver :

- `Receiver` possède et met à jour le cache ; `Owner` dans
  `ug_gnss_service.cpp` possède la lecture UART et les mutations de session.
- `SatelliteReports::Observe` utilise les observations décodées ;
  `Latest(now)` sélectionne les rapports sans réécrire leur `receipt_ns`.
  Le délai de fraîcheur reste trois secondes sur l'horloge monotone native.
- Les cycles GSV restent séparés par talker/signal ; les namespaces protocoles
  et les époques vendor ne sont jamais fusionnés pour économiser de la place.
- `Receiver::InvalidateObservations`, les changements de configuration/source
  et les chemins d'invalidation du service conservent leur autorité. Les
  snapshots sont invalidés sous mutex, sans nouvelle observation fictive.
- Le snapshot transporte `receiver_incarnation`. `ReceiverSatellites()` en
  fait une copie sous `status_guard`, puis HTTPS sérialise après libération
  du verrou. Une opération en attente rend le snapshot indisponible.
- L'API recalcule l'âge et `stale` au moment de la lecture, avec le timestamp
  original. Copier moins souvent ne devra jamais rafraîchir cette observation.

Le cache est un membre par valeur de `Receiver` : allouer seulement un deuxième
cache externe sans retirer l'ancien ne libérerait aucun BSS interne. Déplacer
`receiver` entier déplacerait aussi sessions, parseurs et autres buffers ; ce
n'est pas un changement de stockage satellites isolé. La prochaine conception
doit séparer le stockage borné des rapports de l'acteur et des contrôles, avec
allocation S3 explicite et gestion de l'échec, sans modifier le moteur partagé.
Les références renvoyées par `Latest()` doivent rester valides jusqu'à la
prochaine mutation par leur propriétaire ; aucune durée de vie temporaire.

## Postes conservés et variantes écartées pour le prochain lot

- Ne pas déplacer un `Outbox` entier ou `ingress_memory` comme de simples
  tableaux : `xQueueCreateStatic` associe contrôle FreeRTOS et copies sous
  verrouillage SDK. Une preuve distincte de compatibilité est nécessaire.
- Ne pas déplacer en masse l'historique NTRIP et ses verrous : l'implémentation
  utilise `portENTER_CRITICAL`. Garder les régions de contrôle internes tant
  que leurs conditions d'accès externe ne sont pas qualifiées.
- Ne pas déporter `gateway` ou les codecs/files radio dans le lot satellites :
  ils touchent au temps réel et à l'invalidation RTCM/ESP-NOW.
- Ne pas réduire/déplacer les piles. Les cinq tâches publisher réservent
  40960 octets de pile même quand leurs services configurés sont inactifs ;
  `StartNetworkServices` les crée selon les capacités de carte. Une création
  à la demande changerait le cycle de vie, l'observabilité, le watchdog et
  l'activation dynamique. Ce serait un lot séparé, pas un gain immédiat acquis.
- Conserver les hooks JSON qualifiés et la configuration TLS. Les volumes
  cumulés des traces ne permettent pas de réserver leur somme comme gain vivant.

## Taille du problème restant

Au premier GET de référence du firmware final, 76539 octets internes libres
laissent **21765 octets** jusqu'au seuil inchangé de 96 Kio. Avec trois clients,
le minimum observé de 75439 laisse **22865 octets** ; le bloc maximal de 31744
reste aussi 1024 octets sous 32 Kio.

Même une récupération idéale des 12936 octets satellites laisserait au moins
8829 octets à récupérer au premier GET, et 9929 sous trois clients, avant
surcoûts. Le placement satellites seul ne suffit donc pas à promettre la sortie
de pression réduite ; il ne promet pas non plus une amélioration de contiguïté.

## Prochaine expérience bornée

1. Concevoir le stockage externe natif des rapports sans deuxième copie BSS,
   sans déplacer sessions, verrous, piles ou buffers DMA.
2. Ajouter les tests d'allocation refusée et de durée de vie, puis conserver
   les corpus satellites/receiver : cycles GSV, sélection vendor, époques
   identiques, invalidation, timestamps absents et incarnations.
3. Comparer l'ELF Xtensa et les symboles liés avant/après ; ne pas traduire
   directement la taille théorique en gain de tas.
4. Après qualification logicielle, reprendre le protocole HTTPS identique,
   libre total et plus grand bloc, plus le profilage du propriétaire GNSS et
   une charge série synthétique bornée. Aucun nouveau flash dans cette étude.
5. Distinguer les mesures synthétiques d'une qualification GNSS/RTCM terrain,
   toujours absente ici. L'endurance prolongée n'est pas acquise par les dix
   cycles précédents.

**Décision : GO pour concevoir ce stockage séparé ; aucune optimisation
supplémentaire n'est encore implémentée ou qualifiée.** Les résultats de la
qualification publiée ne sont pas modifiés. Toute modification du SDK, de
l'ELF ou de l'ownership décrit impose de réévaluer cette sélection.


## Phase 2 — variante expérimentale S3

Base de travail actualisée par fast-forward :
`c79e3b39b755282b1b14a580035f6eb47574094f`, qui conserve l'exclusion CI du C3 Relay.
Son unique modification par rapport à `8a5ca854` est cette ligne de matrice CI ;
le firmware qualifié de référence reste inchangé.

La variante déplace **le cache seul**. Les 1080 octets du snapshot partagé restent
internes, avec leur mutex et leurs copies inchangés. Les deux caches vendor,
le rapport NMEA complet et les huit cycles partiels conservent leurs structures.
Sur Xtensa : 1072 octets par rapport ; 1080 par cycle partiel, soit
`2 × 1072 + 1072 + 8 × 1080 = 11856` octets de stockage.

`SatelliteStorage<Data>` remplace le membre volumineux par un propriétaire stable
non copiable. La construction globale n'alloue rien. `StartGnssService()`
initialise le stockage après le démarrage PSRAM, avant de créer le propriétaire
UART. L'allocation utilise exclusivement `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`,
sans repli interne. En cas d'échec, le service GNSS retourne `ESP_ERR_NO_MEM` ;
les services indépendants restent disponibles et aucune opération GNSS ne peut
être mise en file sans propriétaire. L'échec est mémorisé jusqu'à recréation du
sous-système/redémarrage, sans boucle de réallocation cachée.

`Observe()` conserve la possibilité d'une initialisation au premier usage pour
les corpus hôtes ; le service natif initialise explicitement avant toute réception.
`Latest()` n'alloue jamais et renvoie un rapport vide constant si le stockage est
indisponible. `Reset()` vide les contenus sans libérer/réallouer leur stockage.
La destruction détruit la valeur et libère une fois son allocation. Aucune copie
supplémentaire du cache de 11856 octets n'est conservée.

### Qualification logicielle

- **51/51 CTest ASan/UBSan** : les contrats receiver et satellites sont compilés
  et exécutés avec stockage interne, puis avec stockage externe simulé.
- Comparaison canonique stricte de toutes les projections du même corpus
  satellites, champs et 64 entrées compris : sorties identiques. Les assertions
  existantes ne sont pas affaiblies : cycles incomplets/tronqués, talker/signal,
  UBX, Unicore, mêmes époques, timestamps absents, fraîcheur et remise à zéro.
- Tests de premier refus d'allocation, absence de repli/réessai, lecture sans
  allocation, pointeurs stables, reset/réutilisation, destruction unique et
  canaris intacts. Ces refus sont simulés, pas un épuisement matériel de PSRAM.
- SDK S3 épinglé : deux reconstructions indépendantes identiques, 51 tests hôtes,
  12 tests QuickJS WebUI et les suites flasheur/portable/capture conservées.
- Arbre qualifié : `eb24085abfa20028a22980b236c601841c73ea08` ; application
  **1470672 octets**, SHA-256
  `477093f1e4716501dd7ccdf471adb2b36088411ce9a9a2a1eabe1e8ffa7c682f`.
  Par rapport à la référence : +1440 octets Flash, aucun changement de partition.
- ELF/MAP : `receiver` 19576 → **7728** octets ; `.dram0.bss` 107920 → **96048**.
  Snapshot inchangé à 1080. Le rapport vide de 1072 octets est dans
  `.flash.rodata`, pas dans un second tampon interne. Les piles, DMA et sources
  du moteur/protocoles UG partagés sont inchangés.

### Comparaison matérielle A/B — 15 novembre 2026

Carte S3 réidentifiée par ROM, flash 16 Mio, application de référence vérifiée
par digest. Table/NVS/OTA comparées à la sauvegarde privée avant et après
l'écriture de la seule application `0x20000`. Aucun effacement global, autre
segment écrit, changement de configuration ou périphérique C3/C6 utilisé.
La PSRAM effective du cache est confirmée au boot : 11856 octets, externe, prête.

Même suite de requêtes que le lot précédent : première lecture après délai de
cinq secondes, 1/2/3 sessions TLS persistantes, huit tours chacun, puis 72 GET
status/config et récupération. Ensuite trois séries CPU de 24 GET, traces TLS
et JSON distinctes, dix cycles WebUI desktop/mobile/login/logout/six onglets.
Certificat vérifié, paramètres réels et identité administrateur conservés.
Le relevé de démarrage reste un premier GET après contrôles, pas un tas à froid :
le repos préalable de la référence est plus long en raison des préparatifs.
La référence historique au même premier GET donnait 76539 octets contre 76523
ici, soit 16 octets de variabilité ; aucune précision temporelle à l'instruction
près ni significativité statistique des latences n'est revendiquée.

Cellules : **libre interne / plus grand bloc**, octets. Les séries HTTPS donnent
les minima échantillonnés ; ce ne sont pas tous les pics d'allocation TLS.

| Mesure | Référence USB+JSON | Cache externe | Gain libre interne |
| --- | ---: | ---: | ---: |
| Premier GET | 76523 / 32768 | 88383 / 34816 | +11860 |
| 1 client(s) HTTPS | 76523 / 32768 | 86943 / 34816 | +10420 |
| 2 client(s) HTTPS | 76099 / 31744 | 87935 / 34816 | +11836 |
| 3 client(s) HTTPS | 75611 / 31744 | 87315 / 34816 | +11704 |
| Après 72 GET | 76523 / 32768 | 88359 / 34816 | +11836 |

Le gain observé au premier GET est **11860 octets**, pas les 12936 octets du
candidat initial. Le snapshot reste interne et les métadonnées/alignements
expliquent pourquoi une taille de type ne se traduit pas directement en libre.
Le plus grand bloc progresse de 2048 octets au premier GET et de 3072 sous trois
clients. La pression reste `reduced` : aucun seuil n'a été diminué et les API
réservées à la pression normale restent protégées.

PSRAM libre au premier GET : **8343168 → 8331132**, soit −12036 octets observés.
Après les cycles, la console sans session HTTPS relève :

| Tas | Libre référence → variante | Plus grand bloc | Minimum historique |
| --- | ---: | ---: | ---: |
| Interne | 77143 → 88975 | 32768 → 34816 | 39828 → 47140 |
| DMA | 69355 → 81187 | 32768 → 34816 | 32040 → 39352 |
| PSRAM | 8368220 → 8356184 | 8257536 → 8257536 | 8269200 → 8256144 |

DMA est un sous-ensemble de capacités du tas interne : ne pas additionner les
deux lignes. Les minima SDK sont historiques et par région, pas un minimum
global simultané ni le libre utilisé pour l'admission. Après protocole/cycles,
les blocs internes alloués passent 298 → 295 dans les deux variantes ; blocs
libres 8 → 11 pour la référence, 10 → 11 pour la variante. La PSRAM conserve
une allocation supplémentaire permanente (53 → 54 blocs). Aucun échec réel
`heap_caps` n'a été enregistré. Ces compteurs ne sont pas un indice universel
de fragmentation, notamment avec plusieurs régions de tas.

Dix cycles : référence **129,83 s**, variante **132,03 s**, 280 requêtes API chacune,
zéro erreur JavaScript/POST. Libre interne de récupération : 76443–76463 contre
88255–88295 ; plus grand bloc constant 32768 contre 34816 ; PSRAM constante à
8343168 contre 8331132. Aucune dérive décroissante soutenue dans cette fenêtre.
Ce test ne remplace pas l'endurance prolongée.

Latences des 72 GET : médiane **62,82 → 64,06 ms**, maximum **85,38 → 84,52 ms**.
Navigateur : médiane **74,14 → 74,52 ms**, maximum **2161,33 → 2225,03 ms** ;
pas d'expiration de requête à 10 secondes. Pas de gain CPU revendiqué : les trois
séries donnent 40,24 / 40,20 / 39,37 % pour la référence et
27,59 / 39,06 / 39,85 % pour la variante. La première valeur plus faible est
conservée comme observation variable, pas utilisée pour promettre une économie.
Ces chiffres incluent ordonnancement/réseau/HTTPS, pas la latence GNSS/RTCM.

Traces sans saturation : TLS **557 → 581** entrées, JSON **562 → 585**. Les
106 allocations attribuées à TLS/crypto demandent 25929 octets cumulés dans
chaque cas (23017 internes, 2912 externes), attribution limitée à quatre frames.
Les 301 allocations cJSON demandent 14603 octets cumulés, toutes externes et
libérées dans les deux cas. Les différences des autres catégories incluent les
activités SDK concurrentes ; on ne peut pas en déduire un pic vivant exact.
Allocations ISR exclues par le mode de trace externe. Pas de changement TLS.

Le désassemblage Xtensa montre un cadre de 32 octets pour `Initialize()` et
pour le constructeur `Data`, qui initialise le stockage en place ; aucun
second cache de 11856 octets n'est créé sur la pile. Ces cadres isolés ne sont
pas un maximum de chaîne d'appels. Les piles FreeRTOS réservées sont inchangées.

### Résultat et limites

Redémarrage final réussi : HTTPS, Ethernet et propriétaire GNSS `running` ;
identité TLS prête, administrateur conservé et génération de configuration 1.
GET `/`, ressources, statut et configuration : 200 avec certificat vérifié ;
configuration strictement identique ; absence d'authentification refusée 401.
Sans récepteur raccordé, `gnss_owner running` ne prouve aucune réception ni FIX.

Un test de politique CI préexistant exigeait encore C3 Relay malgré son retrait
par `c79e3b39`. Le commit séparé `4dc2f83` aligne seulement cette assertion sur
les trois variantes automatiques et conserve le profil historique manuel.
Workflow, gate et firmware C3 inchangés ; **17/17 tests CI/alpha passent ensuite**.
Le moteur UG partagé, les profils C3, ROSIDL et les versions sont inchangés.
La qualification GitHub de ces nouveaux commits n'a pas été exécutée ici.

**GO pour conserver et commiter la variante cache satellites S3.** Gain interne
et gain contigu démontrés, comparaisons fonctionnelles identiques, aucune
régression bloquante observée sous la charge testée. Pas de promesse de mode
normal, de baisse CPU ou de qualification GNSS/RTCM physique ; endurance et
latence sous réception réelle restent ouvertes.

### Piste suivante, étudiée sans implémentation

Les buffers radio permanents représentent **13800 octets liés** : mailbox8328,
deux codecs4432 et message worker1040. Ils restent réservés quand la radio est
désactivée. Une allocation uniquement à l'activation paraît une piste possible,
mais pas encore qualifiée : contrôle/stats, mutex, callbacks et travailleurs
continuent d'exister ou de consulter cet état. Il faut séparer payload et
contrôle, traiter les échecs d'activation tardive, la fragmentation et la
quarantaine des ACK tardifs. Libérer à la désactivation demande de prouver
l'absence de callback/worker en vol ; conserver la réservation après première
activation serait une autre politique explicite. Aucune donnée DMA/Wi-Fi n'est
classée déplaçable sur la seule base de sa taille.

Les tâches des cinq publishers réservent toujours 40960 octets de pile. Leur
création à la demande serait un chantier de cycle de vie distinct ; ces piles
ne sont ni déplacées ni réduites ici. Aucune seconde optimisation n'est implémentée.
