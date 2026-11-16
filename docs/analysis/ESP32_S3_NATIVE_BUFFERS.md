# ESP32-S3 — stockage natif RTCM en PSRAM

Statut : **PARTIAL**, portée DEPLOYMENT, acceptation **HARDWARE_REQUIRED**.
Branche locale `perf/esp32-s3-native-buffers`, base
`19541b77f8d565d95bb40e250b5acdee798b3c4a`. Créée sur `c185166…`, puis avancée
uniquement par le correctif de test PR #51, sans changement fonctionnel de base.
Le commit et le push pour revue sont autorisés par le mainteneur. Aucun flash
ni publication de firmware ; la qualification physique reste à effectuer. Les
métadonnées de mesure décrivent l’arbre testé avant cette conservation Git.

## Résultat mesuré et frontière de preuve

L’ELF S3 libère **28224 octets de stockage statique DRAM** ; le routeur alloue
explicitement **28240 octets en PSRAM** au démarrage. Les tests logiciels et les
reconstructions HTTP/HTTPS passent. Ce gain statique réel n’est **pas** une mesure
du heap libre, du plus grand bloc ou de la latence sur la carte après installation.
Les seuils restent 96/32 Kio (normal) et 48/16 Kio (critique).

Les [mesures et empreintes JSON](ESP32_S3_NATIVE_BUFFERS_MEASUREMENTS.json) conservent
la base, l’arbre de validation, les tailles, les résultats bruts A/B et les hashes.
L’étude du 15 novembre (`docs/analysis/ESP32_S3_MEMORY_AUDIT.md`) est une référence
locale préexistante non suivie, laissée intacte ; ses estimations ne remplacent pas
ces ELF. Elle n’est pas une dépendance documentaire à ajouter au commit.
Les [structures GNSS compactes](GNSS_BUFFER_MEMORY_OPTIMIZATION.md) sont déjà dans
la référence logicielle : leur gain n’est pas compté une deuxième fois ici.

## Optimisations déjà présentes — non refaites

| Stockage | État constaté dans le code | Décision de ce lot |
| --- | --- | --- |
| Ligne USB de maintenance | PSRAM explicite S3 | Conservée |
| Allocations cJSON | Hooks PSRAM, échecs et effacement déjà testés | Conservés |
| Cache satellites possédé | `SatelliteStorage`, initialisé avant la tâche GNSS en PSRAM | Conservé |
| Outboxes cinq publishers + caster | Six payloads de 2112 octets en PSRAM, contrôles FreeRTOS internes | Conservées |
| ESP-NOW | Bundle de 13800 octets INTERNAL/8BIT, une allocation à la première activation, retenue jusqu’au reboot | Inchangé |

Le snapshot satellite de supervision restant et les piles ne sont pas déplacés.
L’ESP-NOW conserve ses codecs, mailbox, epochs, ACK tardifs et quarantaine. Son
économie radio arrêtée ne devient pas un gain avec radio active. Déplacer aussi
ce bundle sans nouvelle preuve de contexte/cycle de vie aurait un risque supérieur.

## Modification retenue

Le service natif de routage S3 place dans un unique `RoutingPayload` :

| Élément Xtensa | Octets |
| --- | ---: |
| Objet Gateway, dont 16 slots de trames | 17608 |
| Ingress : huit messages Input | 8512 |
| Trame de sortie possédée par le worker | 1056 |
| Message d’entrée possédé par le worker | 1064 |
| Total PSRAM demandé | **28240** |

Le petit `BootPayload` interne fait 8 octets. Le contrôle FreeRTOS de l’ingress
reste interne, **92 octets**. Verrous, contrôles de source/destination, statistiques
publiées, piles et buffers UART/SPI/DMA restent dans leur classe mémoire existante.
Les champs propres au Gateway sont exclusivement accédés par sa tâche propriétaire ;
ils ne sont pas des contrôles FreeRTOS. Les vecteurs dynamiques internes aux framers
conservent leur allocateur existant et ne sont pas comptés comme relocalisés.

`MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT` est obligatoire, puis la région retournée est
vérifiée par `esp_ptr_external_ram`. Un résultat non externe est libéré et refusé.
Allocation et construction précèdent la publication de la file et la création de
la tâche. Aucun fallback en SRAM, aucune réduction de capacité, aucun free ou
reallocation en fonctionnement. Un échec mémoire reste refusé jusqu’au reboot.
Après un échec SDK de file/tâche, le bloc déjà valide est réutilisable à la même
adresse ; aucun second bloc n’est alloué. La construction Gateway/framer n’ajoute
pas d’allocation C++ cachée dans le test du vrai démarrage.

La supervision marque le routage en échec si nécessaire ; NTRIP/radio dépendants
ne démarrent pas via le plan de boot. La console locale et les services indépendants
ne sont pas supprimés. Le diagnostic `UG_MEM routing_storage` expose uniquement
une taille, la région et la disponibilité, sans contenu RTCM ni secret.

Le chemin non-S3 garde ses stockages statiques, types et capacités. Aucun fichier
spécifique C3 ni profil C3 n’est modifié ; aucune nouvelle compilation/écriture
matérielle C3 n’est effectuée. Le moteur UG commun et ses contrats sont inchangés.

## Accès et copies RTCM

Les producteurs continuent à copier un message complet borné dans l’ingress, sans
attente. Le worker est seul propriétaire du Gateway et de ses deux temporaires.
Les destinations empruntent une trame uniquement pendant le callback et copient
vers leur propre file ; aucun pointeur vers un buffer réutilisable n’est conservé.
Les contrôles et verrous de publication restent internes. Les appels examinés sont
des tâches ordinaires, pas des ISR/DMA ou des chemins cache désactivé. Le SDK 6.1
épinglé accepte séparément le contrôle et le stockage de `xQueueCreateStatic` ;
c’est le même modèle déjà employé par les outboxes externes. Les copies FreeRTOS
restent sous les verrous du SDK ; leur durée sous contention PSRAM reste à mesurer.

Les copies ne sont pas éliminées : entrée vers Input, deux transferts de file,
construction d’une Frame, fanout vers les files Gateway, Pop, puis outboxes réseau
ou mailbox ESP-NOW et fragmentation. Une Frame fait 1056 octets même pour une
petite trame ; l’ingress copie un Input de 1064 octets. À sept destinations,
chaque écriture/lecture des slots Gateway peut représenter 7×1056 octets, en plus
des copies aval. Les petites trames sont donc un stress nécessaire. Les contrôles
CRC/fraîcheur/incarnation restent aux mêmes frontières. Pas de handles partagés,
référence comptée, nouvelle propriété de buffer ou réduction de backpressure.

## Comparaison ELF et images reproductibles

Référence : arbre compact validé `2275b09f614a0894f396811b7c9f92c4a608e692`, dont
les entrées de production concernées correspondent à la base actuelle. Candidat :
`14d053ae0b36ec9b59044b56656fd4fa3a8eedbc`, arbre local et non commit.
SDK : `espressif/idf@sha256:81893c71bb5e570088901f21def8684c25cd2a9020281bd01b843a7655edb18c`.

| Mesure | Référence compacte | Candidat natif | Variation |
| --- | ---: | ---: | ---: |
| `.dram0.data` | 23757 | 23757 | 0 |
| `.dram0.bss` | 70608 | 42384 | **−28224** |
| Payload externe au démarrage | 0 pour ce service | 28240 demandés | +28240, hors overhead SDK |
| Application HTTP | 1485792 | 1486000 | +208 |
| Application HTTPS | 1490544 | 1490768 | +224 |

Les symboles statiques Gateway/Input/Frame/ingress disparaissent au profit du
contrôle de 8 octets ; l’alignement de section explique l’écart entre la somme
brute relocalisée et la baisse de BSS. Les quatre segments de chaque variante
sont identiques entre deux reconstructions indépendantes. Bootloader, partitions
et initialisation OTA restent identiques aux références. Assets WebUI, mémoire de
boot et budgets réseau restent identiques. Le probe de tailles n’est jamais lié.

## Débit, temps et limites du banc hôte

Le [comparateur](../../embedded/esp32/native/tests/compare_routing_storage.py)
compile le véritable service antérieur extrait du commit de base et le candidat,
avec le même harness, les mêmes stubs et les bibliothèques Gateway/CRC inchangées.
Sept paires alternées Release, après la fin des autres compilations ; 16000 entrées
et 112000 livraisons à sept destinations par taille/exécution, sans perte attendue.
Les valeurs de temps concernent le processus hôte, avec assertions et sorties
synthétiques, **pas** le S3, une vraie PSRAM, le Wi-Fi ou des sockets NTRIP réels.

| Taille RTCM | Durée totale avant / après | Variation | p95 d’un lot de 8 avant / après |
| --- | ---: | ---: | ---: |
| 6 | 5.137 / 5.198 ms | +1.20 % | 2.538 / 2.595 µs |
| 38 | 8.145 / 8.231 ms | +1.06 % | 4.083 / 4.120 µs |
| 1029 | 102.779 / 102.599 ms | -0.18 % | 52.453 / 52.155 µs |

Les temps CPU complets et débits calculables sont conservés dans le JSON. Sur ce
banc, les petites trames ajoutent environ 1 % de coût ; le cas maximal ne montre
pas de variation importante. Cette comparaison ne qualifie pas le coût des défauts
de cache PSRAM. Les délais d’attente réseau/ordonnancement et la livraison USB au
Rover ne sont pas simulés comme des mesures de latence physique.

```bash
python3 embedded/esp32/native/tests/compare_routing_storage.py \
  --base 19541b77f8d565d95bb40e250b5acdee798b3c4a \
  --output /tmp/ug-routing-comparison-new --runs 7
```

## Tests exécutés et frontières Linux/ROS2

- **76/76 CTest hôte ASan/UBSan**, dont huit nouveaux scénarios du service réel :
  normal, absence PSRAM, mauvaise région, création de file/tâche refusée, réutilisation
  stable après échec SDK ; référence interne également testée. CRC, saturation,
  source remplacée en cours de traitement, trames fragmentées, timestamp initial,
  stale et sept destinations indépendantes passent.
- **265/265 tests générateur/politique CI**, manifeste et règles inchangés.
- **HTTP et HTTPS S3 : deux builds indépendants chacun**, tests hôte et vérification
  de l’isolation/reproductibilité passent ; ni réseau ni montage des sources ni
  périphérique pendant les builds/tests du SDK, exécution non-root.
- Les 13 composants et huit combinaisons ROS2 gardent les mêmes blobs et modes de
  sources sélectionnées entre la base et l’arbre candidat. Les 14 fichiers natifs
  ajoutés/modifiés sont explicitement hors distributions. Pas de contournement du
  générateur commit-only, pas de nouveau commit de distribution ni build Kilted.
- Les validations Linux/ROS2 précédentes ne sont pas rejouées : leurs sources sont
  inchangées. La CI PR #51 est verte sur la base `19541b7`, pas sur ce lot non poussé.

## Référence physique et campagne proposée — non exécutée

Dernier relevé physique enregistré : application HTTP `9df28836…`, antérieure aux
buffers GNSS compacts, radio désactivée ; interne libre115095, minimum48560,
bloc34816, PSRAM libre8344068 octets, zéro échec. Ce n’est pas un A/B du présent lot.
Pour le candidat, **heap libre/minimum, plus grand bloc, PSRAM effectivement débitée,
HWM des piles, pertes, CPU et latences physiques sont NON MESURÉS**. Les valeurs ne
sont pas extrapolées par addition du gain BSS.

Après revue et autorisation explicite d’application seule : identifier le même S3,
sauvegarder/vérifier app et régions protégées, contrôler offsets/digests, puis comparer
les images HTTP de référence et candidate indiquées dans le JSON. Aucun effacement,
changement de partition, TLS, OTA, identifiants, clé ou paramètre GNSS. Vérifier la
NVS et l’identité après chaque étape ; arrêter au moindre écart non autorisé.

| Palier | Relevés identiques sur les deux images | Prérequis / état |
| --- | --- | --- |
| Boot et radio arrêtée | Interne libre/minimum/bloc, PSRAM libre/consommée, échecs, HWM, services | Flash explicite requis ; aucune écriture de configuration |
| HTTP 1/2/3 clients | Mêmes API read-only, mêmes durées/cycles, p50/p95/max, récupération | Authentification privée, aucun formulaire enregistré |
| Plusieurs publishers / caster | Débit/pertes/CRC, files pleines, fraîcheur et latences, HWM | Scénario synthétique isolé à autoriser ; aucune destination réelle modifiée |
| ESP-NOW canal1 existant | Radio off/on, coûts persistants, mêmes clients et critères mémoire | Activation volatile revue ou autorisation distincte nécessaire ; pas de nouvelle clé/pair |
| Charge GNSS/RTCM | Trames longues/courtes, burst/perte/reconnexion, timing de routage | Plafond opérationnel BASE initial à évaluer : **460800 bauds**, soit 46080 octets/s en 8N1 |

Le plafond n’est imposé ni au moteur commun, ni à la configuration physique/NVS.
Les APIs actuelles de configuration persistent leurs modifications : elles ne
seront pas utilisées pour contourner la contrainte sans changement NVS. En leur
absence, une charge publishers/radio active ne peut pas être annoncée qualifiée.
Aucun paramètre ni périphérique C3 ne doit être modifié. La phase radio sans données
ne prouverait pas le débit RTCM radio ni un RTK FIX. Sous charge, les 96 Kio libres
et 32 Kio contigus restent les seuils normaux ; tout passage sous seuil est déclaré,
jamais transformé en PASS par abaissement des protections.

## Décision et inventaire

**GO logiciel pour revue du candidat ; qualification physique encore requise.**
L’échec historique du bloc actif à 31744 octets n’est pas déclaré résolu. Aucun
nouveau déplacement PSRAM radio, changement du core, NVS ou WebUI n’est effectué.
Le prochain acte est la revue de cette proposition et de sa campagne matérielle,
pas un flash automatique ni une publication de firmware. La conservation Git
autorisée ne clôt pas les critères matériels.

Fichiers de ce lot (les six fichiers locaux hors périmètre sont exclus) :

- `embedded/esp32/components/ug_gateway_service/CMakeLists.txt`
- `embedded/esp32/components/ug_gateway_service/src/ug_gateway_service.cpp`
- `embedded/esp32/components/ug_gateway_service/include/ug_boot_payload.hpp`
- `embedded/esp32/native/tests/CMakeLists.txt`
- `embedded/esp32/native/tests/test_routing_storage.cpp`
- `embedded/esp32/native/tests/routing_stubs/esp_err.h`
- `embedded/esp32/native/tests/routing_stubs/esp_heap_caps.h`
- `embedded/esp32/native/tests/routing_stubs/esp_memory_utils.h`
- `embedded/esp32/native/tests/routing_stubs/esp_timer.h`
- `embedded/esp32/native/tests/routing_stubs/freertos/FreeRTOS.h`
- `embedded/esp32/native/tests/routing_stubs/freertos/queue.h`
- `embedded/esp32/native/tests/routing_stubs/freertos/task.h`
- `embedded/esp32/native/tests/routing_stubs/sdkconfig.h`
- `embedded/esp32/native/tests/compare_routing_storage.py`
- `docs/analysis/ESP32_S3_NATIVE_BUFFERS.md`
- `docs/analysis/ESP32_S3_NATIVE_BUFFERS_MEASUREMENTS.json`
- `TODO.md`
- `ROADMAP.md`
- `.agent/shared/checkpoints/active/UG-ESP32-RADIO-MEMORY.md`
