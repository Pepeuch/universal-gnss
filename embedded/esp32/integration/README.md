# Intégration applicative RTCM UG — référence transitoire S3/C3

Base UG : `134e48b12acec960bba842cb06715847ec960a99`, branche `feat/esp32`.
Ce lot **ADAPT** construit les véritables applications historiques avec le CRC
UG ; ce n'est pas l'architecture définitive des firmwares UG. Aucun client
NTRIP, serveur Web, schéma NVS ou runtime n'est migré ni réécrit dans UG.

La future distribution unique `esp32` conserve les profils `gateway`,
`relay-ntrip` et `rover`. Le futur moteur commun de passerelle permettra BASE
avec GNSS facultatif et relais NTRIP sans GNSS local, sans deux implémentations
NTRIP/routage. Le répéteur radio ESP-NOW reste un autre lot. Les applications
historiques compilées ici ne qualifient pas encore ces profils définitifs.
Le [portail Build & Flash](../README.md#portail-officiel-ug-build--flash--lot-futur)
reste futur, avec revue de licences et véritables builds CI autorisés.

## Sources et périmètre exact

- Historique : `7a1ec69f5e9d07522c0284ec2dbae380f72c7e85`.
- SX126x imbriqué : `a10c5dfdf89788c6ac805e9fe98889de44175aa2`.
- mDNS : 1.5.0, sources déjà suivies à cette référence, hash de composant
  `3d16bbbb5531a04cdc2ec5cc9a961e0e7380710ed4128ce87496daf0d73dda89`.
- ESP-IDF : 5.5.3. Image utilisée :
  `espressif/idf@sha256:8ccd4d2ce413889c6c2bba57e986c670302094efb91c913c6091152e317a7805`.
  GCC Espressif 14.2.0 (`esp-14.2.0_20251107`), CMake 3.30.2.

`prepare.py` ne fetch/checkout rien : il archive les objets Git identifiés,
refuse les entrées non régulières et prépare un nouveau répertoire externe aux
sources UG/historiques. Un seul overlay change six fichiers parmi 291 :

| Fichier historique | Changement |
|---|---|
| `components/rtcm_filter/src/rtcm_filter.c` | Include façade et corps CRC24Q délégué à `ug_rtcm_crc24q` |
| `components/espnow_rtcm/src/espnow_rtcm_codec.c` | Include façade et validation complète déléguée à `ug_rtcm_frame_valid` |
| `components/rtcm_filter/CMakeLists.txt` | Dépendance `ug_rtcm` |
| `components/espnow_rtcm/CMakeLists.txt` | Dépendance `ug_rtcm` |
| `CMakeLists.txt` | Répertoire externe du composant UG |
| `firmware/espnow_rover/CMakeLists.txt` | Répertoire externe du composant UG |

Le moteur compilé est **directement**
`protocols/gnss_protocols/src/rtcm_crc24q.cpp`, sans copie ni fallback.
Le CRC16, le framing/parser, les filtres, le wire format, les files, callbacks,
expirations, réassemblage et sortie USB conservent leurs octets historiques.
Les tests comparent aussi le code hors corps CRC dans les deux fichiers C édités.
Tous les modes des sources archivées sont conservés. `overlay.json` enregistre
les empreintes avant/après, les modes, les cinq entrées UG compilées et les
notices ; `overlay.patch` donne le diff exact des six adaptations.

Les sources mDNS présentes sont vérifiées avec `CHECKSUMS.json`. Ses 24 vecteurs
binaires de fuzzing amont absents du commit historique sont énumérés séparément
(`absent_mdns_upstream_fuzz_vectors`), sans les déclarer vérifiés. Aucun fichier
source de production manquant ou empreinte incohérente n'est accepté.

Le checkout local éventuel `oldrtkesp/` est **strictement local**, à exclure via
`.git/info/exclude` (`/oldrtkesp/`), jamais par ajout au manifeste des distributions.
Ses modifications locales ne sont pas utilisées : seule la référence Git épinglée
est lue. Aucun de ses fichiers ne doit être ajouté à l'index UG.

## Recette sans flash

Utilisateur normal, Python 3.12 et PyYAML ; activer l'environnement ESP-IDF 5.5.3
avant les commandes de compilation. Les dépôts Git historiques et SX126x doivent
contenir les objets épinglés. La recette ne les télécharge pas automatiquement.
Depuis la racine UG, avec `oldrtkesp/` disponible localement :

```bash
python3 embedded/esp32/tests/check_application_overlay.py --historical-repo oldrtkesp
python3 embedded/esp32/integration/prepare.py --historical-repo oldrtkesp --output /tmp/ug-app-a
python3 embedded/esp32/integration/build.py --prepared /tmp/ug-app-a --profile s3-gateway
python3 embedded/esp32/integration/build.py --prepared /tmp/ug-app-a --profile c3-rover
python3 embedded/esp32/integration/verify.py --prepared /tmp/ug-app-a --profile s3-gateway
python3 embedded/esp32/integration/verify.py --prepared /tmp/ug-app-a --profile c3-rover
```

Pour la reproductibilité, refaire préparation et builds dans `/tmp/ug-app-b`,
puis exécuter `verify.py` pour chaque profil avec `--prepared /tmp/ug-app-b
--compare-with /tmp/ug-app-a`. Cette vérification compare les entrées, toutes les
images `.bin` et l'ELF final ; elle échoue sur une différence. Les fichiers de
configuration C3 comportent un chemin absolu de table propre à chaque répertoire,
mais les tables binaires et les ELF sont identiques.

Les quatre builds de qualification ont été exécutés dans le conteneur IDF
ci-dessus, UID/GID 1000, réseau `none`, sources montées en lecture seule, sans
périphériques exposés, sans socket Docker ni accès aux cartes. Dans un
conteneur, fournir les chemins visibles dans celui-ci, par exemple `/historical`
pour la référence et `/ug/embedded/esp32/integration/...` pour les scripts.
Ne pas exécuter les instructions de flash affichées automatiquement par IDF.

Configurations de **compilation seulement** : canal fixe 1, pairs fictifs
`02:00:00:00:00:01` / `02:00:00:00:00:02`, aucune MAC de banc versionnée.
S3 : Waveshare Ethernet W5500, BASE ESP-NOW, LoRa désactivé, flash 16 Mo,
PSRAM octale 8 Mo, table historique (slots OTA de 3 Mio et SPIFFS Web 9 Mio).
Le firmware historique BASE conserve ses services/initialisations GNSS ; ce
build ne démontre pas encore le futur mode relais sans GNSS.
C3 : application rover minimale, flash 4 Mo, table OTA historique de référence
(slots 1280 Kio, NVS 20 Kio), USB Serial/JTAG ; consoles/logs applicatifs et
bootloader désactivés. Les octets ROM au reset restent une limite matérielle.
Aucune de ces tables n'est une autorisation de flash ou de repartitionnement.

## Preuves logicielles rejouées

| Contrôle | Résultat |
|---|---|
| Façade C/C++17 UG, ASan/UBSan | 1 test, 1024 tailles de payload et cas invalides : PASS |
| Historique avec overlay commun | 12 groupes C et 11 tests Python inchangés : PASS |
| TCP synthétique → vrai codec C → radio simulée → PTY | PASS, inclus dans les 11 tests Python historiques |
| Recette épinglée et contrôles négatifs | 11 tests : PASS, y compris sur la copie locale `oldrtkesp/` |
| CTest natif UG | 68/68 |
| Générateur et politique CI | 200/200 |
| Python existant | 55/55 |
| Frontières ROS2 | 2 tests : huit payloads/inventaires comparés, voisin non classé rejeté |
| Applications IDF réelles | S3 Gateway et C3 Rover : PASS, chacun construit deux fois |
| Reproductibilité | ELF et toutes images binaires identiques entre deux racines indépendantes |

`verify.py` contrôle `compile_commands.json`, les symboles `nm`, les relocations
`objdump`, la map et les configurations effectives. Un seul calcul UG est lié
par ELF. Les objets historiques référencent la façade ; celle-ci référence le
CRC UG. Sur C3 les entrées CRC brut inutilisées sont éliminées par le linker,
mais `ug_rtcm_frame_valid`, `ValidateRtcmCrc24Q` et `ComputeRtcmCrc24Q` sont liés.
Le CRC16 historique est présent. L'archive UG n'a ni dépendance d'allocation ni
RAM statique. La preuve détaillée est générée dans `verification.json` et les
fichiers `*.relocations.txt` de chaque build.

| Budget | S3 Gateway | C3 Rover |
|---|---:|---:|
| Application `.bin` | 1 077 504 octets | 732 560 octets |
| Slot OTA | 3 145 728 octets | 1 310 720 octets |
| Marge du slot | 2 068 224 octets | 578 160 octets |
| DIRAM utilisée (code + données, rapport IDF) | 164 071 / 341 760 | 126 528 / 321 296 |
| Données + BSS statiques | 67 404 octets | 42 484 octets |
| Code UG lié | 257 octets | 232 octets |
| RAM statique UG | 0 | 0 |
| Pile `app_main` configurée | 3584 octets | 4096 octets |
| Pile worker ESP-NOW historique configurée | 4096 octets | 4096 octets |

L'IRAM dédiée S3 compte aussi 16 384 octets dans le rapport IDF. La DIRAM restante
(177 689 / 194 768 octets) **n'est pas** une mesure de heap minimum à l'exécution.
Aucun high-water mark de pile, heap sous charge, débit RF ou comportement USB
physique nouveau n'est mesuré dans ce lot.

SHA-256 des applications reproduites :

- S3 : `19452d9d1c0fca90a7c20ac5613b61c33b47eaf9dcfc60c82533c30eebc3c5bc`.
- C3 : `f516adb46fe9abba9f9366645f5460a9de5579045d973af339de745fa9b9b0aa`.

## Licences et limites de qualification

Notices conservées dans l'archive : GPLv3 du dépôt historique (notices GPLv3 ou
ultérieure par fichier), Apache 2.0 de W5500 et mDNS, Clear BSD Semtech du
sous-module SX126x (dont réserve sur les droits de brevet). UG reste LGPLv3.
Le SDK IDF et ses composants/fournisseurs restent dans l'image identifiée avec
leurs notices propres. Aucun code historique/fournisseur ni binaire combiné
n'est ajouté à l'arbre versionné UG. L'inventaire de licences n'est pas une
approbation juridique de publication ; la distribution future des binaires liés
et le portail devront respecter les obligations applicables et revoir leurs
assets/notices avant publication.

Les builds sont hors réseau avec les dépendances déjà présentes, pas une preuve
de leur disponibilité future. La reproduction est démontrée avec le même SDK,
toolchain et environnement identifiés ; elle ne promet pas l'identité entre
versions de compilateur. Les avertissements historiques IDF/compilation sont
conservés dans les logs, sans adaptation opportuniste des sources.

Les fichiers ROS2/Linux, ROSIDL, manifestes, versions et workflows sont inchangés.
Les huit plans ROS2 restent identiques et excluent l'ESP32 ; aucun nouveau build
ROS2 complet n'est nécessaire/rejoué pour cet overlay exclu. Aucun flash, essai
RF, endurance, portée, C6, nouveau backend, publication ou commit dans ce lot.
Les preuves physiques nominales concernent le firmware historique, pas ces
nouveaux binaires. **GO pour revue logicielle**, qualification matérielle de ces
binaires différée ; aucune migration NTRIP/WebUI/NVS/runtime n'est engagée.
