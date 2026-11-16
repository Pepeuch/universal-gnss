# ESP32-S3 : qualification WebUI et premières allocations PSRAM

## Périmètre et référence physique — 15 novembre 2026

Branche de travail `perf/esp32-s3-memory`, basée sur la WebUI
`21570ae87028a77e199903f17455629644b9c0a0` et la réparation HTTPS
`59f53a54c27bca9af6df48956c4259ed439dbb19`. Les modifications mémoire
restent distinctes de ces deux commits. Ce rapport ne qualifie pas de release.

Matériel identifié par ROM : Waveshare ESP32-S3 W5500, révision S3 v0.2,
flash 16 Mio et PSRAM 8 Mio. Ethernet W5500, Wi-Fi non associé ; aucun récepteur
GNSS raccordé. Hôte Linux, transport USB Serial/JTAG par identifiant stable,
Chromium/Playwright 1.56.0 pour les essais d'interface. C3 et C6 non utilisés.

SDK épinglé : ESP-IDF 6.1,
`espressif/idf@sha256:81893c71bb5e570088901f21def8684c25cd2a9020281bd01b843a7655edb18c`.
Compilation et tests en UID 1000, sans sources hôtes montées, sans périphériques,
réseau déconnecté après provisionnement. Chaque nouvelle variante a été reconstruite
deux fois indépendamment, à l'identique.

Avant chaque écriture : vérification du binaire installé, de la carte, du bootloader,
de la table et des données sauvegardées. Écriture de la seule application active
`ota_0` à `0x20000`, sans modifier les partitions ni les données NVS/OTA. Les
`0x9000` octets à partir de `0x8000` sont comparés avant/après. Sauvegardes et
matériel d'authentification restent dans un répertoire privé hors dépôt.

## Modifications et frontières

- Instrumentation S3 : tas interne/DMA/PSRAM, blocs, minima historiques, tâches,
  piles minimales, compteurs CPU, temps et mémoire échantillonnée par famille API.
  Les identifiants de tâche ne déréférencent pas de nom TCB emprunté après snapshot.
- `memory trace` active au plus cinq secondes une trace de 1024 entrées en PSRAM
  (57 344 octets), arrêtée automatiquement ; `memory trace-stop` affiche les tailles
  et adresses d'appel, jamais le contenu. Les allocations ISR sont exclues par
  le SDK pour ce buffer externe. Une trace saturée ne constitue pas une preuve
  complète ; les quatre niveaux d'appel limitent l'attribution.
- Tampon de ligne USB de 8224 octets : allocation explicite PSRAM, propriétaire
  console unique, effacement avant libération, échec sans repli en mémoire interne.
- cJSON : hooks initiaux spécifiques S3, allocations explicitement en PSRAM,
  sans repli interne. Un préfixe aligné conserve la taille demandée ; libération
  avec effacement de toute la zone possédée et de ses métadonnées, sans toucher
  aux canaris/padding de l'allocateur. Débordement `SIZE_MAX` rejeté.
- Une allocation JSON refusée pendant une réponse interdit l'envoi d'un objet
  partiel : réponse 503. Les réglages encodés conservent aussi leur vérification
  de construction complète. Les erreurs ne divulguent aucun contenu sensible.
- Console locale uniquement : `memory json-fail N` injecte un seul refus après
  N allocations (0–4096), puis se désarme ; `-1` désarme explicitement. Aucun
  état NVS, aucune API distante de test. Le redémarrage désarme l'injection.
- `memory json-large` construit et détruit des documents synthétiques bornés
  (0/16/64 lignes, plafond 32 Kio), sans créer d'observation GNSS ni changer
  la configuration. Aucune levée des protections des API détaillées.
- WebUI : le service réel est lu sous `ethernet_driver` ; la carte conserve
  l'identifiant d'interface `ethernet`. Les fixtures et assertions couvrent cet état.

Les piles (dont démarrage 32 Kio), les buffers DMA, GNSS/RTCM et les priorités
restent en place. Les protections 96/32 Kio et 48/16 Kio sont inchangées,
comme TLS, NVS, partitionnement, protocoles, moteur partagé et ROS2/Linux.
L'option globale déplaçant le BSS externe a été rejetée : elle déplacerait aussi
les BSS lwIP/radio du SDK. Aucun ajustement du relais C3.

## Comparaison contrôlée des quatre firmwares

La référence est le binaire WebUI validé, non instrumenté. Les trois autres arbres
partagent exactement le même code et ne diffèrent que par les deux commutateurs
`UG_S3_CONSOLE_PSRAM` / `UG_S3_JSON_PSRAM` (00, 10, 11).

| Variante | Arbre source | Application (octets) | SHA-256 binaire |
| --- | --- | ---: | --- |
| Référence | commit `21570ae87028a77e199903f17455629644b9c0a0` | 1461552 | `ad3d398ab6f6a26828ec279c292fcd1ac9c66a139e806892a390aa33f8d98719` |
| Instrumentation seule | `0520ca8ee5cceac9ccdc48f679adbb2af8aa9280` | 1468144 | `5f82f17fb232af51940eb4be3d6d6a6f2c78aa779c9c73fa800dd255a9585d3e` |
| USB PSRAM | `24c462bad7bba855187b54b02895f8a01dbe738d` | 1468384 | `865e5dcfd02852dad0b7a92cacb2098157980502fe4159ef4b5a0e88e0a1128e` |
| USB + JSON PSRAM | `36128b41055b6e8e4d134605f9b674da88b60e52` | 1469232 | `5f2216881bf80467e90a1357b100216563d20e99e85268ad1d48952bf16a83c0` |

Procédure commune, sans navigateur ni autre client concurrent : redémarrage et
contrôle HTTPS/TLS/administrateur ; attente de cinq secondes ; première lecture
HTTPS `/api/resources` ; une, deux puis trois connexions TLS persistantes,
huit tours par connexion et 200 ms entre tours, quatre secondes entre séries ;
72 lectures alternées `/api/status` et `/api/config` sur une connexion ; quatre
secondes puis lecture de récupération. Certificat vérifié et identité épinglée.
Aucun POST. Configuration comparée exactement à la sauvegarde avant essai.
Le relevé « démarrage » est **la première requête HTTPS**, pas un tas sans TLS.
Les clients restent connectés simultanément ; les requêtes sont séquencées.

Chaque cellule mémoire indique **libre interne / plus grand bloc**, en octets.
Les séries HTTPS indiquent les minima observés aux points `/api/resources`,
non un enregistrement de chaque allocation ou du pic transitoire de handshake.

| Variante | Démarrage | HTTPS 1 client | HTTPS 2 clients | HTTPS 3 clients | Après 72 requêtes |
| --- | ---: | ---: | ---: | ---: | ---: |
| Référence | 71327 / 34816 | 71227 / 34816 | 70695 / 34816 | 69687 / 34816 | 71307 / 34816 |
| Instrumentation | 68319 / 34816 | 68299 / 34816 | 67691 / 34816 | 66927 / 34816 | 68295 / 34816 |
| USB PSRAM | 76535 / 32768 | 76511 / 32768 | 76107 / 32768 | 75555 / 31744 | 76535 / 32768 |
| USB + JSON PSRAM | 76539 / 32768 | 76295 / 32768 | 75951 / 32768 | 75439 / 31744 | 76515 / 32768 |

L'instrumentation coûte environ 3 Kio internes. Le tampon USB libère 8216 octets
au premier GET face à l'instrumentation seule ; le gain net du firmware final
face à la référence est 5212 octets au même point, 5752 avec trois clients.
Le plus grand bloc **ne progresse pas** : il baisse de 2048 octets au premier
GET et de 3072 avec trois clients. Le gain total ne supprime donc pas la
fragmentation ni la pression mémoire `reduced`, observée dans toutes les variantes.

PSRAM libre au premier GET : référence/instrumentation 8351620 octets,
USB et USB+JSON 8343168 ; avec trois clients : 8301236 et 8292784 respectivement.
Le JSON est temporaire : pas de gain interne supplémentaire au repos démontré
par l'ajout de ses hooks. Les traces isolées préliminaires sans saturation ont
attribué 301 allocations JSON, 12195 octets demandés cumulés : 7109 internes et
5086 externes avant déplacement, tous externes après. Ces volumes ne sont ni
un gain vivant permanent, ni une preuve d'absence de fuite ; le préfixe final
ajoute ses métadonnées. Les traces mixtes saturées sont exclues de cette preuve.

Minimum historique interne remonté : 21940 / 18884 / 39828 / 39828 octets.
Il s'agit d'une somme des minima par tas du SDK, pas nécessairement d'un minimum
simultané global. Il n'est pas interchangeable avec le libre actuel ou le plus
grand bloc utilisé pour l'admission des opérations.

| Variante | Médiane des 72 GET (ms) | Maximum (ms) |
| --- | ---: | ---: |
| Référence | 60,97 | 87,67 |
| Instrumentation | 62,64 | 84,11 |
| USB PSRAM | 63,87 | 86,55 |
| USB + JSON PSRAM | 63,82 | 85,37 |

Aucune erreur sur les 192 lectures de ressources et 288 lectures status/config
de cette comparaison. Les temps incluent le client, le réseau et HTTPS ; ce ne
sont pas des latences GNSS/RTCM. Les mesures CPU préliminaires comparables des
firmwares instrumentés sont proches de 40 % de capacité totale des deux cœurs,
sans gain CPU significatif démontré. Aucun essai sous flux GNSS/RTCM physique.

## Essais dégradés et validation restante

Dix injections physiques : `/api/status` et `/api/config`, budgets 0/1/5/20/100.
Chaque refus d'allocation a donné 503, puis 200 après désarmement, configuration
inchangée. Documents synthétiques 0/16/64 lignes : succès, refus injecté au
budget 200, puis succès de récupération. Ce test ne coupe pas la PSRAM et ne
reproduit pas une panne électrique ou un épuisement de tous les tas.

Les tests hôtes ASan/UBSan vérifient les limites d'allocation, la précision des
nombres, UTF-8/échappement, les scénarios de panne, l'absence de blocs JSON
restants et l'effacement des zones possédées avec canaris intacts. La fixture
Chromium comporte également une grande réponse status explicitement synthétique.
Une grande télémétrie GNSS réelle n'est pas qualifiée sans récepteur et sans
lever les seuils qui protègent les API détaillées.

Le document synthétique de 64 lignes atteint 14763 octets sur appareil
(0 ligne : 43 ; 16 lignes : 3682). Après les onze refus injectés, le compteur
JSON vaut 11 et le compteur des échecs réels du tas vaut 0. La reprise ne
conserve aucun réglage de test.

## Cycles, trace finale et redémarrage

Dix contextes Chromium indépendants, alternant 1280 et 390 px : connexion,
carte Ethernet réellement `running`, six onglets, deux rafraîchissements explicites,
déconnexion, fermeture, quatre secondes puis nouvelle session TLS de mesure.
Tous les POST sont interdits. Durée réelle : **134,89 secondes** ; 280 requêtes
API observées, aucune erreur JavaScript et aucune écriture de configuration.
Les refus 503 des détails sous pression sont attendus, pas ignorés.

| Cycle | Libre interne | Plus grand bloc | Libre PSRAM |
| --- | ---: | ---: | ---: |
| 1 | 74335 | 31744 | 8343168 |
| 2 | 74339 | 31744 | 8343168 |
| 3 | 74335 | 31744 | 8343168 |
| 4 | 74339 | 31744 | 8343168 |
| 5 | 74335 | 31744 | 8343168 |
| 6 | 74547 | 31744 | 8343168 |
| 7 | 75819 | 31744 | 8343168 |
| 8 | 76455 | 31744 | 8343168 |
| 9 | 76455 | 31744 | 8343168 |
| 10 | 76455 | 32768 | 8343168 |

Pas de dérive décroissante observée sur cette fenêtre courte, PSRAM récupérée
exactement. Ce n'est pas une preuve d'absence de fuite à long terme. Temps API
navigateur médian 72,99 ms, maximum 2239,36 ms ; aucune expiration au délai
client de 10 s. Ces temps d'interface ne qualifient pas la latence GNSS.

Trace du firmware final : 577 entrées TLS et 549 entrées pendant la requête
JSON isolée, aucune saturation. Les 301 allocations attribuées à cJSON représentent
14603 octets cumulés, préfixes inclus, tous en PSRAM et tous libérés à la fin
de la requête. Les autres catégories et allocations ISR ne permettent pas
une comptabilité exhaustive du système. Les volumes TLS attribués restent en
partie internes ; aucune modification de son allocateur n'a été faite.

Trois séries finales de 24 GET donnent 39,48 / 39,13 / 39,69 % de capacité
CPU totale des deux cœurs (compteurs idle/runtime), avec médianes HTTP de
59,01 / 58,88 / 60,37 ms. Pas de gain CPU significatif revendiqué. La médiane
commune de 72 GET reste environ 2,85 ms plus élevée que la référence ; une seule
série ne permet pas de conclure sur sa variabilité ou sa significativité.

Redémarrage de maintenance final : démarrage propre, HTTPS `running`, erreur 0,
identité TLS prête, administrateur présent, génération de configuration 1.
Certificat vérifié, `/`, `/api/resources`, `/api/status` et `/api/config` répondent
200 ; configuration identique à la sauvegarde et absence d'authentification
refusée sur `/api/config` avec 401. Identifiants existants préservés.

## Validation logicielle et limites

- 47/47 CTest hôtes sous ASan/UBSan ; le dernier ajout de fixture sensible a
  aussi été recompilé et son test ciblé rejoué avec succès.
- Dans chacune des trois qualifications SDK : 47 CTest, 12 tests QuickJS WebUI,
  neuf tests flasheur, six tests portable, huit tests capture ; deux binaires
  identiques indépendants. Le SDK ne contient pas de secrets de l'appareil.
- Chromium final sur API synthétiques : Gateway desktop/mobile et capacité
  sans GNSS simulée ; grande réponse, Ethernet réel de fixture, formulaires,
  secrets, erreurs, onglets, sessions, pause masquée et pic d'une requête API.
  La capacité simulée sans GNSS ne qualifie pas le relais C3.
- 17 tests de politique CI/alpha natifs ; contrôles de dashboard/checkpoints.
- La tentative directe QuickJS avec le Python hôte ne pouvait pas démarrer
  (dépendance absente). Les douze tests ont effectivement passé dans les
  environnements SDK qualifiés ; aucune nouvelle dépendance firmware ajoutée.

Le firmware final conserve 1676496 octets libres dans son slot de 3 Mio.
Les assets gzip passent de 15604 à 15634 octets (+30, correction Ethernet),
sans requête supplémentaire. Aucune modification des manifests, workflows,
versions, protocoles, sources métier partagées, ROSIDL ou payloads sélectionnés
pour les distributions ROS2.

**Verdict : gain de mémoire interne USB démontré, placement/effacement JSON
et reprise sur refus qualifiés dans le scénario testé.** La taille du plus
grand bloc, le maintien en pression réduite et les maxima de latence sont des
limites explicites. Endurance prolongée, trafic GNSS/RTCM physique et impact sur
sa latence restent non qualifiés. Les allocations internes TLS et les autres
buffers peuvent faire l'objet de lots S3 séparés ; aucune optimisation du
moteur partagé n'est comprise ici. Ces résultats doivent être réévalués si SDK,
matériel, allocateur, configuration réseau/HTTPS ou charge GNSS changent.

## Périmètre des fichiers

14 fichiers suivis modifiés et 12 fichiers nouveaux, dont ce rapport :

- `ROADMAP.md`
- `TODO.md`
- `docs/analysis/ESP32_S3_MEMORY_QUALIFICATION.md`
- `embedded/esp32/README.md`
- `embedded/esp32/WEBUI.md`
- `embedded/esp32/components/ug_platform/CMakeLists.txt`
- `embedded/esp32/components/ug_platform/include/ug_console_buffer.hpp`
- `embedded/esp32/components/ug_platform/include/ug_json_stress.hpp`
- `embedded/esp32/components/ug_platform/include/ug_memory_diagnostics.hpp`
- `embedded/esp32/components/ug_platform/include/ug_s3_json_memory.hpp`
- `embedded/esp32/components/ug_platform/include/ug_s3_memory_options.hpp`
- `embedded/esp32/components/ug_platform/src/ug_commission.cpp`
- `embedded/esp32/components/ug_platform/src/ug_memory_diagnostics.cpp`
- `embedded/esp32/components/ug_platform/src/ug_s3_json_memory.cpp`
- `embedded/esp32/components/ug_web/assets/app.js`
- `embedded/esp32/components/ug_web/src/ug_web.cpp`
- `embedded/esp32/native/build.py`
- `embedded/esp32/native/main/gateway.cpp`
- `embedded/esp32/native/tests/CMakeLists.txt`
- `embedded/esp32/native/tests/README.md`
- `embedded/esp32/native/tests/memory_stubs/esp_heap_caps.h`
- `embedded/esp32/native/tests/memory_stubs/sdkconfig.h`
- `embedded/esp32/native/tests/test_console_buffer.cpp`
- `embedded/esp32/native/tests/test_s3_json_memory.cpp`
- `embedded/esp32/native/tests/test_web_assets.py`
- `embedded/esp32/native/tests/test_webui_browser.py`

L'audit mémoire initial non suivi et les trois illustrations de carte préexistantes
sont conservés sans modification et hors de ce périmètre. Aucun checkpoint local,
sauvegarde, certificat, clé, jeton ou artefact de compilation n'est destiné au commit.
Les sources de firmware présentes correspondent aux 124 fichiers runtime/build
contrôlés dans l'arbre qualifié final ; seules les extensions des tests et la
documentation ont changé après cette construction.

Contrôles finaux : clang-format-21, syntaxe Python, 58 destinations de liens
locaux existantes, dashboard et checkpoints, index Git vide, `git diff --check`.
Les huit plans de sélection du générateur ont été calculés sur la base : aucun
fichier modifié/nouveau n'appartient à leurs payloads. Pas de nouvelle compilation
ROS2/Linux ou C3, de qualification distante GitHub, ni de campagne Kilted.
Les branches HTTPS/WebUI déjà commitées ont été poussées normalement ; les
modifications mémoire présentes restent locales, non commitées et non poussées.
