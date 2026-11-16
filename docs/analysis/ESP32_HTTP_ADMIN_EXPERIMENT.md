# S3 — variante expérimentale d’administration HTTP

État : variante HTTP installée après accord ; administration et charge radio arrêtée
vérifiées. Le critère du bloc de 32 Kio sous charge radio active échoue encore.
Le propriétaire demande d’évaluer HTTP sur le LAN après le
[critère mémoire non atteint sous HTTPS/radio](ESP32_RADIO_HTTPS_COEXISTENCE.md).
Aucun commit, push ou changement de publication n’est inclus.

## Sélection et contrats

`--web-transport http` est un choix de compilation explicite du Gateway S3.
Les commandes habituelles et la publication existante gardent HTTPS par défaut
pendant cette expérimentation. Un échec TLS ne déclenche jamais un serveur HTTP.
Le TLS client NTRIP, les identités déjà en NVS, l’authentification par mot de passe,
les sessions glissantes et les confirmations restent conservés. Les données de
connexion et d’administration circulent en clair sur le LAN dans ce mode choisi.
La vérification coûteuse du mot de passe reste un sujet distinct du transport.

Le nouveau service `http` est ajouté après les identifiants existants ; les autres
ne sont pas renumérotés. Statut et capacités indiquent `web_transport`. La WebUI
présente l’administration HTTP/HTTPS réellement sélectionnée ; elle ne présente
pas un serveur HTTP comme un HTTPS fonctionnel. En mode HTTP, le serveur HTTPS
n’est pas démarré et son diagnostic reste « non tenté ». Aucun certificat supprimé.

Les piles restent identiques pour cette comparaison : démarrage 32768 octets,
serveur 24576. Seuils mémoire 96/32 et 48/16 Kio, trois clients, files et priorités
inchangés. Pas de modification de partitions, de moteur partagé ou de profil C3.

Sous l’environnement ESP-IDF épinglé par le projet, comme utilisateur normal :

```bash
python embedded/esp32/native/build.py --output /tmp/ug-s3-http \
  --profile gateway --board waveshare-s3-eth --local-gnss \
  --web-transport http --provision
python embedded/esp32/native/verify.py --work /tmp/ug-s3-http \
  --profile gateway --local-gnss --web-transport http
```

Ces commandes compilent/vérifient, sans flash. Le vérificateur refuse une sélection
HTTP/HTTPS incohérente avec le SDK effectif et réserve HTTP à cette expérience S3.

## Preuves logicielles

Snapshot `1d0bd663643f05540032b6a2a8b4910bab03656e` : deux builds indépendants par
transport, quatre segments comparés par paire, compilation/vérification : PASS.
SDK épinglé 6.1, builds sans réseau ni montage des sources, utilisateur normal.

| Variante | Application | SHA-256 |
| --- | --- | --- |
| HTTP | 1483648 octets | `706a302a2727b499f5b86de75d62c21e1117ace2782e5b040340f759b8dd79cc` |
| HTTPS | 1488352 octets | `2f93e09d8cee25257deb8fb70e477806306516f92e2090833854f7889478ccf4` |

Gain Flash HTTP à code égal : 4704 octets. `.dram0.data` 23757 et `.dram0.bss`
70512 sont identiques ; cela ne mesure pas l’occupation dynamique du serveur.
Dans l’ELF HTTP, `httpd_start` est présent et `httpd_ssl_start` absent ; il est
présent dans le témoin HTTPS. Les identités et capacités TLS NTRIP restent liées.

66 CTest ASan/UBSan, 23 tests JavaScript, 11 tests ciblés de politique ESP32 : PASS.
Cinq parcours Chromium sur la vraie HTML et des API/ports simulés : trois HTTPS,
deux HTTP (desktop/mobile). Connexion, onglets, NTRIP, consultation du mot de passe,
session/restreinte, réponses tardives et une seule requête API à la fois : PASS.
Le cas HTTP LAN vérifie explicitement le diagnostic d’indisponibilité Web Serial.
Ces résultats ne prouvent ni la réception GNSS/RTK ni un appairage physique.

## Appairage USB

Web Serial n’est pas exposé aux pages HTTP d’une adresse LAN. HTTPS sur le portail
ou une origine locale `localhost` peut héberger cette partie sans TLS sur le S3.
Le choix d’une solution réutilisant la WebUI locale ou le Web Flasher est en cours ;
aucune nouvelle solution d’appairage n’est encore annoncée comme fonctionnelle.
Références : [Chromium Web Serial](https://developer.chrome.com/docs/capabilities/serial),
[contextes potentiellement fiables](https://www.w3.org/TR/secure-contexts/#is-origin-trustworthy).

La liste de clients MAC et le broadcast non chiffré optionnel demandés par le
propriétaire restent un lot distinct. Le runtime livré ici conserve un pair
chiffré, sans effacement ou création automatique d’association.

## Qualification matérielle suivante

Après accord sur l’image HTTP exacte : application seule avec sauvegarde/digest et
comparaison NVS/table/OTA avant boot ; vérifier HTTP réel et absence d’HTTPS actif.
Rejouer les paliers 1/2/3 clients, radio arrêtée puis active sur le canal 1 avec
les clés existantes, NTRIP suspendu et sans émission RTCM de test. Arrêter au premier
critère non satisfait ; restaurer exactement les paramètres et laisser la radio
arrêtée. Le firmware actuellement installé avant cet essai reste b33e4a12… HTTPS.


## Mesures réelles sur le S3

L’application HTTP 706a302a… a été installée seule avec l’accord du propriétaire,
avec digest vérifié et régions NVS/table/OTA identiques avant boot. Aucun flash C3.
Les requêtes de charge sont publiques (statut/ressources), sans authentification
concurrente ni flux RTCM de test. La pile serveur reste identique à HTTPS.

| Clients, radio arrêtée | Requêtes | Interne minimum | Bloc minimum | PSRAM minimum | p95 |
| --- | --- | --- | --- | --- | --- |
| 1 | 30 | 114279 | 34816 | 8344204 | 62,603 ms |
| 2 | 60 | 113695 | 34816 | 8341124 | 65,486 ms |
| 3 | 90 | 113091 | 34816 | 8338044 | 65,662 ms |

À trois clients, l’écart observé avec l’essai HTTPS précédent est +2668 octets
internes et +73212 octets PSRAM ; le p95 passe de 154,485 à 65,662 ms. Ce sont des
campagnes courtes successives, pas une garantie de performance sous charge GNSS.
Le coût interne principal ne peut donc pas être attribué au seul transport TLS.

Radio active (mêmes pairs/clés, canal 1, NTRIP temporairement suspendu), le démarrage
HTTP/ESP-NOW réussit : interne 59035, bloc 32768, PSRAM 8338620, zéro allocation
échouée. **Le contrôle de charge échoue** quand le bloc descend à 31744 octets
(interne 57155). L’essai est interrompu au premier critère ; aucune réussite des
trois paliers actifs n’est revendiquée. Aucune protection abaissée pour le faire passer.

Après restauration, les réglages logiques sont comparés exactement au préalable
par relecture NVS ; identités, authentification, clés, partitions et OTA restent
préservées. Les calibrations PHY du SDK sont distinguées de cette égalité logique.
Le S3 redémarre en HTTP, radio effectivement arrêtée et NTRIP réactivé comme avant :
interne 115151, bloc 34816, PSRAM 8344068, zéro allocation échouée. HTTP réel vérifié ;
TCP 443 refuse les connexions, conformément au mode choisi. Ports USB libérés.

Le propriétaire confirme la connexion réelle, la navigation dans les six onglets
et Afficher/Masquer du mot de passe NTRIP, sans partage du secret. Le coût
d’environ 88 s du KDF reste ouvert ; une [optimisation compatible](ESP32_LOGIN_LATENCY.md)
est mesurée séparément sur la carte : environ 37–39 s, encore trop lent. Le propriétaire a choisi l’intégration d’un appairage USB dans
le Web Flasher HTTPS ; cette fonction n’est pas encore implémentée à ce stade.


## État consolidé pour le gel Git

Les sections précédentes conservent les mesures de chaque image. L’état final,
la connexion opérateur confirmée et les limites encore ouvertes sont récapitulés
dans le [rapport de latence et gel Git](ESP32_LOGIN_LATENCY.md#gel-git-du-travail-validé--16-novembre-2026).
La conservation dans Git ne constitue pas une publication de firmware.
