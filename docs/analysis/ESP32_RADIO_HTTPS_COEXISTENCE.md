# ESP32-S3 — coexistence ESP-NOW et HTTPS

État : correctif logiciel construit ; qualification radio active encore requise.
Branche locale `fix/esp32-single-login`, base
`d3c9e0688bc6756bc0f493397d03c7bd3c2f1b40`. Aucun commit, push ou publication.
Ce complément suit le [lot WebUI/NTRIP](ESP32_WEBUI_SESSION_NTRIP.md).

## Constat matériel

Sur Waveshare S3 W5500, application installée
`48cbb47e994045648b932ef36c889ec9b42782916a47f68dd19a4a1adb256732`,
l’opérateur a enregistré une association puis redémarré le S3. ESP-NOW et son
contrôle démarrent, mais TCP 443 refuse les connexions. La console indique
`https_startup=server_start_failed`, `sdk_error=45064`. Dans le SDK épinglé,
`0xb008` désigne `ESP_ERR_HTTPD_TASK` : échec de création de la tâche serveur.
L’identité TLS reste valide, et un échec d’allocation est comptabilisé. Il ne
s’agit donc pas du précédent problème de certificat absent.

Sans tâche HTTPS après ce démarrage : RAM interne libre 73615 octets, minimum
23440, plus grand bloc 32768. Ces valeurs sont des relevés après échec, et non
la quantité disponible à l’instant exact de l’allocation ratée. Elles ne prouvent
pas, à elles seules, la taille ou la région de l’allocation refusée.

Le C3 Rover identifié par UGCFG/1 est déjà associé au S3, clé présente, canal 1.
Aucune nouvelle association n’a été écrite par l’agent pendant ce diagnostic.
Avant le redémarrage C3 explicitement autorisé, UGCFG ne répondait plus ; après
redémarrage et OPEN pendant sa fenêtre initiale, identité et état répondent.
Un Rover déjà associé ferme sa maintenance après 8 secondes de démarrage et ne
traite plus ces commandes dans le flux binaire. Cela explique le silence observé,
sans prouver l’égalité des deux clés ni une livraison RTCM physique.

## Récupération autorisée

Le propriétaire a autorisé une désactivation temporaire de la radio S3, avec
sauvegarde privée et conservation de l’association. Les régions partition/NVS/OTA
ont été sauvegardées et vérifiées. Les lectures longues du slot applicatif ont
échoué au même voisinage `0x74000`, y compris une lecture par bloc traversant
cette adresse. Ce contrôle n’est pas présenté comme réussi.

L’application complète a ensuite été vérifiée sur la carte par le digest du SDK
contre le binaire reproductible 48cbb47e… conservé localement ; une copie exacte
de ce binaire vérifié sert de sauvegarde applicative. Aucune sauvegarde brute des
3 Mio du slot, incluant sa fin inutilisée, n’est revendiquée. Aucune application
n’a été écrite pendant la récupération.

Seul `radio.enabled` est passé à `false` par la console USB de maintenance. Les
blobs actifs avant/après ont été relus et réassemblés avec le parseur NVS du SDK,
CRC des entrées/blobs vérifiés. La comparaison logique établit :

- mêmes paramètres GNSS, NTRIP, réseau, MAC et clé radio ;
- seules les données de configuration d’activation radio changent, génération +1 ;
- namespaces `ug_auth` et `ug_identity` identiques ;
- table de partitions et sélection OTA identiques.

Le SDK a aussi renouvelé `phy.cal_data` pendant le démarrage radio. Le premier
comparateur global a signalé cette différence ; elle est conservée séparément.
Il n’est donc pas affirmé que toute la NVS est identique octet par octet après
une modification/restauration logique et un démarrage radio.

Après redémarrage : HTTPS réel vérifié avec le certificat d’origine, HTTP 200 sur
statut et ressources ; radio configurée et réellement arrêtée. RAM interne libre
99135, plus grand bloc 34816, PSRAM libre 8331004, pression normale. Sauvegardes,
configuration et preuves sensibles restent hors dépôt dans un répertoire privé.

## Correctif borné à qualifier

Les six files de publication/caster conservent leur profondeur de deux trames.
Seuls leurs tableaux d’octets sont réservés en PSRAM sur S3 ; structures et
contrôles FreeRTOS restent internes. Le pointeur est publié avant le démarrage
du worker. Échec PSRAM : le service concerné échoue explicitement, sans repli sur
la RAM interne. Une allocation réussie reste conservée jusqu’au redémarrage,
sans libération pendant les accès producteurs/consommateurs.

Ces producteurs et consommateurs sont des tâches, sans accès ISR/DMA à ces
payloads. Le SDK `xQueueCreateStatic` distingue explicitement le contrôle de la
zone d’octets ; seules les adresses de cette dernière changent. Le codec radio,
la mailbox ESP-NOW, les piles, le routage, les timestamps et les seuils restent
inchangés. Aucun changement du SDK global, du partitionnement ou du C3 Relay.

Preuve ELF Xtensa : les objets de file passent ensemble de 13272 à 624 octets
en DRAM. La section `.dram0.bss` passe de 83120 à 70480 octets, gain net
**12640 octets** après alignement ; `.dram0.data` reste à 23757. Ce gain statique
n’est pas une mesure de RAM disponible sous charge radio/HTTPS.

Snapshot `b56946787c72a6971715bbbb6c4fae9b87c9495c`. Application 1488080 octets,
SHA-256 `b33e4a1287a11d36930801fa5b32eb3783ebcd99e723e21eba597e57761b55cb`.
Deux builds S3 indépendants, quatre segments reproductibles, vérifications natives
et 66 CTest ASan/UBSan : PASS. Les tests vérifient notamment le refus d’allocation,
l’absence de réallocation d’un stockage publié et la conservation de deux trames.
Les 22 tests JS et les trois parcours Chromium du complément WebUI restent valides,
leurs fichiers n’ayant pas changé dans ce correctif de stockage.

## Prochaine qualification

L’installation de cette nouvelle application demande un accord distinct de celui
portant exactement sur 48cbb47e…. Le client NTRIP entrant est actuellement activé :
un essai sans émission RTCM nécessite de le suspendre temporairement avec accord,
puis de restaurer exactement ses paramètres. Réutiliser les pairs et clés existants,
canal 1 ; vérifier HTTPS/radio, mémoire, bloc maximal et 1/2/3 clients HTTPS.
Le contrôle réel RTCM/USB, les deux casters physiques, GNSS/RTK, la portée et
l’endurance restent distincts. Le coût d’authentification d’environ 88 secondes
reste excessif et n’est pas corrigé par ce déplacement de stockage.

## Suite demandée : plusieurs clients et broadcast

Le propriétaire demande une liste de MAC et un broadcast non chiffré optionnel.
Le code actuel n’implémente encore qu’un pair chiffré : aucun nouveau mode n’est
annoncé comme disponible. L’extension doit préserver la sérialisation des envois,
la quarantaine après timeout d’ACK et la validation des sources ; un broadcast
ne doit jamais être accepté implicitement par un Rover configuré chiffré.
Le chiffrement multicast ESP-NOW n’est pas pris en charge par le SDK : voir
[la documentation Espressif](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-reference/network/esp_now.html#security).


## Résultat de l’essai autorisé de b33e4a12…

L’application corrective a été installée seule ; digest vérifié et régions
protégées comparées identiques avant le premier démarrage. Radio arrêtée :
HTTPS fonctionne, aucun échec d’allocation. Les trois paliers de clients publics
HTTPS (statut/ressources, connexions persistantes, aucun mot de passe) donnent :

| Clients | Requêtes | Interne libre minimum | Bloc maximal minimum | p95 requête |
| --- | --- | --- | --- | --- |
| 1 | 30 | 111755 | 34816 | 84,272 ms |
| 2 | 60 | 111103 | 34816 | 147,984 ms |
| 3 | 90 | 110423 | 34816 | 154,485 ms |

Après suspension autorisée du client NTRIP entrant et activation des pairs/clés
existants sur le canal 1, le démarrage radio + HTTPS réussit, sans allocation
échouée : interne 57123, bloc 32768, PSRAM 8338620 octets. Le minimum historique
interne de démarrage est 15248 octets ; il est distingué de la mémoire disponible.

**Le test radio active sous requêtes HTTPS échoue** au critère du bloc d’au moins
32768 octets. Il a été interrompu ; le relevé après arrêt des clients donne
31744 octets pour le plus grand bloc, interne libre 55687, zéro échec d’allocation.
Le script s’arrêtant sur ce premier critère, aucune réussite des trois paliers
radio actifs n’est revendiquée. Aucun flux RTCM de test n’a été injecté.

Les réglages logiques initiaux ont été restaurés et comparés par relecture NVS :
client NTRIP activé comme avant, radio désactivée, mêmes clés, MAC et paramètres.
Identités et authentification identiques, table/OTA inchangées, seule calibration
PHY du SDK autorisée en plus des deux générations de configuration. Après le
redémarrage final, HTTPS est vérifié, radio effectivement arrêtée, interne libre
112839, bloc 34816, PSRAM 8343360 octets. Les ports USB sont libérés.

Le propriétaire demande ensuite d’évaluer une administration HTTP locale pour
simplifier le produit et réserver HTTPS/Web Serial au portail ou à un outil USB.
Une variante HTTP explicite est en préparation ; le firmware b33e4a12… actuellement
installé reste HTTPS. Le transport HTTP ne sera jamais un repli automatique d’un
échec TLS ; certificats existants et TLS client NTRIP sont conservés.


## État consolidé pour le gel Git

Les sections précédentes conservent les mesures de chaque image. L’état final,
la connexion opérateur confirmée et les limites encore ouvertes sont récapitulés
dans le [rapport de latence et gel Git](ESP32_LOGIN_LATENCY.md#gel-git-du-travail-validé--16-novembre-2026).
La conservation dans Git ne constitue pas une publication de firmware.
