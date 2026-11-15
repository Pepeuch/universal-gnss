# ESP32-S3 : buffers ESP-NOW conditionnels — phase 3

## Références et limites

Base : `6f4d4768015b9ce7e18d34e3bde5d15059526c07`, phase satellites qualifiée.
Les deux commits de phase 2, dont `4dc2f83eff199e805ce5d48f985d9fb0244c2c7e`,
ont été poussés normalement sur `perf/esp32-s3-memory-followup` ; SHA distant
vérifié. Aucune PR ni fusion n'est effectuée par cet agent. La PR #41 cible `dev` ;
son head observé au contrôle final est `ef3ca2410486e610103f1e5bba393448cfb85fdf`,
distinct du SHA publié ici. Les résultats CI ci-dessous concernent précisément
`6f4d476`, pas automatiquement le head plus récent de la PR.
Au premier contrôle après push, aucune exécution n'était présente (les pushes
feature seuls ne déclenchent pas la CI). Au contrôle final, **sept workflows
pull_request pour ce SHA sont terminés avec succès** : Backlog, Format,
Distribution, CMake, Lyrical, Docker et ESP32 native. Exécution native :
[43754549056](https://github.com/Pepeuch/universal-gnss/actions/runs/43754549056).
Ce succès concerne la phase 2 publiée, pas les fichiers expérimentaux phase 3.

Expérience séparée : `perf/esp32-s3-espnow-memory`. S3 BASE uniquement.
Aucun changement du moteur partagé, des profils C3, du protocole, des dimensions
de buffers RTCM, des piles ou des seuils mémoire. Les sources C communes sont
modifiées sous garde S3 BASE ; le chemin BASE non-S3 est comparé après
prétraitement aux mêmes stubs SDK, tokens identiques à la référence. Cela ne
constitue pas une compilation/qualification C3, volontairement non rejouée.

Les résultats matériels radio désactivée ne qualifient ni une émission ESP-NOW,
ni GNSS/RTCM physique, ni l'endurance. L'état de référence n'a pas de pair/clé
radio configurés. Aucune activation physique ni émission de test n'est faite
sans scénario contrôlé ; ne pas confondre les transitions simulées avec du RF.

## Audit de propriété et de mémoire

ELF/MAP Xtensa exact de la phase 2 : `s_queue` 8328, `s_stream` et `s_radio`
2216 chacun, `s_message` 1040, tous dans `.dram0.bss`, **13800 octets** au total.
Les codecs/framing et CRC UG sont inchangés. Le mailbox garde ses huit slots,
la taille des messages et ses petits curseurs ; ce n'est pas un protocole réseau.

| Stockage / contrôle | Propriétaire et accès |
| --- | --- |
| `s_message`, `s_radio` | Worker radio unique, message copié sous `s_lock`, fragmentation hors verrou |
| `s_queue` | Producteur routage/NTRIP et worker ; copies bornées sous `s_lock` |
| `s_stream` | Propriétaire NTRIP historique unique ; cette API n'est pas le routage natif UG courant |
| `s_lock`, état, compteurs, epoch/halt | Contrôle permanent interne, indépendant du gros stockage |
| callback `sent` | Tâche Wi-Fi : statut ACK et sémaphore, ne garde aucun pointeur vers les payloads |
| callbacks/propriétaires après échec partiel | Peuvent rester vivants ; leur stockage ne doit jamais être libéré/recréé |

L'API du SDK épinglé indique que le buffer `esp_now_send` n'a pas à rester valide
après le retour ; son callback s'exécute dans une tâche Wi-Fi prioritaire. Les
copies de queue s'effectuent sous section critique. Pour cette première variante,
**le stockage reste interne, `MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT`**, sans
repli PSRAM et sans déplacer les données DMA du pilote. Les buffers réellement
transmis au SDK et les piles gardent leurs contrats antérieurs. Une affectation
PSRAM n'est pas considérée sûre sur la seule absence d'ISR dans le callback.

## Cycle de vie implémenté

- Radio inutilisée au boot : aucun gros stockage alloué. Stats et helpers
  stop/invalidation/reset/response avant activation restent sûrs et n'allouent pas.
- Premier démarrage : réservation exclusive du démarrage ; une tentative
  d'allocation, zéro-initialisation hors section critique, publication protégée,
  initialisation des codecs, puis seulement Wi-Fi, tâches et callbacks.
- Le contrôle de publication garde un verrou interne dédié, ordre **verrou du
  driver → verrou de publication**. L'ordre inverse n'existe pas ; le getter
  relâche son verrou avant de rendre le pointeur. Aucun allocateur ni gros
  memset n'est exécuté sous ces verrous.
- Le SDK/toolchain Xtensa n'annonce pas de pointeurs atomiques toujours sans
  verrou. Une première assertion de build a détecté cette limite ; elle n'a
  pas été contournée. La publication utilise donc le verrou explicite ci-dessus.
- Échec d'allocation : `ESP_ERR_NO_MEM` avant toute opération Wi-Fi/ESP-NOW.
  Pas de repli PSRAM ni de boucle de réessai ou d'activation partielle.
- Une tentative de démarrage, même échouée ensuite dans le SDK, interdit la
  reconfiguration/réinitialisation du même driver jusqu'au reboot. Aucun worker
  résiduel ne peut lire des codecs remis à zéro par un second start.
- Dès qu'il existe, le bloc est **conservé jusqu'au redémarrage**, même après
  désactivation ou erreur SDK. Il n'est ni emprunté à une pile ni rendu au tas.
  Les callbacks n'accèdent jamais à des données libérées.
- Les garde-fous d'ACK tardif, arrêt TX permanent, incarnation/epoch, débordement
  d'epoch et expiration à une seconde restent actifs. Ni désactivation ni ACK
  tardif ne relâche une quarantaine. Aucun nouveau mode de reconnexion/activation
  à chaud n'est ajouté : le service natif conserve son démarrage au boot.

**Le gain est au repos seulement.** À l'activation, les 13800 octets sont
réservés sur le tas interne, avec son surcoût, puis viennent Wi-Fi, sémaphore,
pile worker 4096 et ressources SDK. Après une première activation ou un échec
SDK, désactiver la radio ne rend pas cette réservation. La taille BSS retirée
ne doit jamais être annoncée comme une économie en fonctionnement radio actif.

## Qualification logicielle

- **60/60 CTest ASan/UBSan**, suites précédentes conservées.
- Le test de cycle de vie compile le vrai driver C et les codecs UG avec des
  frontières SDK/RTOS simulées : nominal, OOM, échec Wi-Fi, échec d'enregistrement
  callback après création du worker, ACK tardif, arrêt avant start, expiration
  des messages et débordement d'epoch. Aucune radio physique n'est utilisée.
- Thread hôte réel : deux réservations concurrentes, une seule allocation,
  aucune publication avant fin de l'initialisation, données stables ensuite.
  Ce test ne prétend pas simuler l'ordonnancement Wi-Fi réel ni tous ses interleavings.
- Codec/CRC24Q, CRC16, corpus de mutations et pont TCP/PTY existants restent
  exécutés. Aucun changement de protocole ou de taille n'est introduit.
- SDK ESP-IDF 6.1 épinglé, deux reconstructions indépendantes identiques,
  60 tests hôtes et suites QuickJS/flasheur/portable/capture conservées.
  Arbre : `836843d72146f12fa00f393a00628fc8932185c2`.
- Application **1471296 octets**, +624 face à la phase 2. SHA-256 :
  `416f43cc6dfd0537e37688c90ff5fdbc1d580680a969e049e06e51e54ff82249`.
- Données statiques internes : data **23725 → 23757**, BSS **96048 → 82256**,
  noinit 3 inchangé, soit **13760 octets statiques en moins** après le nouveau
  contrôle. Les quatre symboles volumineux ne sont plus dans le BSS du driver ;
  slot de publication 16 octets et verrou 8 octets restent internes.

## Mesures physiques — radio désactivée

S3 réel identifié par ROM, app phase 2 et sauvegarde NVS/table/OTA vérifiées.
Écriture de la seule application qualifiée à `0x20000`, puis comparaison des
`0x9000` octets à partir de `0x8000`, identiques avant/après. Pas de modification
de partition, de provisioning TLS, de clé ou de configuration utilisateur.

Pour chaque image : même helper de boot et lectures de maintenance, délai de
cinq secondes puis premier GET HTTPS ; 1/2/3 sessions TLS persistantes, huit
tours par connexion ; 72 GET status/config et récupération. Les relevés console
après cette suite interviennent à 67,97 / 68,00 secondes d'uptime : pas d'écart
important de repos initial. Le premier GET n'est pas une mesure sans TLS.
Chaque cellule donne **libre interne / plus grand bloc**, octets.

| Scénario | Phase 2 | Variante, radio désactivée |
| --- | ---: | ---: |
| Premier GET | 88383 / 34816 | 102143 / 34816 |
| 1 client(s) | 88363 / 34816 | 102123 / 34816 |
| 2 client(s) | 87935 / 34816 | 101635 / 34816 |
| 3 client(s) | 87423 / 34816 | 100615 / 34816 |
| Après 72 GET | 88119 / 34816 | 102119 / 34816 |

Le gain au premier GET est **13760 octets**. Le plus grand bloc est conservé
à **34816 octets**, au-dessus des 32 Kio requis. La pression passe de `reduced`
à `normal` sur les séries échantillonnées, sans modifier les seuils. Ce n'est
pas une garantie de pression normale sous toute charge future.
PSRAM libre identique : 8331132 au premier GET et 8280748 au minimum sous trois
clients. Aucun déplacement PSRAM n'explique ce gain interne.

Après les cycles, relevés console sans session HTTPS :

| Tas | Libre phase 2 → variante | Plus grand bloc | Minimum historique |
| --- | ---: | ---: | ---: |
| Interne | 88979 → 102707 | 34816 → 34816 | 47140 → 60900 |
| DMA | 81191 → 94919 | 34816 → 34816 | 39352 → 53112 |
| PSRAM | 8356184 → 8356184 | 8257536 → 8257536 | 8256156 → 8257160 |

DMA est une capacité incluse dans l'interne, pas une quantité à lui additionner.
Les minima sont historiques par région et ne remplacent pas l'état d'admission.
Blocs internes après protocole/cycles : phase 2, 296 → 295 alloués et 11 → 10
libres ; variante, 297 → 296 alloués et 10 → 10 libres. Aucun échec réel
`heap_caps` enregistré. Ces valeurs multi-régions ne forment pas à elles seules
un indice de fragmentation ; aucun déclin progressif n'est observé ici.

### HTTPS, CPU et cycles

72 GET : médiane **63,01 → 62,81 ms**, maximum **86,21 → 88,28 ms**.
Trois séries CPU de 24 GET : **39,52 / 39,54 / 39,41 %** de capacité totale
pour la phase 2, **40,34 / 39,72 / 39,97 %** pour la variante. Aucun gain CPU
ni significativité statistique des petits écarts n'est revendiqué.

Dix cycles login/logout/onglets/rafraîchissements desktop/mobile : **133,00 →
129,87 secondes**, zéro erreur JavaScript et zéro POST. La variante effectue
290 GET API contre 280 : l'accès satellites supplémentaire devient admissible
avec la pression normale ; aucune donnée GNSS n'est inventée.
Médianes API navigateur **74,49 → 74,24 ms**, maxima **2246,01 → 2175,01 ms**.
La PSRAM de récupération est constante à 8331132 dans les deux cas ; le libre
interne reste 88279–88303 contre 102019–102043, bloc maximal constant 34816.
Ces dix cycles ne constituent pas une endurance ni une mesure de latence RTCM.

## Radio active — essai reporté par le mainteneur

Le mainteneur a autorisé un essai canal 1 sans données ESP-NOW applicatives,
sans RTCM, sans nouveau pair et sans modification de clé existante.
La lecture authentifiée actuelle confirme **aucun pair, aucune clé**, radio
configurée désactivée, driver désactivé et canal configuré 1. La configuration
logique correspond exactement à la sauvegarde initiale. GNSS et NTRIP entrant
sont désactivés ; compteurs RTCM, trames ESP-NOW envoyées et ACK à zéro.

Le validateur `ug_settings.cpp::ValidateSettings` et le pilote
`espnow_rtcm_configure_secure` exigent un pair unicast explicite et une clé
privée valide. L'activer dans cet état nécessiterait de créer un appairage,
ce que les conditions interdisent. **Aucune tentative d'activation ou écriture
NVS n'est donc faite**, aucune nouvelle clé/association créée. Pas de mode
non sécurisé ajouté pour contourner ce blocage.

Le mainteneur a confirmé que le S3 n'a jamais été appairé et qu'aucune
configuration privée d'association n'existe. Il a choisi de reporter l'essai
actif jusqu'à un véritable appairage, sans générer de clé ni créer de faux pair.
Les paramètres logiques sont restés identiques. La restauration matérielle de
fin d'essai remet uniquement l'application de référence de phase 2 ; NVS, TLS
et partitionnement sont préservés. Les valeurs et le fonctionnement sont
vérifiés après redémarrage ; on ne suppose pas l'identité binaire d'une NVS
qui aurait été modifiée.
L'absence de données applicatives ne doit pas être assimilée au silence RF :
une activation Wi-Fi peut produire son propre trafic de gestion.

| Qualification | Résultat |
| --- | --- |
| Variante radio désactivée, 1/2/3 clients | PASS, mesures ci-dessus |
| Réservation/échecs/transitions/ACK, frontières SDK simulées | PASS, ne qualifie pas le RF |
| Variante radio active sur matériel | **REPORTÉE : S3 jamais appairé, décision du mainteneur** |
| Surcoût Wi-Fi/worker et marge active sous 3 clients | **NON MESURÉS** |
| Débit/délivrance GNSS/RTCM/ESP-NOW réel | Non qualifiés, hors de ces mesures |

À l'activation, le bloc seul retirerait au moins 13800 octets au tas interne
mesuré, avant son arrondi et les ressources Wi-Fi/worker. Le libre de 102143
n'est donc **pas** un budget radio actif : sa borne théorique avant ces autres
coûts serait 88343 octets, non une mesure. Le bloc contigu actif peut aussi
changer ; son maintien à 32 Kio n'est pas prouvé dans ce scénario manquant.

**Verdict : GO pour un commit distinct de l'optimisation qualifiée radio
désactivée et des tests simulés. La qualification matérielle radio active est
HARDWARE_PENDING, reportée explicitement par le mainteneur.** Le gain au repos
ne vaut pas qualification du fonctionnement ou de la marge mémoire radio active.
Un véritable appairage et une campagne matérielle dédiée seront nécessaires
ultérieurement. Les seuils, le moteur partagé, les profils C3, les piles et les
buffers RTCM ne sont pas modifiés. Endurance et GNSS/RTCM physique restent séparés.


## Restauration finale vérifiée

L'application de référence de phase 2 est remise sur `ota_0` à `0x20000` :
**1470672 octets**, SHA-256
`477093f1e4716501dd7ccdf471adb2b36088411ce9a9a2a1eabe1e8ffa7c682f`.
Le digest du firmware expérimental est vérifié avant écriture, celui de la
référence après écriture. Table, NVS et OTA sont comparées avant/après, sans
écriture de ces régions. Aucun effacement global, clé, pair ou paramètre créé.

Un premier contrôle USB à délai fixe n'a pas obtenu toutes ses réponses ; il
n'est pas présenté comme réussi. Les commandes ont ensuite été vérifiées avec
attente de terminaison, puis un nouveau redémarrage a confirmé : console USB,
HTTPS `running`, TLS prête, administrateur conservé et génération 1.
GET WebUI/ressources/statut/configuration/appairage : 200 avec certificat vérifié ;
configuration logique exactement identique à la sauvegarde et accès anonyme à
la configuration refusé 401. ESP-NOW est désactivé, sans pair ni clé, compteurs
de trames RTCM/ESP-NOW et ACK à zéro. Le S3 est ainsi laissé sur sa référence,
pas sur la variante mesurée. Les résultats de gain décrivent cette variante
et ne sont pas la mémoire du firmware de référence remis en service.

## Livraison du lot qualifié radio désactivée

Les fixtures hôtes emploient des pairs et clés synthétiques uniquement dans les
stubs. Ces valeurs ne sont jamais provisionnées sur le S3. Les sauvegardes,
traces, ELF/MAP, identifiants et clés privées restent hors dépôt.
Le code et ses preuves sont préparés dans un commit local distinct, sans push.
Les quatre fichiers préexistants sont préservés. Sauvegardes, clés, traces et
checkpoints privés sont explicitement exclus de l'index.

Le périmètre logiciel initial compte 20 fichiers ; les quatre guides de suivi
sont également actualisés : `TODO.md`, `ROADMAP.md`, `embedded/esp32/README.md`
et `embedded/esp32/native/tests/README.md`.

Périmètre logiciel et rapport :

- `docs/analysis/ESP32_S3_ESPNOW_MEMORY.md`
- `embedded/esp32/components/espnow_rtcm/CMakeLists.txt`
- `embedded/esp32/components/espnow_rtcm/include/espnow_rtcm_storage.h`
- `embedded/esp32/components/espnow_rtcm/src/espnow_rtcm.c`
- `embedded/esp32/components/espnow_rtcm/src/espnow_rtcm_storage.c`
- `embedded/esp32/native/tests/CMakeLists.txt`
- `embedded/esp32/native/tests/espnow_driver_stubs/driver/usb_serial_jtag.h`
- `embedded/esp32/native/tests/espnow_driver_stubs/esp_heap_caps.h`
- `embedded/esp32/native/tests/espnow_driver_stubs/esp_log.h`
- `embedded/esp32/native/tests/espnow_driver_stubs/esp_now.h`
- `embedded/esp32/native/tests/espnow_driver_stubs/esp_radio_test_sdk.h`
- `embedded/esp32/native/tests/espnow_driver_stubs/esp_random.h`
- `embedded/esp32/native/tests/espnow_driver_stubs/esp_timer.h`
- `embedded/esp32/native/tests/espnow_driver_stubs/esp_wifi.h`
- `embedded/esp32/native/tests/espnow_driver_stubs/freertos/FreeRTOS.h`
- `embedded/esp32/native/tests/espnow_driver_stubs/freertos/semphr.h`
- `embedded/esp32/native/tests/espnow_driver_stubs/freertos/task.h`
- `embedded/esp32/native/tests/espnow_driver_stubs/sdkconfig.h`
- `embedded/esp32/native/tests/test_espnow_lifecycle.c`
- `embedded/esp32/native/tests/test_espnow_storage_concurrent.c`
