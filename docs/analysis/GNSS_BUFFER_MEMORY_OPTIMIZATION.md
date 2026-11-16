# Moteur commun — buffers GNSS compacts (premier lot)

Base : `e5cb849dd2e1830d98dfb5ec893af2648a91bd40`, branche
`fix/esp32-single-login`. Les mesures ont été réalisées sur un arbre local avant
commit ; la conservation Git et le push pour revue sont maintenant autorisés.
Aucun flash ni publication de firmware. Les résultats portent sur le code final
de cette proposition ; les métadonnées décrivent son état lors des mesures.

## Conclusion

Les buffers de `UbloxSession`, `UnicoreSession` et `ReceiverSession` utilisent un
stockage commun compact, sans changement fonctionnel détecté. Pour 4096 octets
incomplets et un timestamp commun, le stockage retenu passe de **98304 à 4112
octets** (−95,8 %). Le gain de pic total dépend du décodeur : les temporaires
existants des framers ne sont pas supprimés. Aucun déplacement PSRAM spécifique.

Il n’existe **pas de gain CPU universel** : les segments très denses ralentissent
certains scénarios. Le lot est présenté pour revue mémoire/CPU avant intégration,
sans déclarer la qualification matérielle GNSS, RTK ou radio accomplie.

## Contrats et représentation

Le propriétaire est chaque session, avec accès séquentiel par son propriétaire
existant. `FeedBytes` ajoute les données, le décodage consomme les préfixes et
`Reset`/`Finalize` conservent leurs règles d’invalidation. Aucun nouveau verrou.
Le buffer n’est pas une nouvelle interface publique thread-safe.

- Un `vector<uint8_t>` contigu porte les octets ; un second tableau contient des
  segments horodatés. Un segment fait 16 octets sur x86-64 et Xtensa S3 mesurés.
- L’int64 complet et un bit de présence séparé conservent `nullopt`, zéro,
  valeurs négatives et extrêmes, sans timestamp sentinelle ni perte de précision.
- Seules les métadonnées d’octets adjacents de même timestamp sont fusionnées.
  Aucun octet, observation ou événement n’est fusionné. Des observations
  identiques restent distinctes ; les frontières des appels Feed sont conservées.
- Un curseur mémorise la fin et le timestamp du segment : recherche initiale
  O(log R), puis progression O(B+R), sans recherche binaire par octet.
- La consommation avance une tête logique ; la copie du préfixe est différée
  jusqu’au besoin d’espace. Les métadonnées consommées sont retirées.
- L’autodétection sonde directement les octets contigus, supprimant
  `ToByteVector`. Le rejeu vers la session choisie reste **octet par octet** :
  regrouper arbitrairement les Feed aurait pu changer les resynchronisations.

Les framers, parsers, CRC, mappings, fraîcheur, sélection et structures publiques
d’observations ne sont pas réécrits. Les trois anciens types BufferedByte restent
présents pour la compatibilité source ; ils ne servent plus au stockage privé.
Les tailles privées de sessions changent : **reconstruire les consommateurs C++**,
les anciennes mises en page binaires ne sont pas interchangeables. Aucun nouveau
format externe, ROSIDL, réglage GNSS, NVS, partition ou version.

## Capacités, durée de vie et échecs

Les octets croissent géométriquement ×2, les segments d’environ 25 %. Même un
timestamp distinct par octet reste borné par la taille du lot/pending : environ
22N+constante de capacité retenue sur les ABI mesurées, contre 24N pour le reserve
exact historique ; le cas réellement mesuré figure plus bas. Arithmétique et
limites vector sont vérifiées avant croissance.

Après décodage, trim d’autodétection et reset, une capacité dépassant deux fois
les limites configurées de détection/framing est réduite. Les capacités ordinaires
sont réutilisées ; un reset ne provoque pas une réallocation systématique.
Sous Linux, la réduction d’un buffer vivant est best effort : une erreur
`bad_alloc` conserve exactement les octets/timestamps et permet une nouvelle
tentative. Les erreurs de croissance ne laissent pas d’append partiel.

ESP-IDF garde les exceptions C++ désactivées. Une réduction optionnelle de buffer
vivant n’y alloue pas : elle est différée, et le stockage surdimensionné vide ou
réinitialisé est libéré sans allocation. La capacité vivante peut donc dépendre
du plus gros Feed fourni par l’appelant ; aucune limite arbitraire de taille Feed
n’a été introduite. Il faut toujours inspecter le lot entier avant de choisir un
récepteur, comme auparavant. Le chemin S3 actuel fournit des lots de **512 octets**
(`ug_uart_drain.hpp`), distincts du stress à 262144 octets du benchmark.

Les curseurs et pointeurs expirent à la prochaine mutation ; aucun emprunt n’est
conservé dans un framer ou après Feed. Les horodatages restent dans le domaine
monotone fourni, sans conversion vers UTC ou redéfinition d’observation physique.

## Méthode de mesure reproductible

[Script de comparaison](../../core/gnss_driver/tests/compare_session_buffers.py),
[harness C++](../../core/gnss_driver/tests/benchmark_session_buffers.cpp),
[mesures CSV](GNSS_BUFFER_BENCHMARK.csv) et
[provenance JSON](GNSS_BUFFER_BENCHMARK.json).

```bash
python3 core/gnss_driver/tests/compare_session_buffers.py \
  --base e5cb849dd2e1830d98dfb5ec893af2648a91bd40 \
  --output /tmp/ug-buffer-comparison-new --runs 7
```

Le script archive la base immuable hors dépôt et compile les deux moteurs avec
le même harness, les mêmes fixtures et GCC Release/C++17. Il ne crée aucun commit.
L’option CMake `UNIVERSAL_GNSS_BUILD_BUFFER_BENCHMARK` permet aussi une mesure locale
opt-in. `-fno-access-control` ne sert qu’au harness pour lire les capacités privées,
jamais aux bibliothèques du moteur. Le harness n’est pas installé.

Les compteurs de new/new[] capturent nombre, volume demandé et somme/pic des octets
utilisables réellement réservés par malloc (`malloc_usable_size`). Ce ne sont ni
le RSS, ni les piles, ni les métadonnées internes de malloc ; les allocations
libc internes sont exclues. Les changements de buffer utilisent les allocations
C++ instrumentées. La taille statique des sessions est mesurée séparément.

Les temps sont mesurés dans des processus séparés **sans instrumentation des
allocations ni trace canonique**, avec ordre référence/candidat alterné et sept
paires, sans autre build de cette campagne en cours. Les latences p50/p95 sont
celles des appels Feed, pas des latences série/GNSS physiques. Le temps CPU
user+system du processus inclut la boucle de test, les horloges et le CSV : il
ne doit pas être présenté comme un profil cycles exclusif du moteur.

Les 222 scénarios couvrent fixtures NMEA, UBX, Unicore ASCII, PVTSLN binaire
synthétique selon le fixture existant, RTCM et flux mixtes ; fragments 1/7/64/4096,
timestamps absents/communs/changeants, données corrompues, reset au milieu du flux,
finalisation, préfixes parasites et saturation des capacités. Une trace canonique
compare après **chaque Feed, Finalize et Reset** tous les champs d’état (flottants
en hexfloat exact) et toutes les métriques exposées, y compris fraîcheur et
compteurs par type RTCM. Les callbacks d’événements extérieurs ne sont pas modifiés.

Les 7 paires mémoire donnent une trace identique (SHA-256 dans le JSON). Un rejeu
final confirme aussi l’identité après l’ajout de libération de capacité au Reset.
Les mesures CPU finales utilisent exactement ces derniers hooks Reset. Le premier
jeu de temps chevauchant une compilation a été écarté des résultats retenus.

## Mémoire réellement réservée : 4096 octets incomplets

Timestamp commun ; tailles en octets. Les capacités vector sont conservées après
Finalize et Reset dans ces cas normaux (4112 contre 98304), sans données actives.
Le pic inclut les temporaires de décodage, pas seulement le stockage principal.

| Session | Stockage référence | Stockage compact | Heap retenu réf. | Heap retenu compact | Pic réf. | Pic compact |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| pending-ubx | 98304 | 4112 | 98312 | 4128 | 200736 | 106552 |
| pending-unicore | 98304 | 4112 | 98312 | 4128 | 147472 | 10288 |
| pending-auto | 98304 | 4112 | 98312 | 4128 | 190480 | 6184 |

| Session | Allocations réf. | Allocations compact | Volume demandé réf. | Volume compact |
| --- | ---: | ---: | ---: | ---: |
| pending-ubx | 428 | 418 | 2462943 | 1635311 |
| pending-unicore | 411 | 405 | 379591 | 191087 |
| pending-auto | 33 | 6 | 874496 | 7952 |

Avec 16/32 lots de timestamps changeants ou absents, les stockages retenus mesurés
sont 4384 / 4688 / 4384 octets respectivement pour u-blox / Unicore / autodétection.
Dans le stress 4097 octets et timestamp variant à chaque octet, ils atteignent
**75984 octets**, contre 98328 pour u-blox/autodétection et 196608 pour Unicore
(surallocation historique ×2). Ce cas est inclus, pas assimilé à un timestamp unique.

Le bruit en un lot de 262144 octets laisse historiquement **6291456 octets** par
buffer. Le candidat conserve 16 / 16 / 4112 octets après traitement pour u-blox /
Unicore / autodétection. Ces cas synthétiques vérifient les capacités, sans promettre
ce gain sur un trafic GNSS réel. Le JSON précise les unités des colonnes CSV :
capacité historique en slots BufferedByte, capacité compacte en octets + segments.

## Traitement, latence, débit et compromis CPU

Sept mesures, médiane des durées Feed ; débit effectif synthétique, hors attente I/O.

| Cas | Référence | Compact | Débit réf. | Débit compact | Variation durée |
| --- | ---: | ---: | ---: | ---: | ---: |
| pending-ubx | 0.480 ms | 0.407 ms | 8334.4 Kio/s | 9838.0 Kio/s | -15.3 % |
| pending-unicore | 0.628 ms | 0.580 ms | 6370.4 Kio/s | 6895.2 Kio/s | -7.6 % |
| pending-auto | 2.289 ms | 2.143 ms | 1747.1 Kio/s | 1866.7 Kio/s | -6.4 % |
| dense-ubx | 105.338 ms | 133.593 ms | 38.0 Kio/s | 29.9 Kio/s | +26.8 % |
| dense-unicore | 73.503 ms | 93.088 ms | 54.4 Kio/s | 43.0 Kio/s | +26.6 % |
| dense-auto | 534.143 ms | 579.608 ms | 7.5 Kio/s | 6.9 Kio/s | +8.5 % |

Les p50/p95 et leurs bornes min/max sur les sept exécutions sont dans le CSV.
Sur les 207 scénarios de fixtures ordinaires, allocations et volume demandés
cumulés passent de **337630 à 328204** et de **52854911 à 35658967 octets**.
Il ne s’agit pas d’un mix de production. Le tableau séparé des pics compte les
allocations de framers encore présentes ; aucun gain théorique n’est assimilé
à une suppression de tous les temporaires du moteur.

Le second tableau de métadonnées peut ajouter une allocation : **79/222 cas**
augmentent leur nombre d’allocations (souvent +1 à l’amorçage). Le cas Unicore
dense passe de 49195 à 49228 allocations malgré un volume légèrement réduit.
Cette régression est conservée dans les résultats ; aucune allocation n’est
masquée en la sortant de l’intervalle de mesure du nouveau moteur.

Temps CPU du harness timing-only complet : **0.808340 s → 0.887456 s**, soit
**+9.8 %**. Il n’y a donc pas de gain CPU global sur ce stress. Les
resondages de trames incomplètes restent quadratiques à fragmentation extrême ;
ils ne sont pas réécrits ici. Le parcours dense des timestamps apporte aussi un
surcoût. Ce compromis doit être accepté avant d’intégrer le lot ; le gain mémoire
ne justifie pas de prétendre que chaque chemin accélère.

## Linux, ROS2 et ESP32

- **70/70 CTest Linux sous ASan/UBSan**, incluant 20000 opérations aléatoires du
  buffer comparées à un oracle par octet, extrêmes int64, absence, copie/move,
  préfixes, débordements et échecs d’allocation/compaction : PASS.
- **68/68 tests hôte ESP32 sous ASan/UBSan** : PASS ; GNSS, transactions/quarantaine,
  RTCM/ESP-NOW, NTRIP et tests mémoire existants conservés.
- **265/265 tests générateur/politique CI** : PASS, manifeste inchangé.
- **Lyrical : 166 tests, zéro erreur/échec/skip**, dans un conteneur utilisateur
  non-root sans réseau, périphérique ni montage des sources, sans overlay UG
  préinstallé. Build et tests des packages existants, code final Reset inclus.
  Une première préparation incomplète omettait des fixtures Python ; elle a été
  corrigée dans l’environnement de validation, sans modification du produit.
- S3 Gateway GNSS local, **HTTP et HTTPS : deux builds indépendants par variante**,
  vérification et reproductibilité des quatre segments : PASS. SDK 6.1 épinglé,
  C++17 et exceptions désactivées, aucun nouveau flash.

La version de validation est un arbre local non commité
`2275b09f614a0894f396811b7c9f92c4a608e692`. Les deux tableaux/probes Xtensa confirment
24 octets pour l’ancien type, 16 par segment et 28 pour le contrôle du nouveau
buffer. Sur Linux, tailles de session u-blox 1568→1600, Unicore 1592→1624,
autodétection 5248→5344 : les contrôles supplémentaires ont aussi un coût.

Dans les images S3, `.dram0.data` reste à 23757 octets et `.dram0.bss` passe de
70576 à **70608 (+32 octets)**. Les variantes HTTP/HTTPS prennent respectivement
**+576/+544 octets Flash**. Assets WebUI, partitions, OTA, bootloader, seuils et
configuration réseau sont inchangés. Le JSON conserve les SHA complets des images
construites localement ; elles ne sont ni publiées ni installées.

## Limites et décision de revue

Le gain dynamique est mesuré sur Linux. Il ne constitue **pas une mesure du heap
interne S3**, de la fragmentation, des piles ou de la latence sous radio active.
Le placement interne/PSRAM dépend du seuil de l’allocateur ESP-IDF déjà configuré :
réduire une allocation peut aussi changer sa région. Il n’est pas modifié ici.
Sans octets GNSS reçus, ces buffers étaient déjà vides : aucune économie dynamique
au repos sans GNSS n’est revendiquée, et le coût statique +32 demeure.

Pas de validation physique GNSS/RTCM/RTK, C3, portée ou endurance, pas de nouvelle
campagne Kilted, pas de changement du SDK ni de politique de publication. Le C3
Relay et les optimisations PSRAM spécifiques restent hors périmètre. Les noms des
cibles, packages et interfaces sont conservés. Aucune adaptation aval MowgliNext
n’est nécessaire ; reconstruire les consommateurs C++ contre les nouveaux headers.

**Gain mémoire confirmé, équivalence fonctionnelle soutenue par les tests ; coût
CPU défavorable sur certains flux très fragmentés à arbitrer avant intégration.**
Le prochain travail éventuel pourra cibler ces coûts après revue, sans lancer une
réécriture des framers dans ce lot.
