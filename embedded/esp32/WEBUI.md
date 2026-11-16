# WebUI native ESP32 : navigation et validation

La WebUI reste constituée de trois assets HTML/CSS/JavaScript, compressés en
Flash et servis exclusivement par HTTPS. Aucun framework, CDN ou SVG interactif
de brochage n'est ajouté. La réparation HTTPS est un commit séparé :
`59f53a54c27bca9af6df48956c4259ed439dbb19` ; voir son
[rapport matériel](../../docs/analysis/ESP32_S3_HTTPS_REPAIR.md).

## Navigation

| Onglet | Contenu |
| --- | --- |
| Tableau de bord | Services, Ethernet/HTTPS, GNSS/FIX/RTK observé, compteurs RTCM, client NTRIP, ressources ; rapport brut repliable |
| GNSS / BASE | UART/source RTCM, identification, satellites, position fixe, Survey-In/TIME, sortie BASE, télémétrie, résultats de commandes |
| RTCM / NTRIP | Client entrant, publications SOURCE v1, TLS/GGA, connexions, redémarrage et historique à la demande |
| Réseau & radio | État observé, W5500, activations caster/Wi-Fi STA/AP/ESP-NOW, association locale ; MQTT/LoRa indiqués indisponibles |
| Diagnostics | Interne/PSRAM, services, événements et autotests hors ligne |
| Paramètres | Configuration avancée, certificats NTRIP, import/export portable, prévisualisation et restauration |

Le formulaire cohérent des activations reste dans Réseau & radio, y compris le
caster ; RTCM / NTRIP indique où le configurer. Tous les contrôles précédents
sont conservés. Le profil relais sans GNSS présente cette indisponibilité dans
l'onglet GNSS. Sur un profil GNSS, les réglages restent accessibles sans récepteur
raccordé ; les commandes matérielles exigent toujours une identité vérifiée,
un propriétaire disponible et l'absence de quarantaine. Un ACK, un service
réactif ou un compteur cumulatif ne constituent pas une preuve de RTK FIX.

Les six onglets utilisent les rôles ARIA tab/tablist/tabpanel, les flèches,
Home/End et un focus visible. La navigation et les formulaires s'adaptent aux
petits écrans. Le message d'opération reste visible même lorsque l'administration
est repliée. Les secrets ne sont pas stockés dans le navigateur ; les champs
sensibles sont vidés lors de leur préparation, d'un changement de connexion ou
de session. Cela inclut désormais le PEM en cas d'échec d'envoi.

## Requêtes et fraîcheur

Une seule file couvre toutes les requêtes API, y compris configuration,
provisionnement et diagnostics manuels. Le timeout de 10 secondes commence lors
de l'envoi effectif, puis couvre la lecture de la réponse. Un échec ne bloque pas
la file. Chaque opération capture sa session et son jeton ; une opération encore
en file d'une ancienne session n'est pas envoyée. Une écriture déjà envoyée ne
peut pas être annulée par une déconnexion de l'interface.

Les réponses de rafraîchissement sont liées à la session et à la génération de
l'onglet. Les lectures de configuration et les rapports manuels ont aussi leur
génération. Une saisie plus récente empêche une relecture automatique de réécrire
le formulaire. Le cache ne crée aucune observation physique : l'heure de lecture
API est locale au navigateur, tandis que l'âge/staleness GNSS reste celui fourni
par le firmware. Un ancien RTK FIX n'est pas présenté comme une position actuelle.

Une réponse de ressources inconnue et une perte d'API désactivent les opérations.
Les seuils et protections mémoire du serveur ne changent pas. Sous pression,
les satellites et autotests détaillés restent limités par le contrat existant.
Les formulaires ne sont pas rechargés par le polling.

Après chaque cycle terminé, le prochain est planifié selon l'onglet actif :

| Vue authentifiée, régime établi | GET par cycle | Délai après cycle | Plafond théorique/min |
| --- | --- | --- | --- |
| Ancienne page, GNSS disponible | 4 | 3 s (ancien intervalle) | 80 |
| Tableau de bord | 3 | 5 s | 36 |
| GNSS / BASE | 4 | 3 s | 80 |
| RTCM / NTRIP | 3 | 5 s | 36 |
| Réseau & radio | 2 | 5 s | 24 |
| Diagnostics | 2 | 5 s | 24 |
| Paramètres | 1 | 10 s | 6 |

Ces décomptes proviennent des tests de cycle ; les taux sont des estimations sans
latence, pas des mesures sur le S3. Les capacités générales sont lues au démarrage,
les capacités du récepteur à son changement d'identité. Login et actions manuelles
ajoutent leurs propres requêtes. Sans authentification/récepteur, certains GET
disparaissent. Les historiques sont exclusivement à la demande. Quand le document
est masqué, aucun nouveau polling n'est lancé ; une requête déjà envoyée peut
finir, mais sa réponse ne repeuple pas la vue. Le retour relance un cycle.

## Validation locale de cette refonte

Arbre de sources qualifié avant commit :
`8ce21849e76d71fdf82ce6f732d682f1c1100496` (objet tree local, pas une publication).
Les changements ultérieurs concernent la documentation et le retrait du test
imposant la limite flash du relais C3. Les assets compilés sont inchangés. SDK inchangé : ESP-IDF 6.1,
`espressif/idf@sha256:81893c71bb5e570088901f21def8684c25cd2a9020281bd01b843a7655edb18c`.

| Asset (JavaScript incluant le validateur portable partagé) | Brut avant → après | Gzip avant → après |
| --- | --- | --- |
| HTML | 19 003 → 14 331 | 6 529 → 4 305 |
| JavaScript | 30 059 → 34 724 | 8 555 → 10 112 |
| CSS | 979 → 3 069 | 527 → 1 187 |
| Total servi | 50 041 → 52 124 | **15 611 → 15 604** |

Les trois téléchargements initiaux sont conservés. La compression déterministe
et le packer sont inchangés. Le gain de 7 octets maintient la taille globale,
sans constituer une optimisation significative de flash ou de RAM. Aucun budget
flash du relais C3 n’est imposé à cette refonte S3. Ce profil est reporté à un lot
ultérieur après optimisation ; son code n’est pas supprimé.

- Onze tests QuickJS : contrôles/confirmations et secrets précédents conservés,
  navigation, cadences, file unique, réponse tardive, logout, page masquée,
  erreur de login, saisie récente, imports tardifs, pression inconnue et échec PEM.
- Playwright 1.56.0 / Chromium sur API simulée : Gateway 1280 px, Gateway 390 px et relais
  390 px (capacité sans GNSS simulée, sans qualification C3) ; six onglets, formulaires sans GNSS raccordé, 401, envois acceptés/refusés,
  activation caster, déconnexion API/reprise, événements, logout et absence de
  débordement horizontal. Pic observé : **une requête API active**. Pendant
  5,3 secondes de document déclaré masqué : zéro nouvelle requête.
- S3 Gateway avec GNSS compilé : **45/45 CTest** sous ASan/UBSan, onze tests
  QuickJS WebUI, neuf tests flasheur, six tests portable et huit tests capture.
  Deux reconstructions indépendantes sont identiques. Application : **1 461 552 octets**,
  SHA-256 `ad3d398ab6f6a26828ec279c292fcd1ac9c66a139e806892a390aa33f8d98719`. Pile de démarrage 32 Kio et TLS externe
  vérifiés, sans changement de configuration.
- Tests CI/alpha ESP32 : 17 réussis. Les builds natifs et leurs tests sont
  exécutés sans montages de sources hôtes/périphériques, en UID 1000, réseau coupé
  après provisionnement. Voir aussi les [instructions natives](native/CI.md).

Le test Chromium est permanent mais optionnel dans la validation locale :

```bash
PYTHONDONTWRITEBYTECODE=1 python3 embedded/esp32/native/tests/test_webui_browser.py --artifacts /tmp/ug-webui-previews
```

Il requiert `playwright==1.56.0` avec Chromium disponible (image de test
`mcr.microsoft.com/playwright/python:v1.56.0-noble`). Les assets et API synthétiques
sont interceptés localement sous CSP stricte, sans accès à un appareil réel ni
service externe. Il n'est pas ajouté au conteneur de dépendances du firmware.

## Validation physique et mémoire S3

La nouvelle interface a été flashée sur le Waveshare S3 identifié, application
seule, avec NVS/TLS/configuration/partitionnement conservés. Chromium a vérifié
les six onglets, authentification/déconnexion, formulaires locaux sans écriture,
mobile, refus attendus sous pression et redémarrage HTTPS. Dix cycles de session
et de rafraîchissement ont ensuite été effectués sur la variante PSRAM.

La lecture du service Ethernet utilise désormais `status.services.ethernet_driver`
(identifiant de carte `ethernet` inchangé). Assertion dans les tests QuickJS,
Chromium simulé et sur le S3 réel. Le JavaScript servi passe de 34 724/10 112
à 34 777/10 142 octets brut/gzip ; total gzip 15 634, soit +30 octets,
sans requête ni session HTTPS supplémentaire.

Voir le [rapport mémoire S3](../../docs/analysis/ESP32_S3_MEMORY_QUALIFICATION.md)
pour les quatre firmwares comparés, le protocole commun, les budgets, les défauts
JSON injectés et les limites des cycles. Les relevés de mémoire sont des
échantillons, pas une mesure de chaque pic transitoire.

## Limites et suite

Aucune qualification GNSS/RF/RTK ni d'endurance prolongée en l'absence de récepteur.
Les grandes réponses de télémétrie des fixtures sont synthétiques. Les protections
mémoire restent actives et peuvent refuser les historiques/API détaillées avec 503.
La pression reste `reduced`, le plus grand bloc n'est pas amélioré.

Moteur partagé, ROS2/Linux, NVS, TLS, partitions, seuils et publication restent
inchangés. Les optimisations ultérieures S3 et celles des copies/buffers/timestamps
du core sont des phases distinctes. Le brochage interactif et le relais C3 restent
reportés. Ces essais alpha ne ferment aucun gate de release UG.

## Évolution locale : appairage USB et connexion par mot de passe

La WebUI remplace la saisie du jeton par Identifiant/Mot de passe et propose le
changement du mot de passe dans Paramètres, sans page ni assistant supplémentaire.
Installation réellement vierge : admin/admin autorise seulement ce changement.
Une installation déjà provisionnée accepte son ancien jeton comme mot de passe
de migration pour admin ; aucune réinitialisation NVS/TLS/GNSS automatique.
Le changement validé révoque les sessions et l'usage réseau de l'ancien jeton.
Les API métier conservent le schéma Bearer ; les sessions nouvelles restent en
mémoire seulement, expirent et se ferment explicitement.

Le bouton d'[appairage Rover USB](PAIRING.md) conserve la confirmation et la fenêtre
S3, automatise UGCFG/1 et ne présente jamais une écriture partielle comme une
association complète. Les essais physiques et les coûts d'authentification S3
sont distingués des tests simulés dans le
[rapport dédié](../../docs/analysis/ESP32_WEBUI_PAIRING_AUTH.md).
