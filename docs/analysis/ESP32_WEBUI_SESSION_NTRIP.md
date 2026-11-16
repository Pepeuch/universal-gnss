# ESP32 — connexion unique et filtrage RTCM par publication

État : logiciel validé, qualification sur S3 installé encore requise. Branche
`fix/esp32-single-login`, base `d3c9e0688bc6756bc0f493397d03c7bd3c2f1b40`.
Installation applicative S3 autorisée et réalisée après la validation logicielle ;
aucun changement de configuration réelle, commit, push ou publication.

## Connexion

L’HTML initial montre uniquement la connexion ; navigation et formulaires métier
sont dans une région masquée. Une connexion normale ouvre le tableau de bord.
Le formulaire de connexion n’est pas répété dans les onglets. Le changement de
mot de passe volontaire reste dans Paramètres. Une installation neuve ou une
migration exige toujours le changement initial avant d’ouvrir la WebUI complète,
sur le même écran de connexion, sans dupliquer le formulaire de changement.

Les sessions restent en RAM et sont révoquées au redémarrage, à la déconnexion
et au changement de mot de passe. L’expiration est glissante après 10 minutes
sans requête authentifiée ; la limite absolue de 30 minutes est retirée. Un
petit GET authentifié `/api/auth/session` participe au rafraîchissement existant,
y compris dans Paramètres, et détecte une session perdue après reboot. Aucun
polling métier avant connexion ou onglet masqué, aucune nouvelle session HTTPS
parallèle. Un 401 nettoie la session, les secrets et affiche la connexion.
La réponse de connexion précise `expires_in_s:600, sliding:true`.

La dérivation de mot de passe à 600000 itérations est inchangée : la latence
connue d’environ 90 secondes reste ouverte. Aucune optimisation cryptographique
ni abaissement du niveau de protection n’est inclus.

## Éditeur NTRIP et préréglages

L’éditeur conserve hôte, port, mountpoint, utilisateur, mot de passe et activation.
Pour les publications sortantes, il ajoute `rtcm_messages` ; TLS et GGA sont sous
Options avancées. Un patch ne concerne que le slot sélectionné. Les cinq sorties
restent indépendantes ; les mots de passe enregistrés ne sont pas renvoyés.

Sur installation neuve seulement, le client entrant propose `crtk.net:2101`,
mountpoint `NEAR` et les identifiants publics `centipede`/`centipede` documentés
par [Centipède](https://docs.centipede.fr/docs/proprietaire/). Les cinq publications
proposent les mêmes paramètres avec mountpoint vide, et la liste demandée :

```text
1004,1005(10),1006(10),1008(10),1012,1019,1020,1033(10),1042,1045,1046,1077,1087,1097,1127,1230(10)
```

Toutes les connexions restent désactivées par défaut. Les paramètres existants ne
sont pas écrasés ; les anciennes banques persistées qui omettent `rtcm_messages`
restent sans filtre, même si les nouveaux préréglages en proposent un. Les cinq
listes sont encodées dans les banques existantes avec checksum, commit et readback.
Le downgrade vers un firmware ignorant ces nouveaux champs n’est pas qualifié.

GGA manuelle et position de référence restent vides. L’envoi GGA est désactivé par
défaut : le client actuel consomme la GGA locale du S3 ; le retour GGA du rover
vers le S3 n’est pas implémenté/qualifié ici. Il est nécessaire pour utiliser
`NEAR` dans le scénario souhaité sans GNSS local ; aucune GGA n’est inventée.

## Politique RTCM effective

`rtcm_filter` héberge une nouvelle politique portable bornée, distincte des
profils hérités fixes à 16 règles. Elle réutilise l’extraction du type RTCM du
composant existant et la validation CRC UG déjà présente dans le publisher.
Pas de duplication du CRC ni modification de l’ancien filtre radio.

Une liste contient au plus 32 types distincts (1..4095), au plus 512 caractères.
Les intervalles sont des secondes entières de 0 à 86400 ; type nu ou intervalle
zéro transmet chaque trame disponible. Liste vide : aucun filtrage, compatibilité
historique. Une liste explicite est une liste autorisée : les autres types sont
ignorés pour cette publication. Doublons, types hors plage, syntaxe invalide et
intervalles négatifs/décimaux/hors plage sont refusés avant persistance, atomiquement.

Chaque worker possède ses règles et horodatages. Son horloge est le temps monotone
local du routage, en nanosecondes, sans conversion depuis l’heure GNSS. L’intervalle
est mesuré entre les débuts des envois réussis de trames complètes. La date n’est
engagée qu’après écriture réussie. Les timestamps restent conservés lors d’une
reconnexion au même hôte/port/mountpoint avec la même liste ; une modification de
cette destination/liste ou un reboot réinitialise la politique. Un retour en arrière
de l’horloge n’accorde pas un nouvel envoi avant le dernier envoi connu.

Une trame trop précoce est ignorée, jamais endormie ni gardée pour être envoyée
plus tard. Le contrôle de fraîcheur de 3 secondes reste inchangé. Aucun effet sur
les autres casters, le caster local ou la sortie ESP-NOW. Aucun message synthétisé.
Le code s’exécute dans les workers réseau existants, hors tâche critique de routage.

Le profil BASE UG Unicore actuel génère `1006,1074,1084,1094,1124,1230` (voir
`core/gnss_driver/src/base_config_profile_builder.cpp`). La liste demandée comprend
MSM7 : elle ne reconfigure pas ce profil MSM4 et ne peut pas produire 1077/1087/1097/
1127 manquants. L’interface l’indique. La production réellement observée sur un
récepteur physique doit être qualifiée séparément ; aucun récepteur n’est raccordé.

## Validation et budgets

- 65 CTest natifs ASan/UBSan : PASS. Sessions actives au-delà de 30 min et jusqu’à
  environ 14 h simulées, expiration inactive, révocation, limite de slots conservées.
- 22 tests JS WebUI : PASS (dont reprise restreinte et consultation NTRIP). Connexion unique, absence de polling avant login,
  erreurs/réponses obsolètes, 401 et nettoyage des secrets.
- Chromium réel avec API simulées, desktop/mobile : PASS. Six onglets, enregistrement
  NTRIP entrant et deux listes sortantes indépendantes sans reconnexion, refus de
  sauvegarde, logout/reboot simulé, parcours initial restreint, une requête à la fois.
- Deux casters TCP loopback simultanés reçoivent et décodent les vrais octets RTCM
  avec CRC vérifié : séquences et compteurs filtrés exacts. Temps des intervalles
  simulé pour éviter 20 secondes d’attente réelle ; transport TCP et octets réels.
- Sauvegarde/rechargement après redémarrage logique du stockage des cinq listes,
  banque ancienne sans champ de filtre et échec de validation : PASS.
- 265 tests générateur/politique : PASS. SDK épinglé 6.1, deux builds S3 indépendants,
  comparaison des quatre segments, vérification et tests host SDK : PASS.

Snapshot compilé `bf1c5b14f7ab056a7c5ec332da49383a55067a8a` (arbre de travail,
pas commit publiable). Application 1486992 octets, SHA-256
`48cbb47e994045648b932ef36c889ec9b42782916a47f68dd19a4a1adb256732`.
Par rapport au firmware publié : +3264 octets Flash. `.dram0.data` : 23757 et
`.dram0.bss` : 83120 octets, inchangés. Assets gzip : 4751 + 14407 + 1258 octets.

Les tailles des types sont confirmées par compilation d’un probe avec l’ABI Xtensa :
`Settings` 2016 → 2136 octets (+120), `NtripPublisher` 1896 → 2448 (+552), politique
RTCM 520 octets. Les cinq piles restent à 8192 octets : leur réservation ne croît
pas, mais leurs objets locaux occupent au total 2760 octets supplémentaires. Les
chaînes persistantes et leurs copies ont aussi un coût heap ; ne pas le confondre
avec la seule taille statique. La marge réelle des piles/heap reste à mesurer sur S3.

La décision ne réalise ni allocation ni attente, au plus 32 comparaisons, puis au
plus 32 pour marquer l’envoi. Le benchmark host ASan/debug mesure environ 189 ns par
recherche au pire cas ; **ce n’est pas une mesure CPU/latence S3**. Les exports de
build et tailles sont conservés hors dépôt sous `/tmp/ug-single-ntrip-artifacts`.
Un premier export de diagnostic avait échoué (bytes/string) après les deux builds ;
il a été corrigé et le contrôle complet a ensuite abouti. Aucun échec n’est masqué.

Restent sur matériel : hautes eaux des cinq piles, RAM interne/bloc maximum, CPU,
latence avec charge et réception GNSS réelle, persistance après reboot physique,
scénario de deux casters contrôlés et parcours opérateur avec son mot de passe.
Aucune connexion de test vers les vrais casters ni changement des identifiants
opérateur ou des réglages installés pendant ce lot.


## Installation matérielle autorisée

Le propriétaire a autorisé l’application seule (SHA ci-dessus), port série libre.
S3 ROM v0.2, MAC STA vérifiée, flash 16 Mio/PSRAM 8 Mio ; tables et sélection OTA
vérifiées avant écriture. L’application active de référence et les régions
NVS/table/OTA sont sauvegardées hors dépôt en fichiers privés. Écriture à `0x20000`
seulement, vérification du binaire et comparaison binaire des régions protégées
avant redémarrage : PASS. Pas d’effacement global ni écriture de configuration.

Après démarrage : pas de panic/overflow, HTTPS `running`, identité TLS prête.
L’HTML réellement servi expose la connexion unique et masque la WebUI avant auth.
Un relevé avant/après sous HTTPS donne interne libre 101135 → 100047 octets,
plus grand bloc 34816 → 34816, PSRAM libre 8331132 → 8331004, pression normale.
Ces snapshots ne sont pas une campagne de charge reproductible ; ne pas les
présenter comme une qualification CPU/routage/deux casters actifs sur S3.

Le mot de passe opérateur est préservé et inconnu de l’agent. Le contrôle du
parcours authentifié réel est demandé au propriétaire, sans partage du secret.
La persistance physique des nouvelles listes et le test de deux casters locaux
exigent une autorisation séparée de configuration temporaire ; ils restent ouverts.


## Complément : consultation du mot de passe NTRIP

À la demande du propriétaire, le champ porte le nom « Mot de passe » et dispose
d’un bouton Afficher/Masquer. Il relit la valeur enregistrée de la seule connexion
choisie via GET authentifié `/api/ntrip/password/upstream` ou `/0` à `/4`.
La configuration/export ordinaire reste expurgée. Les sessions restreintes n’ont
pas accès à cette lecture. Les réponses sont `no-store` ; aucune clé radio ni
mot de passe administrateur n’est rendu par cette API. Aucune valeur n’est placée
dans localStorage/sessionStorage, les journaux ou le résultat d’une sauvegarde.
Les copies temporaires serveur JSON et le tampon de réponse borné (800 octets)
sont effacés avant libération/retour. Le navigateur ne garantit pas l’effacement
physique de ses chaînes immuables ; le champ est vidé lors du changement de cible,
du rechargement, de l’enregistrement ou de la fermeture de session.

Les réponses tardives après modification du champ, changement de connexion ou
logout sont ignorées. Chromium teste l’entrant et les cinq sortants, l’affichage,
le masquage et la conservation des parcours existants. La session initiale
restreinte dispose maintenant d’un contrôle de validité sans données métier,
d’un retour après reboot simulé et d’un bouton d’annulation.

Snapshot complémentaire compilé : `fe9c8de24ebe1337665e9f665eb1fc48891c148f`.
SDK épinglé, deux builds indépendants reproductibles, quatre segments vérifiés,
65 CTest ASan/UBSan, 22 tests JS et trois parcours Chromium : PASS.
Application **1488048 octets**, SHA-256
`b945c25b16871b0e93d03d77813d3cba69d0a0863e87c12d52ce8df341ecd5a2`.
Assets gzip : 4851 + 14807 + 1258 octets. Cette application complémentaire n’est
pas installée : l’accord précédent concernait exactement l’application 48cbb47e…,
qui reste le firmware réellement installé au moment de ce relevé.

Le propriétaire a confirmé sa connexion réelle et fourni une capture de l’onglet
RTCM/NTRIP avec confirmation de sauvegarde. Ce constat ne qualifie ni les cinq
filtres sur le matériel ni les six onglets. Un essai négatif HTTP isolé avec un
mot de passe volontairement invalide reçoit correctement 401 en **88,439 s**,
longueur complète vérifiée ; aucune panne mémoire/serveur n’a été observée.
La latence d’authentification reste excessive et ouverte. Les accès LAN doivent
être effectués depuis le réseau de l’hôte Docker dans cet environnement ; le
conteneur de développement n’a pas de route LAN utilisable au même instant.


## État consolidé pour le gel Git

Les sections précédentes conservent les mesures de chaque image. L’état final,
la connexion opérateur confirmée et les limites encore ouvertes sont récapitulés
dans le [rapport de latence et gel Git](ESP32_LOGIN_LATENCY.md#gel-git-du-travail-validé--16-novembre-2026).
La conservation dans Git ne constitue pas une publication de firmware.
