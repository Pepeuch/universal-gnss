# Configuration utilisateur portable ESP32 v1

État : implémentation et validation logicielle locale ; USB physique, câblage et
récupération sur carte restent à qualifier. Aucun flash, push ou déploiement dans
ce lot. Les applications et les bibliothèques UG Linux/ROS2 restent inchangées.

## Contrat de confidentialité

Le fichier contient **exactement onze champs**, sans texte libre :

```json
{"format":"ug-esp32-portable","schema":1,"board":"supermini-c3","profile":"rover","ethernet":"none","output":"uart","uart_tx":7,"uart_rx":-1,"uart_baud":460800,"gnss_model":"none","radio_channel":1}
```

Le [schéma descriptif](config/portable-v1.schema.json), le parseur C sans allocation
`ug_portable_parse`, le validateur Python `tools/portable_config.py` et le JavaScript
partagé [portable.js](../../docs/flasher/portable.js) utilisent le même contrat.
Le format transmis est limité à **512 octets ASCII** : objet plat, clés uniques,
chaînes d’énumération sans échappement, entiers décimaux sans exposant/fraction,
aucun champ manquant ou supplémentaire. L’ordre et les espaces JSON standards
sont libres. Une version inconnue est refusée ; il n’y a pas de migration implicite.

Mot de passe, clé, certificat, MAC, SSID, nom personnalisé, endpoint, compte,
coordonnées, historique et diagnostic sont **absents**, sans masque ni encodage.
L’export S3 est construit par sélection explicite des champs, jamais par copie de
la configuration privée. Le canal est une préférence radio non identifiante ;
il ne transporte pas d’association. Sur un appareil déjà appairé, un canal
importé différent est refusé : utiliser le mécanisme d’appairage explicite.
Le remplacement d’une carte exige donc un nouvel appairage privé séparé.

Le portail conserve les données seulement en mémoire de la page, ne journalise
pas leur contenu et n’utilise ni stockage navigateur ni service distant pour ce
fichier. La demande GitHub contient le profil de ROM générique, **pas** le fichier
portable. Un import n’insère aucun réglage utilisateur dans une image publique.
Le catalogue exige une qualification annonçant effectivement le schéma portable
v1 ; les anciens bundles ne peuvent pas revendiquer cette capacité.

## Capacités matérielles bornées

| Carte | Runtime | Fixé par le matériel/profil |
| --- | --- | --- |
| Waveshare S3 ETH | UART GNSS TX/RX distincts, 9600–921600 baud, modèle parmi auto/NMEA/UM980/UM981/UM982/ZED-F9P | W5500 soudé ; Flash 16 MB et PSRAM 8 MB |
| C3 SuperMini | Sortie USB ou UART1 TX, GPIO4/5/6/7/10, 115200/230400/460800/921600 baud | USB Serial/JTAG réservé ; Flash 4 MB, aucun GNSS/Ethernet dans le rover |

S3 : GPIO GNSS autorisés `1,2,15,16,17,18,38,39,40,41,42,43,44,47,48`.
Le profil ne prend en charge aucun module caméra raccordé. Les broches des
périphériques embarqués restent exclues : SD4–7, W5500 9–14, LED21, USB19/20,
alimentation caméra8, strapping et bus Flash/PSRAM. Le débogage/console natif
utilise USB ; ne pas raccorder simultanément un périphérique incompatible à ces
lignes. Affecter TX=RX est refusé. `-1/-1` signifie GNSS non câblé.

Le [schéma Waveshare du 21 décembre 2024](https://files.waveshare.com/wiki/ESP32-S3-ETH/ESP32-S3-ETH-Schematic.pdf)
et les [définitions officielles](https://docs.waveshare.com/ESP32-S3-ETH/Development-Environment-Setup-ESP-IDF)
confirment le câblage W5500 : MOSI11, MISO12, SCLK13, CS14, INT10, RST9.
Ce sont des pistes soudées, pas un choix libre de GPIO ; elles restent fixes,
sans variante de compilation ni remappage fictif. Cette revue documentaire ne
qualifie pas la révision réelle de la carte, ses niveaux électriques ou le GNSS.

C3 : GPIO2/8/9 de boot/LED, USB18/19, Flash et console UART0 ne sont pas proposés.
Les validations de carte s’appliquent aussi aux fichiers importés ; l’interface
ne s’appuie pas sur une liste générique de GPIO ESP32.

## S3 : WebUI, persistance et récupération locale

- **Exporter ma configuration portable** appelle `GET /api/config/portable` et
  produit seulement le document ci-dessus, adapté au S3.
- Importer le fichier prépare un aperçu avant/après par
  `POST /api/config/portable/preview`, sans écriture.
- L’application nécessite `POST /api/config/portable/apply` avec
  `{"confirm":"APPLY_PORTABLE_CONFIGURATION","configuration":{...}}`.
  Ces opérations sont authentifiées et conservent les contrôles de ressources Web.

L’application atomique réutilise le verrou, les instantanés immuables et les deux
banques NVS existants. Elle préserve les secrets, endpoints, identité, association
radio et journal GNSS. Elle **désactive GNSS, source locale et émission GGA** :
le câblage et les services doivent être revus avant réactivation explicite. Elle
n’envoie aucune commande au récepteur et ne lève aucune quarantaine.
Une écriture indéterminée verrouille les mutations selon la politique existante.

La console USB démarre **avant** les services réseau/GNSS dans une tâche dédiée.
Un mauvais câblage GNSS accepté syntaxiquement ne retire donc pas cette voie
logicielle de récupération. Commandes locales :

```text
portable-export
portable-check <JSON portable sur une ligne>
portable-apply CONFIRM <JSON portable sur une ligne>
```

Réponses réservées `UGPORT/1 JSON <JSON>`, `UGPORT/1 VALID` et
`UGPORT/1 SAVED_RESTART_REQUIRED` ; un refus ne renvoie pas le fichier reçu.
Les journaux de console habituels peuvent coexister ; ce port S3 n’est pas un
flux RTCM binaire. Les fichiers NVS inconnus/corrompus restent préservés et les
écritures restent verrouillées ; ce protocole ne contourne pas une migration de
schéma persistante ou une restauration de sauvegarde nécessitant autorisation.

Le profil C3 relais expérimental ne reçoit pas ces services portables S3. Son
budget Flash reste vérifié sans agrandir ses partitions ni supprimer de fonction.

## C3 : même ROM, configuration avant le flux RTCM

Les deux backends USB/UART sont présents dans l’image générique C3, USB par défaut.
La sélection persistante est chargée **avant** Wi-Fi, radio et émission RTCM.
Les anciennes options de build UART restent des valeurs initiales pour les
fixtures CI historiques ; le portail utilise la seule ROM C3 à défaut USB, même
pour préparer une sortie UART. Aucun workflow n’est modifié dans ce lot.

Le [contrat de maintenance](PAIRING.md) garde sa fenêtre silencieuse de 8 secondes,
son OPEN avec nonce frais et sa limite de 120 secondes. Nouvelles commandes :

```text
UGCFG/1 CONFIG_GET
UGCFG/1 CONFIG_CHECK <JSON portable>
UGCFG/1 CONFIG_APPLY CONFIRM <JSON portable>
```

Les réponses `UGCFG/1 {"portable":{...},"result":"current|valid|saved"}` ne
contiennent pas de MAC ou clé. CONFIG_CHECK ne fait aucune écriture. APPLY écrit
un blob `ug_rover/portable` de 24 octets, versionné, avec CRC24Q UG, commit et
relecture avant acquittement. Le blob privé d’appairage reste séparé et inchangé.
Son canal prévaut lorsqu’un pair existe. Une corruption portable connue interdit
le démarrage du flux et peut être remplacée uniquement par APPLY confirmé ; une
version future ou une erreur de lecture verrouille les écritures et préserve
l’original. Aucun effacement de namespace ou de flash n’est automatique.

RUN explicite ou expiration avec configuration et pair valides draine la réponse
de fermeture avant de démarrer les données. Aucun analyseur de commande ne reste
actif dans le flux RTCM. La sortie UART ne réaffecte jamais les GPIO USB réservés.
Une erreur de configuration ne démarre pas le flux et conserve la maintenance.

Outil Linux, depuis la racine, avec un port **identifié explicitement** et reset
manuel du C3 préalable :

```bash
python3 embedded/esp32/tools/portable_config.py /chemin/config.json
python3 embedded/esp32/tools/rover_config.py /dev/serial/by-id/PORT_IDENTIFIE \
  --portable-file /chemin/config.json
# Après revue : même commande avec --apply-portable.
# Export anonyme : --export-portable /chemin/nouveau-fichier.json
```

Aucun scan/reset/flash, accès réseau ou RUN automatique. Les fichiers sont
validés avant ouverture du port. Pour une nouvelle carte, provisionner le pair
séparément par la procédure privée de PAIRING.md.

## Web Flasher et restauration après installation

Le formulaire portable importe/exporte ce schéma et contrôle la carte, les GPIO,
les conflits et le débit. Ces données ne participent pas à la clé de ROM. Après
installation d’un firmware générique compatible, **Restaurer localement par USB**
ouvre le port choisi par l’utilisateur via Web Serial et réutilise les protocoles
ci-dessus, avec lecture de la configuration courante, validation et confirmation
avant écriture. Il ne flashe pas, ne redémarre pas et ne lance pas le RTCM.

La restauration USB a été testée en simulation de transport (succès S3/C3,
annulation, refus, nonce invalide, délai et nettoyage). Le client Linux est testé
sur PTY. Ce n’est pas une qualification d’ouverture Web Serial, des signaux DTR,
de reset, des octets ROM au boot ou de réénumération sur les cartes réelles.
Les images, approvals et l’hébergement demeurent soumis au
[processus Build & Flash](../../docs/flasher/README.md), sans publication dans ce lot.

## Résultats logiciels du lot

- 43/43 CTests natifs avec ASan/UBSan, incluant la production NVS, l’immutabilité
  des instantanés, la persistance du C3 et la récupération explicite.
- 5 tests Python du schéma, dont comparaison C/Python des broches/valeurs et
  conformité au fichier JSON Schema, exécutés par CTest ; les tests du client
  existant incluent maintenant une application/exportation portable sur PTY.
- 9 tests du vérificateur de firmware public, 6 tests JS du schéma/transport/
  formulaire, et le test WebUI étendu à l’export/import anonyme et à l’aperçu.
- 236 tests générateur/politique CI et 55 tests Python existants conservés.
- Quatre configurations IDF 6.1 compilées et vérifiées : S3, C3 relais, C3 USB
  par défaut et fixture C3 UART par défaut. Pour chacune, deux reconstructions
  produisent les mêmes application, bootloader, table et otadata. Un contrôle
  d’empreinte du jeu de sources a encadré cette validation ; le dernier correctif
  C3 de préférence de canal a été reconstruit/vérifié séparément pour ses deux
  valeurs initiales, sans rejouer les sources S3/relais inchangées.

| Configuration | App / slot (octets) | RAM liée (octets ; pas le heap libre) |
| --- | --- | --- |
| S3 Gateway | 1 458 800 / 3 145 728 | DIRAM 215 004 + IRAM 16 384 |
| C3 USB initial | 702 912 / 1 310 720 | DRAM 111 784 |
| C3 UART initial | 702 944 / 1 310 720 | DRAM 111 784 |
| C3 relais historique | 1 307 872 / 1 310 720 | DRAM 160 534 |

Le premier essai du relais dépassait le slot de 2 432 octets. L’exclusion des
services portables S3 non applicables au relais a rétabli le build, avec une
marge finale de **2 848 octets**, sans changement de partition ni suppression
d’un service existant. Ce profil contraint ne doit pas absorber de nouveaux
services par défaut. Le parser portable ne lui est pas annoncé comme disponible.

Le CRC UG reste unique, les standards C17/C++17 et les exclusions GNSS du rover
sont contrôlés dans les objets liés. Aucun workflow GitHub, manifeste ROS2,
package, ROSIDL ou numéro de version n’a été modifié. La CI distante en cours
n’est pas qualifiée par ces essais locaux ; aucune publication n’est effectuée.
Les mesures de heap/stack en fonctionnement, RF, USB, niveaux électriques,
reboot/hotplug et pinout réel restent des acceptations matérielles distinctes.

Référence locale immuable : `24bcd15235ab9581b220102e4fffa3132b56f1bd`.
Le runner `native/ci.py` a aussi réellement reconstruit le C3 USB depuis l’archive
Git de ce commit, dans un conteneur jetable non-root sans réseau pendant les
builds, ni montage hôte/périphérique. Les deux reconstructions et l’export local
du bundle ont réussi ; ses quatre segments et les onze entrées du ZIP sont
intègres. Le vérificateur JavaScript accepte ses véritables octets dans une VM,
avec une approbation de test uniquement en mémoire. Registre et catalogue publics
restent vides. Aucun transfert Web Serial physique n’a été effectué.

Les huit combinaisons ROS2 ont été générées deux fois depuis ce commit : intégrité
et reproductibilité réussies, inventaires/octets copiés/modes inchangés par rapport
à la référence antérieure. Les treize composants du manifeste sont préservés ;
ce lot n’ajoute aucun payload ESP32 aux distributions ROS2.
