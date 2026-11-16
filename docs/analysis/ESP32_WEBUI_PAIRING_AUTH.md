# ESP32 WebUI — appairage Rover USB et authentification

État : **PARTIAL / HARDWARE_REQUIRED avant publication**. Branche locale
`feat/esp32-webui-pairing-auth`, base
`b433f7e6f5947ef54eab287b5a6079f216730b94`. Travail non fusionné ni publié comme firmware ; essais applicatifs locaux S3/C3 ci-dessous.
Les changements appartiennent aux applications ESP32 ; moteur UG partagé, ROS2,
Linux, profils C3 Relay, partitions et seuils mémoire inchangés.

## Contrat d'appairage

Le [parcours utilisateur et le protocole](../../embedded/esp32/PAIRING.md) décrivent
le bouton unique dans Réseau & radio, la permission USB Chromium, les identités
STA croisées, nonce UGCFG/1, LMK WebCrypto 16 octets et canal S3 conservé.
Pas de découverte RF, WebUI supplémentaire, ticket, copie de MAC/clé, secret fixe,
connexion Wi-Fi STA ou changement automatique de canal.

Les nouvelles métadonnées de la maintenance UGCFG identifient C3 Rover UG ; les
anciens firmwares doivent être mis à jour explicitement. Les octets de contrôle
ne partagent jamais le flux RTCM actif. Les contrôles existants de chiffrement,
validation, persistance/readback, fenêtre S3 et quarantaine jusqu'au reboot sont
conservés. L'API de reboot confirme une association enregistrée, respecte la
politique mémoire et refuse un second reboot déjà planifié.

L'écriture C3 précède l'écriture S3. Un résultat de transport perdu après SET est
une écriture **incertaine**, même si le navigateur n'a pas reçu d'acquittement.
Aucune réussite n'est déduite d'une seule écriture. Pas de rollback automatique
qui détruirait une ancienne clé inconnue ; le résultat signale les cartes à
recontrôler. Le bouton devient Annuler, avec timeouts et libération des streams.
Les autres mutations Web locales sont refusées durant l'opération.

Après les deux vérifications, RUN termine la maintenance C3 ; le S3 redémarre.
Ses sessions volatiles sont invalidées par le reboot. Une reconnexion utilisateur
permet de reprendre la vérification du démarrage radio, sans conserver son mot de
passe dans le navigateur. Les MAC/canal de cette vérification restent uniquement
en RAM. Configuration vérifiée et radio démarrée ne prouvent pas une livraison RF,
RTCM, USB ou un FIX GNSS.

## Authentification et migration

- Identifiant `admin`, mot de passe saisi sous HTTPS, sans nouvel écran d'accueil.
- `admin/admin` uniquement quand le stockage de configuration est sain et vierge
  (aucune génération persistée, aucun ancien hash) et le stockage d'authentification
  est absent. Une erreur, un schéma inconnu ou un appareil configuré sans ancien
  hash ne sont jamais assimilés à une installation vierge.
- Cette session initiale ne peut que changer le mot de passe. Les API métier
  refusent son token. Même restriction après connexion de migration avec l'ancien
  jeton d'un appareil déjà provisionné ; aucune identité existante n'est effacée.
- Mot de passe de 8 à 128 octets UTF-8, différent de l'actuel, confirmé dans
  Paramètres et vérifié avec le mot de passe actuel. Le navigateur efface ses
  champs après soumission et ne place aucun mot de passe/session dans un stockage
  persistant. JavaScript ne garantit pas l'effacement physique de ses chaînes.
- PBKDF2-HMAC-SHA256, **600000 itérations**, sel aléatoire 16 octets, résultat 32
  octets. `PasswordKdf` est comparé à OpenSSL ; la plateforme utilise le HMAC/SHA-256 du SDK et
  rend régulièrement le CPU aux tâches existantes. Aucun plaintext en NVS.
- Un seul enregistrement NVS atomique/readback `ug_auth/password`, 56 octets,
  magic/version 1 (600000 itérations fixes), sel et résultat. Changer ces paramètres
  exige une migration de version explicite. Pas de modification des banques GNSS,
  du certificat HTTPS ou des partitions. Protection contre la lecture physique de
  flash inchangée : pas de prétention de chiffrement NVS matériel.
- Les sessions et sels sont tirés du CTR-DRBG du SDK, initialisé une seule fois
  au début de `app_main` S3 avec la source d'entropie non RF temporaire. Elle est
  désactivée avant les périphériques. Aucun réarmement d'entropie n'est fait dans
  une requête HTTPS, avec une radio/ADC active. Un échec de seed ou un besoin de
  reseed ultérieur non satisfait fait échouer la connexion, sans fallback faible.
  Le chemin `mbedtls/private/ctr_drbg.h` est celui exporté par le SDK 6.1/MbedTLS4
  épinglé ; une montée de SDK impose de le revoir. Voir les
  [préconditions d'entropie Espressif S3](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/random.html).
- Quatre sessions bornées en RAM, secrets aléatoires 32 octets, durée absolue 30 min
  et inactivité 10 min, horloge monotone locale. Pas de jeton permanent commun,
  cookie, stockage navigateur ou éviction d'une session valide par une connexion
  supplémentaire. Fermeture et changement de mot de passe révoquent les sessions.
  Une réponse 401 nettoie la session WebUI et empêche les anciennes réponses de la
  repeupler. Les vérifications sensibles comparent les données à temps constant.
- Limitation globale des tentatives de connexion ; réponses d'échec génériques,
  requêtes bornées avant parsing, buffers de mot de passe effacés. Aucun journal
  de clé/mot de passe. Les opérations restent soumises aux seuils mémoire existants.
- Les API métier et le schéma Bearer existants restent en place. L'ancien jeton
  garde sa compatibilité API **jusqu'au changement de mot de passe**, qui désactive
  ce chemin dans le nouveau firmware. Son ancien hash de configuration est préservé
  (pas de reset implicite), mais ne permet plus d'accès réseau dans cette version.
  Un downgrade de firmware ne fournit pas cette nouvelle garantie de révocation.

Le générateur local de commissioning conserve son comportement historique par
 défaut. `commission.py --tls-only` prépare explicitement une identité TLS unique
sans modifier les identifiants ; sur un appareil réellement neuf cela laisse la
connexion restreinte admin/admin. Il ne lit/flashe aucune carte et ne remplace
jamais des fichiers privés existants. Ce n'est pas une instruction d'écraser un
certificat déjà valide.

`auth-benchmark` sur la console locale dérive un vecteur public fixe, contrôle son
résultat et rapporte sa durée, **sans lire ni changer de mot de passe opérateur**.
Il sert à mesurer le coût réel S3 ; aucune estimation hôte ne le remplace. Le coût
CPU/latence est mesuré sur S3 ci-dessous et reste excessif. La réactivité HTTPS
des autres clients pendant la dérivation reste à qualifier.

## Vérifications et preuves

- 21 scénarios Chromium simulés : nominal, annulation avant écriture, ouverture
  refusée, nonce périmé, mauvaise carte/rôle, stockage verrouillé, interruption,
  perte de réponse SET, timeout, readback incohérent, refus S3, états croisés
  différents, Wi-Fi STA actif, fenêtre S3 refusée, sortie UART/portable invalide, refus RUN/reboot, nettoyage USB et annulation après écriture S3.
  Aucune clé retournée dans les messages de progression ou d'erreur.
- 16 tests JavaScript WebUI (QuickJS) : anciens contrats conservés, connexion
  restreinte, confirmation/effacement des mots de passe et navigateur incompatible.
- Chromium réel avec API simulées : six onglets, desktop/mobile, authentification,
  formulaires, refus, déconnexion, pause masquée, au plus une requête API active.
- Tests C++ de sessions/migration : mauvais identifiant/mot de passe, limitation,
  refus des opérations sensibles initiales, changement et révocation, redémarrage
  logique, expiration absolue/idle, horloge invalide, erreur NVS, échecs RNG/KDF,
  readback refusé et capacité des sessions.
- Le même KDF de production est comparé au PBKDF2 OpenSSL à 600000 itérations ;
  échec HMAC intermédiaire et effacement du résultat vérifiés.
- Tests commissioning TLS-only/legacy : pas de reset d'identifiants, inventaire
  privé attendu, modes 0700/0600 et refus d'écrasement des fichiers.
- Le vérificateur des assets doit inclure le nouveau module de pairing avant
  `app.js`. Sa première vérification a refusé cette divergence ; l'attente et son
  test synthétique sont adaptés. Le rejet des assets absents, périmés ou dupliqués
  demeure actif.

## Builds et budgets finaux

SDK 6.1 épinglé :
`espressif/idf@sha256:81893c71bb5e570088901f21def8684c25cd2a9020281bd01b843a7655edb18c`.
Deux builds S3 indépendants, comparaison des quatre segments, vérification des
objets/CRC UG, C17/C++17 et protections mémoire : PASS. Snapshot logiciel
`6fca80deeb4d5a0dd6fdb9ddb131c5f2e6b0a968` ; seuls les rapports évoluent ensuite.
63 CTest ASan/UBSan, 16 tests JavaScript, 9 flasher, 6 portable et 8 capture : PASS.
242 tests générateur/politique CI : PASS. Chromium desktop/mobile couvre aussi le
bouton réel avec des streams série simulés et la comparaison des deux clés
synthétiques, sans exposition dans le DOM. Ces essais ne sont pas du RF.

| Mesure | Référence b433f7e | Candidat final |
| --- | ---: | ---: |
| Application S3 | 1471296 octets | 1483728 octets |
| `.dram0.data` | 23757 | 23757 |
| `.dram0.bss` | 82256 | 83120 |
| Trois assets gzip | 15634 | 19846 |

Le coût statique interne net est **+864 octets** pour l'appairage/authentification,
dont objet de sessions 536 et DRBG 328 octets (les alignements/flags font partie
du total ELF). L'adaptateur HMAC sélectionne l'implémentation logicielle existante
du SDK pour cette seule dérivation ; TLS conserve son moteur/configuration. Le
contexte et les pads restent possédés/libérés par `mbedtls_md_free`. Les champs
privés MD et déclarations HMAC demandent une revue à toute montée de SDK.

C3 Rover : deux builds comparés et vérifiés depuis
`9edca60d5cf9bf694a0e597ed4806b1790ddb58a`. Les entrées de production Rover sont
identiques aux snapshots finaux : preuve réutilisée, pas de nouvelle campagne
C3 Relay. Application 702976 octets, sortie USB :
`d0de2b8908a12007a581d62ab55d5aeff8a819d625c3ccafab023cd781a256b9`.
Une copie locale de résultats avait échoué après validation à cause de modes
lecture seule ; le binaire et le rapport de vérification ont été récupérés et
contrôlés. Ce problème d'export n'est pas présenté comme un test de compilation.

## Vérifications matérielles réellement réalisées

Le blocage initial (port C3 occupé) a été levé par le propriétaire. L'ancien accès
S3 était indisponible alors que le S3 restait en téléchargement ROM ; la séquence
USB-JTAG qualifiée a rétabli le démarrage applicatif. Le simple reset RTS de la
CLI ne suffisait pas et n'est pas compté comme démarrage réussi.

- S3 Waveshare W5500, révision 0.2, flash 16 Mio/PSRAM 8 Mio ; C3 SuperMini,
  révision 0.4, flash 4 Mio. Identités ROM et MAC réelles vérifiées.
- Sauvegarde C3 complète 4 Mio, sauvegarde S3 du slot actif 3 Mio et des régions
  NVS/table/OTA, copiées/vérifiées hors dépôt avec modes privés.
- Tables compatibles avec les builds ; séquences OTA valides/CRC vérifiés,
  slot actif `ota_0` sur les deux cartes. Bootloader C3 identique au SDK épinglé.
- Applications seules écrites à `0x20000` (S3) et `0x10000` (C3), lectures de
  contrôle. Pas d'effacement global, table, bootloader ou sélecteur OTA réécrits.
  Comparaison binaire NVS/table/OTA avant/après chaque écriture applicative : PASS.
- Application S3 finale :
  `a87ff178abf66e00fe62da7830bfa7df8696e6236d7e6b904bf873ffebb09dbc`.
  Console, TLS prête, HTTPS `running` et identité administrateur conservées.
- C3 réel : OPEN avec nonce, identité UG/C3/Rover, stockage modifiable et sortie
  portable USB vérifiés. C3 non appairé ; aucune commande SET/CLEAR, clé ou pair
  provisionnés par cet essai.
- Sur l'installation existante, admin/admin a été refusé, l'ancien jeton a donné
  une session restreinte, configuration sensible/fenêtre refusées, mauvais mot de
  passe actuel refusé et fermeture de session vérifiée.
- Ensuite, sur **demande explicite du propriétaire**, le minimum est passé à
  8 caractères et son mot de passe choisi a été enregistré via le chemin
  authentifié existant. Aucune valeur de ce mot de passe n'est dans ce rapport,
  le firmware, les tests ou un fichier de mot de passe généré.
- Connexion complète avec ce mot de passe : PASS. Ancien Bearer refusé : PASS.
  Après le redémarrage applicatif suivant, nouvelle connexion complète : PASS.
  Configuration GNSS/NTRIP/réseau/caster préservée. L'écriture autorisée du mot de
  passe modifie `ug_auth` : aucune identité binaire NVS globale n'est prétendue
  après cette opération. NVS commit/readback et persistance sont vérifiés.

## Coût CPU encore ouvert

Même vecteur public PBKDF2-HMAC-SHA256, sel de 16 octets, **600000 itérations**,
résultat comparé au vecteur OpenSSL indépendant sur le véritable S3 :

| Implémentation mesurée | Durée |
| --- | ---: |
| Opération PSA reconstruite par HMAC | 137,083 s |
| Contexte HMAC réutilisé, moteur PSA | 99,825 s |
| Contexte HMAC réutilisé, SHA logiciel local | 86,221 s |

Le premier résultat dépassait le timeout Web de 120 s. Les adaptations du backend
le ramènent sous ce timeout mais **la latence reste excessive** : connexion complète
après reboot mesurée à **89,094 s**. Ce résultat n'est pas un PASS de performance.
Les itérations, seuils mémoire, piles et priorités n'ont pas été réduits pour
obtenir un résultat vert. Une conception de preuve côté navigateur avec un
protocole standard doit être évaluée séparément avant publication ; aucun nouveau
protocole de preuve ou cache rapide de mot de passe n'est implémenté ici.

Un échantillon après connexion : interne libre 100939, plus grand bloc 34816,
PSRAM libre 8331132 octets, pression normale. C'est un échantillon à un client,
pas une qualification de trois connexions pendant la dérivation. La RAM dynamique,
les cycles de connexion et la latence GNSS/RTCM sous charge restent à qualifier.

## Acceptation restante

**NO-GO pour publication globale** tant que la latence d'authentification et
l'appairage complet via le vrai navigateur/Web Serial ne sont pas qualifiés.
Le compte demandé fonctionne et persiste sur le matériel ; le C3 est prêt pour
le parcours USB. Aucun couple ni LMK fictif n'ont été créés sur les cartes.

Restent : permission réelle du port dans Chromium, double écriture et résultat
croisé sur les deux cartes, reboot S3, retour Web après reconnexion, tests réels
d'annulation/débranchement, mesures 1/2/3 clients et coût de la radio active.
Pas de preuve CCMP/RTCM sur l'air, GNSS/RTK, portée ou endurance ajoutée ici.
C6 et C3 Relay hors périmètre. La sauvegarde Git de cette branche ne vaut ni
qualification complète ni publication du firmware. Aucune fusion ou Release.

## Inventaire du chantier

- `ROADMAP.md`
- `TODO.md`
- `docs/analysis/ESP32_WEBUI_PAIRING_AUTH.md`
- `embedded/esp32/PAIRING.md`
- `embedded/esp32/README.md`
- `embedded/esp32/WEBUI.md`
- `embedded/esp32/components/ug_platform/CMakeLists.txt`
- `embedded/esp32/components/ug_platform/include/ug_web_auth_platform.hpp`
- `embedded/esp32/components/ug_platform/src/ug_commission.cpp`
- `embedded/esp32/components/ug_platform/src/ug_web_auth_platform.cpp`
- `embedded/esp32/components/ug_settings/CMakeLists.txt`
- `embedded/esp32/components/ug_settings/include/ug_password_kdf.hpp`
- `embedded/esp32/components/ug_settings/include/ug_web_auth.hpp`
- `embedded/esp32/components/ug_settings/src/ug_password_kdf.cpp`
- `embedded/esp32/components/ug_settings/src/ug_web_auth.cpp`
- `embedded/esp32/components/ug_web/CMakeLists.txt`
- `embedded/esp32/components/ug_web/assets/app.js`
- `embedded/esp32/components/ug_web/assets/index.html`
- `embedded/esp32/components/ug_web/assets/pairing.js`
- `embedded/esp32/components/ug_web/src/ug_web.cpp`
- `embedded/esp32/components/ug_web/tools/pack_assets.py`
- `embedded/esp32/native/main/gateway.cpp`
- `embedded/esp32/native/main/rover_config.c`
- `embedded/esp32/native/tests/CMakeLists.txt`
- `embedded/esp32/native/tests/README.md`
- `embedded/esp32/native/tests/test_commission.py`
- `embedded/esp32/native/tests/test_native_build_contract.py`
- `embedded/esp32/native/tests/test_pairing_browser.py`
- `embedded/esp32/native/tests/test_password_kdf.cpp`
- `embedded/esp32/native/tests/test_rover_control.c`
- `embedded/esp32/native/tests/test_web_assets.py`
- `embedded/esp32/native/tests/test_web_auth.cpp`
- `embedded/esp32/native/tests/test_webui_browser.py`
- `embedded/esp32/native/tools/commission.py`
- `embedded/esp32/native/verify.py`
